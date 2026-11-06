/* EmuBuddy: the Drives window, disk insert/eject, part chooser and the
 * recent-disks list. Slot 7 is presented as a SCSI card with 7 hard drives
 * (the core's SmartPort/ProDOS block-device controller). Hard disk images
 * (.2mg, .hdv, .po, ...) are read by the core; this file is the UI for them. */
#include "defc.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* core */
int		iwm_motor_busy(void);
void		iwm_toggle_lock(Disk *dsk);
int		cfg_guess_slot(const char *filename);
int		cfg_ui_partition_count(void);
const char	*cfg_ui_partition_name(int i);
int		cfg_ui_partition_choose(int pos, int slot, int drive,
							const char *path);
extern int	g_cfg_select_partition;
extern int	g_cfg_slotdrive;
extern int	g_slot7_scsi;

#define N_RECENT	10
#define SCSI_DRIVES	7	/* the SCSI card has 7 hard drives (S7D1-S7D7) */

static const char *
base_name(const char *path)
{
	const char *p = strrchr(path, '/');

	return p ? p + 1 : path;
}

/* ------------------------------------------------------------------ */
/* Recent disks, saved in the per-user preferences folder */

static char	*g_recent[N_RECENT];
static int	g_n_recent;

static void
recent_file(char *buf, size_t n)
{
	char	*pref = SDL_GetPrefPath("EmuBuddy", "EmuBuddy");

	snprintf(buf, n, "%srecent.txt", pref ? pref : "");
	SDL_free(pref);
}

static void
recent_save(void)
{
	char	path[1024];
	FILE	*f;
	int	i;

	recent_file(path, sizeof(path));
	f = fopen(path, "w");
	if(!f) {
		return;
	}
	for(i = 0; i < g_n_recent; i++) {
		fprintf(f, "%s\n", g_recent[i]);
	}
	fclose(f);
}

static void
recent_load(void)
{
	char	path[1024], line[1100];
	FILE	*f;

	recent_file(path, sizeof(path));
	f = fopen(path, "r");
	if(!f) {
		return;
	}
	while(g_n_recent < N_RECENT && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if(line[0]) {
			g_recent[g_n_recent++] = strdup(line);
		}
	}
	fclose(f);
}

static void
recent_add(const char *path)
{
	int	i, j;

	for(i = 0; i < g_n_recent; i++) {
		if(!strcmp(g_recent[i], path)) {
			break;
		}
	}
	if(i == g_n_recent) {
		if(g_n_recent == N_RECENT) {
			free(g_recent[--g_n_recent]);
		}
		i = g_n_recent++;
		g_recent[i] = strdup(path);
	}
	for(j = i; j > 0; j--) {
		char *t = g_recent[j];

		g_recent[j] = g_recent[j - 1];
		g_recent[j - 1] = t;
	}
	recent_save();
}

int
ui_recent_count(void)
{
	return g_n_recent;
}

const char *
ui_recent_label(int i)
{
	return (i >= 0 && i < g_n_recent) ? base_name(g_recent[i]) : "";
}

/* ------------------------------------------------------------------ */
/* Insert / eject */

static int
path_mounted_anywhere(const char *path)
{
	int	slot, d, nd;
	Disk	*dsk;

	for(slot = 5; slot <= 7; slot++) {
		nd = (slot == 7) ? MAX_C7_DISKS : 2;
		for(d = 0; d < nd; d++) {
			dsk = iwm_get_dsk_from_slot_drive(slot, d);
			if(dsk->fd >= 0 && dsk->name_ptr &&
						!strcmp(dsk->name_ptr, path)) {
				return 1;
			}
		}
	}
	return 0;
}

/* The part chooser, for images that hold several partitions. */
static UiWindow	g_part_win;
static int	g_part_slot, g_part_drive, g_part_sel, g_part_top;
static char	g_part_path[1100];

static void
part_draw(UiWindow *win, int cx, int cy)
{
	static const char *names[512];
	int	n = cfg_ui_partition_count(), i, w = win->w - 2, r, by;

	if(n > 512) {
		n = 512;
	}
	for(i = 0; i < n; i++) {
		names[i] = cfg_ui_partition_name(i);
	}
	ui_draw_text(cx + 8, cy + 6, "This image has several parts. Choose one:",
								UI_BLACK);
	r = ui_list(win, cx, cy, 8, 28, w - 16, win->h - UI_TITLE_H - 28 - 44,
				names, n, &g_part_sel, &g_part_top);
	by = win->h - UI_TITLE_H - 34;
	if(ui_button(win, cx, cy, w - 8 - 70 - 8 - 70, by, 70, 24, "Cancel", 1)) {
		g_cfg_select_partition = -1;
		g_cfg_slotdrive = 0;
		ui_window_close(win);
		return;
	}
	if(r == 2 || ui_button(win, cx, cy, w - 8 - 70, by, 70, 24, "Insert",
							g_part_sel >= 0)) {
		if(g_part_sel >= 0 && g_part_sel < n) {
			if(cfg_ui_partition_choose(g_part_sel, g_part_slot,
					g_part_drive, g_part_path)) {
				recent_add(g_part_path);
				ui_message("Inserted in S%dD%d", g_part_slot,
							g_part_drive + 1);
				ui_window_close(win);
			} else {
				g_part_sel = -1;	/* entered a folder */
				g_part_top = 0;
			}
		}
	}
}

static void
part_open(int slot, int drive, const char *path)
{
	g_part_slot = slot;
	g_part_drive = drive;
	snprintf(g_part_path, sizeof(g_part_path), "%s", path);
	g_part_sel = -1;
	g_part_top = 0;
	snprintf(g_part_win.title, sizeof(g_part_win.title), "Choose Part");
	g_part_win.w = 400;
	g_part_win.h = 300;
	g_part_win.closable = 0;
	g_part_win.modal = 1;
	g_part_win.draw = part_draw;
	ui_window_center(&g_part_win);
	ui_window_open(&g_part_win);
}

static int
is_folder(const char *path)
{
	struct stat	sb;

	return stat(path, &sb) == 0 && S_ISDIR(sb.st_mode);
}

/* Blocks for a folder volume in this kind of drive. */
static unsigned
folder_blocks(int slot)
{
	return slot == 5 ? 1600 : slot == 6 ? 280 : 65535;
}

/* Mount a Mac/PC folder as a live ProDOS volume in a drive. */
static void
folder_mount(int slot, int drive, const char *path)
{
	Disk	*dsk = iwm_get_dsk_from_slot_drive(slot, drive);
	char	clean[1100];
	size_t	n;

	snprintf(clean, sizeof(clean), "%s", path);
	n = strlen(clean);
	while(n > 1 && clean[n - 1] == '/') {
		clean[--n] = 0;
	}
	if(iwm_motor_busy() && dsk->fd >= 0) {
		ui_message("Drive is busy - try again in a moment");
		return;
	}
	insert_disk(slot, drive, clean, 0, 0, -1, folder_blocks(slot));
	if(dsk->fd >= 0) {
		ui_message("Shared folder %s is in S%dD%d", base_name(clean),
							slot, drive + 1);
	} else {
		ui_message("Can't share %s (too big for a %s volume?)",
			base_name(clean), slot == 7 ? "32 MB" : "floppy");
	}
}

/* Put a disk image into a specific drive. Returns 1 (handled). */
static int
drive_insert(int slot, int drive, const char *path)
{
	Disk		*dsk = iwm_get_dsk_from_slot_drive(slot, drive);
	struct stat	sb;

	if(stat(path, &sb) < 0) {
		ui_message("Cannot open %s", base_name(path));
		return 1;
	}
	if(slot == 7 && !g_slot7_scsi) {
		ui_message("No SCSI card - plug it in under Machine > Slots");
		return 1;
	}
	if(S_ISDIR(sb.st_mode)) {
		folder_mount(slot, drive, path);
		return 1;
	}
	if(!ui_is_disk_image(path)) {
		/* a plain file: copy it into the folder in this drive */
		if(dsk->fd >= 0 && dsk->dynapro_info_ptr) {
			return ui_drop_regular_file(path);
		}
		ui_message("%s is not a disk image", base_name(path));
		return 1;
	}
	if(iwm_motor_busy() && (dsk->fd >= 0 || path_mounted_anywhere(path))) {
		ui_message("Drive is busy - try again in a moment");
		return 1;
	}
	if(cfg_maybe_insert_disk(slot, drive, path)) {
		recent_add(path);
		ui_message("Inserted %s in S%dD%d", base_name(path), slot,
								drive + 1);
	} else {
		part_open(slot, drive, path);
	}
	return 1;
}

/* Choose a drive for a disk image: the first empty drive of the right kind. */
void
ui_insert_auto(const char *path)
{
	int	slot, d, nd;

	if(is_folder(path)) {
		/* a folder becomes a hard disk volume on the SCSI card */
		if(!g_slot7_scsi) {
			ui_message("No SCSI card - plug it in under Machine > Slots");
			return;
		}
		for(d = 0; d < SCSI_DRIVES; d++) {
			if(iwm_get_dsk_from_slot_drive(7, d)->fd < 0) {
				break;
			}
		}
		if(d == SCSI_DRIVES) {
			ui_message("All 7 SCSI drives are in use");
			return;
		}
		folder_mount(7, d, path);
		return;
	}
	if(!ui_is_disk_image(path)) {
		ui_drop_regular_file(path);
		return;
	}
	slot = cfg_guess_slot(path);

	if(slot == 0) {
		ui_message("Not a disk image: %s", base_name(path));
		return;
	}
	if(slot == 7 && !g_slot7_scsi) {
		ui_message("No SCSI card - plug it in under Machine > Slots");
		return;
	}
	nd = (slot == 7) ? SCSI_DRIVES : 2;
	for(d = 0; d < nd; d++) {
		if(iwm_get_dsk_from_slot_drive(slot, d)->fd < 0) {
			break;
		}
	}
	if(d == nd) {
		d = 0;			/* all full: replace the first */
	}
	drive_insert(slot, d, path);
}

void
ui_recent_activate(int i)
{
	if(i >= 0 && i < g_n_recent) {
		ui_insert_auto(g_recent[i]);
	}
}

static void
drive_eject(int slot, int drive)
{
	Disk	*dsk = iwm_get_dsk_from_slot_drive(slot, drive);

	if(dsk->fd < 0) {
		return;
	}
	if(iwm_motor_busy()) {
		ui_message("Drive is busy - try again in a moment");
		return;
	}
	iwm_eject_disk_by_num(slot, drive);
	ui_message("Ejected S%dD%d", slot, drive + 1);
}

void
ui_eject_all(void)
{
	int	slot, d, nd;

	if(iwm_motor_busy()) {
		ui_message("Drive is busy - try again in a moment");
		return;
	}
	for(slot = 5; slot <= 7; slot++) {
		nd = (slot == 7) ? MAX_C7_DISKS : 2;
		for(d = 0; d < nd; d++) {
			if(iwm_get_dsk_from_slot_drive(slot, d)->fd >= 0) {
				iwm_eject_disk_by_num(slot, d);
			}
		}
	}
	ui_message("All disks ejected");
}

/* ------------------------------------------------------------------ */
/* Drives window */

#define DR_ROW_H	26
#define DR_VISIBLE	11		/* 2 + 2 floppies, 7 SCSI drives */
#define DR_TOP		4
#define DR_SB_W		14

typedef struct { int slot, drive; } DriveRow;
static DriveRow	g_rows[40];
static int	g_nrows;
static UiWindow	g_drives_win;
static int	g_drives_top;
static int	g_pick_slot, g_pick_drive;

static void
rows_build(void)
{
	int	d;

	g_nrows = 0;
	for(d = 0; d < 2; d++) {
		g_rows[g_nrows].slot = 5; g_rows[g_nrows++].drive = d;
	}
	for(d = 0; d < 2; d++) {
		g_rows[g_nrows].slot = 6; g_rows[g_nrows++].drive = d;
	}
	for(d = 0; d < MAX_C7_DISKS && g_nrows < 40; d++) {
		/* the 7 SCSI drives always; others only if a saved setup has one;
		 * none if the SCSI card is unplugged (Machine > Slots) */
		if(g_slot7_scsi && (d < SCSI_DRIVES ||
				iwm_get_dsk_from_slot_drive(7, d)->fd >= 0)) {
			g_rows[g_nrows].slot = 7;
			g_rows[g_nrows++].drive = d;
		}
	}
}

static void
fit_text(char *out, size_t n, const char *s, int max_w)
{
	snprintf(out, n, "%s", s);
	while(ui_text_width(out) > max_w && strlen(out) > 4) {
		memmove(out + 3, out + 4, strlen(out + 4) + 1);
		memcpy(out, "...", 3);
	}
}

static void
drive_pick(const char *path)
{
	drive_insert(g_pick_slot, g_pick_drive, path);
}

static int
drives_row_at(int y)
{
	int	r = (y - DR_TOP) / DR_ROW_H + g_drives_top;

	if(y < DR_TOP || y >= DR_TOP + DR_VISIBLE * DR_ROW_H ||
						r < 0 || r >= g_nrows) {
		return -1;
	}
	return r;
}

static int
drives_drop(UiWindow *win, int x, int y, const char *path)
{
	int	r = drives_row_at(y);

	if(r < 0 || x >= win->w - 2 - DR_SB_W) {
		return 0;			/* not on a row: auto-place */
	}
	return drive_insert(g_rows[r].slot, g_rows[r].drive, path);
}

static void
drives_draw(UiWindow *win, int cx, int cy)
{
	int	cw = win->w - 2, i, r, y, hx, hy, drop_row = -1;
	int	eject_x = cw - DR_SB_W - 4 - 54;
	int	lock_x = eject_x - 8 - 52;
	int	name_x = 84, name_w = lock_x - 6 - name_x;
	int	bottom = DR_TOP + DR_VISIBLE * DR_ROW_H + 6;
	char	label[16], shown[300];

	rows_build();
	if(g_drives_top > g_nrows - DR_VISIBLE) {
		g_drives_top = g_nrows > DR_VISIBLE ? g_nrows - DR_VISIBLE : 0;
	}
	if(ui_drop_hover(win, cx, cy, &hx, &hy) && hx < cw - DR_SB_W) {
		drop_row = drives_row_at(hy);
	}

	for(i = 0; i < DR_VISIBLE; i++) {
		Disk	*dsk;
		int	slot, drive, occupied;
		const char *kind, *name;

		r = g_drives_top + i;
		y = DR_TOP + i * DR_ROW_H;
		if(r >= g_nrows) {
			break;
		}
		slot = g_rows[r].slot;
		drive = g_rows[r].drive;
		dsk = iwm_get_dsk_from_slot_drive(slot, drive);
		occupied = dsk->fd >= 0;
		kind = (slot == 5) ? "3.5" : (slot == 6) ? "5.25" : "HD";

		if(r == drop_row) {
			ui_fill(cx, cy + y, cw - DR_SB_W, DR_ROW_H, UI_BLACK);
			ui_fill(cx + 2, cy + y + 2, cw - DR_SB_W - 4,
						DR_ROW_H - 4, UI_WHITE);
		}
		ui_hline(cx, cy + y + DR_ROW_H - 1, cw - DR_SB_W, UI_LGREY);
		if(slot == 7 && drive == 0) {
			/* SCSI card starts here */
			ui_hline(cx, cy + y, cw - DR_SB_W, UI_BLACK);
		}
		ui_frame(cx + 6, cy + y + 3, 32, 20, UI_BLACK);
		if(occupied) {
			ui_fill(cx + 7, cy + y + 4, 30, 18, UI_BLACK);
		}
		ui_draw_text(cx + 6 + (32 - ui_text_width(kind)) / 2, cy + y + 5,
				kind, occupied ? UI_WHITE : UI_BLACK);
		snprintf(label, sizeof(label), "S%dD%d", slot, drive + 1);
		ui_draw_text(cx + 44, cy + y + 5, label, UI_BLACK);

		if(occupied) {
			name = dsk->name_ptr ? base_name(dsk->name_ptr) : "?";
			if(dsk->dynapro_info_ptr) {
				snprintf(shown, sizeof(shown), "Folder: %s", name);
				fit_text(shown, sizeof(shown), shown, name_w);
			} else {
				fit_text(shown, sizeof(shown), name, name_w);
			}
			ui_draw_text(cx + name_x, cy + y + 5, shown, UI_BLACK);
		} else {
			ui_draw_text(cx + name_x, cy + y + 5, "(empty)", UI_GREY);
		}
		/* click the name area to choose a disk for this drive */
		if(ui_hit(win, cx, cy, 0, y, lock_x - 4, DR_ROW_H - 1)) {
			g_pick_slot = slot;
			g_pick_drive = drive;
			ui_file_browser_open((slot == 7) ?
				"Choose Hard Disk Image" : "Choose Disk Image",
				drive_pick);
		}
		/* write protect */
		if(occupied && dsk->write_prot != 2) {
			int v = dsk->write_prot != 0;

			if(ui_checkbox(win, cx, cy, lock_x, y + 5, "Lock", &v)) {
				iwm_toggle_lock(dsk);
			}
		} else {
			ui_draw_text(cx + lock_x + 18, cy + y + 5, "Lock",
				occupied ? UI_BLACK : UI_LGREY);
			if(occupied) {		/* read-only file: locked */
				ui_fill(cx + lock_x + 2, cy + y + 8, 10, 10,
								UI_BLACK);
			}
		}
		if(ui_button(win, cx, cy, eject_x, y + 2, 54, DR_ROW_H - 5,
							"Eject", occupied)) {
			drive_eject(slot, drive);
		}
	}
	if(!g_slot7_scsi) {
		y = DR_TOP + g_nrows * DR_ROW_H + 14;
		ui_draw_text(cx + 20, cy + y, "Slot 7 is empty: no SCSI card.", UI_GREY);
		ui_draw_text(cx + 20, cy + y + 18,
				"Plug it in under Machine > Slots.", UI_GREY);
	}
	ui_vscroll(win, cx, cy, cw - DR_SB_W, DR_TOP, DR_VISIBLE * DR_ROW_H,
			g_nrows, DR_VISIBLE, &g_drives_top);
	if(ui_button(win, cx, cy, 8, bottom, 80, 24, "Eject All", 1)) {
		ui_eject_all();
	}
	if(ui_button(win, cx, cy, 96, bottom, 120, 24, "Share Folder", 1)) {
		ui_shared_folder_choose();
	}
	ui_draw_text(cx + 226, cy + bottom + 4, "Drop disks or folders here.",
								UI_GREY);
}

/* A shared folder being refreshed: ejected now, put back after a pause so
 * GS/OS sees the disk go away first (an instant swap makes the Finder
 * think there are two volumes with the same name). */
#define REMOUNT_FRAMES	180		/* 3 seconds */
static struct {
	int		active, slot, drive, frames;
	unsigned	blocks;
	char		path[1100];
} g_remount;

void
ui_drives_tick(void)
{
	if(!g_remount.active || --g_remount.frames > 0) {
		return;
	}
	if(iwm_motor_busy()) {
		g_remount.frames = 30;		/* try again shortly */
		return;
	}
	g_remount.active = 0;
	insert_disk(g_remount.slot, g_remount.drive, g_remount.path, 0, 0, -1,
							g_remount.blocks);
	ui_message("%s is back with the new files", base_name(g_remount.path));
}

/* Copy a plain Mac/PC file into the first shared folder and refresh that
 * volume so the IIgs sees the file. Returns 1 (handled). */
int
ui_drop_regular_file(const char *path)
{
	int	slot, d = 0, nd, i, found = 0;

	if(g_remount.active) {
		/* the folder is being refreshed already: just add the file */
		char dst2[1400];

		snprintf(dst2, sizeof(dst2), "%s/%s", g_remount.path,
							base_name(path));
		if(SDL_CopyFile(path, dst2)) {
			g_remount.frames = REMOUNT_FRAMES;
			ui_message("Copied %s", base_name(path));
		}
		return 1;
	}
	Disk	*dsk = NULL;
	char	dst[1400], folder[1100];
	unsigned blocks;

	for(slot = 7; slot >= 5 && !found; slot--) {
		nd = (slot == 7) ? MAX_C7_DISKS : 2;
		for(d = 0; d < nd; d++) {
			dsk = iwm_get_dsk_from_slot_drive(slot, d);
			if(dsk->fd >= 0 && dsk->dynapro_info_ptr && dsk->name_ptr) {
				found = 1;
				break;
			}
		}
	}
	if(!found) {
		ui_message("To copy files in, share a folder first (Disks menu)");
		return 1;
	}
	slot++;				/* undo the loop's last step */
	snprintf(folder, sizeof(folder), "%s", dsk->name_ptr);
	snprintf(dst, sizeof(dst), "%s/%s", folder, base_name(path));
	for(i = 2; access(dst, F_OK) == 0 && i < 100; i++) {
		snprintf(dst, sizeof(dst), "%s/%d %s", folder, i, base_name(path));
	}
	if(!SDL_CopyFile(path, dst)) {
		ui_message("Could not copy %s", base_name(path));
		return 1;
	}
	if(iwm_motor_busy()) {
		ui_message("Copied %s - it shows up after the disk is re-read",
							base_name(path));
		return 1;
	}
	/* re-mount so the new file appears on the volume */
	blocks = dsk->dynapro_blocks;
	iwm_eject_disk(dsk);
	g_remount.active = 1;
	g_remount.slot = slot;
	g_remount.drive = d;
	g_remount.blocks = blocks;
	g_remount.frames = REMOUNT_FRAMES;
	snprintf(g_remount.path, sizeof(g_remount.path), "%s", folder);
	ui_message("Copied %s - refreshing %s...", base_name(path),
							base_name(folder));
	return 1;
}

void
ui_shared_folder_choose(void)
{
	ui_folder_browser_open("Choose a Folder to Share", ui_insert_auto);
}

int
ui_drives_is_open(void)
{
	return g_drives_win.open;
}

void
ui_drives_toggle(void)
{
	if(g_drives_win.open) {
		ui_window_close(&g_drives_win);
		return;
	}
	snprintf(g_drives_win.title, sizeof(g_drives_win.title), "Drives");
	g_drives_win.w = 480;
	g_drives_win.h = UI_TITLE_H + DR_TOP + DR_VISIBLE * DR_ROW_H + 6 + 24 + 10;
	g_drives_win.closable = 1;
	g_drives_win.modal = 0;
	g_drives_win.draw = drives_draw;
	g_drives_win.on_drop = drives_drop;
	if(g_drives_win.x == 0 && g_drives_win.y == 0) {
		ui_window_center(&g_drives_win);
	}
	ui_window_open(&g_drives_win);
}

void
ui_drives_init(void)
{
	recent_load();
}
