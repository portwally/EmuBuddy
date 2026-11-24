/* Host test for the NCR 53C80 and the SCSI hard disk (ncr5380.c,
 * scsi_disk.c). It drives the chip's registers the way card firmware does:
 * arbitration, selection, programmed I/O and pseudo-DMA. Two disks sit in
 * memory at SCSI IDs 0 and 2; ID 1 is empty.
 *   cmake --build build --target test_scsi && build/test_scsi */

#include "../scsi.h"
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
	cdb[0] = 0xc5;
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

	if(g_fails) {
		printf("%d SCSI tests FAILED\n", g_fails);
		return 1;
	}
	printf("all SCSI tests passed\n");
	return 0;
}
