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

/* ---- the window: EmuBuddy's own UI toolkit (GS/OS "Retro" look) ----
 * The toolkit sources are shared with EmuBuddy (../emubuddy/src/ui*.c). */
#include "ui.h"

#define LW	800
#define LH	640

static SDL_Window	*g_win;
static SDL_Renderer	*g_ren;
static int		g_sel, g_top, g_running = 1;
static UiWindow		g_lib, g_edit_win;
static char		g_toast[200];

/* text fields: one is focused at a time (pointer to its buffer) */
static char		*g_focus;
static int		g_focus_cap;

static int
field(UiWindow *win, int cx, int cy, int x, int y, int w, char *buf, int cap)
{
	int	focused = g_focus == buf, n;
	const char *t = buf;

	ui_fill(cx + x, cy + y, w, 18, UI_WHITE);
	ui_frame(cx + x, cy + y, w, 18, focused ? 0x2040C0 : UI_BLACK);
	while(*t && ui_text_width(t) > w - 10) {
		t++;
	}
	ui_draw_text(cx + x + 4, cy + y + 1, t, UI_BLACK);
	if(focused && (SDL_GetTicks() / 500) % 2 == 0) {
		n = ui_text_width(t);
		ui_fill(cx + x + 4 + n, cy + y + 3, 1, 12, UI_BLACK);
	}
	if(ui_hit(win, cx, cy, x, y, w, 18)) {
		g_focus = buf;
		g_focus_cap = cap;
		SDL_StartTextInput(g_win);
		return 1;
	}
	return 0;
}

static void
focus_clear(void)
{
	g_focus = NULL;
}

/* ---- the edit window ---- */

typedef struct {
	const char	*key;
	const char	*labels[8];
	int		n, sel;
} Choice;

static Choice g_ch[] = {
	{ "ramsize", { "0M", "1M", "2M", "4M", "8M" }, 5, -1 },
	{ "mhz", { "1", "2.8", "8", "16", "32", "64", "128", "max" }, 8, -1 },
	{ "sl1", { "none", "imagewriter", "postscript", "modem" }, 4, 0 },
	{ "sl2", { "none", "modem", "imagewriter" }, 3, 0 },
	{ "sl3", { "none", "uthernet2", "voc" }, 3, 0 },
	{ "sl4", { "none", "mockingboard", "uthernet2" }, 3, 0 },
	{ "sl7", { "none", "scsi" }, 2, 1 },
	{ "scale", { "1", "2", "3", "4" }, 4, -1 },
};
#define NCH	((int)(sizeof(g_ch) / sizeof(g_ch[0])))

typedef struct {
	const char	*key, *label;
	char		buf[420];
	int		folder;		/* choose a folder, not a file */
} PathField;

static PathField g_pf[] = {
	{ "rom", "ROM file", "", 0 },
	{ "hard1", "Hard disk 1", "", 0 },
	{ "hard2", "Hard disk 2", "", 0 },
	{ "hard3", "Hard disk 3", "", 0 },
	{ "flop3", "3.5\" floppy", "", 0 },
	{ "flop1", "5.25\" floppy", "", 0 },
	{ "share", "Shared folder", "", 1 },
	{ "bridge", "Bridge folder", "", 1 },
};
#define NPF	((int)(sizeof(g_pf) / sizeof(g_pf[0])))

static struct {
	Profile	p;
	char	name[100], desc[200], extra[300];
	int	crt, full, is_new;
	char	orig_path[1100];
} g_e;

static char g_pick[1300];
static SDL_AtomicInt g_pick_ready;
static char *g_pick_target;
static int g_pick_cap;

static void SDLCALL
pick_cb(void *ud, const char * const *list, int filter)
{
	(void)ud; (void)filter;
	if(list && list[0]) {
		SDL_strlcpy(g_pick, list[0], sizeof(g_pick));
		SDL_SetAtomicInt(&g_pick_ready, 1);
	}
}

static void
choose(PathField *f)
{
	g_pick_target = f->buf;
	g_pick_cap = (int)sizeof(f->buf);
	if(f->folder) {
		SDL_ShowOpenFolderDialog(pick_cb, NULL, g_win, NULL, false);
	} else {
		SDL_ShowOpenFileDialog(pick_cb, NULL, g_win, NULL, 0, NULL, false);
	}
}

static int
idx_of(const Choice *c, const char *v)
{
	for(int i = 0; i < c->n; i++) {
		if(!SDL_strcasecmp(c->labels[i], v)) {
			return i;
		}
	}
	return (c->key[0] == 's' && c->key[1] == 'l' && !v[0]) ? 0 : -1;
}

static void
edit_load(const Profile *src, int is_new)
{
	g_e.p = *src;
	g_e.is_new = is_new;
	snprintf(g_e.orig_path, sizeof(g_e.orig_path), "%s", src->path);
	snprintf(g_e.name, sizeof(g_e.name), "%s", profile_name(src));
	snprintf(g_e.desc, sizeof(g_e.desc), "%s", profile_get(src, "description"));
	snprintf(g_e.extra, sizeof(g_e.extra), "%s", profile_get(src, "extra"));
	g_e.crt = !!profile_get(src, "crt")[0] && strcmp(profile_get(src, "crt"), "0");
	g_e.full = !!profile_get(src, "fullscreen")[0] && strcmp(profile_get(src, "fullscreen"), "0");
	for(int i = 0; i < NCH; i++) {
		g_ch[i].sel = idx_of(&g_ch[i], profile_get(src, g_ch[i].key));
	}
	for(int i = 0; i < NPF; i++) {
		snprintf(g_pf[i].buf, sizeof(g_pf[i].buf), "%s", profile_get(src, g_pf[i].key));
	}
}

static void
file_name_for(const char *name, char *out, size_t n)
{
	char	clean[200];
	int	k = 0;

	for(const char *p = name; *p && k < 150; p++) {
		clean[k++] = strchr("/\\:*?\"<>|", *p) ? '_' : *p;
	}
	clean[k] = 0;
	snprintf(out, n, "%s/%s.emubuddy-profile", g_dir, clean[0] ? clean : "profile");
}

/* Don't write empty keys that were not there before. */
static void
set_keep(Profile *p, const char *key, const char *v)
{
	if(v[0] || profile_get(p, key)[0]) {
		profile_set(p, key, v);
	}
}

static void
edit_save(void)
{
	char	path[1300];
	int	i;

	profile_set(&g_e.p, "name", g_e.name);
	set_keep(&g_e.p, "description", g_e.desc);
	set_keep(&g_e.p, "extra", g_e.extra);
	for(i = 0; i < NCH; i++) {
		set_keep(&g_e.p, g_ch[i].key, g_ch[i].sel >= 0 ? g_ch[i].labels[g_ch[i].sel] : "");
	}
	for(i = 0; i < NPF; i++) {
		set_keep(&g_e.p, g_pf[i].key, g_pf[i].buf);
	}
	set_keep(&g_e.p, "crt", g_e.crt ? "1" : "");
	set_keep(&g_e.p, "fullscreen", g_e.full ? "1" : "");
	file_name_for(g_e.name, path, sizeof(path));
	if(profile_save(&g_e.p, path) != 0) {
		snprintf(g_toast, sizeof(g_toast), "Could not save %s", path);
		return;
	}
	if(!g_e.is_new && g_e.orig_path[0] && strcmp(path, g_e.orig_path) != 0) {
		SDL_RemovePath(g_e.orig_path);		/* renamed */
	}
	snprintf(g_toast, sizeof(g_toast), "Saved %s", g_e.name);
	ui_message("Saved %s", g_e.name);
	focus_clear();
	ui_window_close(&g_edit_win);
	load_profiles();
	for(i = 0; i < g_nprof; i++) {
		if(!strcmp(g_prof[i].path, path)) {
			g_sel = i;
		}
	}
}

static void
edit_draw(UiWindow *win, int cx, int cy)
{
	int	y = 8, i;
	char	lab[40];

	ui_draw_text(cx + 10, cy + y + 1, "Name", UI_BLACK);
	field(win, cx, cy, 110, y, 220, g_e.name, sizeof(g_e.name));
	ui_draw_text(cx + 346, cy + y + 1, "Description", UI_BLACK);
	field(win, cx, cy, 440, y, 230, g_e.desc, sizeof(g_e.desc));
	y += 30;
	for(i = 0; i < NPF; i++) {
		PathField *f = &g_pf[i];

		ui_draw_text(cx + 10, cy + y + 1, f->label, UI_BLACK);
		field(win, cx, cy, 110, y, 460, f->buf, sizeof(f->buf));
		if(ui_button(win, cx, cy, 578, y - 1, 60, 20, "Choose...", 1)) {
			choose(f);
		}
		if(ui_button(win, cx, cy, 642, y - 1, 30, 20, "X", f->buf[0] != 0)) {
			f->buf[0] = 0;
		}
		y += 24;
	}
	y += 6;
	ui_hline(cx + 10, cy + y, 660, UI_LGREY);
	y += 8;
	static const char *names[] = { "Memory", "Speed (MHz)", "Slot 1 printer", "Slot 2 modem",
		"Slot 3", "Slot 4", "Slot 7", "Window size" };
	for(i = 0; i < NCH; i++) {
		Choice *c = &g_ch[i];

		ui_draw_text(cx + 10, cy + y + 2, names[i], UI_BLACK);
		ui_segment(win, cx, cy, 130, y, c->n * (c->n <= 4 ? 106 : 64), 20, c->labels, c->n, &c->sel,
						(1u << c->n) - 1);
		y += 24;
	}
	ui_checkbox(win, cx, cy, 10, y, "Curved CRT look", &g_e.crt);
	ui_checkbox(win, cx, cy, 200, y, "Full screen", &g_e.full);
	y += 24;
	ui_draw_text(cx + 10, cy + y + 1, "Extra options", UI_BLACK);
	field(win, cx, cy, 110, y, 560, g_e.extra, sizeof(g_e.extra));
	y += 34;
	snprintf(lab, sizeof(lab), "Drop disks and folders here");
	ui_draw_text(cx + 10, cy + y + 4, lab, UI_GREY);
	if(ui_button(win, cx, cy, 500, y, 80, 24, "Cancel", 1)) {
		focus_clear();
		ui_window_close(win);
	}
	if(ui_button(win, cx, cy, 590, y, 80, 24, "Save", g_e.name[0] != 0)) {
		edit_save();
	}
}

static int
edit_drop(UiWindow *win, int x, int y, const char *path)
{
	SDL_PathInfo	info;
	size_t		n = strlen(path);
	const char	*ext = strrchr(path, '.');
	int		slot = -1;

	(void)win; (void)x; (void)y;
	if(!SDL_GetPathInfo(path, &info)) {
		return 0;
	}
	if(info.type == SDL_PATHTYPE_DIRECTORY) {
		snprintf(g_pf[6].buf, sizeof(g_pf[6].buf), "%s", path);
		return 1;
	}
	if(ext && (!SDL_strcasecmp(ext, ".woz") || !SDL_strcasecmp(ext, ".dsk") ||
			!SDL_strcasecmp(ext, ".do")) && info.size <= 150000) {
		slot = 5;
	} else if(info.size <= 1600000) {
		slot = 4;
	} else {
		for(int i = 1; i <= 3 && slot < 0; i++) {
			if(!g_pf[i].buf[0]) {
				slot = i;
			}
		}
		if(slot < 0) {
			slot = 1;
		}
	}
	(void)n;
	snprintf(g_pf[slot].buf, sizeof(g_pf[slot].buf), "%s", path);
	return 1;
}

static void
edit_open(const Profile *src, int is_new)
{
	edit_load(src, is_new);
	focus_clear();
	snprintf(g_edit_win.title, sizeof(g_edit_win.title), is_new ? "New Profile" : "Edit Profile");
	g_edit_win.w = 690;
	g_edit_win.h = 566;
	g_edit_win.modal = 1;
	g_edit_win.closable = 1;
	g_edit_win.draw = edit_draw;
	g_edit_win.on_drop = edit_drop;
	ui_window_center(&g_edit_win);
	g_edit_win.y = 26;
	ui_window_open(&g_edit_win);
}

/* ---- the library window ---- */

static void
new_profile(void)
{
	Profile	p;

	profile_init(&p);
	profile_set(&p, "name", "New profile");
	profile_set(&p, "ramsize", "8M");
	profile_set(&p, "mhz", "2.8");
	profile_set(&p, "sl7", "scsi");
	profile_set(&p, "scale", "2");
	edit_open(&p, 1);
}

static void
duplicate_profile(void)
{
	Profile	p;
	char	n[200];

	if(!g_nprof) {
		return;
	}
	p = g_prof[g_sel];
	snprintf(n, sizeof(n), "%s copy", profile_name(&p));
	profile_set(&p, "name", n);
	p.path[0] = 0;
	edit_open(&p, 1);
}

static void
delete_profile(void)
{
	SDL_MessageBoxButtonData btn[2];
	SDL_MessageBoxData box;
	char	msg[300];
	int	pick = 2;

	if(!g_nprof) {
		return;
	}
	SDL_zero(btn);
	btn[0].buttonID = 1;
	btn[0].text = "Delete";
	btn[1].buttonID = 2;
	btn[1].text = "Cancel";
	btn[1].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
	snprintf(msg, sizeof(msg), "Delete the profile \"%s\"? The disks it uses are not touched.",
							profile_name(&g_prof[g_sel]));
	SDL_zero(box);
	box.title = "EmuBuddy Launcher";
	box.message = msg;
	box.numbuttons = 2;
	box.buttons = btn;
	if(SDL_ShowMessageBox(&box, &pick) && pick == 1) {
		SDL_RemovePath(g_prof[g_sel].path);
		load_profiles();
		if(g_sel >= g_nprof) {
			g_sel = g_nprof ? g_nprof - 1 : 0;
		}
	}
}

static void
show_folder(void)
{
	char	url[1500] = "file://", *q = url + 7;

	for(const char *p = g_dir; *p && q < url + 1400; p++) {
		if(*p == ' ') {
			*q++ = '%'; *q++ = '2'; *q++ = '0';
		} else {
			*q++ = *p;
		}
	}
	*q = 0;
	SDL_OpenURL(url);
}

static void
lib_play(void)
{
	if(g_nprof) {
		play(&g_prof[g_sel], g_toast, sizeof(g_toast));
		ui_message("%s", g_toast);
	}
}

static void
lib_draw(UiWindow *win, int cx, int cy)
{
	static const char *names[LH];
	static const char *keys[][2] = { { "rom", "ROM" }, { "ramsize", "Memory" },
		{ "mhz", "Speed" }, { "sl1", "Slot 1" }, { "sl2", "Slot 2" }, { "sl3", "Slot 3" },
		{ "sl4", "Slot 4" }, { "sl7", "Slot 7" }, { "hard1", "Hard disk 1" },
		{ "hard2", "Hard disk 2" }, { "hard3", "Hard disk 3" }, { "flop3", "3.5\" floppy" },
		{ "flop1", "5.25\" floppy" }, { "share", "Shared folder" }, { "scale", "Window size" },
		{ NULL, NULL } };
	int	i, y = 10, r;

	for(i = 0; i < g_nprof && i < LH; i++) {
		names[i] = profile_name(&g_prof[i]);
	}
	r = ui_list(win, cx, cy, 10, 10, 240, 470, names, g_nprof, &g_sel, &g_top);
	if(r == 2) {
		lib_play();
	}
	if(g_nprof) {
		const Profile	*p = &g_prof[g_sel];
		char		buf[6000], err[200], line[7000], *a[PROFILE_MAX_ARGS], part[100];
		int		n;

		ui_draw_text(cx + 270, cy + y, profile_name(p), UI_BLACK);
		y += 22;
		ui_draw_text(cx + 270, cy + y, profile_get(p, "description"), UI_GREY);
		y += 28;
		for(i = 0; keys[i][0]; i++) {
			const char *v = profile_get(p, keys[i][0]);

			if(v[0]) {
				const char *t = v;

				while(*t && ui_text_width(t) > 360) {
					t++;
				}
				ui_draw_text(cx + 270, cy + y, keys[i][1], UI_GREY);
				ui_draw_text(cx + 380, cy + y, t, UI_BLACK);
				y += 18;
			}
		}
		y += 10;
		ui_draw_text(cx + 270, cy + y, "Command line", UI_GREY);
		y += 18;
		n = build(p, buf, sizeof(buf), a, err, sizeof(err), 0);
		if(n > 0) {
			int	k = 0, len;

			profile_cmdline(a, n, line, sizeof(line));
			len = (int)strlen(line);
			while(k < len && y < 470) {
				int m = 0, w = 0;

				while(k + m < len && m < 99) {
					part[m] = line[k + m];
					part[m + 1] = 0;
					if(ui_text_width(part) > 490) {
						break;
					}
					m++;
				}
				w = m ? m : 1;
				SDL_strlcpy(part, line + k, (size_t)w + 1);
				ui_draw_text(cx + 270, cy + y, part, UI_BLACK);
				k += w;
				y += 16;
			}
		} else {
			ui_draw_text(cx + 270, cy + y, err, UI_BAD);
		}
	}
	if(ui_button(win, cx, cy, 10, 490, 70, 28, "New", 1)) {
		new_profile();
	}
	if(ui_button(win, cx, cy, 86, 490, 80, 28, "Duplicate", g_nprof > 0)) {
		duplicate_profile();
	}
	if(ui_button(win, cx, cy, 172, 490, 78, 28, "Delete", g_nprof > 0)) {
		delete_profile();
	}
	if(ui_button(win, cx, cy, 540, 490, 100, 28, "Edit...", g_nprof > 0)) {
		edit_open(&g_prof[g_sel], 0);
	}
	if(ui_button(win, cx, cy, 650, 490, 100, 28, "Play", g_nprof > 0)) {
		lib_play();
	}
}

/* ---- menus ---- */

enum { C_NEW = 1, C_DUP, C_DEL, C_FOLDER, C_QUIT, C_PLAY, C_EDIT, C_ABOUT };

static const UiMenuItem m_file[] = {
	{ "New Profile", C_NEW, "Cmd-N", 0 },
	{ "Duplicate Profile", C_DUP, NULL, 0 },
	{ "Delete Profile...", C_DEL, NULL, 0 },
	{ NULL, 0, NULL, UI_MI_SEP },
	{ "Show Profiles Folder", C_FOLDER, NULL, 0 },
	{ NULL, 0, NULL, UI_MI_SEP },
	{ "Quit", C_QUIT, "Cmd-Q", 0 },
};
static const UiMenuItem m_prof[] = {
	{ "Play", C_PLAY, "Return", 0 },
	{ "Edit...", C_EDIT, "Cmd-E", 0 },
};
static const UiMenuItem m_help[] = {
	{ "About EmuBuddy Launcher", C_ABOUT, NULL, 0 },
};
static const UiMenu g_menus[] = {
	{ "File", m_file, 7 }, { "Profile", m_prof, 2 }, { "Help", m_help, 1 },
};

static void
app_command(int id)
{
	switch(id) {
	case C_NEW: new_profile(); break;
	case C_DUP: duplicate_profile(); break;
	case C_DEL: delete_profile(); break;
	case C_FOLDER: show_folder(); break;
	case C_QUIT: g_running = 0; break;
	case C_PLAY: lib_play(); break;
	case C_EDIT: if(g_nprof) { edit_open(&g_prof[g_sel], 0); } break;
	case C_ABOUT:
		ui_message("EmuBuddy Launcher %s - 2026, Walter Tengler", LAUNCHER_VERSION_STR);
		break;
	}
}

static int
app_state(int id)
{
	(void)id;
	return 0;
}

static void
app_status(char *l, char *c, char *r, int n)
{
	(void)c;
	snprintf(l, (size_t)n, "%d profile%s", g_nprof, g_nprof == 1 ? "" : "s");
	snprintf(r, (size_t)n, "EmuBuddy Launcher");
}

static void
app_tick(void)
{
	if(SDL_GetAtomicInt(&g_pick_ready)) {
		SDL_SetAtomicInt(&g_pick_ready, 0);
		if(g_pick_target) {
			SDL_strlcpy(g_pick_target, g_pick, (size_t)g_pick_cap);
		}
	}
}

static const UiApp g_app = { g_menus, 3, app_state, app_command, app_status, app_tick,
	NULL, NULL, NULL };

static void
window(void)
{
	if(!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return;
	}
	if(!SDL_CreateWindowAndRenderer("EmuBuddy Launcher", LW, LH, SDL_WINDOW_RESIZABLE,
							&g_win, &g_ren)) {
		fprintf(stderr, "window: %s\n", SDL_GetError());
		return;
	}
	SDL_SetRenderLogicalPresentation(g_ren, LW, LH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	ui_init(g_win, g_ren, &g_app);
	snprintf(g_lib.title, sizeof(g_lib.title), "Profiles");
	g_lib.x = 10;
	g_lib.y = UI_BAR_H + 10;
	g_lib.w = 780;
	g_lib.h = 540;
	g_lib.draw = lib_draw;
	ui_window_open(&g_lib);
	while(g_running) {
		SDL_Event e;

		while(SDL_PollEvent(&e)) {
			if(e.type == SDL_EVENT_QUIT) {
				g_running = 0;
				continue;
			}
			SDL_ConvertEventToRenderCoordinates(g_ren, &e);
			if(g_focus && e.type == SDL_EVENT_TEXT_INPUT) {
				size_t n = strlen(g_focus), m = strlen(e.text.text);

				if(n + m < (size_t)g_focus_cap) {
					memcpy(g_focus + n, e.text.text, m + 1);
				}
				continue;
			}
			if(g_focus && e.type == SDL_EVENT_KEY_DOWN) {
				SDL_Keycode k = e.key.key;

				if(k == SDLK_BACKSPACE) {
					size_t n = strlen(g_focus);

					while(n > 0 && ((unsigned char)g_focus[n - 1] & 0xC0) == 0x80) {
						n--;
					}
					if(n > 0) {
						n--;
					}
					g_focus[n] = 0;
					continue;
				} else if(k == SDLK_RETURN || k == SDLK_TAB || k == SDLK_ESCAPE) {
					focus_clear();
					continue;
				} else if((k == SDLK_V) && (e.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) {
					char *t = SDL_GetClipboardText();

					if(t) {
						size_t n = strlen(g_focus);

						for(char *p = t; *p && n + 1 < (size_t)g_focus_cap; p++) {
							if(*p >= 32) {
								g_focus[n++] = *p;
							}
						}
						g_focus[n] = 0;
						SDL_free(t);
					}
					continue;
				}
			}
			if(e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
				focus_clear();
			}
			if(ui_handle_event(&e)) {
				continue;
			}
			if(e.type == SDL_EVENT_KEY_DOWN && !g_focus && !ui_is_modal()) {
				if(e.key.key == SDLK_RETURN) {
					lib_play();
				} else if(e.key.key == SDLK_DOWN && g_sel + 1 < g_nprof) {
					g_sel++;
				} else if(e.key.key == SDLK_UP && g_sel > 0) {
					g_sel--;
				}
			}
		}
		SDL_SetRenderDrawColor(g_ren, 34, 34, 255, 255);
		SDL_RenderClear(g_ren);
		ui_draw(LW, LH);
		{	/* developer aids: LAUNCHER_EDIT=1 opens the editor, LAUNCHER_SHOT=<file.bmp> saves a picture and quits */
			static int frame;

			frame++;
			if(frame == 5 && getenv("LAUNCHER_EDIT") && g_nprof) {
				edit_open(&g_prof[g_sel], 0);
			}
			if(frame == 8 && getenv("LAUNCHER_SAVE") && g_e.name[0]) {	/* test aid */
				snprintf(g_e.name, sizeof(g_e.name), "%s", getenv("LAUNCHER_SAVE"));
				snprintf(g_pf[1].buf, sizeof(g_pf[1].buf), "/disks/My Disk.hdv");
				g_ch[3].sel = 2;
				edit_save();
			}
			if(frame == 20 && getenv("LAUNCHER_SHOT")) {
				SDL_Surface *sf = SDL_RenderReadPixels(g_ren, NULL);

				if(sf) {
					SDL_SaveBMP(sf, getenv("LAUNCHER_SHOT"));
					SDL_DestroySurface(sf);
				}
				g_running = 0;
			}
		}
		SDL_RenderPresent(g_ren);
		SDL_Delay(16);
	}
	ui_shutdown();
	SDL_DestroyRenderer(g_ren);
	SDL_DestroyWindow(g_win);
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
