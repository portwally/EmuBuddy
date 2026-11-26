/* Applied Engineering PC Transporter for EmuBuddy.
 *
 * The card is a PC-XT on an Apple II card: an NEC V30 at 7.16 MHz, 768 KB of
 * RAM, a CGA and a floppy controller (the TransDrive, an ISM). It has no ROM:
 * the Applied Engineering software, a ProDOS 8 program, loads the PC BIOS into
 * the card's RAM, starts the V30 and then does keyboard, disks, clock, timer
 * and interrupt controller in 6502 code, through a mailbox. The PC's hard disk
 * is a file (MSDOSVOL) on a ProDOS volume.
 *
 * The register map, the mailbox, the port routing table and the CGA and ISM
 * wiring follow MAME's a2bus/pc_xporter.cpp:
 *   license:BSD-3-Clause, copyright-holders:R. Belmont
 *   (additional reverse-engineering by Peter Ferrie)
 * The V30 is libx86emu (x86emu/, MIT-style licence), a 386 core run in real
 * mode.
 *
 * Apple side, once $Cn00 has been read or written:
 *   $C800-$CBFF  the V30's I/O ports 000-3FF (dual-ported RAM; 3D0-3DF CGA)
 *   $CC00-$CDFF  a 2-page window into the card RAM, base $CF05/$CF06
 *   $CE00-$CEFF  a 1-page window, base $CF0E/$CF0F
 *   $CF00-$CF02  a 24-bit RAM pointer, $CF03 data with increment, $CF04
 *                without
 *   $CF20-$CF27  ISM (TransDrive), $CF2C-$CF2F CGA registers
 *   $CF30/$CF31  control clear/set: bit 4 V30 RESET, 5 V30 HALT, 6 V30 INT,
 *                7 card IRQ enabled. $CF31 read: bit 7 the V30 read, bit 6 a
 *                16-bit access
 *   $CF32        bit 7 read: the V30 waits; write bit 7: the Apple is done
 *   $CF34/$CF35  the port the V30 touched
 * V30 side: RAM 00000-AFFFF, the CGA's 16 KB at B8000 (mirrored at BC000),
 * and A0000-AFFFF again, read only, at F0000 (the BIOS).
 *
 * The routing table at BF400 (card RAM, one nibble per port) says which V30
 * port accesses the 6502 must hear about: 0 none, even: the V30 continues
 * with what the mailbox holds, odd (reads): the 6502 works the answer out and
 * the V30 runs the same IN again.
 *
 * EMUBUDDY_PCXPORT_TRACE=1 prints the mailbox traffic and the control
 * register writes, =2 also every access to $CFxx. */

#include "defc.h"
#include "slotcard.h"
#include "pcxport.h"
#include "x86emu/include/x86emu.h"
#include <string.h>
#include <stdlib.h>

extern dword64	g_cur_dfcyc;
extern int	g_slot4_mockingboard;
extern int	g_uthernet2_slot;

int	g_pcxport_slot = 0;		/* config: the card's slot, 0 = none */

#define PCX_RAM_SIZE		(768 * 1024)
#define PCX_VRAM_SIZE		0x4000
#define PCX_ROUTING_TABLE	0xbf400
#define PCX_VECTOR		0xbfffe

/* The V30 runs in slices of PCX_SLICE_FCYC 1.02 MHz cycles (64 us), with
 * PCX_INSTR_PER_FCYC instructions per cycle: a 7.16 MHz V30 averages about
 * 7 clocks an instruction, so about one per 1.02 MHz cycle. */
#define PCX_SLICE_FCYC		64
#define PCX_INSTR_PER_FCYC	1

typedef struct {
	byte	ram[PCX_RAM_SIZE];
	byte	vram[PCX_VRAM_SIZE];	/* the CGA's own 16 KB */
	byte	ports[0x400];		/* the dual-ported I/O RAM */
	byte	regs[0x400];		/* $CC00-$CFFF as plain registers */
	word32	ptr, cc00, ce00;
	word32	cpu_status;
	int	irq_enable;
	int	irq_raised;
	word32	data_pending;		/* 0x80: the V30 waits for the Apple */
	word32	in_restart;		/* port of an IN to run again, or -1 */
	int	stalled;		/* the V30 waits for $CF32 bit 7 */
	int	apple_io;		/* the Apple, not the V30, is at a port */
	int	reset_line, halt_line, int_line;
	int	reset_during_halt;
	int	hlt_wait;		/* the V30 ran HLT, waits for an INT */
	int	stop_by_card;		/* x86emu_stop() came from us */
	int	event_queued;
	/* CGA */
	word32	crtc_index;
	byte	crtc[32];
	word32	cga_mode, cga_color;
	/* ISM with no drive attached */
	word32	ism_mode, ism_setup, ism_phases, ism_error, ism_param_idx;
	byte	ism_param[16];
} Pcxport;

static Pcxport	*g_pcx;
static x86emu_t	*g_v30;
static int	g_pcx_trace;
static int	g_pcx_slot_in_use;	/* where the card is plugged in now */

static unsigned	pcx_rom_read(int off, dword64 dfcyc);
static unsigned	pcx_io_read(int reg, dword64 dfcyc);
static void	pcx_io_write(int reg, unsigned val, dword64 dfcyc);
static unsigned	pcx_exp_read(int off, dword64 dfcyc);
static void	pcx_exp_write(int off, unsigned val, dword64 dfcyc);
static void	pcx_reset(void);

static Slot_card g_pcx_card = {
	"PC Transporter",
	pcx_io_read, pcx_io_write, pcx_rom_read,
	pcx_exp_read, pcx_exp_write, pcx_reset
};

/* ------------------------------------------------------------------ */
/* Card RAM, as the Apple sees it */

static unsigned
ram_r(word32 addr)
{
	return (addr < PCX_RAM_SIZE) ? g_pcx->ram[addr] : 0xff;
}

static void
ram_w(word32 addr, unsigned val)
{
	if(addr < PCX_RAM_SIZE) {
		g_pcx->ram[addr] = val;
	}
}

/* $CF03/$CF04 writes to B0000-BFFFF also reach the CGA's memory (MAME folds
 * all four 16 KB quarters onto it) */
static void
ptr_w(word32 addr, unsigned val)
{
	ram_w(addr, val);
	if(addr >= 0xb0000 && addr <= 0xbffff) {
		g_pcx->vram[addr & (PCX_VRAM_SIZE - 1)] = val;
	}
}

/* ------------------------------------------------------------------ */
/* The Apple's IRQ */

static void
update_irq(void)
{
	int	want;

	want = g_pcx->data_pending && g_pcx->irq_enable;
	if(want && !g_pcx->irq_raised) {
		add_irq(IRQ_PENDING_PCXPORT);
	} else if(!want && g_pcx->irq_raised) {
		remove_irq(IRQ_PENDING_PCXPORT);
	}
	g_pcx->irq_raised = want;
}

/* ------------------------------------------------------------------ */
/* CGA registers (the picture comes later: docs/PCTRANSPORTER_PLAN.md,
 * phase 4) */

static unsigned
cga_read(word32 port)
{
	dword64	fcyc;
	word32	line, col, val;

	switch(port & 0xf) {
	case 0x1: case 0x3: case 0x5: case 0x7:		/* 6845 data */
		if(g_pcx->crtc_index >= 0x0c && g_pcx->crtc_index <= 0x11) {
			return g_pcx->crtc[g_pcx->crtc_index];
		}
		return 0;
	case 0xa:				/* status */
		/* bit 0: no display (retrace), bit 3: vertical retrace;
		 *  timed on the IIgs's 1.02 MHz clock, 262 lines of 65 */
		fcyc = g_cur_dfcyc >> 16;
		line = (word32)((fcyc / 65) % 262);
		col = (word32)(fcyc % 65);
		val = 0xf0;
		if(line >= 200) {
			val |= 0x09;
		} else if(col >= 52) {
			val |= 0x01;
		}
		return val;
	}
	return 0xff;
}

static void
cga_write(word32 port, unsigned val)
{
	switch(port & 0xf) {
	case 0x0: case 0x2: case 0x4: case 0x6:
		g_pcx->crtc_index = val & 0x1f;
		break;
	case 0x1: case 0x3: case 0x5: case 0x7:
		g_pcx->crtc[g_pcx->crtc_index] = val;
		break;
	case 0x8:
		g_pcx->cga_mode = val;
		break;
	case 0x9:
		g_pcx->cga_color = val;
		break;
	}
}

/* ------------------------------------------------------------------ */
/* The ISM, with no TransDrive on it: the driver finds no drive. The register
 * order and readbacks follow MAME's swim1.cpp (ISM mode) and the PC
 * Transporter wiring in pc_xporter.cpp. */

static unsigned
ism_read(int reg)
{
	unsigned	val;

	switch(reg & 7) {
	case 0:			/* data */
	case 1:			/* mark */
		if(!g_pcx->ism_error) {
			g_pcx->ism_error |= 4;		/* FIFO underrun */
		}
		return 0xff;
	case 2:			/* error */
		val = g_pcx->ism_error;
		g_pcx->ism_error = 0;
		return val;
	case 3:			/* parameter RAM */
		val = g_pcx->ism_param[g_pcx->ism_param_idx];
		g_pcx->ism_param_idx = (g_pcx->ism_param_idx + 1) & 15;
		return val;
	case 4:			/* phases: PHASE 2 is TRACK 0, high: no drive */
		return (g_pcx->ism_phases & ~0x04) | 0x04;
	case 5:
		return g_pcx->ism_setup;
	case 6:
		return g_pcx->ism_mode;
	case 7:			/* handshake: SENSE (bit 3) low, no drive */
		val = 0x04;
		if(g_pcx->ism_error) {
			val |= 0x20;
		}
		if(g_pcx->ism_mode & 0x10) {
			val |= 0xc0;		/* writing: FIFO empty */
		}
		return val;
	}
	return 0xff;
}

static void
ism_write(int reg, unsigned val)
{
	switch(reg & 7) {
	case 3:
		g_pcx->ism_param[g_pcx->ism_param_idx] = val;
		g_pcx->ism_param_idx = (g_pcx->ism_param_idx + 1) & 15;
		break;
	case 4:
		g_pcx->ism_phases = val;
		break;
	case 5:
		g_pcx->ism_setup = val;
		break;
	case 6:			/* mode, write zeroes; ISM mode stays on */
		g_pcx->ism_mode &= ~(val & ~0x40);
		g_pcx->ism_param_idx = 0;
		break;
	case 7:			/* mode, write ones */
		g_pcx->ism_mode |= val | 0x40;
		break;
	}
}

/* ------------------------------------------------------------------ */
/* The V30 */

static void
v30_reset(void)
{
	x86emu_reset(g_v30);
	/* the 8086 starts at FFFF:0000; libx86emu's F000:FFF0 is the same
	 *  address, but give the BIOS the segment it expects */
	x86emu_set_seg_register(g_v30, g_v30->x86.R_CS_SEL, 0xffff);
	g_v30->x86.R_EIP = 0;
	g_pcx->hlt_wait = 0;
}

/* The routing nibble of a port, from the table the software keeps */
static unsigned
port_routing(word32 port)
{
	unsigned	entry;

	entry = ram_r(PCX_ROUTING_TABLE + (port & 0x3fe));
	return (port & 1) ? (entry >> 4) : (entry & 0x0f);
}

/* The V30 has touched a port the Apple looks after: it waits until the
 * Apple writes $CF32 bit 7. For a read whose answer the Apple works out,
 * the IN is run again afterwards (MAME does the same). */
static void
stall_v30(word32 port, int is_read, int is_16bit, int supply)
{
	g_pcx->data_pending = 0x80;
	g_pcx->regs[0x334] = port & 0xff;
	g_pcx->regs[0x335] = (port >> 8) & 0xff;
	g_pcx->regs[0x331] &= ~0xc0;
	if(is_read) {
		g_pcx->regs[0x331] |= 0x80;
	}
	if(is_16bit) {
		g_pcx->regs[0x331] |= 0x40;
	}
	if(is_read && supply) {
		x86emu_set_seg_register(g_v30, g_v30->x86.R_CS_SEL,
							g_v30->x86.saved_cs);
		g_v30->x86.R_EIP = g_v30->x86.saved_eip;
	}
	g_pcx->stalled = 1;
	g_pcx->stop_by_card = 1;
	x86emu_stop(g_v30);
	update_irq();
	if(g_pcx_trace) {
		printf("pcxport: V30 %s %03x (%s%s) waits, at %04x:%04x\n",
			is_read ? "IN " : "OUT", port,
			is_16bit ? "word" : "byte",
			(is_read && supply) ? ", will repeat" : "",
			g_v30->x86.saved_cs, g_v30->x86.saved_eip);
	}
}

/* A 16-bit access to the port pair at port & ~1, as MAME's x86_port_r:
 * mask 0x00ff the even byte, 0xff00 the odd one, 0xffff both */
static word32
x86_port_r(word32 port, word32 mask)
{
	word32	base, val;
	unsigned routing;

	base = port & 0x3fe;
	val = g_pcx->ports[base] | (g_pcx->ports[base + 1] << 8);
	if(mask == 0xff00) {
		base++;
	}
	routing = g_pcx->apple_io ? 0 : port_routing(base);
	if(routing == 0) {
		/* the dual-ported RAM is all there is */
	} else if(!(routing & 1)) {
		/* the mailbox already holds the answer; the Apple runs
		 *  afterwards and what it leaves is for the next read */
		stall_v30(base, 1, mask == 0xffff, 0);
	} else if(base != g_pcx->in_restart) {
		g_pcx->in_restart = base;
		stall_v30(base, 1, mask == 0xffff, 1);
	} else {
		if(g_pcx_trace) {
			printf("pcxport: V30 IN  %03x -> %04x (answered)\n",
								base, val);
		}
		g_pcx->in_restart = 0xffffffff;
	}
	return val;
}

static void
x86_port_w(word32 port, word32 val, word32 mask)
{
	word32	base;

	base = port & 0x3fe;
	if(mask & 0x00ff) {
		g_pcx->ports[base] = val & 0xff;
	}
	if(mask & 0xff00) {
		g_pcx->ports[base + 1] = (val >> 8) & 0xff;
	}
	if(mask == 0xff00) {
		base++;
	}
	if(!g_pcx->apple_io && port_routing(base) != 0) {
		if(g_pcx_trace) {
			printf("pcxport: V30 OUT %03x <- %04x\n", base, val);
		}
		stall_v30(base, 0, mask == 0xffff, 0);
	}
}

/* One byte from a V30 port */
static unsigned
v30_in8(word32 port)
{
	port &= 0xffff;
	if(port >= 0x400) {
		return 0xff;
	}
	if(port >= 0x3d0 && port <= 0x3df) {
		return cga_read(port);
	}
	if(port & 1) {
		return x86_port_r(port, 0xff00) >> 8;
	}
	return x86_port_r(port, 0x00ff) & 0xff;
}

static void
v30_out8(word32 port, unsigned val)
{
	port &= 0xffff;
	if(port >= 0x400) {
		return;
	}
	if(port >= 0x3d0 && port <= 0x3df) {
		cga_write(port, val);
		return;
	}
	if(port & 1) {
		x86_port_w(port, (val & 0xff) << 8, 0xff00);
	} else {
		x86_port_w(port, val & 0xff, 0x00ff);
	}
}

static word32
v30_in16(word32 port)
{
	port &= 0xffff;
	if(!(port & 1) && port < 0x3d0) {
		return x86_port_r(port, 0xffff);
	}
	/* an odd port, or the CGA: two byte cycles, as on the 8086 */
	return v30_in8(port) | (v30_in8(port + 1) << 8);
}

static void
v30_out16(word32 port, word32 val)
{
	port &= 0xffff;
	if(!(port & 1) && port < 0x3d0) {
		x86_port_w(port, val & 0xffff, 0xffff);
		return;
	}
	v30_out8(port, val & 0xff);
	v30_out8(port + 1, (val >> 8) & 0xff);
}

/* One byte of the V30's memory (1 MB, it wraps like an 8086) */
static unsigned
v30_mem_r8(word32 addr)
{
	addr &= 0xfffff;
	if(addr < 0xb0000) {
		return g_pcx->ram[addr];
	}
	if(addr >= 0xb8000 && addr < 0xc0000) {
		return g_pcx->vram[addr & (PCX_VRAM_SIZE - 1)];
	}
	if(addr >= 0xf0000) {
		return g_pcx->ram[0xa0000 + (addr & 0xffff)];
	}
	return 0xff;
}

static void
v30_mem_w8(word32 addr, unsigned val)
{
	addr &= 0xfffff;
	if(addr < 0xb0000) {
		g_pcx->ram[addr] = val;
	} else if(addr >= 0xb8000 && addr < 0xc0000) {
		g_pcx->vram[addr & (PCX_VRAM_SIZE - 1)] = val;
	}
}

static unsigned
v30_memio(x86emu_t *emu, u32 addr, u32 *val, unsigned type)
{
	unsigned	bits, i, n;
	word32	v;

	(void)emu;
	bits = type & 0xff;
	n = (bits == X86EMU_MEMIO_16) ? 2 : (bits == X86EMU_MEMIO_32) ? 4 : 1;
	switch(type & ~0xff) {
	case X86EMU_MEMIO_R:
	case X86EMU_MEMIO_X:
		v = 0;
		for(i = 0; i < n; i++) {
			v |= v30_mem_r8(addr + i) << (8 * i);
		}
		*val = v;
		break;
	case X86EMU_MEMIO_W:
		for(i = 0; i < n; i++) {
			v30_mem_w8(addr + i, (*val >> (8 * i)) & 0xff);
		}
		break;
	case X86EMU_MEMIO_I:
		if(n == 1) {
			*val = v30_in8(addr);
		} else if(n == 2) {
			*val = v30_in16(addr);
		} else {
			*val = v30_in16(addr) | (v30_in16(addr + 2) << 16);
		}
		break;
	case X86EMU_MEMIO_O:
		if(n == 1) {
			v30_out8(addr, *val & 0xff);
		} else if(n == 2) {
			v30_out16(addr, *val & 0xffff);
		} else {
			v30_out16(addr, *val & 0xffff);
			v30_out16(addr + 2, (*val >> 16) & 0xffff);
		}
		break;
	}
	return 0;
}

static void
v30_push16(word32 val)
{
	word32	sp;

	sp = (g_v30->x86.R_SP - 2) & 0xffff;
	g_v30->x86.R_SP = sp;
	v30_mem_w8(g_v30->x86.R_SS_BASE + sp, val & 0xff);
	v30_mem_w8(g_v30->x86.R_SS_BASE + sp + 1, (val >> 8) & 0xff);
}

/* The V30 takes the interrupt the Apple asked for ($CF31 bit 6); its INTA
 * cycle reads the vector the Apple left at BFFFE */
static void
v30_take_int(void)
{
	word32	vector, ivt, ip, cs;

	vector = ram_r(PCX_VECTOR);
	v30_push16(g_v30->x86.R_FLG & 0xffff);
	v30_push16(g_v30->x86.R_CS);
	v30_push16(g_v30->x86.R_IP);
	g_v30->x86.R_FLG &= ~(F_IF | F_TF);
	ivt = vector * 4;
	ip = v30_mem_r8(ivt) | (v30_mem_r8(ivt + 1) << 8);
	cs = v30_mem_r8(ivt + 2) | (v30_mem_r8(ivt + 3) << 8);
	x86emu_set_seg_register(g_v30, g_v30->x86.R_CS_SEL, cs);
	g_v30->x86.R_EIP = ip;
	/* the instruction about to run is the handler's first */
	g_v30->x86.saved_cs = cs;
	g_v30->x86.saved_eip = ip;
	g_pcx->hlt_wait = 0;
	if(g_pcx_trace) {
		printf("pcxport: V30 INT %02x -> %04x:%04x\n", vector, cs, ip);
	}
}

/* Before every instruction */
static int
v30_code_check(x86emu_t *emu)
{
	(void)emu;
	if(g_pcx->stalled) {
		return 1;
	}
	if(g_pcx->int_line && (g_v30->x86.R_FLG & F_IF)) {
		v30_take_int();
	}
	return 0;
}

static int
v30_running(void)
{
	return !g_pcx->reset_line && !g_pcx->halt_line && !g_pcx->stalled;
}

/* Run the V30 for up to n instructions */
static void
v30_run(word32 n)
{
	word32	cs, ip;

	if(!v30_running()) {
		return;
	}
	if(g_pcx->hlt_wait) {
		if(!(g_pcx->int_line && (g_v30->x86.R_FLG & F_IF))) {
			return;
		}
		g_pcx->hlt_wait = 0;
	}
	g_pcx->stop_by_card = 0;
	g_v30->max_instr = g_v30->x86.R_TSC + n;
	(void)x86emu_run(g_v30, X86EMU_RUN_MAX_INSTR);
	if(!g_pcx->stop_by_card && !g_pcx->stalled &&
				g_v30->x86.R_TSC < g_v30->max_instr) {
		/* stopped by itself: HLT (F4, perhaps after a prefix) */
		cs = g_v30->x86.saved_cs;
		ip = g_v30->x86.saved_eip;
		if(v30_mem_r8((cs << 4) + ip) == 0xf4 ||
				v30_mem_r8((cs << 4) + ip + 1) == 0xf4) {
			g_pcx->hlt_wait = 1;
		}
	}
}

/* ------------------------------------------------------------------ */
/* Time for the V30 */

static void
queue_event(dword64 dfcyc)
{
	if(g_pcx && !g_pcx->event_queued && g_pcx_slot_in_use) {
		g_pcx->event_queued = 1;
		add_event_pcxport(dfcyc + ((dword64)PCX_SLICE_FCYC << 16));
	}
}

void
pcxport_event(dword64 dfcyc)
{
	if(!g_pcx) {
		return;
	}
	g_pcx->event_queued = 0;
	if(!g_pcx_slot_in_use) {
		return;			/* the card was taken out */
	}
	v30_run(PCX_SLICE_FCYC * PCX_INSTR_PER_FCYC);
	queue_event(dfcyc);
}

/* ------------------------------------------------------------------ */
/* The Apple side */

static unsigned
pcx_io_read(int reg, dword64 dfcyc)
{
	(void)reg; (void)dfcyc;
	return 0xff;
}

static void
pcx_io_write(int reg, unsigned val, dword64 dfcyc)
{
	(void)reg; (void)val; (void)dfcyc;
}

/* No firmware: reading $Cn00 only hands the card $C800 */
static unsigned
pcx_rom_read(int off, dword64 dfcyc)
{
	(void)off; (void)dfcyc;
	return 0xff;
}

static unsigned
pcx_exp_read(int off, dword64 dfcyc)
{
	unsigned	val;
	int	reg;

	(void)dfcyc;
	if(off < 0x400) {		/* $C800-$CBFF: the V30's ports */
		if(off >= 0x3d0 && off <= 0x3df) {
			return cga_read(off);
		}
		return g_pcx->ports[off];
	}
	if(off < 0x600) {
		return ram_r(g_pcx->cc00 + off - 0x400);
	}
	if(off < 0x700) {
		return ram_r(g_pcx->ce00 + off - 0x600);
	}
	reg = off & 0x3ff;
	switch(off) {
	case 0x700:
		return g_pcx->ptr & 0xff;
	case 0x701:
		return (g_pcx->ptr >> 8) & 0xff;
	case 0x702:
		return (g_pcx->ptr >> 16) & 0xff;
	case 0x703:
		val = ram_r(g_pcx->ptr);
		g_pcx->ptr = (g_pcx->ptr + 1) & 0xffffff;
		return val;
	case 0x704:
		return ram_r(g_pcx->ptr);
	case 0x705:
		return (g_pcx->cc00 >> 16) & 0xff;
	case 0x706:
		return (g_pcx->cc00 >> 8) & 0xff;
	case 0x70e:
		return (g_pcx->ce00 >> 16) & 0xff;
	case 0x70f:
		return (g_pcx->ce00 >> 8) & 0xff;
	case 0x720: case 0x721: case 0x722: case 0x723:
	case 0x724: case 0x725: case 0x726: case 0x727:
		return ism_read(off);
	case 0x730:
	case 0x732:
		return (g_pcx->regs[reg] & 0x7f) | g_pcx->data_pending;
	case 0x731:
		return g_pcx->cpu_status | (g_pcx->regs[0x331] & 0xcf);
	case 0x736:
		/* modifier keys, for a IIe; the IIgs driver reads $C025.
		 *  Nothing pressed: control and shift high, caps lock off */
		return 0x03;
	}
	if(g_pcx_trace > 1) {
		printf("pcxport: read $C%03x = %02x\n", off + 0x800,
							g_pcx->regs[reg]);
	}
	return g_pcx->regs[reg];
}

static void
pcx_control_clear(unsigned val)		/* $CF30 */
{
	if(val & 0x10) {		/* release RESET: the V30 starts over */
		g_pcx->reset_line = 0;
		v30_reset();
		g_pcx->reset_during_halt = 1;
		g_pcx->cpu_status &= ~0x10;
	}
	if(val & 0x20) {		/* release HALT */
		if(g_pcx->reset_during_halt) {
			v30_reset();
			g_pcx->reset_during_halt = 0;
		}
		g_pcx->halt_line = 0;
		g_pcx->cpu_status &= ~0x20;
	}
	if(val & 0x40) {		/* drop the V30's INT */
		g_pcx->int_line = 0;
	}
	if(val & 0x80) {
		g_pcx->irq_enable = 0;
		update_irq();
	}
}

static void
pcx_control_set(unsigned val)		/* $CF31 */
{
	if(val & 0x10) {
		g_pcx->reset_line = 1;
	}
	if(val & 0x20) {
		g_pcx->halt_line = 1;
	}
	if(val & 0x40) {		/* interrupt the V30 */
		g_pcx->int_line = 1;
	}
	if(val & 0x80) {
		g_pcx->irq_enable = 1;
		update_irq();
	}
	g_pcx->cpu_status |= val & 0x30;
	g_pcx->regs[0x331] = (g_pcx->regs[0x331] & ~0x0f) | (val & 0x0f);
}

static void
pcx_exp_write(int off, unsigned val, dword64 dfcyc)
{
	val &= 0xff;
	if(off < 0x400) {
		if(off >= 0x3d0 && off <= 0x3df) {
			cga_write(off, val);
			return;
		}
		g_pcx->apple_io = 1;
		g_pcx->ports[off] = val;
		g_pcx->apple_io = 0;
		return;
	}
	if(off < 0x600) {
		ram_w(g_pcx->cc00 + off - 0x400, val);
		return;
	}
	if(off < 0x700) {
		ram_w(g_pcx->ce00 + off - 0x600, val);
		return;
	}
	if(g_pcx_trace > 1 || (g_pcx_trace &&
					off >= 0x730 && off <= 0x732)) {
		printf("pcxport: write $C%03x = %02x\n", off + 0x800, val);
	}
	switch(off) {
	case 0x700:
		g_pcx->ptr = (g_pcx->ptr & ~0xff) | val;
		break;
	case 0x701:
		g_pcx->ptr = (g_pcx->ptr & ~0xff00) | (val << 8);
		break;
	case 0x702:
		g_pcx->ptr = (g_pcx->ptr & ~0xff0000) | (val << 16);
		break;
	case 0x703:
		ptr_w(g_pcx->ptr, val);
		g_pcx->ptr = (g_pcx->ptr + 1) & 0xffffff;
		break;
	case 0x704:
		ptr_w(g_pcx->ptr, val);
		break;
	case 0x705:
		g_pcx->cc00 = (g_pcx->cc00 & 0xffff) | (val << 16);
		break;
	case 0x706:
		g_pcx->cc00 = (g_pcx->cc00 & 0xff00ff) | (val << 8);
		break;
	case 0x70e:
		g_pcx->ce00 = (g_pcx->ce00 & 0xffff) | (val << 16);
		break;
	case 0x70f:
		g_pcx->ce00 = (g_pcx->ce00 & 0xff00ff) | (val << 8);
		break;
	case 0x720: case 0x721: case 0x722: case 0x723:
	case 0x724: case 0x725: case 0x726: case 0x727:
		ism_write(off, val);
		break;
	case 0x72c:
		cga_write(0x3d6, val);
		break;
	case 0x72d:
		cga_write(0x3d7, val);
		break;
	case 0x72e:
		cga_write(0x3d8, val);
		break;
	case 0x72f:
		cga_write(0x3d9, val);
		break;
	case 0x730:
		pcx_control_clear(val);
		break;
	case 0x731:
		pcx_control_set(val);
		break;
	case 0x732:
		if(val & 0x80) {	/* the Apple is done: the V30 goes on */
			g_pcx->data_pending = 0;
			g_pcx->stalled = 0;
			update_irq();
		}
		break;
	default:
		g_pcx->regs[off & 0x3ff] = val;
		break;
	}
	queue_event(dfcyc);
}

static void
pcx_reset(void)
{
	word32	i;

	if(!g_pcx) {
		return;
	}
	g_pcx->halt_line = 1;		/* the V30 waits for the software */
	g_pcx->reset_line = 0;
	g_pcx->int_line = 0;
	g_pcx->reset_during_halt = 0;
	g_pcx->stalled = 0;
	g_pcx->in_restart = 0xffffffff;
	g_pcx->data_pending = 0;
	g_pcx->irq_enable = 0;
	g_pcx->apple_io = 0;
	g_pcx->hlt_wait = 0;
	g_pcx->ism_mode = 0x40;		/* the card runs its ISM in ISM mode */
	g_pcx->ism_setup = 0;
	g_pcx->ism_phases = 0;
	g_pcx->ism_error = 0;
	g_pcx->ism_param_idx = 0;
	for(i = 0; i < 16; i++) {
		g_pcx->ism_param[i] = 0;
	}
	if(g_pcx->irq_raised) {
		remove_irq(IRQ_PENDING_PCXPORT);
		g_pcx->irq_raised = 0;
	}
	g_pcx->event_queued = 0;	/* do_reset() may have cleared it */
	queue_event(g_cur_dfcyc);
}

/* ------------------------------------------------------------------ */

/* Plug the card into slot 1-7, or take it out (0). Its RAM is kept while it
 * moves, as a real card keeps it while the power is on. */
void
pcxport_attach(int slot)
{
	if(slot != 1 && slot != 2 && slot != 4) {
		slot = 0;	/* 3 is 80 columns, 5 and 6 the drives, 7 the
				 *  hard disks */
	}
	if(g_pcx_slot_in_use && g_pcx_slot_in_use != slot) {
		if(slot_card_get(g_pcx_slot_in_use) == &g_pcx_card) {
			slot_card_set(g_pcx_slot_in_use, 0);
		}
		if(g_pcx && g_pcx->irq_raised) {
			remove_irq(IRQ_PENDING_PCXPORT);
			g_pcx->irq_raised = 0;
		}
		g_pcx_slot_in_use = 0;
	}
	g_pcxport_slot = slot;
	if(!slot) {
		return;
	}
	if(!g_pcx) {
		g_pcx = calloc(1, sizeof(Pcxport));
		g_v30 = x86emu_new(0, 0);
		if(!g_pcx || !g_v30) {
			printf("pcxport: out of memory\n");
			g_pcxport_slot = 0;
			return;
		}
		x86emu_set_memio_handler(g_v30, v30_memio);
		x86emu_set_code_handler(g_v30, v30_code_check);
		v30_reset();
		g_pcx_trace = getenv("EMUBUDDY_PCXPORT_TRACE") ?
				atoi(getenv("EMUBUDDY_PCXPORT_TRACE")) : 0;
	}
	/* one card per slot: what else was in it comes out */
	if(slot == 4) {
		g_slot4_mockingboard = 0;
	}
	if(g_uthernet2_slot == slot) {
		g_uthernet2_slot = 0;
	}
	g_pcx_slot_in_use = slot;
	slot_card_set(slot, &g_pcx_card);	/* calls pcx_reset() */
	printf("pcxport: PC Transporter in slot %d\n", slot);
}
