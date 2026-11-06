/* Host test for the bridge (bridge.c): drives the commands against a flat RAM
 * array and a scratch folder. Build and run:
 *   cmake --build build --target test_bridge && build/test_bridge */
#include "../bridge.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned char	ram[0x1000000];
static int		fails;

static unsigned rd(unsigned long a) { return ram[a & 0xffffff]; }
static void wr(unsigned long a, unsigned v) { ram[a & 0xffffff] = (unsigned char)v; }

#define BLK	0x1000
#define STR	0x2000
#define BUF	0x10000

#define CHECK(cond, ...) do { if(!(cond)) { printf("FAIL line %d: ", __LINE__); \
	printf(__VA_ARGS__); printf("\n"); fails++; } } while(0)

static void w16(unsigned long a, unsigned v) { wr(a, v); wr(a + 1, v >> 8); }
static void w32(unsigned long a, unsigned long v) { w16(a, v & 0xffff); w16(a + 2, v >> 16); }
static unsigned long r32(unsigned long a) { return rd(a) | (rd(a+1) << 8) | (rd(a+2) << 16) | ((unsigned long)rd(a+3) << 24); }

/* put a path at addr (u16 length + chars), return addr */
static unsigned long path(unsigned long addr, const char *s)
{
	w16(addr, (unsigned)strlen(s));
	memcpy(ram + addr + 2, s, strlen(s));
	return addr;
}

static int call(unsigned cmd, unsigned long p1, unsigned long p2, unsigned long p3,
							unsigned long p4)
{
	w16(BLK, cmd);
	w16(BLK + 2, 0xffff);
	w32(BLK + 4, p1);
	w32(BLK + 8, p2);
	w32(BLK + 12, p3);
	w32(BLK + 16, p4);
	return bridge_command(BLK);
}

static int overlap_yes(const char *f) { (void)f; return 1; }

int
main(void)
{
	char	root[1024], outside[1024], name[300], buf[300];
	int	t, a, rc;
	unsigned long h;

	bridge_set_memory(rd, wr);
	snprintf(root, sizeof(root), "%s/emubuddy-bridge-test-%d", getenv("TMPDIR") ?
					getenv("TMPDIR") : "/tmp", (int)getpid());
	snprintf(outside, sizeof(outside), "%s-outside", root);
	SDL_CreateDirectory(root);
	SDL_CreateDirectory(outside);

	/* names */
	bridge_decode_name("HELLO.txt", name, sizeof(name), &t, &a);
	CHECK(!strcmp(name, "HELLO") && t == 4 && a == 0, "decode txt: %s %d %d", name, t, a);
	bridge_decode_name("GAME,t$B3,a$DB03", name, sizeof(name), &t, &a);
	CHECK(!strcmp(name, "GAME") && t == 0xb3 && a == 0xdb03, "decode: %s %x %x", name, t, a);
	bridge_decode_name("PIC#C10000", name, sizeof(name), &t, &a);
	CHECK(!strcmp(name, "PIC") && t == 0xc1 && a == 0, "decode cadius: %s %x %x", name, t, a);
	bridge_decode_name("PLAIN", name, sizeof(name), &t, &a);
	CHECK(!strcmp(name, "PLAIN") && t == 6 && a == 0, "decode plain");
	bridge_encode_name("GAME", 0xb3, 0xdb03, buf, sizeof(buf));
	CHECK(!strcmp(buf, "GAME,t$B3,a$DB03"), "encode: %s", buf);
	bridge_encode_name("README", 4, 0, buf, sizeof(buf));
	CHECK(!strcmp(buf, "README.txt"), "encode txt: %s", buf);
	bridge_encode_name("DATA", 6, 0, buf, sizeof(buf));
	CHECK(!strcmp(buf, "DATA"), "encode bin: %s", buf);

	/* no folder set */
	g_bridge_root = "";
	rc = call(0, 0, 0, 0, 0);
	CHECK(rc == 0x2f, "INFO with no folder: %x", rc);
	CHECK(r32(BLK + 0x14) == BRIDGE_VERSION, "version");
	CHECK(call(0x10, 0, BUF, 4096, 0) == 0x2f, "LIST with no folder");

	g_bridge_root = root;
	w32(BLK + 4, 0);
	CHECK(call(0, BUF, 64, 0, 0) == 0, "INFO");

	/* folder + file with type */
	CHECK(call(0x11, path(STR, "docs"), 0, 0, 0) == 0, "MKDIR");
	CHECK(call(0x11, path(STR, "docs"), 0, 0, 0) == 0x47, "MKDIR again = duplicate");
	rc = call(0x20, path(STR, "docs/Hello"), 2, 4, 0);
	CHECK(rc == 0, "OPEN write: %x", rc);
	h = r32(BLK + 0x14);
	memcpy(ram + BUF, "hello", 5);
	CHECK(call(0x22, h, BUF, 5, 0) == 0 && r32(BLK + 0x14) == 5, "WRITE");
	CHECK(call(0x23, h, 0, 0, 0) == 0, "CLOSE");
	{
		char p[1200];
		snprintf(p, sizeof(p), "%s/docs/Hello.txt", root);
		CHECK(SDL_GetPathInfo(p, NULL), "host file Hello.txt exists");
	}
	rc = call(0x20, path(STR, "docs/GAME"), 2, 0xb3, 0xdb03);
	CHECK(rc == 0, "OPEN write typed");
	call(0x23, r32(BLK + 0x14), 0, 0, 0);
	{
		char p[1200];
		snprintf(p, sizeof(p), "%s/docs/GAME,t$B3,a$DB03", root);
		CHECK(SDL_GetPathInfo(p, NULL), "host file with type suffix exists");
	}

	/* read back, case-insensitive */
	rc = call(0x20, path(STR, "DOCS/hello"), 1, 0, 0);
	CHECK(rc == 0, "OPEN read: %x", rc);
	h = r32(BLK + 0x14);
	CHECK(r32(BLK + 0x18) == 5 && r32(BLK + 0x1c) == 4, "size/type from OPEN");
	memset(ram + BUF, 0, 16);
	CHECK(call(0x21, h, BUF, 100, 0) == 0 && r32(BLK + 0x14) == 5 &&
			!memcmp(ram + BUF, "hello", 5), "READ");
	CHECK(call(0x21, h, BUF, 100, 0) == 0 && r32(BLK + 0x14) == 0, "READ at end");
	CHECK(call(0x23, h, 0, 0, 0) == 0, "CLOSE");
	CHECK(call(0x23, h, 0, 0, 0) == 0x43, "CLOSE twice");
	CHECK(call(0x20, path(STR, "docs/nothere"), 1, 0, 0) == 0x46, "open missing");
	CHECK(call(0x20, path(STR, "nodir/x"), 2, 6, 0) == 0x44, "path not found");

	/* listing */
	rc = call(0x10, path(STR, "docs"), BUF, 4096, 0);
	CHECK(rc == 0 && r32(BLK + 0x14) == 2, "LIST count %lu", r32(BLK + 0x14));
	{
		unsigned off = 0, n = 0;
		char names[4][64];

		while(n < 2) {
			unsigned len = rd(BUF + off) | (rd(BUF + off + 1) << 8);
			unsigned nl = rd(BUF + off + 10) | (rd(BUF + off + 11) << 8);
			memcpy(names[n], ram + BUF + off + 12, nl);
			names[n][nl] = 0;
			if(!strcmp(names[n], "Hello")) {
				CHECK(rd(BUF + off + 3) == 4, "Hello type");
			}
			off += len;
			n++;
		}
		CHECK(!strcmp(names[0], "GAME") && !strcmp(names[1], "Hello"),
				"sorted names: %s %s", names[0], names[1]);
	}
	/* paging: room for one entry only */
	rc = call(0x10, path(STR, "docs"), BUF, 20, 0);
	CHECK(rc == 0 && r32(BLK + 0x14) == 1 && r32(BLK + 0x18) == 1, "LIST paging");

	/* GETINFO */
	CHECK(call(0x13, path(STR, "docs/GAME"), 0, 0, 0) == 0 &&
			r32(BLK + 0x1c) == 0xb3 && r32(BLK + 0x20) == 0xdb03, "GETINFO type");
	CHECK(call(0x13, path(STR, "docs"), 0, 0, 0) == 0 && r32(BLK + 0x14) == 1, "GETINFO dir");

	/* jail */
	CHECK(call(0x20, path(STR, "../x"), 2, 6, 0) == 0x40, "dot-dot refused");
	CHECK(call(0x20, path(STR, "docs/../../x"), 2, 6, 0) == 0x40, "dot-dot inside refused");
	CHECK(call(0x20, path(STR, "docs\\x"), 2, 6, 0) == 0x40, "backslash refused");
	CHECK(call(0x20, path(STR, "/etc/passwd"), 1, 0, 0) == 0x44, "absolute becomes relative");
	{
		char link[1200];

		snprintf(link, sizeof(link), "%s/escape", root);
		if(symlink(outside, link) == 0) {
			rc = call(0x10, path(STR, "escape"), BUF, 4096, 0);
			CHECK(rc != 0, "symlink out of the folder must not list (got %x)", rc);
			rc = call(0x20, path(STR, "escape/x"), 2, 6, 0);
			CHECK(rc != 0, "symlink out of the folder must not write (got %x)", rc);
			unlink(link);
		}
	}

	/* delete */
	CHECK(call(0x12, path(STR, "docs/GAME"), 0, 0, 0) == 0, "DELETE file");
	CHECK(call(0x12, path(STR, "docs"), 0, 0, 0) != 0, "DELETE non-empty folder refused");
	CHECK(call(0x12, path(STR, "docs/hello"), 0, 0, 0) == 0, "DELETE file 2");
	CHECK(call(0x12, path(STR, "docs"), 0, 0, 0) == 0, "DELETE empty folder");

	/* a folder that is also mounted as a disk is refused */
	bridge_overlap_check = overlap_yes;
	CHECK(call(0x10, 0, BUF, 4096, 0) == 0x4e, "overlap refused");
	bridge_overlap_check = NULL;

	bridge_close_all();
	SDL_RemovePath(root);
	SDL_RemovePath(outside);
	printf(fails ? "%d FAILED\n" : "all bridge tests passed\n", fails);
	return fails ? 1 : 0;
}
