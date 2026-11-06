/* Uthernet II (WIZnet W5100) emulation for EmuBuddy. See uthernet2.h.
 *
 * W5100 memory map (32 KB address space):
 *   0x0000-0x002F  common registers (MR, gateway, subnet, MAC, IP, IR, ...)
 *   0x0400-0x07FF  socket registers, 0x100 per socket
 *   0x4000-0x5FFF  transmit buffer (8 KB, split between sockets by TMSR)
 *   0x6000-0x7FFF  receive buffer  (8 KB, split between sockets by RMSR)
 *
 * Supported: indirect bus mode with auto-increment, software reset, buffer
 * size registers, MACRAW on socket 0 (send/receive raw frames with the
 * optional MAC filter). TCP/UDP/IPRAW hardware sockets are not emulated yet:
 * opening one is logged loudly so we can see which mode a driver uses. */
#include "uthernet2.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MR_RST		0x80
#define MR_AI		0x02
#define MR_IND		0x01

/* common registers */
#define R_SHAR		0x0009		/* MAC address, 6 bytes */
#define R_IR		0x0015
#define R_RMSR		0x001A
#define R_TMSR		0x001B

/* socket register offsets */
#define S_MR		0x00
#define S_CR		0x01
#define S_IR		0x02
#define S_SR		0x03
#define S_TX_FSR	0x20
#define S_TX_RD		0x22
#define S_TX_WR		0x24
#define S_RX_RSR	0x26
#define S_RX_RD		0x28

/* Sn_MR protocols */
#define P_CLOSED	0
#define P_TCP		1
#define P_UDP		2
#define P_IPRAW		3
#define P_MACRAW	4
#define MR_MF		0x40		/* MAC filter (MACRAW) */

/* Sn_CR commands */
#define C_OPEN		0x01
#define C_LISTEN	0x02
#define C_CONNECT	0x04
#define C_DISCON	0x08
#define C_CLOSE		0x10
#define C_SEND		0x20
#define C_SEND_MAC	0x21
#define C_SEND_KEEP	0x22
#define C_RECV		0x40

/* Sn_SR status */
#define SR_CLOSED	0x00
#define SR_INIT		0x13
#define SR_UDP		0x22
#define SR_IPRAW	0x32
#define SR_MACRAW	0x42

/* Sn_IR bits */
#define IR_RECV		0x04
#define IR_SEND_OK	0x10

#define TX_BASE		0x4000
#define RX_BASE		0x6000

static uint8_t	g_mem[0x8000];
static uint8_t	g_mode;
static uint16_t	g_addr;

typedef struct {
	uint16_t	tx_base, tx_size;	/* in g_mem, size 0 = none */
	uint16_t	rx_base, rx_size;
	uint16_t	rx_wr;			/* internal receive write pointer */
	uint16_t	rx_rsr;			/* received size not yet RECV'd */
} Sock;
static Sock	g_sock[4];

/* Frames from the network waiting for room in the receive buffer. */
#define QN	64
#define QMAX	1600
static uint8_t	g_q[QN][QMAX];
static int	g_qlen[QN];
static int	g_qhead, g_qcount;

static unsigned	g_tx_count, g_rx_count;
static int	g_net_up;

static uint16_t
get16(int a)
{
	return (uint16_t)((g_mem[a] << 8) | g_mem[a + 1]);
}

static void
put16(int a, uint16_t v)
{
	g_mem[a] = (uint8_t)(v >> 8);
	g_mem[a + 1] = (uint8_t)v;
}

static int
sreg(int s, int off)
{
	return 0x0400 + s * 0x100 + off;
}

static void
compute_buffers(void)
{
	int	s, tx = 0, rx = 0, sz;

	for(s = 0; s < 4; s++) {
		sz = 1024 << ((g_mem[R_TMSR] >> (2 * s)) & 3);
		if(tx + sz > 0x2000) {
			sz = 0;
		}
		g_sock[s].tx_base = (uint16_t)(TX_BASE + tx);
		g_sock[s].tx_size = (uint16_t)sz;
		tx += sz;

		sz = 1024 << ((g_mem[R_RMSR] >> (2 * s)) & 3);
		if(rx + sz > 0x2000) {
			sz = 0;
		}
		g_sock[s].rx_base = (uint16_t)(RX_BASE + rx);
		g_sock[s].rx_size = (uint16_t)sz;
		rx += sz;
	}
}

void
uthernet2_reset(void)
{
	int	s;

	memset(g_mem, 0, sizeof(g_mem));
	memset(g_sock, 0, sizeof(g_sock));
	g_mode = 0;
	g_addr = 0;
	put16(0x0017, 0x07D0);		/* RTR: retry time 200ms */
	g_mem[0x0019] = 0x08;		/* RCR: retry count */
	g_mem[R_RMSR] = 0x55;		/* 2 KB per socket */
	g_mem[R_TMSR] = 0x55;
	for(s = 0; s < 4; s++) {
		g_mem[sreg(s, 0x16)] = 0x80;	/* TTL */
	}
	compute_buffers();
	g_qhead = g_qcount = 0;
}

static uint16_t
tx_fsr(int s)
{
	uint16_t used = (uint16_t)(get16(sreg(s, S_TX_WR)) -
						get16(sreg(s, S_TX_RD)));

	return used > g_sock[s].tx_size ? 0 :
					(uint16_t)(g_sock[s].tx_size - used);
}

/* EMUBUDDY_NETDEBUG=1 also prints a one-line summary of every frame. */
static void
pkt_log(const char *dir, const uint8_t *f, int len)
{
	static int	on = -1;
	int		type, proto;

	if(on < 0) {
		on = getenv("EMUBUDDY_NETDEBUG") != NULL;
	}
	if(!on || len < 14) {
		return;
	}
	type = (f[12] << 8) | f[13];
	if(type == 0x0806 && len >= 42) {
		printf("NET %s ARP %s %d.%d.%d.%d\n", dir, f[21] == 1 ? "who-has" :
			"reply", f[38], f[39], f[40], f[41]);
	} else if(type == 0x0800 && len >= 38) {
		proto = f[23];
		printf("NET %s IP %d.%d.%d.%d -> %d.%d.%d.%d %s port %d -> %d len %d\n",
			dir, f[26], f[27], f[28], f[29], f[30], f[31], f[32], f[33],
			proto == 6 ? "TCP" : proto == 17 ? "UDP" : "proto",
			(f[34] << 8) | f[35], (f[36] << 8) | f[37], len);
	} else {
		printf("NET %s type %04x len %d\n", dir, type, len);
	}
}

/* ---- commands ---- */

static void
sock_send_macraw(int s)
{
	Sock		*k = &g_sock[s];
	uint16_t	rd = get16(sreg(s, S_TX_RD));
	uint16_t	wr = get16(sreg(s, S_TX_WR));
	uint16_t	len = (uint16_t)(wr - rd), i, mask;
	uint8_t		frame[QMAX];

	if(k->tx_size == 0) {
		return;
	}
	mask = (uint16_t)(k->tx_size - 1);
	if(len > k->tx_size || len > sizeof(frame)) {
		printf("Uthernet II: bad send length %u\n", len);
		len = 0;
	}
	for(i = 0; i < len; i++) {
		frame[i] = g_mem[k->tx_base + ((rd + i) & mask)];
	}
	if(len >= 14 && g_net_up) {
		pkt_log("out", frame, len);
		net_send(frame, len);
		g_tx_count++;
	}
	put16(sreg(s, S_TX_RD), wr);
	g_mem[sreg(s, S_IR)] |= IR_SEND_OK;
}

static void
sock_command(int s, int cmd)
{
	int	proto = g_mem[sreg(s, S_MR)] & 0x0f;
	Sock	*k = &g_sock[s];

	switch(cmd) {
	case C_OPEN:
		put16(sreg(s, S_TX_RD), get16(sreg(s, S_TX_WR)));
		k->rx_wr = get16(sreg(s, S_RX_RD));
		k->rx_rsr = 0;
		switch(proto) {
		case P_MACRAW:
			if(s != 0) {
				printf("Uthernet II: MACRAW only on socket 0\n");
				g_mem[sreg(s, S_SR)] = SR_CLOSED;
				break;
			}
			g_mem[sreg(s, S_SR)] = SR_MACRAW;
			g_qhead = g_qcount = 0;
			if(!g_net_up) {
				g_net_up = (net_init() == 0);
			}
			printf("Uthernet II: socket 0 opened in MACRAW mode "
				"(network: %s)\n", g_net_up ? net_backend_name() :
							"unavailable");
			break;
		case P_TCP:
			printf("Uthernet II: socket %d opened in TCP mode - "
				"hardware TCP is not emulated yet\n", s);
			g_mem[sreg(s, S_SR)] = SR_INIT;
			break;
		case P_UDP:
			printf("Uthernet II: socket %d opened in UDP mode - "
				"hardware UDP is not emulated yet\n", s);
			g_mem[sreg(s, S_SR)] = SR_UDP;
			break;
		case P_IPRAW:
			printf("Uthernet II: socket %d opened in IPRAW mode - "
				"not emulated yet\n", s);
			g_mem[sreg(s, S_SR)] = SR_IPRAW;
			break;
		default:
			g_mem[sreg(s, S_SR)] = SR_CLOSED;
		}
		break;
	case C_CLOSE:
	case C_DISCON:
		g_mem[sreg(s, S_SR)] = SR_CLOSED;
		if(s == 0) {
			g_qhead = g_qcount = 0;
		}
		break;
	case C_SEND:
	case C_SEND_MAC:
		if(g_mem[sreg(s, S_SR)] == SR_MACRAW) {
			sock_send_macraw(s);
		} else {
			/* not emulated: pretend it went out */
			put16(sreg(s, S_TX_RD), get16(sreg(s, S_TX_WR)));
			g_mem[sreg(s, S_IR)] |= IR_SEND_OK;
		}
		break;
	case C_RECV:
		k->rx_rsr = (uint16_t)(k->rx_wr - get16(sreg(s, S_RX_RD)));
		if(k->rx_rsr > k->rx_size) {
			k->rx_rsr = 0;		/* guest moved RX_RD wrongly */
			k->rx_wr = get16(sreg(s, S_RX_RD));
		}
		break;
	case C_LISTEN:
	case C_CONNECT:
	case C_SEND_KEEP:
	default:
		break;
	}
}

/* ---- memory access ---- */

static int
mem_read(uint16_t a)
{
	int	s, off;

	if(a == 0) {
		return g_mode;
	}
	if(a >= 0x0400 && a < 0x0800) {
		s = (a >> 8) - 4;
		off = a & 0xff;
		switch(off) {
		case S_CR:
			return 0;		/* commands complete at once */
		case S_TX_FSR:
			return tx_fsr(s) >> 8;
		case S_TX_FSR + 1:
			return tx_fsr(s) & 0xff;
		case S_RX_RSR:
			return g_sock[s].rx_rsr >> 8;
		case S_RX_RSR + 1:
			return g_sock[s].rx_rsr & 0xff;
		}
		return g_mem[a];
	}
	if(a < 0x0030 || a >= 0x4000) {
		return g_mem[a & 0x7fff];
	}
	return 0;
}

static void
mem_write(uint16_t a, int val)
{
	int	s, off;

	val &= 0xff;
	if(a == 0) {
		if(val & MR_RST) {
			uthernet2_reset();
		} else {
			g_mode = (uint8_t)val;
		}
		return;
	}
	if(a < 0x0030) {
		if(a == R_IR) {
			g_mem[a] &= (uint8_t)~val;	/* write 1 to clear */
			return;
		}
		g_mem[a] = (uint8_t)val;
		if(a == R_RMSR || a == R_TMSR) {
			compute_buffers();
		}
		return;
	}
	if(a >= 0x0400 && a < 0x0800) {
		s = (a >> 8) - 4;
		off = a & 0xff;
		switch(off) {
		case S_CR:
			sock_command(s, val);
			return;
		case S_IR:
			g_mem[a] &= (uint8_t)~val;
			if(!g_mem[a]) {
				g_mem[R_IR] &= (uint8_t)~(1 << s);
			}
			return;
		case S_SR:
		case S_TX_FSR: case S_TX_FSR + 1:
		case S_TX_RD: case S_TX_RD + 1:
		case S_RX_RSR: case S_RX_RSR + 1:
			return;			/* read-only */
		}
		g_mem[a] = (uint8_t)val;
		return;
	}
	if(a >= 0x4000 && a < 0x8000) {
		g_mem[a] = (uint8_t)val;
	}
}

static void
addr_step(void)
{
	if(!(g_mode & MR_AI)) {
		return;
	}
	g_addr++;
	/* auto-increment wraps inside the TX and RX buffer regions */
	if(g_addr == 0x6000) {
		g_addr = 0x4000;
	} else if(g_addr == 0x8000) {
		g_addr = 0x6000;
	}
}

/* EMUBUDDY_NETDEBUG=1 logs the first register accesses (driver bring-up). */
static void
dbg(const char *rw, int reg, int val)
{
	static int	n = -1;

	if(n < 0) {
		n = getenv("EMUBUDDY_NETDEBUG") ? 0 : 1000000;
	}
	if(n < 400) {
		n++;
		printf("U2 %s reg%x addr=%04x val=%02x\n", rw, reg, g_addr, val);
	}
}

int
uthernet2_read(int reg)
{
	int	v;

	switch(reg & 0x0f) {
	case 4:
		return g_mode;
	case 5:
		return g_addr >> 8;
	case 6:
		return g_addr & 0xff;
	case 7:
		v = mem_read(g_addr);
		dbg("rd", reg, v);
		addr_step();
		return v;
	}
	dbg("rd", reg, 0);
	return 0;
}

void
uthernet2_write(int reg, int val)
{
	dbg("wr", reg, val);
	switch(reg & 0x0f) {
	case 4:
		mem_write(0, val);
		break;
	case 5:
		g_addr = (uint16_t)(((val & 0x7f) << 8) | (g_addr & 0xff));
		break;
	case 6:
		g_addr = (uint16_t)((g_addr & 0x7f00) | (val & 0xff));
		break;
	case 7:
		mem_write(g_addr, val);
		addr_step();
		break;
	}
}

/* ---- receive path ---- */

int
uthernet2_active(void)
{
	return g_mem[sreg(0, S_SR)] == SR_MACRAW;
}

void
uthernet2_rx_frame(const uint8_t *frame, int len)
{
	int	slot;

	if(!uthernet2_active() || len < 14 || len > QMAX) {
		return;
	}
	if(g_mem[sreg(0, S_MR)] & MR_MF) {
		/* MAC filter: own address, broadcast or multicast only */
		if(!(frame[0] & 1) && memcmp(frame, &g_mem[R_SHAR], 6) != 0) {
			return;
		}
	}
	if(g_qcount == QN) {
		return;				/* queue full: drop */
	}
	slot = (g_qhead + g_qcount) % QN;
	memcpy(g_q[slot], frame, (size_t)len);
	g_qlen[slot] = len;
	g_qcount++;
}

static void
deliver(void)
{
	Sock		*k = &g_sock[0];
	uint16_t	mask, need, free_space;
	int		i, len;
	uint8_t		*f;

	if(!uthernet2_active() || k->rx_size == 0) {
		return;
	}
	mask = (uint16_t)(k->rx_size - 1);
	while(g_qcount > 0) {
		len = g_qlen[g_qhead];
		f = g_q[g_qhead];
		need = (uint16_t)(len + 2);
		free_space = (uint16_t)(k->rx_size - k->rx_rsr);
		if(need > free_space) {
			break;			/* wait for the guest to read */
		}
		g_mem[k->rx_base + (k->rx_wr & mask)] = (uint8_t)(need >> 8);
		g_mem[k->rx_base + ((k->rx_wr + 1) & mask)] = (uint8_t)need;
		for(i = 0; i < len; i++) {
			g_mem[k->rx_base + ((k->rx_wr + 2 + i) & mask)] = f[i];
		}
		pkt_log("in ", f, len);
		k->rx_wr = (uint16_t)(k->rx_wr + need);
		k->rx_rsr = (uint16_t)(k->rx_rsr + need);
		g_mem[sreg(0, S_IR)] |= IR_RECV;
		g_mem[R_IR] |= 1;
		g_rx_count++;
		g_qhead = (g_qhead + 1) % QN;
		g_qcount--;
	}
}

void
uthernet2_poll(void)
{
	if(g_net_up) {
		net_poll();
	}
	deliver();
}

void
uthernet2_counters(unsigned *tx, unsigned *rx)
{
	*tx = g_tx_count;
	*rx = g_rx_count;
}
