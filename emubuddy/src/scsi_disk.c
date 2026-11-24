/* A SCSI hard disk with 512-byte blocks, for EmuBuddy. Its blocks come from a
 * backend: in the emulator the drives S7D1-S7D7 (smartport.c), in
 * tests/test_scsi.c a block of memory. */

#include "scsi.h"
#include <string.h>

#define BLK	512

static unsigned
be16(const unsigned char *p)
{
	return (p[0] << 8) | p[1];
}

static unsigned
be32(const unsigned char *p)
{
	return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static void
put32(unsigned char *p, unsigned v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

static int
disk_present(Scsi_dev *d)
{
	Scsi_disk *k = d->ctx;

	return k->be->present(k->unit);
}

/* A short answer from k->resp: at most alloc bytes of len */
static void
answer(Scsi_disk *k, unsigned len, unsigned alloc)
{
	k->dev.xfer_dir = SCSI_DIR_IN;
	k->dev.xfer_len = len < alloc ? len : alloc;
	k->dev.xfer_chunk = sizeof(k->resp);
}

/* MODE SENSE pages. Returns the bytes added at p. */
static unsigned
mode_page(Scsi_disk *k, int page, unsigned char *p)
{
	unsigned blocks = k->be->blocks(k->unit);
	unsigned cyl;

	switch(page) {
	case 0x01:		/* read-write error recovery */
		memset(p, 0, 8);
		p[0] = 0x01;
		p[1] = 6;
		return 8;
	case 0x03:		/* format device */
		memset(p, 0, 24);
		p[0] = 0x03;
		p[1] = 22;
		p[10] = 0;
		p[11] = 32;	/* sectors per track */
		p[12] = BLK >> 8;
		p[13] = BLK & 0xff;
		return 24;
	case 0x04:		/* rigid disk geometry: 32 sectors, 16 heads */
		memset(p, 0, 24);
		cyl = (blocks + 511) / 512;
		p[0] = 0x04;
		p[1] = 22;
		p[2] = cyl >> 16;
		p[3] = cyl >> 8;
		p[4] = cyl;
		p[5] = 16;
		return 24;
	}
	return 0;
}

static void
mode_sense(Scsi_disk *k, const unsigned char *cdb)
{
	unsigned char *r = k->resp;
	unsigned blocks = k->be->blocks(k->unit);
	unsigned n;
	int	page = cdb[2] & 0x3f;

	memset(r, 0, 12);
	r[1] = 0;				/* medium type */
	r[2] = k->be->write_prot(k->unit) ? 0x80 : 0;
	n = 4;
	if(!(cdb[1] & 0x08)) {			/* block descriptor */
		r[3] = 8;
		r[5] = blocks >> 16;
		r[6] = blocks >> 8;
		r[7] = blocks;
		r[10] = BLK >> 8;
		r[11] = BLK & 0xff;
		n = 12;
	}
	if(page == 0x3f) {
		n += mode_page(k, 0x01, r + n);
		n += mode_page(k, 0x03, r + n);
		n += mode_page(k, 0x04, r + n);
	} else if(page) {
		n += mode_page(k, page, r + n);
	}
	r[0] = n - 1;
	answer(k, n, cdb[4] ? cdb[4] : 256);
}

/* Checks a read or write of count blocks at lba. 0 ok. */
static int
range_ok(Scsi_disk *k, unsigned lba, unsigned count)
{
	unsigned blocks = k->be->blocks(k->unit);

	if(lba >= blocks || count > blocks - lba) {
		scsi_sense(&k->dev, SENSE_ILLEGAL_REQUEST, 0x21, 0);
		return 0;
	}
	return 1;
}

static void
disk_start(Scsi_dev *d, const unsigned char *cdb, int lun)
{
	Scsi_disk *k = d->ctx;
	unsigned char *r = k->resp;
	unsigned lba, count, n;

	k->op = cdb[0];
	if(cdb[0] != 0x03) {			/* REQUEST SENSE keeps it */
		d->sense_key = d->asc = d->ascq = 0;
	}
	if(lun != 0) {
		if(cdb[0] == 0x12) {		/* INQUIRY: no such LUN */
			memset(r, 0, 36);
			r[0] = 0x7f;
			answer(k, 36, cdb[4]);
		} else if(cdb[0] != 0x03) {
			scsi_sense(d, SENSE_ILLEGAL_REQUEST, 0x25, 0);
		}
		return;
	}
	switch(cdb[0]) {
	case 0x00:			/* TEST UNIT READY */
	case 0x01:			/* REZERO UNIT */
	case 0x04:			/* FORMAT UNIT */
	case 0x07:			/* REASSIGN BLOCKS */
	case 0x0b:			/* SEEK(6) */
	case 0x16:			/* RESERVE */
	case 0x17:			/* RELEASE */
	case 0x1b:			/* START STOP UNIT */
	case 0x1d:			/* SEND DIAGNOSTIC */
	case 0x1e:			/* PREVENT ALLOW MEDIUM REMOVAL */
	case 0x2b:			/* SEEK(10) */
	case 0x2f:			/* VERIFY(10) */
	case 0x35:			/* SYNCHRONIZE CACHE */
		break;
	case 0x03:			/* REQUEST SENSE */
		memset(r, 0, 18);
		r[0] = 0x70;
		r[2] = d->sense_key;
		r[7] = 10;
		r[12] = d->asc;
		r[13] = d->ascq;
		answer(k, 18, cdb[4] ? cdb[4] : 4);
		d->sense_key = d->asc = d->ascq = 0;
		break;
	case 0x12:			/* INQUIRY */
		memset(r, 0, 36);
		r[0] = 0x00;		/* direct-access device */
		r[2] = 0x01;		/* SCSI-1 / CCS */
		r[3] = 0x01;
		r[4] = 31;
		memcpy(r + 8, "EMUBUDDYHARD DISK       1.0 ", 28);
		answer(k, 36, cdb[4]);
		break;
	case 0x25:			/* READ CAPACITY */
		n = k->be->blocks(k->unit);
		put32(r, n ? n - 1 : 0);
		put32(r + 4, BLK);
		answer(k, 8, 8);
		break;
	case 0x1a:			/* MODE SENSE(6) */
		mode_sense(k, cdb);
		break;
	case 0x15:			/* MODE SELECT(6): taken, ignored */
		d->xfer_dir = SCSI_DIR_OUT;
		d->xfer_len = cdb[4];
		d->xfer_chunk = sizeof(k->resp);
		break;
	case 0x08:			/* READ(6) */
	case 0x0a:			/* WRITE(6) */
	case 0x28:			/* READ(10) */
	case 0x2a:			/* WRITE(10) */
		if(cdb[0] < 0x20) {
			lba = ((cdb[1] & 0x1f) << 16) | be16(cdb + 2);
			count = cdb[4] ? cdb[4] : 256;
		} else {
			lba = be32(cdb + 2);
			count = be16(cdb + 7);
		}
		if(count == 0 || !range_ok(k, lba, count)) {
			break;
		}
		if((cdb[0] & 0x02) && k->be->write_prot(k->unit)) {
			scsi_sense(d, SENSE_DATA_PROTECT, 0x27, 0);
			break;
		}
		k->lba = lba;
		d->xfer_dir = (cdb[0] & 0x02) ? SCSI_DIR_OUT : SCSI_DIR_IN;
		d->xfer_len = count * BLK;
		d->xfer_chunk = BLK;
		break;
	default:
		scsi_sense(d, SENSE_ILLEGAL_REQUEST, 0x20, 0);
		break;
	}
}

static int
disk_data_in(Scsi_dev *d, unsigned off, unsigned char *buf, unsigned n)
{
	Scsi_disk *k = d->ctx;

	if(k->op == 0x08 || k->op == 0x28) {
		if(k->be->read(k->unit, k->lba + off / BLK, buf) != 0) {
			return scsi_sense(d, SENSE_MEDIUM_ERROR, 0x11, 0);
		}
		return 0;
	}
	memcpy(buf, k->resp + off, n);
	return 0;
}

static int
disk_data_out(Scsi_dev *d, unsigned off, const unsigned char *buf, unsigned n)
{
	Scsi_disk *k = d->ctx;

	(void)n;
	if(k->op == 0x0a || k->op == 0x2a) {
		if(k->be->write(k->unit, k->lba + off / BLK, buf) != 0) {
			return scsi_sense(d, SENSE_MEDIUM_ERROR, 0x0c, 0);
		}
	}
	return 0;
}

void
scsi_disk_init(Scsi_disk *k, int unit, const Scsi_block_backend *be)
{
	memset(k, 0, sizeof(*k));
	k->unit = unit;
	k->be = be;
	k->dev.ctx = k;
	k->dev.present = disk_present;
	k->dev.start = disk_start;
	k->dev.data_in = disk_data_in;
	k->dev.data_out = disk_data_out;
}
