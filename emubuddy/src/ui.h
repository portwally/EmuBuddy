/* EmuBuddy UI toolkit: GS/OS-style (white) windows, menus and widgets drawn
 * with the SDL renderer. All coordinates are "logical" units: the same space
 * the emulator picture is scaled in, so the UI scales with the picture.
 *
 * Window layout (logical): a menu bar of UI_BAR_H on top, the emulator
 * picture, then a status bar of UI_BAR_H at the bottom. */
#ifndef EMUBUDDY_UI_H
#define EMUBUDDY_UI_H

#include <SDL3/SDL.h>

#define UI_BAR_H	20		/* menu bar and status bar height */
extern int	g_ui_hide_status;	/* 1: no status bar (Launcher front end) */
extern int	g_ui_top_h;		/* drawn menu bar height: 20, or 0 with the native menu bar */
#define UI_TOP_H	g_ui_top_h
#define UI_BOT_H	(g_ui_hide_status ? 0 : UI_BAR_H)	/* status bar height */
#define UI_FONT_H	16
#define UI_MAX_WINDOWS	16

/* ---- font (ui_font.c) ---- */
int	ui_font_init(SDL_Renderer *r);
/* Switch to a TrueType file at ppem pixels; NULL = the built-in font. */
int	ui_font_load_file(const char *path, int ppem);
void	ui_font_shutdown(void);
int	ui_text_width(const char *s);
/* More fonts (other files and pixel sizes) for the Launcher's front end. */
typedef struct UiFont UiFont;
UiFont	*ui_font_open(const char *path, int ppem);
void	ui_font_close(UiFont *f);
void	ui_font_set(UiFont *f);			/* NULL = the UI font */
int	ui_font_size(void);			/* pixel size of the current font */
void	ui_draw_text_a(int x, int y, const char *s, Uint32 rgb, int alpha);
void	ui_draw_text(int x, int y, const char *s, Uint32 rgb);

/* ---- toolkit core (ui.c) ---- */
typedef struct UiWindow UiWindow;
struct UiWindow {
	char	title[48];
	int	x, y, w, h;		/* outer rect, including title bar */
	int	open;
	int	modal;
	int	closable;
	/* Draws the window content. (cx,cy) is the content origin. */
	void	(*draw)(UiWindow *win, int cx, int cy);
	void	(*on_close)(UiWindow *win);
	/* A file was dropped on the window. (x,y) are content coordinates.
	 * Return 1 if handled. */
	int	(*on_drop)(UiWindow *win, int x, int y, const char *path);
	void	*user;
};

#define UI_TITLE_H	20

/* menu item flags */
#define UI_MI_SEP	1
#define UI_MI_DISABLED	2
#define UI_MI_CHECKED	4

typedef struct {
	const char	*label;
	int		id;		/* command id, 0 for separators */
	const char	*shortcut;	/* display only, may be NULL */
	int		flags;
} UiMenuItem;

typedef struct {
	const char		*title;
	const UiMenuItem	*items;
	int			n_items;
} UiMenu;

/* The app supplies these. */
typedef struct {
	const UiMenu	*menus;
	int		n_menus;
	/* Returns UI_MI_CHECKED / UI_MI_DISABLED bits for a command, live. */
	int		(*item_state)(int id);
	void		(*command)(int id);
	/* Text for the status bar: left, center, right. */
	void		(*status)(char *left, char *center, char *right, int n);
	/* Called once per frame before drawing (open/close windows here). */
	void		(*tick)(void);
	/* Return 1 to blank the emulator picture (e.g. no ROM loaded). */
	int		(*cover_picture)(void);
	/* Hotkey before the emulator sees it; return 1 if used. */
	int		(*hotkey)(int scancode, int mods);
	/* Extra drawing in the status bar (x, y, w, h of the bar). */
	void		(*status_draw)(int x, int y, int w, int h);
	/* How wide that drawing is, at the right end of the bar before the right
	 * text; the message in the middle keeps clear of it. */
	int		status_draw_w;
} UiApp;

void	ui_init(SDL_Window *w, SDL_Renderer *r, const UiApp *app);
void	ui_shutdown(void);
/* Feed an SDL event. Returns 1 if the UI consumed it (not for the emulator).
 * Mouse coordinates must already be in full logical space (menu bar at y=0). */
int	ui_handle_event(SDL_Event *ev);
/* Draw menu bar, status bar and windows. Call after the emulator picture,
 * with logical presentation on and no viewport. */
void	ui_draw(int logical_w, int logical_h);
/* True while a menu is open or a modal window is up. */
int	ui_is_modal(void);
/* True while a drop-down menu is open. */
int	ui_menu_is_open(void);
/* True while the pointer is over the menu bar, status bar or a window. */
int	ui_pointer_over_ui(void);

void	ui_window_open(UiWindow *w);
void	ui_window_close(UiWindow *w);
void	ui_window_center(UiWindow *w);

/* Drawing helpers (valid inside a window draw callback or ui_draw). */
void	ui_fill(int x, int y, int w, int h, Uint32 rgb);
void	ui_frame(int x, int y, int w, int h, Uint32 rgb);
void	ui_hline(int x, int y, int w, Uint32 rgb);
void	ui_vline(int x, int y, int h, Uint32 rgb);

/* Widgets. Return 1 on the frame the user clicks. (x,y) are relative to the
 * window content origin given to the draw callback; pass it back in cx,cy. */
int	ui_button(UiWindow *win, int cx, int cy, int x, int y, int w, int h,
						const char *label, int enabled);
int	ui_checkbox(UiWindow *win, int cx, int cy, int x, int y, const char *label,
						int *value);
/* Scrolling list. *sel is the selected row (-1 none), *top the first visible
 * row. Returns 1 when a row is clicked, 2 on a double-click. */
int	ui_list(UiWindow *win, int cx, int cy, int x, int y, int w, int h,
		const char **items, int n_items, int *sel, int *top);
/* Row of choices; see ui.c. Returns 1 when the selection changes. */
int	ui_segment(UiWindow *win, int cx, int cy, int x, int y, int w, int h,
		const char **labels, int n, int *sel, unsigned enabled);
/* 1 once when a press inside the rect turns into a drag. */
int	ui_drag_started(UiWindow *win, int cx, int cy, int x, int y, int w, int h);
void	ui_reset_press(void);
/* Invisible click area. Returns 1 when the user clicks it. */
int	ui_hit(UiWindow *win, int cx, int cy, int x, int y, int w, int h);
/* Vertical scroll bar at (x,y), SB width 14, height h. Scrolls *top within
 * [0, total-visible]. Wheel works over the window content. Always call it
 * with the same (x,y) each frame. */
int	ui_vscroll(UiWindow *win, int cx, int cy, int x, int y, int h,
		int total, int visible, int *top);
/* True while a file is being dragged over the window; gives its position in
 * window content coordinates. */
int	ui_drop_hover(UiWindow *win, int cx, int cy, int *x, int *y);
/* Short message in the status bar for about 3 seconds. */
void	ui_message(const char *fmt, ...);
/* Column x offsets for the next ui_list call (items split on tabs). */
void	ui_list_columns(const int *xs, int n);
/* Mouse position in window content coordinates, or 0 if the window is not
 * under the mouse. */
int	ui_mouse_in(UiWindow *win, int cx, int cy, int x, int y, int w, int h);

/* Colors. EmuBuddy uses the fixed GS/OS white look. The Launcher builds the
 * toolkit with UI_THEMABLE, and then the colors and the window title style
 * come from g_ui_theme (set by its skins). */
typedef struct {
	const char	*name;
	Uint32		bg, fg, dim, line, good, warn, bad;	/* windows, text, grey text, rules */
	Uint32		accent, accent_fg;			/* title bar of flat skins, buttons */
	Uint32		desktop;				/* behind the windows */
	int		title_style;				/* UI_TITLE_* */
	int		checker;				/* desktop is a black/white dither */
} UiTheme;
enum { UI_TITLE_GSOS, UI_TITLE_MAC1, UI_TITLE_PLATINUM, UI_TITLE_FLAT, UI_TITLE_MAGAZINE,
	UI_TITLE_TERMINAL };
extern UiTheme	g_ui_theme;
#ifdef UI_THEMABLE
#define UI_WHITE	(g_ui_theme.bg)
#define UI_BLACK	(g_ui_theme.fg)
#define UI_GREY		(g_ui_theme.dim)
#define UI_LGREY	(g_ui_theme.line)
#define UI_GOOD		(g_ui_theme.good)
#define UI_WARN		(g_ui_theme.warn)
#define UI_BAD		(g_ui_theme.bad)
#else
#define UI_WHITE	0xFFFFFF
#define UI_BLACK	0x000000
#define UI_GREY		0x777777
#define UI_LGREY	0xCCCCCC
#define UI_GOOD		0x00AA00
#define UI_WARN		0xFF6600
#define UI_BAD		0xDD0000
/* ---- joysticks (sdl_driver.c), for the Controllers window ---- */
typedef struct {
	char	name[80];
	char	kind[24];		/* "game controller" / "USB joystick" */
	int	x, y;			/* -32768..32767, D-pad / hat included */
	int	dpad;			/* 1 up, 2 right, 4 down, 8 left */
	int	buttons;		/* bit 0, bit 1: IIgs buttons 0 and 1 */
	char	label[2][16];		/* controller button for IIgs button 0, 1 */
} JoyInfo;
int	joystick_info(JoyInfo *ji);
int	joystick_list(char (*names)[80], int max, int *used);
void	ui_controllers_toggle(void);
int	ui_controllers_is_open(void);

#endif

/* ---- EmuBuddy app layer (ui_app.c) ---- */
void	ui_app_init(SDL_Window *w, SDL_Renderer *r);
/* Put a dropped/chosen disk image into the first suitable empty drive. */
void	ui_insert_auto(const char *path);
void	ui_file_browser_open(const char *title, void (*pick)(const char *path));
/* Same browser, but it picks a folder. */
void	ui_folder_browser_open(const char *title, void (*pick)(const char *path));
/* Disk images by extension (.po .2mg .hdv .dsk .woz ...). */
int	ui_is_disk_image(const char *path);
/* Shared Folder and Disk Browser (ui_drives.c / ui_browser.c) */
void	ui_shared_folder_choose(void);
int	ui_drop_regular_file(const char *path);
void	ui_browser_toggle(void);
int	ui_browser_is_open(void);
void	ui_browser_open_image(const char *path);

/* ---- drives window and disk handling (ui_drives.c) ---- */
void	ui_slots_toggle(void);
void	ui_printer_toggle(void);
int	ui_printer_is_open(void);
SDL_Renderer *ui_renderer(void);
int	ui_slots_is_open(void);
void	ui_drives_init(void);
void	ui_drives_tick(void);		/* once per frame */
void	ui_drives_toggle(void);
int	ui_drives_is_open(void);
void	ui_eject_all(void);
int	ui_recent_count(void);
const char *ui_recent_label(int i);
void	ui_recent_activate(int i);

#endif
