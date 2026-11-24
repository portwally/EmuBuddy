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
 * A .cue image has tracks, and its audio tracks play: the Apple CD SC's own
 * audio commands (0xC8-0xCE) start, pause and stop them, and scsi_cd_audio()
 * hands the samples to the emulator's sound. */

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

static void answer(Scsi_cd *k, unsigned len, unsigned alloc);

/* ---- tracks ---- */
static int
cd_ntracks(Scsi_cd *k)
{
	int	n = k->be->tracks ? k->be->tracks(k->unit) : 0;

	return n > 0 ? n : 1;
}

/* Start (in sectors) and kind of track n, 0-based; n = cd_ntracks(): the
 * lead-out. Returns the ADR/control byte: 0x14 data, 0x10 audio. */
static int
cd_track(Scsi_cd *k, int n, unsigned *start)
{
	int	audio = 0;

	if(k->be->tracks && k->be->tracks(k->unit) > 0 && k->be->track) {
		k->be->track(k->unit, n, start, &audio);
	} else {
		*start = (n == 0) ? 0 : cd_sectors(k);
	}
	return audio ? 0x10 : 0x14;
}

/* The track a sector is in, 0-based */
static int
cd_track_of(Scsi_cd *k, unsigned lba)
{
	unsigned start;
	int	i, n = cd_ntracks(k);

	for(i = n - 1; i > 0; i--) {
		cd_track(k, i, &start);
		if(lba >= start) {
			return i;
		}
	}
	return 0;
}

static unsigned
unbcd(unsigned v)
{
	return ((v >> 4) & 15) * 10 + (v & 15);
}

/* minutes, seconds, frames as the Apple commands count them: sector 0 is
 * 0:00:00 (no 2-second lead-in), all in BCD */
static void
put_amsf(unsigned char *p, unsigned lba)
{
	p[0] = bcd(lba / (75 * 60));
	p[1] = bcd((lba / 75) % 60);
	p[2] = bcd(lba % 75);
}

static unsigned
get_amsf(const unsigned char *p)
{
	return (unbcd(p[0]) * 60 + unbcd(p[1])) * 75 + unbcd(p[2]);
}

/* ---- audio ---- */
static void
audio_stop(Scsi_cd *k)
{
	k->playing = 0;
	k->paused = 0;
	k->ended = 0;
}

static void
audio_start(Scsi_cd *k, unsigned lba, unsigned stop)
{
	k->play_lba = lba;
	k->stop_lba = stop;
	k->samp_pos = 0;
	k->have_sector = 0;
	k->playing = (stop > lba);
	k->ended = !k->playing;
	k->paused = 0;
}

int
scsi_cd_audio(Scsi_cd *k, short *out, int frames)
{
	const unsigned char *p;
	int	i;

	if(!k->playing || k->paused || !k->be->read_audio ||
						!k->be->present(k->unit)) {
		memset(out, 0, (size_t)frames * 2 * sizeof(short));
		return 0;
	}
	for(i = 0; i < frames; i++) {
		if(k->playing && !k->have_sector) {
			if(k->play_lba >= k->stop_lba) {
				k->playing = 0;		/* ran to its end */
				k->ended = 1;
			} else {
				if(k->be->read_audio(k->unit, k->play_lba,
							k->audio_buf) != 0) {
					memset(k->audio_buf, 0, 2352);
				}
				k->have_sector = 1;
			}
		}
		if(!k->playing) {
			out[2 * i] = out[2 * i + 1] = 0;
			continue;
		}
		p = k->audio_buf + 4 * k->samp_pos;
		out[2 * i] = (short)(((short)(p[0] | (p[1] << 8)) *
							k->vol[0]) / 255);
		out[2 * i + 1] = (short)(((short)(p[2] | (p[3] << 8)) *
							k->vol[1]) / 255);
		if(++k->samp_pos >= 588) {
			k->samp_pos = 0;
			k->play_lba++;
			k->have_sector = 0;
		}
	}
	return 1;
}

/* The address of an Apple audio command: in the form the top bits of CDB
 * byte 9 say. *track gets the track number (1-based) or 0. */
static unsigned
audio_addr(Scsi_cd *k, const unsigned char *cdb, int msf_at, int *track)
{
	unsigned start;

	*track = 0;
	switch(cdb[9] & 0xc0) {
	case 0x40:			/* minutes, seconds, frames */
		return get_amsf(cdb + msf_at);
	case 0x80:			/* track number */
		*track = (int)unbcd(cdb[5]);
		if(*track < 1) {
			return 0;
		}
		if(*track > cd_ntracks(k)) {
			*track = cd_ntracks(k);
		}
		cd_track(k, *track - 1, &start);
		return start;
	}
	return be32(cdb + 2);		/* sector number */
}

/* The Apple CD SC's audio commands (0xC8-0xCE), as in MAME's nscsi/cd.cpp,
 * which follows "Apple CD-ROM SCSI Command Set" 1.4. Returns 0 if cdb is not
 * one of them. */
static int
audio_command(Scsi_cd *k, const unsigned char *cdb)
{
	unsigned char *r = k->resp;
	unsigned addr, start;
	int	track;

	switch(cdb[0]) {
	case 0xc8:			/* AUDIO TRACK SEARCH: go there */
		addr = audio_addr(k, cdb, 5, &track);
		if((cdb[9] & 0xc0) == 0x80) {
			if(track == 0) {
				audio_stop(k);
				return 1;
			}
			cd_track(k, track, &k->stop_lba);	/* its end */
		}
		audio_start(k, addr, k->stop_lba);
		if(!(cdb[1] & 0x10)) {
			k->paused = 1;		/* stay there, paused */
		}
		k->stopped = 0;
		return 1;
	case 0xc9:			/* AUDIO PLAY */
		addr = audio_addr(k, cdb, 3, &track);
		if((cdb[9] & 0xc0) == 0x80 && track == 0) {
			audio_stop(k);		/* "play track 0" stops */
			k->stopped = 1;
			return 1;
		}
		if(cdb[1] & 0x10) {
			/* programs search a track and then play "to" the
			 * same one: they mean all of that track */
			if(k->stop_lba == addr) {
				cd_track(k, track >= cd_ntracks(k) ?
					cd_ntracks(k) : track, &k->stop_lba);
			}
		} else {
			k->stopped = 0;
		}
		audio_start(k, addr, k->stop_lba);
		return 1;
	case 0xca:			/* AUDIO PAUSE: on, or off */
		k->paused = (cdb[1] == 0x10) && k->playing;
		return 1;
	case 0xcb:			/* AUDIO STOP: where play is to end */
		addr = audio_addr(k, cdb, 5, &track);
		if((cdb[9] & 0xc0) == 0x80) {	/* at the end of that track */
			cd_track(k, track >= cd_ntracks(k) ? cd_ntracks(k) :
								track, &addr);
			if(addr) {
				addr--;
			}
		}
		k->stop_lba = addr;
		if(!k->stopped && k->play_lba >= k->stop_lba) {
			k->stopped = 1;
			audio_stop(k);
		}
		return 1;
	case 0xcc:			/* AUDIO STATUS */
		memset(r, 0, 6);
		if(cdb[3] == 1) {		/* the volume */
			r[0] = k->vol[0];
			r[1] = k->vol[1];
		} else if(k->stopped) {
			r[0] = 5;		/* idle */
		} else if(k->ended) {
			r[0] = 3;		/* reached the end */
		} else if(k->playing) {
			r[0] = k->paused ? 1 : 0;
		} else {
			r[0] = 5;
		}
		r[2] = (unsigned char)cd_track(k, cd_track_of(k, k->play_lba),
								&start);
		put_amsf(r + 3, k->play_lba);
		answer(k, 6, 6);
		return 1;
	case 0xcd:			/* AUDIO SCAN: go on from there */
		if(!k->stopped) {
			addr = audio_addr(k, cdb, 5, &track);
			k->play_lba = addr;
			k->samp_pos = 0;
			k->have_sector = 0;
		}
		return 1;
	case 0xce:			/* AUDIO CONTROL: the volume follows */
		k->dev.xfer_dir = SCSI_DIR_OUT;
		k->dev.xfer_len = cdb[8];
		k->dev.xfer_chunk = sizeof(k->resp);
		return 1;
	}
	return 0;
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
	int	msf, nt, i;

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
		audio_stop(k);
		k->stopped = 1;
		k->play_lba = k->stop_lba = 0;
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
		/* The rest is the drive's list of the commands it knows, as
		 * GS/OS's SCSICD.Driver reads it (at $1322 in the driver):
		 * records of a number n and two words, the bit masks for the
		 * commands 0x(2n)0-0x(2n)F and 0x(2n+1)0-0x(2n+1)F, ended by
		 * 0xFF. The values are those of an AppleCD's ROM (from
		 * MAME's nscsi/cd.cpp): n = 0, 1 and 6, so the usual
		 * commands, READ(10), and Apple's own 0xC0-0xCE for audio.
		 * Without the list, or without its end mark, the driver
		 * uses a list of its own that has no audio commands. */
		memcpy(r + 38, "\x00\xd0\x90\x27\x3e" "\x01\x04\x91\x00\x18"
				"\x06\xf0\xfe\x00\x00" "\xff", 16);
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
		/* An audio track cannot be read as data: "illegal mode for
		 * this track" (0x64). GS/OS's driver takes that code for "no
		 * data disc here" and stays quiet; with zeroes for data,
		 * GS/OS offers to initialize an audio CD. */
		if(cd_track(k, cd_track_of(k, (unsigned)(((unsigned long long)
				lba * k->bs) / 2048)), &n) == 0x10) {
			scsi_sense(d, SENSE_ILLEGAL_REQUEST, 0x64, 0);
			break;
		}
		audio_stop(k);			/* reading ends the music */
		k->stopped = 1;
		k->lba = lba;
		d->xfer_dir = SCSI_DIR_IN;
		d->xfer_len = count * k->bs;
		d->xfer_chunk = k->bs;
		break;
	case 0x0a:			/* WRITE(6) */
	case 0x2a:			/* WRITE(10) */
		scsi_sense(d, SENSE_DATA_PROTECT, 0x27, 0);
		break;
	case 0x43:			/* READ TOC */
		msf = cdb[1] & 2;
		alloc = be16(cdb + 7);
		nt = cd_ntracks(k);
		memset(r, 0, sizeof(k->resp));
		r[2] = 1;		/* first track */
		r[3] = (unsigned char)nt;
		n = 4;
		for(i = (cdb[6] > 1) ? cdb[6] - 1 : 0; i <= nt &&
					n + 8 <= sizeof(k->resp); i++) {
			r[n + 1] = (unsigned char)cd_track(k, i, &lba);
			r[n + 2] = (i == nt) ? 0xaa : (unsigned char)(i + 1);
			put_addr(r + n + 4, lba, msf);
			n += 8;
		}
		r[0] = (n - 2) >> 8;
		r[1] = (n - 2) & 0xff;
		answer(k, n, alloc);
		break;
	case 0x42:			/* READ SUB-CHANNEL: where play is */
		alloc = be16(cdb + 7);
		memset(r, 0, 16);
		r[1] = k->playing ? (k->paused ? 0x12 : 0x11) :
						(k->ended ? 0x13 : 0x15);
		r[3] = 12;
		r[4] = 0x01;
		i = cd_track_of(k, k->play_lba);
		r[5] = (unsigned char)cd_track(k, i, &lba);
		r[6] = (unsigned char)(i + 1);
		r[7] = 1;
		put_addr(r + 8, k->play_lba, cdb[1] & 2);
		put_addr(r + 12, k->play_lba - lba, cdb[1] & 2);
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
		nt = cd_ntracks(k);
		memset(r, 0, sizeof(k->resp));
		switch(cdb[9] & 0xc0) {
		case 0x00:		/* first and last track */
			r[0] = 1;
			r[1] = bcd((unsigned)nt);
			answer(k, 4, alloc);
			break;
		case 0x40:		/* where the lead-out starts */
			cd_track(k, nt, &lba);
			put_amsf(r, lba);
			answer(k, 4, alloc);
			break;
		default:		/* the tracks from track cdb[5] on: as
					 * many as fit; past the last track
					 * the lead-out, again and again */
			i = (int)unbcd(cdb[5]) - 1;
			if(i < 0) {
				i = 0;
			}
			for(n = 0; n + 4 <= alloc && n + 4 <= sizeof(k->resp);
								n += 4, i++) {
				r[n] = (unsigned char)cd_track(k,
						i < nt ? i : nt, &lba);
				put_amsf(r + n + 1, lba);
			}
			answer(k, n, alloc);
			break;
		}
		break;
	case 0xc2:			/* Apple: READ Q SUBCODE: where play is */
		memset(r, 0, 9);
		i = cd_track_of(k, k->play_lba);
		r[0] = (unsigned char)cd_track(k, i, &lba);
		r[1] = bcd((unsigned)i + 1);
		r[2] = 1;
		put_amsf(r + 3, k->play_lba > lba ? k->play_lba - lba : 0);
		put_amsf(r + 6, k->play_lba > lba ? k->play_lba : lba);
		answer(k, 9, 9);
		break;
	default:
		if(audio_command(k, cdb)) {
			break;
		}
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

	if(k->op == 0xce && off == 0) {		/* AUDIO CONTROL: the volume */
		if(n >= 1) {
			k->vol[0] = buf[0];
		}
		if(n >= 2) {
			k->vol[1] = buf[1];
		}
		return 0;
	}
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
	k->stopped = 1;
	k->vol[0] = k->vol[1] = 255;
	k->dev.ctx = k;
	k->dev.present = cd_present;
	k->dev.start = cd_start;
	k->dev.data_in = cd_data_in;
	k->dev.data_out = cd_data_out;
}
