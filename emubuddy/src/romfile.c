/**********************************************************************/
/*                    EmuBuddy - Apple //gs Emulator                  */
/*                    Copyright 2026 EmuBuddy Contributors            */
/*                                                                    */
/*      This code is covered by the GNU GPL v3                        */
/*      See the file COPYING.txt or https://www.gnu.org/licenses/     */
/**********************************************************************/

/* Reads the ROM of a card (the High-Speed SCSI Card, the Votrax SC-01):
 * a file of exactly its size, or a zip file (MAME's sets) with it inside. */

#include "defc.h"
#include <ctype.h>
#include <string.h>

/* Reads the file named want (any case, any folder inside the zip) out of
 * a zip file, size bytes. 0 when it worked. The zip directory is read
 * here: EmuBuddy's zip reader for disk images (cfg_partition_make_list)
 * leaves out files under 140 KB. */
static int
rom_zip_read(const char *path, const char *want, byte *out, word32 size)
{
	Disk	tmp;
	byte	buf[1024], *dir, *e;
	dword64	dsize, dlen, dir_off, dir_size, off, local;
	word32	entries, method, csize, usize, name_len, hdr_len, i, j, wlen;
	int	fd, pos, ret = -1;

	wlen = (word32)strlen(want);
	fd = open(path, O_RDONLY | O_BINARY, 0x1b6);
	if(fd < 0) {
		return -1;
	}
	dsize = cfg_get_fd_size(fd);
	dlen = (dsize > sizeof(buf)) ? sizeof(buf) : dsize;
	dir = 0;
	if(dlen < 22 || cfg_read_from_fd(fd, buf, dsize - dlen, dlen) != dlen) {
		goto done;
	}
	/* the "end of central directory" record is the last one in the file */
	for(pos = (int)dlen - 22; pos >= 0; pos--) {
		if(buf[pos] == 'P' && buf[pos + 1] == 'K' &&
				buf[pos + 2] == 5 && buf[pos + 3] == 6) {
			break;
		}
	}
	if(pos < 0) {
		goto done;
	}
	entries = cfg_get_le16(&buf[pos + 10]);
	dir_size = cfg_get_le32(&buf[pos + 12]);
	dir_off = cfg_get_le32(&buf[pos + 16]);
	if(dir_size > (1 << 20) || (dir_off + dir_size) > dsize) {
		goto done;
	}
	dir = malloc((size_t)dir_size + 1);
	if(!dir || cfg_read_from_fd(fd, dir, dir_off, dir_size) != dir_size) {
		goto done;
	}
	for(i = 0, off = 0; i < entries && (off + 46) <= dir_size; i++) {
		e = dir + off;
		if(e[0] != 'P' || e[1] != 'K' || e[2] != 1 || e[3] != 2) {
			break;
		}
		method = cfg_get_le16(e + 10);
		csize = cfg_get_le32(e + 20);
		usize = cfg_get_le32(e + 24);
		name_len = cfg_get_le16(e + 28);
		hdr_len = 46 + name_len + cfg_get_le16(e + 30) +
							cfg_get_le16(e + 32);
		local = cfg_get_le32(e + 42);
		if((off + hdr_len) > dir_size) {
			break;
		}
		off += hdr_len;
		if(usize != size || name_len < wlen) {
			continue;
		}
		/* the name ends in want, in any case */
		e += 46 + name_len - wlen;
		for(j = 0; j < wlen; j++) {
			if(tolower(e[j]) != tolower((byte)want[j])) {
				break;
			}
		}
		if(j < wlen) {
			continue;
		}
		if(method == 0) {		/* stored */
			if(cfg_read_from_fd(fd, buf, local, 30) != 30) {
				break;
			}
			local += 30 + cfg_get_le16(&buf[26]) +
							cfg_get_le16(&buf[28]);
			if(cfg_read_from_fd(fd, out, local, size) == size) {
				ret = 0;
			}
		} else if(method == 8) {	/* deflate */
			memset(&tmp, 0, sizeof(tmp));
			tmp.fd = -1;
			if(undeflate_zipfile(&tmp, fd, local, usize, csize)
					== 0 && tmp.raw_data &&
					tmp.raw_dsize == size) {
				memcpy(out, tmp.raw_data, size);
				ret = 0;
			}
			free(tmp.raw_data);
		}
		break;
	}
done:
	free(dir);
	close(fd);
	return ret;
}

/* Reads a card ROM of size bytes from path: the file itself (exactly
 * that size), or, for a .zip, the file named want inside. 0 when it
 * worked. */
int
rom_file_read(const char *path, const char *want, byte *out, word32 size)
{
	FILE	*f;
	long	len;
	size_t	plen = strlen(path);

	if(plen > 4 && !cfgcasecmp(path + plen - 4, ".zip")) {
		return rom_zip_read(path, want, out, size);
	}
	f = fopen(path, "rb");
	if(!f) {
		return -1;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(len != (long)size || fread(out, 1, size, f) != size) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}
