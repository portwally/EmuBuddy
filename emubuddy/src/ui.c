/* EmuBuddy UI toolkit core: window manager, input routing, menu bar, status
 * bar and widgets, in the white Apple IIgs GS/OS look. See ui.h. */
#include "ui.h"

int	g_ui_top_h = UI_BAR_H;
int	g_ui_hide_status = 0;		/* the Launcher's front end draws full screen */
UiTheme	g_ui_theme = { "GS/OS", 0xFFFFFF, 0x000000, 0x777777, 0xCCCCCC, 0x00AA00, 0xFF6600,
	0xDD0000, 0x000000, 0xFFFFFF, 0x2222FF, UI_TITLE_GSOS, 0 };
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static SDL_Renderer	*g_r;
static SDL_Window	*g_sdlwin;
static const UiApp	*g_app;

static UiWindow	*g_wins[UI_MAX_WINDOWS];	/* z-order, last is topmost */
static int	g_nwins;
static int	g_lw, g_lh;			/* logical size, last draw */

/* Pointer / input state. Edges are set by events and cleared after a draw. */
static float	g_mx = -1, g_my = -1;
static int	g_down, g_pressed, g_released, g_dblclick;
static float	g_wheel;
static int	g_press_owned;		/* the current press began on the UI */
static int	g_over_ui;
static int	g_drop_active;
static float	g_press_x, g_press_y;
static int	g_drag_taken;
static float	g_drop_x, g_drop_y;

/* What the current press is doing. */
enum { CAP_NONE, CAP_DRAG, CAP_CLOSE, CAP_WIDGET, CAP_MENU };
static int	g_cap;
static UiWindow	*g_cap_win;
static int	g_drag_dx, g_drag_dy;

static int	g_menu_open = -1;
static void	*g_active_widget;	/* widget currently being pressed */
static long	g_active_id;

/* ------------------------------------------------------------------ */
/* drawing helpers */

static void
setcol(Uint32 rgb)
{
	SDL_SetRenderDrawColor(g_r, (rgb >> 16) & 255, (rgb >> 8) & 255,
							rgb & 255, 255);
}

void
ui_fill(int x, int y, int w, int h, Uint32 rgb)
{
	SDL_FRect r = { (float)x, (float)y, (float)w, (float)h };

	setcol(rgb);
	SDL_RenderFillRect(g_r, &r);
}

void
ui_hline(int x, int y, int w, Uint32 rgb)
{
	ui_fill(x, y, w, 1, rgb);
}

void
ui_vline(int x, int y, int h, Uint32 rgb)
{
	ui_fill(x, y, 1, h, rgb);
}

void
ui_frame(int x, int y, int w, int h, Uint32 rgb)
{
	ui_hline(x, y, w, rgb);
	ui_hline(x, y + h - 1, w, rgb);
	ui_vline(x, y, h, rgb);
	ui_vline(x + w - 1, y, h, rgb);
}

static void
text_centered(int x, int y, int w, int h, const char *s, Uint32 rgb)
{
	int	tw = ui_text_width(s);

	ui_draw_text(x + (w - tw) / 2, y + (h - UI_FONT_H) / 2, s, rgb);
}

/* ------------------------------------------------------------------ */
/* init / lifecycle */

const UiApp *
ui_get_app(void)
{
	return g_app;
}

/* Commands chosen in the system menu bar run on the next frame. */
#define POST_MAX	32
static int	g_posted[POST_MAX];
static int	g_npost;

void
ui_post_command(int id)
{
	if(g_npost < POST_MAX) {
		g_posted[g_npost++] = id;
	}
}

static void
run_posted(void)
{
	int	i, n = g_npost;
	int	ids[POST_MAX];

	memcpy(ids, g_posted, sizeof(int) * (size_t)n);
	g_npost = 0;
	for(i = 0; i < n; i++) {
		if(g_app->command && ids[i]) {
			g_app->command(ids[i]);
		}
	}
}

void
ui_init(SDL_Window *w, SDL_Renderer *r, const UiApp *app)
{
	g_sdlwin = w;
	g_r = r;
	g_app = app;
	if(ui_font_init(r) != 0) {
		printf("EmuBuddy UI: font init failed\n");
	}
	if(getenv("EMUBUDDY_MENU")) {		/* developer aid */
		g_menu_open = atoi(getenv("EMUBUDDY_MENU"));
	}
}

void
ui_shutdown(void)
{
	ui_font_shutdown();
}

/* ------------------------------------------------------------------ */
/* windows */

static int
win_index(UiWindow *w)
{
	int	i;

	for(i = 0; i < g_nwins; i++) {
		if(g_wins[i] == w) {
			return i;
		}
	}
	return -1;
}

static void
win_raise(UiWindow *w)
{
	int	i = win_index(w);

	if(i < 0) {
		return;
	}
	for(; i < g_nwins - 1; i++) {
		g_wins[i] = g_wins[i + 1];
	}
	g_wins[g_nwins - 1] = w;
}

void
ui_window_open(UiWindow *w)
{
	if(win_index(w) < 0 && g_nwins < UI_MAX_WINDOWS) {
		g_wins[g_nwins++] = w;
	}
	w->open = 1;
	win_raise(w);
}

void
ui_window_close(UiWindow *w)
{
	int	i = win_index(w);

	if(i >= 0) {
		for(; i < g_nwins - 1; i++) {
			g_wins[i] = g_wins[i + 1];
		}
		g_nwins--;
	}
	w->open = 0;
	if(g_cap_win == w) {
		g_cap = CAP_NONE;
		g_cap_win = NULL;
	}
	if(w->on_close) {
		w->on_close(w);
	}
}

void
ui_window_center(UiWindow *w)
{
	int	lw = g_lw ? g_lw : 640;
	int	lh = g_lh ? g_lh : 440;

	w->x = (lw - w->w) / 2;
	w->y = UI_TOP_H + (lh - UI_TOP_H - UI_BOT_H - w->h) / 2;
	if(w->y < UI_TOP_H) {
		w->y = UI_TOP_H;
	}
}

static UiWindow *
top_modal(void)
{
	int	i;

	for(i = g_nwins - 1; i >= 0; i--) {
		if(g_wins[i]->modal) {
			return g_wins[i];
		}
	}
	return NULL;
}

static int
in_rect(float px, float py, int x, int y, int w, int h)
{
	return px >= x && px < x + w && py >= y && py < y + h;
}

/* Topmost window under (px,py), honoring modality. */
static UiWindow *
win_at(float px, float py)
{
	int		i;
	UiWindow	*m = top_modal();

	for(i = g_nwins - 1; i >= 0; i--) {
		UiWindow *w = g_wins[i];

		if(in_rect(px, py, w->x, w->y, w->w, w->h)) {
			return (m && w != m) ? NULL : w;
		}
	}
	return NULL;
}

SDL_Window *
ui_window(void)
{
	return g_sdlwin;
}

SDL_Renderer *
ui_renderer(void)
{
	return g_r;
}

/* True while a drop-down menu is open. */
int
ui_menu_is_open(void)
{
	return g_menu_open >= 0;
}

int
ui_is_modal(void)
{
	return g_menu_open >= 0 || top_modal() != NULL;
}

int
ui_pointer_over_ui(void)
{
	return g_over_ui;
}

/* ------------------------------------------------------------------ */
/* menu layout (shared by drawing and hit testing) */

#define MENU_PAD	8

/* Colours of the menu bar, drop-down menus and status bar. The Launcher's
 * skins theme them like everything else; in EmuBuddy they are a neutral dark
 * strip that can't be mistaken for the GS/OS menu bar inside the emulator. */
#ifdef UI_THEMABLE
#define CH_BG		UI_WHITE
#define CH_FG		UI_BLACK
#define CH_DIM		UI_GREY
#define CH_SEL_BG	UI_BLACK
#define CH_SEL_FG	UI_WHITE
#define CH_LINE		UI_BLACK
#define CH_SHADOW	UI_BLACK
#define CH_BRAND	UI_BLACK
#else
#define CH_BG		0x2A2D34
#define CH_FG		0xE8EAF0
#define CH_DIM		0x8D93A3
#define CH_SEL_BG	0x3D6AE0
#define CH_SEL_FG	0xFFFFFF
#define CH_LINE		0x555B6B
#define CH_SHADOW	0x0C0D10
#define CH_BRAND	0x7FA6FF
#endif
#define MENU_ITEM_H	18
#define MENU_SEP_H	8

static int
menu_x0(void)
{
#ifdef UI_THEMABLE
	return 30;
#else
	return ui_text_width("EmuBuddy") + 28;
#endif
}

static int
menu_title_x(int i)
{
	int	x = menu_x0(), k;

	for(k = 0; k < i; k++) {
		x += ui_text_width(g_app->menus[k].title) + 2 * MENU_PAD;
	}
	return x;
}

static int
menu_title_w(int i)
{
	return ui_text_width(g_app->menus[i].title) + 2 * MENU_PAD;
}

static int
menu_item_h(const UiMenuItem *it)
{
	return (it->flags & UI_MI_SEP) ? MENU_SEP_H : MENU_ITEM_H;
}

static int
menu_box_w(const UiMenu *m)
{
	int	i, w = 0, tw, sw;

	for(i = 0; i < m->n_items; i++) {
		tw = ui_text_width(m->items[i].label);
		sw = m->items[i].shortcut ? ui_text_width(m->items[i].shortcut) +
								16 : 0;
		if(tw + sw > w) {
			w = tw + sw;
		}
	}
	return w + 32;
}

static int
menu_box_h(const UiMenu *m)
{
	int	i, h = 0;

	for(i = 0; i < m->n_items; i++) {
		h += menu_item_h(&m->items[i]);
	}
	return h + 2;
}

/* Returns the item index under (px,py) in the open menu, or -1. */
static int
menu_item_at(float px, float py)
{
	const UiMenu	*m;
	int		i, bx, by, y;

	if(g_menu_open < 0) {
		return -1;
	}
	m = &g_app->menus[g_menu_open];
	bx = menu_title_x(g_menu_open) - MENU_PAD / 2;
	by = UI_BAR_H;
	if(!in_rect(px, py, bx, by, menu_box_w(m), menu_box_h(m))) {
		return -1;
	}
	y = by + 1;
	for(i = 0; i < m->n_items; i++) {
		int h = menu_item_h(&m->items[i]);

		if(py >= y && py < y + h) {
			return i;
		}
		y += h;
	}
	return -1;
}

static int
menu_title_at(float px, float py)
{
	int	i;

	if(py < 0 || py >= UI_TOP_H) {
		return -1;
	}
	for(i = 0; i < g_app->n_menus; i++) {
		if(px >= menu_title_x(i) && px < menu_title_x(i) + menu_title_w(i)) {
			return i;
		}
	}
	return -1;
}

static int
menu_dropdown_hit(float px, float py)
{
	const UiMenu	*m;

	if(g_menu_open < 0) {
		return 0;
	}
	m = &g_app->menus[g_menu_open];
	return in_rect(px, py, menu_title_x(g_menu_open) - MENU_PAD / 2,
			UI_BAR_H, menu_box_w(m) + 1, menu_box_h(m) + 1);
}

/* ------------------------------------------------------------------ */
/* events */

static int
over_ui(float px, float py)
{
	if(py < UI_TOP_H || py >= g_lh - UI_BOT_H) {
		return 1;
	}
	if(menu_dropdown_hit(px, py)) {
		return 1;
	}
	return win_at(px, py) != NULL || top_modal() != NULL;
}

static void
close_box_rect(UiWindow *w, int *x, int *y)
{
	*x = w->x + 8;
	*y = w->y + 4;
}

static int
handle_mouse(SDL_Event *ev)
{
	SDL_Event	t = *ev;
	UiWindow	*w;
	int		it, ti, bx, by;

	if(!g_lw) {
		return 0;			/* nothing drawn yet */
	}
	SDL_ConvertEventToRenderCoordinates(g_r, &t);

	switch(ev->type) {
	case SDL_EVENT_MOUSE_MOTION:
		g_mx = t.motion.x;
		g_my = t.motion.y;
		g_over_ui = over_ui(g_mx, g_my) || g_cap != CAP_NONE;
		if(g_cap == CAP_DRAG && g_cap_win) {
			int nx = (int)g_mx - g_drag_dx;
			int ny = (int)g_my - g_drag_dy;

			if(nx < 40 - g_cap_win->w) nx = 40 - g_cap_win->w;
			if(nx > g_lw - 40) nx = g_lw - 40;
			if(ny < UI_TOP_H) ny = UI_TOP_H;
			if(ny > g_lh - UI_BOT_H - UI_TITLE_H)
				ny = g_lh - UI_BOT_H - UI_TITLE_H;
			g_cap_win->x = nx;
			g_cap_win->y = ny;
		}
		if(g_menu_open >= 0 && g_cap == CAP_MENU) {
			ti = menu_title_at(g_mx, g_my);
			if(ti >= 0) {
				g_menu_open = ti;
			}
		} else if(g_menu_open >= 0 && g_my < UI_BAR_H) {
			ti = menu_title_at(g_mx, g_my);
			if(ti >= 0) {
				g_menu_open = ti;	/* slide between menus */
			}
		}
		return g_over_ui || g_press_owned;

	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if(ev->button.button != SDL_BUTTON_LEFT) {
			return over_ui(t.button.x, t.button.y);
		}
		g_mx = t.button.x;
		g_my = t.button.y;
		g_dblclick = ev->button.clicks >= 2;
		/* Menu bar. */
		ti = menu_title_at(g_mx, g_my);
		if(ti >= 0 && !top_modal()) {
			g_menu_open = (g_menu_open == ti) ? -1 : ti;
			g_cap = CAP_MENU;
			g_press_owned = 1;
			g_down = 1;
			return 1;
		}
		if(g_menu_open >= 0) {
			if(menu_dropdown_hit(g_mx, g_my)) {
				g_press_owned = 1;
				g_down = 1;
				return 1;		/* act on release */
			}
			g_menu_open = -1;		/* click away closes */
			g_press_owned = 1;
			g_cap = CAP_NONE;
			return 1;
		}
		w = win_at(g_mx, g_my);
		if(!w) {
			if(over_ui(g_mx, g_my)) {
				g_press_owned = 1;	/* bars/modal backdrop */
				return 1;
			}
			return 0;
		}
		g_press_owned = 1;
		g_down = 1;
		win_raise(w);
		if(g_my < w->y + UI_TITLE_H) {
			close_box_rect(w, &bx, &by);
			if(w->closable && in_rect(g_mx, g_my, bx, by, 11, 11)) {
				g_cap = CAP_CLOSE;
			} else {
				g_cap = CAP_DRAG;
				g_drag_dx = (int)g_mx - w->x;
				g_drag_dy = (int)g_my - w->y;
			}
			g_cap_win = w;
		} else {
			g_cap = CAP_WIDGET;
			g_cap_win = w;
			g_pressed = 1;
			g_press_x = g_mx;
			g_press_y = g_my;
			g_drag_taken = 0;
		}
		return 1;

	case SDL_EVENT_MOUSE_BUTTON_UP:
		if(ev->button.button != SDL_BUTTON_LEFT) {
			return g_press_owned;
		}
		g_mx = t.button.x;
		g_my = t.button.y;
		if(!g_press_owned) {
			return 0;
		}
		g_press_owned = 0;
		g_down = 0;
		g_released = 1;
		if(g_menu_open >= 0) {
			it = menu_item_at(g_mx, g_my);
			if(it >= 0) {
				const UiMenuItem *mi =
					&g_app->menus[g_menu_open].items[it];
				int st = g_app->item_state ?
						g_app->item_state(mi->id) : 0;

				if(!(mi->flags & (UI_MI_SEP | UI_MI_DISABLED)) &&
						!(st & UI_MI_DISABLED)) {
					g_menu_open = -1;
					if(g_app->command) {
						g_app->command(mi->id);
					}
				}
			}
		}
		if(g_cap == CAP_CLOSE && g_cap_win) {
			close_box_rect(g_cap_win, &bx, &by);
			if(in_rect(g_mx, g_my, bx, by, 11, 11)) {
				ui_window_close(g_cap_win);
			}
		}
		g_cap = CAP_NONE;
		g_cap_win = NULL;
		return 1;

	case SDL_EVENT_MOUSE_WHEEL:
		if(over_ui(g_mx, g_my)) {
			g_wheel += ev->wheel.y;
			return 1;
		}
		return 0;
	}
	return 0;
}

int
ui_handle_event(SDL_Event *ev)
{
	switch(ev->type) {
	case SDL_EVENT_MOUSE_MOTION:
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
	case SDL_EVENT_MOUSE_WHEEL:
		return handle_mouse(ev);
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		if(ev->key.scancode == SDL_SCANCODE_F4) {
			return 1;		/* the old text config panel is gone */
		}
		if(g_app->hotkey && !ui_is_modal() &&
				g_app->hotkey(ev->key.scancode,
				ev->type == SDL_EVENT_KEY_DOWN ?
				(ev->key.repeat ? -1 : (int)ev->key.mod) : -2)) {
			return 1;
		}
		if(ev->type == SDL_EVENT_KEY_UP) {
			return ui_is_modal();
		}
		if(ev->key.scancode == SDL_SCANCODE_ESCAPE && g_menu_open >= 0) {
			g_menu_open = -1;
			return 1;
		}
		if(ev->key.scancode == SDL_SCANCODE_ESCAPE && top_modal() &&
						top_modal()->closable) {
			ui_window_close(top_modal());
			return 1;
		}
		return ui_is_modal();
	case SDL_EVENT_TEXT_INPUT:
		return ui_is_modal();
	case SDL_EVENT_DROP_BEGIN:
	case SDL_EVENT_DROP_POSITION:
	case SDL_EVENT_DROP_COMPLETE:
	case SDL_EVENT_DROP_FILE: {
		SDL_Event	t = *ev;
		UiWindow	*w;

		if(!g_lw) {
			return 0;
		}
		if(ev->type == SDL_EVENT_DROP_COMPLETE) {
			g_drop_active = 0;
			return 0;
		}
		SDL_ConvertEventToRenderCoordinates(g_r, &t);
		if(ev->type != SDL_EVENT_DROP_FILE) {
			g_drop_active = 1;
			g_drop_x = t.drop.x;
			g_drop_y = t.drop.y;
			return 0;
		}
		g_drop_active = 0;
		w = win_at(t.drop.x, t.drop.y);
		if(w && w->on_drop && ev->drop.data) {
			return w->on_drop(w, (int)t.drop.x - w->x - 1,
				(int)t.drop.y - w->y - UI_TITLE_H,
				ev->drop.data);
		}
		return 0;
	}
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		g_over_ui = 0;
		g_mx = g_my = -1;
		return 0;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* widgets */

int
ui_mouse_in(UiWindow *win, int cx, int cy, int x, int y, int w, int h)
{
	UiWindow	*m = top_modal();

	if(g_menu_open >= 0) {
		return 0;
	}
	if(win_at(g_mx, g_my) != win || (m && win != m)) {
		return 0;
	}
	return in_rect(g_mx, g_my, cx + x, cy + y, w, h);
}

static long
widget_id(int x, int y)
{
	return ((long)x << 16) ^ (long)y ^ 0x40000000L;
}

int
ui_button(UiWindow *win, int cx, int cy, int x, int y, int w, int h,
						const char *label, int enabled)
{
	int	inside = enabled && ui_mouse_in(win, cx, cy, x, y, w, h);
	long	id = widget_id(x, y);
	int	pressed_look, result = 0;
	Uint32	fg = enabled ? UI_BLACK : UI_GREY;

	if(inside && g_pressed) {
		g_active_widget = win;
		g_active_id = id;
	}
	pressed_look = (g_active_widget == win && g_active_id == id) &&
							g_down && inside;
	if(g_active_widget == win && g_active_id == id && g_released) {
		result = inside;
		g_active_widget = NULL;
	}
	cx += x;
	cy += y;
	ui_fill(cx, cy, w, h, pressed_look ? UI_BLACK : UI_WHITE);
	ui_frame(cx, cy, w, h, enabled ? UI_BLACK : UI_GREY);
	/* round the corners */
	ui_fill(cx, cy, 1, 1, UI_WHITE);
	ui_fill(cx + w - 1, cy, 1, 1, UI_WHITE);
	ui_fill(cx, cy + h - 1, 1, 1, UI_WHITE);
	ui_fill(cx + w - 1, cy + h - 1, 1, 1, UI_WHITE);
	text_centered(cx, cy, w, h, label, pressed_look ? UI_WHITE : fg);
	return result;
}

int
ui_checkbox(UiWindow *win, int cx, int cy, int x, int y, const char *label,
								int *value)
{
	int	w = 14 + ui_text_width(label) + 4;
	int	inside = ui_mouse_in(win, cx, cy, x, y, w, 14);
	long	id = widget_id(x, y);
	int	result = 0;

	if(inside && g_pressed) {
		g_active_widget = win;
		g_active_id = id;
	}
	if(g_active_widget == win && g_active_id == id && g_released) {
		if(inside) {
			*value = !*value;
			result = 1;
		}
		g_active_widget = NULL;
	}
	cx += x;
	cy += y;
	ui_fill(cx, cy + 1, 12, 12, UI_WHITE);
	ui_frame(cx, cy + 1, 12, 12, UI_BLACK);
	if(*value) {
		SDL_FPoint a[2];

		setcol(UI_BLACK);
		a[0].x = (float)cx + 3; a[0].y = (float)cy + 4;
		a[1].x = (float)cx + 9; a[1].y = (float)cy + 10;
		SDL_RenderLines(g_r, a, 2);
		a[0].x = (float)cx + 9; a[0].y = (float)cy + 4;
		a[1].x = (float)cx + 3; a[1].y = (float)cy + 10;
		SDL_RenderLines(g_r, a, 2);
	}
	ui_draw_text(cx + 18, cy - 1, label, UI_BLACK);
	return result;
}

#define SB_W	14		/* scroll bar width */

/* Optional columns for ui_list: items with tabs are split and each part is
 * drawn at the next column offset. Set before ui_list, cleared after. */
static const int	*g_list_cols;
static int		g_list_ncols;

void
ui_list_columns(const int *xs, int n)
{
	g_list_cols = xs;
	g_list_ncols = n;
}

static void
list_text(int x, int y, const char *s, Uint32 rgb)
{
	char	part[256];
	int	col = 0, n;
	const char *tab;

	if(!g_list_cols) {
		ui_draw_text(x, y, s, rgb);
		return;
	}
	while(s) {
		tab = strchr(s, '\t');
		n = tab ? (int)(tab - s) : (int)strlen(s);
		if(n > 255) n = 255;
		memcpy(part, s, (size_t)n);
		part[n] = 0;
		ui_draw_text(x + (col < g_list_ncols ? g_list_cols[col] : 0), y,
							part, rgb);
		col++;
		s = tab ? tab + 1 : NULL;
	}
}
#define ROW_H	18

int
ui_list(UiWindow *win, int cx, int cy, int x, int y, int w, int h,
		const char **items, int n_items, int *sel, int *top)
{
	int	rows = (h - 2) / ROW_H, i, result = 0, max_top;
	int	lx = cx + x, ly = cy + y;
	int	inside = ui_mouse_in(win, cx, cy, x, y, w - SB_W, h);

	max_top = n_items > rows ? n_items - rows : 0;
	if(*top > max_top) *top = max_top;
	if(*top < 0) *top = 0;

	/* click on a row */
	if(inside && g_pressed) {
		int row = ((int)g_my - (ly + 1)) / ROW_H + *top;

		if(row >= 0 && row < n_items) {
			*sel = row;
			result = g_dblclick ? 2 : 1;
		}
	}

	ui_fill(lx, ly, w, h, UI_WHITE);
	ui_frame(lx, ly, w, h, UI_BLACK);
	{
		SDL_Rect clip = { lx + 1, ly + 1, w - SB_W - 1, h - 2 };
		SDL_Rect old;
		int had = SDL_GetRenderClipRect(g_r, &old);

		SDL_SetRenderClipRect(g_r, &clip);
		for(i = 0; i < rows && *top + i < n_items; i++) {
			int ry = ly + 1 + i * ROW_H;
			int idx = *top + i;

			if(idx == *sel) {
				ui_fill(lx + 1, ry, w - SB_W - 1, ROW_H, UI_BLACK);
				list_text(lx + 4, ry + 1, items[idx], UI_WHITE);
			} else {
				list_text(lx + 4, ry + 1, items[idx], UI_BLACK);
			}
		}
		if(had) {
			SDL_SetRenderClipRect(g_r, &old);
		} else {
			SDL_SetRenderClipRect(g_r, NULL);
		}
	}

	ui_vscroll(win, cx, cy, x + w - SB_W, y, h, n_items, rows, top);
	return result;
}

int
ui_vscroll(UiWindow *win, int cx, int cy, int x, int y, int h,
		int total, int visible, int *top)
{
	int	max_top = total > visible ? total - visible : 0;
	int	tx = cx + x, ty0 = cy + y, trk = h - 2 * SB_W, th = 0, ty = 0, i;
	long	id = widget_id(x, y) ^ 0x5555;
	int	changed = 0, old = *top;

	if(*top > max_top) *top = max_top;
	if(*top < 0) *top = 0;
	if(g_wheel != 0 && ui_mouse_in(win, cx, cy, 0, 0, win->w - 2,
						win->h - UI_TITLE_H - 1)) {
		*top -= (g_wheel > 0 ? 1 : -1) * 2;
		g_wheel = 0;
	}
	if(total > visible && trk > 8) {
		th = trk * visible / total;
		if(th < 8) th = 8;
		ty = ty0 + SB_W + (trk - th) * (*top) / max_top;
	}
	if(g_pressed && ui_mouse_in(win, cx, cy, x, y, SB_W, h)) {
		if(g_my < ty0 + SB_W) {
			*top -= 1;
		} else if(g_my >= ty0 + h - SB_W) {
			*top += 1;
		} else if(th > 0) {
			if(g_my < ty) *top -= visible - 1;
			else if(g_my >= ty + th) *top += visible - 1;
			else {
				g_active_widget = win;
				g_active_id = id;
			}
		}
	}
	if(g_active_widget == win && g_active_id == id) {
		if(g_down && th > 0 && trk > th) {
			*top = (int)(((g_my - ty0 - SB_W - th / 2) * max_top) /
								(trk - th));
		}
		if(g_released) {
			g_active_widget = NULL;
		}
	}
	if(*top > max_top) *top = max_top;
	if(*top < 0) *top = 0;
	changed = (*top != old);

	/* draw */
	ui_fill(tx, ty0, SB_W, h, UI_WHITE);
	ui_vline(tx, ty0, h, UI_BLACK);
	ui_hline(tx, ty0, SB_W, UI_BLACK);
	ui_hline(tx, ty0 + h - 1, SB_W, UI_BLACK);
	ui_frame(tx, ty0, SB_W, SB_W, UI_BLACK);
	ui_frame(tx, ty0 + h - SB_W, SB_W, SB_W, UI_BLACK);
	for(i = 0; i < 4; i++) {
		ui_hline(tx + 7 - i - 1, ty0 + 4 + i, 2 * i + 2, UI_BLACK);
		ui_hline(tx + 7 - i - 1, ty0 + h - SB_W + 9 - i, 2 * i + 2,
								UI_BLACK);
	}
	if(th > 0) {
		ui_fill(tx + 1, ty, SB_W - 2, th, UI_BLACK);
	}
	return changed;
}

/* A row of n equal buttons; the selected one is drawn black. Bit i of
 * 'enabled' set means choice i can be clicked. Returns 1 when *sel changes. */
int
ui_segment(UiWindow *win, int cx, int cy, int x, int y, int w, int h,
	const char **labels, int n, int *sel, unsigned enabled)
{
	int	i, cw = w / n, changed = 0;

	for(i = 0; i < n; i++) {
		int	bx = x + i * cw, bw = (i == n - 1) ? w - i * cw : cw;
		int	on = (enabled >> i) & 1, chosen = (i == *sel);
		long	id = widget_id(bx, y) ^ 0x7777;
		int	inside = on && ui_mouse_in(win, cx, cy, bx, y, bw, h);

		if(inside && g_pressed) {
			g_active_widget = win;
			g_active_id = id;
		}
		if(g_active_widget == win && g_active_id == id && g_released) {
			if(inside && !chosen) {
				*sel = i;
				chosen = 1;
				changed = 1;
			}
			g_active_widget = NULL;
		}
		ui_fill(cx + bx, cy + y, bw, h, chosen ? UI_BLACK : UI_WHITE);
		ui_frame(cx + bx, cy + y, bw, h, on ? UI_BLACK : UI_LGREY);
		text_centered(cx + bx, cy + y, bw, h, labels[i],
				chosen ? UI_WHITE : (on ? UI_BLACK : UI_LGREY));
	}
	return changed;
}

/* 1 once, when a press that began inside the rect has moved far enough to
 * count as a drag (the pointer may have left the window since). */
int
ui_drag_started(UiWindow *win, int cx, int cy, int x, int y, int w, int h)
{
	float	dx = g_mx - g_press_x, dy = g_my - g_press_y;

	if(!g_down || g_drag_taken || g_cap != CAP_WIDGET || g_cap_win != win) {
		return 0;
	}
	if(!in_rect(g_press_x, g_press_y, cx + x, cy + y, w, h)) {
		return 0;
	}
	if(dx * dx + dy * dy < 36) {
		return 0;
	}
	g_drag_taken = 1;
	return 1;
}

/* Forget the current press (a native drag took over the mouse). */
void
ui_reset_press(void)
{
	g_down = 0;
	g_press_owned = 0;
	g_cap = CAP_NONE;
	g_cap_win = NULL;
	g_active_widget = NULL;
}

int
ui_hit(UiWindow *win, int cx, int cy, int x, int y, int w, int h)
{
	int	inside = ui_mouse_in(win, cx, cy, x, y, w, h);
	long	id = widget_id(x, y) ^ 0x3333;
	int	result = 0;

	if(inside && g_pressed) {
		g_active_widget = win;
		g_active_id = id;
	}
	if(g_active_widget == win && g_active_id == id && g_released) {
		result = inside;
		g_active_widget = NULL;
	}
	return result;
}

int
ui_drop_hover(UiWindow *win, int cx, int cy, int *x, int *y)
{
	if(!g_drop_active || win_at(g_drop_x, g_drop_y) != win) {
		return 0;
	}
	*x = (int)g_drop_x - cx;
	*y = (int)g_drop_y - cy;
	return 1;
}

static char	g_msg[96];
static Uint64	g_msg_until;

void
ui_message(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	vsnprintf(g_msg, sizeof(g_msg), fmt, ap);
	va_end(ap);
	g_msg_until = SDL_GetTicks() + 3000;
}

/* ------------------------------------------------------------------ */
/* drawing: windows, menu bar, status bar */

static void
draw_window(UiWindow *w, int active)
{
	int	x = w->x, y = w->y, i, tw, bx, by;
	SDL_Rect clip;

	ui_fill(x, y, w->w, w->h, UI_WHITE);
	tw = ui_text_width(w->title);
	switch(g_ui_theme.title_style) {
	case UI_TITLE_MAC1:			/* plain bar, centred title, square close box */
		ui_frame(x, y, w->w, w->h, UI_BLACK);
		ui_frame(x + 1, y + 1, w->w - 2, w->h - 2, UI_BLACK);
		ui_hline(x, y + UI_TITLE_H - 1, w->w, UI_BLACK);
		ui_draw_text(x + (w->w - tw) / 2, y + 2, w->title, UI_BLACK);
		if(w->closable) {
			close_box_rect(w, &bx, &by);
			ui_frame(bx, by + 1, 10, 10, UI_BLACK);
		}
		break;
	case UI_TITLE_PLATINUM:			/* grey bevel with ridged title bar */
		ui_fill(x, y, w->w, UI_TITLE_H, UI_LGREY);
		ui_frame(x, y, w->w, w->h, UI_BLACK);
		ui_hline(x + 1, y + 1, w->w - 2, UI_WHITE);
		ui_vline(x + 1, y + 1, w->h - 2, UI_WHITE);
		ui_hline(x, y + UI_TITLE_H - 1, w->w, UI_GREY);
		if(active) {
			for(i = y + 4; i < y + UI_TITLE_H - 4; i += 2) {
				ui_hline(x + 24, i, w->w - 48, UI_WHITE);
				ui_hline(x + 24, i + 1, w->w - 48, UI_GREY);
			}
		}
		ui_fill(x + (w->w - tw) / 2 - 8, y + 2, tw + 16, UI_TITLE_H - 5, UI_LGREY);
		ui_draw_text(x + (w->w - tw) / 2, y + 1, w->title, active ? UI_BLACK : UI_GREY);
		if(w->closable) {
			close_box_rect(w, &bx, &by);
			ui_fill(bx, by, 11, 11, UI_WHITE);
			ui_frame(bx, by, 11, 11, UI_GREY);
		}
		break;
	case UI_TITLE_FLAT:			/* solid coloured bar */
		ui_fill(x, y, w->w, UI_TITLE_H, g_ui_theme.accent);
		ui_frame(x, y, w->w, w->h, g_ui_theme.accent);
		ui_draw_text(x + (w->w - tw) / 2, y + 1, w->title, g_ui_theme.accent_fg);
		if(w->closable) {
			close_box_rect(w, &bx, &by);
			ui_frame(bx + 1, by + 1, 9, 9, g_ui_theme.accent_fg);
		}
		break;
	case UI_TITLE_MAGAZINE:			/* headline and a double rule, no bar */
		ui_frame(x, y, w->w, w->h, UI_LGREY);
		ui_draw_text(x + 12, y + 1, w->title, UI_BLACK);
		ui_hline(x + 8, y + UI_TITLE_H - 4, w->w - 16, UI_BLACK);
		ui_hline(x + 8, y + UI_TITLE_H - 2, w->w - 16, UI_BLACK);
		if(w->closable) {
			close_box_rect(w, &bx, &by);
			ui_draw_text(x + w->w - 22, y + 1, "x", UI_BLACK);
		}
		break;
	case UI_TITLE_TERMINAL: {		/* [ title ] and a plain rule */
		char	t[80];

		snprintf(t, sizeof(t), "[ %s ]", w->title);
		ui_frame(x, y, w->w, w->h, UI_BLACK);
		ui_draw_text(x + 10, y + 1, t, UI_BLACK);
		ui_hline(x, y + UI_TITLE_H - 1, w->w, UI_BLACK);
		if(w->closable) {
			ui_draw_text(x + w->w - 22, y + 1, "x", UI_BLACK);
		}
		break; }
	default:				/* the GS/OS look */
		ui_frame(x, y, w->w, w->h, UI_BLACK);
		ui_hline(x, y + UI_TITLE_H - 1, w->w, UI_BLACK);
		if(active) {
			for(i = y + 4; i < y + UI_TITLE_H - 4; i += 2) {
				ui_hline(x + 2, i, w->w - 4, UI_BLACK);
			}
		}
		ui_fill(x + (w->w - tw) / 2 - 6, y + 2, tw + 12, UI_TITLE_H - 5, UI_WHITE);
		ui_draw_text(x + (w->w - tw) / 2, y + 1, w->title, active ? UI_BLACK : UI_GREY);
		if(w->closable) {
			close_box_rect(w, &bx, &by);
			ui_fill(bx - 2, by - 2, 15, 15, UI_WHITE);
			ui_frame(bx, by, 11, 11, UI_BLACK);
		}
	}
	if(w->draw) {
		clip.x = x + 1;
		clip.y = y + UI_TITLE_H;
		clip.w = w->w - 2;
		clip.h = w->h - UI_TITLE_H - 1;
		SDL_SetRenderClipRect(g_r, &clip);
		w->draw(w, x + 1, y + UI_TITLE_H);
		SDL_SetRenderClipRect(g_r, NULL);
	}
}

static void
draw_logo(int x, int y)
{
	int	r, wd;

	for(r = 0; r < 11; r++) {
		wd = (r <= 5 ? r : 10 - r) * 2 + 1;
		ui_hline(x + 5 - wd / 2, y + r, wd, UI_BLACK);
	}
}

static void
draw_menu_bar(void)
{
	int	i, x, tw;

	if(UI_TOP_H == 0) {
		return;			/* the system's menu bar is used */
	}
	ui_fill(0, 0, g_lw, UI_BAR_H, CH_BG);
	ui_hline(0, UI_BAR_H - 1, g_lw, CH_LINE);
#ifdef UI_THEMABLE
	draw_logo(10, 4);
#else
	ui_draw_text(10, 1, "EmuBuddy", CH_BRAND);		/* bold, so it reads as the app's own bar */
	ui_draw_text(11, 1, "EmuBuddy", CH_BRAND);
#endif
	for(i = 0; i < g_app->n_menus; i++) {
		x = menu_title_x(i);
		tw = menu_title_w(i);
		if(i == g_menu_open) {
			ui_fill(x, 0, tw, UI_BAR_H - 1, CH_SEL_BG);
			ui_draw_text(x + MENU_PAD, 1, g_app->menus[i].title, CH_SEL_FG);
		} else {
			ui_draw_text(x + MENU_PAD, 1, g_app->menus[i].title, CH_FG);
		}
	}
}

static void
draw_dropdown(void)
{
	const UiMenu	*m;
	int		i, bx, by, bw, bh, y, hover, st;

	if(g_menu_open < 0) {
		return;
	}
	m = &g_app->menus[g_menu_open];
	bx = menu_title_x(g_menu_open) - MENU_PAD / 2;
	by = UI_BAR_H;
	bw = menu_box_w(m);
	bh = menu_box_h(m);
	hover = menu_item_at(g_mx, g_my);
	ui_fill(bx + 2, by + 2, bw, bh, CH_SHADOW);		/* shadow */
	ui_fill(bx, by, bw, bh, CH_BG);
	ui_frame(bx, by, bw, bh, CH_LINE);
	y = by + 1;
	for(i = 0; i < m->n_items; i++) {
		const UiMenuItem *it = &m->items[i];
		int h = menu_item_h(it);
		int dis, chk;
		Uint32 fg;

		if(it->flags & UI_MI_SEP) {
			ui_hline(bx + 4, y + h / 2, bw - 8, CH_LINE);
			y += h;
			continue;
		}
		st = g_app->item_state ? g_app->item_state(it->id) : 0;
		dis = (it->flags | st) & UI_MI_DISABLED;
		chk = (it->flags | st) & UI_MI_CHECKED;
		if(i == hover && !dis) {
			ui_fill(bx + 1, y, bw - 2, h, CH_SEL_BG);
			fg = CH_SEL_FG;
		} else {
			fg = dis ? CH_DIM : CH_FG;
		}
		if(chk) {
			SDL_FPoint p[3];

			SDL_SetRenderDrawColor(g_r, (fg >> 16) & 255,
					(fg >> 8) & 255, fg & 255, 255);
			p[0].x = (float)bx + 6; p[0].y = (float)y + 8;
			p[1].x = (float)bx + 9; p[1].y = (float)y + 11;
			p[2].x = (float)bx + 15; p[2].y = (float)y + 4;
			SDL_RenderLines(g_r, p, 3);
		}
		ui_draw_text(bx + 20, y + 1, it->label, fg);
		if(it->shortcut) {
			ui_draw_text(bx + bw - 8 - ui_text_width(it->shortcut),
							y + 1, it->shortcut, fg);
		}
		y += h;
	}
}

/* Shorten text to at most max pixels, ending in "..." when something is cut. */
static void
fit_text(char *text, size_t size, int max)
{
	size_t	n = strlen(text);

	if(ui_text_width(text) <= max) {
		return;
	}
	while(n > 0) {
		n--;
		while(n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) {
			n--;			/* don't cut a character in two */
		}
		if(n + 4 <= size) {
			memcpy(text + n, "...", 4);
			if(ui_text_width(text) <= max) {
				return;
			}
		}
	}
	text[0] = 0;
}

static void
draw_status_bar(void)
{
	char	l[96] = "", c[96] = "", r[96] = "";
	int	y = g_lh - UI_BAR_H, from, to, tw, x;

	if(g_ui_hide_status) {
		return;
	}

	ui_fill(0, y, g_lw, UI_BAR_H, CH_BG);
	ui_hline(0, y, g_lw, CH_LINE);
	if(g_app->status) {
		g_app->status(l, c, r, 96);
	}
	if(SDL_GetTicks() < g_msg_until) {
		snprintf(c, sizeof(c), "%s", g_msg);
	}
	ui_draw_text(8, y + 3, l, CH_FG);
	ui_draw_text(g_lw - 8 - ui_text_width(r), y + 3, r, CH_FG);
	/* The text in the middle stays between the left text and what is on the
	 * right (the app's own drawing, such as drive lights, and the right
	 * text): centred when there is room, shortened when there is not. */
	from = 8 + ui_text_width(l) + (l[0] ? 14 : 0);
	to = g_lw - 8 - ui_text_width(r) - 14;
	if(g_app->status_draw) {
		to = g_lw - 16 - ui_text_width(r) - g_app->status_draw_w - 14;
	}
	fit_text(c, sizeof(c), to - from);
	tw = ui_text_width(c);
	x = (g_lw - tw) / 2;
	if(x + tw > to) {
		x = to - tw;
	}
	if(x < from) {
		x = from;
	}
	ui_draw_text(x, y + 3, c, CH_FG);
	if(g_app->status_draw) {
		g_app->status_draw(0, y, g_lw - 16 - ui_text_width(r), UI_BAR_H);
	}
}

void
ui_draw(int lw, int lh)
{
	int		i;
	UiWindow	*w;

	g_lw = lw;
	g_lh = lh;
	SDL_SetRenderViewport(g_r, NULL);
	SDL_SetRenderClipRect(g_r, NULL);
	SDL_SetRenderDrawBlendMode(g_r, SDL_BLENDMODE_NONE);

	if(g_app->tick) {
		g_app->tick();
	}
	run_posted();
	if(g_app->cover_picture && g_app->cover_picture()) {
		ui_fill(0, UI_TOP_H, lw, lh - UI_TOP_H - UI_BOT_H, UI_BLACK);
	}
	{
		UiWindow *snap[UI_MAX_WINDOWS];
		int n = g_nwins;

		memcpy(snap, g_wins, sizeof(snap));
		for(i = 0; i < n; i++) {
			w = snap[i];
			if(w->open) {
				draw_window(w, i == n - 1);
			}
		}
	}
	draw_status_bar();
	draw_menu_bar();
	draw_dropdown();

	g_pressed = 0;
	g_released = 0;
	g_dblclick = 0;
	g_wheel = 0;
}
