/* CD images with tracks: reads a .cue sheet and the files it names.
 *
 *   FILE "game.bin" BINARY
 *     TRACK 01 MODE1/2352
 *       INDEX 01 00:00:00
 *     TRACK 02 AUDIO
 *       INDEX 00 12:30:00
 *       INDEX 01 12:32:00
 *
 * Known track kinds: MODE1/2048, MODE1/2352, MODE2/2352 (data) and AUDIO.
 * File kinds: BINARY, and WAVE (16 bits, stereo, 44100 Hz) for audio. A track
 * starts at its INDEX 01 and ends where the next track of the same file
 * starts, or at the end of the file. With several files, the tracks follow
 * each other on the disc in the order of the sheet. */

#include "cdimage.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# define fseek64 _fseeki64
# define ftell64 _ftelli64
#else
# define fseek64 fseeko
# define ftell64 ftello
#endif

static int
word_is(const char *s, const char *w)
{
	size_t	n = strlen(w);
	size_t	i;

	for(i = 0; i < n; i++) {
		if(toupper((unsigned char)s[i]) != w[i]) {
			return 0;
		}
	}
	return s[n] == 0 || isspace((unsigned char)s[n]);
}

/* Where the samples start in a WAVE file, and how many bytes there are */
static int
wave_data(FILE *f, long long *off, long long *len)
{
	unsigned char h[12], c[8];
	unsigned long n;

	if(fseek64(f, 0, SEEK_SET) != 0 || fread(h, 1, 12, f) != 12 ||
			memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
		return -1;
	}
	while(fread(c, 1, 8, f) == 8) {
		n = c[4] | (c[5] << 8) | ((unsigned long)c[6] << 16) |
						((unsigned long)c[7] << 24);
		if(!memcmp(c, "data", 4)) {
			*off = ftell64(f);
			*len = (long long)n;
			return 0;
		}
		if(fseek64(f, (long long)(n + (n & 1)), SEEK_CUR) != 0) {
			break;
		}
	}
	return -1;
}

void
cdimage_close(Cd_image *cd)
{
	int	i;

	for(i = 0; i < cd->nfiles; i++) {
		if(cd->files[i]) {
			fclose(cd->files[i]);
		}
	}
	memset(cd, 0, sizeof(*cd));
}

/* Gives the tracks of the file that was just read their lengths, and their
 * places on the disc. first: the first track of that file. */
static void
close_file(Cd_image *cd, int first, long long file_off, long long file_len)
{
	Cd_track *t;
	long long end;
	int	i;

	for(i = first; i < cd->ntracks; i++) {
		t = &cd->t[i];
		end = (i + 1 < cd->ntracks) ? cd->t[i + 1].off :
							file_off + file_len;
		t->frames = (end > t->off) ?
				(unsigned)((end - t->off) / t->secsize) : 0;
		t->start = cd->total;
		cd->total += t->frames;
	}
}

int
cdimage_open_cue(Cd_image *cd, const char *cue_path)
{
	char	line[1024], name[1024], path[2048], *p, *q;
	const char *slash;
	FILE	*cue, *f = 0;
	Cd_track *t = 0;
	long long file_off = 0, file_len = 0;
	int	first = 0, wave = 0, dirlen, m, s, fr, n;

	memset(cd, 0, sizeof(*cd));
	cue = fopen(cue_path, "r");
	if(!cue) {
		return -1;
	}
	slash = strrchr(cue_path, '/');
#ifdef _WIN32
	if(strrchr(cue_path, '\\') > slash) {
		slash = strrchr(cue_path, '\\');
	}
#endif
	dirlen = slash ? (int)(slash - cue_path + 1) : 0;
	while(fgets(line, sizeof(line), cue)) {
		for(p = line; isspace((unsigned char)*p); p++) {
		}
		if(word_is(p, "FILE")) {
			if(f) {
				close_file(cd, first, file_off, file_len);
			}
			f = 0;
			t = 0;
			first = cd->ntracks;
			p += 4;
			while(isspace((unsigned char)*p)) {
				p++;
			}
			if(*p == '"') {
				q = strchr(++p, '"');
			} else {
				for(q = p; *q && !isspace((unsigned char)*q); q++) {
				}
			}
			if(!q || q == p || (size_t)(q - p) >= sizeof(name)) {
				break;
			}
			memcpy(name, p, (size_t)(q - p));
			name[q - p] = 0;
			for(p = q + 1; isspace((unsigned char)*p); p++) {
			}
			wave = word_is(p, "WAVE");
			if(name[0] == '/' || dirlen == 0) {
				snprintf(path, sizeof(path), "%s", name);
			} else {
				snprintf(path, sizeof(path), "%.*s%s", dirlen,
							cue_path, name);
			}
			if(cd->nfiles >= CD_MAX_TRACKS) {
				break;
			}
			f = fopen(path, "rb");
			if(!f) {
				break;
			}
			cd->files[cd->nfiles++] = f;
			file_off = 0;
			if(wave) {
				if(wave_data(f, &file_off, &file_len) != 0) {
					f = 0;
					break;
				}
			} else {
				fseek64(f, 0, SEEK_END);
				file_len = ftell64(f);
			}
		} else if(word_is(p, "TRACK") && f) {
			if(cd->ntracks >= CD_MAX_TRACKS) {
				break;
			}
			t = &cd->t[cd->ntracks++];
			memset(t, 0, sizeof(*t));
			t->f = f;
			t->off = file_off;
			for(p += 5; isspace((unsigned char)*p); p++) {
			}
			while(isdigit((unsigned char)*p)) {
				p++;
			}
			while(isspace((unsigned char)*p)) {
				p++;
			}
			if(word_is(p, "AUDIO")) {
				t->audio = 1;
				t->secsize = CD_RAW;
			} else if(word_is(p, "MODE1/2048")) {
				t->secsize = 2048;
			} else if(word_is(p, "MODE1/2352")) {
				t->secsize = CD_RAW;
				t->data_off = 16;
			} else if(word_is(p, "MODE2/2352")) {
				t->secsize = CD_RAW;
				t->data_off = 24;
			} else {
				f = 0;			/* a kind not known */
				break;
			}
		} else if(word_is(p, "INDEX") && t) {
			n = 0;
			if(sscanf(p + 5, "%d %d:%d:%d", &n, &m, &s, &fr) == 4 &&
								n == 1) {
				t->off = file_off + (long long)((m * 60 + s) *
						75 + fr) * t->secsize;
			}
		}
	}
	fclose(cue);
	if(f) {
		close_file(cd, first, file_off, file_len);
	}
	if(!f || cd->ntracks == 0 || cd->total == 0) {
		cdimage_close(cd);
		return -1;
	}
	return 0;
}

int
cdimage_track_of(const Cd_image *cd, unsigned lba)
{
	int	i;

	for(i = 0; i < cd->ntracks; i++) {
		if(lba >= cd->t[i].start &&
				lba < cd->t[i].start + cd->t[i].frames) {
			return i;
		}
	}
	return -1;
}

static int
read_at(Cd_track *t, unsigned lba, int skip, unsigned char *buf, int n)
{
	long long off = t->off + (long long)(lba - t->start) * t->secsize + skip;

	if(fseek64(t->f, off, SEEK_SET) != 0 ||
				fread(buf, 1, (size_t)n, t->f) != (size_t)n) {
		return -1;
	}
	return 0;
}

int
cdimage_read_data(Cd_image *cd, unsigned lba, unsigned char *buf)
{
	int	i = cdimage_track_of(cd, lba);

	if(i < 0 || cd->t[i].audio) {
		memset(buf, 0, 2048);
		return i < 0 ? -1 : 0;
	}
	return read_at(&cd->t[i], lba, cd->t[i].data_off, buf, 2048);
}

int
cdimage_read_audio(Cd_image *cd, unsigned lba, unsigned char *buf)
{
	int	i = cdimage_track_of(cd, lba);

	if(i < 0 || !cd->t[i].audio) {
		memset(buf, 0, CD_RAW);
		return i < 0 ? -1 : 0;
	}
	return read_at(&cd->t[i], lba, 0, buf, CD_RAW);
}
