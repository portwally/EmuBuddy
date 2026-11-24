/* SCSI for EmuBuddy: the NCR 5380 / 53C80 controller (ncr5380.c), the SCSI
 * bus with its targets, and SCSI devices (scsi_disk.c).
 *
 * None of this knows about the IIgs: a card (hsscsi.c) passes register reads
 * and writes to the chip, and a device reads and writes its blocks through a
 * backend. So tests/test_scsi.c can drive it all without an emulator. */
#ifndef EMUBUDDY_SCSI_H
#define EMUBUDDY_SCSI_H

#define SCSI_MAX_ID		8
#define SCSI_CHUNK_MAX		2048	/* largest block size (CD-ROM) */

/* Status bytes */
#define SCSI_GOOD		0x00
#define SCSI_CHECK_CONDITION	0x02

/* Sense keys */
#define SENSE_NONE		0x0
#define SENSE_NOT_READY		0x2
#define SENSE_MEDIUM_ERROR	0x3
#define SENSE_ILLEGAL_REQUEST	0x5
#define SENSE_UNIT_ATTENTION	0x6
#define SENSE_DATA_PROTECT	0x7

#define SCSI_DIR_NONE		0
#define SCSI_DIR_IN		1	/* device to initiator */
#define SCSI_DIR_OUT		2	/* initiator to device */

typedef struct Scsi_dev Scsi_dev;

/* A SCSI device (target). The bus calls start() when a command arrived; the
 * device then sets xfer_dir, xfer_len, xfer_chunk and status. The data moves
 * in chunks of xfer_chunk bytes through data_in() / data_out(); off is the
 * byte offset in the transfer and always a multiple of xfer_chunk. A chunk
 * that fails returns -1 after setting the sense data: the transfer stops and
 * the status becomes CHECK CONDITION. */
struct Scsi_dev {
	void	*ctx;
	int	(*present)(Scsi_dev *d);	/* answers selection */
	void	(*start)(Scsi_dev *d, const unsigned char *cdb, int lun);
	int	(*data_in)(Scsi_dev *d, unsigned off, unsigned char *buf,
								unsigned n);
	int	(*data_out)(Scsi_dev *d, unsigned off, const unsigned char *buf,
								unsigned n);
	/* set by start() */
	int		xfer_dir;
	unsigned	xfer_len;
	unsigned	xfer_chunk;
	int		status;
	/* sense data for REQUEST SENSE */
	unsigned char	sense_key, asc, ascq;
};

/* Fills in the sense data and returns -1 (for data_in / data_out). */
int	scsi_sense(Scsi_dev *d, int key, int asc, int ascq);
/* The length of the command block from its first byte (6, 10 or 12). */
int	scsi_cdb_len(unsigned char op);

/* ---- NCR 5380 / 53C80 with its bus ---- */
extern int g_scsi_trace;	/* 1: print every command and its result */
void	ncr5380_init(int own_id);
void	ncr5380_reset(void);
/* Attaches device d at SCSI ID id (NULL removes it). */
void	ncr5380_attach(int id, Scsi_dev *d);
/* reg 0-7, as at the chip's register select pins */
unsigned ncr5380_read(int reg);
void	ncr5380_write(int reg, unsigned val);
/* The DMA port (pseudo-DMA: the card hands bytes over one at a time) */
unsigned ncr5380_dma_read(void);
void	ncr5380_dma_write(unsigned val);
int	ncr5380_drq(void);
void	ncr5380_eop(void);	/* the card's DMA counter ran out */
int	ncr5380_irq(void);
/* For the drive activity lights: the ID being talked to, or -1 */
int	ncr5380_busy_id(void);

/* ---- SCSI hard disk ---- */
typedef struct {
	int	(*present)(int unit);	/* a disk image is in the drive */
	unsigned (*blocks)(int unit);	/* size in 512-byte blocks */
	int	(*write_prot)(int unit);
	int	(*read)(int unit, unsigned blk, unsigned char *buf);	/* 0 ok */
	int	(*write)(int unit, unsigned blk, const unsigned char *buf);
	/* for removable media (the CD-ROM); both may be 0 */
	unsigned (*media)(int unit);	/* a number that changes with the disc */
	void	(*eject)(int unit);	/* the computer asks for the disc */
	/* for a CD with tracks (a .cue image); all three may be 0: the disc
	 * is then one data track. Sectors count from 0, in 2048-byte units. */
	int	(*tracks)(int unit);	/* number of tracks, 0 = not known */
	/* start and kind of track n (0-based); n = tracks(): the lead-out */
	void	(*track)(int unit, int n, unsigned *start, int *audio);
	/* the 2352 bytes of an audio sector; 0 ok */
	int	(*read_audio)(int unit, unsigned lba, unsigned char *buf);
} Scsi_block_backend;

typedef struct {
	Scsi_dev	dev;
	int		unit;
	const Scsi_block_backend *be;
	unsigned	lba;		/* first block of the current transfer */
	unsigned char	op;		/* current command */
	unsigned char	resp[256];	/* answer of a short command */
} Scsi_disk;

void	scsi_disk_init(Scsi_disk *disk, int unit, const Scsi_block_backend *be);

/* ---- SCSI CD-ROM drive (scsi_cd.c) ---- */
typedef struct {
	Scsi_dev	dev;
	int		unit;
	const Scsi_block_backend *be;
	unsigned	lba;		/* first block of the current transfer */
	unsigned	bs;		/* block size: 2048, or 512 after MODE SELECT */
	unsigned	media;		/* the disc the computer knows; 0 none */
	int		ua;		/* a new disc went in: unit attention */
	unsigned char	op;
	unsigned char	resp[512];
	/* audio: sectors are 588 stereo samples at 44100 Hz */
	int		playing;	/* a play command runs (also when paused) */
	int		paused;
	int		stopped;	/* nothing was started since the last stop */
	int		ended;		/* play ran to its stop position */
	unsigned	play_lba;	/* the sector being played */
	unsigned	stop_lba;	/* play ends before this sector */
	unsigned	samp_pos;	/* sample within the sector, 0-587 */
	int		have_sector;	/* audio_buf holds sector play_lba */
	unsigned char	vol[2];		/* left, right: 0-255 */
	unsigned char	audio_buf[2352];
} Scsi_cd;

void	scsi_cd_init(Scsi_cd *cd, int unit, const Scsi_block_backend *be);
/* Gives the next frames of the audio that plays: 16-bit samples, left then
 * right, 44100 Hz; silence when nothing plays. Returns 1 if it plays. */
int	scsi_cd_audio(Scsi_cd *cd, short *out, int frames);

#endif
