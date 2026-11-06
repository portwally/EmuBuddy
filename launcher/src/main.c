/* EmuBuddy Launcher - first version: a list of profiles, Play starts EmuBuddy.
 * Phase 1 of docs/LAUNCHER_PLAN.md (the real front end and skins come next).
 *
 *   emubuddy-launcher                 the window
 *   emubuddy-launcher --list          list the profiles
 *   emubuddy-launcher --cmd <name>    print the EmuBuddy command line
 *   emubuddy-launcher --play <name>   start EmuBuddy with the profile
 *   EMUBUDDY_EXE=<path>               EmuBuddy to start (else found next to this program)
 *   EMUBUDDY_PROFILES=<folder>        profile folder (else the application data folder) */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "profile.h"

#define MAX_PROFILES	64

static Profile	g_prof[MAX_PROFILES];
static int	g_nprof;
static char	g_dir[1100];
static char	g_status[300] = "";

static void
profile_dir(void)
{
	const char	*e = getenv("EMUBUDDY_PROFILES");

	if(e && e[0]) {
		snprintf(g_dir, sizeof(g_dir), "%s", e);
		SDL_CreateDirectory(g_dir);
		return;
	}
	char	*pref = SDL_GetPrefPath("EmuBuddy", "Profiles");

	snprintf(g_dir, sizeof(g_dir), "%s", pref ? pref : "./Profiles/");
	if(pref) {
		SDL_free(pref);
	}
	if(g_dir[0] && g_dir[strlen(g_dir) - 1] == '/') {
		g_dir[strlen(g_dir) - 1] = 0;
	}
	SDL_CreateDirectory(g_dir);
}

static void
make_starter(const char *file, const char *name, const char *desc, const char *kv)
{
	char	path[1300];
	FILE	*f;

	snprintf(path, sizeof(path), "%s/%s.emubuddy-profile", g_dir, file);
	f = fopen(path, "w");
	if(f) {
		fprintf(f, "name = %s\ndescription = %s\n%s", name, desc, kv);
		fclose(f);
	}
}

static void
first_run(void)
{
	make_starter("Web apps", "Web apps", "Internet: Uthernet II and a fast CPU",
		"ramsize = 8M\nmhz = 8\nsl3 = uthernet2\nsl7 = scsi\nscale = 2\n");
	make_starter("Projects", "Projects", "Programming with a printer and a shared folder",
		"ramsize = 8M\nmhz = 2.8\nsl1 = postscript\nsl7 = scsi\nscale = 2\n");
	make_starter("Games", "Games", "Sound card, stock speed",
		"ramsize = 8M\nmhz = 2.8\nsl4 = mockingboard\nsl7 = scsi\nscale = 3\n");
}

static void
list_cb_collect(const char *name)
{
	size_t	n = strlen(name), e = strlen(".emubuddy-profile");
	char	path[1300];

	if(n > e && !strcmp(name + n - e, ".emubuddy-profile") && g_nprof < MAX_PROFILES) {
		snprintf(path, sizeof(path), "%s/%s", g_dir, name);
		if(profile_load(&g_prof[g_nprof], path) == 0) {
			g_nprof++;
		}
	}
}

static SDL_EnumerationResult SDLCALL
enum_cb(void *ud, const char *dirname, const char *fname)
{
	(void)ud; (void)dirname;
	list_cb_collect(fname);
	return SDL_ENUM_CONTINUE;
}

static int
by_name(const void *a, const void *b)
{
	return SDL_strcasecmp(profile_name((const Profile *)a), profile_name((const Profile *)b));
}

static void
load_profiles(void)
{
	int	count = 0;
	char	**g;

	g_nprof = 0;
	g = SDL_GlobDirectory(g_dir, "*.emubuddy-profile", SDL_GLOB_CASEINSENSITIVE, &count);
	if(!g || count == 0) {
		if(g) {
			SDL_free(g);
		}
		first_run();
		g = SDL_GlobDirectory(g_dir, "*.emubuddy-profile", SDL_GLOB_CASEINSENSITIVE, &count);
	}
	for(int i = 0; g && i < count; i++) {
		list_cb_collect(g[i]);
	}
	if(g) {
		SDL_free(g);
	}
	qsort(g_prof, (size_t)g_nprof, sizeof(Profile), by_name);
	(void)enum_cb;
}

static int
exe_exists(const char *p)
{
	SDL_PathInfo	info;

	return SDL_GetPathInfo(p, &info) && info.type == SDL_PATHTYPE_FILE;
}

static int
find_emubuddy(char *out, size_t n)
{
	const char	*e = getenv("EMUBUDDY_EXE"), *base = SDL_GetBasePath();

	if(e && e[0]) {
		snprintf(out, n, "%s", e);
		return 1;
	}
	if(base) {
		static const char *rel[] = { "EmuBuddy", "EmuBuddy.exe",
			"EmuBuddy.app/Contents/MacOS/EmuBuddy",
			"../EmuBuddy.app/Contents/MacOS/EmuBuddy",
			"../../../EmuBuddy.app/Contents/MacOS/EmuBuddy",
			"../MacOS/EmuBuddy", NULL };

		for(int i = 0; rel[i]; i++) {
			snprintf(out, n, "%s%s", base, rel[i]);
			if(exe_exists(out)) {
				return 1;
			}
		}
	}
	snprintf(out, n, "/Applications/EmuBuddy.app/Contents/MacOS/EmuBuddy");
	return exe_exists(out);
}

static int
build(const Profile *p, char *buf, int bsz, char **argv, char *err, int esz, int make_data)
{
	char	exe[1100], data[1300];

	if(!find_emubuddy(exe, sizeof(exe))) {
		snprintf(exe, sizeof(exe), "EmuBuddy");	/* show the line anyway */
	}
	snprintf(data, sizeof(data), "%s/%s.data", g_dir, profile_name(p));
	if(make_data) {
		SDL_CreateDirectory(data);
	}
	return profile_build_args(p, exe, data, buf, bsz, argv, PROFILE_MAX_ARGS, err, esz);
}

static int
play(const Profile *p, char *msg, size_t msgsz)
{
	char		buf[6000], err[200], exe[1100];
	char		*argv[PROFILE_MAX_ARGS];
	int		n;
	SDL_Process	*proc;

	if(!find_emubuddy(exe, sizeof(exe))) {
		snprintf(msg, msgsz, "EmuBuddy was not found (set EMUBUDDY_EXE)");
		return -1;
	}
	n = build(p, buf, sizeof(buf), argv, err, sizeof(err), 1);
	if(n < 0) {
		snprintf(msg, msgsz, "%s", err);
		return -1;
	}
	proc = SDL_CreateProcess((const char * const *)argv, false);
	if(!proc) {
		snprintf(msg, msgsz, "Could not start EmuBuddy: %s", SDL_GetError());
		return -1;
	}
	SDL_DestroyProcess(proc);
	snprintf(msg, msgsz, "Started %s", profile_name(p));
	return 0;
}

static const Profile *
find_profile(const char *name)
{
	for(int i = 0; i < g_nprof; i++) {
		if(!SDL_strcasecmp(profile_name(&g_prof[i]), name)) {
			return &g_prof[i];
		}
	}
	return NULL;
}

static int
cli(int argc, char **argv)
{
	const Profile	*p;
	char		buf[6000], err[200], line[7000], msg[300], *a[PROFILE_MAX_ARGS];
	int		n;

	if(!strcmp(argv[1], "--list")) {
		for(int i = 0; i < g_nprof; i++) {
			printf("%s\t%s\n", profile_name(&g_prof[i]),
					profile_get(&g_prof[i], "description"));
		}
		return 0;
	}
	if(argc < 3 || !(p = find_profile(argv[2]))) {
		fprintf(stderr, "No such profile (see --list)\n");
		return 2;
	}
	if(!strcmp(argv[1], "--cmd")) {
		n = build(p, buf, sizeof(buf), a, err, sizeof(err), 0);
		if(n < 0) {
			fprintf(stderr, "%s\n", err);
			return 1;
		}
		profile_cmdline(a, n, line, sizeof(line));
		printf("%s\n", line);
		return 0;
	}
	if(!strcmp(argv[1], "--play")) {
		int r = play(p, msg, sizeof(msg));

		printf("%s\n", msg);
		return r ? 1 : 0;
	}
	fprintf(stderr, "Unknown option %s\n", argv[1]);
	return 2;
}

/* ---- the window (placeholder look; the skin engine replaces it) ---- */

static void
window(void)
{
	SDL_Window	*w;
	SDL_Renderer	*r;
	int		sel = 0, running = 1;
	SDL_FRect	play_btn = { 520, 410, 100, 30 };

	if(!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return;
	}
	if(!SDL_CreateWindowAndRenderer("EmuBuddy Launcher", 640, 460, 0, &w, &r)) {
		fprintf(stderr, "window: %s\n", SDL_GetError());
		return;
	}
	while(running) {
		SDL_Event e;

		while(SDL_PollEvent(&e)) {
			if(e.type == SDL_EVENT_QUIT) {
				running = 0;
			} else if(e.type == SDL_EVENT_KEY_DOWN) {
				if(e.key.key == SDLK_DOWN && sel + 1 < g_nprof) {
					sel++;
				} else if(e.key.key == SDLK_UP && sel > 0) {
					sel--;
				} else if(e.key.key == SDLK_RETURN && g_nprof) {
					play(&g_prof[sel], g_status, sizeof(g_status));
				}
			} else if(e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
				float x = e.button.x, y = e.button.y;

				if(x >= play_btn.x && x < play_btn.x + play_btn.w &&
						y >= play_btn.y && y < play_btn.y + play_btn.h && g_nprof) {
					play(&g_prof[sel], g_status, sizeof(g_status));
				} else if(x < 300 && y >= 40) {
					int i = (int)((y - 40) / 24);

					if(i >= 0 && i < g_nprof) {
						sel = i;
					}
				}
			}
		}
		SDL_SetRenderDrawColor(r, 30, 32, 40, 255);
		SDL_RenderClear(r);
		SDL_SetRenderDrawColor(r, 235, 235, 240, 255);
		SDL_RenderDebugText(r, 16, 16, "EmuBuddy Launcher  - Up/Down choose, Return plays");
		for(int i = 0; i < g_nprof; i++) {
			if(i == sel) {
				SDL_SetRenderDrawColor(r, 60, 90, 160, 255);
				SDL_FRect b = { 8, 38 + i * 24.0f, 284, 22 };
				SDL_RenderFillRect(r, &b);
			}
			SDL_SetRenderDrawColor(r, 235, 235, 240, 255);
			SDL_RenderDebugText(r, 16, 44 + i * 24.0f, profile_name(&g_prof[i]));
		}
		if(g_nprof) {
			const Profile	*p = &g_prof[sel];
			char		line[300];
			static const char *keys[] = { "description", "rom", "ramsize", "mhz",
				"sl1", "sl2", "sl3", "sl4", "sl7", "hard1", "share", "scale", NULL };

			for(int k = 0, y = 44; keys[k]; k++) {
				if(profile_get(p, keys[k])[0]) {
					snprintf(line, sizeof(line), "%-12s %.34s", keys[k], profile_get(p, keys[k]));
					SDL_RenderDebugText(r, 320, (float)y, line);
					y += 18;
				}
			}
		}
		SDL_SetRenderDrawColor(r, 60, 140, 80, 255);
		SDL_RenderFillRect(r, &play_btn);
		SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
		SDL_RenderDebugText(r, play_btn.x + 28, play_btn.y + 11, "Play");
		SDL_RenderDebugText(r, 16, 440, g_status);
		SDL_RenderPresent(r);
		SDL_Delay(16);
	}
	SDL_DestroyRenderer(r);
	SDL_DestroyWindow(w);
}

int
main(int argc, char **argv)
{
	profile_dir();
	load_profiles();
	if(argc > 1) {
		return cli(argc, argv);
	}
	window();
	return 0;
}
