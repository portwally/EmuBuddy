/* EmuBuddy: read-only ProDOS disk image access and file export. See
 * prodos_img.h. */
#include "prodos_img.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* ProDOS sector -> DOS 3.3 sector, for 5.25" images stored in DOS order */
static const int g_p2d[16] = { 0, 13, 11, 9, 7, 5, 3, 1,
				14, 12, 10, 8, 6, 4, 2, 15 };

static int
read_at(PImg *img, long pos, uint8_t *buf, size_t n)
{
	if(fseek(img->f, img->offset + pos, SEEK_SET) != 0) {
		return -1;
	}
	return fread(buf, 1, n, img->f) == n ? 0 : -1;
}

static int
read_block(PImg *img, unsigned blk, uint8_t *buf)
{
	if(blk >= img->blocks) {
		return -1;
	}
	if(img->dos_order) {
		unsigned trk = blk / 8, s0 = (blk % 8) * 2;

		if(read_at(img, (long)(trk * 16 + g_p2d[s0]) * 256, buf, 256) ||
				read_at(img, (long)(trk * 16 + g_p2d[s0 + 1]) * 256,
							buf + 256, 256)) {
			return -1;
		}
		return 0;
	}
	return read_at(img, (long)blk * 512, buf, 512);
}

static const char *
ext_of(const char *path)
{
	const char *d = strrchr(path, '.');

	return d ? d + 1 : "";
}

int
pimg_open(PImg *img, const char *path, const char **err)
{
	uint8_t		hdr[64], vd[512];
	long		size;
	const char	*ext = ext_of(path);
	int		n;

	memset(img, 0, sizeof(*img));
	*err = "Cannot open the file";
	img->f = fopen(path, "rb");
	if(!img->f) {
		return -1;
	}
	fseek(img->f, 0, SEEK_END);
	size = ftell(img->f);
	fseek(img->f, 0, SEEK_SET);
	if(fread(hdr, 1, 64, img->f) == 64 && !memcmp(hdr, "2IMG", 4)) {
		unsigned fmt = hdr[12] | (hdr[13] << 8);
		unsigned off = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16);
		unsigned len = hdr[28] | (hdr[29] << 8) | (hdr[30] << 16) |
							((unsigned)hdr[31] << 24);

		img->offset = off;
		size = len ? (long)len : size - off;
		img->dos_order = (fmt == 0);
		if(fmt == 2) {
			*err = "Nibble images can't be browsed";
			pimg_close(img);
			return -1;
		}
	} else if(!strcasecmp(ext, "dsk") || !strcasecmp(ext, "do")) {
		img->dos_order = (size == 143360);
	} else if(!strcasecmp(ext, "woz") || !strcasecmp(ext, "nib") ||
			!strcasecmp(ext, "shk") || !strcasecmp(ext, "sdk") ||
			!strcasecmp(ext, "gz") || !strcasecmp(ext, "zip")) {
		*err = "This image type can't be browsed yet";
		pimg_close(img);
		return -1;
	}
	img->blocks = (uint32_t)(size / 512);
	if(img->blocks > 65535) {
		img->blocks = 65535;
	}
	if(read_block(img, 2, vd) != 0 || (vd[4] >> 4) != 0xF ||
						vd[0x23] != 0x27) {
		*err = "Not a ProDOS disk";
		pimg_close(img);
		return -1;
	}
	n = vd[4] & 15;
	memcpy(img->volume, vd + 5, (size_t)n);
	img->volume[n] = 0;
	*err = "";
	return 0;
}

void
pimg_close(PImg *img)
{
	if(img->f) {
		fclose(img->f);
	}
	img->f = NULL;
}

int
pimg_list(PImg *img, unsigned key, PEntry *out, int max)
{
	uint8_t		b[512];
	int		n = 0, i, guard = 0;
	unsigned	blk = key;

	while(blk && guard++ < 2000) {
		if(read_block(img, blk, b) != 0) {
			return n ? n : -1;
		}
		for(i = 0; i < 13; i++) {
			const uint8_t *e = b + 4 + i * 39;
			int st = e[0] >> 4, len = e[0] & 15;

			if(st == 0 || st == 0xE || st == 0xF) {
				continue;	/* deleted or header */
			}
			if(n >= max) {
				return n;
			}
			memcpy(out[n].name, e + 1, (size_t)len);
			out[n].name[len] = 0;
			out[n].storage = st;
			out[n].type = e[0x10];
			out[n].key = e[0x11] | (e[0x12] << 8);
			out[n].blocks_used = e[0x13] | (e[0x14] << 8);
			out[n].eof = e[0x15] | (e[0x16] << 8) | (e[0x17] << 16);
			out[n].access = e[0x1E];
			out[n].aux = e[0x1F] | (e[0x20] << 8);
			if(st == 5) {
				/* forked file: real size is data + resource */
				uint8_t x[512];

				if(read_block(img, out[n].key, x) == 0) {
					out[n].eof = (x[5] | (x[6] << 8) |
						(x[7] << 16)) + (x[256 + 5] |
						(x[256 + 6] << 8) |
						(x[256 + 7] << 16));
				}
			}
			n++;
		}
		blk = b[2] | (b[3] << 8);
	}
	return n;
}

/* Read a fork stored as seedling (1), sapling (2) or tree (3). */
static uint8_t *
read_fork(PImg *img, int st, unsigned key, unsigned eof)
{
	uint8_t		*data = calloc(eof ? eof : 1, 1), b[512], ix[512], mx[512];
	unsigned	nblk = (eof + 511) / 512, i, blk, pos;

	if(!data) {
		return NULL;
	}
	for(i = 0; i < nblk; i++) {
		blk = 0;
		if(st == 1) {
			blk = (i == 0) ? key : 0;
		} else if(st == 2) {
			if(i == 0 && read_block(img, key, ix) != 0) {
				break;
			}
			blk = (i < 256) ? (unsigned)(ix[i] | (ix[256 + i] << 8)) : 0;
		} else if(st == 3) {
			if(i == 0 && read_block(img, key, mx) != 0) {
				break;
			}
			if(i % 256 == 0) {
				unsigned ib = mx[i / 256] | (mx[256 + i / 256] << 8);

				memset(ix, 0, sizeof(ix));
				if(ib && read_block(img, ib, ix) != 0) {
					break;
				}
			}
			blk = ix[i % 256] | (ix[256 + i % 256] << 8);
		}
		pos = i * 512;
		if(blk == 0) {
			continue;		/* sparse block: zeros */
		}
		if(read_block(img, blk, b) != 0) {
			break;
		}
		memcpy(data + pos, b, (eof - pos) < 512 ? eof - pos : 512);
	}
	return data;
}

int
pimg_read_file(PImg *img, const PEntry *e, uint8_t **data, unsigned *dlen,
					uint8_t **rsrc, unsigned *rlen)
{
	uint8_t	ext[512];

	*data = NULL;
	*dlen = 0;
	if(rsrc) {
		*rsrc = NULL;
		*rlen = 0;
	}
	if(e->storage >= 1 && e->storage <= 3) {
		*data = read_fork(img, e->storage, e->key, e->eof);
		*dlen = e->eof;
		return *data ? 0 : -1;
	}
	if(e->storage != 5 || read_block(img, e->key, ext) != 0) {
		return -1;
	}
	*dlen = ext[5] | (ext[6] << 8) | (ext[7] << 16);
	*data = read_fork(img, ext[0] & 15, ext[1] | (ext[2] << 8), *dlen);
	if(rsrc) {
		*rlen = ext[256 + 5] | (ext[256 + 6] << 8) | (ext[256 + 7] << 16);
		*rsrc = read_fork(img, ext[256] & 15, ext[257] | (ext[258] << 8),
								*rlen);
	}
	return *data ? 0 : -1;
}

const char *
pimg_type_name(int type)
{
	static char	buf[8];

	switch(type) {
	case 0x04: return "TXT";
	case 0x06: return "BIN";
	case 0x0F: return "DIR";
	case 0x19: return "ADB";
	case 0x1A: return "AWP";
	case 0x1B: return "ASP";
	case 0xB0: return "SRC";
	case 0xB3: return "S16";
	case 0xB6: return "PIF";
	case 0xB8: return "NDA";
	case 0xB9: return "CDA";
	case 0xBA: return "TOL";
	case 0xBC: return "LDF";
	case 0xC0: return "PNT";
	case 0xC1: return "PIC";
	case 0xC7: return "CDV";
	case 0xC9: return "FND";
	case 0xCA: return "ICN";
	case 0xD5: return "MUS";
	case 0xD8: return "SND";
	case 0xE0: return "LBR";
	case 0xEF: return "PAS";
	case 0xF0: return "CMD";
	case 0xFA: return "INT";
	case 0xFC: return "BAS";
	case 0xFF: return "SYS";
	}
	snprintf(buf, sizeof(buf), "$%02X", type & 0xff);
	return buf;
}

static void
be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void
be16(uint8_t *p, unsigned v)
{
	p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}

int
pimg_export(PImg *img, const PEntry *e, const char *dir, char *out, int outlen)
{
	uint8_t		*data, *rsrc;
	unsigned	dlen, rlen;
	char		base[64];
	FILE		*f;
	int		i, ok;

	if(e->storage == 0xD) {
		return -1;			/* folders: export contents */
	}
	if(pimg_read_file(img, e, &data, &dlen, &rsrc, &rlen) != 0) {
		return -1;
	}
	for(i = 0; e->name[i]; i++) {
		base[i] = e->name[i];
	}
	base[i] = 0;
	if(rlen > 0) {
		/* AppleSingle keeps both forks plus the ProDOS type */
		uint8_t hdr[26 + 4 * 12 + 8];
		unsigned nl = (unsigned)strlen(base), o = 26 + 4 * 12;

		snprintf(out, (size_t)outlen, "%s/%s.applesingle", dir, base);
		f = fopen(out, "wb");
		if(!f) {
			free(data);
			free(rsrc);
			return -1;
		}
		memset(hdr, 0, sizeof(hdr));
		be32(hdr, 0x00051600);
		be32(hdr + 4, 0x00020000);
		be16(hdr + 24, 4);
		/* entries: 3 real name, 11 ProDOS info, 2 resource, 1 data */
		be32(hdr + 26, 3); be32(hdr + 30, o); be32(hdr + 34, nl);
		be32(hdr + 38, 11); be32(hdr + 42, o + nl); be32(hdr + 46, 8);
		be32(hdr + 50, 2); be32(hdr + 54, o + nl + 8); be32(hdr + 58, rlen);
		be32(hdr + 62, 1); be32(hdr + 66, o + nl + 8 + rlen);
		be32(hdr + 70, dlen);
		fwrite(hdr, 1, o, f);
		fwrite(base, 1, nl, f);
		{
			uint8_t info[8];

			be16(info, (unsigned)e->access);
			be16(info + 2, (unsigned)e->type);
			be32(info + 4, e->aux);
			fwrite(info, 1, 8, f);
		}
		fwrite(rsrc, 1, rlen, f);
		fwrite(data, 1, dlen, f);
		ok = (fclose(f) == 0);
	} else {
		if(e->type == 0x04 && e->aux == 0) {
			snprintf(out, (size_t)outlen, "%s/%s.txt", dir, base);
		} else {
			snprintf(out, (size_t)outlen, "%s/%s,t$%02X,a$%04X", dir,
						base, e->type & 0xff, e->aux & 0xffff);
		}
		f = fopen(out, "wb");
		if(!f) {
			free(data);
			free(rsrc);
			return -1;
		}
		/* byte for byte (text keeps CR line ends) so files round-trip
		 * through folder volumes unchanged */
		fwrite(data, 1, dlen, f);
		ok = (fclose(f) == 0);
	}
	free(data);
	free(rsrc);
	return ok ? 0 : -1;
}
