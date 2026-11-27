/* EmuBuddy Launcher - first version: a list of profiles, Play starts EmuBuddy.
 * Phase 1 of docs/LAUNCHER_PLAN.md (the real front end and skins come next).
 *
 *   emubuddy-launcher                 the window
 *   emubuddy-launcher --list          list the profiles
 *   emubuddy-launcher --cmd <name>    print the EmuBuddy command line
 *   emubuddy-launcher --play <name>   start EmuBuddy with the profile
 *   EMUBUDDY_EXE=<path>               EmuBuddy to start (else found next to this program)
 *   EMUBUDDY_PROFILES=<folder>        profile folder (else the application data folder)
 *   LAUNCHER_LANG=de | en             texts in German or English, whatever the system and the setting say */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "profile.h"
#include "library.h"
#include "skins.h"
#include "sound.h"
#include "frontend.h"
#include "attract.h"
#include "launcher_icon.h"
#include "lang.h"

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

static int	g_sort;			/* 0 A-Z, 1 favourites first, 2 last played, 3 most played */
static int	g_favonly;
static const char *g_sort_names[] = { "A-Z", "Favourites first", "Last played", "Most played" };
static const char *g_layout_names[] = { "Carousel", "1 Row", "2 Rows", "3 Rows" };

static int
is_fav(const Profile *p)
{
	return profile_get(p, "favorite")[0] == '1';
}

static int
by_name(const void *a, const void *b)
{
	const Profile *x = a, *y = b;
	long	r = 0;

	switch(g_sort) {
	case 1:
		r = (long)is_fav(y) - (long)is_fav(x);
		break;
	case 2:
		r = atol(profile_get(y, "last_played")) - atol(profile_get(x, "last_played"));
		break;
	case 3:
		r = atol(profile_get(y, "plays")) - atol(profile_get(x, "plays"));
		break;
	}
	if(r) {
		return r < 0 ? -1 : 1;
	}
	return SDL_strcasecmp(profile_name(x), profile_name(y));
}

/* sort again and keep the same game selected */
static int	g_sel;
static void
resort_keep(void)
{
	char	keep[1100];

	keep[0] = 0;
	if(g_nprof && g_sel >= 0 && g_sel < g_nprof) {
		snprintf(keep, sizeof(keep), "%s", g_prof[g_sel].path);
	}
	qsort(g_prof, (size_t)g_nprof, sizeof(Profile), by_name);
	for(int i = 0; keep[0] && i < g_nprof; i++) {
		if(!strcmp(g_prof[i].path, keep)) {
			g_sel = i;
		}
	}
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
	if(g_favonly) {			/* only favourites; if there are none, show everything */
		int k = 0;

		for(int i = 0; i < g_nprof; i++) {
			if(is_fav(&g_prof[i])) {
				if(k != i) {
					g_prof[k] = g_prof[i];
				}
				k++;
			}
		}
		if(k > 0) {
			g_nprof = k;
		} else {
			g_favonly = 0;
			load_profiles();
			return;
		}
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
			"../MacOS/EmuBuddy",
			/* run from the build folder: build/launcher/EmuBuddy Launcher.app next to build/EmuBuddy.app */
			"../../../../EmuBuddy.app/Contents/MacOS/EmuBuddy", "../EmuBuddy", "../EmuBuddy.exe", NULL };

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

#ifdef __APPLE__
void	launcher_mac_front(SDL_Process *proc);	/* launcher_mac.m */
#endif

static SDL_Window	*g_win;
static int		g_game_other;	/* 1: games open on a screen the Launcher is not on */

/* The number EmuBuddy's -screen takes (1 = the first screen the system lists) of the first
 * screen the Launcher's window is not on, or 0 when there is only one. */
static int
other_screen(void)
{
	SDL_DisplayID	*d, own = SDL_GetDisplayForWindow(g_win);
	int		n = 0, i, r = 0;

	d = SDL_GetDisplays(&n);
	for(i = 0; d && i < n && !r; i++) {
		if(d[i] != own) {
			r = i + 1;
		}
	}
	SDL_free(d);
	return r;
}

static int
play(const Profile *p, char *msg, size_t msgsz)
{
	char		buf[6000], err[200], exe[1100], scr[8];
	char		*argv[PROFILE_MAX_ARGS];
	int		n;
	SDL_Process	*proc;

	if(!find_emubuddy(exe, sizeof(exe))) {
		snprintf(msg, msgsz, "%s", T("EmuBuddy was not found (set EMUBUDDY_EXE)"));
		return -1;
	}
	n = build(p, buf, sizeof(buf), argv, err, sizeof(err), 1);
	if(n < 0) {
		snprintf(msg, msgsz, "%s", err);
		return -1;
	}
	if(g_game_other && g_win && n + 2 < PROFILE_MAX_ARGS) {	/* the game on the other screen */
		int	other = other_screen();

		if(other > 0) {
			snprintf(scr, sizeof(scr), "%d", other);
			argv[n++] = "-screen";
			argv[n++] = scr;
			argv[n] = NULL;
		}
	}
	proc = SDL_CreateProcess((const char * const *)argv, false);
	if(!proc) {
		snprintf(msg, msgsz, T("Could not start EmuBuddy: %s"), SDL_GetError());
		return -1;
	}
#ifdef __APPLE__
	launcher_mac_front(proc);	/* the game gets the keys without a click */
#endif
	SDL_DestroyProcess(proc);
	snprintf(msg, msgsz, T("Started %s"), profile_name(p));
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
#define AW	1280			/* the Arcade view's logical size */
#define AH	720
#define PW	800			/* the same on a portrait screen (9:16); the dialogs need 760 across */
#define PH	1422

static void lib_open(int kind, char *target, int cap);
static int		g_idle = 60;	/* seconds before the slideshow, 0 = off */
static int		g_arcade = 1, g_bw = LW, g_bh = LH;	/* the Arcade view or the Classic one; logical size */
static int		g_portrait;	/* the Arcade window is opened upright (View > Portrait Window) */
static int		g_tall;		/* the window is taller than wide right now: portrait layout */
static int		g_lang;		/* texts: 0 = like the system, 1 = English, 2 = German */
static char		g_art_root[1100];
static int		g_skin, g_wscale = 100, g_full;	/* window size in percent of 800x640 */
static const int	g_steps[6] = { 80, 100, 125, 150, 200, 300 };
static SDL_Renderer	*g_ren;
static int		g_sel, g_top, g_running = 1;
static UiWindow		g_lib, g_edit_win, g_easy_win;
static char		g_toast[200];

/* text fields: one is focused at a time (pointer to its buffer) */
static char		*g_focus;
static int		g_focus_cap;

static int	g_caret;		/* byte offset of the cursor in the focused field */
static int	g_pending_unfocus;	/* a click landed; unfocus unless a field takes it */

static int
utf8_prev(const char *t, int i)
{
	if(i > 0) {
		i--;
	}
	while(i > 0 && ((unsigned char)t[i] & 0xC0) == 0x80) {
		i--;
	}
	return i;
}

static int
utf8_next(const char *t, int i)
{
	if(t[i]) {
		i++;
	}
	while(t[i] && ((unsigned char)t[i] & 0xC0) == 0x80) {
		i++;
	}
	return i;
}

/* width of t[from..to) */
static int
span_w(const char *t, int from, int to)
{
	char	tmp[700];
	int	n = to - from;

	if(n <= 0) {
		return 0;
	}
	if(n > (int)sizeof(tmp) - 1) {
		n = (int)sizeof(tmp) - 1;
	}
	memcpy(tmp, t + from, (size_t)n);
	tmp[n] = 0;
	return ui_text_width(tmp);
}

/* ---- arrow keys and the game controller in dialogs ----
 * The toolkit's widgets are made for the mouse. These wrappers note where each widget of the
 * front dialog is; the arrow keys (or the D-pad and the stick) move a focus frame from widget to
 * widget and Return (or A) presses the one in the frame. Inside a list Up and Down choose a row,
 * inside a row of choices Left and Right choose one. Esc (or B) closes the dialog. */

enum { NAV_BUTTON, NAV_CHECK, NAV_SEG, NAV_LIST, NAV_FIELD };

typedef struct {
	UiWindow	*win;
	int		kind, x, y, w, h;
	int		*sel, *top, n, rows;	/* lists and rows of choices */
	unsigned	mask;			/* which choices can be chosen */
} NavItem;

static NavItem	g_nav[200];			/* the widgets drawn in the last frame */
static int	g_nnav;
static struct { UiWindow *win; int x, y; } g_nav_at[8];	/* where the frame is, per window */
static int	g_nav_on;			/* show the frame: keys or the controller are in use */
static int	g_nav_press;			/* Return: the widget in the frame acts in the next frame */
static UiWindow	g_ovl;

static int
nav_slot(UiWindow *win, int make)
{
	int	i, free_slot = -1;

	for(i = 0; i < 8; i++) {
		if(g_nav_at[i].win == win) {
			return i;
		}
		if(free_slot < 0 && (!g_nav_at[i].win || !g_nav_at[i].win->open)) {
			free_slot = i;
		}
	}
	if(make && free_slot >= 0) {
		g_nav_at[free_slot].win = win;
		g_nav_at[free_slot].x = g_nav_at[free_slot].y = -1;
		return free_slot;
	}
	return -1;
}

/* the modal window in front, or NULL: that is where the keys go */
static UiWindow *
nav_front(void)
{
	UiWindow *front = NULL;

	for(int i = 0; i < g_nnav; i++) {
		if(g_nav[i].win->modal && g_nav[i].win->open) {
			front = g_nav[i].win;
		}
	}
	return front == &g_ovl ? NULL : front;
}

static NavItem *
nav_current(UiWindow *front)
{
	int	s = nav_slot(front, 0);

	for(int i = 0; s >= 0 && i < g_nnav; i++) {
		if(g_nav[i].win == front && g_nav[i].x == g_nav_at[s].x && g_nav[i].y == g_nav_at[s].y) {
			return &g_nav[i];
		}
	}
	return NULL;
}

static void
nav_go(const NavItem *it)
{
	int	s = nav_slot(it->win, 1);

	if(s >= 0) {
		g_nav_at[s].x = it->x;
		g_nav_at[s].y = it->y;
	}
}

/* Notes a widget. Returns 1 if the focus frame is on it (and draws the frame). */
static int
nav_add(UiWindow *win, int kind, int cx, int cy, int x, int y, int w, int h, int enabled)
{
	NavItem	*it;
	int	s;

	if(!enabled || g_nnav >= (int)(sizeof(g_nav) / sizeof(g_nav[0]))) {
		return 0;
	}
	it = &g_nav[g_nnav++];
	memset(it, 0, sizeof(*it));
	it->win = win;
	it->kind = kind;
	it->x = x;
	it->y = y;
	it->w = w;
	it->h = h;
	s = nav_slot(win, 0);
	if(!g_nav_on || !win->modal || s < 0 || g_nav_at[s].x != x || g_nav_at[s].y != y) {
		return 0;
	}
	ui_frame(cx + x - 2, cy + y - 2, w + 4, h + 4, g_ui_theme.accent);
	ui_frame(cx + x - 3, cy + y - 3, w + 6, h + 6, g_ui_theme.accent);
	return 1;
}

static int
nbutton(UiWindow *win, int cx, int cy, int x, int y, int w, int h, const char *label, int enabled)
{
	int	r = ui_button(win, cx, cy, x, y, w, h, T(label), enabled);

	if(nav_add(win, NAV_BUTTON, cx, cy, x, y, w, h, enabled) && g_nav_press) {
		g_nav_press = 0;
		r = 1;
	}
	return r;
}

static int
ncheck(UiWindow *win, int cx, int cy, int x, int y, const char *label, int *value)
{
	int	r = ui_checkbox(win, cx, cy, x, y, T(label), value);

	if(nav_add(win, NAV_CHECK, cx, cy, x, y, 18 + ui_text_width(T(label)), 14, 1) && g_nav_press) {
		g_nav_press = 0;
		*value = !*value;
		r = 1;
	}
	return r;
}

static int
nsegment(UiWindow *win, int cx, int cy, int x, int y, int w, int h, const char **labels, int n, int *sel, unsigned enabled)
{
	int	r = ui_segment(win, cx, cy, x, y, w, h, labels, n, sel, enabled);

	nav_add(win, NAV_SEG, cx, cy, x, y, w, h, enabled != 0);
	if(g_nnav > 0 && g_nav[g_nnav - 1].kind == NAV_SEG && g_nav[g_nnav - 1].x == x && g_nav[g_nnav - 1].y == y) {
		g_nav[g_nnav - 1].sel = sel;
		g_nav[g_nnav - 1].n = n;
		g_nav[g_nnav - 1].mask = enabled;
	}
	return r;
}

static int
nlist(UiWindow *win, int cx, int cy, int x, int y, int w, int h, const char **items, int n_items, int *sel, int *top)
{
	int	r = ui_list(win, cx, cy, x, y, w, h, items, n_items, sel, top);
	int	f = nav_add(win, NAV_LIST, cx, cy, x, y, w, h, 1);

	if(g_nnav > 0 && g_nav[g_nnav - 1].kind == NAV_LIST && g_nav[g_nnav - 1].x == x && g_nav[g_nnav - 1].y == y) {
		g_nav[g_nnav - 1].sel = sel;
		g_nav[g_nnav - 1].top = top;
		g_nav[g_nnav - 1].n = n_items;
		g_nav[g_nnav - 1].rows = (h - 2) / 18;
	}
	if(f && g_nav_press) {
		g_nav_press = 0;
		if(*sel >= 0 && *sel < n_items) {
			r = 2;				/* like a double-click on the row */
		}
	}
	return r;
}

/* called before the windows are drawn: forget the widgets of the frame before */
static void
nav_begin(void)
{
	g_nnav = 0;
}

/* called after the windows are drawn: the frame goes to a sensible widget when it has none */
static void
nav_end(void)
{
	UiWindow *front = nav_front();

	g_nav_press = 0;
	if(front && g_nav_on && !nav_current(front)) {
		NavItem *first = NULL;

		for(int i = 0; i < g_nnav; i++) {
			if(g_nav[i].win == front && (!first || (g_nav[i].kind == NAV_LIST && first->kind != NAV_LIST))) {
				first = &g_nav[i];
			}
		}
		if(first) {
			nav_go(first);
		}
	}
}

static void	focus_clear(void);

/* An arrow key, Return or Esc while a dialog is open. Returns 1 if the key was used. */
static int
nav_key(SDL_Keycode k)
{
	UiWindow *front = nav_front();
	NavItem	*cur, *best = NULL;
	float	best_score = 0;
	int	horiz = k == SDLK_LEFT || k == SDLK_RIGHT, fwd = k == SDLK_RIGHT || k == SDLK_DOWN;

	if(!front || ui_menu_is_open() || (!horiz && k != SDLK_UP && k != SDLK_DOWN && k != SDLK_RETURN && k != SDLK_ESCAPE)) {
		return 0;
	}
	if(k == SDLK_ESCAPE) {
		if(front->closable) {
			focus_clear();
			ui_window_close(front);
			snd_play(SND_BACK);
		}
		return 1;
	}
	cur = nav_current(front);
	if(!g_nav_on || !cur) {				/* the first key only shows the frame */
		g_nav_on = 1;
		snd_play(SND_MOVE);
		return 1;
	}
	if(k == SDLK_RETURN) {
		g_nav_press = 1;
		snd_play(SND_SELECT);
		return 1;
	}
	if(cur->kind == NAV_LIST && !horiz && cur->sel && cur->top) {
		int s = *cur->sel + (fwd ? 1 : -1);

		if(s >= 0 && s < cur->n) {
			*cur->sel = s;
			if(s < *cur->top) {
				*cur->top = s;
			} else if(cur->rows > 0 && s >= *cur->top + cur->rows) {
				*cur->top = s - cur->rows + 1;
			}
			snd_play(SND_MOVE);
			return 1;
		}
	}
	if(cur->kind == NAV_SEG && horiz && cur->sel) {
		for(int s = *cur->sel + (fwd ? 1 : -1); s >= 0 && s < cur->n; s += fwd ? 1 : -1) {
			if((cur->mask >> s) & 1) {
				*cur->sel = s;
				snd_play(SND_MOVE);
				return 1;
			}
		}
	}
	/* the nearest widget in that direction, widgets in the same row or column first */
	for(int i = 0; i < g_nnav; i++) {
		NavItem	*c = &g_nav[i];
		float	ccx = (float)c->x + (float)c->w / 2, ccy = (float)c->y + (float)c->h / 2;
		float	ox = (float)cur->x + (float)cur->w / 2, oy = (float)cur->y + (float)cur->h / 2;
		float	along, across, score;

		if(c->win != front || c == cur) {
			continue;
		}
		if(horiz) {
			along = fwd ? ccx - ox : ox - ccx;
			across = (float)SDL_max(0, SDL_max(c->y - (cur->y + cur->h), cur->y - (c->y + c->h)));
			score = along + across * 6 + SDL_fabsf(ccy - oy) * 0.01f;
		} else {
			along = fwd ? ccy - oy : oy - ccy;
			across = (float)SDL_max(0, SDL_max(c->x - (cur->x + cur->w), cur->x - (c->x + c->w)));
			score = along + across * 6 + (float)SDL_abs(c->x - cur->x) * 0.01f;	/* forms are left-aligned */
		}
		if(along > 1 && (!best || score < best_score)) {
			best = c;
			best_score = score;
		}
	}
	if(best) {
		nav_go(best);
		snd_play(SND_MOVE);
	}
	return 1;
}

static int
field(UiWindow *win, int cx, int cy, int x, int y, int w, char *buf, int cap)
{
	int	focused = g_focus == buf, len = (int)strlen(buf), caret, s = 0, e;
	char	vis[700];

	if(focused && g_caret > len) {
		g_caret = len;
	}
	caret = focused ? g_caret : len;
	ui_fill(cx + x, cy + y, w, 18, UI_WHITE);
	ui_frame(cx + x, cy + y, w, 18, focused ? 0x2040C0 : UI_BLACK);
	/* scroll so that the cursor is visible */
	while(s < caret && span_w(buf, s, caret) > w - 14) {
		s = utf8_next(buf, s);
	}
	e = s;
	while(buf[e] && span_w(buf, s, utf8_next(buf, e)) <= w - 8 && utf8_next(buf, e) - s < (int)sizeof(vis) - 1) {
		e = utf8_next(buf, e);
	}
	memcpy(vis, buf + s, (size_t)(e - s));
	vis[e - s] = 0;
	ui_draw_text(cx + x + 4, cy + y + 1, vis, UI_BLACK);
	if(focused && (SDL_GetTicks() / 500) % 2 == 0) {
		ui_fill(cx + x + 4 + span_w(buf, s, caret), cy + y + 3, 1, 12, UI_BLACK);
	}
	if(nav_add(win, NAV_FIELD, cx, cy, x, y, w, 18, 1) && g_nav_press) {	/* Return: type here */
		g_nav_press = 0;
		g_focus = buf;
		g_focus_cap = cap;
		g_caret = len;
		SDL_StartTextInput(g_win);
	}
	if(ui_hit(win, cx, cy, x, y, w, 18)) {
		float	mx, my, lx = 0, ly = 0;
		int	k = s, best = s;

		SDL_GetMouseState(&mx, &my);
		SDL_RenderCoordinatesFromWindow(g_ren, mx, my, &lx, &ly);
		while(k < e) {			/* the character gap nearest to the click */
			int nk = utf8_next(buf, k), mid = (span_w(buf, s, k) + span_w(buf, s, nk)) / 2;

			if(lx - (cx + x + 4) >= mid) {
				best = nk;
			}
			k = nk;
		}
		g_focus = buf;
		g_focus_cap = cap;
		g_caret = best;
		g_pending_unfocus = 0;
		if(g_nnav > 0 && g_nav[g_nnav - 1].kind == NAV_FIELD && g_nav[g_nnav - 1].x == x && g_nav[g_nnav - 1].y == y) {
			nav_go(&g_nav[g_nnav - 1]);		/* the arrow keys go on from here */
		}
		SDL_StartTextInput(g_win);
		return 1;
	}
	return 0;
}

/* Put text at the cursor of the focused field. */
static void
focus_insert(const char *text)
{
	int	len = (int)strlen(g_focus), m = (int)strlen(text);

	if(len + m >= g_focus_cap) {
		return;
	}
	memmove(g_focus + g_caret + m, g_focus + g_caret, (size_t)(len - g_caret + 1));
	memcpy(g_focus + g_caret, text, (size_t)m);
	g_caret += m;
}

/* Clipboard commands for the focused field (keys and the Edit menu). */
static void
clip_paste(void)
{
	char	*t;

	if(!g_focus) {
		ui_message(T("Click in a text field first"));
		return;
	}
	t = SDL_HasClipboardText() ? SDL_GetClipboardText() : NULL;
	if(t && t[0]) {
		char	clean[700];
		int	n = 0;

		for(char *q = t; *q && n < (int)sizeof(clean) - 1; q++) {
			if((unsigned char)*q >= 32) {
				clean[n++] = *q;
			}
		}
		int before = (int)strlen(g_focus);

		clean[n] = 0;
		focus_insert(clean);
		if(n > 0 && (int)strlen(g_focus) == before) {
			ui_message(T("Too long for this field"));
		} else {
			ui_message(T("Pasted %d characters"), n);
		}
	} else {
		ui_message(T("The clipboard has no text (copy some text first)"));
	}
	SDL_free(t);
}

static void
clip_copy(int cut)
{
	if(!g_focus) {
		ui_message(T("Click in a text field first"));
		return;
	}
	SDL_SetClipboardText(g_focus);
	if(cut) {
		g_focus[0] = 0;
		g_caret = 0;
	}
	ui_message(cut ? T("Cut %d characters") : T("Copied %d characters"), (int)strlen(g_focus));
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
	{ "sl2", { "none", "modem", "imagewriter", "fujinet" }, 4, 0 },
	{ "sl3", { "none", "uthernet2", "voc", "secondsight" }, 4, 0 },
	{ "sl4", { "none", "mockingboard", "uthernet2" }, 3, 0 },
	{ "sl7", { "none", "scsi", "hsscsi" }, 3, 1 },	/* scsi: the hard disk card */
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
	{ "flop3", "3.5\" drive 1", "", 0 },
	{ "flop1", "5.25\" drive 1", "", 0 },
	{ "share", "Shared folder", "", 1 },
	{ "bridge", "Bridge folder", "", 1 },
	{ "cover", "Cover picture", "", 0 },
	{ "hard4", "Hard disk 4", "", 0 },
	{ "hard5", "Hard disk 5", "", 0 },
	{ "hard6", "Hard disk 6", "", 0 },
	{ "hard7", "Hard disk 7", "", 0 },
	{ "cdrom", "CD-ROM", "", 0 },		/* with sl7 = hsscsi */
	{ "flop4", "3.5\" drive 2", "", 0 },
	{ "flop2", "5.25\" drive 2", "", 0 },
	{ "screen", "Screenshot", "", 0 },	/* shown when the Launcher shows screenshots */
};
#define NPF	((int)(sizeof(g_pf) / sizeof(g_pf[0])))

static struct {
	Profile	p;
	char	name[100], desc[200], extra[300];
	int	crt, full, bar, is_new;	/* bar: the status bar under the picture */
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
	g_e.bar = strcmp(profile_get(src, "statusbar"), "0") != 0;
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
	set_keep(&g_e.p, "statusbar", g_e.bar ? "" : "0");
	file_name_for(g_e.name, path, sizeof(path));
	if(profile_save(&g_e.p, path) != 0) {
		snprintf(g_toast, sizeof(g_toast), T("Could not save %s"), path);
		return;
	}
	if(!g_e.is_new && g_e.orig_path[0] && strcmp(path, g_e.orig_path) != 0) {
		SDL_RemovePath(g_e.orig_path);		/* renamed */
	}
	snprintf(g_toast, sizeof(g_toast), T("Saved %s"), g_e.name);
	ui_message(T("Saved %s"), g_e.name);
	focus_clear();
	ui_window_close(&g_edit_win);
	ui_window_close(&g_easy_win);
	load_profiles();
	for(i = 0; i < g_nprof; i++) {
		if(!strcmp(g_prof[i].path, path)) {
			g_sel = i;
		}
	}
}

/* A button that is wide enough for its text (some skin fonts are wide). */
static int
fit_button(UiWindow *win, int cx, int cy, int *x, int y, int minw, int h, const char *label, int enabled)
{
	int	w = ui_text_width(T(label)) + 20, r;

	if(w < minw) {
		w = minw;
	}
	r = nbutton(win, cx, cy, *x, y, w, h, label, enabled);
	*x += w + 6;
	return r;
}

static int
text_w(const char *s, int min)
{
	int	w = ui_text_width(s) + 16;

	return w > min ? w : min;
}

static void
edit_draw(UiWindow *win, int cx, int cy)
{
	int	y = 8, i, lc = 110, bw_lib, bw_choose, xlib, xch, xclr, fend_file, fend_dir;
	static const char *names[] = { "Memory", "Speed (MHz)", "Slot 1 printer", "Slot 2 modem",
		"Slot 3", "Slot 4", "Slot 7", "Window size" };

	/* the label column is as wide as the widest label in the skin's font */
	for(i = 0; i < NPF; i++) {
		lc = SDL_max(lc, ui_text_width(T(g_pf[i].label)) + 18);
	}
	for(i = 0; i < NCH; i++) {
		lc = SDL_max(lc, ui_text_width(T(names[i])) + 18);
	}
	lc = SDL_max(lc, ui_text_width(T("Extra options")) + 18);
	bw_lib = text_w(T("Library"), 74);
	bw_choose = text_w(T("Choose..."), 74);
	xclr = win->w - 2 - 40;
	xch = xclr - 4 - bw_choose;
	xlib = xch - 4 - bw_lib;
	fend_file = xlib - 6;
	fend_dir = xch - 6;

	ui_draw_text(cx + 10, cy + y + 1, T("Name"), UI_BLACK);
	field(win, cx, cy, lc, y, 200, g_e.name, sizeof(g_e.name));
	{
		int dx = lc + 200 + 14, dl = ui_text_width(T("Description")) + 8;

		ui_draw_text(cx + dx, cy + y + 1, T("Description"), UI_BLACK);
		field(win, cx, cy, dx + dl, y, win->w - 2 - 10 - dx - dl, g_e.desc, sizeof(g_e.desc));
	}
	y += 30;
	{
		/* ROM on its own row, then two columns: the 7 hard disks | the 4 floppy drives and the folders */
		static const int left[] = { 1, 2, 3, 9, 10, 11, 12, -1 }, right[] = { 4, 13, 5, 14, 6, 7, 8, 15 };
		int	colw = (win->w - 2 - 20 - 12) / 2, c, r, bl = text_w(T("Lib"), 36), bc = text_w("...", 30), bx = 24;

		/* a narrower label column for the two-column part */
		int	lw = 10;

		for(i = 1; i < NPF; i++) {
			lw = SDL_max(lw, ui_text_width(T(g_pf[i].label)) + 14);
		}
		ui_draw_text(cx + 10, cy + y + 1, T(g_pf[0].label), UI_BLACK);
		field(win, cx, cy, lc, y, fend_file - lc, g_pf[0].buf, sizeof(g_pf[0].buf));
		if(nbutton(win, cx, cy, xlib, y - 1, bw_lib, 20, "Library", 1)) {
			lib_open(LIB_ROM, g_pf[0].buf, (int)sizeof(g_pf[0].buf));
		}
		if(nbutton(win, cx, cy, xch, y - 1, bw_choose, 20, "Choose...", 1)) {
			choose(&g_pf[0]);
		}
		if(nbutton(win, cx, cy, xclr, y - 1, 30, 20, "X", g_pf[0].buf[0] != 0)) {
			g_pf[0].buf[0] = 0;
		}
		y += 28;
		for(c = 0; c < 2; c++) {
			int	x0 = 10 + c * (colw + 12), xx = x0 + colw - bx, xc = xx - 2 - bc, xl = xc - 2 - bl;

			for(r = 0; r < 8; r++) {
				int	fi = c ? right[r] : left[r];
				PathField *f;
				int	yy = y + r * 24, disk;

				if(fi < 0) {
					continue;
				}
				f = &g_pf[fi];
				disk = !f->folder && strcmp(f->key, "cover") != 0 && strcmp(f->key, "screen") != 0;

				ui_draw_text(cx + x0, cy + yy + 1, T(f->label), UI_BLACK);
				field(win, cx, cy, x0 + lw, yy, (disk ? xl : xc) - 4 - x0 - lw, f->buf, sizeof(f->buf));
				if(disk && nbutton(win, cx, cy, xl, yy - 1, bl, 20, "Lib", 1)) {
					lib_open(LIB_DISK, f->buf, (int)sizeof(f->buf));
				}
				if(nbutton(win, cx, cy, xc, yy - 1, bc, 20, "...", 1)) {
					choose(f);
				}
				if(nbutton(win, cx, cy, xx, yy - 1, bx, 20, "X", f->buf[0] != 0)) {
					f->buf[0] = 0;
				}
			}
		}
		y += 8 * 24;
	}
	y += 6;
	ui_hline(cx + 10, cy + y, win->w - 22, UI_LGREY);
	y += 8;
	for(i = 0; i < NCH; i++) {
		Choice *c = &g_ch[i];
		int	cw = 0, k;

		for(k = 0; k < c->n; k++) {
			cw = SDL_max(cw, ui_text_width(c->labels[k]) + 16);
		}
		ui_draw_text(cx + 10, cy + y + 2, T(names[i]), UI_BLACK);
		nsegment(win, cx, cy, lc, y, c->n * cw, 20, c->labels, c->n, &c->sel,
						(1u << c->n) - 1);
		y += 24;
	}
	ncheck(win, cx, cy, 10, y, "Curved CRT look", &g_e.crt);
	ncheck(win, cx, cy, 30 + ui_text_width(T("Curved CRT look")) + 20, y, "Full screen", &g_e.full);
	ncheck(win, cx, cy, 30 + ui_text_width(T("Curved CRT look")) + 20 + 30 +
			ui_text_width(T("Full screen")) + 20, y, "Status bar", &g_e.bar);
	y += 24;
	ui_draw_text(cx + 10, cy + y + 1, T("Extra options"), UI_BLACK);
	field(win, cx, cy, lc, y, win->w - 2 - 10 - lc, g_e.extra, sizeof(g_e.extra));
	y += 34;
	ui_draw_text(cx + 10, cy + y + 4, T("Drop disks and folders here"), UI_GREY);
	{
		int wc = text_w(T("Cancel"), 80), ws = text_w(T("Save"), 80), xs = win->w - 2 - 10 - ws;

		if(nbutton(win, cx, cy, xs - 8 - wc, y, wc, 24, "Cancel", 1)) {
			focus_clear();
			ui_window_close(win);
		}
		if(nbutton(win, cx, cy, xs, y, ws, 24, "Save", g_e.name[0] != 0)) {
			edit_save();
		}
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
	if(ext && fe_is_picture(path)) {
		snprintf(g_pf[8].buf, sizeof(g_pf[8].buf), "%s", path);
		return 1;
	}
	if(ext && (!SDL_strcasecmp(ext, ".woz") || !SDL_strcasecmp(ext, ".dsk") ||
			!SDL_strcasecmp(ext, ".do")) && info.size <= 150000) {
		slot = 5;
	} else if(info.size <= 1600000) {
		slot = 4;
	} else {
		static const int hd[] = { 1, 2, 3, 9, 10, 11, 12 };

		for(int i = 0; i < 7 && slot < 0; i++) {
			if(!g_pf[hd[i]].buf[0]) {
				slot = hd[i];
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
edit_open_full(void)
{
	focus_clear();
	snprintf(g_edit_win.title, sizeof(g_edit_win.title), "%s", g_e.is_new ? T("New Profile") : T("Edit Profile"));
	g_edit_win.w = 740;
	g_edit_win.h = 614;
	g_edit_win.modal = 1;
	g_edit_win.closable = 1;
	g_edit_win.draw = edit_draw;
	g_edit_win.on_drop = edit_drop;
	ui_window_center(&g_edit_win);
	g_edit_win.y = 24;
	ui_window_open(&g_edit_win);
}

/* ---- the simple editor: name, picture, two disks, a few switches ---- */

static SDL_Texture	*g_prev_tex;
static char		g_prev_path[1300];
static int		g_prev_w, g_prev_h;

static int		g_easy_fast, g_easy_sound, g_easy_net, g_easy_print;

static int
ch_index(const char *key)
{
	for(int i = 0; i < NCH; i++) {
		if(!strcmp(g_ch[i].key, key)) {
			return i;
		}
	}
	return 0;
}

static const char *
ch_value(const char *key)
{
	Choice *c = &g_ch[ch_index(key)];

	return c->sel >= 0 ? c->labels[c->sel] : "";
}

static void
ch_set(const char *key, const char *v)
{
	Choice *c = &g_ch[ch_index(key)];

	c->sel = idx_of(c, v);
}

/* the switches are read from the settings and written back to them */
static void
easy_read(void)
{
	g_easy_sound = !strcmp(ch_value("sl4"), "mockingboard");
	g_easy_net = !strcmp(ch_value("sl3"), "uthernet2") || !strcmp(ch_value("sl4"), "uthernet2");
	g_easy_print = !strcmp(ch_value("sl1"), "imagewriter") || !strcmp(ch_value("sl1"), "postscript");
	g_easy_fast = g_ch[ch_index("mhz")].sel >= 3;
}

static void
easy_sound_set(void)
{
	ch_set("sl4", g_easy_sound ? "mockingboard" : "none");
}

static void
easy_net_set(void)
{
	if(g_easy_net) {
		ch_set("sl3", "uthernet2");
		if(!strcmp(ch_value("sl4"), "uthernet2")) {
			ch_set("sl4", "none");
		}
	} else {
		ch_set("sl3", "none");
		if(!strcmp(ch_value("sl4"), "uthernet2")) {
			ch_set("sl4", "none");
		}
	}
}

static void
easy_print_set(void)
{
	ch_set("sl1", g_easy_print ? "imagewriter" : "none");
}

static void
easy_fast_set(void)
{
	ch_set("mhz", g_easy_fast ? "16" : "2.8");
}

static const char *
base_name(const char *path)
{
	const char *s = strrchr(path, '/');
	const char *b = strrchr(path, '\\');

	if(b && (!s || b > s)) {
		s = b;
	}
	return s ? s + 1 : path;
}

/* the picture shown in the editor: the one chosen here, else the game's own */
static SDL_Texture *
easy_preview(int *w, int *h)
{
	Profile	tmp = g_e.p;
	char	path[1400];

	profile_set(&tmp, "name", g_e.name);
	profile_set(&tmp, "cover", g_pf[8].buf);
	{
		int	scr = fe_art(), found;

		fe_set_art(0);			/* the editor shows the box picture it chooses */
		found = fe_find_art(&tmp, g_art_root, 0, path, sizeof(path));
		fe_set_art(scr);
		if(!found) {
			return NULL;
		}
	}
	if(strcmp(path, g_prev_path) != 0) {
		SDL_Surface *sf = fe_load_picture(path);

		if(g_prev_tex) {
			SDL_DestroyTexture(g_prev_tex);
			g_prev_tex = NULL;
		}
		snprintf(g_prev_path, sizeof(g_prev_path), "%s", path);
		if(sf) {
			g_prev_w = sf->w;
			g_prev_h = sf->h;
			g_prev_tex = SDL_CreateTextureFromSurface(g_ren, sf);
			SDL_DestroySurface(sf);
		}
	}
	*w = g_prev_w;
	*h = g_prev_h;
	return g_prev_tex;
}

static void
easy_picture_choose(void)
{
	static const SDL_DialogFileFilter filters[] = { { "Pictures", "png;jpg;jpeg;bmp" } };

	g_pick_target = g_pf[8].buf;
	g_pick_cap = (int)sizeof(g_pf[8].buf);
	SDL_ShowOpenFileDialog(pick_cb, NULL, g_win, filters, 1, NULL, false);
}

static void
easy_screen_choose(void)
{
	static const SDL_DialogFileFilter filters[] = { { "Pictures", "png;jpg;jpeg;bmp" } };

	g_pick_target = g_pf[15].buf;
	g_pick_cap = (int)sizeof(g_pf[15].buf);
	SDL_ShowOpenFileDialog(pick_cb, NULL, g_win, filters, 1, NULL, false);
}

/* one disk line: a name box, Library, Choose, clear */
static void
easy_disk_row(UiWindow *win, int cx, int cy, int y, int lc, const char *label, int fi, int is_rom)
{
	PathField *f = &g_pf[fi];
	int	bl = text_w(T("Library"), 70), bc = text_w(T("Choose..."), 70), x2 = win->w - 2 - 10 - 28;
	int	xc = x2 - 4 - bc, xl = xc - 4 - bl, w = xl - 6 - lc;
	const char *show = f->buf[0] ? base_name(f->buf) : T("(none)");
	char	vis[200];
	int	n;

	ui_draw_text(cx + 10, cy + y + 3, T(label), UI_BLACK);
	ui_fill(cx + lc, cy + y, w, 22, UI_WHITE);
	ui_frame(cx + lc, cy + y, w, 22, UI_BLACK);
	snprintf(vis, sizeof(vis), "%s", show);
	n = (int)strlen(vis);
	while(n > 3 && ui_text_width(vis) > w - 10) {
		n--;
		while(n > 0 && ((unsigned char)vis[n] & 0xC0) == 0x80) {
			n--;
		}
		vis[n] = 0;
	}
	ui_draw_text(cx + lc + 5, cy + y + 4, vis, f->buf[0] ? UI_BLACK : UI_GREY);
	if(nbutton(win, cx, cy, xl, y, bl, 22, "Library", 1)) {
		lib_open(is_rom ? LIB_ROM : LIB_DISK, f->buf, (int)sizeof(f->buf));
	}
	if(nbutton(win, cx, cy, xc, y, bc, 22, "Choose...", 1)) {
		choose(f);
	}
	if(nbutton(win, cx, cy, x2, y, 28, 22, "X", f->buf[0] != 0)) {
		f->buf[0] = 0;
	}
}

static void
easy_draw(UiWindow *win, int cx, int cy)
{
	int	y = 10, lc, pw = 200, ph = 150, bx, i, was;
	int	fl = (!g_pf[4].buf[0] && g_pf[5].buf[0]) ? 5 : 4;
	SDL_Texture *tx;
	int	tw = 0, th = 0;

	lc = SDL_max(ui_text_width(T("Main disk")), ui_text_width(T("Floppy disk"))) + 24;
	lc = SDL_max(lc, ui_text_width(T("Name")) + 24);
	ui_draw_text(cx + 10, cy + y + 1, T("Name"), UI_BLACK);
	field(win, cx, cy, lc, y, win->w - 2 - 10 - lc, g_e.name, sizeof(g_e.name));
	y += 34;

	/* the picture */
	ui_fill(cx + 10, cy + y, pw, ph, 0x202428);
	ui_frame(cx + 10, cy + y, pw, ph, UI_BLACK);
	tx = easy_preview(&tw, &th);
	if(tx && tw > 0 && th > 0) {
		float s = SDL_min((float)(pw - 4) / (float)tw, (float)(ph - 4) / (float)th);
		SDL_FRect r = { (float)(cx + 10) + ((float)pw - (float)tw * s) / 2, (float)(cy + y) + ((float)ph - (float)th * s) / 2,
			(float)tw * s, (float)th * s };

		SDL_RenderTexture(g_ren, tx, NULL, &r);
	} else {
		const char *t = T("No picture");

		ui_draw_text(cx + 10 + (pw - ui_text_width(t)) / 2, cy + y + ph / 2 - 6, t, 0x9AA0B0);
	}
	bx = 10 + pw + 14;
	{
		int	bw = SDL_max(text_w(T("Choose Picture..."), 150), text_w(T("Choose Screenshot..."), 150));

		if(nbutton(win, cx, cy, bx, y, bw, 24, "Choose Picture...", 1)) {
			easy_picture_choose();
		}
		if(nbutton(win, cx, cy, bx, y + 30, bw, 24, "Remove Picture", g_pf[8].buf[0] != 0)) {
			g_pf[8].buf[0] = 0;
		}
		if(nbutton(win, cx, cy, bx, y + 60, bw, 24, "Choose Screenshot...", 1)) {
			easy_screen_choose();
		}
		ui_draw_text(cx + bx, cy + y + 96, T("or drop a picture"), UI_GREY);
		ui_draw_text(cx + bx, cy + y + 114, T("or a disk here"), UI_GREY);
	}
	y += ph + 14;
	ui_hline(cx + 10, cy + y, win->w - 22, UI_LGREY);
	y += 10;
	easy_disk_row(win, cx, cy, y, lc + 10, "Main disk", 1, 0);
	y += 30;
	easy_disk_row(win, cx, cy, y, lc + 10, "Floppy disk", fl, 0);
	y += 36;
	ui_hline(cx + 10, cy + y, win->w - 22, UI_LGREY);
	y += 10;
	{
		int	x2 = 10 + SDL_max(ui_text_width(T("Sound card")), ui_text_width(T("Fast (16 MHz)"))) + 60;

		was = g_easy_sound;
		ncheck(win, cx, cy, 10, y, "Sound card", &g_easy_sound);
		if(was != g_easy_sound) {
			easy_sound_set();
		}
		was = g_easy_net;
		ncheck(win, cx, cy, x2, y, "Internet", &g_easy_net);
		if(was != g_easy_net) {
			easy_net_set();
		}
		was = g_easy_print;
		ncheck(win, cx, cy, x2 + ui_text_width(T("Internet")) + 60, y, "Printer", &g_easy_print);
		if(was != g_easy_print) {
			easy_print_set();
		}
		y += 26;
		was = g_easy_fast;
		ncheck(win, cx, cy, 10, y, "Fast (16 MHz)", &g_easy_fast);
		if(was != g_easy_fast) {
			easy_fast_set();
		}
		ncheck(win, cx, cy, x2, y, "Full screen", &g_e.full);
		ncheck(win, cx, cy, x2 + ui_text_width(T("Full screen")) + 60, y, "Curved screen", &g_e.crt);
		y += 26;
		ncheck(win, cx, cy, x2, y, "Status bar", &g_e.bar);
	}
	y += 40;
	{
		int	wm = text_w(T("More settings..."), 130), wc = text_w(T("Cancel"), 80), ws = text_w(T("Save"), 80);
		int	xs = win->w - 2 - 10 - ws;

		if(nbutton(win, cx, cy, 10, y, wm, 26, "More settings...", 1)) {
			ui_window_close(win);
			edit_open_full();
			return;
		}
		if(nbutton(win, cx, cy, xs - 8 - wc, y, wc, 26, "Cancel", 1)) {
			focus_clear();
			ui_window_close(win);
		}
		if(nbutton(win, cx, cy, xs, y, ws, 26, "Save", g_e.name[0] != 0)) {
			edit_save();
		}
	}
	(void)i;
}

static void
edit_open(const Profile *src, int is_new)
{
	edit_load(src, is_new);
	if(getenv("LAUNCHER_EDIT") && !strcmp(getenv("LAUNCHER_EDIT"), "full")) {
		edit_open_full();
		return;
	}
	easy_read();
	focus_clear();
	snprintf(g_easy_win.title, sizeof(g_easy_win.title), "%s", is_new ? T("New Game") : T("Edit Game"));
	g_easy_win.w = 560;
	g_easy_win.h = 20 + 10 + 34 + 150 + 14 + 10 + 60 + 36 + 10 + 26 + 26 + 40 + 26 - 6;
	g_easy_win.modal = 1;
	g_easy_win.closable = 1;
	g_easy_win.draw = easy_draw;
	g_easy_win.on_drop = edit_drop;
	ui_window_center(&g_easy_win);
	ui_window_open(&g_easy_win);
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
	snprintf(n, sizeof(n), T("%s copy"), profile_name(&p));
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
	btn[0].text = T("Delete");
	btn[1].buttonID = 2;
	btn[1].text = T("Cancel");
	btn[1].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
	snprintf(msg, sizeof(msg), T("Delete the profile \"%s\"? The disks it uses are not touched."),
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
		if(play(&g_prof[g_sel], g_toast, sizeof(g_toast)) == 0) {
			Profile	*p = &g_prof[g_sel];
			char	v[32];

			snprintf(v, sizeof(v), "%ld", atol(profile_get(p, "plays")) + 1);
			profile_set(p, "plays", v);
			snprintf(v, sizeof(v), "%lld", (long long)time(NULL));
			profile_set(p, "last_played", v);
			if(p->path[0]) {
				profile_save(p, p->path);
			}
			if(g_sort >= 2) {
				resort_keep();
			}
		}
		ui_message("%s", g_toast);
	}
}

/* ---- fonts: each skin has its own (files in the fonts folder) ---- */

static void
skin_switch(int i)
{
	int		ppem = 16;
	const char	*file = skin_font(i, &ppem);
	char		path[1400];
	const char	*dir = getenv("LAUNCHER_FONTS"), *base = SDL_GetBasePath();

	skin_apply(i);
	lang_set_plain(file && !strcmp(file, "amstrad_cpc464.ttf"));	/* that font has no umlauts */
	if(!file) {
		ui_font_load_file(NULL, 16);
		return;
	}
	if(file[0] == '/') {			/* a system font */
		snprintf(path, sizeof(path), "%s", file);
	} else if(dir && dir[0]) {
		snprintf(path, sizeof(path), "%s/%s", dir, file);
	} else {
		snprintf(path, sizeof(path), "%sfonts/%s", base ? base : "", file);
	}
	if(ui_font_load_file(path, ppem) != 0) {
		const char *fb = skin_font_fallback(i, &ppem);

		if(getenv("LAUNCHER_DEBUG")) {
			fprintf(stderr, "font %s did not load; fallback %s\n", path, fb ? fb : "(built-in)");
		}
		if(!fb || ui_font_load_file(fb, ppem) != 0) {
			if(getenv("LAUNCHER_DEBUG")) {
				fprintf(stderr, "fallback did not load either\n");
			}
			ui_font_load_file(NULL, 16);	/* font file missing: the built-in one */
		}
	}
}

/* ---- settings and covers ---- */

/* ---- window size: 1x, 2x, 3x of the 800x640 design, or full screen ---- */

static int
size_fits(int n)
{
	SDL_Rect	r;

	if(n <= 100) {
		return 1;
	}
	if(!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(g_win), &r)) {
		return 1;
	}
	if(g_arcade && g_portrait) {
		return 1;			/* the upright window is made to fit */
	}
	return (g_arcade ? AW : g_bw) * n / 100 <= r.w && (g_arcade ? AH : g_bh) * n / 100 + 30 <= r.h;
}

static void
settings_save(void);

static void
apply_window_mode(void)
{
	if(g_full) {
		SDL_SetWindowFullscreen(g_win, true);
	} else {
		int		up = g_arcade && g_portrait;
		int		w = (g_arcade ? (up ? PW : AW) : LW) * g_wscale / 100;
		int		h = (g_arcade ? (up ? PH : AH) : LH) * g_wscale / 100;
		SDL_Rect	r;

		/* an upright window is taller than most screens: make it fit */
		if(up && SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(g_win), &r) && h > r.h - 40) {
			w = w * (r.h - 40) / h;
			h = r.h - 40;
		}
		SDL_SetWindowFullscreen(g_win, false);
		SDL_SetWindowSize(g_win, w, h);
		SDL_SetWindowPosition(g_win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	}
}

/* The Arcade view follows the shape of the window: taller than wide is the portrait layout
 * (a turned screen in full screen, the Portrait Window, or a window dragged tall). */
static void
follow_window_shape(void)
{
	int	w = 0, h = 0, tall;

	if(!g_arcade || !SDL_GetWindowSizeInPixels(g_win, &w, &h) || w < 1 || h < 1) {
		return;
	}
	tall = h > w;
	if(tall != g_tall) {
		g_tall = tall;
		g_bw = tall ? PW : AW;
		g_bh = tall ? PH : AH;
		SDL_SetRenderLogicalPresentation(g_ren, g_bw, g_bh, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	}
}

static void
set_window_size(int n)
{
	if(!size_fits(n)) {
		ui_message(T("That size does not fit this screen"));
		return;
	}
	g_wscale = n;
	g_full = 0;
	apply_window_mode();
	settings_save();
}

static void
toggle_fullscreen(void)
{
	g_full = !g_full;
	apply_window_mode();
	settings_save();
}

/* The Arcade view (pictures, full window) or the Classic one (skins, windows, menu bar). */
static void
set_view(int arcade)
{
	g_arcade = arcade;
	g_tall = arcade && g_portrait && !g_full;
	g_bw = arcade ? (g_tall ? PW : AW) : LW;
	g_bh = arcade ? (g_tall ? PH : AH) : LH;
	SDL_SetRenderLogicalPresentation(g_ren, g_bw, g_bh, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	g_ui_top_h = arcade ? 0 : UI_BAR_H;
	g_ui_hide_status = arcade;
	skin_switch(arcade ? skin_find("Modern") : g_skin);	/* dialogs over the pictures are dark */
	if(arcade) {
		ui_window_close(&g_lib);
		SDL_StartTextInput(g_win);
	} else if(!g_lib.open) {
		ui_window_open(&g_lib);
	}
	if(g_wscale != 100 && !g_full && !size_fits(g_wscale)) {
		g_wscale = 100;
	}
	apply_window_mode();
	settings_save();
}

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
		} else if(!strncmp(line, "size = ", 7)) {
			g_wscale = atoi(line + 7);
			if(g_wscale < 50 || g_wscale > 400) {
				g_wscale = 100;
			}
		} else if(!strncmp(line, "view = ", 7)) {
			g_arcade = strcmp(line + 7, "classic") != 0;
		} else if(!strncmp(line, "artwork = ", 10)) {
			snprintf(g_art_root, sizeof(g_art_root), "%s", line + 10);
		} else if(!strncmp(line, "fullscreen = ", 13)) {
			g_full = atoi(line + 13) != 0;
		} else if(!strncmp(line, "sort = ", 7)) {
			g_sort = atoi(line + 7);
			if(g_sort < 0 || g_sort > 3) {
				g_sort = 0;
			}
		} else if(!strncmp(line, "favonly = ", 10)) {
			g_favonly = atoi(line + 10) != 0;
		} else if(!strncmp(line, "idle = ", 7)) {
			g_idle = atoi(line + 7);
			if(g_idle < 0 || g_idle > 3600) {
				g_idle = 60;
			}
		} else if(!strncmp(line, "sound = ", 8)) {
			snd_enable(atoi(line + 8) != 0);
		} else if(!strncmp(line, "portrait = ", 11)) {
			g_portrait = atoi(line + 11) != 0;
		} else if(!strncmp(line, "layout = ", 9)) {
			fe_set_rows(atoi(line + 9));
		} else if(!strncmp(line, "gamescreen = ", 13)) {
			g_game_other = !strcmp(line + 13, "other");
		} else if(!strncmp(line, "pictures = ", 11)) {
			fe_set_art(!strcmp(line + 11, "screen"));
		} else if(!strncmp(line, "language = ", 11)) {
			g_lang = !strcmp(line + 11, "en") ? 1 : (!strcmp(line + 11, "de") ? 2 : 0);
		}
	}
	if(f) {
		fclose(f);
	}
	/* the font is applied after ui_init (see window()) */
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
		fprintf(f, "size = %d\nfullscreen = %d\nview = %s\nartwork = %s\nsound = %d\nsort = %d\nfavonly = %d\nidle = %d\n", g_wscale, g_full,
			g_arcade ? "arcade" : "classic", g_art_root, snd_enabled(), g_sort, g_favonly, g_idle);
		fprintf(f, "portrait = %d\nlanguage = %s\n", g_portrait, g_lang == 1 ? "en" : (g_lang == 2 ? "de" : "auto"));
		fprintf(f, "layout = %d\npictures = %s\ngamescreen = %s\n", fe_rows(), fe_art() ? "screen" : "box",
			g_game_other ? "other" : "this");
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

/* The profile's own picture if it has one (png, jpg or bmp), else a generated cover. */
static void
draw_cover(const Profile *p, int x, int y, int w, int h)
{
	char	full[1200];
	const char *v = profile_get(p, "cover");

	profile_expand_path(v, full, sizeof(full));
	if(v[0]) {
		if(strcmp(full, g_cover_path) != 0) {
			SDL_Surface *sf = fe_load_picture(full);

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
	static const char *names[640];
	static const char *keys[][2] = { { "rom", "ROM" }, { "ramsize", "Memory" },
		{ "mhz", "Speed" }, { "sl1", "Slot 1" }, { "sl2", "Slot 2" }, { "sl3", "Slot 3" },
		{ "sl4", "Slot 4" }, { "sl7", "Slot 7" }, { "hard1", "Hard disk 1" },
		{ "hard2", "Hard disk 2" }, { "hard3", "Hard disk 3" }, { "hard4", "Hard disk 4" },
		{ "hard5", "Hard disk 5" }, { "hard6", "Hard disk 6" }, { "hard7", "Hard disk 7" },
		{ "cdrom", "CD-ROM" }, { "flop3", "3.5\" drive 1" }, { "flop4", "3.5\" drive 2" },
		{ "flop1", "5.25\" drive 1" }, { "flop2", "5.25\" drive 2" }, { "share", "Shared folder" }, { "scale", "Window size" },
		{ NULL, NULL } };
	int	i, y = 10, r;

	if(skin_layout(g_skin) == SKIN_LAYOUT_SHELF) {
		draw_shelf(win, cx, cy);
	} else if(skin_layout(g_skin) == SKIN_LAYOUT_FLOW) {
		draw_flow(win, cx, cy);
	} else {
	for(i = 0; i < g_nprof && i < 640; i++) {
		names[i] = profile_name(&g_prof[i]);
	}
	r = nlist(win, cx, cy, 10, 10, 240, 470, names, g_nprof, &g_sel, &g_top);
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
		int vx = 380;

		for(i = 0; keys[i][0]; i++) {	/* the value column follows the widest label */
			int lw = 270 + ui_text_width(T(keys[i][1])) + 14;

			if(lw > vx) {
				vx = lw;
			}
		}
		for(i = 0; keys[i][0]; i++) {
			const char *v = profile_get(p, keys[i][0]);

			if(v[0]) {
				const char *t = v;

				while(*t && ui_text_width(t) > 740 - vx) {
					t++;
				}
				int	is_path = !strcmp(keys[i][0], "rom") || !strncmp(keys[i][0], "hard", 4) ||
						!strncmp(keys[i][0], "flop", 4) || !strcmp(keys[i][0], "share") ||
						!strcmp(keys[i][0], "cdrom");
				int	missing = 0;

				if(is_path) {
					char	full[1200];
					SDL_PathInfo	pi;

					profile_expand_path(v, full, sizeof(full));
					missing = !SDL_GetPathInfo(full, &pi);
				}
				ui_draw_text(cx + 270, cy + y, T(keys[i][1]), UI_GREY);
				ui_draw_text(cx + vx, cy + y, t, missing ? UI_BAD : UI_BLACK);
				if(missing) {
					ui_draw_text(cx + vx + ui_text_width(t) + 8, cy + y, T("(missing)"), UI_BAD);
				}
				y += 18;
			}
		}
		y += 10;
		ui_draw_text(cx + 270, cy + y, T("Command line"), UI_GREY);
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
	{
		/* six buttons in one row; with a wide skin font or long words they get less padding */
		static const char *lab[6] = { "New", "Duplicate", "Delete", "Library...", "Edit...", "Play" };
		static const int minw[6] = { 70, 80, 78, 100, 100, 100 };
		int	w[6], i, pad, total, x = 10, hit = -1, room = win->w - 2 - 20;

		for(pad = 20; pad >= 6; pad -= 7) {
			total = 4 * 6 + 20;				/* the gaps */
			for(i = 0; i < 6; i++) {
				w[i] = ui_text_width(T(lab[i])) + pad;
				if(pad == 20 && w[i] < minw[i]) {
					w[i] = minw[i];
				}
				total += w[i];
			}
			if(total <= room) {
				break;
			}
		}
		for(i = 0; i < 6; i++) {
			if(i == 4) {					/* Edit and Play sit at the right edge */
				x = SDL_max(x, 10 + room - w[4] - 6 - w[5]);
			}
			if(nbutton(win, cx, cy, x, 490, w[i], 28, lab[i], i == 0 || i == 3 || g_nprof > 0)) {
				hit = i;
			}
			x += w[i] + (i == 3 ? 20 : 6);
		}
		switch(hit) {
		case 0: new_profile(); break;
		case 1: duplicate_profile(); break;
		case 2: delete_profile(); break;
		case 3: lib_open(LIB_DISK, NULL, 0); break;
		case 4: edit_open(&g_prof[g_sel], 0); break;
		case 5: lib_play(); break;
		}
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
	ui_message(g_nlfold == 1 ? T("Library: %d files in 1 folder") : T("Library: %d files in %d folders"), g_library.n, g_nlfold);
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
		"hard6", "hard7", "cdrom", "flop1", "flop2", "flop3", "flop4", NULL };
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
		ui_message(T("No free drive in %s for this disk"), profile_name(&p));
		return;
	}
	SDL_zero(btn);
	btn[0].buttonID = 1;
	btn[0].text = T("Make Copy");
	btn[1].buttonID = 2;
	btn[1].text = T("Cancel");
	btn[1].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
	snprintf(msg, sizeof(msg), T("Make a copy of \"%s\" (%ld MB) for the profile \"%s\" and use it as %s?\nThe original stays as it is."), it->name, it->size / (1024 * 1024), profile_name(&p), key);
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
		ui_message(T("Copy failed: %s"), SDL_GetError());
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
		ui_message(T("%s now uses its own copy as %s"), profile_name(&p), key);
	}
}

static char	g_lline[LIB_MAX_ITEMS][160];
static const char *g_lnames[LIB_MAX_ITEMS];
static int	g_lmap[LIB_MAX_ITEMS];

static void
libwin_draw(UiWindow *win, int cx, int cy)
{
	const char *tabs[] = { T("ROMs"), T("Disks") };
	int	i, n = 0, tab = g_lpick ? g_ltab : g_ltab;
	char	used[300], line[400];

	if(!g_lib_scanned) {
		lib_rescan();
	}
	if(!g_lpick) {
		nsegment(win, cx, cy, 10, 8, 200, 22, tabs, 2, &g_ltab, 3);
	} else {
		ui_draw_text(cx + 10, cy + 10, tab == LIB_ROM ? T("Choose a ROM") : T("Choose a disk"), UI_BLACK);
	}
	ui_draw_text(cx + 226, cy + 10, T("Search"), UI_BLACK);
	field(win, cx, cy, 290, 9, 240, g_lsearch, sizeof(g_lsearch));
	if(nbutton(win, cx, cy, 540, 8, 120, 22, "Check files", 1)) {
		lib_check(&g_library);
		ui_message(T("Checked %d files"), g_library.n);
	}
	for(i = 0; i < g_library.n; i++) {
		const LibItem *it = &g_library.items[i];

		if(it->kind != tab || !lib_matches(it, g_lsearch)) {
			continue;
		}
		used_by(it->path, used, sizeof(used));
		snprintf(g_lline[n], sizeof(g_lline[0]), "%.40s\t%s\t%s\t%.50s%s", it->name,
				it->label, it->volume, used[0] ? used : T("(not used)"),
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
		ui_draw_text(cx + 14, cy + 44, g_nlfold ? T("Nothing found in these folders.") :
			T("Add the folders that hold your ROMs and disks."), UI_GREY);
	}
	{
		static const int cols[] = { 4, 300, 440, 560 };

		ui_list_columns(cols, 4);
	}
	i = nlist(win, cx, cy, 10, 36, 740, 246, g_lnames, n, &g_lsel, &g_ltop);
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
			it->volume[0] ? T(", volume ") : "", it->volume, it->writable ? "" : T(", write-protected"));
		ui_draw_text(cx + 12, cy + 308, line, UI_GREY);
		snprintf(line, sizeof(line), T("Used by: %s"), used[0] ? used : T("no profile"));
		ui_draw_text(cx + 12, cy + 326, line, used[0] ? UI_BLACK : UI_WARN);
		if(it->flags) {
			snprintf(line, sizeof(line), T("Check: %s"), it->note);
			ui_draw_text(cx + 300, cy + 326, line, UI_BAD);
		}
		if(g_ltag_for != idx) {
			g_ltag_for = idx;
			snprintf(g_ltag, sizeof(g_ltag), "%s", it->tags);
		}
		ui_draw_text(cx + 12, cy + 348, T("Tags"), UI_BLACK);
		{
			int	tx = SDL_max(60, 12 + ui_text_width(T("Tags")) + 10);

			field(win, cx, cy, tx, 346, 300, g_ltag, sizeof(g_ltag));	/* saved below when it differs */
		}
		if(strcmp(g_ltag, it->tags) != 0 && g_focus != g_ltag) {
			char	tp[1300];

			tags_path(tp, sizeof(tp));
			lib_tags_set(&g_library, idx, g_ltag, tp);
		}
		{
			int	bw = text_w(T("Private Copy for Profile..."), 190);

			if(!g_lpick && it->kind == LIB_DISK && nbutton(win, cx, cy, 750 - bw, 344, bw, 22,
					"Private Copy for Profile...", g_nprof > 0)) {
				private_copy(it);
			}
		}
	}
	ui_hline(cx + 10, cy + 374, 740, UI_LGREY);
	ui_draw_text(cx + 12, cy + 380, T("Library folders"), UI_GREY);
	{
		static const char *fn[16];

		for(i = 0; i < g_nlfold; i++) {
			fn[i] = g_lfold[i];
		}
		nlist(win, cx, cy, 10, 400, 560, 76, fn, g_nlfold, &g_lfsel, &g_lftop);
	}
	if(nbutton(win, cx, cy, 580, 400, 170, 22, "Add Folder...", g_nlfold < 16)) {
		SDL_ShowOpenFolderDialog(add_cb, NULL, g_win, NULL, false);
	}
	if(nbutton(win, cx, cy, 580, 427, 170, 22, "Remove Folder", g_lfsel >= 0 && g_lfsel < g_nlfold)) {
		for(i = g_lfsel; i + 1 < g_nlfold; i++) {
			memcpy(g_lfold[i], g_lfold[i + 1], sizeof(g_lfold[0]));
		}
		g_nlfold--;
		g_lfsel = -1;
		lfold_save();
		lib_rescan();
	}
	if(nbutton(win, cx, cy, 580, 454, 170, 22, "Rescan", 1)) {
		lib_rescan();
	}
	if(g_lpick) {
		if(nbutton(win, cx, cy, 580, 490, 80, 24, "Cancel", 1)) {
			g_lpick = NULL;
			ui_window_close(win);
		}
		if(nbutton(win, cx, cy, 670, 490, 80, 24, "Use", g_lsel >= 0 && g_lsel < n)) {
			SDL_strlcpy(g_lpick, g_library.items[g_lmap[g_lsel]].path, (size_t)g_lpick_cap);
			g_lpick = NULL;
			ui_window_close(win);
		}
	} else if(nbutton(win, cx, cy, 670, 490, 80, 24, "Close", 1)) {
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
	snprintf(g_libwin.title, sizeof(g_libwin.title), "%s", T("Library"));
	g_libwin.w = 760;
	g_libwin.h = 536;
	g_libwin.modal = target != NULL || g_arcade;	/* modal in the Arcade view: keys and controller work */
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
		"GPL v3. Skin fonts by Kreative Software,",
		"Wesley Clarke and Sizenko Alexander",
		"(see fonts/CREDITS.md).",
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
		int tw = ui_text_width(T(lines[i]));

		ui_draw_text(cx + (cw - tw) / 2, cy + 108 + i * 17, T(lines[i]), i == 0 ? UI_BLACK : (i == 1 ? UI_GREY : UI_BLACK));
	}
	if(nbutton(win, cx, cy, (cw - 80) / 2, win->h - UI_TITLE_H - 38, 80, 24, "OK", 1)) {
		ui_window_close(win);
	}
}

static void
about_open(void)
{
	snprintf(g_about.title, sizeof(g_about.title), "%s", T("About EmuBuddy Launcher"));
	g_about.w = 340;
	g_about.h = 374;
	g_about.closable = 1;
	g_about.modal = 1;
	g_about.draw = about_draw;
	ui_window_center(&g_about);
	ui_window_open(&g_about);
}

/* ---- menus ---- */

enum { C_VIEW = 60, C_INFO, C_ARTDIR, C_IMPORT, C_SETPIC, C_RMPIC, C_SIZE1 = 70, C_SIZE2, C_SIZE3, C_SIZE4, C_SIZE5, C_SIZE6, C_FAV = 85, C_SORT0 = 80, C_SORT1, C_SORT2, C_SORT3, C_FAVONLY = 84, C_SORT = 86, C_SOUND = 78, C_IDLE = 77, C_FULL = 79, C_PORTRAIT = 76, C_LANG = 87, C_LAY0 = 110, C_LAY1, C_LAY2, C_LAY3, C_LAYOUT, C_ARTBOX, C_ARTSCREEN, C_ART, C_SCRTHIS, C_SCROTHER, C_SCR, C_CUT = 90, C_COPY, C_PASTE, C_SELALL, C_SKIN0 = 100, C_NEW = 1, C_DUP, C_DEL, C_FOLDER, C_QUIT, C_PLAY, C_EDIT, C_ABOUT, C_LIB };

static const UiMenuItem m_file[] = {
	{ "New Profile", C_NEW, "Cmd-N", 0 },
	{ "Duplicate Profile", C_DUP, NULL, 0 },
	{ "Delete Profile...", C_DEL, NULL, 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Show Profiles Folder", C_FOLDER, NULL, 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Quit", C_QUIT, "Cmd-Q", 0 },
};
static const UiMenuItem m_edit[] = {
	{ "Cut", C_CUT, "Cmd-X", 0 },
	{ "Copy", C_COPY, "Cmd-C", 0 },
	{ "Paste", C_PASTE, "Cmd-V", 0 },
	{ "Select All", C_SELALL, "Cmd-A", 0 },
};
static const UiMenuItem m_view[] = {
	{ "Window Size 0.8x", C_SIZE1, "Cmd-1", 0 },
	{ "Window Size 1x", C_SIZE2, "Cmd-2", 0 },
	{ "Window Size 1.25x", C_SIZE3, "Cmd-3", 0 },
	{ "Window Size 1.5x", C_SIZE4, "Cmd-4", 0 },
	{ "Window Size 2x", C_SIZE5, "Cmd-5", 0 },
	{ "Window Size 3x", C_SIZE6, "Cmd-6", 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Full Screen", C_FULL, "F11", 0 },
	{ "Portrait Window", C_PORTRAIT, NULL, 0 },
	{ "Sounds", C_SOUND, NULL, 0 },
	{ "Idle Slideshow", C_IDLE, NULL, 0 },
	{ "Deutsch", C_LANG, NULL, 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Sort: A-Z", C_SORT0, NULL, 0 },
	{ "Sort: Favourites First", C_SORT1, NULL, 0 },
	{ "Sort: Last Played", C_SORT2, NULL, 0 },
	{ "Sort: Most Played", C_SORT3, NULL, 0 },
	{ "Favourites Only", C_FAVONLY, NULL, 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Layout: Carousel", C_LAY0, NULL, 0 },
	{ "Layout: 1 Row", C_LAY1, NULL, 0 },
	{ "Layout: 2 Rows", C_LAY2, NULL, 0 },
	{ "Layout: 3 Rows", C_LAY3, NULL, 0 },
	{ "Pictures: Box Art", C_ARTBOX, NULL, 0 },
	{ "Pictures: Screenshots", C_ARTSCREEN, NULL, 0 },
	{ "Games on This Screen", C_SCRTHIS, NULL, 0 },
	{ "Games on the Other Screen", C_SCROTHER, NULL, 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Arcade View", C_VIEW, "Cmd-0", 0 },
};
static const UiMenuItem m_prof[] = {
	{ "Play", C_PLAY, "Return", 0 },
	{ "Edit...", C_EDIT, "Cmd-E", 0 },
	{ "Favourite", C_FAV, "Cmd-D", 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Library...", C_LIB, "Cmd-L", 0 },
};
static const UiMenuItem m_help[] = {
	{ "About EmuBuddy Launcher", C_ABOUT, NULL, 0 },
};
static UiMenuItem m_skin[16];
static const UiMenu g_menus[] = {
	#define NITEMS(a)	((int)(sizeof(a) / sizeof((a)[0])))
	{ "File", m_file, NITEMS(m_file) }, { "Edit", m_edit, NITEMS(m_edit) }, { "Profile", m_prof, NITEMS(m_prof) }, { "View", m_view, NITEMS(m_view) },
	{ "Skin", m_skin, 0 }, { "Help", m_help, NITEMS(m_help) },
};
static UiMenu g_menus_rw[6];
static UiMenuItem g_menu_items[64];	/* the items in the language in use */

/* The menus as the toolkit gets them: texts in the language in use, the Skin menu filled in. */
static void
menus_build(void)
{
	int	k = 0;

	memcpy(g_menus_rw, g_menus, sizeof(g_menus));
	for(int m = 0; m < 6; m++) {
		g_menus_rw[m].title = T(g_menus[m].title);
		if(g_menus[m].items == m_skin) {
			g_menus_rw[m].n_items = skin_count() < 16 ? skin_count() : 16;
			continue;
		}
		g_menus_rw[m].items = &g_menu_items[k];
		for(int i = 0; i < g_menus[m].n_items && k < 64; i++, k++) {
			g_menu_items[k] = g_menus[m].items[i];
			g_menu_items[k].label = T(g_menus[m].items[i].label);
		}
	}
}

/* German when it is chosen, or when the setting is "like the system" and the system is German. */
static void
lang_apply(void)
{
	int	german = g_lang == 2;

	if(g_lang == 0) {
		int		n = 0;
		SDL_Locale	**loc = SDL_GetPreferredLocales(&n);

		german = loc && n > 0 && loc[0]->language && !strcmp(loc[0]->language, "de");
		SDL_free(loc);
	}
	if(getenv("LAUNCHER_LANG")) {			/* test aid: de or en */
		german = !strcmp(getenv("LAUNCHER_LANG"), "de");
	}
	lang_set_german(german);
	menus_build();
	snprintf(g_lib.title, sizeof(g_lib.title), "%s", T("Profiles"));
}

/* ---- pictures of a game: set and remove ---- */

static char		g_picpath[1300];
static SDL_AtomicInt	g_pic_ready;

static void SDLCALL
pic_cb(void *ud, const char * const *list, int filter)
{
	(void)ud; (void)filter;
	if(list && list[0]) {
		SDL_strlcpy(g_picpath, list[0], sizeof(g_picpath));
		SDL_SetAtomicInt(&g_pic_ready, 1);
	}
}

static void
pic_set_choose(void)
{
	static const SDL_DialogFileFilter filters[] = { { "Pictures", "png;jpg;jpeg;bmp" } };

	if(g_nprof) {
		SDL_ShowOpenFileDialog(pic_cb, NULL, g_win, filters, 1, NULL, false);
	}
}

/* where a picture for this game goes: the first picture folder of the game
 * (from an import), else <artwork folder>/snap, else the game's own folder */
static void
pic_target_dir(const Profile *p, char *out, size_t n)
{
	char	first[1200], root[1200];
	const char *t = profile_get(p, "art_tile");

	if(t[0]) {
		const char *semi = strchr(t, ';');

		snprintf(first, sizeof(first), "%.*s", semi ? (int)(semi - t) : (int)strlen(t), t);
		profile_expand_path(first, out, (int)n);
		return;
	}
	profile_expand_path(profile_get(p, "artwork")[0] ? profile_get(p, "artwork") : g_art_root, root, sizeof(root));
	if(root[0]) {
		snprintf(out, n, "%s/snap", root);
		return;
	}
	snprintf(out, n, "%s/%s.data/art", g_dir, profile_name(p));
}

static void
pic_apply(const char *src)
{
	char	dir[1400], dst[1600], parent[1400];
	const char *p = profile_get(&g_prof[g_sel], "art_name")[0] ? profile_get(&g_prof[g_sel], "art_name") :
			profile_name(&g_prof[g_sel]);
	const char *ext = strrchr(src, '.');

	pic_target_dir(&g_prof[g_sel], dir, sizeof(dir));
	snprintf(parent, sizeof(parent), "%s", dir);
	{
		char *sl = strrchr(parent, '/');

		if(sl) {
			*sl = 0;
			SDL_CreateDirectory(parent);
		}
	}
	SDL_CreateDirectory(dir);
	{	/* an older picture of this game with another ending would be found first: remove it */
		static const char *all[] = { ".png", ".jpg", ".jpeg", ".bmp" };

		for(int i = 0; i < 4; i++) {
			snprintf(dst, sizeof(dst), "%s/%s%s", dir, p, all[i]);
			if(strcmp(dst, src) != 0) {
				SDL_RemovePath(dst);
			}
		}
	}
	snprintf(dst, sizeof(dst), "%s/%s%s", dir, p, ext && !SDL_strcasecmp(ext, ".bmp") ? ".bmp" :
			ext && (!SDL_strcasecmp(ext, ".jpg") || !SDL_strcasecmp(ext, ".jpeg")) ? ".jpg" : ".png");
	if(!strcmp(dst, src) || SDL_CopyFile(src, dst)) {
		ui_message(T("Picture set for %s"), profile_name(&g_prof[g_sel]));
	} else {
		ui_message(T("Could not copy the picture: %s"), SDL_GetError());
	}
}

static void
pic_remove(void)
{
	char	art[1400];
	SDL_MessageBoxButtonData btn[2];
	SDL_MessageBoxData box;
	char	msg[1700];
	int	pick = 2;

	if(!g_nprof || !fe_find_art(&g_prof[g_sel], g_art_root, 0, art, sizeof(art))) {
		ui_message(T("This game has no picture"));
		return;
	}
	SDL_zero(btn);
	btn[0].buttonID = 1;
	btn[0].text = T("Delete");
	btn[1].buttonID = 2;
	btn[1].text = T("Cancel");
	btn[1].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
	snprintf(msg, sizeof(msg), T("Delete the picture file of \"%s\"?\n\n%s"), profile_name(&g_prof[g_sel]), art);
	SDL_zero(box);
	box.title = "EmuBuddy Launcher";
	box.message = msg;
	box.numbuttons = 2;
	box.buttons = btn;
	if(SDL_ShowMessageBox(&box, &pick) && pick == 1) {
		if(SDL_RemovePath(art)) {
			ui_message(T("Picture deleted"));
		} else {
			ui_message(T("Could not delete: %s"), SDL_GetError());
		}
	}
}

/* ---- Import from Attract-Mode ---- */

static UiWindow	g_imp;
static char	g_imp_names[ATTRACT_MAX_EMUS][100];
static const char *g_imp_list[ATTRACT_MAX_EMUS];
static int	g_imp_n, g_imp_sel, g_imp_top;
static char	g_imp_dir[1100];

static void
import_run(void)
{
	static Profile	made[64];
	char		err[200], path[1500];
	int		n, saved = 0, existed = 0;

	if(g_imp_sel < 0 || g_imp_sel >= g_imp_n) {
		return;
	}
	n = attract_import(g_imp_dir, g_imp_names[g_imp_sel], made, 64, err, sizeof(err));
	if(n < 0) {
		ui_message("%s", err);
		return;
	}
	for(int i = 0; i < n; i++) {
		SDL_PathInfo info;

		file_name_for(profile_name(&made[i]), path, sizeof(path));
		if(SDL_GetPathInfo(path, &info)) {
			existed++;
			continue;
		}
		if(profile_save(&made[i], path) == 0) {
			saved++;
		}
	}
	load_profiles();
	ui_message(saved == 1 ? T("Imported %d game%s") : T("Imported %d games%s"), saved, existed ? T(" (the others were there already)") : "");
	ui_window_close(&g_imp);
}

static void
imp_draw(UiWindow *win, int cx, int cy)
{
	int	r, x = 10;

	if(!g_imp_n) {
		ui_draw_text(cx + 12, cy + 14, T("No Attract-Mode set-up found."), UI_BAD);
		ui_draw_text(cx + 12, cy + 36, g_imp_dir, UI_GREY);
	} else {
		ui_draw_text(cx + 12, cy + 8, T("Which system do you want to bring in?"), UI_BLACK);
		r = nlist(win, cx, cy, 10, 30, win->w - 22, 220, g_imp_list, g_imp_n, &g_imp_sel, &g_imp_top);
		if(r == 2) {
			import_run();
			return;
		}
	}
	ui_draw_text(cx + 12, cy + 258, T("Your Attract-Mode folder is not changed. Games that exist already are skipped."), UI_GREY);
	if(fit_button(win, cx, cy, &x, win->h - UI_TITLE_H - 34, 90, 26, "Cancel", 1)) {
		ui_window_close(win);
		return;
	}
	if(g_imp_n && nbutton(win, cx, cy, win->w - 112, win->h - UI_TITLE_H - 34, 100, 26, "Import", 1)) {
		import_run();
	}
}

static void
import_open(void)
{
	const char *e = getenv("LAUNCHER_ATTRACT"), *home = getenv("HOME");

	if(e && e[0]) {
		snprintf(g_imp_dir, sizeof(g_imp_dir), "%s", e);
	} else {
		snprintf(g_imp_dir, sizeof(g_imp_dir), "%s/.attract", home ? home : "");
	}
	g_imp_n = attract_list_emulators(g_imp_dir, g_imp_names, ATTRACT_MAX_EMUS);
	for(int i = 0; i < g_imp_n; i++) {
		g_imp_list[i] = g_imp_names[i];
		if(!strcmp(g_imp_names[i], "Apple IIGS")) {
			g_imp_sel = i;
		}
	}
	snprintf(g_imp.title, sizeof(g_imp.title), "%s", T("Import from Attract-Mode"));
	g_imp.w = 520;
	g_imp.h = 340;
	g_imp.modal = 1;
	g_imp.closable = 1;
	g_imp.draw = imp_draw;
	ui_window_center(&g_imp);
	ui_window_open(&g_imp);
}

static void	app_command(int id);

/* artwork folder chooser */
static char		g_artpath[1300];
static SDL_AtomicInt	g_art_ready;

static void SDLCALL
art_cb(void *ud, const char * const *list, int filter)
{
	(void)ud; (void)filter;
	if(list && list[0]) {
		SDL_strlcpy(g_artpath, list[0], sizeof(g_artpath));
		SDL_SetAtomicInt(&g_art_ready, 1);
	}
}

static void
art_dir_choose(void)
{
	SDL_ShowOpenFolderDialog(art_cb, NULL, g_win, NULL, false);
}

/* The menu of the Arcade view (Space): the same commands the menu bar has. */

static struct { const char *label; int id; } g_ovl_items[] = {
	{ "Play", C_PLAY }, { "Details", C_INFO }, { "Favourite", C_FAV }, { "Sort", C_SORT }, { "Layout", C_LAYOUT }, { "Pictures", C_ART }, { "Games on", C_SCR }, { "Edit...", C_EDIT }, { "New Game...", C_NEW },
	{ "Duplicate", C_DUP }, { "Delete...", C_DEL }, { "Disks and ROMs...", C_LIB },
	{ "Set Picture...", C_SETPIC }, { "Remove Picture...", C_RMPIC },
	{ "Artwork Folder...", C_ARTDIR }, { "Import from Attract-Mode...", C_IMPORT },
	{ "Classic View", C_VIEW }, { "Full Screen", C_FULL }, { "Portrait Window", C_PORTRAIT }, { "Sounds on/off", C_SOUND },
	{ "Language", C_LANG }, { "About", C_ABOUT }, { "Quit", C_QUIT },
};

static int	g_ovl_sel;		/* the highlighted item (keys and game controller) */

static int
ovl_enabled(int i)
{
	int id = g_ovl_items[i].id;

	return (id == C_PLAY || id == C_INFO || id == C_FAV || id == C_EDIT || id == C_DUP || id == C_DEL || id == C_SETPIC ||
		id == C_RMPIC) ? g_nprof > 0 : 1;
}

/* Up/Down/Return/Esc in the Space menu; returns 1 if the key was used. */
static int
ovl_key(SDL_Keycode k)
{
	int	n = (int)(sizeof(g_ovl_items) / sizeof(g_ovl_items[0])), i;

	if(!g_ovl.open) {
		return 0;
	}
	if(k == SDLK_DOWN || k == SDLK_UP) {
		int step = k == SDLK_DOWN ? 1 : -1;

		for(i = 1; i <= n; i++) {
			int c = (g_ovl_sel + step * i + n * 2) % n;

			if(ovl_enabled(c)) {
				g_ovl_sel = c;
				snd_play(SND_MOVE);
				break;
			}
		}
		return 1;
	}
	if(k == SDLK_RETURN && ovl_enabled(g_ovl_sel)) {
		int id = g_ovl_items[g_ovl_sel].id;

		ui_window_close(&g_ovl);
		snd_play(SND_SELECT);
		app_command(id);
		return 1;
	}
	if(k == SDLK_ESCAPE || k == SDLK_SPACE) {
		ui_window_close(&g_ovl);
		snd_play(SND_BACK);
		return 1;
	}
	return 1;			/* the menu is modal: it eats the other keys */
}

static void
ovl_draw(UiWindow *win, int cx, int cy)
{
	int	i, y = 8, n = (int)(sizeof(g_ovl_items) / sizeof(g_ovl_items[0]));

	for(i = 0; i < n; i++) {
		int en = ovl_enabled(i);

		if(i == g_ovl_sel) {		/* the highlight */
			ui_frame(cx + 7, cy + y - 3, win->w - 16, 32, UI_BLACK);
			ui_frame(cx + 8, cy + y - 2, win->w - 18, 30, UI_BLACK);
		}
		if(nbutton(win, cx, cy, 10, y, win->w - 22, 26, g_ovl_items[i].label, en)) {
			int id = g_ovl_items[i].id;

			ui_window_close(win);
			app_command(id);
			return;
		}
		y += 30;
	}
}

static void
ovl_open(void)
{
	snprintf(g_ovl.title, sizeof(g_ovl.title), "%s", T("Menu"));
	{
		static char fav[48], sort[48], lang[48], up[48], lay[48], art[48], scrl[64];

		snprintf(fav, sizeof(fav), "%s", g_nprof && is_fav(&g_prof[g_sel]) ? T("Remove from Favourites") : T("Add to Favourites"));
		snprintf(sort, sizeof(sort), T("Sort: %s"), T(g_sort_names[g_sort]));
		for(int i = 0; i < (int)(sizeof(g_ovl_items) / sizeof(g_ovl_items[0])); i++) {
			if(g_ovl_items[i].id == C_FAV) {
				g_ovl_items[i].label = fav;
			} else if(g_ovl_items[i].id == C_SORT) {
				g_ovl_items[i].label = sort;
			} else if(g_ovl_items[i].id == C_LAYOUT) {
				snprintf(lay, sizeof(lay), T("Layout: %s"), T(g_layout_names[fe_rows()]));
				g_ovl_items[i].label = lay;
			} else if(g_ovl_items[i].id == C_SCR) {
				snprintf(scrl, sizeof(scrl), "%s", T(g_game_other ? "Games on the Other Screen" : "Games on This Screen"));
				g_ovl_items[i].label = scrl;
			} else if(g_ovl_items[i].id == C_ART) {
				snprintf(art, sizeof(art), T("Pictures: %s"), T(fe_art() ? "Screenshots" : "Box Art"));
				g_ovl_items[i].label = art;
			} else if(g_ovl_items[i].id == C_LANG) {	/* says what a click changes to */
				snprintf(lang, sizeof(lang), "%s", lang_german() ? "English" : "Deutsch");
				g_ovl_items[i].label = lang;
			} else if(g_ovl_items[i].id == C_PORTRAIT) {
				snprintf(up, sizeof(up), "%s", g_portrait ? T("Landscape Window") : T("Portrait Window"));
				g_ovl_items[i].label = up;
			}
		}
	}
	g_ovl.w = 330;
	for(int i = 0; i < (int)(sizeof(g_ovl_items) / sizeof(g_ovl_items[0])); i++) {	/* wide enough for every line */
		g_ovl.w = SDL_max(g_ovl.w, ui_text_width(T(g_ovl_items[i].label)) + 60);
	}
	g_ovl.h = 20 + 8 + 30 * (int)(sizeof(g_ovl_items) / sizeof(g_ovl_items[0])) + 10;
	g_ovl.modal = 1;
	g_ovl.closable = 1;
	g_ovl.draw = ovl_draw;
	g_ovl_sel = 0;
	snd_play(SND_MENU);
	ui_window_center(&g_ovl);
	ui_window_open(&g_ovl);
}

static void
fe_play(void)
{
	snd_play(SND_LAUNCH);
	lib_play();
}

static void
fe_edit(void)
{
	if(g_nprof) {
		edit_open(&g_prof[g_sel], 0);
	}
}

static void
app_command(int id)
{
	if(id >= C_SKIN0 && id < C_SKIN0 + skin_count()) {
		g_skin = id - C_SKIN0;
		skin_switch(g_skin);
		settings_save();
		return;
	}
	switch(id) {
	case C_VIEW: set_view(!g_arcade); break;
	case C_INFO: fe_key(SDLK_TAB, 0); break;
	case C_ARTDIR: art_dir_choose(); break;
	case C_IMPORT: import_open(); break;
	case C_SETPIC: pic_set_choose(); break;
	case C_RMPIC: pic_remove(); break;
	case C_SIZE1: case C_SIZE2: case C_SIZE3: case C_SIZE4: case C_SIZE5: case C_SIZE6:
		set_window_size(g_steps[id - C_SIZE1]);
		break;
	case C_FULL: toggle_fullscreen(); break;
	case C_PORTRAIT:
		g_portrait = !g_portrait;
		g_full = 0;
		if(!g_arcade) {
			set_view(1);			/* the upright window belongs to the Arcade view */
		} else {
			apply_window_mode();
			settings_save();
		}
		break;
	case C_LANG:
		g_lang = lang_german() ? 1 : 2;
		lang_apply();
		settings_save();
		break;
	case C_FAV:
		if(g_nprof) {
			Profile	*p = &g_prof[g_sel];

			profile_set(p, "favorite", is_fav(p) ? "" : "1");
			if(p->path[0]) {
				profile_save(p, p->path);
			}
			snd_play(SND_SELECT);
			if(g_favonly && !is_fav(p)) {
				load_profiles();
				g_sel = SDL_min(g_sel, g_nprof - 1);
			} else if(g_sort == 1) {
				resort_keep();
			}
			snprintf(g_toast, sizeof(g_toast), "%s", is_fav(&g_prof[g_sel]) ? T("Added to Favourites") : T("Removed from Favourites"));
		}
		break;
	case C_SORT0: case C_SORT1: case C_SORT2: case C_SORT3: case C_SORT:
		g_sort = id == C_SORT ? (g_sort + 1) % 4 : id - C_SORT0;
		resort_keep();
		snd_play(SND_SELECT);
		settings_save();
		break;
	case C_LAY0: case C_LAY1: case C_LAY2: case C_LAY3: case C_LAYOUT:
		fe_set_rows(id == C_LAYOUT ? (fe_rows() + 1) % 4 : id - C_LAY0);
		if(id != C_LAYOUT) {
			set_view(1);			/* the layouts belong to the Arcade view */
		}
		snd_play(SND_SELECT);
		settings_save();
		break;
	case C_SCRTHIS: case C_SCROTHER: case C_SCR:
		g_game_other = id == C_SCR ? !g_game_other : id == C_SCROTHER;
		if(g_game_other && !other_screen()) {
			ui_message("%s", T("Only one screen is connected now: games open on it"));
		}
		snd_play(SND_SELECT);
		settings_save();
		break;
	case C_ARTBOX: case C_ARTSCREEN: case C_ART:
		fe_set_art(id == C_ART ? !fe_art() : id == C_ARTSCREEN);
		snd_play(SND_SELECT);
		settings_save();
		break;
	case C_FAVONLY: {
		char	keep[1100];

		snprintf(keep, sizeof(keep), "%s", g_nprof ? g_prof[g_sel].path : "");
		g_favonly = !g_favonly;
		load_profiles();
		for(int i = 0; i < g_nprof; i++) {
			if(!strcmp(g_prof[i].path, keep)) {
				g_sel = i;
			}
		}
		g_sel = SDL_min(g_sel, SDL_max(g_nprof - 1, 0));
		snd_play(SND_SELECT);
		settings_save();
		break;
	}
	case C_SOUND:
		snd_enable(!snd_enabled());
		snd_play(SND_SELECT);
		settings_save();
		break;
	case C_IDLE:
		g_idle = g_idle ? 0 : 60;
		fe_set_idle(g_idle);
		snd_play(SND_SELECT);
		settings_save();
		break;
	case C_CUT: clip_copy(1); break;
	case C_COPY: clip_copy(0); break;
	case C_PASTE: clip_paste(); break;
	case C_SELALL: if(g_focus) { g_caret = (int)strlen(g_focus); } break;
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
	if(id >= C_SIZE1 && id <= C_SIZE6) {
		int n = g_steps[id - C_SIZE1];

		return (g_wscale == n && !g_full ? UI_MI_CHECKED : 0) | (size_fits(n) ? 0 : UI_MI_DISABLED);
	}
	if(id == C_FULL) {
		return g_full ? UI_MI_CHECKED : 0;
	}
	if(id == C_IDLE) {
		return g_idle ? UI_MI_CHECKED : 0;
	}
	if(id == C_PORTRAIT) {
		return g_portrait ? UI_MI_CHECKED : 0;
	}
	if(id >= C_LAY0 && id <= C_LAY3) {
		return fe_rows() == id - C_LAY0 ? UI_MI_CHECKED : 0;
	}
	if(id == C_SCRTHIS || id == C_SCROTHER) {
		return g_game_other == (id == C_SCROTHER) ? UI_MI_CHECKED : 0;
	}
	if(id == C_ARTBOX || id == C_ARTSCREEN) {
		return fe_art() == (id == C_ARTSCREEN) ? UI_MI_CHECKED : 0;
	}
	if(id == C_LANG) {
		return lang_german() ? UI_MI_CHECKED : 0;
	}
	if(id == C_SOUND) {
		return snd_enabled() ? UI_MI_CHECKED : 0;
	}
	if(id >= C_SORT0 && id <= C_SORT3) {
		return g_sort == id - C_SORT0 ? UI_MI_CHECKED : 0;
	}
	if(id == C_FAVONLY) {
		return g_favonly ? UI_MI_CHECKED : 0;
	}
	if(id == C_FAV) {
		return g_nprof && is_fav(&g_prof[g_sel]) ? UI_MI_CHECKED : 0;
	}
	return (id >= C_SKIN0 && id - C_SKIN0 == g_skin) ? UI_MI_CHECKED : 0;
}

static void
app_status(char *l, char *c, char *r, int n)
{
	(void)c;
	snprintf(l, (size_t)n, g_nprof == 1 ? T("%d profile") : T("%d profiles"), g_nprof);
	snprintf(r, (size_t)n, "EmuBuddy Launcher");
}

static void
app_tick(void)
{
	if(SDL_GetAtomicInt(&g_pic_ready)) {
		SDL_SetAtomicInt(&g_pic_ready, 0);
		pic_apply(g_picpath);
	}
	if(SDL_GetAtomicInt(&g_art_ready)) {
		SDL_SetAtomicInt(&g_art_ready, 0);
		snprintf(g_art_root, sizeof(g_art_root), "%s", g_artpath);
		settings_save();
		ui_message(T("Artwork folder: %s"), g_art_root);
	}
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

static UiApp g_app = { g_menus_rw, 6, app_state, app_command, app_status, app_tick,
	NULL, NULL, NULL };

/* ---- game controller: buttons and the stick become the keys of the Arcade view ----
 * D-pad or left stick move, A plays, B goes back, X shows the details, Y or Start opens the menu,
 * the shoulder buttons jump five games. Holding a direction repeats it. */

static SDL_Gamepad	*g_pads[4];
static int		g_pad_dir;		/* 0 none, else the key being held by D-pad or stick */
static Uint64		g_pad_next;		/* when a held direction repeats */
static int		g_stick_x, g_stick_y;	/* -1, 0, 1 from the left stick */

static void
pad_key(SDL_Keycode k)
{
	SDL_Event ke;

	SDL_zero(ke);
	ke.type = SDL_EVENT_KEY_DOWN;
	ke.key.windowID = SDL_GetWindowID(g_win);
	ke.key.key = k;
	ke.key.down = true;
	SDL_PushEvent(&ke);
}

static void
pad_set_dir(SDL_Keycode k)
{
	if(k == g_pad_dir) {
		return;
	}
	g_pad_dir = (int)k;
	if(k) {
		pad_key(k);
		g_pad_next = SDL_GetTicks() + 420;
	}
}

/* the direction the D-pad or the stick is holding right now */
static SDL_Keycode
pad_dir_now(int dpad_l, int dpad_r, int dpad_u, int dpad_d)
{
	if(dpad_l || g_stick_x < 0) {
		return SDLK_LEFT;
	} else if(dpad_r || g_stick_x > 0) {
		return SDLK_RIGHT;
	} else if(dpad_u || g_stick_y < 0) {
		return SDLK_UP;
	} else if(dpad_d || g_stick_y > 0) {
		return SDLK_DOWN;
	}
	return 0;
}

static int	g_dpad[4];			/* left, right, up, down held */

static void
pad_event(const SDL_Event *e)
{
	if(e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
		fe_set_pad_hints(1);
		g_nav_on = 1;				/* dialogs show the focus frame right away */
	}
	switch(e->type) {
	case SDL_EVENT_GAMEPAD_ADDED:
		for(int i = 0; i < 4; i++) {
			if(!g_pads[i]) {
				g_pads[i] = SDL_OpenGamepad(e->gdevice.which);
				break;
			}
		}
		break;
	case SDL_EVENT_GAMEPAD_REMOVED:
		for(int i = 0; i < 4; i++) {
			if(g_pads[i] && SDL_GetGamepadID(g_pads[i]) == e->gdevice.which) {
				SDL_CloseGamepad(g_pads[i]);
				g_pads[i] = NULL;
			}
		}
		break;
	case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
		int v = e->gaxis.value, dir = v > 16000 ? 1 : (v < -16000 ? -1 : 0);

		if(e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
			g_stick_x = dir;
		} else if(e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
			g_stick_y = dir;
		} else {
			break;
		}
		pad_set_dir(pad_dir_now(g_dpad[0], g_dpad[1], g_dpad[2], g_dpad[3]));
		break; }
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
		int down = e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;

		switch(e->gbutton.button) {
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT: g_dpad[0] = down; break;
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: g_dpad[1] = down; break;
		case SDL_GAMEPAD_BUTTON_DPAD_UP: g_dpad[2] = down; break;
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN: g_dpad[3] = down; break;
		case SDL_GAMEPAD_BUTTON_SOUTH: if(down) pad_key(SDLK_RETURN); break;
		case SDL_GAMEPAD_BUTTON_EAST: if(down) pad_key(SDLK_ESCAPE); break;
		case SDL_GAMEPAD_BUTTON_WEST: if(down) pad_key(SDLK_TAB); break;
		case SDL_GAMEPAD_BUTTON_NORTH:
		case SDL_GAMEPAD_BUTTON_START: if(down) pad_key(SDLK_SPACE); break;
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: if(down) pad_key(SDLK_UP); break;
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: if(down) pad_key(SDLK_DOWN); break;
		default: return;
		}
		pad_set_dir(pad_dir_now(g_dpad[0], g_dpad[1], g_dpad[2], g_dpad[3]));
		break; }
	}
}

/* called once per frame: repeat a held direction */
static void
pad_tick(void)
{
	if(g_pad_dir && SDL_GetTicks() >= g_pad_next) {
		pad_key((SDL_Keycode)g_pad_dir);
		g_pad_next = SDL_GetTicks() + 110;
	}
}

static void
window(void)
{
	if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
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
	load_profiles();		/* again: sort order and favourites filter come from the settings */
	for(int i = 0; i < skin_count() && i < 16; i++) {
		m_skin[i].label = skin_get(i)->name;
		m_skin[i].id = C_SKIN0 + i;
	}
	lang_apply();
	{
		SDL_Surface *ic = SDL_CreateSurfaceFrom(launcher_icon_size, launcher_icon_size,
				SDL_PIXELFORMAT_RGBA32, (void *)launcher_icon_rgba, launcher_icon_size * 4);

		if(ic) {
			SDL_SetWindowIcon(g_win, ic);
			SDL_DestroySurface(ic);
		}
	}
	ui_init(g_win, g_ren, &g_app);
	snd_init();
	skin_switch(g_skin);
	g_lib.x = 10;
	g_lib.y = UI_BAR_H + 10;
	g_lib.w = 780;
	g_lib.h = 540;
	g_lib.draw = lib_draw;
	{
		FeData fd = { g_prof, &g_nprof, &g_sel, g_art_root, fe_play, ovl_open, fe_edit, g_dir };
		const char *fdir = getenv("LAUNCHER_FONTS"), *base = SDL_GetBasePath();
		char	fpath[1400];

		snprintf(fpath, sizeof(fpath), "%s%s", fdir && fdir[0] ? fdir : (base ? base : ""), fdir && fdir[0] ? "" : "fonts");
		fe_set_data(&fd);
		if(fe_init(g_ren, fpath) != 0) {
			fprintf(stderr, "front end fonts not found in %s: using the Classic view\n", fpath);
			g_arcade = 0;
		}
	}
	if(g_wscale != 100 && !g_full && !size_fits(g_wscale)) {
		g_wscale = 100;
	}
	fe_set_idle(getenv("LAUNCHER_IDLE") ? atoi(getenv("LAUNCHER_IDLE")) : g_idle);
	set_view(g_arcade);		/* sets the size, the logical size and the Profiles window */
	while(g_running) {
		SDL_Event e;

		pad_tick();
		fe_set_busy(g_ovl.open || ui_is_modal() || g_focus != NULL);
		while(SDL_PollEvent(&e)) {
			if(e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_WHEEL ||
					e.type == SDL_EVENT_TEXT_INPUT || e.type == SDL_EVENT_MOUSE_MOTION ||
					(e.type >= SDL_EVENT_GAMEPAD_AXIS_MOTION && e.type <= SDL_EVENT_GAMEPAD_BUTTON_UP)) {
				fe_activity();
			}
			if(e.type == SDL_EVENT_QUIT) {
				g_running = 0;
				continue;
			}
			if(e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
				fe_refresh();			/* back from EmuBuddy: it may have left a new picture */
			}
			if((e.type == SDL_EVENT_KEY_DOWN && e.key.key != SDLK_UNKNOWN && e.key.scancode != SDL_SCANCODE_UNKNOWN) ||
					e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
				fe_set_pad_hints(0);	/* the keyboard or the mouse was used last */
			}
			if(e.type >= SDL_EVENT_GAMEPAD_AXIS_MOTION && e.type <= SDL_EVENT_GAMEPAD_REMOVED) {
				pad_event(&e);
				continue;
			}
			if(g_ovl.open && e.type == SDL_EVENT_KEY_DOWN && !g_focus && ovl_key(e.key.key)) {
				continue;
			}
			if(g_focus && e.type == SDL_EVENT_TEXT_INPUT) {
				focus_insert(e.text.text);
				continue;
			}
			if(g_focus && e.type == SDL_EVENT_KEY_DOWN) {
				SDL_Keycode k = e.key.key;
				int len = (int)strlen(g_focus), cmd = (e.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0;

				if(g_caret > len) {
					g_caret = len;
				}
				if(k == SDLK_BACKSPACE) {
					int p = utf8_prev(g_focus, g_caret);

					memmove(g_focus + p, g_focus + g_caret, (size_t)(len - g_caret + 1));
					g_caret = p;
					continue;
				} else if(k == SDLK_DELETE) {
					int nx = utf8_next(g_focus, g_caret);

					memmove(g_focus + g_caret, g_focus + nx, (size_t)(len - nx + 1));
					continue;
				} else if(k == SDLK_LEFT) {
					g_caret = cmd ? 0 : utf8_prev(g_focus, g_caret);
					continue;
				} else if(k == SDLK_RIGHT) {
					g_caret = cmd ? len : utf8_next(g_focus, g_caret);
					continue;
				} else if(k == SDLK_HOME) {
					g_caret = 0;
					continue;
				} else if(k == SDLK_END) {
					g_caret = len;
					continue;
				} else if(k == SDLK_RETURN || k == SDLK_TAB || k == SDLK_ESCAPE) {
					focus_clear();
					continue;
				} else if((k == SDLK_UP || k == SDLK_DOWN) && nav_front()) {
					focus_clear();			/* leave the text and move on */
					g_nav_on = 1;
					nav_key(k);
					continue;
				} else if(k == SDLK_V && cmd) {
					clip_paste();
					continue;
				} else if((k == SDLK_C || k == SDLK_X) && cmd) {
					clip_copy(k == SDLK_X);
					continue;
				} else if(k == SDLK_A && cmd) {
					g_caret = len;		/* no selection: go to the end */
					continue;
				}
			}
			if(e.type == SDL_EVENT_KEY_DOWN && !g_focus && !ui_is_modal() &&
					(e.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) {
				SDL_Keycode k = e.key.key;

				if(k >= SDLK_1 && k <= SDLK_6) {
					app_command(C_SIZE1 + (int)(k - SDLK_1));
					continue;
				} else if(k == SDLK_N) {
					app_command(C_NEW);
					continue;
				} else if(k == SDLK_E) {
					app_command(C_EDIT);
					continue;
				} else if(k == SDLK_D) {
					app_command(C_FAV);
					continue;
				} else if(k == SDLK_L) {
					app_command(C_LIB);
					continue;
				} else if(k == SDLK_0) {
					app_command(C_VIEW);
					continue;
				} else if(k == SDLK_Q) {
					g_running = 0;
					continue;
				}
			}
			if(e.type == SDL_EVENT_KEY_DOWN && !g_focus && !(e.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) &&
					nav_key(e.key.key)) {
				continue;
			}
			if(e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F11 && !e.key.repeat) {
				toggle_fullscreen();
				continue;
			}
			if(e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
				float lx, ly;

				SDL_RenderCoordinatesFromWindow(g_ren, e.button.x, e.button.y, &lx, &ly);
				if(!ui_menu_is_open() && ly >= UI_BAR_H) {
					g_pending_unfocus = 1;	/* unless a field takes the click */
				}
				g_nav_on = 0;			/* the mouse is in use: no focus frame */
			}
			if(g_arcade && !g_focus && !ui_is_modal()) {
				if(e.type == SDL_EVENT_KEY_DOWN && !(e.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) &&
						fe_key(e.key.key, e.key.mod)) {
					continue;
				}
				if(e.type == SDL_EVENT_TEXT_INPUT && fe_text(e.text.text)) {
					continue;
				}
				if(e.type == SDL_EVENT_MOUSE_WHEEL) {
					fe_wheel(e.wheel.y);
					continue;
				}
			}
			if(ui_handle_event(&e)) {
				continue;
			}
			if(g_arcade && !ui_is_modal() && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
				float lx, ly;

				SDL_RenderCoordinatesFromWindow(g_ren, e.button.x, e.button.y, &lx, &ly);
				if(fe_click(lx, ly, e.button.clicks)) {
					continue;
				}
			}
			if(!g_arcade && e.type == SDL_EVENT_KEY_DOWN && !g_focus && !ui_is_modal()) {
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
		follow_window_shape();
		if(g_arcade) {
			fe_draw(g_bw, g_bh);
		} else {
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
		}
		nav_begin();
		ui_draw(g_bw, g_bh);
		nav_end();
		if(g_pending_unfocus) {
			g_pending_unfocus = 0;
			focus_clear();
		}
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
				skin_switch(g_skin);
			}
			if(frame == 4 && getenv("LAUNCHER_RESIZE")) {		/* test aid: WxH */
				int rw = 0, rh = 0;

				if(sscanf(getenv("LAUNCHER_RESIZE"), "%dx%d", &rw, &rh) == 2) {
					SDL_SetWindowSize(g_win, rw, rh);
				}
			}
			if(getenv("LAUNCHER_CLICK") && (frame == 38 || frame == 40 || frame == 42)) {	/* test aid: window x,y */
				float wx = 0, wy = 0;

				if(sscanf(getenv("LAUNCHER_CLICK"), "%f,%f", &wx, &wy) == 2) {
					SDL_Event ce;
					int ww, wh;
					float lx, ly;

					SDL_GetWindowSize(g_win, &ww, &wh);
					SDL_RenderCoordinatesFromWindow(g_ren, wx, wy, &lx, &ly);
					if(frame == 38) {
						printf("window %dx%d, click %.0f,%.0f -> logical %.1f,%.1f\n", ww, wh, wx, wy, lx, ly);
					}

					SDL_zero(ce);
					ce.type = frame == 38 ? SDL_EVENT_MOUSE_MOTION : frame == 40 ? SDL_EVENT_MOUSE_BUTTON_DOWN :
							SDL_EVENT_MOUSE_BUTTON_UP;
					ce.button.windowID = SDL_GetWindowID(g_win);
					ce.button.button = SDL_BUTTON_LEFT;
					ce.button.down = frame == 40;
					ce.button.clicks = 1;
					ce.button.x = wx;
					ce.button.y = wy;
					SDL_PushEvent(&ce);
				}
			}
			if(getenv("LAUNCHER_FIELDTEST") && frame >= 10 && frame <= 20) {	/* test aid: click, Left, type X, Delete */
				SDL_Event ce;

				SDL_zero(ce);
				ce.button.windowID = SDL_GetWindowID(g_win);
				if(frame == 10 || frame == 11) {
					ce.type = frame == 10 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
					ce.button.button = SDL_BUTTON_LEFT;
					ce.button.down = frame == 10;
					ce.button.clicks = 1;
					ce.button.x = 161;
					ce.button.y = 60;
					SDL_PushEvent(&ce);
				} else if(frame == 14 && getenv("LAUNCHER_PASTE")) {
					SDL_SetClipboardText(getenv("LAUNCHER_PASTE"));
					ce.type = SDL_EVENT_KEY_DOWN;
					ce.key.key = SDLK_V;
					ce.key.mod = SDL_KMOD_GUI;
					ce.key.down = true;
					SDL_PushEvent(&ce);
				} else if((frame == 14 || frame == 18) && !getenv("LAUNCHER_PASTE")) {
					ce.type = SDL_EVENT_KEY_DOWN;
					ce.key.key = frame == 14 ? SDLK_LEFT : SDLK_DELETE;
					ce.key.down = true;
					SDL_PushEvent(&ce);
				} else if(frame == 16) {
					ce.type = SDL_EVENT_TEXT_INPUT;
					ce.text.text = "X";
					SDL_PushEvent(&ce);
				}
			}
			if(getenv("LAUNCHER_PADSEQ") && frame >= 10 && frame < 10 + 4 * 24 && (frame - 10) % 4 == 0) {
				/* test aid: letters L R U D A B X Y S, one every 4 frames, as controller buttons */
				const char *q = getenv("LAUNCHER_PADSEQ");
				size_t idx = (size_t)((frame - 10) / 4);

				if(idx < strlen(q)) {
					static const struct { char c; int b; } map[] = { { 'L', SDL_GAMEPAD_BUTTON_DPAD_LEFT },
						{ 'R', SDL_GAMEPAD_BUTTON_DPAD_RIGHT }, { 'U', SDL_GAMEPAD_BUTTON_DPAD_UP },
						{ 'D', SDL_GAMEPAD_BUTTON_DPAD_DOWN }, { 'A', SDL_GAMEPAD_BUTTON_SOUTH },
						{ 'B', SDL_GAMEPAD_BUTTON_EAST }, { 'X', SDL_GAMEPAD_BUTTON_WEST },
						{ 'Y', SDL_GAMEPAD_BUTTON_NORTH }, { 'S', SDL_GAMEPAD_BUTTON_START } };

					for(size_t m = 0; m < sizeof(map) / sizeof(map[0]); m++) {
						if(map[m].c == q[idx]) {
							SDL_Event pe;

							SDL_zero(pe);
							pe.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
							pe.gbutton.button = (Uint8)map[m].b;
							pe.gbutton.down = true;
							SDL_PushEvent(&pe);
							pe.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
							pe.gbutton.down = false;
							SDL_PushEvent(&pe);
						}
					}
				}
			}
			if(frame == 3 && getenv("LAUNCHER_SEL") && g_nprof > 0) {		/* test aid: select a game */
				g_sel = atoi(getenv("LAUNCHER_SEL")) % g_nprof;
			}
			if(frame == 25 && getenv("LAUNCHER_INFO")) {
				fe_key(SDLK_TAB, 0);
			}
			if(frame == 25 && getenv("LAUNCHER_IMPORT")) {
				import_open();
			}
			if(frame == 40 && getenv("LAUNCHER_IMPORT") && !strcmp(getenv("LAUNCHER_IMPORT"), "run")) {
				import_run();
			}
			if(frame == 25 && getenv("LAUNCHER_OVL")) {
				ovl_open();
			}
			if(frame == 5 && getenv("LAUNCHER_ABOUT")) {
				about_open();
			}
			if(frame == (getenv("LAUNCHER_FIELDTEST") ? 30 : 90) && getenv("LAUNCHER_SHOT")) {
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
	snd_shutdown();
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
