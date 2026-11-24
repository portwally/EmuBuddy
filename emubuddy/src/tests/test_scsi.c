/* Host test for the NCR 53C80 and the SCSI hard disk (ncr5380.c,
 * scsi_disk.c). It drives the chip's registers the way card firmware does:
 * arbitration, selection, programmed I/O and pseudo-DMA. Two disks sit in
 * memory at SCSI IDs 0 and 2; ID 1 is empty.
 *   cmake --build build --target test_scsi && build/test_scsi */

#include "../scsi.h"
#include "../cdimage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define OWN_ID	7
#define NBLK	64

static unsigned char	g_img[3][NBLK * 512];
static int		g_wp[3];
static int		g_fails;

#define CHECK(c, ...) do { if(!(c)) { printf("FAIL %s:%d: ", __FILE__, \
	__LINE__); printf(__VA_ARGS__); printf("\n"); g_fails++; } } while(0)

/* memory backend: units 0 and 2 have a disk */
static int be_present(int u) { return u == 0 || u == 2; }
static unsigned be_blocks(int u) { (void)u; return NBLK; }
static int be_wp(int u) { return g_wp[u]; }
static int
be_read(int u, unsigned blk, unsigned char *buf)
{
	memcpy(buf, &g_img[u][blk * 512], 512);
	return 0;
}
static int
be_write(int u, unsigned blk, const unsigned char *buf)
{
	memcpy(&g_img[u][blk * 512], buf, 512);
	return 0;
}
static const Scsi_block_backend g_be = {
	be_present, be_blocks, be_wp, be_read, be_write
};

/* the CD-ROM drive: unit 1 of the images, with a disc that can be changed */
static int	g_cd_in, g_cd_gen, g_cd_ejects;
static int cd_be_present(int u) { (void)u; return g_cd_in; }
static unsigned cd_be_media(int u) { (void)u; return (unsigned)g_cd_gen * 2; }
static void cd_be_eject(int u) { (void)u; g_cd_in = 0; g_cd_ejects++; }
static const Scsi_block_backend g_cd_be = {
	cd_be_present, be_blocks, be_wp, be_read, be_write, cd_be_media,
	cd_be_eject
};

/* a CD with tracks, from a .cue sheet this test writes: unit 0 of g_cue_be */
static Cd_image	g_cue;
static int cue_present(int u) { (void)u; return 1; }
static unsigned cue_blocks(int u) { (void)u; return g_cue.total * 4; }
static int cue_wp(int u) { (void)u; return 1; }
static int
cue_read(int u, unsigned blk, unsigned char *buf)
{
	unsigned char sec[2048];

	(void)u;
	if(cdimage_read_data(&g_cue, blk / 4, sec) != 0) {
		return 1;
	}
	memcpy(buf, sec + (blk & 3) * 512, 512);
	return 0;
}
static int cue_tracks(int u) { (void)u; return g_cue.ntracks; }
static void
cue_track(int u, int n, unsigned *start, int *audio)
{
	(void)u;
	*start = n < g_cue.ntracks ? g_cue.t[n].start : g_cue.total;
	*audio = n < g_cue.ntracks ? g_cue.t[n].audio : 0;
}
static int
cue_read_audio(int u, unsigned lba, unsigned char *buf)
{
	(void)u;
	return cdimage_read_audio(&g_cue, lba, buf);
}
static const Scsi_block_backend g_cue_be = {
	cue_present, cue_blocks, cue_wp, cue_read, 0, 0, 0,
	cue_tracks, cue_track, cue_read_audio
};

/* The audio sample at place n (0 left, 1 right, 2 left ...) of track t */
static short
tone(int t, unsigned n)
{
	return (short)((n * 7 + (unsigned)t * 1000) & 0x7fff);
}

/* Writes a CD for the test into the current folder: a data track of 40
 * sectors (2352 bytes each, Mode 1), an audio track of 30 sectors in the
 * same file, and an audio track of 20 sectors in a WAVE file. */
static void
make_test_cd(void)
{
	static unsigned char sec[2352], hdr[44];
	FILE	*f;
	unsigned i, j, n;
	short	v;

	f = fopen("test_scsi_cd.bin", "wb");
	for(i = 0; i < 40; i++) {
		memset(sec, 0xaa, sizeof(sec));
		for(j = 0; j < 2048; j++) {
			sec[16 + j] = (unsigned char)(i * 3 + j);
		}
		fwrite(sec, 1, sizeof(sec), f);
	}
	for(n = 0; n < 30 * 1176; n++) {
		v = tone(2, n);
		fputc(v & 0xff, f);
		fputc((v >> 8) & 0xff, f);
	}
	fclose(f);
	f = fopen("test_scsi_cd.wav", "wb");
	memset(hdr, 0, sizeof(hdr));
	memcpy(hdr, "RIFF", 4);
	memcpy(hdr + 8, "WAVEfmt ", 8);
	hdr[16] = 16;
	memcpy(hdr + 36, "data", 4);
	n = 20 * 2352;
	hdr[40] = n & 0xff;
	hdr[41] = (n >> 8) & 0xff;
	hdr[42] = (n >> 16) & 0xff;
	fwrite(hdr, 1, sizeof(hdr), f);
	for(n = 0; n < 20 * 1176; n++) {
		v = tone(3, n);
		fputc(v & 0xff, f);
		fputc((v >> 8) & 0xff, f);
	}
	fclose(f);
	f = fopen("test_scsi_cd.cue", "w");
	fputs("FILE \"test_scsi_cd.bin\" BINARY\n  TRACK 01 MODE1/2352\n"
		"    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n"
		"    INDEX 00 00:00:38\n    INDEX 01 00:00:40\n"
		"FILE \"test_scsi_cd.wav\" WAVE\n  TRACK 03 AUDIO\n"
		"    INDEX 01 00:00:00\n", f);
	fclose(f);
}

/* ---- initiator, as card firmware does it ---- */
#define RD(r)		ncr5380_read(r)
#define WR(r, v)	ncr5380_write(r, v)

static int
select_id(int id, int atn)
{
	WR(0, 1 << OWN_ID);
	WR(2, 0x01);				/* arbitrate */
	if(!(RD(1) & 0x40)) {
		return -1;			/* no AIP */
	}
	WR(1, 0x0c | 0x01);			/* SEL BSY DBUS */
	WR(2, 0x00);
	WR(0, (1 << OWN_ID) | (1 << id));
	WR(1, 0x04 | 0x01 | (atn ? 0x02 : 0));	/* release BSY: selection */
	if(!(RD(4) & 0x40)) {
		WR(1, 0);
		return -1;			/* no BSY: nobody there */
	}
	WR(1, atn ? 0x02 : 0);			/* release SEL and DBUS */
	return 0;
}

static int
wait_req(int phase)
{
	unsigned s = RD(4);

	if(!(s & 0x20)) {
		return -1;
	}
	if(((s >> 2) & 7) != (unsigned)phase) {
		return -2;
	}
	WR(3, phase);
	return 0;
}

static int
pio_out(int phase, unsigned v, int keep_atn)
{
	int r = wait_req(phase);

	if(r) {
		return r;
	}
	WR(0, v);
	WR(1, 0x01 | 0x10 | (keep_atn ? 0x02 : 0));	/* DBUS ACK */
	WR(1, keep_atn ? 0x02 : 0);
	return 0;
}

static int
pio_in(int phase)
{
	unsigned v;
	int	r = wait_req(phase);

	if(r) {
		return r - 1000;
	}
	v = RD(0);
	WR(1, 0x10);
	WR(1, 0);
	return (int)v;
}

/* Sends a command with an IDENTIFY message first; returns 0 or -1 */
static int
command(int id, const unsigned char *cdb, int len)
{
	int	i;

	if(select_id(id, 1) != 0) {
		return -1;
	}
	if(pio_out(6, 0x80, 0) != 0) {		/* IDENTIFY, LUN 0 */
		return -1;
	}
	for(i = 0; i < len; i++) {
		if(pio_out(2, cdb[i], 0) != 0) {
			return -1;
		}
	}
	return 0;
}

/* Status and message in; returns the status byte, -1 on a bad end */
static int
finish(void)
{
	int st = pio_in(3);
	int msg = pio_in(7);

	if(st < 0 || msg != 0) {
		return -1;
	}
	if(RD(4) & 0x40) {
		return -2;			/* bus still busy */
	}
	return st;
}

static int
dma_in(unsigned char *buf, int max)
{
	int	n = 0;

	WR(3, 1);			/* data in */
	WR(2, 0x02);			/* DMA mode */
	WR(7, 0);			/* start DMA initiator receive */
	while(n < max && (RD(5) & 0x40)) {
		buf[n++] = ncr5380_dma_read();
	}
	return n;
}

static int
dma_out(const unsigned char *buf, int len)
{
	int	n = 0;

	WR(3, 0);
	WR(2, 0x02);
	WR(1, 0x01);			/* DBUS */
	WR(5, 0);			/* start DMA send */
	while(n < len && (RD(5) & 0x40)) {
		ncr5380_dma_write(buf[n++]);
	}
	return n;
}

static void
dma_end(void)
{
	WR(2, 0);
	WR(1, 0);
	RD(7);
}

int
main(void)
{
	static Scsi_disk disk[3];
	unsigned char cdb[10], buf[4096], wbuf[1536];
	int	i, n, st;

	for(i = 0; i < NBLK * 512; i++) {
		g_img[0][i] = (unsigned char)(i * 7 + i / 512);
		g_img[2][i] = 0xee;
	}
	ncr5380_init(OWN_ID);
	ncr5380_reset();
	for(i = 0; i < 3; i++) {
		scsi_disk_init(&disk[i], i, &g_be);
		ncr5380_attach(i, &disk[i].dev);
	}

	/* 1. selection: ID 1 has no disk, ID 5 no device */
	CHECK(select_id(1, 0) != 0, "ID 1 (empty drive) answered");
	CHECK(select_id(5, 0) != 0, "ID 5 (nothing) answered");
	CHECK(!(RD(4) & 0x40), "bus busy after failed selections");

	/* 2. INQUIRY by programmed I/O */
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x12;
	cdb[4] = 36;
	CHECK(command(0, cdb, 6) == 0, "INQUIRY not taken");
	for(i = 0; i < 36; i++) {
		n = pio_in(1);
		if(n < 0) {
			break;
		}
		buf[i] = (unsigned char)n;
	}
	CHECK(i == 36, "INQUIRY gave %d bytes", i);
	CHECK(buf[0] == 0 && !memcmp(buf + 8, "EMUBUDDY", 8),
						"INQUIRY data wrong");
	CHECK(finish() == 0, "INQUIRY status not GOOD");

	/* 3. READ CAPACITY, without IDENTIFY */
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x25;
	CHECK(select_id(0, 0) == 0, "select without ATN failed");
	for(i = 0; i < 10; i++) {
		CHECK(pio_out(2, cdb[i], 0) == 0, "command byte %d", i);
	}
	for(i = 0; i < 8; i++) {
		buf[i] = (unsigned char)pio_in(1);
	}
	CHECK(buf[3] == NBLK - 1 && buf[6] == 2 && buf[7] == 0,
		"capacity %02x%02x%02x%02x / %02x%02x", buf[0], buf[1], buf[2],
		buf[3], buf[6], buf[7]);
	CHECK(finish() == 0, "READ CAPACITY status");

	/* 4. READ(10) of 3 blocks by pseudo-DMA */
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x28;
	cdb[5] = 10;
	cdb[8] = 3;
	CHECK(command(0, cdb, 10) == 0, "READ(10) not taken");
	n = dma_in(buf, sizeof(buf));
	CHECK(n == 1536, "READ(10) moved %d bytes", n);
	CHECK(!memcmp(buf, &g_img[0][10 * 512], 1536), "READ(10) data wrong");
	CHECK(ncr5380_irq(), "no interrupt at the phase change");
	CHECK(!(RD(5) & 0x08), "phase still matches after the data");
	dma_end();
	CHECK(finish() == 0, "READ(10) status");

	/* 5. WRITE(10) of 3 blocks to ID 2, read back with READ(6) */
	for(i = 0; i < 1536; i++) {
		wbuf[i] = (unsigned char)(i ^ 0x5a);
	}
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x2a;
	cdb[5] = 20;
	cdb[8] = 3;
	CHECK(command(2, cdb, 10) == 0, "WRITE(10) not taken");
	n = dma_out(wbuf, 1536);
	CHECK(n == 1536, "WRITE(10) moved %d bytes", n);
	dma_end();
	CHECK(finish() == 0, "WRITE(10) status");
	CHECK(!memcmp(&g_img[2][20 * 512], wbuf, 1536), "image not written");
	CHECK(g_img[2][23 * 512] == 0xee, "wrote past the end");
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x08;
	cdb[3] = 20;
	cdb[4] = 3;
	CHECK(command(2, cdb, 6) == 0, "READ(6) not taken");
	n = dma_in(buf, sizeof(buf));
	dma_end();
	CHECK(n == 1536 && !memcmp(buf, wbuf, 1536), "READ(6) back: %d", n);
	CHECK(finish() == 0, "READ(6) status");

	/* 6. reading past the end: CHECK CONDITION, then REQUEST SENSE */
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x08;
	cdb[3] = NBLK - 1;
	cdb[4] = 2;
	CHECK(command(0, cdb, 6) == 0, "READ past end not taken");
	CHECK(finish() == SCSI_CHECK_CONDITION, "no CHECK CONDITION");
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x03;
	cdb[4] = 18;
	CHECK(command(0, cdb, 6) == 0, "REQUEST SENSE not taken");
	for(i = 0; i < 18; i++) {
		buf[i] = (unsigned char)pio_in(1);
	}
	CHECK(finish() == 0, "REQUEST SENSE status");
	CHECK(buf[0] == 0x70 && (buf[2] & 0x0f) == SENSE_ILLEGAL_REQUEST &&
		buf[12] == 0x21, "sense %02x %02x %02x", buf[0], buf[2],
		buf[12]);

	/* 7. write protection */
	g_wp[0] = 1;
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x0a;
	cdb[4] = 1;
	CHECK(command(0, cdb, 6) == 0, "WRITE(6) not taken");
	st = finish();
	CHECK(st == SCSI_CHECK_CONDITION, "write-protected write: %d", st);
	CHECK(disk[0].dev.sense_key == SENSE_DATA_PROTECT, "sense key %d",
						disk[0].dev.sense_key);
	g_wp[0] = 0;

	/* 8. unknown command, and MODE SENSE */
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0xe5;
	CHECK(command(0, cdb, 6) == 0 && finish() == SCSI_CHECK_CONDITION,
						"unknown command not refused");
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x1a;
	cdb[2] = 0x3f;
	cdb[4] = 255;
	CHECK(command(0, cdb, 6) == 0, "MODE SENSE not taken");
	for(n = 0; n < 255; n++) {
		st = pio_in(1);
		if(st < 0) {
			break;
		}
		buf[n] = (unsigned char)st;
	}
	CHECK(n == buf[0] + 1 && n == 12 + 8 + 24 + 24, "MODE SENSE %d bytes",
									n);
	CHECK(finish() == 0, "MODE SENSE status");

	/* 9. bus reset in the middle of a command */
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x28;
	cdb[8] = 1;
	CHECK(command(0, cdb, 10) == 0, "READ before reset");
	WR(1, 0x80);
	WR(1, 0);
	CHECK(!(RD(4) & 0x40), "bus busy after reset");
	CHECK(ncr5380_irq(), "no interrupt after reset");
	RD(7);
	CHECK(!ncr5380_irq(), "interrupt not cleared");
	CHECK(select_id(0, 0) == 0, "no selection after reset");
	WR(1, 0x80);
	WR(1, 0);
	RD(7);

	/* 10. The firmware of Apple's High-Speed SCSI Card leaves the DMA mode
	 * bit on after the data and takes status and message by hand */
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x08;
	cdb[3] = 5;
	cdb[4] = 1;
	CHECK(command(0, cdb, 6) == 0, "READ(6) not taken");
	n = dma_in(buf, sizeof(buf));
	CHECK(n == 512 && !memcmp(buf, &g_img[0][5 * 512], 512),
						"READ(6) moved %d bytes", n);
	WR(2, 0);
	WR(2, 0x02);			/* DMA mode, no transfer started */
	WR(3, 3);
	CHECK((RD(4) & 0x7c) == 0x6c, "no status phase: %02x", RD(4));
	st = (int)RD(0);
	WR(1, 0x11);			/* ACK */
	CHECK(!(RD(4) & 0x20), "REQ stays up: ACK ignored in DMA mode");
	WR(1, 0x01);
	WR(3, 7);
	CHECK((RD(4) & 0x7c) == 0x7c, "no message in phase: %02x", RD(4));
	WR(1, 0x11);
	WR(1, 0x01);
	WR(1, 0);
	WR(2, 0);
	CHECK(st == 0 && !(RD(4) & 0x40), "status %d, bus %02x", st, RD(4));
	RD(7);

	/* 10a. The status byte is read with "assert data bus" still on and
	 * a non-zero byte in the output register, as Apple's firmware does
	 * after a command whose last byte is not zero */
	memset(cdb, 0, sizeof(cdb));		/* TEST UNIT READY */
	cdb[5] = 0x80;
	CHECK(command(0, cdb, 6) == 0, "TUR not taken");
	WR(3, 3);
	WR(1, 0x01);				/* DBUS on, 0x80 in the ODR */
	CHECK(RD(0) == 0, "status read as %02x with DBUS on", RD(0));
	WR(1, 0);
	CHECK(finish() == 0, "TUR status");

	/* 10b. The card's DMA counter runs out (EOP) in the middle of the
	 * data: the chip stops asking for bytes and says "end of DMA" */
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x08;
	cdb[3] = 5;
	cdb[4] = 1;
	CHECK(command(0, cdb, 6) == 0, "READ(6) not taken");
	WR(3, 1);
	WR(2, 0x02);
	WR(7, 0);
	for(n = 0; n < 100 && (RD(5) & 0x40); n++) {
		buf[n] = (unsigned char)ncr5380_dma_read();
	}
	CHECK(n == 100, "DMA gave %d of 100 bytes", n);
	ncr5380_eop();
	CHECK(!ncr5380_drq() && (RD(5) & 0x80), "EOP: bus status %02x", RD(5));
	WR(1, 0x80);			/* give the command up: bus reset */
	WR(1, 0);
	WR(2, 0);
	RD(7);

	/* 11. CD-ROM drive at ID 4: empty, then a disc, block sizes, eject */
	{
		static Scsi_cd cd;

		for(i = 0; i < NBLK * 512; i++) {
			g_img[1][i] = (unsigned char)(i * 3 + i / 2048);
		}
		scsi_cd_init(&cd, 1, &g_cd_be);
		ncr5380_attach(4, &cd.dev);
		memset(cdb, 0, sizeof(cdb));
		cdb[0] = 0x12;
		cdb[4] = 36;
		CHECK(command(4, cdb, 6) == 0, "empty CD drive not on the bus");
		for(i = 0; i < 36; i++) {
			buf[i] = (unsigned char)pio_in(1);
		}
		CHECK(finish() == 0 && buf[0] == 5 && (buf[1] & 0x80) &&
			!memcmp(buf + 8, "SONY    CD-ROM CDU-8002", 23),
			"CD INQUIRY wrong");
		memset(cdb, 0, sizeof(cdb));		/* TEST UNIT READY */
		CHECK(command(4, cdb, 6) == 0 &&
			finish() == SCSI_CHECK_CONDITION &&
			cd.dev.sense_key == SENSE_NOT_READY &&
			cd.dev.asc == 0xb0, "empty drive: sense %x/%02x",
			cd.dev.sense_key, cd.dev.asc);
		cdb[0] = 0x25;		/* the empty drive gives a size too */
		CHECK(command(4, cdb, 10) == 0, "READ CAPACITY, empty");
		for(i = 0; i < 8; i++) {
			buf[i] = (unsigned char)pio_in(1);
		}
		CHECK(finish() == 0 && (buf[1] || buf[2] >= 0x40),
						"empty drive: no size");
		memset(cdb, 0, sizeof(cdb));
		g_cd_in = 1;				/* a disc goes in */
		g_cd_gen = 1;
		CHECK(command(4, cdb, 6) == 0 &&
			finish() == SCSI_CHECK_CONDITION &&
			cd.dev.sense_key == SENSE_UNIT_ATTENTION &&
			cd.dev.asc == 0x28, "new disc: sense %x/%02x",
			cd.dev.sense_key, cd.dev.asc);
		CHECK(command(4, cdb, 6) == 0 && finish() == 0,
						"disc not ready the 2nd time");
		/* READ CAPACITY: a small disc says 32 MB, so that GS/OS's
		 * driver allows itself READ(10) */
		cdb[0] = 0x25;
		CHECK(command(4, cdb, 10) == 0, "CD READ CAPACITY");
		for(i = 0; i < 8; i++) {
			buf[i] = (unsigned char)pio_in(1);
		}
		CHECK(finish() == 0 && buf[1] == 0 && buf[2] == 0x3f &&
			buf[3] == 0xff && buf[6] == 8,
			"CD capacity %02x%02x%02x blocks of %d", buf[1],
			buf[2], buf[3], buf[6] << 8);
		memset(cdb, 0, sizeof(cdb));
		cdb[0] = 0x28;				/* 2 sectors at 3 */
		cdb[5] = 3;
		cdb[8] = 2;
		CHECK(command(4, cdb, 10) == 0, "CD READ(10)");
		n = dma_in(buf, sizeof(buf));
		dma_end();
		CHECK(n == 4096 && !memcmp(buf, &g_img[1][3 * 2048], 4096),
					"CD read of 2048-byte blocks: %d", n);
		CHECK(finish() == 0, "CD READ(10) status");
		memset(cdb, 0, sizeof(cdb));		/* 512-byte blocks */
		cdb[0] = 0x15;
		cdb[4] = 12;
		memset(wbuf, 0, 12);
		wbuf[3] = 8;
		wbuf[10] = 2;
		CHECK(command(4, cdb, 6) == 0, "CD MODE SELECT");
		for(i = 0; i < 12; i++) {
			CHECK(pio_out(0, wbuf[i], 0) == 0, "mode byte %d", i);
		}
		CHECK(finish() == 0, "CD MODE SELECT status");
		memset(cdb, 0, sizeof(cdb));
		cdb[0] = 0x08;
		cdb[3] = 9;
		cdb[4] = 1;
		CHECK(command(4, cdb, 6) == 0, "CD READ(6)");
		n = dma_in(buf, sizeof(buf));
		dma_end();
		CHECK(n == 512 && !memcmp(buf, &g_img[1][9 * 512], 512),
					"CD read of a 512-byte block: %d", n);
		CHECK(finish() == 0, "CD READ(6) status");
		cdb[0] = 0x0a;				/* a CD is read-only */
		CHECK(command(4, cdb, 6) == 0 &&
			finish() == SCSI_CHECK_CONDITION, "CD took a write");
		memset(cdb, 0, sizeof(cdb));		/* takes that sense */
		cdb[0] = 0x03;
		cdb[4] = 16;
		CHECK(command(4, cdb, 6) == 0, "CD REQUEST SENSE");
		for(i = 0; i < 16; i++) {
			buf[i] = (unsigned char)pio_in(1);
		}
		CHECK(finish() == 0 && (buf[2] & 15) == SENSE_DATA_PROTECT,
						"sense after the write");
		g_cd_gen = 2;				/* another disc */
		memset(cdb, 0, sizeof(cdb));		/* GS/OS asks by sense */
		cdb[0] = 0x03;
		cdb[4] = 16;
		CHECK(command(4, cdb, 6) == 0, "CD REQUEST SENSE");
		for(i = 0; i < 16; i++) {
			buf[i] = (unsigned char)pio_in(1);
		}
		CHECK(finish() == 0 && (buf[2] & 15) == SENSE_UNIT_ATTENTION &&
			buf[12] == 0x28, "disc change not in the sense data");
		memset(cdb, 0, sizeof(cdb));
		cdb[0] = 0x1b;				/* eject */
		cdb[4] = 2;
		CHECK(command(4, cdb, 6) == 0 && finish() == 0 &&
			g_cd_ejects == 1 && !g_cd_in, "CD not ejected");
		ncr5380_attach(4, 0);
	}

	/* 12. A CD with tracks (.cue): the image, the table of contents, and
	 * playing audio with the Apple CD SC's commands */
	{
		static Scsi_cd cd;
		static short pcm[2 * 588 * 4];
		int	bad;

		make_test_cd();
		CHECK(cdimage_open_cue(&g_cue, "test_scsi_cd.cue") == 0,
							"cue sheet not read");
		CHECK(g_cue.ntracks == 3 && g_cue.total == 90 &&
			!g_cue.t[0].audio && g_cue.t[1].audio &&
			g_cue.t[1].start == 40 && g_cue.t[1].frames == 30 &&
			g_cue.t[2].start == 70 && g_cue.t[2].frames == 20,
			"tracks: %d, %u sectors, track 2 at %u, track 3 at %u",
			g_cue.ntracks, g_cue.total, g_cue.t[1].start,
			g_cue.t[2].start);
		scsi_cd_init(&cd, 0, &g_cue_be);
		ncr5380_attach(4, &cd.dev);
		memset(cdb, 0, sizeof(cdb));		/* takes the new disc */
		CHECK(command(4, cdb, 6) == 0 &&
			finish() == SCSI_CHECK_CONDITION, "no unit attention");
		cdb[0] = 0x28;				/* data sector 7 */
		cdb[5] = 7;
		cdb[8] = 1;
		CHECK(command(4, cdb, 10) == 0, "cue READ(10)");
		n = dma_in(buf, sizeof(buf));
		dma_end();
		CHECK(n == 2048 && buf[0] == 21 && buf[2047] == (unsigned char)
					(21 + 2047), "cue data sector: %d", n);
		CHECK(finish() == 0, "cue READ(10) status");
		memset(cdb, 0, sizeof(cdb));		/* Apple READ TOC */
		cdb[0] = 0xc1;
		cdb[8] = 4;
		CHECK(command(4, cdb, 10) == 0, "READ TOC 0");
		for(i = 0; i < 4; i++) {
			buf[i] = (unsigned char)pio_in(1);
		}
		CHECK(finish() == 0 && buf[0] == 1 && buf[1] == 3,
				"TOC: tracks %x to %x", buf[0], buf[1]);
		cdb[5] = 1;				/* 4 entries from 1 */
		cdb[8] = 16;
		cdb[9] = 0x80;
		CHECK(command(4, cdb, 10) == 0, "READ TOC tracks");
		for(i = 0; i < 16; i++) {
			buf[i] = (unsigned char)pio_in(1);
		}
		CHECK(finish() == 0 && buf[0] == 0x14 && buf[3] == 0 &&
			buf[4] == 0x10 && buf[7] == 0x40 &&	/* 40 in BCD */
			buf[8] == 0x10 && buf[11] == 0x70 &&
			buf[14] == 0x01 && buf[15] == 0x15,	/* 90 = 1:15 */
			"TOC entries %02x..%02x %02x..%02x %02x..%02x "
			"..%02x:%02x", buf[0], buf[3], buf[4], buf[7], buf[8],
			buf[11], buf[14], buf[15]);

		CHECK(!scsi_cd_audio(&cd, pcm, 100), "plays before any command");
		memset(cdb, 0, sizeof(cdb));		/* play track 3 */
		cdb[0] = 0xc8;
		cdb[1] = 0x10;
		cdb[5] = 3;
		cdb[9] = 0x80;
		CHECK(command(4, cdb, 10) == 0 && finish() == 0, "TRACK SEARCH");
		memset(cdb, 0, sizeof(cdb));
		cdb[0] = 0xcc;				/* status: playing */
		CHECK(command(4, cdb, 10) == 0, "AUDIO STATUS");
		for(i = 0; i < 6; i++) {
			buf[i] = (unsigned char)pio_in(1);
		}
		CHECK(finish() == 0 && buf[0] == 0 && buf[2] == 0x10 &&
			buf[5] == 0x70, "status %d at frame %02x", buf[0],
			buf[5]);
		CHECK(scsi_cd_audio(&cd, pcm, 588 + 100), "track 3 is silent");
		bad = 0;
		for(i = 0; i < 2 * (588 + 100); i++) {
			bad += pcm[i] != tone(3, (unsigned)i);
		}
		CHECK(bad == 0, "%d wrong samples in track 3", bad);
		cdb[0] = 0xca;				/* pause */
		cdb[1] = 0x10;
		CHECK(command(4, cdb, 10) == 0 && finish() == 0, "PAUSE");
		CHECK(!scsi_cd_audio(&cd, pcm, 50) && pcm[0] == 0,
							"plays while paused");
		cdb[1] = 0;				/* and on */
		CHECK(command(4, cdb, 10) == 0 && finish() == 0, "PAUSE off");
		CHECK(scsi_cd_audio(&cd, pcm, 10) && pcm[0] ==
			tone(3, 2 * 688), "does not go on where it paused");
		for(i = 0; i < 25; i++) {		/* to the end of it */
			scsi_cd_audio(&cd, pcm, 588 * 4);
		}
		cdb[0] = 0xcc;
		CHECK(command(4, cdb, 10) == 0, "AUDIO STATUS at the end");
		for(i = 0; i < 6; i++) {
			buf[i] = (unsigned char)pio_in(1);
		}
		CHECK(finish() == 0 && buf[0] == 3, "status at the end: %d",
								buf[0]);
		memset(cdb, 0, sizeof(cdb));		/* half volume, left */
		cdb[0] = 0xce;
		cdb[8] = 2;
		CHECK(command(4, cdb, 10) == 0, "AUDIO CONTROL");
		CHECK(pio_out(0, 128, 0) == 0 && pio_out(0, 255, 0) == 0,
								"volume bytes");
		CHECK(finish() == 0, "AUDIO CONTROL status");
		cdb[0] = 0xc8;				/* track 2 */
		cdb[1] = 0x10;
		cdb[5] = 2;
		cdb[8] = 0;
		cdb[9] = 0x80;
		CHECK(command(4, cdb, 10) == 0 && finish() == 0, "search 2");
		scsi_cd_audio(&cd, pcm, 8);
		CHECK(pcm[4] == (tone(2, 4) * 128) / 255 && pcm[5] == tone(2, 5),
			"track 2 with volume: %d %d", pcm[4], pcm[5]);
		for(i = 0; i < 40; i++) {	/* it ends where track 3 starts */
			scsi_cd_audio(&cd, pcm, 588 * 4);
		}
		CHECK(!cd.playing && cd.play_lba == 70, "track 2 ran to %u",
								cd.play_lba);
		ncr5380_attach(4, 0);
		cdimage_close(&g_cue);
		remove("test_scsi_cd.cue");
		remove("test_scsi_cd.bin");
		remove("test_scsi_cd.wav");
	}

	if(g_fails) {
		printf("%d SCSI tests FAILED\n", g_fails);
		return 1;
	}
	printf("all SCSI tests passed\n");
	return 0;
}
