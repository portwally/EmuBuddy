#include "../src/library.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int fails;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while(0)

static void
mk(const char *path, long size)
{
	FILE *f = fopen(path, "wb");

	fseek(f, size - 1, SEEK_SET);
	fputc(0, f);
	fclose(f);
}

int
main(void)
{
	static char	folders[1][1100];
	Library		l;
	char		label[64];

	system("rm -rf /tmp/eblib && mkdir -p /tmp/eblib/sub/deep");
	mk("/tmp/eblib/ROM03", 262144);
	mk("/tmp/eblib/sub/Apple IIgs ROM 01.bin", 131072);
	mk("/tmp/eblib/sub/System6.hdv", 32 * 1024 * 1024);
	mk("/tmp/eblib/sub/deep/Game.2mg", 800 * 1024 + 64);
	mk("/tmp/eblib/sub/deep/Old.dsk", 143360);
	mk("/tmp/eblib/notes.txt", 5000);
	mk("/tmp/eblib/random.bin", 1000);
	mk("/tmp/eblib/.hidden.po", 143360);
	snprintf(folders[0], sizeof(folders[0]), "/tmp/eblib");
	lib_init(&l);
	lib_scan(&l, folders, 1);
	CHECK(l.n == 5);
	CHECK(l.items[0].kind == LIB_ROM && l.items[1].kind == LIB_ROM);
	CHECK(strstr(l.items[0].label, "ROM 01") || strstr(l.items[1].label, "ROM 01"));
	CHECK(lib_classify("a.po", 800 * 1024, label, sizeof(label)) == LIB_DISK);
	CHECK(!strcmp(label, "3.5\" 800 KB"));
	CHECK(lib_classify("x.txt", 5, label, sizeof(label)) == -1);
	lib_scan(&l, folders, 1);		/* again: no duplicates, no growth */
	CHECK(l.n == 5);
	/* duplicates, damage, tags, search */
	system("cp /tmp/eblib/sub/System6.hdv \"/tmp/eblib/sub/Copy of System6.hdv\"");
	mk("/tmp/eblib/sub/bad.po", 1000);
	lib_scan(&l, folders, 1);
	lib_check(&l);
	{
		int dups = 0, bad = 0, i;

		for(i = 0; i < l.n; i++) {
			dups += !!(l.items[i].flags & LIB_DUP);
			bad += !!(l.items[i].flags & LIB_BAD);
		}
		CHECK(dups == 1);
		CHECK(bad >= 1);
	}
	lib_tags_set(&l, 0, "web, work", "/tmp/eblib_tags.txt");
	lib_scan(&l, folders, 1);
	lib_tags_apply(&l, "/tmp/eblib_tags.txt");
	CHECK(!strcmp(l.items[0].tags, "web, work"));
	CHECK(lib_matches(&l.items[0], "WORK"));
	CHECK(lib_matches(&l.items[0], "rom work"));
	CHECK(!lib_matches(&l.items[0], "nothing"));
	CHECK(lib_matches(&l.items[0], ""));
	remove("/tmp/eblib_tags.txt");
	lib_free(&l);
	system("rm -rf /tmp/eblib");
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
