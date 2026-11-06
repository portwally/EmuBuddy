/* EmuBuddy app layer: menus, commands, status bar, About window and the
 * in-skin file browser. Built on the toolkit in ui.c. */
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* core state (sim65816.c, config.c) */
extern int	g_limit_speed;		/* 0 unlimited, 1 1MHz, 2 2.8MHz, 3 ZipGS */
extern int	g_zip_speed_mhz;
extern double	g_eff_mhz;
extern int	g_fullscreen;
extern int	g_crt;
extern int	g_scanline_simulator;
extern int	g_ui_pause;
extern int	g_rom_version;
void		do_reset(void);
void		cfg_file_update_rom(const char *str);
void		cfg_set_config_panel(int panel);
extern int	g_config_control_panel;
void		cfg_inspect_maybe_insert_file(const char *filename);
/* sdl_driver.c */
void		sdl_request_screenshot(void);

#define EMUBUDDY_VERSION	"0.1"

enum {
	CMD_OPEN_DISK = 1, CMD_QUIT, CMD_CHOOSE_ROM,
	CMD_RESET = 10, CMD_PAUSE,
	CMD_SPEED_1 = 20, CMD_SPEED_28, CMD_ZIP_8, CMD_ZIP_16, CMD_ZIP_32,
	CMD_ZIP_64, CMD_ZIP_128, CMD_UNLIMITED,
	CMD_FULLSCREEN = 30, CMD_CRT, CMD_SCANLINES, CMD_SCREENSHOT,
	CMD_ABOUT = 40,
};

static const UiMenuItem file_items[] = {
	{ "Open Disk Image...", CMD_OPEN_DISK, NULL, 0 },
	{ "Choose ROM File...", CMD_CHOOSE_ROM, NULL, 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Quit EmuBuddy", CMD_QUIT, NULL, 0 },
};
static const UiMenuItem machine_items[] = {
	{ "Reset", CMD_RESET, NULL, 0 },
	{ "Pause", CMD_PAUSE, NULL, 0 },
};
static const UiMenuItem speed_items[] = {
	{ "1.0 MHz", CMD_SPEED_1, NULL, 0 },
	{ "2.8 MHz (stock IIgs)", CMD_SPEED_28, NULL, 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "ZipGS 8 MHz", CMD_ZIP_8, NULL, 0 },
	{ "ZipGS 16 MHz", CMD_ZIP_16, NULL, 0 },
	{ "ZipGS 32 MHz", CMD_ZIP_32, NULL, 0 },
	{ "ZipGS 64 MHz", CMD_ZIP_64, NULL, 0 },
	{ "ZipGS 128 MHz", CMD_ZIP_128, NULL, 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Unlimited (maximum)", CMD_UNLIMITED, NULL, 0 },
};
static const UiMenuItem view_items[] = {
	{ "Full Screen", CMD_FULLSCREEN, "F11", 0 },
	{ "CRT Effect", CMD_CRT, "Ctrl-F11", 0 },
	{ "Scanlines", CMD_SCANLINES, "Shift-F11", 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Save Screenshot", CMD_SCREENSHOT, "F10", 0 },
};
static const UiMenuItem help_items[] = {
	{ "About EmuBuddy", CMD_ABOUT, NULL, 0 },
};

#define N(a)	((int)(sizeof(a) / sizeof((a)[0])))
static const UiMenu g_menus[] = {
	{ "File", file_items, N(file_items) },
	{ "Machine", machine_items, N(machine_items) },
	{ "Speed", speed_items, N(speed_items) },
	{ "View", view_items, N(view_items) },
	{ "Help", help_items, N(help_items) },
};

/* ------------------------------------------------------------------ */
/* file browser */

#define FB_MAX		2048
static UiWindow	g_fb_win;
static char	g_fb_dir[1024];
static char	*g_fb_names[FB_MAX];		/* display names ("name/" for dirs) */
static int	g_fb_isdir[FB_MAX];
static int	g_fb_n, g_fb_sel = -1, g_fb_top;
static void	(*g_fb_pick)(const char *path);
static char	g_fb_last_dir[1024];

static int
fb_cmp(const void *a, const void *b)
{
	int ia = *(const int *)a, ib = *(const int *)b;

	if(g_fb_isdir[ia] != g_fb_isdir[ib]) {
		return g_fb_isdir[ib] - g_fb_isdir[ia];
	}
	return strcasecmp(g_fb_names[ia], g_fb_names[ib]);
}

static SDL_EnumerationResult SDLCALL
fb_enum(void *ud, const char *dirname, const char *fname)
{
	char		full[1536];
	SDL_PathInfo	info;
	(void)ud;

	if(fname[0] == '.' || g_fb_n >= FB_MAX) {
		return SDL_ENUM_CONTINUE;
	}
	snprintf(full, sizeof(full), "%s%s", dirname, fname);
	if(!SDL_GetPathInfo(full, &info)) {
		return SDL_ENUM_CONTINUE;
	}
	g_fb_isdir[g_fb_n] = (info.type == SDL_PATHTYPE_DIRECTORY);
	if(g_fb_isdir[g_fb_n]) {
		size_t n = strlen(fname) + 2;
		g_fb_names[g_fb_n] = malloc(n);
		snprintf(g_fb_names[g_fb_n], n, "%s/", fname);
	} else {
		g_fb_names[g_fb_n] = strdup(fname);
	}
	g_fb_n++;
	return SDL_ENUM_CONTINUE;
}

static void
fb_clear(void)
{
	int	i;

	for(i = 0; i < g_fb_n; i++) {
		free(g_fb_names[i]);
	}
	g_fb_n = 0;
}

static void
fb_read_dir(const char *dir)
{
	int	order[FB_MAX], i, k;
	char	*names[FB_MAX];
	int	isdir[FB_MAX];
	size_t	len;

	fb_clear();
	snprintf(g_fb_dir, sizeof(g_fb_dir), "%s", dir);
	len = strlen(g_fb_dir);
	if(len == 0 || g_fb_dir[len - 1] != '/') {
		if(len + 1 < sizeof(g_fb_dir)) {
			strcat(g_fb_dir, "/");
		}
	}
	SDL_EnumerateDirectory(g_fb_dir, fb_enum, NULL);
	for(i = 0; i < g_fb_n; i++) {
		order[i] = i;
	}
	qsort(order, (size_t)g_fb_n, sizeof(int), fb_cmp);
	for(i = 0; i < g_fb_n; i++) {
		k = order[i];
		names[i] = g_fb_names[k];
		isdir[i] = g_fb_isdir[k];
	}
	for(i = 0; i < g_fb_n; i++) {
		g_fb_names[i] = names[i];
		g_fb_isdir[i] = isdir[i];
	}
	g_fb_sel = -1;
	g_fb_top = 0;
}

static void
fb_up(void)
{
	char	*p;
	size_t	len = strlen(g_fb_dir);

	if(len > 1 && g_fb_dir[len - 1] == '/') {
		g_fb_dir[len - 1] = 0;
	}
	p = strrchr(g_fb_dir, '/');
	if(p) {
		p[1] = 0;
	}
	if(g_fb_dir[0] == 0) {
		strcpy(g_fb_dir, "/");
	}
	fb_read_dir(g_fb_dir);
}

static void
fb_accept(void)
{
	char	full[1536], name[1024];
	size_t	n;

	if(g_fb_sel < 0 || g_fb_sel >= g_fb_n) {
		return;
	}
	snprintf(name, sizeof(name), "%s", g_fb_names[g_fb_sel]);
	if(g_fb_isdir[g_fb_sel]) {
		n = strlen(name);
		if(n && name[n - 1] == '/') {
			name[n - 1] = 0;
		}
		snprintf(full, sizeof(full), "%s%s", g_fb_dir, name);
		fb_read_dir(full);
		return;
	}
	snprintf(full, sizeof(full), "%s%s", g_fb_dir, name);
	snprintf(g_fb_last_dir, sizeof(g_fb_last_dir), "%s", g_fb_dir);
	ui_window_close(&g_fb_win);
	if(g_fb_pick) {
		g_fb_pick(full);
	}
}

static void
fb_draw(UiWindow *win, int cx, int cy)
{
	int	r;
	char	shown[200];
	int	w = win->w - 2;

	/* path line, trimmed from the left if too long */
	snprintf(shown, sizeof(shown), "%s", g_fb_dir);
	while(ui_text_width(shown) > w - 16 && strlen(shown) > 4) {
		memmove(shown + 3, shown + 4, strlen(shown + 4) + 1);
		memcpy(shown, "...", 3);
	}
	ui_draw_text(cx + 8, cy + 6, shown, UI_BLACK);
	r = ui_list(win, cx, cy, 8, 28, w - 16, win->h - UI_TITLE_H - 28 - 44,
			(const char **)g_fb_names, g_fb_n, &g_fb_sel, &g_fb_top);
	if(r == 2) {
		fb_accept();
		return;
	}
	{
		int by = win->h - UI_TITLE_H - 34;

		if(ui_button(win, cx, cy, 8, by, 70, 24, "Up", 1)) {
			fb_up();
		}
		if(ui_button(win, cx, cy, 86, by, 70, 24, "Home", 1)) {
			const char *h = SDL_GetUserFolder(SDL_FOLDER_HOME);

			fb_read_dir(h ? h : "/");
		}
		if(ui_button(win, cx, cy, w - 8 - 70 - 8 - 70, by, 70, 24,
								"Cancel", 1)) {
			ui_window_close(win);
		}
		if(ui_button(win, cx, cy, w - 8 - 70, by, 70, 24, "Open",
				g_fb_sel >= 0)) {
			fb_accept();
		}
	}
}

static void
file_browser_open(const char *title, void (*pick)(const char *))
{
	const char	*h;

	snprintf(g_fb_win.title, sizeof(g_fb_win.title), "%s", title);
	g_fb_win.w = 440;
	g_fb_win.h = 330;
	g_fb_win.closable = 1;
	g_fb_win.modal = 1;
	g_fb_win.draw = fb_draw;
	g_fb_pick = pick;
	if(g_fb_last_dir[0]) {
		h = g_fb_last_dir;
	} else {
		h = SDL_GetUserFolder(SDL_FOLDER_HOME);
	}
	fb_read_dir(h ? h : "/");
	ui_window_center(&g_fb_win);
	ui_window_open(&g_fb_win);
}

/* ------------------------------------------------------------------ */
/* About window */

static UiWindow g_about_win;

static void
about_draw(UiWindow *win, int cx, int cy)
{
	static const char *lines[] = {
		"EmuBuddy " EMUBUDDY_VERSION,
		"Apple IIgs emulator",
		"",
		"Based on KEGS by Kent Dickey",
		"and GSplus by Dagen Brock.",
		"GPL v3.",
		"",
		"UI font: Shaston 640 by",
		"Kreative Software.",
	};
	int	i;

	for(i = 0; i < N(lines); i++) {
		int tw = ui_text_width(lines[i]);

		ui_draw_text(cx + (win->w - 2 - tw) / 2, cy + 10 + i * 17,
						lines[i], UI_BLACK);
	}
	if(ui_button(win, cx, cy, (win->w - 2 - 80) / 2, win->h - UI_TITLE_H - 36,
							80, 24, "OK", 1)) {
		ui_window_close(win);
	}
}

static void
about_open(void)
{
	snprintf(g_about_win.title, sizeof(g_about_win.title), "About EmuBuddy");
	g_about_win.w = 300;
	g_about_win.h = 250;
	g_about_win.closable = 1;
	g_about_win.modal = 1;
	g_about_win.draw = about_draw;
	ui_window_center(&g_about_win);
	ui_window_open(&g_about_win);
}

/* ------------------------------------------------------------------ */
/* commands / state / status */

static void
disk_picked(const char *path)
{
	cfg_inspect_maybe_insert_file(path);
}

/* ---- ROM handling: no valid ROM -> "ROM Required" window, picture blanked */

static UiWindow g_rom_win;
static int	g_rom_error;
static int	g_rom_was_missing;

static int
rom_missing(void)
{
	return g_rom_version < 0;
}

static void
rom_picked(const char *path)
{
	cfg_file_update_rom(path);
	g_rom_error = rom_missing();
}

static void
rom_draw(UiWindow *win, int cx, int cy)
{
	static const char *lines[] = {
		"EmuBuddy needs an Apple IIgs ROM file.",
		"ROM 01 is 128 KB, ROM 03 is 256 KB.",
		"Drop the file on this window, or",
		"choose it with the button below.",
	};
	int	i;

	for(i = 0; i < N(lines); i++) {
		ui_draw_text(cx + 16, cy + 14 + i * 18, lines[i], UI_BLACK);
	}
	if(g_rom_error) {
		ui_draw_text(cx + 16, cy + 14 + 4 * 18 + 4,
				"That file is not a valid ROM.", UI_BAD);
	}
	if(ui_button(win, cx, cy, (win->w - 2 - 160) / 2, win->h - UI_TITLE_H - 40,
						160, 26, "Choose ROM File...", 1)) {
		file_browser_open("Choose ROM File", rom_picked);
	}
}

static void
set_limit(int limit, int zip_mhz)
{
	g_limit_speed = limit;
	if(zip_mhz) {
		g_zip_speed_mhz = zip_mhz;
	}
}

static void
app_command(int id)
{
	switch(id) {
	case CMD_OPEN_DISK: file_browser_open("Open Disk Image", disk_picked); break;
	case CMD_CHOOSE_ROM: file_browser_open("Choose ROM File", rom_picked); break;
	case CMD_QUIT: {
		SDL_Event q;

		SDL_zero(q);
		q.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&q);
		break; }
	case CMD_RESET: do_reset(); break;
	case CMD_PAUSE: g_ui_pause = !g_ui_pause; break;
	case CMD_SPEED_1: set_limit(1, 0); break;
	case CMD_SPEED_28: set_limit(2, 0); break;
	case CMD_ZIP_8: set_limit(3, 8); break;
	case CMD_ZIP_16: set_limit(3, 16); break;
	case CMD_ZIP_32: set_limit(3, 32); break;
	case CMD_ZIP_64: set_limit(3, 64); break;
	case CMD_ZIP_128: set_limit(3, 128); break;
	case CMD_UNLIMITED: set_limit(0, 0); break;
	case CMD_FULLSCREEN: g_fullscreen = !g_fullscreen; break;
	case CMD_CRT: g_crt = !g_crt; break;
	case CMD_SCANLINES:
		g_scanline_simulator = g_scanline_simulator ? 0 : 50;
		break;
	case CMD_SCREENSHOT: sdl_request_screenshot(); break;
	case CMD_ABOUT: about_open(); break;
	}
}

static int
app_item_state(int id)
{
	int	zip = (g_limit_speed == 3) ? g_zip_speed_mhz : 0;

	switch(id) {
	case CMD_PAUSE: return g_ui_pause ? UI_MI_CHECKED : 0;
	case CMD_SPEED_1: return g_limit_speed == 1 ? UI_MI_CHECKED : 0;
	case CMD_SPEED_28: return g_limit_speed == 2 ? UI_MI_CHECKED : 0;
	case CMD_ZIP_8: return zip == 8 ? UI_MI_CHECKED : 0;
	case CMD_ZIP_16: return zip == 16 ? UI_MI_CHECKED : 0;
	case CMD_ZIP_32: return zip == 32 ? UI_MI_CHECKED : 0;
	case CMD_ZIP_64: return zip == 64 ? UI_MI_CHECKED : 0;
	case CMD_ZIP_128: return zip == 128 ? UI_MI_CHECKED : 0;
	case CMD_UNLIMITED: return g_limit_speed == 0 ? UI_MI_CHECKED : 0;
	case CMD_FULLSCREEN: return g_fullscreen ? UI_MI_CHECKED : 0;
	case CMD_CRT: return g_crt ? UI_MI_CHECKED : 0;
	case CMD_SCANLINES: return g_scanline_simulator ? UI_MI_CHECKED : 0;
	}
	return 0;
}

static void
app_status(char *left, char *center, char *right, int n)
{
	const char	*limit;
	char		buf[32];

	switch(g_limit_speed) {
	case 1: limit = "1.0 MHz"; break;
	case 2: limit = "2.8 MHz"; break;
	case 3:
		snprintf(buf, sizeof(buf), "ZipGS %d MHz", g_zip_speed_mhz);
		limit = buf;
		break;
	default: limit = "Unlimited"; break;
	}
	if(g_eff_mhz > 0.0) {
		snprintf(left, n, "%.1f MHz", g_eff_mhz);
	}
	snprintf(center, n, "%s", g_ui_pause ? "PAUSED" : "");
	snprintf(right, n, "%s", limit);
}

static void
app_tick(void)
{
	if(rom_missing()) {
		if(!g_rom_win.open) {
			snprintf(g_rom_win.title, sizeof(g_rom_win.title),
							"ROM Required");
			g_rom_win.w = 360;
			g_rom_win.h = 190;
			g_rom_win.closable = 0;
			g_rom_win.modal = 0;
			g_rom_win.draw = rom_draw;
			ui_window_center(&g_rom_win);
			ui_window_open(&g_rom_win);
		}
		g_rom_was_missing = 1;
		g_config_control_panel = 1;	/* keep the core halted */
	} else {
		if(g_rom_win.open) {
			ui_window_close(&g_rom_win);
		}
		g_rom_was_missing = 0;
		g_rom_error = 0;
		if(g_config_control_panel) {
			/* The old text config panel is never shown. */
			cfg_set_config_panel(0);
		}
	}
}

static int
app_cover_picture(void)
{
	return rom_missing();
}

static const UiApp g_app = {
	g_menus, N(g_menus), app_item_state, app_command, app_status,
	app_tick, app_cover_picture
};

void
ui_app_init(SDL_Window *w, SDL_Renderer *r)
{
	const char *t;

	ui_init(w, r, &g_app);
	/* Developer aid for headless UI snapshots. */
	t = getenv("EMUBUDDY_OPEN");
	if(t && !strcmp(t, "about")) app_command(CMD_ABOUT);
	if(t && !strcmp(t, "open")) app_command(CMD_OPEN_DISK);
}
