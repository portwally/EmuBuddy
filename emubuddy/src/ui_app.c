/* EmuBuddy app layer: menus, commands, status bar, About window and the
 * in-skin file browser. Built on the toolkit in ui.c. */
#include "defc.h"
#include "ui.h"
#include "uthernet2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* core state (sim65816.c, config.c) */
extern int	g_limit_speed;		/* 0 unlimited, 1 1MHz, 2 2.8MHz, 3 ZipGS */
extern int	g_zip_speed_mhz;
extern double	g_eff_mhz;
extern int	g_fullscreen;
extern int	g_window_scale;
int		rec_active(void);
void		ui_drop_dest_toggle(void);
void		ui_show_shared_folder(void);
void		ui_bridge_choose(void);
void		ui_install_receive(void);
extern int	g_skin;
extern int	g_auto_share;
void		sdl_set_skin(int skin);
void		ui_bridge_off(void);
int		ui_bridge_is_on(void);
double		rec_seconds(void);
void		sdl_request_window_screenshot(void);
void		sdl_request_record_toggle(void);
int		sdl_scale_fits(int s);
void		sdl_set_window_scale(int s);
extern int	g_crt;
extern int	g_status_enable;
extern dword64	g_cur_dfcyc;
char		*cfg_text_screen_str(void);
extern int	g_scanline_simulator;
extern int	g_ui_pause;
extern int	g_rom_version;
void		do_reset(void);
void		load_roms_init_memory(void);
void		cfg_file_update_rom(const char *str);
void		cfg_set_config_panel(int panel);
extern int	g_config_control_panel;
int		iwm_motor_busy(void);
int		cfg_guess_slot(const char *filename);
int		cfg_ui_partition_count(void);
const char	*cfg_ui_partition_name(int i);
int		cfg_ui_partition_choose(int pos, int slot, int drive, const char *path);
extern int	g_cfg_select_partition;
extern int	g_cfg_slotdrive;
/* sdl_driver.c */
void		sdl_request_screenshot(void);

#define EMUBUDDY_VERSION	"0.1"

enum {
	CMD_OPEN_DISK = 1, CMD_QUIT, CMD_CHOOSE_ROM, CMD_DRIVES, CMD_EJECT_ALL,
	CMD_SHARE, CMD_BROWSER,
	CMD_RESET = 10, CMD_PAUSE, CMD_SLOTS, CMD_PRINTER, CMD_HARD_RESET,
	CMD_SPEED_1 = 20, CMD_SPEED_28, CMD_ZIP_8, CMD_ZIP_16, CMD_ZIP_32,
	CMD_ZIP_64, CMD_ZIP_128, CMD_UNLIMITED,
	CMD_FULLSCREEN = 30, CMD_CRT, CMD_SCANLINES, CMD_SCREENSHOT, CMD_CORE_STATUS,
	CMD_PASTE = 45, CMD_COPY_SCREEN,
	CMD_SIZE_1 = 50, CMD_SIZE_2, CMD_SIZE_3, CMD_SIZE_4,
	CMD_SHOT_WIN = 55, CMD_RECORD, CMD_DROPDEST, CMD_SHOWSHARE, CMD_BRIDGE, CMD_BRIDGE_OFF, CMD_SKIN_IIGS, CMD_SKIN_SYS, CMD_INSTALL_RECEIVE, CMD_AUTOSHARE,
	CMD_ABOUT = 40,
	CMD_RECENT0 = 60,		/* 60..69 */
};
#define N_RECENT 10

#define DISKS_FIXED	8
static UiMenuItem disks_items[DISKS_FIXED + 2 + N_RECENT] = {
	{ "Drives Window", CMD_DRIVES, "F2", 0 },
	{ "Open Disk Image...", CMD_OPEN_DISK, NULL, 0 },
	{ "Share a Folder...", CMD_SHARE, NULL, 0 },
	{ "Drop Destination...", CMD_DROPDEST, NULL, 0 },
	{ "Show Shared Folder", CMD_SHOWSHARE, NULL, 0 },
	{ "Mount Shared Folder at Start", CMD_AUTOSHARE, NULL, 0 },
	{ "Disk Browser", CMD_BROWSER, NULL, 0 },
	{ "Eject All", CMD_EJECT_ALL, NULL, 0 },
};
static const UiMenuItem file_items[] = {
	{ "Choose ROM File...", CMD_CHOOSE_ROM, NULL, 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Save Screenshot", CMD_SCREENSHOT, "F10", 0 },
	{ "Save Window Screenshot", CMD_SHOT_WIN, "Shift-F10", 0 },
	{ "Record Video", CMD_RECORD, "Ctrl-F10", 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Quit EmuBuddy", CMD_QUIT, NULL, 0 },
};
static const UiMenuItem machine_items[] = {
	{ "Soft Reset", CMD_RESET, "Ctrl-F12", 0 },
	{ "Hard Reset", CMD_HARD_RESET, NULL, 0 },
	{ "Pause", CMD_PAUSE, "F3", 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Slots...", CMD_SLOTS, "F4", 0 },
	{ "Printer", CMD_PRINTER, NULL, 0 },
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
	{ "Window Size 1x", CMD_SIZE_1, NULL, 0 },
	{ "Window Size 2x", CMD_SIZE_2, NULL, 0 },
	{ "Window Size 3x", CMD_SIZE_3, NULL, 0 },
	{ "Window Size 4x", CMD_SIZE_4, NULL, 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Full Screen", CMD_FULLSCREEN, "F11", 0 },
	{ "CRT Effect", CMD_CRT, "Ctrl-F11", 0 },
	{ "Scanlines", CMD_SCANLINES, "Shift-F11", 0 },
	{ "", 0, NULL, UI_MI_SEP },
	{ "Core Debug Lines", CMD_CORE_STATUS, NULL, 0 },
};
static const UiMenuItem edit_items[] = {
	{ "Paste Text", CMD_PASTE, NULL, 0 },
	{ "Copy Screen Text", CMD_COPY_SCREEN, NULL, 0 },
};
static const UiMenuItem help_items[] = {
	{ "About EmuBuddy", CMD_ABOUT, NULL, 0 },
};

#define N(a)	((int)(sizeof(a) / sizeof((a)[0])))
static UiMenu g_menus[] = {
	{ "File", file_items, N(file_items) },
	{ "Edit", edit_items, N(edit_items) },
	{ "Disks", disks_items, DISKS_FIXED },
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
static int	g_fb_folder_mode;	/* choosing a folder, not a file */

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
	if(g_fb_folder_mode && !g_fb_isdir[g_fb_n]) {
		return SDL_ENUM_CONTINUE;	/* choosing a folder: hide files */
	}
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

/* Folder mode: choose the selected folder, or the one being shown. */
static void
fb_choose_folder(void)
{
	char	full[1536], name[1024];
	size_t	n;

	if(g_fb_sel >= 0 && g_fb_sel < g_fb_n) {
		snprintf(name, sizeof(name), "%s", g_fb_names[g_fb_sel]);
		n = strlen(name);
		if(n && name[n - 1] == '/') {
			name[n - 1] = 0;
		}
		snprintf(full, sizeof(full), "%s%s", g_fb_dir, name);
	} else {
		snprintf(full, sizeof(full), "%s", g_fb_dir);
		n = strlen(full);
		if(n > 1 && full[n - 1] == '/') {
			full[n - 1] = 0;
		}
	}
	snprintf(g_fb_last_dir, sizeof(g_fb_last_dir), "%s", g_fb_dir);
	ui_window_close(&g_fb_win);
	if(g_fb_pick) {
		g_fb_pick(full);
	}
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
		if(g_fb_folder_mode) {
			if(ui_button(win, cx, cy, w - 8 - 70, by, 70, 24, "Choose",
									1)) {
				fb_choose_folder();
			}
		} else if(ui_button(win, cx, cy, w - 8 - 70, by, 70, 24, "Open",
				g_fb_sel >= 0)) {
			fb_accept();
		}
	}
}

/* ---- System skin: the system's own file and folder choosers ---- */

extern int	g_skin;
SDL_Window	*ui_window(void);

static void	(*g_native_pick)(const char *);	/* the chooser that is open */
static char	g_native_path[1100];
static SDL_AtomicInt g_native_ready;

/* May run on another thread: hand the result to app_tick(). */
static void SDLCALL
native_chooser_cb(void *ud, const char * const *list, int filter)
{
	(void)ud;
	(void)filter;
	if(list && list[0] && g_native_pick) {
		snprintf(g_native_path, sizeof(g_native_path), "%s", list[0]);
		SDL_SetAtomicInt(&g_native_ready, 1);
	} else {
		g_native_pick = NULL;		/* cancelled */
	}
}

static void
native_chooser_tick(void)
{
	if(SDL_GetAtomicInt(&g_native_ready)) {
		void (*pick)(const char *) = g_native_pick;
		char	path[1100];

		snprintf(path, sizeof(path), "%s", g_native_path);
		g_native_pick = NULL;
		SDL_SetAtomicInt(&g_native_ready, 0);
		if(pick) {
			pick(path);
		}
	}
}

static int
native_chooser_open(const char *title, int folder, void (*pick)(const char *))
{
	SDL_PropertiesID props;

	if(g_skin != 1 || g_native_pick) {
		return g_skin == 1;		/* a chooser is open already: ignore */
	}
	props = SDL_CreateProperties();
	SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, ui_window());
	SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title);
	if(g_fb_last_dir[0]) {
		SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING,
							g_fb_last_dir);
	}
	g_native_pick = pick;
	if(getenv("EMUBUDDY_PICK")) {		/* test aid: answer without a dialog */
		const char *fake[2] = { getenv("EMUBUDDY_PICK"), NULL };

		native_chooser_cb(NULL, fake, 0);
		SDL_DestroyProperties(props);
		return 1;
	}
	SDL_ShowFileDialogWithProperties(folder ? SDL_FILEDIALOG_OPENFOLDER :
				SDL_FILEDIALOG_OPENFILE, native_chooser_cb, NULL, props);
	SDL_DestroyProperties(props);
	return 1;
}

void
ui_folder_browser_open(const char *title, void (*pick)(const char *))
{
	if(native_chooser_open(title, 1, pick)) {
		return;
	}
	g_fb_folder_mode = 1;
	ui_file_browser_open(title, pick);
	g_fb_folder_mode = 1;		/* (cleared by the plain opener) */
	fb_read_dir(g_fb_dir);
}

int
ui_is_disk_image(const char *path)
{
	static const char *exts[] = { "po", "hdv", "2mg", "2img", "dsk", "do",
		"woz", "nib", "dc", "dc42", "image", "img", "raw", "sdk",
		"gz", NULL };
	const char	*dot = strrchr(path, '.'), *slash = strrchr(path, '/');
	int		i;

	if(!dot || (slash && dot < slash)) {
		return 0;
	}
	for(i = 0; exts[i]; i++) {
		if(!strcasecmp(dot + 1, exts[i])) {
			return 1;
		}
	}
	return 0;
}

void
ui_file_browser_open(const char *title, void (*pick)(const char *))
{
	const char	*h;

	if(g_fb_folder_mode == 0 && native_chooser_open(title, 0, pick)) {
		return;
	}
	g_fb_folder_mode = 0;
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
		"2026, Walter Tengler",
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
	g_about_win.h = 286;
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
	ui_insert_auto(path);
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
		ui_file_browser_open("Choose ROM File", rom_picked);
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

/* Type the host clipboard into the IIgs keyboard. */
static void
paste_text(void)
{
	char	*t = SDL_GetClipboardText(), *p;
	int	n = 0;

	if(!t || !*t) {
		ui_message("The clipboard has no text");
		SDL_free(t);
		return;
	}
	for(p = t; *p; p++) {
		unsigned char c = (unsigned char)*p;

		if(c == '\t') {
			c = ' ';
		}
		if(c < 0x80 && adb_paste_add_buf(c) == 0) {
			n++;
		}
	}
	SDL_free(t);
	ui_message("Pasting %d characters", n);
}

static void
app_command(int id)
{
	switch(id) {
	case CMD_OPEN_DISK: ui_file_browser_open("Open Disk Image", disk_picked); break;
	case CMD_CHOOSE_ROM: ui_file_browser_open("Choose ROM File", rom_picked); break;
	case CMD_DRIVES: ui_drives_toggle(); break;
	case CMD_SLOTS: ui_slots_toggle(); break;
	case CMD_PRINTER: ui_printer_toggle(); break;
	case CMD_EJECT_ALL: ui_eject_all(); break;
	case CMD_SHARE: ui_shared_folder_choose(); break;
	case CMD_BROWSER: ui_browser_toggle(); break;
	case CMD_QUIT: {
		SDL_Event q;

		SDL_zero(q);
		q.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&q);
		break; }
	case CMD_RESET: do_reset(); break;
	case CMD_HARD_RESET:
		load_roms_init_memory();	/* reload ROM, wipe RAM */
		do_reset();			/* and boot from the startup disk */
		break;
	case CMD_PAUSE: g_ui_pause = !g_ui_pause; break;
	case CMD_SPEED_1: set_limit(1, 0); break;
	case CMD_SPEED_28: set_limit(2, 0); break;
	case CMD_ZIP_8: set_limit(3, 8); break;
	case CMD_ZIP_16: set_limit(3, 16); break;
	case CMD_ZIP_32: set_limit(3, 32); break;
	case CMD_ZIP_64: set_limit(3, 64); break;
	case CMD_ZIP_128: set_limit(3, 128); break;
	case CMD_UNLIMITED: set_limit(0, 0); break;
	case CMD_SIZE_1: case CMD_SIZE_2: case CMD_SIZE_3: case CMD_SIZE_4:
		sdl_set_window_scale(id - CMD_SIZE_1 + 1);
		break;
	case CMD_FULLSCREEN: g_fullscreen = !g_fullscreen; break;
	case CMD_CRT: g_crt = !g_crt; break;
	case CMD_SCANLINES:
		g_scanline_simulator = g_scanline_simulator ? 0 : 50;
		break;
	case CMD_SCREENSHOT: sdl_request_screenshot(); break;
	case CMD_SHOT_WIN: sdl_request_window_screenshot(); break;
	case CMD_RECORD: sdl_request_record_toggle(); break;
	case CMD_DROPDEST: ui_drop_dest_toggle(); break;
	case CMD_SHOWSHARE: ui_show_shared_folder(); break;
	case CMD_AUTOSHARE: g_auto_share = !g_auto_share; break;
	case CMD_BRIDGE: ui_bridge_choose(); break;
	case CMD_INSTALL_RECEIVE: ui_install_receive(); break;
	case CMD_BRIDGE_OFF: ui_bridge_off(); break;
	case CMD_CORE_STATUS: g_status_enable = !g_status_enable; break;
	case CMD_ABOUT: about_open(); break;
	case CMD_PASTE: paste_text(); break;
	case CMD_COPY_SCREEN:
		SDL_SetClipboardText(cfg_text_screen_str());
		ui_message("Screen text copied");
		break;
	default:
		if(id >= CMD_RECENT0 && id < CMD_RECENT0 + N_RECENT) {
			ui_recent_activate(id - CMD_RECENT0);
		}
	}
}

static int
app_item_state(int id)
{
	int	zip = (g_limit_speed == 3) ? g_zip_speed_mhz : 0;

	switch(id) {
	case CMD_PAUSE: return g_ui_pause ? UI_MI_CHECKED : 0;
	case CMD_DRIVES: return ui_drives_is_open() ? UI_MI_CHECKED : 0;
	case CMD_BROWSER: return ui_browser_is_open() ? UI_MI_CHECKED : 0;
	case CMD_SLOTS: return ui_slots_is_open() ? UI_MI_CHECKED : 0;
	case CMD_PRINTER: return ui_printer_is_open() ? UI_MI_CHECKED : 0;
	case CMD_SPEED_1: return g_limit_speed == 1 ? UI_MI_CHECKED : 0;
	case CMD_SPEED_28: return g_limit_speed == 2 ? UI_MI_CHECKED : 0;
	case CMD_ZIP_8: return zip == 8 ? UI_MI_CHECKED : 0;
	case CMD_ZIP_16: return zip == 16 ? UI_MI_CHECKED : 0;
	case CMD_ZIP_32: return zip == 32 ? UI_MI_CHECKED : 0;
	case CMD_ZIP_64: return zip == 64 ? UI_MI_CHECKED : 0;
	case CMD_ZIP_128: return zip == 128 ? UI_MI_CHECKED : 0;
	case CMD_UNLIMITED: return g_limit_speed == 0 ? UI_MI_CHECKED : 0;
	case CMD_SIZE_1: case CMD_SIZE_2: case CMD_SIZE_3: case CMD_SIZE_4: {
		int n = id - CMD_SIZE_1 + 1;

		return (g_window_scale == n && !g_fullscreen ? UI_MI_CHECKED : 0) |
			(sdl_scale_fits(n) ? 0 : UI_MI_DISABLED);
	}
	case CMD_AUTOSHARE: return g_auto_share ? UI_MI_CHECKED : 0;
	case CMD_BRIDGE: return ui_bridge_is_on() ? UI_MI_CHECKED : 0;
	case CMD_BRIDGE_OFF: return ui_bridge_is_on() ? 0 : UI_MI_DISABLED;
	case CMD_RECORD: return rec_active() ? UI_MI_CHECKED : 0;
	case CMD_FULLSCREEN: return g_fullscreen ? UI_MI_CHECKED : 0;
	case CMD_CRT: return g_crt ? UI_MI_CHECKED : 0;
	case CMD_CORE_STATUS: return g_status_enable ? UI_MI_CHECKED : 0;
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
	if(uthernet2_active()) {
		unsigned tx, rx;
		size_t l = strlen(left);

		uthernet2_counters(&tx, &rx);
		snprintf(left + l, (size_t)n - l, "   Net out %u in %u", tx, rx);
	}
	if(rec_active()) {
		int sec = (int)rec_seconds();

		snprintf(center, n, "REC %d:%02d", sec / 60, sec % 60);
	} else {
		snprintf(center, n, "%s", g_ui_pause ? "PAUSED" : "");
	}
	snprintf(right, n, "%s", limit);
}

/* Disks menu: fixed items, then the recent disks. */
static void
menu_rebuild_recent(void)
{
	int	i, k = DISKS_FIXED, n = ui_recent_count();

	if(n > 0) {
		disks_items[k].label = "";
		disks_items[k].id = 0;
		disks_items[k].shortcut = NULL;
		disks_items[k].flags = UI_MI_SEP;
		k++;
	}
	for(i = 0; i < n && k < DISKS_FIXED + 1 + N_RECENT; i++, k++) {
		disks_items[k].label = ui_recent_label(i);
		disks_items[k].id = CMD_RECENT0 + i;
		disks_items[k].shortcut = NULL;
		disks_items[k].flags = 0;
	}
	g_menus[2].n_items = k;
}

static void
app_tick(void)
{
	native_chooser_tick();
	menu_rebuild_recent();
	ui_drives_tick();
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

/* F2 Drives, F3 Pause, F4 Slots; F7 (terminal debugger) is blocked. */
static int
app_hotkey(int sc, int mods)
{
	int	cmd = 0;
	(void)mods;

	switch(sc) {
	case SDL_SCANCODE_F2: cmd = CMD_DRIVES; break;
	case SDL_SCANCODE_F3: cmd = CMD_PAUSE; break;
	case SDL_SCANCODE_F4: cmd = CMD_SLOTS; break;
	case SDL_SCANCODE_F7: return 1;
	default: return 0;
	}
	if(mods >= 0) {				/* key down, not a repeat */
		app_command(cmd);
	}
	return 1;
}

/* Disk activity lights: 3.5, 5.25 and hard disk, lit for a moment after
 * the drive is read or written. */
static void
app_status_draw(int x, int y, int w, int h)
{
	static const char *names[3] = { "3.5", "5.25", "HD" };
	int	k, d, nd, lx, active;
	dword64	window = (dword64)300000 << 16;	/* ~0.3 s of IIgs time */

	(void)x;
	lx = w - 3 * 52;
	for(k = 0; k < 3; k++) {
		int slot = 5 + k;

		active = 0;
		nd = (slot == 7) ? MAX_C7_DISKS : 2;
		for(d = 0; d < nd; d++) {
			Disk *dsk = iwm_get_dsk_from_slot_drive(slot, d);

			if(dsk->fd >= 0 && dsk->dfcyc_last_read &&
				g_cur_dfcyc - dsk->dfcyc_last_read < window) {
				active = 1;
			}
		}
		ui_fill(lx, y + 6, 9, 9, active ? 0x44DD77 : 0x3A3E48);
		ui_frame(lx, y + 6, 9, 9, 0x8D93A3);
		ui_draw_text(lx + 13, y + 3, names[k], 0xE8EAF0);
		lx += 52;
	}
	(void)h;
}

static int
app_cover_picture(void)
{
	return rom_missing();
}

static const UiApp g_app = {
	g_menus, N(g_menus), app_item_state, app_command, app_status,
	app_tick, app_cover_picture, app_hotkey, app_status_draw
};

void
ui_app_init(SDL_Window *w, SDL_Renderer *r)
{
	const char *t;

	ui_init(w, r, &g_app);
	ui_drives_init();
	/* Developer aid for headless UI snapshots. */
	t = getenv("EMUBUDDY_OPEN");
	if(t && !strcmp(t, "about")) app_command(CMD_ABOUT);
	if(t && !strcmp(t, "open")) app_command(CMD_OPEN_DISK);
	if(t && !strcmp(t, "drives")) app_command(CMD_DRIVES);
	if(t && !strcmp(t, "slots")) app_command(CMD_SLOTS);
	if(t && !strcmp(t, "printer")) app_command(CMD_PRINTER);
	if(t && !strcmp(t, "browser")) app_command(CMD_BROWSER);
	t = getenv("EMUBUDDY_BROWSE");		/* developer aid: image path */
	if(t) ui_browser_open_image(t);
	t = getenv("EMUBUDDY_INSERT");		/* developer aid: path;path;.. */
	if(t) {
		char *copy = strdup(t), *save = NULL, *p;

		for(p = strtok_r(copy, ";", &save); p;
					p = strtok_r(NULL, ";", &save)) {
			ui_insert_auto(p);
		}
		free(copy);
	}
}
