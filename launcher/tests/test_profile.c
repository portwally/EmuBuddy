/* Tests for profile.c: reading, writing and the command line. */
#include "../src/profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while(0)

int
main(void)
{
	Profile	p, q;
	char	buf[4000], err[200], line[2000], *argv[PROFILE_MAX_ARGS];
	const char *path = "/tmp/emubuddy_test_profile.txt";
	int	n;
	FILE	*f = fopen(path, "w");

	fputs("# Games profile\nname = Games\nrom = /roms/ROM03\nramsize = 8M\nmhz = 2.8\n"
		"sl3 = uthernet2  # internet\nsl4 = mockingboard\nsl7 = scsi\n"
		"hard1 = /disks/My System.hdv\nshare = ~/IIgsGames\nscale = 3\ncrt = 1\n"
		"unknown_future_key = keep me\nextra = -g_limit_speed 0 -title 'two words'\n", f);
	fclose(f);
	CHECK(profile_load(&p, path) == 0);
	CHECK(!strcmp(profile_name(&p), "Games"));
	CHECK(!strcmp(profile_get(&p, "SL3"), "uthernet2"));
	CHECK(!strcmp(profile_get(&p, "nothing"), ""));

	n = profile_build_args(&p, "EmuBuddy", "/data/Games", buf, sizeof(buf), argv,
						PROFILE_MAX_ARGS, err, sizeof(err));
	CHECK(n > 0);
	profile_cmdline(argv, n, line, sizeof(line));
	printf("%s\n", line);
	CHECK(strstr(line, "-rom /roms/ROM03"));
	CHECK(strstr(line, "-sl3 uthernet2"));
	CHECK(strstr(line, "-hard1 \"/disks/My System.hdv\""));
	CHECK(strstr(line, "-scale 3"));
	CHECK(strstr(line, "-crt"));
	CHECK(strstr(line, "-profile Games"));
	CHECK(strstr(line, "-cfg /data/Games/emubuddy.config"));
	CHECK(strstr(line, "-g_limit_speed 0"));
	CHECK(strstr(line, "-title \"two words\""));
	CHECK(!strstr(line, "-nowindow"));
	CHECK(!strstr(line, "~"));

	/* save and read again: keys, order and the unknown key survive */
	CHECK(profile_save(&p, "/tmp/emubuddy_test_profile2.txt") == 0);
	CHECK(profile_load(&q, "/tmp/emubuddy_test_profile2.txt") == 0);
	CHECK(q.n == p.n);
	CHECK(!strcmp(profile_get(&q, "unknown_future_key"), "keep me"));
	CHECK(!strcmp(q.comments[0], "# Games profile"));

	/* bad slot */
	profile_set(&q, "sl5", "x");
	CHECK(profile_build_args(&q, "EmuBuddy", "", buf, sizeof(buf), argv,
				PROFILE_MAX_ARGS, err, sizeof(err)) == -1);
	CHECK(err[0] != 0);

	remove(path);
	remove("/tmp/emubuddy_test_profile2.txt");
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
