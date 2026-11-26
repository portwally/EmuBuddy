/* EmuBuddy command line, in the style of MAME's apple2gs driver:
 *
 *   EmuBuddy [apple2gs] [options] [image ...]
 *
 * The options are translated into the KEGS core's own arguments
 * (-s7d1 <file>, -g_limit_speed 0, ...) before the core parses them. A few
 * things are kept for after start-up (shared folders, loose image files,
 * window size). Settings given on the command line apply to that run only:
 * unless -save is given, nothing from the run is written to the config file.
 *
 * Unknown options are passed through, so all KEGS options (-cfg, -rom,
 * -<config variable> <value>) keep working. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "cli.h"

int	g_config_session_only = 0;	/* checked by config.c before saving */

static char	*g_out[512];
static int	g_nout;
static char	*g_later_insert[32];	/* shares and loose images */
static int	g_nlater;
static int	g_win_w, g_win_h, g_scale, g_screen;	/* g_screen: -screen, 1 = main */
static char	g_title[200], g_profile[100];

/* 1 when the Launcher started this EmuBuddy (it always passes -profile) */
int
cli_from_launcher(void)
{
	return g_profile[0] != 0;
}
static char	g_quitshot[1100];	/* -quitshot: picture of the screen when quitting */
static char	*g_check[64];		/* paths -validate looks at: "kind path" */
static int	g_ncheck;

static void
check(const char *kind, const char *path)
{
	char	buf[1200];

	if(g_ncheck < 64 && path && path[0]) {
		snprintf(buf, sizeof(buf), "%s\t%s", kind, path);
		g_check[g_ncheck++] = strdup(buf);
	}
}

static void
emit(const char *a)
{
	if(g_nout < 510) {
		g_out[g_nout++] = strdup(a);
	}
}

static void
emit2(const char *a, const char *b)
{
	emit(a);
	emit(b);
}

static void
later(const char *path)
{
	if(g_nlater < 32) {
		g_later_insert[g_nlater++] = strdup(path);
	}
}

static int
is(const char *a, const char *b)
{
	return !strcasecmp(a, b);
}

static void
usage(void)
{
	printf(
"EmuBuddy - Apple IIgs emulator\n"
"\n"
"Usage: EmuBuddy [apple2gs] [options] [disk image or folder ...]\n"
"  Options follow MAME's apple2gs driver where they exist. They apply to this\n"
"  run only (add -save to keep them).\n"
"\n"
"Disks:\n"
"  -flop1 <img>   -flop2 <img>    5.25\" drives (slot 6, drive 1/2)\n"
"  -flop3 <img>   -flop4 <img>    3.5\" drives (slot 5, drive 1/2)\n"
"  -hard1 <img> ... -hard7 <img>  SCSI hard disks (slot 7, drive 1-7)\n"
"  -cdrom <img>                   CD image (.iso, .cdr, .toast, .cue) in hard disk drive 7,\n"
"                                 the CD-ROM drive of the High-Speed SCSI Card\n"
"  -hsscsirom <file>              ROM of the High-Speed SCSI Card (341-0803.bin or a zip)\n"
"  -fujinet on | off              FujiNet: its devices follow the hard disks of the hard disk\n"
"                                 card; needs FujiNet-PC running on this computer\n"
"  -fujinetport <n>               the port FujiNet-PC connects to (default 1985)\n"
"  -fujinetpc <folder>            the folder of FujiNet-PC: EmuBuddy starts and stops it\n"
"  -share <folder>                Mac/PC folder as a ProDOS volume\n"
"  -bridge <folder>               folder IIgs programs may read/write (host bridge)\n"
"  -webgate <port> | off          web gateway for IIgs browsers (proxy 192.168.64.254,\n"
"                                 port 6502 unless changed here)\n"
"  -appletalk on | off            AppleTalk for a IIgs whose slot 1 or 2 is set to\n"
"                                 AppleTalk (LocalTalk over UDP; on unless changed)\n"
"  -title <text>                  window title\n"
"  -profile <name>                 name of the Launcher profile (shown in the window title)\n"
"  -quitshot <file.png>           save a picture of the IIgs screen there when EmuBuddy quits\n"
"                                 (the Launcher uses it as the picture of a game)\n"
"  -skin iigs | system            (accepted, no effect: macOS always uses its menu bar)\n"
"  <img or folder>                put in the first free fitting drive\n"
"\n"
"Slots (-slN <card>):\n"
"  -sl1  imagewriter | postscript | modem | none   printer port\n"
"  -sl2  modem | imagewriter | fujinet | none   modem port; fujinet: the FujiNet as a card\n"
"                                     of its own (the IIgs can start up from its disks)\n"
"  -sl3  uthernet2 | voc | none\n"
"  -sl4  mockingboard | uthernet2 | pcxport | none   pcxport: the PC Transporter\n"
"  -sl7  hd | hsscsi | none           hd: the hard disk card (scsi and cffa2 mean the same)\n"
"                                     hsscsi: Apple II High-Speed SCSI Card; needs its ROM\n"
"                                     341-0803.bin or a2hsscsi.zip next to the IIgs ROM,\n"
"                                     or -hsscsirom <file>\n"
"\n"
"Machine:\n"
"  -rom <file>          IIgs ROM file (ROM 01 or ROM 03)\n"
"  -rompath <folder>    look for the ROM file in this folder\n"
"  -ramsize <n>M        memory expansion (0M-8M)\n"
"  -mhz <n>             1, 2.8 (stock), 8, 16, 32, 64, 128 (ZipGS) or max\n"
"  -nothrottle          run as fast as possible (same as -mhz max)\n"
"\n"
"Display:\n"
"  -window | -nowindow  window or full screen\n"
"  -resolution <WxH>    window size in pixels, e.g. 1408x1136\n"
"  -scale <n>           window size as a multiple of the IIgs screen\n"
"  -crt                 curved CRT effect\n"
"  -nostatus            no status bar under the picture\n"
"  -screen <n>          open on screen n: 1 the main screen, 2 the next one\n"
"  -scanlines <0-100>   scanline strength\n"
"\n"
"Other:\n"
"  -save                also save these settings to the config file\n"
"  -cfg <file>          use another config file\n"
"  -validate            check the files and folders named here, print the result and exit\n"
"                       (exit code 0 = fine, 1 = something is missing)\n"
"  -showconfig          print the settings this command line stands for and exit\n"
"  -h, -help            this help\n"
"\n"
"Exit codes: 0 fine, 1 error while running, 2 bad command line.\n"
"\n"
"Example:\n"
"  EmuBuddy apple2gs -rom ROM03 -hard1 System6.hdv -sl4 uthernet2 -mhz 16 \\\n"
"           -scale 2 -share ~/IIgsFiles\n");
}

/* Find a ROM file in a folder (names KEGS and other emulators use). */
static int
rom_in_folder(const char *dir, char *out, size_t n)
{
	static const char *names[] = { "ROM.03", "ROM03", "APPLE2GS.ROM2",
		"Rom03gd", "ROM", "ROM.01", "ROM01", "APPLE2GS.ROM", "xgs.rom",
		"342-0077-b", NULL };
	FILE	*f;
	int	i;

	for(i = 0; names[i]; i++) {
		snprintf(out, n, "%s/%s", dir, names[i]);
		f = fopen(out, "rb");
		if(f) {
			fclose(f);
			return 1;
		}
	}
	return 0;
}

static int
mhz_option(const char *v)
{
	int	m = atoi(v);

	if(is(v, "max") || is(v, "unlimited")) {
		emit2("-g_limit_speed", "0");
	} else if(is(v, "1") || is(v, "1.0")) {
		emit2("-g_limit_speed", "1");
	} else if(is(v, "2.8") || is(v, "2")) {
		emit2("-g_limit_speed", "2");
	} else if(m == 8 || m == 16 || m == 32 || m == 64 || m == 128) {
		char buf[8];

		snprintf(buf, sizeof(buf), "%d", m);
		emit2("-g_limit_speed", "3");
		emit2("-g_zip_speed_mhz", buf);
	} else {
		fprintf(stderr, "EmuBuddy: -mhz %s: use 1, 2.8, 8, 16, 32, 64, "
						"128 or max\n", v);
		return -1;
	}
	return 0;
}

static int
slot_option(int slot, const char *card)
{
	int	port = slot - 1;
	char	name[32];

	switch(slot) {
	case 1:
	case 2:
		snprintf(name, sizeof(name), "-g_serial_cfg[%d]", port);
		if(is(card, "imagewriter") || is(card, "printer") ||
						is(card, "iw2")) {
			emit2(name, "4");
		} else if(is(card, "postscript") || is(card, "laserwriter") ||
						is(card, "ps")) {
			emit2(name, "5");
		} else if(slot == 2 && is(card, "fujinet")) {
			emit2(name, "0");	/* the FujiNet card in slot 2 */
			emit2("-g_fujinet", "1");
			emit2("-g_fujinet_slot", "2");
		} else if(is(card, "modem") || is(card, "null_modem")) {
			emit2(name, "1");
		} else if(is(card, "none") || is(card, "")) {
			emit2(name, "0");
			snprintf(name, sizeof(name), "-g_serial_device[%d]", port);
			emit2(name, "");
		} else {
			break;
		}
		return 0;
	case 3:
		if(is(card, "uthernet2") || is(card, "uthernet")) {
			emit2("-g_uthernet2_slot", "3");
			emit2("-g_voc_enable", "0");
		} else if(is(card, "voc")) {
			emit2("-g_voc_enable", "1");
		} else if(is(card, "none") || is(card, "")) {
			emit2("-g_voc_enable", "0");
		} else {
			break;
		}
		return 0;
	case 4:
		if(is(card, "mockingboard") || is(card, "mockingboarda")) {
			emit2("-g_slot4_mockingboard", "1");
			emit2("-g_pcxport_slot", "0");
		} else if(is(card, "uthernet2") || is(card, "uthernet")) {
			emit2("-g_uthernet2_slot", "4");
			emit2("-g_slot4_mockingboard", "0");
			emit2("-g_pcxport_slot", "0");
		} else if(is(card, "pcxport")) {	/* MAME's name */
			emit2("-g_pcxport_slot", "4");
			emit2("-g_slot4_mockingboard", "0");
		} else if(is(card, "none") || is(card, "")) {
			emit2("-g_slot4_mockingboard", "0");
			emit2("-g_pcxport_slot", "0");
		} else {
			break;
		}
		return 0;
	case 5:
	case 6:
		fprintf(stderr, "EmuBuddy: slot %d holds the built-in drives\n",
								slot);
		return 0;
	case 7:
		if(is(card, "hsscsi")) {
			emit2("-g_slot7_scsi", "2");
		} else if(is(card, "hd") || is(card, "scsi") || is(card, "cffa2") ||
						is(card, "a2scsi")) {
			emit2("-g_slot7_scsi", "1");
		} else if(is(card, "none") || is(card, "")) {
			emit2("-g_slot7_scsi", "0");
		} else {
			break;
		}
		return 0;
	}
	fprintf(stderr, "EmuBuddy: -sl%d %s: card not available in that slot "
						"(see -help)\n", slot, card);
	return -1;
}

/* -validate: do the files and folders named on the command line exist? */
static int
validate_files(void)
{
	struct stat	sb;
	int		i, bad = 0;

	for(i = 0; i < g_ncheck; i++) {
		char	*tab = strchr(g_check[i], '\t'), *path = tab + 1;
		int	want_dir = !strncmp(g_check[i], "folder", 6);

		*tab = 0;
		if(stat(path, &sb) != 0) {
			printf("MISSING %s: %s\n", g_check[i], path);
			bad++;
		} else if(want_dir && !S_ISDIR(sb.st_mode)) {
			printf("NOT A FOLDER: %s\n", path);
			bad++;
		} else if(!strcmp(g_check[i], "rom") && S_ISREG(sb.st_mode) &&
				sb.st_size != 131072 && sb.st_size != 262144) {
			printf("ROM has an unusual size (%ld bytes): %s\n",
						(long)sb.st_size, path);
			bad++;
		} else {
			printf("OK %s: %s\n", g_check[i], path);
		}
	}
	if(g_ncheck == 0) {
		printf("Nothing to check (no disks, folders or ROM named).\n");
	}
	printf(bad ? "%d problem(s)\n" : "All fine\n", bad);
	return bad ? 1 : 0;
}

/* -showconfig: the KEGS arguments this command line turned into. */
static void
show_config(void)
{
	int	i;

	printf("Settings for this run:\n");
	for(i = 1; i < g_nout; i++) {
		if(g_out[i][0] == '-') {
			printf("  %s", g_out[i]);
			if(i + 1 < g_nout && g_out[i + 1][0] != '-') {
				printf(" %s", g_out[++i]);
			}
			printf("\n");
		}
	}
	for(i = 0; i < g_nlater; i++) {
		printf("  insert %s\n", g_later_insert[i]);
	}
	if(g_win_w || g_scale) {
		printf("  window %dx%d scale %d\n", g_win_w, g_win_h, g_scale);
	}
	if(g_profile[0]) {
		printf("  profile %s\n", g_profile);
	}
	if(g_title[0]) {
		printf("  title %s\n", g_title);
	}
}

int
emubuddy_cli_screen(void)
{
	return g_screen;
}

const char *
emubuddy_cli_title(void)
{
	static char	t[320];

	if(g_title[0]) {
		return g_title;
	}
	if(g_profile[0]) {
		snprintf(t, sizeof(t), "EmuBuddy - %s", g_profile);
		return t;
	}
	return "";
}

int
emubuddy_cli(int *pargc, char ***pargv)
{
	int	argc = *pargc, i, any = 0, save = 0, n, validate = 0, showconfig = 0;
	char	**argv = *pargv, buf[1100];

	g_nout = 0;
	emit(argv[0]);
	for(i = 1; i < argc; i++) {
		const char	*a = argv[i];
		const char	*v = (i + 1 < argc) ? argv[i + 1] : NULL;
		const char	*o = (a[0] == '-' && a[1] == '-') ? a + 1 : a;

		if(is(o, "-h") || is(o, "-help") || is(o, "-?")) {
			usage();
			exit(0);
		}
		if(is(a, "apple2gs") || is(a, "apple2gsr3") || is(a, "apple2gsr1")) {
			continue;		/* MAME machine names */
		}
		if(a[0] != '-') {
			later(a);		/* image or folder */
			any = 1;
			continue;
		}
		if(is(o, "-save")) {
			save = 1;
			continue;
		}
		if(is(o, "-validate")) {
			validate = 1;
			continue;
		}
		if(is(o, "-showconfig")) {
			showconfig = 1;
			continue;
		}
		if(is(o, "-nothrottle")) {
			emit2("-g_limit_speed", "0");
			any = 1;
			continue;
		}
		if(is(o, "-window")) {
			emit2("-fullscreen", "0");
			any = 1;
			continue;
		}
		if(is(o, "-nowindow") ||
				(is(o, "-fullscreen") && (!v || v[0] == '-'))) {
			emit2("-fullscreen", "1");
			any = 1;
			continue;
		}
		if(is(o, "-nostatus")) {
			extern int g_ui_hide_status;	/* ui.c */

			g_ui_hide_status = 1;
			any = 1;
			continue;
		}
		if(is(o, "-crt") && (!v || v[0] == '-')) {
			emit2("-crt", "1");
			any = 1;
			continue;
		}
		/* options with a value from here on */
		if(!v && (is(o, "-flop1") || is(o, "-flop2") || is(o, "-flop3") ||
				is(o, "-flop4") || !strncasecmp(o, "-hard", 5) ||
				!strncasecmp(o, "-sl", 3) || is(o, "-share") || is(o, "-bridge") || is(o, "-skin") || is(o, "-webgate") || is(o, "-appletalk") ||
				is(o, "-mhz") || is(o, "-ramsize") ||
				is(o, "-resolution") || is(o, "-scale") ||
				is(o, "-rompath") || is(o, "-scanlines") ||
				is(o, "-cdrom") || is(o, "-hsscsirom") ||
				is(o, "-fujinet") || is(o, "-fujinetport") ||
				is(o, "-fujinetpc") ||
				is(o, "-title") || is(o, "-profile") || is(o, "-quitshot") || is(o, "-screen"))) {
			fprintf(stderr, "EmuBuddy: %s needs a value (see -help)\n", a);
			return -1;
		}
		if(is(o, "-flop1") || is(o, "-flop2")) {
			emit2(is(o, "-flop1") ? "-s6d1" : "-s6d2", v);
			check("disk", v);
		} else if(is(o, "-flop3") || is(o, "-flop4")) {
			emit2(is(o, "-flop3") ? "-s5d1" : "-s5d2", v);
			check("disk", v);
		} else if(!strncasecmp(o, "-hard", 5)) {
			n = o[5] ? atoi(o + 5) : 1;
			if(n < 1 || n > 7) {
				fprintf(stderr, "EmuBuddy: use -hard1 to -hard7\n");
				return -1;
			}
			snprintf(buf, sizeof(buf), "-s7d%d", n);
			emit2(buf, v);
			check("disk", v);
		} else if(!strncasecmp(o, "-sl", 3) && o[3] >= '1' && o[3] <= '7' &&
								!o[4]) {
			if(slot_option(o[3] - '0', v) != 0) {
				return -1;
			}
		} else if(is(o, "-cdrom")) {
			emit2("-s7d7", v);	/* the CD-ROM drive (hsscsi.c) */
			check("disk", v);
		} else if(is(o, "-fujinet")) {
			if(is(v, "on") || is(v, "off")) {
				emit2("-g_fujinet", is(v, "on") ? "1" : "0");
			} else {
				fprintf(stderr, "EmuBuddy: -fujinet on or off\n");
				return -1;
			}
		} else if(is(o, "-fujinetpc")) {
			emit2("-g_fujinet_program", v);
			check("folder", v);
		} else if(is(o, "-fujinetport")) {
			if(atoi(v) < 1024 || atoi(v) > 65535) {
				fprintf(stderr, "EmuBuddy: -fujinetport 1024 to 65535\n");
				return -1;
			}
			emit2("-g_fujinet_port", v);
		} else if(is(o, "-hsscsirom")) {
			emit2("-g_hsscsi_rom", v);
			check("ROM", v);
		} else if(is(o, "-share")) {
			later(v);
			check("folder", v);
		} else if(is(o, "-title")) {
			snprintf(g_title, sizeof(g_title), "%s", v);
		} else if(is(o, "-profile")) {
			snprintf(g_profile, sizeof(g_profile), "%s", v);
		} else if(is(o, "-quitshot")) {
			snprintf(g_quitshot, sizeof(g_quitshot), "%s", v);
		} else if(is(o, "-bridge")) {
			emit2("-g_bridge_root", v);
			check("folder", v);
		} else if(is(o, "-webgate")) {
			if(is(v, "off")) {
				emit2("-g_webgate_port", "0");
			} else if(atoi(v) >= 1024 && atoi(v) <= 65535) {
				emit2("-g_webgate_port", v);
			} else {
				fprintf(stderr, "EmuBuddy: -webgate off, or a port from 1024 to 65535\n");
				return -1;
			}
		} else if(is(o, "-appletalk")) {
			if(is(v, "off") || is(v, "on")) {
				emit2("-g_appletalk", is(v, "on") ? "1" : "0");
			} else {
				fprintf(stderr, "EmuBuddy: -appletalk on or off\n");
				return -1;
			}
		} else if(is(o, "-skin")) {
			if(is(v, "system")) {
				emit2("-g_skin", "1");
			} else if(is(v, "iigs")) {
				emit2("-g_skin", "0");
			} else {
				fprintf(stderr, "EmuBuddy: -skin iigs or system\n");
				return -1;
			}
		} else if(is(o, "-mhz")) {
			if(mhz_option(v) != 0) {
				return -1;
			}
		} else if(is(o, "-ramsize")) {
			n = atoi(v);
			if(n < 0 || n > 8) {
				fprintf(stderr, "EmuBuddy: -ramsize 0M to 8M\n");
				return -1;
			}
			snprintf(buf, sizeof(buf), "%d", n * 1024 * 1024);
			emit2("-mem", buf);
		} else if(is(o, "-resolution")) {
			if(sscanf(v, "%dx%d", &g_win_w, &g_win_h) != 2 ||
						g_win_w < 320 || g_win_h < 240) {
				fprintf(stderr, "EmuBuddy: -resolution WxH, e.g. "
							"1408x1136\n");
				return -1;
			}
		} else if(is(o, "-screen")) {
			g_screen = atoi(v);
			if(g_screen < 1 || g_screen > 16) {
				fprintf(stderr, "EmuBuddy: -screen 1 (the main screen) to 16\n");
				return -1;
			}
			any = 1;
		} else if(is(o, "-scale")) {
			g_scale = atoi(v);
			if(g_scale < 1 || g_scale > 8) {
				fprintf(stderr, "EmuBuddy: -scale 1 to 8\n");
				return -1;
			}
		} else if(is(o, "-rompath")) {
			if(!rom_in_folder(v, buf, sizeof(buf))) {
				fprintf(stderr, "EmuBuddy: no IIgs ROM file found in "
							"%s\n", v);
				return -1;
			}
			emit2("-rom", buf);
			check("rom", buf);
		} else if(is(o, "-scanlines")) {
			emit2("-scanline", v);
		} else {
			/* KEGS option: pass it (and its value) through */
			if(is(o, "-rom") && v) {
				check("rom", v);
			}
			emit(a);
			if(v && v[0] != '-') {
				emit(v);
				i++;
			}
			if(!is(o, "-cfg")) {
				any = 1;
			}
			continue;
		}
		any = 1;
		i++;				/* consumed the value */
	}
	if(validate) {
		exit(validate_files());
	}
	if(showconfig) {
		show_config();
		exit(0);
	}
	g_config_session_only = any && !save;
	g_out[g_nout] = NULL;
	*pargc = g_nout;
	*pargv = g_out;
	return 0;
}

const char *
emubuddy_cli_quitshot(void)
{
	return g_quitshot;
}

int
emubuddy_cli_later_count(void)
{
	return g_nlater;
}

const char *
emubuddy_cli_later(int i)
{
	return (i >= 0 && i < g_nlater) ? g_later_insert[i] : NULL;
}

void
emubuddy_cli_window(int *w, int *h, int *scale)
{
	*w = g_win_w;
	*h = g_win_h;
	*scale = g_scale;
}
