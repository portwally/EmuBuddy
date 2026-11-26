/* PC Transporter test: drives the card only through its Apple-side
 * registers, the way the Applied Engineering 6502 driver does, and runs a
 * small V30 program (its source is at the end of this file):
 *   1. the RAM pointer ($CF00-$CF04) and the $CC00 window
 *   2. load the program at A0000 and the reset vector at AFFF0, which the V30
 *      sees at F0000/FFFF0; set the port routing table at BF400
 *   3. release HALT: the V30 runs until its OUT to a routed port, waits, and
 *      raises the Apple's IRQ; $CF31/$CF34/$CF35 describe the access
 *   4. $CF32 bit 7 lets it go on; an IN from an "ask the Apple" port waits,
 *      the Apple puts the answer in the mailbox, the V30 runs the IN again
 *   5. CGA memory at B8000, a port that is not routed, an 80186 PUSH imm
 *   6. HLT, then an interrupt with the vector the Apple left at BFFFE
 * Exit code 0 when everything passes. */
#include "../defc.h"
#include "../slotcard.h"
#include "../pcxport.h"
#include <stdio.h>
#include <string.h>

static int g_fail;
#define CHECK(c, msg) do { if(c) printf("  ok   %s\n", msg); \
	else { printf("  FAIL %s\n", msg); g_fail++; } } while(0)

/* ---- what the card needs from the emulator ---- */
dword64	g_cur_dfcyc;
int	g_slot4_mockingboard = 1;
int	g_uthernet2_slot = 4;
static Slot_card *g_card;
static int	g_irq;
static int	g_events;

void slot_card_set(int slot, Slot_card *card)
{
	(void)slot;
	g_card = card;
	if(card && card->reset) {
		card->reset();
	}
}
Slot_card *slot_card_get(int slot) { (void)slot; return g_card; }
void add_irq(word32 mask) { if(mask == IRQ_PENDING_PCXPORT) g_irq = 1; }
void remove_irq(word32 mask) { if(mask == IRQ_PENDING_PCXPORT) g_irq = 0; }
void add_event_pcxport(dword64 dfcyc) { (void)dfcyc; g_events++; }

static const unsigned char g_code[] = {
	0xb8, 0x00, 0x90, 0x8e, 0xd0, 0xbc, 0xfe, 0xff, 0x31, 0xc0, 0x8e, 0xd8, 0xc7, 0x06, 0x00, 0x01, 0x47, 0x00, 0xc7, 0x06, 0x02, 0x01, 0x00, 0xf0, 0xc6, 0x06, 0x00, 0x05, 0x11, 0xb0, 0x55, 0xe6, 0x80, 0xc6, 0x06, 0x01, 0x05, 0x22, 0xe4, 0x81, 0xa2, 0x02, 0x05, 0xb8, 0x00, 0xb8, 0x8e, 0xc0, 0x26, 0xc6, 0x06, 0x00, 0x00, 0x41, 0xe4, 0x90, 0xa2, 0x04, 0x05, 0x68, 0x34, 0x12, 0x5b, 0x89, 0x1e, 0x06, 0x05, 0xfb, 0xf4, 0xeb, 0xfd, 0xc6, 0x06, 0x03, 0x05, 0x33, 0xcf
};
static const unsigned char g_reset[] = { 0xea, 0x00, 0x00, 0x00, 0xf0 };

/* ---- the Apple side ---- */
static unsigned rd(int a) { return g_card->exp_read(a - 0xc800, 0); }
static void wr(int a, unsigned v) { g_card->exp_write(a - 0xc800, v, 0); }
static void setptr(word32 p)
{
	wr(0xcf00, p & 0xff); wr(0xcf01, (p >> 8) & 0xff); wr(0xcf02, p >> 16);
}
static unsigned peek(word32 p) { setptr(p); return rd(0xcf04); }

/* run the V30 for up to n slices, until it waits for the Apple */
static int run(int n)
{
	int	i;

	for(i = 0; i < n; i++) {
		g_cur_dfcyc += 64 << 16;
		pcxport_event(g_cur_dfcyc);
		if(rd(0xcf32) & 0x80) {
			return 1;
		}
	}
	return 0;
}

int
main(void)
{
	word32	i;

	printf("PC Transporter\n");
	pcxport_attach(4);
	CHECK(g_card != 0, "card in slot 4");
	CHECK(g_slot4_mockingboard == 0 && g_uthernet2_slot == 0,
					"the Mockingboard and Uthernet II left slot 4");
	CHECK(g_events > 0, "the V30 has its time event");
	CHECK(g_card->rom_read(0, 0) == 0xff, "no firmware at $Cn00");

	/* 1 */
	setptr(0x012345);
	CHECK(rd(0xcf00) == 0x45 && rd(0xcf01) == 0x23 && rd(0xcf02) == 0x01,
							"pointer reads back");
	wr(0xcf03, 0xaa); wr(0xcf03, 0xbb);
	CHECK(rd(0xcf00) == 0x47, "$CF03 counts up");
	wr(0xcf05, 0x01); wr(0xcf06, 0x23);
	CHECK(rd(0xcc45) == 0xaa && rd(0xcc46) == 0xbb,
					"$CC00 window shows the same RAM");
	CHECK(rd(0xcf05) == 0x01 && rd(0xcf06) == 0x23, "$CF05/$CF06 read back");
	wr(0xce10, 0x5a);
	wr(0xcf0e, 0x00); wr(0xcf0f, 0x00);
	wr(0xce10, 0x5a);
	CHECK(peek(0x000010) == 0x5a, "$CE00 window writes card RAM");

	/* 2 */
	setptr(0xa0000);
	for(i = 0; i < sizeof(g_code); i++) {
		wr(0xcf03, g_code[i]);
	}
	setptr(0xafff0);
	for(i = 0; i < sizeof(g_reset); i++) {
		wr(0xcf03, g_reset[i]);
	}
	setptr(0xbf400 + 0x80);
	wr(0xcf04, 0x12);	/* port 80: 2 (latched), port 81: 1 (ask) */
	wr(0xc881, 0x77);	/* stale mailbox value for port 81 */
	wr(0xc890, 0x3c);	/* port 90, not routed */
	CHECK(peek(0xa0000) == 0xb8, "program loaded at A0000");

	/* 3 */
	wr(0xcf31, 0x80);	/* card IRQ on */
	CHECK(!run(10), "V30 still halted after reset");
	wr(0xcf30, 0x20);	/* release HALT */
	CHECK(run(50), "V30 waits after OUT 80");
	CHECK(g_irq, "the Apple's IRQ is up");
	CHECK(rd(0xcf34) == 0x80 && rd(0xcf35) == 0x00, "$CF34/$CF35 = port 80");
	CHECK((rd(0xcf31) & 0xc0) == 0x00, "$CF31: a byte write");
	CHECK(rd(0xc880) == 0x55, "the Apple reads the byte at $C880");
	CHECK(peek(0x500) == 0x11 && peek(0x501) == 0x00,
					"V30 stopped right after the OUT");

	/* 4 */
	wr(0xcf32, 0x80);
	CHECK(!g_irq, "IRQ down after $CF32");
	CHECK(run(50), "V30 waits at IN 81");
	CHECK((rd(0xcf31) & 0xc0) == 0x80 && rd(0xcf34) == 0x81,
					"$CF31: a byte read of port 81");
	CHECK(peek(0x501) == 0x22 && peek(0x502) == 0x00,
					"the IN has not stored anything yet");
	wr(0xc881, 0x99);	/* the Apple's answer */
	wr(0xcf32, 0x80);
	CHECK(!run(50), "V30 runs on to HLT");
	CHECK(peek(0x502) == 0x99, "the IN ran again and got the answer");

	/* 5 */
	CHECK(peek(0x504) == 0x3c, "unrouted port read from dual-ported RAM");
	CHECK(peek(0x506) == 0x34 && peek(0x507) == 0x12, "PUSH imm (80186)");
	CHECK(peek(0xb8000) == 0x00, "CGA memory is the card's own, not RAM");

	/* 6 */
	CHECK(peek(0x503) == 0x00, "no interrupt yet");
	setptr(0xbfffe);
	wr(0xcf04, 0x40);
	wr(0xcf31, 0x40);	/* INT to the V30 */
	run(5);
	CHECK(peek(0x503) == 0x33, "interrupt 40h taken out of HLT");
	wr(0xcf30, 0x40);
	CHECK(peek(0x0100) == 0x47 && peek(0x0103) == 0xf0, "IVT written by V30");

	/* reset */
	g_card->reset();
	CHECK(!run(5) && !g_irq, "Apple reset halts the V30");
	pcxport_attach(0);
	CHECK(g_card == 0, "card out");

	printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
	return g_fail ? 1 : 0;
}

/* The V30 program (nasm -f bin):
 *	bits 16
 *	org 0
 *  start:	mov ax, 0x9000
 *		mov ss, ax
 *		mov sp, 0xfffe
 *		xor ax, ax
 *		mov ds, ax
 *		mov word [0x40*4], isr
 *		mov word [0x40*4+2], 0xf000
 *		mov byte [0x500], 0x11
 *		mov al, 0x55
 *		out 0x80, al
 *		mov byte [0x501], 0x22
 *		in al, 0x81
 *		mov [0x502], al
 *		mov ax, 0xb800
 *		mov es, ax
 *		mov byte [es:0], 'A'
 *		in al, 0x90
 *		mov [0x504], al
 *		push word 0x1234
 *		pop bx
 *		mov [0x506], bx
 *		sti
 *  idle:	hlt
 *		jmp idle
 *  isr:	mov byte [0x503], 0x33
 *		iret
 *  and at FFF0: jmp 0xf000:start */
