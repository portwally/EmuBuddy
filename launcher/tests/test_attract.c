#include "../src/attract.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while(0)

int
main(void)
{
	static char	names[ATTRACT_MAX_EMUS][100];
	Profile		prof[8];
	char		err[200];
	int		n;
	FILE		*f;

	system("rm -rf /tmp/ebatt && mkdir -p /tmp/ebatt/emulators /tmp/ebatt/romlists /tmp/ebatt/roms/art");
	f = fopen("/tmp/ebatt/emulators/Apple IIGS.cfg", "w");
	fputs("# Generated\nexecutable           sh\nrompath              /tmp/ebatt/roms\nromext               .sh\n"
		"artwork    snap            /tmp/ebatt/roms/video;/tmp/ebatt/roms/snap\nartwork    wheel           /tmp/ebatt/roms/wheel\n", f);
	fclose(f);
	f = fopen("/tmp/ebatt/emulators/Atari 2600.cfg", "w");
	fputs("rompath /tmp/x\n", f);
	fclose(f);
	f = fopen("/tmp/ebatt/romlists/Apple IIGS.txt", "w");
	fputs("#Name;Title;Emulator;CloneOf\nGames Collection;Games Collection;Apple IIGS;\nsystem;System;Apple IIGS;\n", f);
	fclose(f);
	f = fopen("/tmp/ebatt/roms/Games Collection.sh", "w");
	fputs("cd /tmp/ebatt/roms && /usr/bin/mame64 apple2gs -skip_gameinfo -ramsize 4M -gameio joy -sl4 uthernet2 -sl7 scsi "
		"-hard2 '/tmp/ebatt/roms/Games Two.hdv' -hard1 Boot.po -flop3 \"/tmp/ebatt/roms/Floppy One.2mg\"\n", f);
	fclose(f);
	system("touch /tmp/ebatt/roms/Boot.po");

	n = attract_list_emulators("/tmp/ebatt", names, ATTRACT_MAX_EMUS);
	CHECK(n == 2);
	CHECK(!strcmp(names[0], "Apple IIGS"));
	n = attract_import("/tmp/ebatt", "Apple IIGS", prof, 8, err, sizeof(err));
	CHECK(n == 2);
	CHECK(!strcmp(profile_name(&prof[0]), "Games Collection"));
	CHECK(!strcmp(profile_get(&prof[0], "ramsize"), "4M"));
	CHECK(!strcmp(profile_get(&prof[0], "sl4"), "uthernet2"));
	CHECK(!strcmp(profile_get(&prof[0], "sl7"), "scsi"));
	CHECK(!strcmp(profile_get(&prof[0], "hard2"), "/tmp/ebatt/roms/Games Two.hdv"));
	CHECK(!strcmp(profile_get(&prof[0], "hard1"), "/tmp/ebatt/roms/Boot.po"));
	CHECK(!strcmp(profile_get(&prof[0], "flop3"), "/tmp/ebatt/roms/Floppy One.2mg"));
	CHECK(!strcmp(profile_get(&prof[0], "art_tile"), "/tmp/ebatt/roms/video;/tmp/ebatt/roms/snap"));
	CHECK(!strcmp(profile_get(&prof[0], "art_logo"), "/tmp/ebatt/roms/wheel"));
	CHECK(!strcmp(profile_name(&prof[1]), "System"));
	CHECK(!strcmp(profile_get(&prof[1], "art_name"), "system"));
	CHECK(attract_import("/tmp/ebatt", "Nothing", prof, 8, err, sizeof(err)) == -1);
	system("rm -rf /tmp/ebatt");
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
