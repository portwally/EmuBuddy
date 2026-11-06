/* EmuBuddy: read-only access to ProDOS disk images (.po .hdv .2mg .dsk .do)
 * for the Disk Browser, and export of files to the host in the naming the
 * core's folder volumes (dynapro) understand, so files round-trip. */
#ifndef EMUBUDDY_PRODOS_IMG_H
#define EMUBUDDY_PRODOS_IMG_H

#include <stdio.h>
#include <stdint.h>

typedef struct {
	FILE		*f;
	long		offset;		/* start of disk data in the file */
	int		dos_order;	/* 5.25" image in DOS 3.3 sector order */
	uint32_t	blocks;
	char		volume[16];
} PImg;

typedef struct {
	char		name[16];
	int		storage;	/* 1-3 file, 5 extended (forks), 0xD dir */
	int		type;
	unsigned	aux;
	unsigned	key;
	unsigned	eof;
	unsigned	blocks_used;
	int		access;
} PEntry;

/* Returns 0 on success; on failure, *err says why. */
int	pimg_open(PImg *img, const char *path, const char **err);
void	pimg_close(PImg *img);
/* List a directory (key block 2 = volume root). Returns count, -1 on error. */
int	pimg_list(PImg *img, unsigned dir_key, PEntry *out, int max);
/* Read a file's forks (rsrc may be NULL; no resource fork gives len 0).
 * Buffers are malloc'ed. Returns 0 on success. */
int	pimg_read_file(PImg *img, const PEntry *e, uint8_t **data, unsigned *dlen,
					uint8_t **rsrc, unsigned *rlen);
/* Write a file into host directory dir; returns 0 and the full path made. */
int	pimg_export(PImg *img, const PEntry *e, const char *dir, char *out_path,
							int out_len);
/* Short type name ("TXT", "BIN", "S16", or "$C1"). */
const char *pimg_type_name(int type);

#endif
