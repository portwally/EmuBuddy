/* CD images with tracks, for EmuBuddy's CD-ROM drive: a .cue sheet with its
 * .bin (or .iso, .wav) files. Data tracks and audio tracks. Knows nothing
 * about the IIgs or SCSI. */
#ifndef EMUBUDDY_CDIMAGE_H
#define EMUBUDDY_CDIMAGE_H

#include <stdio.h>

#define CD_MAX_TRACKS	99
#define CD_RAW		2352	/* bytes of a raw sector; 588 stereo samples */

typedef struct {
	int		audio;		/* 1 audio, 0 data */
	unsigned	start;		/* first sector on the disc */
	unsigned	frames;		/* length in sectors */
	FILE		*f;		/* the file it is in (shared by tracks) */
	long long	off;		/* where it starts in the file */
	int		secsize;	/* bytes a sector takes in the file */
	int		data_off;	/* where the 2048 data bytes are in it */
} Cd_track;

typedef struct {
	int		ntracks;
	Cd_track	t[CD_MAX_TRACKS];
	unsigned	total;		/* sectors on the disc: the lead-out */
	FILE		*files[CD_MAX_TRACKS];
	int		nfiles;
} Cd_image;

/* Opens the image a .cue sheet describes. 0 when it worked. */
int	cdimage_open_cue(Cd_image *cd, const char *cue_path);
void	cdimage_close(Cd_image *cd);
/* The track sector lba is in (0-based), or -1 */
int	cdimage_track_of(const Cd_image *cd, unsigned lba);
/* The 2048 data bytes of a sector; zeroes for an audio sector. 0 ok. */
int	cdimage_read_data(Cd_image *cd, unsigned lba, unsigned char *buf);
/* The 2352 bytes of an audio sector (588 samples, 16 bits, little-endian,
 * left then right); silence for a data sector. 0 ok. */
int	cdimage_read_audio(Cd_image *cd, unsigned lba, unsigned char *buf);

#endif
