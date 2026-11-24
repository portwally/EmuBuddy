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
static dword64	g_hs_watchdog_dfcyc;	/* the watchdog runs until then */
static Scsi_disk g_hs_disk[HSSCSI_DISKS];
static Scsi_cd	g_hs_cd[HSSCSI_DISKS];
static int	g_hs_is_cd[HSSCSI_DISKS];	/* the drive is a CD-ROM drive */
#define HSSCSI_CD_UNIT	(HSSCSI_DISKS - 1)	/* S7D7: a CD drive when empty */
static unsigned g_hs_inserts[HSSCSI_DISKS];	/* discs put in, counted */
static int	g_hs_had_disk[HSSCSI_DISKS];
static Slot_card g_hs_card;
static void	hs_update_devices(int at_reset);
static int	g_hs_scanned;	/* the firmware used the 53C80 since the reset */

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
	g_hs_watchdog_dfcyc = 0;
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

static unsigned
hs_be_blocks(int unit)
{
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
	return smartport_read_block(unit, blk, buf);
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
	hs_be_media, hs_be_eject
};

/* 1 if the file name is that of a CD image */
int
hsscsi_is_cd_image(const char *name)
{
	static const char *ext[] = { ".iso", ".cdr", ".toast", 0 };
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

/* 1 when drive unit (0 = S7D1) is a CD-ROM drive on the card: a disc put in
 * shows up without a restart. */
int
hsscsi_drive_is_cd(int unit)
{
	return slot_card_get(7) == &g_hs_card && unit >= 0 &&
				unit < HSSCSI_DISKS && g_hs_is_cd[unit];
}

static unsigned hs_io_read2(int reg, unsigned long long dfcyc);
static void hs_io_write2(int reg, unsigned val, unsigned long long dfcyc);

static unsigned
hs_io_read(int reg, unsigned long long dfcyc)
{
	unsigned val = hs_io_read2(reg, dfcyc);

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
		/* bit 4: the watchdog still runs. Bit 6 (SW4, true DMA) is
		 * off. Reading clears bit 2 (DMA address rollover). */
		val = g_hs_c0nc;
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
		ncr5380_write(reg, val);
		break;
	case 0x8: g_hs_dma_addr = (g_hs_dma_addr & 0xff00) | val; break;
	case 0x9: g_hs_dma_addr = (g_hs_dma_addr & 0xff) | (val << 8); break;
	case 0xa: g_hs_dma_size = (g_hs_dma_size & 0xff00) | val; break;
	case 0xb: g_hs_dma_size = (g_hs_dma_size & 0xff) | (val << 8); break;
	case 0xc: g_hs_c0nc = (g_hs_c0nc & ~0x2bU) | (val & 0x2b); break;
	case 0xd:
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

/* Reads 341-0803.bin out of a zip file (MAME's a2hsscsi.zip). 0 when it
 * worked. The zip directory is read here: EmuBuddy's zip reader for disk
 * images (cfg_partition_make_list) leaves out files under 140 KB. */
static int
hs_read_rom_zip(const char *path)
{
	static const char want[] = "341-0803.bin";
	Disk	tmp;
	byte	buf[1024], *dir, *e;
	dword64	dsize, dlen, dir_off, dir_size, off, local;
	word32	entries, method, csize, usize, name_len, hdr_len, i, j;
	int	fd, pos, ret = -1;

	fd = open(path, O_RDONLY | O_BINARY, 0x1b6);
	if(fd < 0) {
		return -1;
	}
	dsize = cfg_get_fd_size(fd);
	dlen = (dsize > sizeof(buf)) ? sizeof(buf) : dsize;
	dir = 0;
	if(dlen < 22 || cfg_read_from_fd(fd, buf, dsize - dlen, dlen) != dlen) {
		goto done;
	}
	/* the "end of central directory" record is the last one in the file */
	for(pos = (int)dlen - 22; pos >= 0; pos--) {
		if(buf[pos] == 'P' && buf[pos + 1] == 'K' &&
				buf[pos + 2] == 5 && buf[pos + 3] == 6) {
			break;
		}
	}
	if(pos < 0) {
		goto done;
	}
	entries = cfg_get_le16(&buf[pos + 10]);
	dir_size = cfg_get_le32(&buf[pos + 12]);
	dir_off = cfg_get_le32(&buf[pos + 16]);
	if(dir_size > (1 << 20) || (dir_off + dir_size) > dsize) {
		goto done;
	}
	dir = malloc((size_t)dir_size + 1);
	if(!dir || cfg_read_from_fd(fd, dir, dir_off, dir_size) != dir_size) {
		goto done;
	}
	for(i = 0, off = 0; i < entries && (off + 46) <= dir_size; i++) {
		e = dir + off;
		if(e[0] != 'P' || e[1] != 'K' || e[2] != 1 || e[3] != 2) {
			break;
		}
		method = cfg_get_le16(e + 10);
		csize = cfg_get_le32(e + 20);
		usize = cfg_get_le32(e + 24);
		name_len = cfg_get_le16(e + 28);
		hdr_len = 46 + name_len + cfg_get_le16(e + 30) +
							cfg_get_le16(e + 32);
		local = cfg_get_le32(e + 42);
		if((off + hdr_len) > dir_size) {
			break;
		}
		off += hdr_len;
		if(usize != HSSCSI_ROM_SIZE || name_len < sizeof(want) - 1) {
			continue;
		}
		/* the name ends in 341-0803.bin, in any case */
		e += 46 + name_len - (sizeof(want) - 1);
		for(j = 0; j < sizeof(want) - 1; j++) {
			if(tolower(e[j]) != want[j]) {
				break;
			}
		}
		if(j < sizeof(want) - 1) {
			continue;
		}
		if(method == 0) {		/* stored */
			if(cfg_read_from_fd(fd, buf, local, 30) != 30) {
				break;
			}
			local += 30 + cfg_get_le16(&buf[26]) +
							cfg_get_le16(&buf[28]);
			if(cfg_read_from_fd(fd, g_hs_rom, local,
				HSSCSI_ROM_SIZE) == HSSCSI_ROM_SIZE) {
				ret = 0;
			}
		} else if(method == 8) {	/* deflate */
			memset(&tmp, 0, sizeof(tmp));
			tmp.fd = -1;
			if(undeflate_zipfile(&tmp, fd, local, usize, csize)
					== 0 && tmp.raw_data &&
					tmp.raw_dsize == HSSCSI_ROM_SIZE) {
				memcpy(g_hs_rom, tmp.raw_data, HSSCSI_ROM_SIZE);
				ret = 0;
			}
			free(tmp.raw_data);
		}
		break;
	}
done:
	free(dir);
	close(fd);
	return ret;
}

/* Reads the card ROM: a 32 KB file, or a zip file with 341-0803.bin in it.
 * 0 when it worked. */
static int
hs_read_rom(const char *path)
{
	FILE	*f;
	long	len;
	size_t	plen = strlen(path);

	f = fopen(path, "rb");
	if(!f) {
		return -1;
	}
	if(plen > 4 && !cfgcasecmp(path + plen - 4, ".zip")) {
		fclose(f);
		if(hs_read_rom_zip(path) != 0) {
			return -1;
		}
		printf("hsscsi: card ROM %s\n", path);
		return 0;
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
 * of the IIgs ROM in use: the card ROM (341-0803.bin or a2hsscsi.zip) is
 * looked for next to it when no path is set. Returns 0 when the card is in, -1 when its ROM was not found. */
int
hsscsi_attach(int on, const char *iigs_rom)
{
	char	path[1024];
	const char *slash;
	int	dirlen, i;

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
		if(!g_hs_rom_ok) {
			snprintf(path, sizeof(path), "%.*sa2hsscsi.zip",
							dirlen, iigs_rom);
			g_hs_rom_ok = hs_read_rom(path) == 0;
		}
	}
	if(!g_hs_rom_ok) {
		printf("hsscsi: no card ROM (341-0803.bin, 32 KB, or "
							"a2hsscsi.zip)\n");
		slot_card_set(7, 0);
		return -1;
	}
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
