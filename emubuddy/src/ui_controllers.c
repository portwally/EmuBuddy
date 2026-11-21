/* EmuBuddy: the Controllers window (Machine > Controllers...). Shows the
 * game controllers and joysticks the computer has, the stick and buttons of
 * the one in use as they move, and what the IIgs program actually reads:
 * the joystick X and Y (0-255) and when it last asked. Also chooses what
 * the IIgs joystick is (Auto / Controller / Keypad / Mouse) and whether
 * buttons 0 and 1 are swapped; both are saved in emubuddy.config. */
#include "defc.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

extern int	g_joystick_type;	/* paddles.c: 4 Auto 2 native 0 keypad 1 mouse */
extern int	g_joystick_swap_buttons;
extern int	g_paddle_seen[2];
extern dword64	g_paddle_trig_count;
extern int	g_config_kegs_update_needed;

#define BOX	132		/* the stick picture */
#define MAXDEV	4

static UiWindow	g_ctl_win;
static dword64	g_last_count;
static Uint64	g_last_read_ms;		/* SDL ticks of the last read seen, 0 none */
static int	g_min[2], g_max[2];	/* range the program got while open */

static const char *g_use_labels[4] = { "Auto", "Controller", "Keypad", "Mouse" };
static const int g_use_type[4] = { 4, 2, 0, 1 };
static const char *g_swap_labels[2] = { "Normal", "Swapped" };

static void
reset_range(void)
{
	g_min[0] = g_min[1] = 256;
	g_max[0] = g_max[1] = -1;
}

/* Joystick value -32768..32767 to a position 0..BOX-1 in the picture */
static int
pos(int v)
{
	return (int)(((long)v + 32768) * (BOX - 1) / 65535);
}

static void
draw_button(int x, int y, int w, const char *text, int on)
{
	ui_fill(x, y, w, 24, on ? UI_BLACK : UI_WHITE);
	ui_frame(x, y, w, 24, UI_BLACK);
	ui_draw_text(x + (w - ui_text_width(text)) / 2, y + 4, text,
						on ? UI_WHITE : UI_BLACK);
}

/* A small D-pad: four cells around a centre, filled while pressed */
static void
draw_dpad(int x, int y, int dpad)
{
	static const int cell[4][3] = {	/* bit, column, row */
		{ 1, 1, 0 }, { 2, 2, 1 }, { 4, 1, 2 }, { 8, 0, 1 } };
	int	i, cx, cy;

	for(i = 0; i < 4; i++) {
		cx = x + cell[i][1] * 13;
		cy = y + cell[i][2] * 13;
		ui_fill(cx, cy, 12, 12, (dpad & cell[i][0]) ? UI_BLACK : UI_WHITE);
		ui_frame(cx, cy, 12, 12, UI_BLACK);
	}
}

static const char *
source_name(void)
{
	switch(paddle_effective_joystick_type()) {
	case 0: return "the keypad";
	case 1: return "the mouse";
	default: return "the controller";
	}
}

static void
ctl_draw(UiWindow *win, int cx, int cy)
{
	char	devs[MAXDEV][80], line[120];
	JoyInfo	ji;
	int	cw = win->w - 2, n, used, i, y, have, sel, bx, by, px, py;
	Uint64	now = SDL_GetTicks();

	/* What the IIgs program reads */
	if(g_paddle_trig_count != g_last_count) {
		g_last_count = g_paddle_trig_count;
		g_last_read_ms = now;
		for(i = 0; i < 2; i++) {
			if(g_paddle_seen[i] < g_min[i]) g_min[i] = g_paddle_seen[i];
			if(g_paddle_seen[i] > g_max[i]) g_max[i] = g_paddle_seen[i];
		}
	}

	/* Connected */
	ui_draw_text(cx + 10, cy + 8, "Connected", UI_BLACK);
	n = joystick_list(devs, MAXDEV, &used);
	if(n == 0) {
		ui_draw_text(cx + 110, cy + 8, "No controller or joystick", UI_GREY);
		ui_draw_text(cx + 110, cy + 25, "The numeric keypad is the joystick",
								UI_GREY);
	}
	for(i = 0; i < n && i < 3; i++) {
		y = cy + 8 + i * 17;
		if(i == used) {
			ui_fill(cx + 110, y + 5, 6, 6, UI_BLACK);
		} else {
			ui_frame(cx + 110, y + 5, 6, 6, UI_GREY);
		}
		ui_draw_text(cx + 122, y, devs[i], i == used ? UI_BLACK : UI_GREY);
	}
	ui_hline(cx, cy + 62, cw, UI_LGREY);

	/* The stick */
	have = joystick_info(&ji);
	bx = cx + 10;
	by = cy + 72;
	ui_fill(bx, by, BOX, BOX, UI_WHITE);
	ui_hline(bx, by + BOX / 2, BOX, UI_LGREY);
	ui_vline(bx + BOX / 2, by, BOX, UI_LGREY);
	ui_frame(bx, by, BOX, BOX, UI_BLACK);
	/* hollow: where the IIgs program last saw the joystick */
	if(g_last_read_ms) {
		px = 6 + g_paddle_seen[0] * (BOX - 13) / 255;	/* inside the box */
		py = 6 + g_paddle_seen[1] * (BOX - 13) / 255;
		ui_frame(bx + px - 6, by + py - 6, 13, 13, UI_GREY);
	}
	/* filled: the controller now */
	if(have) {
		px = pos(ji.x);
		py = pos(ji.y);
		px = px < 4 ? 4 : px > BOX - 5 ? BOX - 5 : px;
		py = py < 4 ? 4 : py > BOX - 5 ? BOX - 5 : py;
		ui_fill(bx + px - 4, by + py - 4, 9, 9, UI_BLACK);
		snprintf(line, sizeof(line), "X %d  Y %d",
			(int)(((long)ji.x + 32768) * 255 / 65535),
			(int)(((long)ji.y + 32768) * 255 / 65535));
	} else {
		snprintf(line, sizeof(line), "No controller");
	}
	ui_draw_text(bx, by + BOX + 4, line, UI_GREY);

	/* Buttons and D-pad */
	{
		int	x = cx + 160, w = 110;

		ui_draw_text(x, cy + 72, "Buttons", UI_BLACK);
		snprintf(line, sizeof(line), have ? "0 = %s" : "0", ji.label[0]);
		draw_button(x, cy + 92, w, line, have && (ji.buttons & 1));
		snprintf(line, sizeof(line), have ? "1 = %s" : "1", ji.label[1]);
		draw_button(x + w + 8, cy + 92, w, line, have && (ji.buttons & 2));
		ui_draw_text(x + 2 * w + 24, cy + 72, "D-pad", UI_BLACK);
		draw_dpad(x + 2 * w + 26, cy + 90, have ? ji.dpad : 0);
	}

	/* What the IIgs sees */
	y = cy + 132;
	ui_draw_text(cx + 160, y, "What the IIgs program reads", UI_BLACK);
	snprintf(line, sizeof(line), "Joystick from %s: X %d, Y %d", source_name(),
					g_paddle_seen[0], g_paddle_seen[1]);
	ui_draw_text(cx + 160, y + 18, line, UI_GREY);
	if(!g_last_read_ms) {
		snprintf(line, sizeof(line), "Not read since this window opened");
	} else if(now - g_last_read_ms < 1000) {
		snprintf(line, sizeof(line), "Reading it now");
	} else {
		snprintf(line, sizeof(line), "Last read %d s ago",
					(int)((now - g_last_read_ms) / 1000));
	}
	ui_draw_text(cx + 160, y + 35, line, UI_GREY);
	if(g_max[0] >= 0) {
		snprintf(line, sizeof(line), "Range so far: X %d-%d, Y %d-%d",
				g_min[0], g_max[0], g_min[1], g_max[1]);
		ui_draw_text(cx + 160, y + 52, line, UI_GREY);
	}
	ui_hline(cx, cy + 228, cw, UI_LGREY);

	/* Settings */
	y = cy + 236;
	ui_draw_text(cx + 10, y + 4, "Joystick", UI_BLACK);
	sel = -1;
	for(i = 0; i < 4; i++) {
		if(g_use_type[i] == g_joystick_type) {
			sel = i;
		}
	}
	if(ui_segment(win, cx, cy, 110, 236, cw - 110 - 10, 24, g_use_labels, 4,
								&sel, 15)) {
		g_joystick_type = g_use_type[sel];
		g_config_kegs_update_needed = 1;
		ui_message("The joystick is now %s",
			sel == 0 ? "the controller, or the keypad without one" :
			sel == 1 ? "the controller" :
			sel == 2 ? "the numeric keypad" : "the mouse");
	}
	ui_draw_text(cx + 10, y + 38, "Buttons", UI_BLACK);
	sel = g_joystick_swap_buttons ? 1 : 0;
	if(ui_segment(win, cx, cy, 110, 270, 220, 24, g_swap_labels, 2, &sel, 3)) {
		g_joystick_swap_buttons = sel;
		g_config_kegs_update_needed = 1;
	}
	ui_draw_text(cx + 10, cy + 306,
		"Move the stick or press a button to see it here.", UI_GREY);
	if(ui_button(win, cx, cy, cw - 10 - 80, 330, 80, 24, "Done", 1)) {
		ui_window_close(win);
	}
}

int
ui_controllers_is_open(void)
{
	return g_ctl_win.open;
}

void
ui_controllers_toggle(void)
{
	if(g_ctl_win.open) {
		ui_window_close(&g_ctl_win);
		return;
	}
	g_last_count = g_paddle_trig_count;
	g_last_read_ms = 0;
	reset_range();
	snprintf(g_ctl_win.title, sizeof(g_ctl_win.title), "Controllers");
	g_ctl_win.w = 580;
	g_ctl_win.h = UI_TITLE_H + 330 + 24 + 12;
	g_ctl_win.closable = 1;
	g_ctl_win.modal = 0;
	g_ctl_win.draw = ctl_draw;
	if(g_ctl_win.x == 0 && g_ctl_win.y == 0) {
		ui_window_center(&g_ctl_win);
	}
	ui_window_open(&g_ctl_win);
}
