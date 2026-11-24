/* NCR 5380 / 53C80 SCSI controller and the SCSI bus, for EmuBuddy.
 *
 * The chip is always the initiator. The targets answer at once: a target
 * raises REQ as soon as it has the next byte ready, so the firmware never
 * waits. Registers (NCR 5380 data sheet):
 *
 *   reg  read                              write
 *   0    current SCSI data                 output data
 *   1    initiator command                 initiator command
 *   2    mode                              mode
 *   3    target command                    target command
 *   4    current SCSI bus status           select enable
 *   5    bus and status                    start DMA send
 *   6    input data                        start DMA target receive
 *   7    reset parity/interrupts           start DMA initiator receive
 *
 * Bus phases, as the MSG, C/D and I/O lines (the target command register's
 * bits 2-0): data out 0, data in 1, command 2, status 3, message out 6,
 * message in 7. */

#include "scsi.h"
#include <stdio.h>
#include <string.h>

int	g_scsi_trace = 0;	/* 1: print every command and its result */

/* initiator command register */
#define ICR_RST		0x80
#define ICR_AIP		0x40	/* read: arbitration in progress */
#define ICR_LA		0x20	/* read: lost arbitration */
#define ICR_ACK		0x10
#define ICR_BSY		0x08
#define ICR_SEL		0x04
#define ICR_ATN		0x02
#define ICR_DBUS	0x01
/* mode register */
#define MR_ARB		0x01
#define MR_DMA		0x02
#define MR_BSY_MON	0x04
#define MR_EOP_INT	0x08
/* current SCSI bus status */
#define CSBS_RST	0x80
#define CSBS_BSY	0x40
#define CSBS_REQ	0x20
#define CSBS_SEL	0x02
/* bus and status */
#define BSR_EOD		0x80
#define BSR_DRQ		0x40
#define BSR_IRQ		0x10
#define BSR_MATCH	0x08
#define BSR_BSYERR	0x04
#define BSR_ATN		0x02
#define BSR_ACK		0x01

#define PH_DATA_OUT	0
#define PH_DATA_IN	1
#define PH_COMMAND	2
#define PH_STATUS	3
#define PH_MSG_OUT	6
#define PH_MSG_IN	7

enum { T_FREE, T_SELECTED, T_MSG_OUT, T_COMMAND, T_DATA_IN, T_DATA_OUT,
						T_STATUS, T_MSG_IN };

enum { DMA_NONE, DMA_SEND, DMA_RECV };

static struct {
	int		own_id;
	Scsi_dev	*dev[SCSI_MAX_ID];
	/* chip registers */
	unsigned	odr, icr, mr, tcr;
	int		irq, bsy_err, dma, eod;
	/* the target in charge of the bus */
	int		state, id, lun;
	int		req, phase;
	unsigned	data;		/* what the target drives in input phases */
	int		ack_pending;	/* ACK seen, waiting for its release */
	unsigned char	cdb[16];
	int		cdb_pos, cdb_len;
	unsigned	pos;		/* bytes moved in the data phase */
	unsigned char	chunk[SCSI_CHUNK_MAX];
	unsigned	chunk_pos, chunk_len;
} g_ncr;

int
scsi_sense(Scsi_dev *d, int key, int asc, int ascq)
{
	d->sense_key = key;
	d->asc = asc;
	d->ascq = ascq;
	d->status = (key == SENSE_NONE) ? SCSI_GOOD : SCSI_CHECK_CONDITION;
	return -1;
}

int
scsi_cdb_len(unsigned char op)
{
	switch(op >> 5) {
	case 0: return 6;
	case 1: case 2: return 10;
	case 5: return 12;
	case 6: return 10;	/* vendor: the AppleCD SC's 0xC0-0xCE */
	}
	return 6;		/* groups 3, 4, 7: take 6 */
}

void
ncr5380_init(int own_id)
{
	memset(&g_ncr, 0, sizeof(g_ncr));
	g_ncr.own_id = own_id & 7;
	g_ncr.id = -1;
}

static void
bus_free(void)
{
	g_ncr.state = T_FREE;
	g_ncr.id = -1;
	g_ncr.req = 0;
	g_ncr.phase = 0;
	g_ncr.ack_pending = 0;
	if(g_ncr.mr & MR_BSY_MON) {		/* BSY went away */
		g_ncr.bsy_err = 1;
		g_ncr.irq = 1;
	}
}

void
ncr5380_reset(void)
{
	g_ncr.odr = g_ncr.icr = g_ncr.mr = g_ncr.tcr = 0;
	g_ncr.irq = g_ncr.bsy_err = g_ncr.eod = 0;
	g_ncr.dma = DMA_NONE;
	g_ncr.state = T_FREE;
	g_ncr.id = -1;
	g_ncr.req = 0;
	g_ncr.phase = 0;
	g_ncr.ack_pending = 0;
}

void
ncr5380_attach(int id, Scsi_dev *d)
{
	if(id >= 0 && id < SCSI_MAX_ID && id != g_ncr.own_id) {
		g_ncr.dev[id] = d;
	}
}

int
ncr5380_busy_id(void)
{
	return g_ncr.state == T_FREE ? -1 : g_ncr.id;
}

static Scsi_dev *
cur_dev(void)
{
	return (g_ncr.id >= 0) ? g_ncr.dev[g_ncr.id] : 0;
}

static int
phase_match(void)
{
	return (g_ncr.tcr & 7) == (unsigned)g_ncr.phase;
}

static void
go_phase(int state, int phase, unsigned data)
{
	g_ncr.state = state;
	g_ncr.phase = phase;
	g_ncr.data = data & 0xff;
	g_ncr.req = 1;
	/* in DMA mode, a phase the firmware did not ask for interrupts */
	if((g_ncr.mr & MR_DMA) && g_ncr.dma != DMA_NONE && !phase_match()) {
		g_ncr.irq = 1;
	}
}

static void
go_status(void)
{
	go_phase(T_STATUS, PH_STATUS, cur_dev()->status);
}

/* Loads the next chunk of a data-in transfer. 0 ok, -1 the device failed. */
static int
load_chunk(Scsi_dev *d)
{
	unsigned n;

	n = d->xfer_len - g_ncr.pos;
	if(n > d->xfer_chunk) {
		n = d->xfer_chunk;
	}
	if(n > SCSI_CHUNK_MAX) {
		n = SCSI_CHUNK_MAX;
	}
	g_ncr.chunk_pos = 0;
	g_ncr.chunk_len = n;
	return d->data_in(d, g_ncr.pos, g_ncr.chunk, n);
}

static unsigned
chunk_size(Scsi_dev *d)
{
	unsigned n = d->xfer_chunk;

	if(n == 0 || n > SCSI_CHUNK_MAX) {
		n = SCSI_CHUNK_MAX;
	}
	return n;
}

/* A whole command block arrived */
static void
run_command(void)
{
	Scsi_dev *d = cur_dev();

	d->xfer_dir = SCSI_DIR_NONE;
	d->xfer_len = 0;
	d->xfer_chunk = 512;
	d->status = SCSI_GOOD;
	d->start(d, g_ncr.cdb, g_ncr.lun);
	if(g_scsi_trace) {
		int	i;

		printf("scsi: id %d lun %d cmd", g_ncr.id, g_ncr.lun);
		for(i = 0; i < g_ncr.cdb_len; i++) {
			printf(" %02x", g_ncr.cdb[i]);
		}
		printf(" -> %s %u bytes, status %02x",
			d->xfer_dir == SCSI_DIR_IN ? "in" :
			d->xfer_dir == SCSI_DIR_OUT ? "out" : "no data",
			d->xfer_len, d->status);
		if(d->status) {
			printf(" (sense %x/%02x/%02x)", d->sense_key, d->asc,
								d->ascq);
		}
		printf("\n");
	}
	g_ncr.pos = 0;
	g_ncr.chunk_pos = 0;
	g_ncr.chunk_len = 0;
	if(d->xfer_dir == SCSI_DIR_IN && d->xfer_len) {
		if(load_chunk(d) != 0) {
			go_status();
			return;
		}
		if(g_scsi_trace && d->xfer_len <= 64) {
			unsigned i;

			printf("scsi:   data in:");
			for(i = 0; i < d->xfer_len; i++) {
				printf(" %02x", g_ncr.chunk[i]);
			}
			printf("\n");
		}
		go_phase(T_DATA_IN, PH_DATA_IN, g_ncr.chunk[0]);
	} else if(d->xfer_dir == SCSI_DIR_OUT && d->xfer_len) {
		g_ncr.chunk_len = chunk_size(d);
		if(g_ncr.chunk_len > d->xfer_len) {
			g_ncr.chunk_len = d->xfer_len;
		}
		go_phase(T_DATA_OUT, PH_DATA_OUT, 0);
	} else {
		go_status();
	}
}

/* The byte of the current REQ/ACK handshake is done (ACK released): the
 * target takes it (output phases) and offers the next one. */
static void
byte_done(unsigned out)
{
	Scsi_dev *d = cur_dev();
	unsigned n;

	g_ncr.req = 0;
	switch(g_ncr.state) {
	case T_MSG_OUT:
		if((out & 0x80) != 0) {			/* IDENTIFY */
			g_ncr.lun = out & 7;
		} else if(out == 0x06 || out == 0x0c) {	/* ABORT, RESET */
			bus_free();
			return;
		}
		if(g_ncr.icr & ICR_ATN) {
			go_phase(T_MSG_OUT, PH_MSG_OUT, 0);
		} else {
			g_ncr.cdb_pos = 0;
			go_phase(T_COMMAND, PH_COMMAND, 0);
		}
		break;
	case T_COMMAND:
		if(g_ncr.cdb_pos == 0) {
			g_ncr.cdb_len = scsi_cdb_len((unsigned char)out);
			if(g_ncr.lun == 0) {
				g_ncr.lun = -1;		/* take it from the CDB */
			}
		}
		g_ncr.cdb[g_ncr.cdb_pos++] = (unsigned char)out;
		if(g_ncr.cdb_pos < g_ncr.cdb_len) {
			go_phase(T_COMMAND, PH_COMMAND, 0);
			break;
		}
		if(g_ncr.lun < 0) {
			g_ncr.lun = (g_ncr.cdb[1] >> 5) & 7;
		}
		run_command();
		break;
	case T_DATA_IN:
		g_ncr.pos++;
		g_ncr.chunk_pos++;
		if(g_ncr.pos >= d->xfer_len) {
			go_status();
			break;
		}
		if(g_ncr.chunk_pos >= g_ncr.chunk_len && load_chunk(d) != 0) {
			go_status();
			break;
		}
		go_phase(T_DATA_IN, PH_DATA_IN, g_ncr.chunk[g_ncr.chunk_pos]);
		break;
	case T_DATA_OUT:
		g_ncr.chunk[g_ncr.chunk_pos++] = (unsigned char)out;
		g_ncr.pos++;
		if(g_ncr.chunk_pos >= g_ncr.chunk_len) {
			if(g_scsi_trace && d->xfer_len <= 64) {
				unsigned i;

				printf("scsi:   data out:");
				for(i = 0; i < g_ncr.chunk_pos; i++) {
					printf(" %02x", g_ncr.chunk[i]);
				}
				printf("\n");
			}
			if(d->data_out(d, g_ncr.pos - g_ncr.chunk_pos,
					g_ncr.chunk, g_ncr.chunk_pos) != 0) {
				go_status();
				break;
			}
			g_ncr.chunk_pos = 0;
			n = d->xfer_len - g_ncr.pos;
			g_ncr.chunk_len = chunk_size(d);
			if(g_ncr.chunk_len > n) {
				g_ncr.chunk_len = n;
			}
		}
		if(g_ncr.pos >= d->xfer_len) {
			go_status();
			break;
		}
		go_phase(T_DATA_OUT, PH_DATA_OUT, 0);
		break;
	case T_STATUS:
		go_phase(T_MSG_IN, PH_MSG_IN, 0x00);	/* COMMAND COMPLETE */
		break;
	case T_MSG_IN:
		bus_free();
		break;
	}
}

/* The value the target takes in an output phase */
static unsigned
out_byte(void)
{
	return g_ncr.odr & 0xff;
}

/* Selection and the start of the information phases */
static void
eval_bus(void)
{
	unsigned ids;
	int	i;

	if(g_ncr.state == T_FREE && (g_ncr.icr & ICR_SEL) &&
			!(g_ncr.icr & ICR_BSY) && (g_ncr.icr & ICR_DBUS)) {
		ids = g_ncr.odr & ~(1U << g_ncr.own_id) & 0xff;
		for(i = 0; i < SCSI_MAX_ID; i++) {
			if((ids & (1U << i)) && g_ncr.dev[i] &&
				(!g_ncr.dev[i]->present ||
				g_ncr.dev[i]->present(g_ncr.dev[i]))) {
				g_ncr.state = T_SELECTED;
				g_ncr.id = i;
				g_ncr.lun = 0;
				g_ncr.req = 0;
				break;
			}
		}
	}
	if(g_ncr.state == T_SELECTED && !(g_ncr.icr & ICR_SEL)) {
		if(g_ncr.icr & ICR_ATN) {
			go_phase(T_MSG_OUT, PH_MSG_OUT, 0);
		} else {
			g_ncr.cdb_pos = 0;
			go_phase(T_COMMAND, PH_COMMAND, 0);
		}
	}
}

static int
bsy(void)
{
	return (g_ncr.icr & ICR_BSY) || g_ncr.state != T_FREE;
}

unsigned
ncr5380_read(int reg)
{
	unsigned v;

	switch(reg & 7) {
	case 0:		/* current SCSI data */
		v = 0;
		if((g_ncr.icr & ICR_DBUS) || (g_ncr.mr & MR_ARB)) {
			v |= g_ncr.odr;
		}
		if(g_ncr.state != T_FREE && (g_ncr.phase & 1)) {
			v |= g_ncr.data;	/* input phase */
		}
		return v & 0xff;
	case 1:
		v = g_ncr.icr & (ICR_RST | ICR_ACK | ICR_BSY | ICR_SEL |
							ICR_ATN | ICR_DBUS);
		if((g_ncr.mr & MR_ARB) && g_ncr.state == T_FREE) {
			v |= ICR_AIP;		/* won at once: no one else */
		}
		return v;
	case 2:
		return g_ncr.mr;
	case 3:
		return g_ncr.tcr & 0x0f;
	case 4:		/* current SCSI bus status */
		v = 0;
		if(g_ncr.icr & ICR_RST) {
			v |= CSBS_RST;
		}
		if(bsy()) {
			v |= CSBS_BSY;
		}
		if(g_ncr.icr & ICR_SEL) {
			v |= CSBS_SEL;
		}
		if(g_ncr.state != T_FREE && g_ncr.state != T_SELECTED) {
			if(g_ncr.req) {
				v |= CSBS_REQ;
			}
			v |= g_ncr.phase << 2;
		}
		return v;
	case 5:		/* bus and status */
		v = 0;
		if(g_ncr.eod) {
			v |= BSR_EOD;
		}
		if(ncr5380_drq()) {
			v |= BSR_DRQ;
		}
		if(g_ncr.irq) {
			v |= BSR_IRQ;
		}
		if(g_ncr.state == T_FREE || g_ncr.state == T_SELECTED ||
								phase_match()) {
			v |= BSR_MATCH;
		}
		if(g_ncr.bsy_err) {
			v |= BSR_BSYERR;
		}
		if(g_ncr.icr & ICR_ATN) {
			v |= BSR_ATN;
		}
		if(g_ncr.icr & ICR_ACK) {
			v |= BSR_ACK;
		}
		return v;
	case 6:		/* input data */
		return g_ncr.data & 0xff;
	case 7:		/* reset parity/interrupt */
		g_ncr.irq = 0;
		g_ncr.bsy_err = 0;
		return 0xff;
	}
	return 0xff;
}

void
ncr5380_write(int reg, unsigned val)
{
	unsigned old;

	val &= 0xff;
	switch(reg & 7) {
	case 0:
		g_ncr.odr = val;
		break;
	case 1:
		old = g_ncr.icr;
		g_ncr.icr = val & (ICR_RST | ICR_ACK | ICR_BSY | ICR_SEL |
							ICR_ATN | ICR_DBUS);
		if((val & ICR_RST) && !(old & ICR_RST)) {	/* bus reset */
			g_ncr.mr = 0;
			g_ncr.dma = DMA_NONE;
			g_ncr.state = T_FREE;
			g_ncr.id = -1;
			g_ncr.req = 0;
			g_ncr.ack_pending = 0;
			g_ncr.irq = 1;
			return;
		}
		/* Also with the DMA mode bit set: Apple's firmware leaves it
		 * on while it takes the status byte by hand */
		if((val & ICR_ACK) && !(old & ICR_ACK) && g_ncr.req) {
			g_ncr.req = 0;		/* target: byte taken */
			g_ncr.ack_pending = 1;
			if(!(g_ncr.phase & 1)) {
				g_ncr.data = out_byte();
			}
		}
		if(!(val & ICR_ACK) && (old & ICR_ACK) && g_ncr.ack_pending) {
			g_ncr.ack_pending = 0;
			byte_done(g_ncr.data);
		}
		break;
	case 2:
		g_ncr.mr = val;
		if(!(val & MR_DMA)) {
			g_ncr.dma = DMA_NONE;
			g_ncr.eod = 0;
		}
		if(!(val & MR_BSY_MON)) {
			g_ncr.bsy_err = 0;
		}
		break;
	case 3:
		g_ncr.tcr = val & 0x0f;
		break;
	case 4:		/* select enable: no reselection here */
		break;
	case 5:
		g_ncr.dma = DMA_SEND;
		g_ncr.eod = 0;
		break;
	case 6:		/* target mode: not used by an initiator */
		break;
	case 7:
		g_ncr.dma = DMA_RECV;
		g_ncr.eod = 0;
		break;
	}
	eval_bus();
}

/* The card's DMA counter ran out (the EOP pin): the chip stops asking for
 * bytes and says so, with an interrupt if the mode register asks for one. */
void
ncr5380_eop(void)
{
	g_ncr.eod = 1;
	if(g_ncr.mr & MR_EOP_INT) {
		g_ncr.irq = 1;
	}
}

int
ncr5380_drq(void)
{
	if(!(g_ncr.mr & MR_DMA) || g_ncr.dma == DMA_NONE || !g_ncr.req ||
								g_ncr.eod) {
		return 0;
	}
	if(!phase_match()) {
		return 0;
	}
	if(g_ncr.dma == DMA_RECV) {
		return (g_ncr.phase & 1) != 0;
	}
	return (g_ncr.phase & 1) == 0;
}

int
ncr5380_irq(void)
{
	return g_ncr.irq;
}

unsigned
ncr5380_dma_read(void)
{
	unsigned v;

	if(!ncr5380_drq() || g_ncr.dma != DMA_RECV) {
		return g_ncr.data & 0xff;
	}
	v = g_ncr.data & 0xff;
	byte_done(v);			/* the chip does REQ/ACK by itself */
	return v;
}

void
ncr5380_dma_write(unsigned val)
{
	g_ncr.odr = val & 0xff;
	if(!ncr5380_drq() || g_ncr.dma != DMA_SEND) {
		return;
	}
	byte_done(val & 0xff);
}
