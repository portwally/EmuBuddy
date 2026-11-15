/* EmuBuddy: the Disk Browser. Opens a ProDOS disk image (also one that is in
 * a drive), lists its files and folders, and gets files out: Export copies
 * the selection to the Desktop, and on macOS a file can be dragged straight
 * to the Finder. Files keep their ProDOS type in the name (",t$TT,a$AAAA" or
 * AppleSingle), the same naming shared folders use, so they round-trip. */
#include "ui.h"
#include "pathutil.h"
#include "prodos_img.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int	mac_drag_file_out(SDL_Window *win, const char *path);	/* ui_mac_drag.m */
SDL_Window *ui_window(void);

#define MAX_ENTRIES	512
#define MAX_DEPTH	16

static UiWindow	g_br_win;
static PImg	g_img;
static int	g_open;
static char	g_img_path[1100];
static unsigned	g_stack[MAX_DEPTH];		/* directory key blocks */
static char	g_names[MAX_DEPTH][16];
static int	g_depth;
static PEntry	g_ent[MAX_ENTRIES];
static char	*g_rows[MAX_ENTRIES];
static int	g_n, g_sel = -1, g_top;

static const char *
base_name(const char *p)
{
	return path_base(p);
}

static void
load_dir(void)
{
	int	i;
	char	buf[96];

	for(i = 0; i < g_n; i++) {
		free(g_rows[i]);
	}
	g_n = pimg_list(&g_img, g_stack[g_depth], g_ent, MAX_ENTRIES);
	if(g_n < 0) {
		g_n = 0;
	}
	for(i = 0; i < g_n; i++) {
		PEntry *e = &g_ent[i];

		if(e->storage == 0xD) {
			snprintf(buf, sizeof(buf), "%s/\tFolder\t", e->name);
		} else {
			unsigned kb = (e->eof + 1023) / 1024;

			snprintf(buf, sizeof(buf), "%s\t%s\t%u K", e->name,
					pimg_type_name(e->type), kb ? kb : 1);
		}
		g_rows[i] = strdup(buf);
	}
	g_sel = -1;
	g_top = 0;
}

void
ui_browser_open_image(const char *path)
{
	const char	*err;

	if(g_open) {
		pimg_close(&g_img);
		g_open = 0;
	}
	if(pimg_open(&g_img, path, &err) != 0) {
		ui_message("%s: %s", base_name(path), err);
		return;
	}
	g_open = 1;
	snprintf(g_img_path, sizeof(g_img_path), "%s", path);
	g_depth = 0;
	g_stack[0] = 2;
	snprintf(g_names[0], sizeof(g_names[0]), "%s", g_img.volume);
	load_dir();
	if(!g_br_win.open) {
		ui_browser_toggle();
	}
}

static int
export_tree(unsigned key, const char *dir, int *count)
{
	PEntry	*list = malloc(sizeof(PEntry) * MAX_ENTRIES);
	char	out[1400], sub[1400];
	int	n, i, bad = 0;

	if(!list) {
		return 1;
	}
	n = pimg_list(&g_img, key, list, MAX_ENTRIES);
	for(i = 0; i < n; i++) {
		if(list[i].storage == 0xD) {
			snprintf(sub, sizeof(sub), "%s/%s", dir, list[i].name);
			SDL_CreateDirectory(sub);
			bad += export_tree(list[i].key, sub, count);
		} else if(pimg_export(&g_img, &list[i], dir, out,
						(int)sizeof(out)) == 0) {
			(*count)++;
		} else {
			bad++;
		}
	}
	free(list);
	return bad;
}

/* Export the selection (or the whole folder being shown) into dir. */
static int
export_selection(const char *dir, char *made, int made_len)
{
	char	sub[1400];
	int	count = 0, bad;

	if(g_sel >= 0 && g_sel < g_n && g_ent[g_sel].storage != 0xD) {
		if(pimg_export(&g_img, &g_ent[g_sel], dir, made, made_len) != 0) {
			return -1;
		}
		return 1;
	}
	if(g_sel >= 0 && g_sel < g_n) {
		snprintf(sub, sizeof(sub), "%s/%s", dir, g_ent[g_sel].name);
		SDL_CreateDirectory(sub);
		bad = export_tree(g_ent[g_sel].key, sub, &count);
	} else {
		snprintf(sub, sizeof(sub), "%s/%s", dir, g_names[g_depth]);
		SDL_CreateDirectory(sub);
		bad = export_tree(g_stack[g_depth], sub, &count);
	}
	snprintf(made, (size_t)made_len, "%s", sub);
	return bad ? -1 : count;
}

static void
export_to_desktop(void)
{
	const char	*desk = SDL_GetUserFolder(SDL_FOLDER_DESKTOP);
	char		dir[1100], made[1400];
	int		r;
	size_t		n;

	if(!desk) {
		desk = SDL_GetUserFolder(SDL_FOLDER_HOME);
	}
	snprintf(dir, sizeof(dir), "%s", desk ? desk : ".");
	n = strlen(dir);
	if(n > 1 && dir[n - 1] == '/') {
		dir[n - 1] = 0;
	}
	r = export_selection(dir, made, (int)sizeof(made));
	if(r < 0) {
		ui_message("Export failed");
	} else {
		ui_message("Exported to Desktop: %s", base_name(made));
	}
}

/* Start a drag of the selected file to the desktop (macOS). */
static void
drag_out(void)
{
	char	*pref, dir[1100], made[1400];

	if(g_sel < 0 || g_sel >= g_n) {
		return;
	}
#ifdef __APPLE__
	pref = SDL_GetPrefPath("EmuBuddy", "EmuBuddy");
	snprintf(dir, sizeof(dir), "%sdrag-out", pref ? pref : "");
	SDL_free(pref);
	SDL_RemovePath(dir);		/* only removes it if empty */
	SDL_CreateDirectory(dir);
	if(export_selection(dir, made, (int)sizeof(made)) < 0) {
		ui_message("Could not export %s", g_ent[g_sel].name);
		return;
	}
	ui_reset_press();		/* the system drag owns the mouse now */
	if(mac_drag_file_out(ui_window(), made) != 0) {
		ui_message("Drag failed - use Export instead");
	}
#else
	(void)pref; (void)dir; (void)made;
	ui_message("Use Export - dragging out works on macOS for now");
#endif
}

static int
br_drop(UiWindow *win, int x, int y, const char *path)
{
	(void)win; (void)x; (void)y;
	ui_browser_open_image(path);
	return 1;
}

static void
image_picked(const char *path)
{
	ui_browser_open_image(path);
}

static void
br_draw(UiWindow *win, int cx, int cy)
{
	static const int cols[3] = { 0, 200, 270 };
	int	cw = win->w - 2, lh = win->h - UI_TITLE_H - 50 - 40, r, by, i;
	char	path[300];

	if(!g_open) {
		ui_draw_text(cx + 20, cy + 60, "Drop a ProDOS disk image here,",
								UI_GREY);
		ui_draw_text(cx + 20, cy + 78, "or click Open Image.", UI_GREY);
	} else {
		path[0] = 0;
		for(i = 0; i <= g_depth; i++) {
			strncat(path, "/", sizeof(path) - strlen(path) - 1);
			strncat(path, g_names[i], sizeof(path) - strlen(path) - 1);
		}
		ui_draw_text(cx + 8, cy + 6, path, UI_BLACK);
		ui_draw_text(cx + 12, cy + 26, "Name", UI_GREY);
		ui_draw_text(cx + 12 + cols[1], cy + 26, "Type", UI_GREY);
		ui_draw_text(cx + 12 + cols[2], cy + 26, "Size", UI_GREY);
		ui_list_columns(cols, 3);
		r = ui_list(win, cx, cy, 8, 46, cw - 16, lh,
				(const char **)g_rows, g_n, &g_sel, &g_top);
		ui_list_columns(NULL, 0);
		if(r == 2 && g_sel >= 0 && g_ent[g_sel].storage == 0xD &&
						g_depth + 1 < MAX_DEPTH) {
			g_depth++;
			g_stack[g_depth] = g_ent[g_sel].key;
			snprintf(g_names[g_depth], sizeof(g_names[0]), "%s",
							g_ent[g_sel].name);
			load_dir();
		} else if(ui_drag_started(win, cx, cy, 8, 46, cw - 16 - 14, lh)) {
			drag_out();
		}
	}
	by = win->h - UI_TITLE_H - 34;
	if(ui_button(win, cx, cy, 8, by, 110, 24, "Open Image", 1)) {
		ui_file_browser_open("Open Disk Image", image_picked);
	}
	if(ui_button(win, cx, cy, 126, by, 60, 24, "Up", g_open && g_depth > 0)) {
		g_depth--;
		load_dir();
	}
	if(ui_button(win, cx, cy, cw - 8 - 150, by, 150, 24, "Export to Desktop",
								g_open)) {
		export_to_desktop();
	}
}

int
ui_browser_is_open(void)
{
	return g_br_win.open;
}

void
ui_browser_toggle(void)
{
	if(g_br_win.open) {
		ui_window_close(&g_br_win);
		return;
	}
	snprintf(g_br_win.title, sizeof(g_br_win.title), "Disk Browser");
	g_br_win.w = 460;
	g_br_win.h = 360;
	g_br_win.closable = 1;
	g_br_win.modal = 0;
	g_br_win.draw = br_draw;
	g_br_win.on_drop = br_drop;
	if(g_br_win.x == 0 && g_br_win.y == 0) {
		ui_window_center(&g_br_win);
	}
	ui_window_open(&g_br_win);
}
