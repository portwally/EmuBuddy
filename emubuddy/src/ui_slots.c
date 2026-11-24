/* EmuBuddy: the Slots window. Shows what is plugged into each of the seven
 * IIgs slots and lets you change it. Choices are staged and applied with the
 * Apply button; changing slot 7 restarts the machine (the card ROM changes). */
#include "defc.h"
#include "ui.h"
#include "webgate.h"
#include "localtalk.h"
#include <stdio.h>
#include <string.h>

extern int	g_serial_cfg[2];
extern char	*g_serial_device[2];
extern int	g_voc_enable;
extern int	g_slot4_mockingboard;
extern int	g_slot7_scsi;
extern int	g_uthernet2_slot;
extern int	g_config_kegs_update_needed;
void		cfg_file_update_ptr(char **strptr, const char *str, int need_update);
void		scc_config_changed(int port, int cfg_changed, int remote_changed,
						int serial_dev_changed);

#define ROW_H	34

typedef struct {
	const char	*name;		/* what the slot is for */
	const char	*labels[4];
	int		n;		/* number of choices, 0 = fixed */
	unsigned	enabled;	/* bit i: choice i selectable */
	const char	*fixed;		/* text for fixed slots */
} SlotDef;

static const SlotDef g_slots[7] = {
	{ "Printer Port", { "Idle", "Modem", "ImageWriter", "PostScript" }, 4, 15, NULL },
	{ "Modem Port", { "Idle", "Modem", NULL, NULL }, 2, 3, NULL },
	{ "Network", { "Empty", "Video Overlay", "Uthernet II", NULL }, 3, 7, NULL },
	{ "Sound / Net", { "Empty", "Mockingboard", "Uthernet II", NULL }, 3, 7, NULL },
	{ "3.5\" Drives", { NULL, NULL, NULL, NULL }, 0, 0, "Built in (see Drives)" },
	{ "5.25\" Drives", { NULL, NULL, NULL, NULL }, 0, 0, "Built in (see Drives)" },
	{ "Storage", { "Empty", "SCSI Card", NULL, NULL }, 2, 3, NULL },
};

static UiWindow	g_slots_win;
static int	g_pend[7];

/* Current choice of each slot, from the core. -1 if it is something this
 * window doesn't show (for example a saved TCP serial setup). */
static int
slot_current(int i)
{
	switch(i) {
	case 0: return g_serial_cfg[0] == 5 ? 3 : g_serial_cfg[0] == 4 ? 2 :
						g_serial_cfg[0] == 1 ? 1 :
						(g_serial_cfg[0] == 0 ? 0 : -1);
	case 1: return g_serial_cfg[1] == 1 ? 1 : (g_serial_cfg[1] == 0 ? 0 : -1);
	case 2: return g_uthernet2_slot == 3 ? 2 : (g_voc_enable ? 1 : 0);
	case 3: return g_uthernet2_slot == 4 ? 2 : (g_slot4_mockingboard ? 1 : 0);
	case 6: return g_slot7_scsi ? 1 : 0;
	}
	return 0;
}

static void
load_pending(void)
{
	int	i;

	for(i = 0; i < 7; i++) {
		g_pend[i] = slot_current(i);
	}
}

static int
is_dirty(void)
{
	int	i;

	for(i = 0; i < 7; i++) {
		if(g_slots[i].n && g_pend[i] != slot_current(i)) {
			return 1;
		}
	}
	return 0;
}

static void
apply(void)
{
	int	restart = (g_pend[6] != slot_current(6)), p;

	for(p = 0; p < 2; p++) {
		if(g_pend[p] != slot_current(p) && g_pend[p] >= 0) {
			/* choice 2 = ImageWriter (mode 4), 3 = PostScript (mode 5) */
			g_serial_cfg[p] = (g_pend[p] == 2) ? 4 :
					(g_pend[p] == 3) ? 5 : g_pend[p];
			scc_config_changed(p, 1, 0, 0);	/* reopen in new mode */
			if(g_pend[p] == 0) {
				/* Idle: no real device either */
				cfg_file_update_ptr(&g_serial_device[p], "", 1);
			}
		}
	}
	g_voc_enable = g_pend[2] == 1;
	g_slot4_mockingboard = g_pend[3] == 1;
	g_uthernet2_slot = (g_pend[2] == 2) ? 3 : (g_pend[3] == 2) ? 4 : 0;
	if(g_pend[6] != slot_current(6)) {
		/* only when changed here: a High-Speed SCSI Card (2), set
		 * elsewhere, stays what it is */
		g_slot7_scsi = g_pend[6] > 0;
	}
	g_config_kegs_update_needed = 1;
	if(restart) {
		load_roms_init_memory();	/* new slot 7 card ROM */
		do_reset();
		ui_message("Slots applied - machine restarted");
	} else {
		ui_message("Slots applied");
	}
	load_pending();
}

static void
slots_draw(UiWindow *win, int cx, int cy)
{
	int	i, y, dirty = is_dirty(), cw = win->w - 2, by;
	char	label[16];

	for(i = 0; i < 7; i++) {
		const SlotDef *s = &g_slots[i];

		y = 6 + i * ROW_H;
		ui_hline(cx, cy + y + ROW_H - 3, cw, UI_LGREY);
		snprintf(label, sizeof(label), "Slot %d", i + 1);
		ui_draw_text(cx + 10, cy + y + 6, label, UI_BLACK);
		ui_draw_text(cx + 70, cy + y + 6, s->name, UI_BLACK);
		if(s->n == 0) {
			ui_draw_text(cx + 196, cy + y + 6, s->fixed, UI_GREY);
			continue;
		}
		if(ui_segment(win, cx, cy, 196, y + 2, cw - 196 - 10, 24,
				s->labels, s->n, &g_pend[i], s->enabled)) {
			/* there is one Uthernet II card: slot 3 or slot 4 */
			if(i == 2 && g_pend[2] == 2 && g_pend[3] == 2) {
				g_pend[3] = 0;
			} else if(i == 3 && g_pend[3] == 2 && g_pend[2] == 2) {
				g_pend[2] = 0;
			}
		}
	}
	by = 6 + 7 * ROW_H + 4;
	ui_draw_text(cx + 10, cy + by,
		"Cards also need \"Your Card\" for that slot in the", UI_GREY);
	ui_draw_text(cx + 10, cy + by + 17,
		"IIgs Control Panel. Slot 7 changes restart the machine.", UI_GREY);
	if(g_uthernet2_slot) {		/* where an IIgs browser finds the web gateway */
		ui_draw_text(cx + 10, cy + by + 34, webgate_status(), UI_GREY);
	}
	/* AppleTalk: the IIgs chooses the port itself (its Control Panel) */
	ui_draw_text(cx + 10, cy + by + 51, localtalk_status(), UI_GREY);
	by += 17 + 17 + 17 + 17 + 8;
	if(ui_button(win, cx, cy, cw - 10 - 80, by, 80, 24, "Apply", dirty)) {
		apply();
	}
	if(ui_button(win, cx, cy, cw - 10 - 80 - 8 - 80, by, 80, 24, "Revert",
								dirty)) {
		load_pending();
	}
}

int
ui_slots_is_open(void)
{
	return g_slots_win.open;
}

void
ui_slots_toggle(void)
{
	if(g_slots_win.open) {
		ui_window_close(&g_slots_win);
		return;
	}
	load_pending();
	snprintf(g_slots_win.title, sizeof(g_slots_win.title), "Slots");
	g_slots_win.w = 580;
	g_slots_win.h = UI_TITLE_H + 6 + 7 * ROW_H + 4 + 68 + 8 + 24 + 12;
	g_slots_win.closable = 1;
	g_slots_win.modal = 0;
	g_slots_win.draw = slots_draw;
	if(g_slots_win.x == 0 && g_slots_win.y == 0) {
		ui_window_center(&g_slots_win);
	}
	ui_window_open(&g_slots_win);
}
