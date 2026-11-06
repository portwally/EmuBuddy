/* EmuBuddy: ask the GS/OS Finder which folder its front window shows, by
 * reading the emulated RAM (no helper program inside the IIgs).
 *
 * What we rely on (found on System 6.0.x, see PLAN.md "Finder window spike"):
 *  - Each window is a Window Manager record (WindRec) with a stable byte
 *    pattern, a title pointer at +0xD4 and the Finder's own block at +0xFC
 *    (wRefCon). The Finder block holds the window's full GS/OS path
 *    (":Volume:Folder").
 *  - The front window's address is referenced several times from the Window
 *    Manager's data in bank 0; windows that were closed are not.
 * Everything is checked for plausibility; anything odd means "don't know". */
#include "defc.h"
#include <string.h>
#include <stdio.h>

extern byte	*g_memory_ptr;
extern word32	g_mem_size_total;

#define WR_SIZE		0x110

static int
in_ram(word32 a, word32 n)
{
	return a >= 0x100 && a + n < g_mem_size_total;
}

static unsigned
rd16(word32 a)
{
	return g_memory_ptr[a] | (g_memory_ptr[a + 1] << 8);
}

static word32
rd32(word32 a)
{
	return rd16(a) | ((word32)rd16(a + 2) << 16);
}

/* Does a window record start at b? */
static int
looks_like_windrec(word32 b)
{
	static const byte ff[32] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff };
	word32	title, ref;
	unsigned len, i;

	if(rd16(b + 0x44) != 0x001d || rd16(b + 0x48) != 1 ||
			rd16(b + 0x4a) != 1) {
		return 0;
	}
	if(memcmp(g_memory_ptr + b + 0x24, ff, 32) != 0 ||
			memcmp(g_memory_ptr + b + 0x6e, ff, 8) != 0) {
		return 0;
	}
	title = rd32(b + 0xd4);
	ref = rd32(b + 0xfc);
	if(!in_ram(title, 64) || !in_ram(ref, 0x200)) {
		return 0;
	}
	len = g_memory_ptr[title];
	if(len < 1 || len > 60) {
		return 0;
	}
	for(i = 1; i <= len; i++) {
		if(g_memory_ptr[title + i] < 0x20 || g_memory_ptr[title + i] > 0x7e) {
			return 0;
		}
	}
	return 1;
}

/* How often is this window pointer stored in bank 0 (Window Manager data)? */
static int
bank0_refs(word32 b)
{
	word32	a;
	int	n = 0;

	for(a = 0; a + 4 <= 0x10000; a++) {
		if(g_memory_ptr[a] == (b & 0xff) &&
				g_memory_ptr[a + 1] == ((b >> 8) & 0xff) &&
				g_memory_ptr[a + 2] == ((b >> 16) & 0xff) &&
				g_memory_ptr[a + 3] == 0) {
			n++;
		}
	}
	return n;
}

/* The path (":Volume:Folder") the Finder keeps with a window. */
static int
finder_block_path(word32 ref, char *out, size_t n)
{
	word32	p;
	unsigned len, i;

	for(p = ref + 0x30; p < ref + 0x150; p++) {
		len = rd16(p);
		if(len < 2 || len > 200 || g_memory_ptr[p + 2] != ':' ||
				!in_ram(p, len + 4) || len + 1 > n) {
			continue;
		}
		for(i = 0; i < len; i++) {
			unsigned char c = g_memory_ptr[p + 2 + i];

			if(c < 0x20 || c > 0x7e) {
				break;
			}
		}
		if(i != len || g_memory_ptr[p + 2 + len] != 0) {
			continue;
		}
		memcpy(out, g_memory_ptr + p + 2, len);
		out[len] = 0;
		return 1;
	}
	return 0;
}

/* The GS/OS path of the front Finder window. Returns 1 and fills out, or 0. */
int
finder_front_path(char *out, size_t n)
{
	word32	b, best = 0;
	int	score, best_score = 1;
	word32	limit;

	if(!g_memory_ptr || g_mem_size_total < 0x100000) {
		return 0;
	}
	limit = g_mem_size_total - WR_SIZE - 0x200;
	for(b = 0x100; b < limit; b++) {
		if(g_memory_ptr[b + 0x44] != 0x1d || g_memory_ptr[b + 0x45] != 0) {
			continue;
		}
		if(!looks_like_windrec(b)) {
			continue;
		}
		score = bank0_refs(b);
		if(score > best_score) {
			best_score = score;
			best = b;
		}
	}
	if(!best) {
		return 0;
	}
	return finder_block_path(rd32(best + 0xfc), out, n);
}
