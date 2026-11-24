/* Apple II High-Speed SCSI Card (Apple 670-0003, ROM 341-0803) for EmuBuddy.
 *
 * The card: an NCR 53C80 SCSI controller, 32 KB of ROM and 8 KB of RAM, and a
 * DMA engine. In the slot:
 *   $Cs00-$CsFF  the first 256 bytes of the ROM
 *   $C800-$CBFF  RAM, 8 banks of 1 KB, bank in $C0nF bits 0-2
 *   $CC00-$CFFF  ROM, 32 banks of 1 KB, bank in $C0nE bits 0-4
 *   $C0n0-$C0n7  53C80 registers ($C0n6 read / $C0n0 write is the pseudo-DMA
 *                port while $C0nC bit 0 is set)
 *   $C0n8-$C0nB  DMA address and count, $C0nC DMA control, $C0nD resume/reset
 *   $C0nE        read: ROM bank and the inverted SCSI ID switches
 * The register map follows MAME's a2hsscsi.cpp (BSD-3-Clause, R. Belmont).
 *
 * Phase 1 of docs/SCSI_PLAN.md: memory map and banks. The 53C80 is not
 * emulated yet: its registers read 0, so the card finds no devices. */

#include "defc.h"
#include "slotcard.h"
#include <string.h>

#define HSSCSI_ROM_SIZE		0x8000
#define HSSCSI_RAM_SIZE		0x2000
#define HSSCSI_ID		7	/* the card's own SCSI ID (DIP SW1-3) */

static byte	g_hs_rom[HSSCSI_ROM_SIZE];
static byte	g_hs_ram[HSSCSI_RAM_SIZE];
static int	g_hs_rom_ok = 0;
static word32	g_hs_rombank, g_hs_rambank;
static word32	g_hs_c0nc, g_hs_c0ne, g_hs_c0nf;
static word32	g_hs_dma_addr, g_hs_dma_size;

char	*g_hsscsi_rom = "";		/* config: the card ROM; "" = search */

static void
hs_reset(void)
{
	g_hs_rombank = 0;
	g_hs_rambank = 0;
	g_hs_c0nc = 0;
	g_hs_c0ne = 0;
	g_hs_c0nf = 0;
	g_hs_dma_addr = 0;
	g_hs_dma_size = 0;
}

static unsigned
hs_io_read(int reg, unsigned long long dfcyc)
{
	(void)dfcyc;
	switch(reg) {
	case 0x8: return g_hs_dma_addr & 0xff;
	case 0x9: return (g_hs_dma_addr >> 8) & 0xff;
	case 0xa: return g_hs_dma_size & 0xff;
	case 0xb: return (g_hs_dma_size >> 8) & 0xff;
	case 0xc: return g_hs_c0nc;	/* SW4 off: pseudo-DMA only */
	case 0xe: return (g_hs_c0ne & 0x1f) | ((~HSSCSI_ID & 7) << 5);
	case 0xf: return g_hs_c0nf;
	case 0xd: return 0xff;
	}
	return 0;			/* 53C80: not emulated yet */
}

static void
hs_io_write(int reg, unsigned val, unsigned long long dfcyc)
{
	(void)dfcyc;
	switch(reg) {
	case 0x8: g_hs_dma_addr = (g_hs_dma_addr & 0xff00) | val; break;
	case 0x9: g_hs_dma_addr = (g_hs_dma_addr & 0xff) | (val << 8); break;
	case 0xa: g_hs_dma_size = (g_hs_dma_size & 0xff00) | val; break;
	case 0xb: g_hs_dma_size = (g_hs_dma_size & 0xff) | (val << 8); break;
	case 0xc: g_hs_c0nc = (g_hs_c0nc & ~0x2bU) | (val & 0x2b); break;
	case 0xe:
		g_hs_c0ne = val;
		g_hs_rombank = (val & 0x1f) * 0x400;
		break;
	case 0xf:
		g_hs_c0nf = val;
		g_hs_rambank = (val & 7) * 0x400;
		break;
	}
}

static unsigned
hs_rom_read(int off, unsigned long long dfcyc)
{
	(void)dfcyc;
	return g_hs_rom[off & 0xff];
}

static unsigned
hs_exp_read(int off, unsigned long long dfcyc)
{
	(void)dfcyc;
	if(off < 0x400) {
		return g_hs_ram[g_hs_rambank + off];
	}
	return g_hs_rom[g_hs_rombank + off - 0x400];
}

static void
hs_exp_write(int off, unsigned val, unsigned long long dfcyc)
{
	(void)dfcyc;
	if(off < 0x400) {
		g_hs_ram[g_hs_rambank + off] = val;
	}
}

static Slot_card g_hs_card = {
	"High-Speed SCSI Card",
	hs_io_read, hs_io_write, hs_rom_read, hs_exp_read, hs_exp_write,
	hs_reset
};

/* Reads a 32 KB ROM file. 0 when it worked. */
static int
hs_read_rom(const char *path)
{
	FILE	*f;
	long	len;

	f = fopen(path, "rb");
	if(!f) {
		return -1;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(len != HSSCSI_ROM_SIZE ||
			fread(g_hs_rom, 1, HSSCSI_ROM_SIZE, f) != HSSCSI_ROM_SIZE) {
		fclose(f);
		return -1;
	}
	fclose(f);
	printf("hsscsi: card ROM %s\n", path);
	return 0;
}

/* Puts the card into slot 7 (on = 1) or takes it out. iigs_rom is the path
 * of the IIgs ROM in use: the card ROM is looked for next to it when no path
 * is set. Returns 0 when the card is in, -1 when its ROM was not found. */
int
hsscsi_attach(int on, const char *iigs_rom)
{
	char	path[1024];
	const char *slash;
	int	dirlen;

	if(!on) {
		if(slot_card_get(7) == &g_hs_card) {
			slot_card_set(7, 0);
		}
		return 0;
	}
	g_hs_rom_ok = 0;
	if(g_hsscsi_rom && g_hsscsi_rom[0]) {
		g_hs_rom_ok = hs_read_rom(g_hsscsi_rom) == 0;
	}
	if(!g_hs_rom_ok && iigs_rom) {
		slash = strrchr(iigs_rom, '/');
		dirlen = slash ? (int)(slash - iigs_rom + 1) : 0;
		snprintf(path, sizeof(path), "%.*s341-0803.bin", dirlen,
								iigs_rom);
		g_hs_rom_ok = hs_read_rom(path) == 0;
	}
	if(!g_hs_rom_ok) {
		printf("hsscsi: no card ROM (341-0803.bin, 32 KB)\n");
		slot_card_set(7, 0);
		return -1;
	}
	memset(g_hs_ram, 0, sizeof(g_hs_ram));
	slot_card_set(7, &g_hs_card);
	return 0;
}
