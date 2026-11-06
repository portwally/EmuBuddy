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
#include "library.h"
#include "skins.h"
#include "launcher_icon.h"

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

static void lib_open(int kind, char *target, int cap);
static int		g_skin;
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
	{ "cover", "Cover picture", "", 0 },
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
		field(win, cx, cy, 110, y, (f->folder || i >= 6) ? 460 : 390, f->buf, sizeof(f->buf));
		if(!f->folder && i < 6 && ui_button(win, cx, cy, 504, y - 1, 70, 20, "Library", 1)) {
			lib_open(i == 0 ? LIB_ROM : LIB_DISK, f->buf, (int)sizeof(f->buf));
		}
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
	g_edit_win.h = 590;
	g_edit_win.modal = 1;
	g_edit_win.closable = 1;
	g_edit_win.draw = edit_draw;
	g_edit_win.on_drop = edit_drop;
	ui_window_center(&g_edit_win);
	g_edit_win.y = 24;
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

/* ---- settings and covers ---- */

static void
settings_load(void)
{
	char	path[1300], line[300];
	FILE	*f;

	snprintf(path, sizeof(path), "%s/Launcher.settings", g_dir);
	f = fopen(path, "r");
	while(f && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if(!strncmp(line, "skin = ", 7) && skin_find(line + 7) >= 0) {
			g_skin = skin_find(line + 7);
		}
	}
	if(f) {
		fclose(f);
	}
	skin_apply(g_skin);
}

static void
settings_save(void)
{
	char	path[1300];
	FILE	*f;

	snprintf(path, sizeof(path), "%s/Launcher.settings", g_dir);
	f = fopen(path, "w");
	if(f) {
		fprintf(f, "skin = %s\n", skin_get(g_skin)->name);
		fclose(f);
	}
}

static void
dot(int x, int y)
{
	ui_fill(x, y, 2, 2, g_ui_theme.accent_fg);
}

static void
ell(int cx, int cy, int rx, int ry)
{
	for(int a = 0; a < 360; a += 3) {
		double t = a * 3.14159265 / 180.0;

		dot(cx + (int)(rx * SDL_cos(t)), cy + (int)(ry * SDL_sin(t)));
	}
}

static void
cover_icon(const Profile *p, int ox, int oy)
{
	char		hay[400];
	const char	*kind = profile_get(p, "icon");
	Uint32		c = g_ui_theme.accent_fg;
	int		cx = ox + 32, cy = oy + 32;

	if(!kind[0]) {
		snprintf(hay, sizeof(hay), "%s %s", profile_name(p), profile_get(p, "description"));
		if(eb_strcasestr(hay, "web") || eb_strcasestr(hay, "internet") || eb_strcasestr(hay, "net")) {
			kind = "globe";
		} else if(eb_strcasestr(hay, "proj") || eb_strcasestr(hay, "dev") || eb_strcasestr(hay, "code") ||
				eb_strcasestr(hay, "program")) {
			kind = "gear";
		} else if(eb_strcasestr(hay, "game") || eb_strcasestr(hay, "play")) {
			kind = "joystick";
		} else {
			kind = "disk";
		}
	}
	if(!strcasecmp(kind, "globe")) {
		ell(cx, cy, 26, 26);
		ell(cx, cy, 12, 26);
		ell(cx, cy, 26, 10);
		ui_fill(cx - 26, cy, 52, 2, c);
		ui_fill(cx, cy - 26, 2, 52, c);
	} else if(!strcasecmp(kind, "gear")) {
		ell(cx, cy, 16, 16);
		ell(cx, cy, 7, 7);
		for(int a = 0; a < 8; a++) {
			double t = a * 3.14159265 / 4;
			int tx = cx + (int)(23 * SDL_cos(t)), ty = cy + (int)(23 * SDL_sin(t));

			ui_fill(tx - 4, ty - 4, 9, 9, c);
		}
	} else if(!strcasecmp(kind, "joystick")) {
		ui_fill(cx - 24, cy + 14, 48, 10, c);
		ui_fill(cx - 2, cy - 14, 5, 30, c);
		ell(cx, cy - 18, 8, 8);
		ui_fill(cx - 8, cy - 22, 17, 9, c);
		ui_fill(cx + 12, cy + 6, 8, 8, c);
	} else {				/* a 3.5" disk */
		ui_frame(ox + 8, oy + 6, 48, 52, c);
		ui_fill(ox + 20, oy + 6, 24, 16, c);
		ui_fill(ox + 36, oy + 9, 5, 10, g_ui_theme.accent);
		ui_frame(ox + 14, oy + 32, 36, 26, c);
		ui_hline(ox + 18, oy + 40, 28, c);
		ui_hline(ox + 18, oy + 48, 28, c);
	}
}

static SDL_Texture *g_cover_tex;
static char	g_cover_path[1200];

/* The profile's own picture if it has one (png or bmp), else a generated cover. */
static void
draw_cover(const Profile *p, int x, int y, int w, int h)
{
	char	full[1200];
	const char *v = profile_get(p, "cover");

	profile_expand_path(v, full, sizeof(full));
	if(v[0]) {
		if(strcmp(full, g_cover_path) != 0) {
			SDL_Surface *sf = SDL_LoadSurface(full);

			if(g_cover_tex) {
				SDL_DestroyTexture(g_cover_tex);
				g_cover_tex = NULL;
			}
			snprintf(g_cover_path, sizeof(g_cover_path), "%s", full);
			if(sf) {
				g_cover_tex = SDL_CreateTextureFromSurface(g_ren, sf);
				SDL_DestroySurface(sf);
			}
		}
		if(g_cover_tex) {
			float	tw, th, sc;
			SDL_FRect d;

			SDL_GetTextureSize(g_cover_tex, &tw, &th);
			sc = SDL_min((float)w / tw, (float)h / th);
			d.w = tw * sc;
			d.h = th * sc;
			d.x = x + (w - d.w) / 2;
			d.y = y + (h - d.h) / 2;
			ui_frame(x - 1, y - 1, w + 2, h + 2, UI_BLACK);
			SDL_RenderTexture(g_ren, g_cover_tex, NULL, &d);
			return;
		}
	}
	ui_fill(x, y, w, h, g_ui_theme.accent);
	ui_frame(x, y, w, h, UI_BLACK);
	if(w >= 110) {
		cover_icon(p, x + (w - 64) / 2, y + (h - 64) / 2);
	} else {
		char	ch[2] = { profile_name(p)[0], 0 };

		ui_draw_text(x + (w - ui_text_width(ch)) / 2, y + h / 2 - 8, ch, g_ui_theme.accent_fg);
	}
}

/* ---- the other layouts: Shelf (a grid of covers on boards) and Cover Flow ---- */

static int	g_gtop;			/* first visible shelf row */
static Uint64	g_last_click;
static int	g_last_idx = -1;
#define SHELF_COLS	4
#define SHELF_ROWS	2

/* One click selects, a second click on the same one soon after plays. */
static void
pick_profile(int i)
{
	Uint64 now = SDL_GetTicks();

	if(i == g_last_idx && now - g_last_click < 450) {
		lib_play();
	}
	g_sel = i;
	g_last_idx = i;
	g_last_click = now;
}

static void
compact_details(int cx, int cy, int y, const Profile *p)
{
	char	line[400], buf[6000], err[200], full[7000], *a[PROFILE_MAX_ARGS], part[200];
	int	n, k = 0, len, w;
	static const char *keys[] = { "ramsize", "mhz", "sl1", "sl2", "sl3", "sl4", "sl7", "scale", NULL };

	ui_draw_text(cx + 14, cy + y, profile_name(p), UI_BLACK);
	ui_draw_text(cx + 14 + ui_text_width(profile_name(p)) + 14, cy + y, profile_get(p, "description"), UI_GREY);
	y += 22;
	line[0] = 0;
	for(int i = 0; keys[i]; i++) {
		const char *v = profile_get(p, keys[i]);

		if(v[0] && strcmp(v, "none")) {
			SDL_strlcat(line, keys[i], sizeof(line));
			SDL_strlcat(line, " ", sizeof(line));
			SDL_strlcat(line, v, sizeof(line));
			SDL_strlcat(line, "   ", sizeof(line));
		}
	}
	ui_draw_text(cx + 14, cy + y, line, UI_BLACK);
	y += 22;
	n = build(p, buf, sizeof(buf), a, err, sizeof(err), 0);
	if(n <= 0) {
		ui_draw_text(cx + 14, cy + y, err, UI_BAD);
		return;
	}
	profile_cmdline(a, n, full, sizeof(full));
	len = (int)strlen(full);
	while(k < len && y < 480) {
		int m = 0;

		while(k + m < len && m < 190) {
			part[m] = full[k + m];
			part[m + 1] = 0;
			if(ui_text_width(part) > 750) {
				break;
			}
			m++;
		}
		w = m ? m : 1;
		SDL_strlcpy(part, full + k, (size_t)w + 1);
		ui_draw_text(cx + 14, cy + y, part, UI_GREY);
		k += w;
		y += 16;
	}
}

static void
draw_shelf(UiWindow *win, int cx, int cy)
{
	int	rows = (g_nprof + SHELF_COLS - 1) / SHELF_COLS, cw = 184, ch = 158, i;

	if(g_sel / SHELF_COLS < g_gtop) {
		g_gtop = g_sel / SHELF_COLS;
	}
	if(g_sel / SHELF_COLS >= g_gtop + SHELF_ROWS) {
		g_gtop = g_sel / SHELF_COLS - SHELF_ROWS + 1;
	}
	ui_fill(cx + 8, cy + 8, SHELF_COLS * cw + 4, SHELF_ROWS * ch + 4, g_ui_theme.line);
	for(i = 0; i < SHELF_ROWS * SHELF_COLS; i++) {
		int idx = g_gtop * SHELF_COLS + i, col = i % SHELF_COLS, row = i / SHELF_COLS;
		int x = 10 + col * cw, y = 10 + row * ch;

		if(col == 0) {			/* the board of this row */
			ui_fill(cx + 8, cy + y + 108, SHELF_COLS * cw + 4, 8, g_ui_theme.dim);
			ui_fill(cx + 8, cy + y + 116, SHELF_COLS * cw + 4, 3, g_ui_theme.bg);
		}
		if(idx >= g_nprof) {
			continue;
		}
		if(idx == g_sel) {
			ui_fill(cx + x + 2, cy + y + 2, cw - 4, 106, g_ui_theme.bg);
			ui_frame(cx + x + 2, cy + y + 2, cw - 4, ch - 6, UI_BLACK);
			ui_frame(cx + x + 3, cy + y + 3, cw - 6, ch - 8, UI_BLACK);
		}
		draw_cover(&g_prof[idx], cx + x + (cw - 120) / 2, cy + y + 12, 120, 90);
		{
			const char *nm = profile_name(&g_prof[idx]);
			int tw = ui_text_width(nm);

			ui_draw_text(cx + x + (cw - tw) / 2, cy + y + 126, nm, idx == g_sel ? UI_BLACK : UI_GREY);
		}
		if(ui_hit(win, cx, cy, x, y, cw, ch - 4)) {
			pick_profile(idx);
		}
	}
	if(rows > SHELF_ROWS) {
		ui_vscroll(win, cx, cy, SHELF_COLS * cw + 16, 10, SHELF_ROWS * ch, rows, SHELF_ROWS, &g_gtop);
	}
	ui_hline(cx + 10, cy + SHELF_ROWS * ch + 20, 760, UI_LGREY);
	if(g_nprof) {
		compact_details(cx, cy, SHELF_ROWS * ch + 30, &g_prof[g_sel]);
	}
}

static void
draw_flow(UiWindow *win, int cx, int cy)
{
	int	centre = 389, base = 130, d;

	for(d = 3; d >= 0; d--) {
		for(int side = -1; side <= 1; side += 2) {
			int idx = g_sel + side * d, w, h, x;

			if(d == 0 && side == 1) {
				continue;
			}
			if(idx < 0 || idx >= g_nprof) {
				continue;
			}
			w = d == 0 ? 220 : 160 - (d - 1) * 38;
			h = w * 3 / 4;
			x = d == 0 ? centre : centre + side * (150 + (d - 1) * 80 + (d > 1 ? 20 : 0));
			draw_cover(&g_prof[idx], cx + x - w / 2, cy + base - h / 2 - 10, w, h);
			if(d > 0 && ui_hit(win, cx, cy, x - w / 2, base - h / 2 - 10, w, h)) {
				g_sel = idx;
			}
			/* a dim "reflection" strip under every cover */
			ui_fill(cx + x - w / 2, cy + base + h / 2 - 6, w, 3, g_ui_theme.line);
		}
	}
	if(g_nprof) {
		const char *nm = profile_name(&g_prof[g_sel]);
		const char *ds = profile_get(&g_prof[g_sel], "description");

		ui_draw_text(cx + centre - ui_text_width(nm) / 2, cy + 218, nm, UI_BLACK);
		ui_draw_text(cx + centre - ui_text_width(ds) / 2, cy + 238, ds, UI_GREY);
		if(ui_hit(win, cx, cy, centre - 110, base - 90, 220, 165)) {
			pick_profile(g_sel);
		}
		ui_hline(cx + 10, cy + 266, 760, UI_LGREY);
		compact_details(cx, cy, 278, &g_prof[g_sel]);
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

	if(skin_layout(g_skin) == SKIN_LAYOUT_SHELF) {
		draw_shelf(win, cx, cy);
	} else if(skin_layout(g_skin) == SKIN_LAYOUT_FLOW) {
		draw_flow(win, cx, cy);
	} else {
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

		draw_cover(p, cx + 270, cy + 10, 120, 90);
		ui_draw_text(cx + 404, cy + y + 4, profile_name(p), UI_BLACK);
		y += 26;
		ui_draw_text(cx + 404, cy + y + 4, profile_get(p, "description"), UI_GREY);
		y = 112;
		for(i = 0; keys[i][0]; i++) {
			const char *v = profile_get(p, keys[i][0]);

			if(v[0]) {
				const char *t = v;

				while(*t && ui_text_width(t) > 360) {
					t++;
				}
				int	is_path = !strcmp(keys[i][0], "rom") || !strncmp(keys[i][0], "hard", 4) ||
						!strncmp(keys[i][0], "flop", 4) || !strcmp(keys[i][0], "share");
				int	missing = 0;

				if(is_path) {
					char	full[1200];
					SDL_PathInfo	pi;

					profile_expand_path(v, full, sizeof(full));
					missing = !SDL_GetPathInfo(full, &pi);
				}
				ui_draw_text(cx + 270, cy + y, keys[i][1], UI_GREY);
				ui_draw_text(cx + 380, cy + y, t, missing ? UI_BAD : UI_BLACK);
				if(missing) {
					ui_draw_text(cx + 380 + ui_text_width(t) + 8, cy + y, "(missing)", UI_BAD);
				}
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
	if(ui_button(win, cx, cy, 300, 490, 100, 28, "Library...", 1)) {
		lib_open(LIB_DISK, NULL, 0);
	}
	if(ui_button(win, cx, cy, 540, 490, 100, 28, "Edit...", g_nprof > 0)) {
		edit_open(&g_prof[g_sel], 0);
	}
	if(ui_button(win, cx, cy, 650, 490, 100, 28, "Play", g_nprof > 0)) {
		lib_play();
	}
}

/* ---- the library window ---- */

static Library	g_library;
static int	g_lib_scanned;
static char	g_lfold[16][1100];
static int	g_nlfold, g_lfsel = -1, g_lftop;
static UiWindow	g_libwin;
static int	g_ltab, g_lsel, g_ltop;
static char	*g_lpick;		/* when choosing for an editor field */
static int	g_lpick_cap;
static char	g_addpath[1300];
static char	g_lsearch[100], g_ltag[100];
static int	g_ltag_for = -2;
static SDL_AtomicInt g_add_ready;

static void
lfold_path(char *out, size_t n)
{
	snprintf(out, n, "%s/Library.folders", g_dir);
}

static void
lfold_load(void)
{
	char	path[1300], line[1200];
	FILE	*f;

	lfold_path(path, sizeof(path));
	g_nlfold = 0;
	f = fopen(path, "r");
	while(f && fgets(line, sizeof(line), f) && g_nlfold < 16) {
		line[strcspn(line, "\r\n")] = 0;
		if(line[0]) {
			snprintf(g_lfold[g_nlfold++], sizeof(g_lfold[0]), "%s", line);
		}
	}
	if(f) {
		fclose(f);
	}
}

static void
lfold_save(void)
{
	char	path[1300];
	FILE	*f;

	lfold_path(path, sizeof(path));
	f = fopen(path, "w");
	for(int i = 0; f && i < g_nlfold; i++) {
		fprintf(f, "%s\n", g_lfold[i]);
	}
	if(f) {
		fclose(f);
	}
}

static void
tags_path(char *out, size_t n)
{
	snprintf(out, n, "%s/Library.tags", g_dir);
}

static void
lib_rescan(void)
{
	char	tp[1300];

	lib_scan(&g_library, g_lfold, g_nlfold);
	tags_path(tp, sizeof(tp));
	lib_tags_apply(&g_library, tp);
	g_ltag_for = -2;
	g_lib_scanned = 1;
	ui_message("Library: %d files in %d folder%s", g_library.n, g_nlfold,
						g_nlfold == 1 ? "" : "s");
}

static void SDLCALL
add_cb(void *ud, const char * const *list, int filter)
{
	(void)ud; (void)filter;
	if(list && list[0]) {
		SDL_strlcpy(g_addpath, list[0], sizeof(g_addpath));
		SDL_SetAtomicInt(&g_add_ready, 1);
	}
}

/* Which profiles use this file (or folder)? */
static void
used_by(const char *path, char *out, size_t n)
{
	static const char *keys[] = { "rom", "hard1", "hard2", "hard3", "hard4", "hard5",
		"hard6", "hard7", "flop1", "flop2", "flop3", "flop4", NULL };
	char	full[1200];

	out[0] = 0;
	for(int i = 0; i < g_nprof; i++) {
		for(int k = 0; keys[k]; k++) {
			profile_expand_path(profile_get(&g_prof[i], keys[k]), full, sizeof(full));
			if(full[0] && !strcmp(full, path)) {
				if(out[0]) {
					SDL_strlcat(out, ", ", n);
				}
				SDL_strlcat(out, profile_name(&g_prof[i]), n);
				break;
			}
		}
	}
}

/* A private copy of a disk for the selected profile: the profile then uses the
 * copy and can change it without touching the original. */
static void
private_copy(const LibItem *it)
{
	SDL_MessageBoxButtonData btn[2];
	SDL_MessageBoxData box;
	Profile		p;
	char		msg[600], dir[1300], dst[1500], key[16], full[1200];
	const char	*kind;
	int		pick = 2, nmax, base, i, n = 0;
	SDL_PathInfo	info;

	if(!g_nprof) {
		return;
	}
	p = g_prof[g_sel];
	if(strstr(it->label, "5.25")) {
		kind = "flop"; base = 1; nmax = 2;
	} else if(strstr(it->label, "3.5")) {
		kind = "flop"; base = 3; nmax = 2;
	} else {
		kind = "hard"; base = 1; nmax = 7;
	}
	key[0] = 0;
	for(i = 0; i < nmax; i++) {
		snprintf(full, sizeof(full), "%s%d", kind, base + i);
		if(!profile_get(&p, full)[0]) {
			snprintf(key, sizeof(key), "%s", full);
			break;
		}
	}
	if(!key[0]) {
		ui_message("No free drive in %s for this disk", profile_name(&p));
		return;
	}
	SDL_zero(btn);
	btn[0].buttonID = 1;
	btn[0].text = "Make Copy";
	btn[1].buttonID = 2;
	btn[1].text = "Cancel";
	btn[1].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
	snprintf(msg, sizeof(msg), "Make a copy of \"%s\" (%ld MB) for the profile \"%s\" and use it as %s?\n"
		"The original stays as it is.", it->name, it->size / (1024 * 1024), profile_name(&p), key);
	SDL_zero(box);
	box.title = "EmuBuddy Launcher";
	box.message = msg;
	box.numbuttons = 2;
	box.buttons = btn;
	if(!SDL_ShowMessageBox(&box, &pick) || pick != 1) {
		return;
	}
	snprintf(dir, sizeof(dir), "%s/%s.data/disks", g_dir, profile_name(&p));
	SDL_CreateDirectory(g_dir);
	snprintf(dst, sizeof(dst), "%s/%s.data", g_dir, profile_name(&p));
	SDL_CreateDirectory(dst);
	SDL_CreateDirectory(dir);
	snprintf(dst, sizeof(dst), "%s/%s", dir, it->name);
	while(SDL_GetPathInfo(dst, &info) && n < 99) {
		n++;
		snprintf(dst, sizeof(dst), "%s/%d %s", dir, n + 1, it->name);
	}
	if(!SDL_CopyFile(it->path, dst)) {
		ui_message("Copy failed: %s", SDL_GetError());
		return;
	}
	profile_set(&p, key, dst);
	if(profile_save(&p, p.path) == 0) {
		char	keep[1100];

		snprintf(keep, sizeof(keep), "%s", p.path);
		load_profiles();
		for(i = 0; i < g_nprof; i++) {
			if(!strcmp(g_prof[i].path, keep)) {
				g_sel = i;
			}
		}
		g_lib_scanned = 0;
		ui_message("%s now uses its own copy as %s", profile_name(&p), key);
	}
}

static char	g_lline[LIB_MAX_ITEMS][160];
static const char *g_lnames[LIB_MAX_ITEMS];
static int	g_lmap[LIB_MAX_ITEMS];

static void
libwin_draw(UiWindow *win, int cx, int cy)
{
	static const char *tabs[] = { "ROMs", "Disks" };
	int	i, n = 0, tab = g_lpick ? g_ltab : g_ltab;
	char	used[300], line[400];

	if(!g_lib_scanned) {
		lib_rescan();
	}
	if(!g_lpick) {
		ui_segment(win, cx, cy, 10, 8, 200, 22, tabs, 2, &g_ltab, 3);
	} else {
		ui_draw_text(cx + 10, cy + 10, tab == LIB_ROM ? "Choose a ROM" : "Choose a disk", UI_BLACK);
	}
	ui_draw_text(cx + 226, cy + 10, "Search", UI_BLACK);
	field(win, cx, cy, 290, 9, 240, g_lsearch, sizeof(g_lsearch));
	if(ui_button(win, cx, cy, 540, 8, 120, 22, "Check files", 1)) {
		lib_check(&g_library);
		ui_message("Checked %d files", g_library.n);
	}
	for(i = 0; i < g_library.n; i++) {
		const LibItem *it = &g_library.items[i];

		if(it->kind != tab || !lib_matches(it, g_lsearch)) {
			continue;
		}
		used_by(it->path, used, sizeof(used));
		snprintf(g_lline[n], sizeof(g_lline[0]), "%.40s\t%s\t%s\t%.50s%s", it->name,
				it->label, it->volume, used[0] ? used : "(not used)",
				it->flags ? " !" : "");
		g_lnames[n] = g_lline[n];
		g_lmap[n++] = i;
	}
	if(g_lsel >= n) {
		g_lsel = n - 1;
	}
	if(g_lsel < 0 && n) {
		g_lsel = 0;
	}
	if(!n) {
		ui_draw_text(cx + 14, cy + 44, g_nlfold ? "Nothing found in these folders." :
			"Add the folders that hold your ROMs and disks.", UI_GREY);
	}
	{
		static const int cols[] = { 4, 300, 440, 560 };

		ui_list_columns(cols, 4);
	}
	i = ui_list(win, cx, cy, 10, 36, 740, 246, g_lnames, n, &g_lsel, &g_ltop);
	if(i == 2 && g_lpick && g_lsel >= 0) {
		SDL_strlcpy(g_lpick, g_library.items[g_lmap[g_lsel]].path, (size_t)g_lpick_cap);
		g_lpick = NULL;
		ui_window_close(win);
		return;
	}
	if(g_lsel >= 0 && g_lsel < n) {
		const LibItem *it = &g_library.items[g_lmap[g_lsel]];
		int	idx = g_lmap[g_lsel];

		used_by(it->path, used, sizeof(used));
		snprintf(line, sizeof(line), "%s", it->path);
		const char *t = line;
		while(*t && ui_text_width(t) > 730) {
			t++;
		}
		ui_draw_text(cx + 12, cy + 290, t, UI_BLACK);
		snprintf(line, sizeof(line), "%s, %ld KB%s%s%s", it->label, it->size / 1024,
			it->volume[0] ? ", volume " : "", it->volume,
			it->writable ? "" : ", write-protected");
		ui_draw_text(cx + 12, cy + 308, line, UI_GREY);
		snprintf(line, sizeof(line), "Used by: %s", used[0] ? used : "no profile");
		ui_draw_text(cx + 12, cy + 326, line, used[0] ? UI_BLACK : UI_WARN);
		if(it->flags) {
			snprintf(line, sizeof(line), "Check: %s", it->note);
			ui_draw_text(cx + 300, cy + 326, line, UI_BAD);
		}
		if(g_ltag_for != idx) {
			g_ltag_for = idx;
			snprintf(g_ltag, sizeof(g_ltag), "%s", it->tags);
		}
		ui_draw_text(cx + 12, cy + 348, "Tags", UI_BLACK);
		if(field(win, cx, cy, 60, 346, 300, g_ltag, sizeof(g_ltag)) || g_focus != g_ltag) {
			/* typing changes the buffer; save when it differs */
		}
		if(strcmp(g_ltag, it->tags) != 0 && g_focus != g_ltag) {
			char	tp[1300];

			tags_path(tp, sizeof(tp));
			lib_tags_set(&g_library, idx, g_ltag, tp);
		}
		if(!g_lpick && it->kind == LIB_DISK && ui_button(win, cx, cy, 560, 344, 190, 22,
				"Private Copy for Profile...", g_nprof > 0)) {
			private_copy(it);
		}
	}
	ui_hline(cx + 10, cy + 374, 740, UI_LGREY);
	ui_draw_text(cx + 12, cy + 380, "Library folders", UI_GREY);
	{
		static const char *fn[16];

		for(i = 0; i < g_nlfold; i++) {
			fn[i] = g_lfold[i];
		}
		ui_list(win, cx, cy, 10, 400, 560, 76, fn, g_nlfold, &g_lfsel, &g_lftop);
	}
	if(ui_button(win, cx, cy, 580, 400, 170, 22, "Add Folder...", g_nlfold < 16)) {
		SDL_ShowOpenFolderDialog(add_cb, NULL, g_win, NULL, false);
	}
	if(ui_button(win, cx, cy, 580, 427, 170, 22, "Remove Folder", g_lfsel >= 0 && g_lfsel < g_nlfold)) {
		for(i = g_lfsel; i + 1 < g_nlfold; i++) {
			memcpy(g_lfold[i], g_lfold[i + 1], sizeof(g_lfold[0]));
		}
		g_nlfold--;
		g_lfsel = -1;
		lfold_save();
		lib_rescan();
	}
	if(ui_button(win, cx, cy, 580, 454, 170, 22, "Rescan", 1)) {
		lib_rescan();
	}
	if(g_lpick) {
		if(ui_button(win, cx, cy, 580, 490, 80, 24, "Cancel", 1)) {
			g_lpick = NULL;
			ui_window_close(win);
		}
		if(ui_button(win, cx, cy, 670, 490, 80, 24, "Use", g_lsel >= 0 && g_lsel < n)) {
			SDL_strlcpy(g_lpick, g_library.items[g_lmap[g_lsel]].path, (size_t)g_lpick_cap);
			g_lpick = NULL;
			ui_window_close(win);
		}
	} else if(ui_button(win, cx, cy, 670, 490, 80, 24, "Close", 1)) {
		ui_window_close(win);
	}
}

static void
lib_open(int kind, char *target, int cap)
{
	g_lpick = target;
	g_lpick_cap = cap;
	if(target) {
		g_ltab = kind;
		g_lsel = 0;
		g_ltop = 0;
	}
	snprintf(g_libwin.title, sizeof(g_libwin.title), "Library");
	g_libwin.w = 760;
	g_libwin.h = 536;
	g_libwin.modal = target != NULL;
	g_libwin.closable = 1;
	g_libwin.draw = libwin_draw;
	g_libwin.on_close = NULL;
	if(!g_libwin.open) {
		ui_window_center(&g_libwin);
		g_libwin.y = 26;
	}
	ui_window_open(&g_libwin);
}

/* ---- About ---- */

static UiWindow		g_about;
static SDL_Texture	*g_icon_tex;

static void
about_draw(UiWindow *win, int cx, int cy)
{
	static const char *lines[] = {
		"EmuBuddy Launcher " LAUNCHER_VERSION_STR,
		"Profiles and skins for EmuBuddy",
		"",
		"2026, Walter Tengler",
		"",
		"Starts EmuBuddy with the command line",
		"of the profile you choose.",
		"",
		"GPL v3. UI font: Shaston 640 by",
		"Kreative Software.",
	};
	int	i, cw = win->w - 2;

	if(!g_icon_tex) {
		SDL_Surface *sf = SDL_CreateSurfaceFrom(launcher_icon_size, launcher_icon_size,
				SDL_PIXELFORMAT_RGBA32, (void *)launcher_icon_rgba, launcher_icon_size * 4);

		if(sf) {
			g_icon_tex = SDL_CreateTextureFromSurface(g_ren, sf);
			SDL_DestroySurface(sf);
		}
	}
	if(g_icon_tex) {
		SDL_FRect d = { (float)(cx + (cw - 80) / 2), (float)(cy + 14), 80, 80 };

		SDL_RenderTexture(g_ren, g_icon_tex, NULL, &d);
	}
	for(i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); i++) {
		int tw = ui_text_width(lines[i]);

		ui_draw_text(cx + (cw - tw) / 2, cy + 108 + i * 17, lines[i], i == 0 ? UI_BLACK : (i == 1 ? UI_GREY : UI_BLACK));
	}
	if(ui_button(win, cx, cy, (cw - 80) / 2, win->h - UI_TITLE_H - 38, 80, 24, "OK", 1)) {
		ui_window_close(win);
	}
}

static void
about_open(void)
{
	snprintf(g_about.title, sizeof(g_about.title), "About EmuBuddy Launcher");
	g_about.w = 340;
	g_about.h = 356;
	g_about.closable = 1;
	g_about.modal = 1;
	g_about.draw = about_draw;
	ui_window_center(&g_about);
	ui_window_open(&g_about);
}

/* ---- menus ---- */

enum { C_SKIN0 = 100, C_NEW = 1, C_DUP, C_DEL, C_FOLDER, C_QUIT, C_PLAY, C_EDIT, C_ABOUT, C_LIB };

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
	{ NULL, 0, NULL, UI_MI_SEP },
	{ "Library...", C_LIB, "Cmd-L", 0 },
};
static const UiMenuItem m_help[] = {
	{ "About EmuBuddy Launcher", C_ABOUT, NULL, 0 },
};
static UiMenuItem m_skin[16];
static const UiMenu g_menus[] = {
	{ "File", m_file, 7 }, { "Profile", m_prof, 4 }, { "Skin", m_skin, 0 }, { "Help", m_help, 1 },
};
static UiMenu g_menus_rw[4];

static void
app_command(int id)
{
	if(id >= C_SKIN0 && id < C_SKIN0 + skin_count()) {
		g_skin = id - C_SKIN0;
		skin_apply(g_skin);
		settings_save();
		return;
	}
	switch(id) {
	case C_NEW: new_profile(); break;
	case C_DUP: duplicate_profile(); break;
	case C_DEL: delete_profile(); break;
	case C_FOLDER: show_folder(); break;
	case C_QUIT: g_running = 0; break;
	case C_PLAY: lib_play(); break;
	case C_EDIT: if(g_nprof) { edit_open(&g_prof[g_sel], 0); } break;
	case C_LIB: lib_open(LIB_DISK, NULL, 0); break;

	case C_ABOUT:
		about_open();
		break;
	}
}

static int
app_state(int id)
{
	return (id >= C_SKIN0 && id - C_SKIN0 == g_skin) ? UI_MI_CHECKED : 0;
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
	if(SDL_GetAtomicInt(&g_add_ready)) {
		SDL_SetAtomicInt(&g_add_ready, 0);
		if(g_nlfold < 16) {
			snprintf(g_lfold[g_nlfold++], sizeof(g_lfold[0]), "%s", g_addpath);
			lfold_save();
			lib_rescan();
		}
	}
	if(SDL_GetAtomicInt(&g_pick_ready)) {
		SDL_SetAtomicInt(&g_pick_ready, 0);
		if(g_pick_target) {
			SDL_strlcpy(g_pick_target, g_pick, (size_t)g_pick_cap);
		}
	}
}

static UiApp g_app = { g_menus_rw, 4, app_state, app_command, app_status, app_tick,
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
	lfold_load();
	settings_load();
	for(int i = 0; i < skin_count() && i < 16; i++) {
		m_skin[i].label = skin_get(i)->name;
		m_skin[i].id = C_SKIN0 + i;
	}
	memcpy(g_menus_rw, g_menus, sizeof(g_menus));
	g_menus_rw[2].n_items = skin_count();
	{
		SDL_Surface *ic = SDL_CreateSurfaceFrom(launcher_icon_size, launcher_icon_size,
				SDL_PIXELFORMAT_RGBA32, (void *)launcher_icon_rgba, launcher_icon_size * 4);

		if(ic) {
			SDL_SetWindowIcon(g_win, ic);
			SDL_DestroySurface(ic);
		}
	}
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
				} else {
					int lay = skin_layout(g_skin), step = 0;

					if(lay == SKIN_LAYOUT_LIST) {
						step = e.key.key == SDLK_DOWN ? 1 : e.key.key == SDLK_UP ? -1 : 0;
					} else if(lay == SKIN_LAYOUT_SHELF) {
						step = e.key.key == SDLK_RIGHT ? 1 : e.key.key == SDLK_LEFT ? -1 :
							e.key.key == SDLK_DOWN ? SHELF_COLS : e.key.key == SDLK_UP ? -SHELF_COLS : 0;
					} else {
						step = e.key.key == SDLK_RIGHT ? 1 : e.key.key == SDLK_LEFT ? -1 : 0;
					}
					if(step && g_sel + step >= 0 && g_sel + step < g_nprof) {
						g_sel += step;
					}
				}
			}
		}
		{
			Uint32 d = g_ui_theme.desktop;

			SDL_SetRenderDrawColor(g_ren, (d >> 16) & 255, (d >> 8) & 255, d & 255, 255);
			SDL_RenderClear(g_ren);
			if(skin_desktop(g_skin) == SKIN_DESK_NEON) {
				float hz = 270;				/* horizon */

				SDL_SetRenderDrawColor(g_ren, 0, 240, 255, 255);
				SDL_RenderLine(g_ren, 0, hz, LW, hz);
				SDL_SetRenderDrawColor(g_ren, 255, 43, 214, 255);
				for(int k = 1; k < 14; k++) {		/* horizontal lines, closer together up top */
					float t = k / 14.0f, yy = hz + (LH - hz) * t * t;

					SDL_RenderLine(g_ren, 0, yy, LW, yy);
				}
				for(int k = -12; k <= 12; k++) {	/* lines running to the vanishing point */
					SDL_RenderLine(g_ren, LW / 2.0f + k * 8, hz, LW / 2.0f + k * 90.0f, LH);
				}
				/* a banded sun above the horizon */
				for(int k = 0; k < 9; k++) {
					float r = 70, yy = hz - 4 - k * 8, hw = SDL_sqrtf(SDL_max(0.0f, r * r - (r - k * 8 - 4) * (r - k * 8 - 4)));

					if(k % 2 == 0 || k > 4) {
						SDL_SetRenderDrawColor(g_ren, 255, 120 - k * 8, 40 + k * 14, 255);
						SDL_FRect b = { LW / 2.0f - hw, yy - 6, hw * 2, 6 };

						SDL_RenderFillRect(g_ren, &b);
					}
				}
			}
			if(g_ui_theme.checker) {
				static SDL_Texture *chk;

				if(!chk) {
					Uint32 px[4] = { 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000 };

					chk = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
								SDL_TEXTUREACCESS_STATIC, 2, 2);
					SDL_UpdateTexture(chk, NULL, px, 8);
					SDL_SetTextureScaleMode(chk, SDL_SCALEMODE_NEAREST);
				}
				SDL_FRect all = { 0, UI_BAR_H, LW, LH - 2 * UI_BAR_H };

				SDL_RenderTextureTiled(g_ren, chk, NULL, 1.0f, &all);
			}
		}
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
			if(frame == 6 && getenv("LAUNCHER_LIB")) {
				lib_open(LIB_DISK, NULL, 0);
				g_ltab = atoi(getenv("LAUNCHER_LIB")) > 1;
			}
			if(frame == 3 && getenv("LAUNCHER_SKIN") && skin_find(getenv("LAUNCHER_SKIN")) >= 0) {
				g_skin = skin_find(getenv("LAUNCHER_SKIN"));
				skin_apply(g_skin);
			}
			if(frame == 5 && getenv("LAUNCHER_ABOUT")) {
				about_open();
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
