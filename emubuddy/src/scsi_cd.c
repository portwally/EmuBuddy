/* A SCSI CD-ROM drive for EmuBuddy. It reads a CD image (.iso, .cdr, .toast:
 * 2048-byte sectors, no header) through the same backend as the hard disk,
 * which counts in 512-byte blocks.
 *
 * The drive says it is a Sony CDU-8002, the drive inside the AppleCD SC: the
 * Apple II firmware and GS/OS's SCSI CD driver know that one. Like it, the
 * drive can be told (MODE SELECT) to use 512-byte blocks.
 *
 * The drive is always on the bus; the disc is not. Without a disc, commands
 * end with NOT READY, and after a new disc went in the first command ends
 * with UNIT ATTENTION, so the computer looks at the disc again.
 *
 * Audio is not played: a disc is one data track. */

#include "scsi.h"
#include <string.h>

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

static unsigned char
bcd(unsigned v)
{
	return (unsigned char)(((v / 10) << 4) | (v % 10));
}

static int
cd_present(Scsi_dev *d)
{
	(void)d;
	return 1;			/* the drive, with or without a disc */
}

/* The disc now in the drive: 0 none, else a number that changes with the
 * disc */
static unsigned
cd_media(Scsi_cd *k)
{
	if(!k->be->present(k->unit)) {
		return 0;
	}
	return k->be->media ? (k->be->media(k->unit) | 1) : 1;
}

/* The smallest size a disc reports, in 512-byte blocks, and the size an
 * empty drive reports (a 74-minute disc).
 *
 * GS/OS's SCSICD.Driver asks READ CAPACITY once, when it starts, and allows
 * itself READ(10) only if the answer is more than 65535 blocks; later it reads
 * with READ(10) only. A smaller disc, or a drive that was empty at that time,
 * could then never be read ("GS/OS can't read this disk"). So a small image
 * reports just over that size (reading past its end gives zeroes), and the
 * empty drive answers READ CAPACITY too. */
#define CD_MIN_BLOCKS512	0x10000U
#define CD_EMPTY_BLOCKS512	(333000U * 4)

/* Size of the disc in 512-byte blocks, as reported */
static unsigned
cd_size512(Scsi_cd *k)
{
	unsigned n;

	if(!k->be->present(k->unit)) {
		return CD_EMPTY_BLOCKS512;
	}
	n = k->be->blocks(k->unit);
	return n < CD_MIN_BLOCKS512 ? CD_MIN_BLOCKS512 : n;
}

/* Size of the disc in blocks of the current block size */
static unsigned
cd_blocks(Scsi_cd *k)
{
	return (unsigned)(((unsigned long long)cd_size512(k) * 512) / k->bs);
}

/* Size in 2048-byte sectors, as the table of contents counts */
static unsigned
cd_sectors(Scsi_cd *k)
{
	return cd_size512(k) / 4;
}

static void
answer(Scsi_cd *k, unsigned len, unsigned alloc)
{
	k->dev.xfer_dir = SCSI_DIR_IN;
	k->dev.xfer_len = len < alloc ? len : alloc;
	k->dev.xfer_chunk = sizeof(k->resp);
}

/* A sector number as minutes, seconds, frames (75 frames a second; sector
 * 0 is at 0:02:00). p[0] is left alone. */
static void
put_msf(unsigned char *p, unsigned lba, int as_bcd)
{
	unsigned f = lba + 150;

	p[1] = (unsigned char)(f / (75 * 60));
	p[2] = (unsigned char)((f / 75) % 60);
	p[3] = (unsigned char)(f % 75);
	if(as_bcd) {
		p[1] = bcd(p[1]);
		p[2] = bcd(p[2]);
		p[3] = bcd(p[3]);
	}
}

static void
put_addr(unsigned char *p, unsigned lba, int msf)
{
	if(msf) {
		p[0] = 0;
		put_msf(p, lba, 0);
	} else {
		put32(p, lba);
	}
}

static void
mode_sense(Scsi_cd *k, const unsigned char *cdb)
{
	unsigned char *r = k->resp;
	unsigned blocks = cd_blocks(k);
	unsigned n;
	int	page = cdb[2] & 0x3f;

	memset(r, 0, 32);
	r[2] = 0x80;				/* write protected */
	n = 4;
	if(!(cdb[1] & 0x08)) {
		r[3] = 8;
		r[5] = blocks >> 16;
		r[6] = blocks >> 8;
		r[7] = blocks;
		r[9] = k->bs >> 16;
		r[10] = k->bs >> 8;
		r[11] = k->bs;
		n = 12;
	}
	if(page == 0x01 || page == 0x3f) {	/* error recovery */
		r[n] = 0x01;
		r[n + 1] = 6;
		n += 8;
	}
	r[0] = n - 1;
	answer(k, n, cdb[4] ? cdb[4] : 256);
}

static void
cd_start(Scsi_dev *d, const unsigned char *cdb, int lun)
{
	Scsi_cd *k = d->ctx;
	unsigned char *r = k->resp;
	unsigned lba, count, n, media, alloc;
	int	msf;

	k->op = cdb[0];
	if(cdb[0] != 0x03) {
		d->sense_key = d->asc = d->ascq = 0;
	}
	if(lun != 0) {
		if(cdb[0] == 0x12) {
			memset(r, 0, 36);
			r[0] = 0x7f;
			answer(k, 36, cdb[4]);
		} else if(cdb[0] != 0x03) {
			scsi_sense(d, SENSE_ILLEGAL_REQUEST, 0x25, 0);
		}
		return;
	}
	media = cd_media(k);
	if(media != k->media) {			/* disc changed */
		k->media = media;
		k->bs = 2048;
		k->ua = (media != 0);
	}
	switch(cdb[0]) {
	case 0x03:			/* REQUEST SENSE */
		/* GS/OS's CD driver asks this again and again to see what
		 * the drive is doing, with no failed command before it: so
		 * a new disc and an empty drive are reported here too */
		if(d->sense_key == 0 && k->ua) {
			k->ua = 0;
			d->sense_key = SENSE_UNIT_ATTENTION;
			d->asc = 0x28;
		} else if(d->sense_key == 0 && !media) {
			d->sense_key = SENSE_NOT_READY;
			d->asc = 0xb0;
		}
		memset(r, 0, 18);
		r[0] = 0x70;
		r[2] = d->sense_key;
		r[7] = 10;
		r[12] = d->asc;
		r[13] = d->ascq;
		answer(k, 18, cdb[4] ? cdb[4] : 4);
		d->sense_key = d->asc = d->ascq = 0;
		return;
	case 0x12:			/* INQUIRY */
		memset(r, 0, 54);
		r[0] = 0x05;		/* CD-ROM */
		r[1] = 0x80;		/* removable */
		r[2] = 0x01;
		r[3] = 0x01;
		r[4] = 0x31;
		memcpy(r + 8, "SONY    CD-ROM CDU-8002 1.8g", 28);
		/* The rest is the drive's own data (from an AppleCD's ROM,
		 * as in MAME's nscsi/cd.cpp). GS/OS's CD driver takes from
		 * it which commands the drive knows: without it the driver
		 * refuses to send READ(10) for a disc that has no Apple
		 * partition map, or that went in while GS/OS runs. */
		memcpy(r + 39, "\xd0\x90\x27\x3e\x01\x04\x91\x00\x18\x06"
							"\xf0\xfe", 12);
		answer(k, 54, cdb[4]);
		return;
	}
	if(k->ua) {			/* a new disc: say so once */
		k->ua = 0;
		scsi_sense(d, SENSE_UNIT_ATTENTION, 0x28, 0);
		return;
	}
	switch(cdb[0]) {
	case 0x15:			/* MODE SELECT(6): the block size */
		d->xfer_dir = SCSI_DIR_OUT;
		d->xfer_len = cdb[4];
		d->xfer_chunk = sizeof(k->resp);
		return;
	case 0x1a:			/* MODE SENSE(6) */
		mode_sense(k, cdb);
		return;
	case 0x1e:			/* PREVENT ALLOW MEDIUM REMOVAL */
	case 0x16:			/* RESERVE */
	case 0x17:			/* RELEASE */
	case 0x1d:			/* SEND DIAGNOSTIC */
		return;
	case 0x1b:			/* START STOP UNIT; bit 1: eject */
		if((cdb[4] & 3) == 2 && media && k->be->eject) {
			k->be->eject(k->unit);
		}
		return;
	case 0xc0:			/* Apple: EJECT DISC */
		if(media && k->be->eject) {
			k->be->eject(k->unit);
		}
		return;
	case 0x25:			/* READ CAPACITY, also when empty */
		n = cd_blocks(k);
		put32(r, n - 1);
		put32(r + 4, k->bs);
		answer(k, 8, 8);
		return;
	}
	if(!media) {
		/* All the rest needs a disc. The code is the AppleCD SC's own
		 * (0xb0), not the usual "medium not present" (0x3a): with
		 * that one, the card's firmware takes the drive for broken
		 * and GS/OS offers to initialize the missing disc (as MAME's
		 * nscsi/cd.cpp notes). */
		scsi_sense(d, SENSE_NOT_READY, 0xb0, 0);
		return;
	}
	switch(cdb[0]) {
	case 0x00:			/* TEST UNIT READY */
	case 0x01:			/* REZERO UNIT */
	case 0x0b:			/* SEEK(6) */
	case 0x2b:			/* SEEK(10) */
	case 0x2f:			/* VERIFY(10) */
		break;
	case 0x08:			/* READ(6) */
	case 0x28:			/* READ(10) */
		if(cdb[0] == 0x08) {
			lba = ((cdb[1] & 0x1f) << 16) | be16(cdb + 2);
			count = cdb[4] ? cdb[4] : 256;
		} else {
			lba = be32(cdb + 2);
			count = be16(cdb + 7);
		}
		n = cd_blocks(k);
		if(count == 0) {
			break;
		}
		if(lba >= n || count > n - lba) {
			scsi_sense(d, SENSE_ILLEGAL_REQUEST, 0x21, 0);
			break;
		}
		k->lba = lba;
		d->xfer_dir = SCSI_DIR_IN;
		d->xfer_len = count * k->bs;
		d->xfer_chunk = k->bs;
		break;
	case 0x0a:			/* WRITE(6) */
	case 0x2a:			/* WRITE(10) */
		scsi_sense(d, SENSE_DATA_PROTECT, 0x27, 0);
		break;
	case 0x43:			/* READ TOC: one data track */
		msf = cdb[1] & 2;
		alloc = be16(cdb + 7);
		memset(r, 0, 20);
		r[1] = 18;
		r[2] = 1;		/* first track */
		r[3] = 1;		/* last track */
		n = 4;
		if(cdb[6] <= 1) {
			r[n + 1] = 0x14;	/* data track */
			r[n + 2] = 1;
			put_addr(r + n + 4, 0, msf);
			n += 8;
		}
		r[n + 1] = 0x14;
		r[n + 2] = 0xaa;		/* lead-out */
		put_addr(r + n + 4, cd_sectors(k), msf);
		n += 8;
		r[1] = n - 2;
		answer(k, n, alloc);
		break;
	case 0x42:			/* READ SUB-CHANNEL: no audio playing */
		alloc = be16(cdb + 7);
		memset(r, 0, 16);
		r[1] = 0x15;		/* no audio status */
		r[3] = 12;
		r[4] = 0x01;
		r[5] = 0x14;
		r[6] = 1;
		r[7] = 1;
		answer(k, 16, alloc);
		break;
	case 0x44:			/* READ HEADER */
		alloc = be16(cdb + 7);
		memset(r, 0, 8);
		r[0] = 1;		/* mode 1 */
		put_addr(r + 4, be32(cdb + 2), cdb[1] & 2);
		answer(k, 8, alloc);
		break;
	case 0xc1:			/* Apple: READ TOC */
		alloc = be16(cdb + 7);
		memset(r, 0, 8);
		switch(cdb[9] >> 6) {
		case 0:			/* first and last track */
			r[0] = bcd(1);
			r[1] = bcd(1);
			break;
		case 1:			/* lead-out */
			put_msf(r + 4, cd_sectors(k), 1);
			memcpy(r, r + 5, 3);
			break;
		default:		/* start of a track */
			r[0] = 0x14;
			put_msf(r, 0, 1);
			break;
		}
		answer(k, 4, alloc);
		break;
	case 0xc2:			/* Apple: READ Q SUBCODE */
		memset(r, 0, 9);
		r[1] = bcd(1);		/* track 1, index 1, at its start */
		r[2] = 1;
		put_msf(r + 5, 0, 1);
		answer(k, 9, 9);
		break;
	case 0xcc:			/* Apple: AUDIO STATUS: nothing plays */
		memset(r, 0, 6);
		r[0] = 5;		/* idle */
		r[2] = 0x14;
		put_msf(r + 2, 0, 1);
		answer(k, 6, 6);
		break;
	case 0xca:			/* Apple: AUDIO PAUSE */
	case 0xcb:			/* Apple: AUDIO STOP */
		break;
	default:
		scsi_sense(d, SENSE_ILLEGAL_REQUEST, 0x20, 0);
		break;
	}
}

static int
cd_data_in(Scsi_dev *d, unsigned off, unsigned char *buf, unsigned n)
{
	Scsi_cd *k = d->ctx;
	unsigned long long blk;
	unsigned total, i;

	if(k->op != 0x08 && k->op != 0x28) {
		memcpy(buf, k->resp + off, n);
		return 0;
	}
	blk = ((unsigned long long)k->lba * k->bs + off) / 512;
	total = k->be->blocks(k->unit);
	for(i = 0; i < n / 512; i++) {
		if(blk + i >= total) {
			memset(buf + i * 512, 0, 512);
		} else if(k->be->read(k->unit, (unsigned)(blk + i),
						buf + i * 512) != 0) {
			return scsi_sense(d, SENSE_MEDIUM_ERROR, 0x11, 0);
		}
	}
	return 0;
}

static int
cd_data_out(Scsi_dev *d, unsigned off, const unsigned char *buf, unsigned n)
{
	Scsi_cd *k = d->ctx;
	unsigned bs;

	/* MODE SELECT: the block length in the block descriptor */
	if(k->op == 0x15 && off == 0 && n >= 12 && buf[3] >= 8) {
		bs = (buf[9] << 16) | (buf[10] << 8) | buf[11];
		if(bs == 512 || bs == 1024 || bs == 2048) {
			k->bs = bs;
		} else if(bs != 0) {
			return scsi_sense(d, SENSE_ILLEGAL_REQUEST, 0x26, 0);
		}
	}
	return 0;
}

void
scsi_cd_init(Scsi_cd *k, int unit, const Scsi_block_backend *be)
{
	memset(k, 0, sizeof(*k));
	k->unit = unit;
	k->be = be;
	k->bs = 2048;
	k->dev.ctx = k;
	k->dev.present = cd_present;
	k->dev.start = cd_start;
	k->dev.data_in = cd_data_in;
	k->dev.data_out = cd_data_out;
}
