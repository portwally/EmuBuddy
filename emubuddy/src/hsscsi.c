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
 * The drives S7D1-S7D7 are SCSI hard disks at the IDs 6 down to 0: the
 * firmware looks for devices from ID 6 downwards and starts up from the first
 * one it finds, so S7D1 is the startup disk and the drives keep their order
 * in ProDOS and GS/OS. The card itself is ID 7.
 *
 * A drive with a CD image (.iso, .cdr, .toast) in it is a CD-ROM drive
 * (scsi_cd.c). It stays one when the disc comes out, so discs can be swapped
 * while the IIgs runs. S7D7 is a CD-ROM drive also when it is empty at
 * startup: a disc can go in later without a restart. DIP switch SW4 is off: the firmware moves the data by
 * pseudo-DMA, byte by byte. The 53C80's interrupt is not connected (as in
 * MAME): Apple's firmware and drivers poll the chip.
 *
 * EMUBUDDY_SCSI_TRACE=1 prints every SCSI command and its result, =2 also
 * every access to the card's registers. */

#include "defc.h"
#include "slotcard.h"
#include "scsi.h"
#include "cdimage.h"
#include <ctype.h>
#include <string.h>

#define HSSCSI_ROM_SIZE		0x8000
#define HSSCSI_RAM_SIZE		0x2000
#define HSSCSI_ID		7	/* the card's own SCSI ID (DIP SW1-3) */
#define HSSCSI_DISKS		7	/* S7D1-S7D7 at the SCSI IDs 6-0 */
/* the watchdog one-shot: 250 ms, in 1.02 MHz cycles */
#define HSSCSI_WATCHDOG_CYCS	255682ULL

extern Iwm	g_iwm;
extern dword64	g_cur_dfcyc;

static byte	g_hs_rom[HSSCSI_ROM_SIZE];
static byte	g_hs_ram[HSSCSI_RAM_SIZE];
static int	g_hs_rom_ok = 0;
static word32	g_hs_rombank, g_hs_rambank;
static word32	g_hs_c0nc, g_hs_c0ne, g_hs_c0nf;
static word32	g_hs_dma_addr, g_hs_dma_size;
static int	g_hs_dma_active;	/* the DMA engine answers the chip's DRQ */
static int	g_hs_dma_in;		/* 1: SCSI to memory, 0: memory to SCSI */
static unsigned g_hs_dma_bytes;		/* bytes moved by true DMA, counted */
int	g_hsscsi_dma = 1;		/* config: switch SW4, true DMA */
extern word32	g_c037_dmareg;		/* moremem.c: the DMA bank */
static dword64	g_hs_watchdog_dfcyc;	/* the watchdog runs until then */
static Scsi_disk g_hs_disk[HSSCSI_DISKS];
static Scsi_cd	g_hs_cd[HSSCSI_DISKS];
static int	g_hs_is_cd[HSSCSI_DISKS];	/* the drive is a CD-ROM drive */
#define HSSCSI_CD_UNIT	(HSSCSI_DISKS - 1)	/* S7D7: a CD drive when empty */
/* a .cue sheet in a drive: the CD image it describes */
static Cd_image	g_hs_cue[HSSCSI_DISKS];
static int	g_hs_cue_open[HSSCSI_DISKS];
static char	g_hs_cue_path[HSSCSI_DISKS][1024];
static unsigned g_hs_inserts[HSSCSI_DISKS];	/* discs put in, counted */
static int	g_hs_had_disk[HSSCSI_DISKS];
static Slot_card g_hs_card;
static void	hs_update_devices(int at_reset);
static int	g_hs_scanned;	/* the firmware used the 53C80 since the reset */

char	*g_hsscsi_rom = "";		/* config: the card ROM; "" = search */
int	g_hsscsi_rom_missing = 0;	/* the card was asked for, its ROM not found */

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
	g_hs_watchdog_dfcyc = 0;
	g_hs_dma_active = 0;
	g_hs_dma_in = 0;
	g_hs_scanned = 0;
	ncr5380_reset();
	hs_update_devices(1);
}

/* 1 when the card is in slot 7 and its firmware has started to look for
 * devices since the last reset. Like a real SCSI bus, firmware and GS/OS
 * only know the devices that were there at that time: a disk put into an
 * empty drive later is found at the next restart. */
int
hsscsi_scanned(void)
{
	return g_hs_scanned;
}

/* ---- the drives S7D1-S7D7 as the blocks of the SCSI hard disks ---- */
static Disk *
hs_drive(int unit)
{
	return &(g_iwm.smartport[unit]);
}

static int
hs_be_present(int unit)
{
	return hs_drive(unit)->fd >= 0;
}

static int
has_ext(const char *name, const char *ext)
{
	size_t	len = name ? strlen(name) : 0, elen = strlen(ext);

	return len > elen && !cfgcasecmp(name + len - elen, ext);
}

/* Opens or closes the CD image of a .cue sheet, to match what is in the
 * drive. Returns 1 while the drive holds an open .cue image. */
static int
hs_cue_sync(int unit)
{
	Disk	*dsk = hs_drive(unit);
	int	want = dsk->fd >= 0 && has_ext(dsk->name_ptr, ".cue");

	if(g_hs_cue_open[unit] && (!want ||
				strcmp(g_hs_cue_path[unit], dsk->name_ptr))) {
		cdimage_close(&g_hs_cue[unit]);
		g_hs_cue_open[unit] = 0;
	}
	if(want && !g_hs_cue_open[unit]) {
		snprintf(g_hs_cue_path[unit], sizeof(g_hs_cue_path[unit]),
							"%s", dsk->name_ptr);
		if(cdimage_open_cue(&g_hs_cue[unit], dsk->name_ptr) == 0) {
			g_hs_cue_open[unit] = 1;
			printf("hsscsi: S7D%d: CD with %d tracks, %u "
				"sectors\n", unit + 1, g_hs_cue[unit].ntracks,
				g_hs_cue[unit].total);
		} else {
			printf("hsscsi: S7D%d: cannot read the CD of %s\n",
						unit + 1, dsk->name_ptr);
			g_hs_cue_path[unit][0] = 0;
			return 0;
		}
	}
	return g_hs_cue_open[unit];
}

static unsigned
hs_be_blocks(int unit)
{
	if(g_hs_cue_open[unit]) {
		return g_hs_cue[unit].total * 4;
	}
	return (unsigned)(hs_drive(unit)->dimage_size >> 9);
}

static int
hs_be_write_prot(int unit)
{
	return hs_drive(unit)->write_prot != 0;
}

static int
hs_be_read(int unit, unsigned blk, unsigned char *buf)
{
	static unsigned char sec[2048];
	static unsigned sec_lba = ~0U;
	static int sec_unit = -1;

	if(g_hs_cue_open[unit]) {		/* a quarter of a sector */
		if(sec_unit != unit || sec_lba != blk / 4) {
			sec_unit = -1;
			if(cdimage_read_data(&g_hs_cue[unit], blk / 4, sec)
									!= 0) {
				return 0x27;
			}
			sec_unit = unit;
			sec_lba = blk / 4;
		}
		memcpy(buf, sec + (blk & 3) * 512, 512);
		hs_drive(unit)->dfcyc_last_read = g_cur_dfcyc;
		return 0;
	}
	return smartport_read_block(unit, blk, buf);
}

static int
hs_be_tracks(int unit)
{
	return g_hs_cue_open[unit] ? g_hs_cue[unit].ntracks : 0;
}

static void
hs_be_track(int unit, int n, unsigned *start, int *audio)
{
	Cd_image *cd = &g_hs_cue[unit];

	if(n >= 0 && n < cd->ntracks) {
		*start = cd->t[n].start;
		*audio = cd->t[n].audio;
	} else {
		*start = cd->total;
		*audio = 0;
	}
}

static int
hs_be_read_audio(int unit, unsigned lba, unsigned char *buf)
{
	if(!g_hs_cue_open[unit]) {
		return -1;
	}
	hs_drive(unit)->dfcyc_last_read = g_cur_dfcyc;	/* activity light */
	return cdimage_read_audio(&g_hs_cue[unit], lba, buf);
}

static int
hs_be_write(int unit, unsigned blk, const unsigned char *buf)
{
	return smartport_write_block(unit, blk, (byte *)buf);
}

/* A number for the disc in the drive, from its name and size */
static unsigned
hs_be_media(int unit)
{
	Disk	*dsk = hs_drive(unit);
	const char *p = dsk->name_ptr ? dsk->name_ptr : "";
	unsigned h = (unsigned)dsk->dimage_size * 2654435761U;

	while(*p) {
		h = (h ^ (unsigned char)*p++) * 16777619U;
	}
	return h + g_hs_inserts[unit] * 40503U;
}

static void
hs_be_eject(int unit)
{
	iwm_eject_disk_by_num(7, unit);
}

static const Scsi_block_backend g_hs_backend = {
	hs_be_present, hs_be_blocks, hs_be_write_prot, hs_be_read, hs_be_write,
	hs_be_media, hs_be_eject, hs_be_tracks, hs_be_track, hs_be_read_audio
};

/* 1 if the file name is that of a CD image */
int
hsscsi_is_cd_image(const char *name)
{
	static const char *ext[] = { ".iso", ".cdr", ".toast", ".cue", 0 };
	size_t	len, elen;
	int	i;

	if(!name) {
		return 0;
	}
	len = strlen(name);
	for(i = 0; ext[i]; i++) {
		elen = strlen(ext[i]);
		if(len > elen && !cfgcasecmp(name + len - elen, ext[i])) {
			return 1;
		}
	}
	return 0;
}

/* Decides for every drive whether a hard disk or a CD-ROM drive sits at its
 * SCSI ID, from what is in it. Called while the bus is free. at_reset: an
 * empty drive goes back to what it is at startup. */
static void
hs_update_devices(int at_reset)
{
	Disk	*dsk;
	int	i, cd;

	for(i = 0; i < HSSCSI_DISKS; i++) {
		dsk = hs_drive(i);
		cd = g_hs_is_cd[i];
		hs_cue_sync(i);
		if(dsk->fd >= 0) {
			cd = !dsk->dynapro_info_ptr &&
					hsscsi_is_cd_image(dsk->name_ptr);
			if(!g_hs_had_disk[i]) {
				g_hs_inserts[i]++;	/* a disc went in */
			}
			g_hs_had_disk[i] = 1;
		} else {
			g_hs_had_disk[i] = 0;
			if(at_reset) {
				cd = (i == HSSCSI_CD_UNIT);
			}
		}
		if(cd != g_hs_is_cd[i] || at_reset) {
			g_hs_is_cd[i] = cd;
			ncr5380_attach(HSSCSI_DISKS - 1 - i, cd ?
				&g_hs_cd[i].dev : &g_hs_disk[i].dev);
		}
	}
}

/* Adds the audio of the CD-ROM drives to num_samps stereo samples at
 * out (left, right; rate samples a second). zero_first: out holds nothing
 * yet. Returns 1 if a CD plays. Called by sound.c. */
int
hsscsi_audio_mix(int *out, int num_samps, int rate, int zero_first)
{
	static short buf[2 * 2048];
	static unsigned frac[HSSCSI_DISKS];	/* 16.16 position in buf */
	static short last[HSSCSI_DISKS][2];
	Scsi_cd	*k;
	unsigned step, pos;
	int	unit, i, need, any = 0;

	if(slot_card_get(7) != &g_hs_card || rate < 8000) {
		return 0;
	}
	step = (unsigned)((44100.0 / rate) * 65536.0);
	for(unit = 0; unit < HSSCSI_DISKS; unit++) {
		k = &g_hs_cd[unit];
		if(!g_hs_is_cd[unit] || !k->playing || k->paused) {
			continue;
		}
		if(!any && zero_first) {
			memset(out, 0, (size_t)num_samps * 2 * sizeof(int));
		}
		any = 1;
		/* the 44100 Hz samples this stretch of output takes */
		pos = frac[unit];
		need = (int)((pos + (unsigned)num_samps * step) >> 16);
		if(need > 2047) {
			need = 2047;
		}
		buf[0] = last[unit][0];
		buf[1] = last[unit][1];
		scsi_cd_audio(k, buf + 2, need);
		for(i = 0; i < num_samps; i++) {
			int	j = (int)(pos >> 16);

			if(j > need) {
				j = need;
			}
			out[2 * i] += buf[2 * j];
			out[2 * i + 1] += buf[2 * j + 1];
			pos += step;
		}
		last[unit][0] = buf[2 * need];
		last[unit][1] = buf[2 * need + 1];
		frac[unit] = pos - ((unsigned)need << 16);
	}
	return any;
}

/* 1 when drive unit (0 = S7D1) is a CD-ROM drive on the card: a disc put in
 * shows up without a restart. */
int
hsscsi_drive_is_cd(int unit)
{
	return slot_card_get(7) == &g_hs_card && unit >= 0 &&
				unit < HSSCSI_DISKS && g_hs_is_cd[unit];
}

/* True DMA (switch SW4 on): while the 53C80 asks for bytes, the card moves
 * them itself between the chip and IIgs memory, at the address in $C0n8/9
 * and in the bank of the IIgs's DMA register ($C037), without the 65816.
 * Address and count both count up: the count is loaded as a negative number
 * and the transfer ends when it reaches zero. When the address goes past the
 * end of the bank first, the card stops (bit 2 of $C0nC) and the firmware
 * moves on to the next bank and resumes with $C0nD. As in MAME's
 * a2hsscsi.cpp. The targets answer at once, so a transfer takes no IIgs
 * time. */
static void
hs_dma_run(void)
{
	word32	addr;
	unsigned start = g_hs_dma_bytes;

	while(g_hs_dma_active && ncr5380_drq()) {
		addr = ((g_c037_dmareg & 0xff) << 16) | (g_hs_dma_addr & 0xffff);
		if(g_hs_dma_in) {
			set_memory_c(addr, ncr5380_dma_read(), 0);
		} else {
			ncr5380_dma_write(get_memory_c(addr) & 0xff);
		}
		g_hs_dma_bytes++;
		g_hs_dma_addr = (g_hs_dma_addr + 1) & 0xffff;
		g_hs_dma_size = (g_hs_dma_size + 1) & 0xffff;
		if(g_hs_dma_size == 0) {
			g_hs_dma_active = 0;
			ncr5380_eop();
		} else if(g_hs_dma_addr == 0) {
			g_hs_dma_active = 0;
			g_hs_c0nc |= 0x04;
		}
	}
	if(g_scsi_trace && g_hs_dma_bytes != start) {
		printf("hs: true DMA %s, %u bytes, ends at %02x/%04x, at %.3f s\n",
			g_hs_dma_in ? "in" : "out", g_hs_dma_bytes - start,
			g_c037_dmareg & 0xff, g_hs_dma_addr,
			(double)(g_cur_dfcyc >> 16) / 1020484.0);
	}
}

static unsigned hs_io_read2(int reg, unsigned long long dfcyc);
static void hs_io_write2(int reg, unsigned val, unsigned long long dfcyc);

static unsigned
hs_io_read(int reg, unsigned long long dfcyc)
{
	unsigned val = hs_io_read2(reg, dfcyc);

	hs_dma_run();

	if(g_scsi_trace > 1) {
		printf("hs: R %x = %02x\n", reg, val);
	}
	return val;
}

static void
hs_io_write(int reg, unsigned val, unsigned long long dfcyc)
{
	if(g_scsi_trace > 1) {
		printf("hs: W %x = %02x\n", reg, val & 0xff);
	}
	hs_io_write2(reg, val, dfcyc);
	hs_dma_run();
}

static unsigned
hs_io_read2(int reg, unsigned long long dfcyc)
{
	unsigned val;

	switch(reg) {
	case 0x6:
		if(g_hs_c0nc & 1) {		/* pseudo-DMA */
			return ncr5380_dma_read();
		}
		return ncr5380_read(reg);
	case 0x8: return g_hs_dma_addr & 0xff;
	case 0x9: return (g_hs_dma_addr >> 8) & 0xff;
	case 0xa: return g_hs_dma_size & 0xff;
	case 0xb: return (g_hs_dma_size >> 8) & 0xff;
	case 0xc:
		/* bit 4: the watchdog still runs. Bit 6: switch SW4, true
		 * DMA. Reading clears bit 2 (DMA address rollover). */
		val = g_hs_c0nc | (g_hsscsi_dma ? 0x40 : 0);
		if(dfcyc < g_hs_watchdog_dfcyc) {
			val |= 0x10;
		}
		g_hs_c0nc &= ~0x04U;
		return val;
	case 0xd: return 0xff;
	case 0xe: return (g_hs_c0ne & 0x1f) | ((~HSSCSI_ID & 7) << 5);
	case 0xf: return g_hs_c0nf;
	}
	return ncr5380_read(reg);
}

static void
hs_io_write2(int reg, unsigned val, unsigned long long dfcyc)
{
	val &= 0xff;
	switch(reg) {
	case 0x0:
		if(g_hs_c0nc & 1) {		/* pseudo-DMA */
			ncr5380_dma_write(val);
		} else {
			ncr5380_write(reg, val);
		}
		break;
	case 0x1: case 0x2: case 0x3: case 0x4: case 0x5: case 0x6: case 0x7:
		g_hs_scanned = 1;
		if(reg == 1 && ncr5380_busy_id() < 0) {
			hs_update_devices(0);	/* before a selection */
		}
		if(reg >= 5) {			/* a DMA transfer starts */
			g_hs_dma_in = (reg != 5);
			if(g_hs_c0nc & 0x02) {
				g_hs_dma_active = 1;
			}
		}
		ncr5380_write(reg, val);
		break;
	case 0x8: g_hs_dma_addr = (g_hs_dma_addr & 0xff00) | val; break;
	case 0x9: g_hs_dma_addr = (g_hs_dma_addr & 0xff) | (val << 8); break;
	case 0xa: g_hs_dma_size = (g_hs_dma_size & 0xff00) | val; break;
	case 0xb: g_hs_dma_size = (g_hs_dma_size & 0xff) | (val << 8); break;
	case 0xc:
		g_hs_c0nc = (g_hs_c0nc & ~0x2bU) | (val & 0x2b);
		if(!(g_hs_c0nc & 0x02)) {
			g_hs_dma_active = 0;
		}
		break;
	case 0xd:
		if((val & 1) && (g_hs_c0nc & 0x02) && g_hs_dma_size &&
							!(g_hs_c0nc & 0x04)) {
			g_hs_dma_active = 1;	/* resume after a rollover */
		}
		if(val & 2) {
			ncr5380_reset();
		}
		break;
	case 0xe:
		/* pulsing bit 7 low starts the watchdog again */
		if((g_hs_c0ne & 0x80) && !(val & 0x80)) {
			g_hs_watchdog_dfcyc = dfcyc +
						(HSSCSI_WATCHDOG_CYCS << 16);
		}
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

/* Reads the card ROM: a 32 KB file, or a zip file with 341-0803.bin in it
 * (MAME's a2hsscsi.zip; romfile.c). 0 when it worked. */
static int
hs_read_rom(const char *path)
{
	if(rom_file_read(path, "341-0803.bin", g_hs_rom, HSSCSI_ROM_SIZE) != 0) {
		return -1;
	}
	printf("hsscsi: card ROM %s\n", path);
	return 0;
}

static char g_hs_iigs_rom[1024];	/* the IIgs ROM in use: where to look */

/* Looks for the card ROM and reads it: the file of the setting, then
 * 341-0803.bin and a2hsscsi.zip next to the IIgs ROM. 1 when found. */
static int
hs_find_rom(const char *iigs_rom)
{
	char	path[1024];
	const char *slash;
	int	dirlen;

	if(g_hsscsi_rom && g_hsscsi_rom[0] && hs_read_rom(g_hsscsi_rom) == 0) {
		return 1;
	}
	if(!iigs_rom || !iigs_rom[0]) {
		return 0;
	}
	slash = strrchr(iigs_rom, '/');
	dirlen = slash ? (int)(slash - iigs_rom + 1) : 0;
	snprintf(path, sizeof(path), "%.*s341-0803.bin", dirlen, iigs_rom);
	if(hs_read_rom(path) == 0) {
		return 1;
	}
	snprintf(path, sizeof(path), "%.*sa2hsscsi.zip", dirlen, iigs_rom);
	return hs_read_rom(path) == 0;
}

/* For the Slots window: 1 when the card's ROM is there */
int
hsscsi_rom_found(void)
{
	return slot_card_get(7) == &g_hs_card || hs_find_rom(g_hs_iigs_rom);
}

/* 1 when path is a ROM file of the card (32 KB, or a zip with it) */
int
hsscsi_rom_file_ok(const char *path)
{
	return hs_read_rom(path) == 0;
}

/* Puts the card into slot 7 (on = 1) or takes it out. iigs_rom is the path
 * of the IIgs ROM in use: the card ROM (341-0803.bin or a2hsscsi.zip) is
 * looked for next to it when no path is set. Returns 0 when the card is in, -1 when its ROM was not found. */
int
hsscsi_attach(int on, const char *iigs_rom)
{
	int	i;

	if(iigs_rom) {
		snprintf(g_hs_iigs_rom, sizeof(g_hs_iigs_rom), "%s", iigs_rom);
	}
	if(!on) {
		if(slot_card_get(7) == &g_hs_card) {
			slot_card_set(7, 0);
		}
		return 0;
	}
	g_hs_rom_ok = hs_find_rom(iigs_rom);
	if(!g_hs_rom_ok) {
		printf("hsscsi: no card ROM (341-0803.bin, 32 KB, or "
							"a2hsscsi.zip)\n");
		slot_card_set(7, 0);
		g_hsscsi_rom_missing = 1;
		return -1;
	}
	g_hsscsi_rom_missing = 0;
	memset(g_hs_ram, 0, sizeof(g_hs_ram));
	ncr5380_init(HSSCSI_ID);
	g_scsi_trace = getenv("EMUBUDDY_SCSI_TRACE") ?
				atoi(getenv("EMUBUDDY_SCSI_TRACE")) : 0;
	for(i = 0; i < HSSCSI_DISKS; i++) {
		scsi_disk_init(&g_hs_disk[i], i, &g_hs_backend);
		scsi_cd_init(&g_hs_cd[i], i, &g_hs_backend);
		g_hs_is_cd[i] = 0;
	}
	slot_card_set(7, &g_hs_card);
	return 0;
}
