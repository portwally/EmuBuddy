/* EmuBuddy: the Drives window, disk insert/eject, part chooser and the
 * recent-disks list. Slot 7 is presented as a SCSI card with 7 hard drives
 * (the core's SmartPort/ProDOS block-device controller). Hard disk images
 * (.2mg, .hdv, .po, ...) are read by the core; this file is the UI for them. */
#include "defc.h"
#include "ui.h"
#include "bridge.h"
#include "receive_bin.h"
#include "helper_bin.h"
#include "prodos_img.h"
int	ui_bridge_is_on(void);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
extern int	g_bridge_off;

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
		if(line[0] && !(strlen(line) > 4 &&
				!SDL_strcasecmp(line + strlen(line) - 4, ".shk"))) {
			/* (ShrinkIt file archives are not disks: old entries are dropped) */
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
	static int	poll;

	if(++poll >= 60) {			/* once a second: notes from Receive */
		char	note[300];

		poll = 0;
		if(ui_bridge_is_on() && bridge_next_result(note, sizeof(note))) {
			printf("Receive note: %s\n", note);
			ui_message("Receive: %s", note);
		}
	}
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

/* ---- Drop destination: which folder inside the shared folder gets the
 * files that are dropped on the window ---- */

#define DEST_MAX	200
static int	g_drop_follow_finder = 1;	/* drop into the front Finder window */
static char	g_drop_sub[600];		/* "" = top of the shared folder */
static UiWindow	g_dest_win;
static char	g_dest_path[DEST_MAX][600];	/* relative path, "" = top */
static char	*g_dest_rows[DEST_MAX];
static char	g_dest_text[DEST_MAX][640];
static int	g_dest_n, g_dest_sel, g_dest_top;
static char	g_dest_root[1100];

static const char *
shared_root(void)
{
	int	slot, d, nd;
	Disk	*dsk;

	for(slot = 7; slot >= 5; slot--) {
		nd = (slot == 7) ? MAX_C7_DISKS : 2;
		for(d = 0; d < nd; d++) {
			dsk = iwm_get_dsk_from_slot_drive(slot, d);
			if(dsk->fd >= 0 && dsk->dynapro_info_ptr && dsk->name_ptr) {
				return dsk->name_ptr;
			}
		}
	}
	return NULL;
}

static void	dest_scan(const char *rel, int depth);

typedef struct { const char *rel; int depth; } DestScan;

static SDL_EnumerationResult SDLCALL
dest_enum_cb(void *ud, const char *dirname, const char *fname)
{
	DestScan	*sc = ud;
	char		full[1800], rel[600];
	SDL_PathInfo	info;

	if(fname[0] == '.' || g_dest_n >= DEST_MAX) {
		return SDL_ENUM_CONTINUE;
	}
	snprintf(full, sizeof(full), "%s%s%s", dirname,
			dirname[strlen(dirname) - 1] == '/' ? "" : "/", fname);
	if(!SDL_GetPathInfo(full, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
		return SDL_ENUM_CONTINUE;
	}
	snprintf(rel, sizeof(rel), "%s%s%s", sc->rel, sc->rel[0] ? "/" : "", fname);
	snprintf(g_dest_path[g_dest_n], sizeof(g_dest_path[0]), "%s", rel);
	g_dest_n++;
	if(sc->depth < 3) {
		dest_scan(rel, sc->depth + 1);
	}
	return SDL_ENUM_CONTINUE;
}

static void
dest_scan(const char *rel, int depth)
{
	char	dir[1800];
	DestScan sc = { rel, depth };

	snprintf(dir, sizeof(dir), "%s%s%s", g_dest_root, rel[0] ? "/" : "", rel);
	SDL_EnumerateDirectory(dir, dest_enum_cb, &sc);
}

static void
dest_rebuild(void)
{
	const char	*root = shared_root();
	int		i, depth;
	const char	*c;

	g_dest_n = 0;
	g_dest_sel = 0;
	g_dest_top = 0;
	if(!root) {
		return;
	}
	snprintf(g_dest_root, sizeof(g_dest_root), "%s", root);
	g_dest_path[g_dest_n++][0] = 0;			/* top level */
	dest_scan("", 1);
	for(i = 0; i < g_dest_n; i++) {
		if(g_dest_path[i][0] == 0) {
			snprintf(g_dest_text[i], sizeof(g_dest_text[0]),
							"(top level)");
		} else {
			depth = 0;
			for(c = g_dest_path[i]; *c; c++) {
				if(*c == '/') {
					depth++;
				}
			}
			snprintf(g_dest_text[i], sizeof(g_dest_text[0]), "%*s%s/",
				depth * 3, "", strrchr(g_dest_path[i], '/') ?
				strrchr(g_dest_path[i], '/') + 1 : g_dest_path[i]);
		}
		g_dest_rows[i] = g_dest_text[i];
		if(!strcmp(g_dest_path[i], g_drop_sub)) {
			g_dest_sel = i;
		}
	}
}

static void
dest_draw(UiWindow *win, int cx, int cy)
{
	int	by = win->h - UI_TITLE_H - 34, prev = g_dest_sel;
	char	line[200];

	ui_checkbox(win, cx, cy, 10, 6, "Drop into the front Finder window",
						&g_drop_follow_finder);
	if(g_dest_n == 0) {
		ui_draw_text(cx + 12, cy + 40, "No shared folder yet.", UI_BLACK);
		ui_draw_text(cx + 12, cy + 58, "Use Disks > Share a Folder.", UI_GREY);
	} else {
		snprintf(line, sizeof(line), "Shared: %.40s", base_name(g_dest_root));
		ui_draw_text(cx + 12, cy + 32, line, UI_BLACK);
		ui_draw_text(cx + 12, cy + 50, g_drop_follow_finder ?
			"Otherwise (not on the shared volume):" : "Dropped files go into:",
			UI_GREY);
		ui_list(win, cx, cy, 8, 70, win->w - 2 - 16, by - 76,
			(const char **)g_dest_rows, g_dest_n, &g_dest_sel,
			&g_dest_top);
		if(g_dest_sel != prev && g_dest_sel >= 0 && g_dest_sel < g_dest_n) {
			snprintf(g_drop_sub, sizeof(g_drop_sub), "%s",
						g_dest_path[g_dest_sel]);
			ui_message("Files now go to %s", g_dest_sel ?
					g_dest_text[g_dest_sel] + strspn(
					g_dest_text[g_dest_sel], " ") : "the top level");
		}
	}
	if(ui_button(win, cx, cy, 8, by, 90, 24, "Refresh", 1)) {
		dest_rebuild();
	}
	if(ui_button(win, cx, cy, win->w - 2 - 8 - 70, by, 70, 24, "Done", 1)) {
		ui_window_close(win);
	}
}

void
ui_drop_dest_toggle(void)
{
	if(g_dest_win.open) {
		ui_window_close(&g_dest_win);
		return;
	}
	dest_rebuild();
	snprintf(g_dest_win.title, sizeof(g_dest_win.title), "Drop Destination");
	g_dest_win.w = 360;
	g_dest_win.h = 350;
	g_dest_win.closable = 1;
	g_dest_win.modal = 0;
	g_dest_win.draw = dest_draw;
	if(g_dest_win.x == 0 && g_dest_win.y == 0) {
		ui_window_center(&g_dest_win);
	}
	ui_window_open(&g_dest_win);
}

/* ---- The front Finder window as the drop target ---- */

int	finder_front_path(char *out, size_t n);		/* finder_probe.c */

static int	g_force_root;				/* copy to the top of the share */
static char	g_drop_label[300];			/* where the last drop went */

/* lower-case letters and digits only, so "My Folder" matches "My.Folder" */
static void
name_key(const char *s, char *o, size_t n)
{
	size_t	k = 0;

	for(; *s && k + 1 < n; s++) {
		if(SDL_isalnum((unsigned char)*s)) {
			o[k++] = (char)SDL_tolower((unsigned char)*s);
		}
	}
	o[k] = 0;
}

typedef struct { char want[300]; char found[300]; } DirFind;

static SDL_EnumerationResult SDLCALL
dirfind_cb(void *ud, const char *dirname, const char *fname)
{
	DirFind		*df = ud;
	char		key[300], full[1800];
	SDL_PathInfo	info;

	name_key(fname, key, sizeof(key));
	if(strcmp(key, df->want) != 0) {
		return SDL_ENUM_CONTINUE;
	}
	snprintf(full, sizeof(full), "%s%s%s", dirname,
			dirname[strlen(dirname) - 1] == '/' ? "" : "/", fname);
	if(SDL_GetPathInfo(full, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
		snprintf(df->found, sizeof(df->found), "%s", fname);
		return SDL_ENUM_SUCCESS;
	}
	return SDL_ENUM_CONTINUE;
}

/* ":Volume:A:B" (the Finder's front window) -> a folder inside the shared
 * folder root. Returns 1 if the window shows the shared volume. */
static int
finder_path_to_host(const char *root, const char *gsos, char *out, size_t n)
{
	char	rootkey[300], key[300], comp[300];
	char	cur[1800];
	const char *p = gsos;
	int	first = 1;

	if(*p == ':') {
		p++;
	}
	name_key(base_name(root), rootkey, sizeof(rootkey));
	snprintf(cur, sizeof(cur), "%s", root);
	while(*p) {
		size_t	l = strcspn(p, ":");

		snprintf(comp, sizeof(comp), "%.*s", (int)(l > 250 ? 250 : l), p);
		p += l;
		if(*p == ':') {
			p++;
		}
		if(first) {			/* the volume itself */
			first = 0;
			name_key(comp, key, sizeof(key));
			if(strcmp(key, rootkey) != 0) {
				return 0;
			}
			continue;
		}
		{
			DirFind df;

			name_key(comp, df.want, sizeof(df.want));
			df.found[0] = 0;
			SDL_EnumerateDirectory(cur, dirfind_cb, &df);
			if(!df.found[0]) {
				return 0;
			}
			snprintf(cur + strlen(cur), sizeof(cur) - strlen(cur), "/%s",
								df.found);
		}
	}
	if(first) {
		return 0;
	}
	snprintf(out, n, "%s", cur);
	return 1;
}

/* ---- Host bridge folder (bridge.c): Disks > Bridge Folder ---- */

#ifndef _WIN32
# include <limits.h>
# include <stdlib.h>
#endif

static void
real_or_same(const char *p, char *out, size_t n)
{
#ifndef _WIN32
	char	tmp[PATH_MAX];

	if(realpath(p, tmp)) {
		snprintf(out, n, "%s", tmp);
		return;
	}
#endif
	snprintf(out, n, "%s", p);
}

static int
paths_overlap(const char *a, const char *b)
{
	size_t	la = strlen(a), lb = strlen(b), m = la < lb ? la : lb;

	if(strncmp(a, b, m) != 0) {
		return 0;
	}
	return (la == lb) || (la < lb ? b[m] == '/' : a[m] == '/');
}

/* 1 if the folder (or a folder inside / above it) is mounted as a disk. */
static int
bridge_overlaps_share(const char *folder)
{
	int	slot, d, nd;
	Disk	*dsk;
	char	f[1500], s[1500];

	real_or_same(folder, f, sizeof(f));
	for(slot = 7; slot >= 5; slot--) {
		nd = (slot == 7) ? MAX_C7_DISKS : 2;
		for(d = 0; d < nd; d++) {
			dsk = iwm_get_dsk_from_slot_drive(slot, d);
			if(dsk->fd >= 0 && dsk->dynapro_info_ptr && dsk->name_ptr) {
				real_or_same(dsk->name_ptr, s, sizeof(s));
				if(paths_overlap(f, s)) {
					return 1;
				}
			}
		}
	}
	return 0;
}

static void
bridge_folder_picked(const char *path)
{
	if(bridge_overlaps_share(path)) {
		ui_message("That folder is shared as a disk - choose another one");
		return;
	}
	cfg_file_update_ptr(&g_bridge_root, (char *)path, 1);
	g_bridge_off = 0;
	ui_message("Bridge folder: %s", base_name(path));
}

void
ui_bridge_choose(void)
{
	ui_folder_browser_open("Choose the Bridge Folder", bridge_folder_picked);
}

void
ui_bridge_off(void)
{
	cfg_file_update_ptr(&g_bridge_root, "", 1);
	g_bridge_off = 1;
	config_write_config_kegs_file(0);
	ui_message("Bridge off");
}

int
ui_bridge_is_on(void)
{
	return g_bridge_root && g_bridge_root[0];
}

/* Disks > Copy Receive to Shared Folder: puts the IIgs program Receive
 * (tools/receive) in the top of the shared folder, with its file type. */
void
ui_install_receive(void)
{
	char		*pref = SDL_GetPrefPath("EmuBuddy", "EmuBuddy");
	char		tmp[1300];
	SDL_IOStream	*io;

	if(!shared_root()) {
		ui_message("No shared folder yet - use Disks > Share a Folder");
		SDL_free(pref);
		return;
	}
	snprintf(tmp, sizeof(tmp), "%sReceive,t$B3,a$DB03", pref ? pref : "");
	SDL_free(pref);
	io = SDL_IOFromFile(tmp, "wb");
	if(!io) {
		ui_message("Could not prepare Receive");
		return;
	}
	SDL_WriteIO(io, receive_bin, sizeof(receive_bin));
	SDL_CloseIO(io);
	g_force_root = 1;
	ui_drop_regular_file(tmp);
	g_force_root = 0;
	SDL_RemovePath(tmp);
}

/* ---- The automatic shared folder ----
 * If no folder is shared as a disk (not in the saved settings, not on the
 * command line), EmuBuddy makes "EmuBuddy Files" in the Documents folder and
 * mounts it, so that file exchange works from the first start without any
 * set-up. It shows in the IIgs as the disk EmuBuddy.Files. */

extern int	g_auto_share;

void
ui_auto_share(void)
{
	const char	*docs;
	char		path[1300], readme[1400];
	SDL_PathInfo	info;
	int		fresh;
	SDL_IOStream	*io;
	static const char text[] =
		"This folder is a disk for the Apple IIgs in EmuBuddy (EmuBuddy.Files).\r"
		"Files you put here show up in the IIgs, and what the IIgs saves here\r"
		"is an ordinary file on your computer.\r"
		"\r"
		"Drop files on the EmuBuddy window to copy them here.\r";

	if(!g_auto_share || !g_slot7_scsi || shared_root()) {
		return;
	}
	docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
	if(!docs) {
		docs = SDL_GetUserFolder(SDL_FOLDER_HOME);
	}
	if(getenv("EMUBUDDY_AUTOSHARE_DIR")) {		/* test aid */
		snprintf(path, sizeof(path), "%s", getenv("EMUBUDDY_AUTOSHARE_DIR"));
	} else {
		snprintf(path, sizeof(path), "%sEmuBuddy Files", docs ? docs : "");
	}
	fresh = !SDL_GetPathInfo(path, &info);
	if(!SDL_CreateDirectory(path)) {
		return;
	}
	if(fresh) {
		snprintf(readme, sizeof(readme), "%s/Read Me.txt", path);
		io = SDL_IOFromFile(readme, "wb");
		if(io) {
			SDL_WriteIO(io, text, sizeof(text) - 1);
			SDL_CloseIO(io);
		}
	}
	ui_insert_auto(path);
}

/* Disks > Show Shared Folder: open the shared folder in the computer's own
 * file manager (Finder / Explorer). */
void
ui_show_shared_folder(void)
{
	const char	*root = shared_root();
	char		url[3000];
	size_t		k = 0;

	if(!root) {
		ui_message("No shared folder yet - use Disks > Share a Folder");
		return;
	}
	k += (size_t)snprintf(url, sizeof(url), "file://");
	for(; *root && k + 4 < sizeof(url); root++) {
		unsigned char c = (unsigned char)*root;

		if(SDL_isalnum(c) || strchr("/-_.~", c)) {
			url[k++] = (char)c;
		} else {
			k += (size_t)snprintf(url + k, sizeof(url) - k, "%%%02X", c);
		}
	}
	url[k] = 0;
	if(!SDL_OpenURL(url)) {
		ui_message("Could not open the folder");
	}
}

/* The folder for dropped files: the front Finder window if it shows the
 * shared volume, else the chosen subfolder if it still exists. */
static void
drop_folder(const char *root, char *out, size_t n)
{
	SDL_PathInfo	info;

	snprintf(out, n, "%s", root);
	snprintf(g_drop_label, sizeof(g_drop_label), "%s", base_name(root));
	if(g_drop_follow_finder && !g_force_root) {
		char	gp[300], host[1800];

		if(finder_front_path(gp, sizeof(gp))) {
			if(finder_path_to_host(root, gp, host, sizeof(host))) {
				snprintf(out, n, "%s", host);
				snprintf(g_drop_label, sizeof(g_drop_label), "%s",
									gp + 1);
				return;
			}
			snprintf(g_drop_label, sizeof(g_drop_label),
				"%s (the front window, %s, is not the shared folder)",
				base_name(root), gp + 1);
		}
	}
	if(g_drop_sub[0] && !g_force_root) {
		char	sub[1800];

		snprintf(sub, sizeof(sub), "%s/%s", root, g_drop_sub);
		if(SDL_GetPathInfo(sub, &info) &&
				info.type == SDL_PATHTYPE_DIRECTORY) {
			snprintf(out, n, "%s", sub);
		}
	}
}

/* The front Finder window shows a folder that the shared folder can't reach
 * (another disk): stage the file in the bridge folder for the IIgs program
 * Receive. Returns 1 if the drop was handled here. */
static int
try_stage_for_receive(const char *path)
{
	char		gp[300], host[1800], msg[300];
	const char	*root = shared_root();

	if(g_force_root || !g_drop_follow_finder || !ui_bridge_is_on() ||
			!finder_front_path(gp, sizeof(gp))) {
		printf("Drop: not staged (force_root %d follow %d bridge %d)\n",
			g_force_root, g_drop_follow_finder, ui_bridge_is_on());
		return 0;
	}
	printf("Drop: front window %s\n", gp);
	if(root && finder_path_to_host(root, gp, host, sizeof(host))) {
		return 0;			/* the shared volume: copy there as usual */
	}
	if(bridge_stage_file(path, gp, msg, (int)sizeof(msg)) == 0) {
		ui_message("Staged %s for %s - run Receive in the IIgs", msg, gp + 1);
	} else {
		ui_message("Could not stage the file: %s", msg);
	}
	return 1;
}

/* Copy a plain Mac/PC file into the chosen folder of the shared volume and
 * refresh that volume so the IIgs sees the file. Returns 1 (handled). */
int
ui_drop_regular_file(const char *path)
{
	int	slot, d = 0, nd, i, found = 0;

	if(try_stage_for_receive(path)) {
		return 1;
	}

	if(g_remount.active) {
		/* the folder is being refreshed already: just add the file */
		char dst2[1700], f2[1500];

		drop_folder(g_remount.path, f2, sizeof(f2));
		snprintf(dst2, sizeof(dst2), "%s/%s", f2, base_name(path));
		if(SDL_CopyFile(path, dst2)) {
			g_remount.frames = REMOUNT_FRAMES;
			ui_message("Copied %s to %s", base_name(path), g_drop_label);
		}
		return 1;
	}
	Disk	*dsk = NULL;
	char	dst[1700], folder[1100], target[1500];
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
	drop_folder(folder, target, sizeof(target));
	snprintf(dst, sizeof(dst), "%s/%s", target, base_name(path));
	for(i = 2; SDL_GetPathInfo(dst, NULL) && i < 100; i++) {
		snprintf(dst, sizeof(dst), "%s/%d %s", target, i, base_name(path));
	}
	if(!SDL_CopyFile(path, dst)) {
		ui_message("Could not copy %s", base_name(path));
		return 1;
	}
	if(iwm_motor_busy()) {
		ui_message("Copied %s to %s - it shows up after the disk is re-read",
						base_name(path), g_drop_label);
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
	ui_message("Copied %s to %s", base_name(path), g_drop_label);
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
	bridge_overlap_check = bridge_overlaps_share;
	recent_load();
}

/* ---- The IIgs helper ----
 * A small program (tools/helper) that runs in the background in the IIgs and
 * copies files that were dropped on the EmuBuddy window into the Finder window
 * they were dropped on. It lives in the boot disk's /SYSTEM/SYSTEM.SETUP
 * folder. EmuBuddy installs it once, after asking. */

extern int	g_helper_ask, g_bridge_off, g_ui_pause;



#define HELPER_NAME	"EMUBUDDYHELPER"
#define HELPER_DIR	"SYSTEM/SYSTEM.SETUP"

/* Is the helper in that image? -1: not a disk this can handle, 0: no, 1: yes */
static int
helper_present(const char *path, unsigned *dirkey)
{
	PImg		img;
	const char	*err;
	int		r;

	if(pimg_open_rw(&img, path, &err) != 0) {
		return -1;
	}
	*dirkey = pimg_find_dir(&img, HELPER_DIR);
	r = !*dirkey ? -1 : pimg_has_entry(&img, *dirkey, HELPER_NAME);
	pimg_close(&img);
	return r;
}

/* Write the helper into the image; the caller has ejected the disk. */
static int
helper_write(const char *path, const char **err)
{
	PImg		img;
	unsigned	dir;
	int		r;

	if(pimg_open_rw(&img, path, err) != 0) {
		return -1;
	}
	dir = pimg_find_dir(&img, HELPER_DIR);
	if(!dir) {
		pimg_close(&img);
		*err = "No SYSTEM/SYSTEM.SETUP folder";
		return -1;
	}
	r = pimg_add_file(&img, dir, HELPER_NAME, 0xB6, 0, helper_bin,
						sizeof(helper_bin), err);
	pimg_close(&img);
	return r;
}

/* Asks, at start, whether to put the helper into the boot disk (S7D1). */
void
ui_helper_offer(void)
{
	Disk		*dsk;
	char		path[1100];
	unsigned	dir;
	int		have, was_paused = g_ui_pause, pick;
	const char	*err = "";
	SDL_MessageBoxButtonData btn[3];
	SDL_MessageBoxData box;

	if(!g_helper_ask || !g_slot7_scsi || getenv("EMUBUDDY_NOHELPER")) {
		return;
	}
	dsk = iwm_get_dsk_from_slot_drive(7, 0);
	if(dsk->fd < 0 || !dsk->name_ptr || dsk->dynapro_info_ptr) {
		return;
	}
	snprintf(path, sizeof(path), "%s", dsk->name_ptr);
	have = helper_present(path, &dir);
	if(have != 0) {				/* installed, or not suitable */
		return;
	}
	g_ui_pause = 1;
	SDL_zero(btn);
	btn[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
	btn[0].buttonID = 1;
	btn[0].text = "Install";
	btn[1].buttonID = 2;
	btn[1].text = "Not Now";
	btn[2].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
	btn[2].buttonID = 3;
	btn[2].text = "Never";
	SDL_zero(box);
	box.flags = SDL_MESSAGEBOX_INFORMATION;
	box.title = "EmuBuddy";
	box.message = "Install the EmuBuddy helper in your startup disk?\n\n"
		"It lets you drop files from your computer on any Finder window in "
		"the Apple IIgs and have them copied there. It is one small file in "
		"the SYSTEM/SYSTEM.SETUP folder of the disk, and you can delete it "
		"at any time.";
	box.numbuttons = 3;
	box.buttons = btn;
	pick = 2;
	if(getenv("EMUBUDDY_HELPER_ANSWER")) {		/* test aid */
		pick = atoi(getenv("EMUBUDDY_HELPER_ANSWER"));
	} else if(!SDL_ShowMessageBox(&box, &pick)) {
		pick = 2;
	}
	if(pick == 3) {
		g_helper_ask = 0;
		config_write_config_kegs_file(0);
	}
	if(pick == 1) {
		iwm_eject_disk_by_num(7, 0);
		if(helper_write(path, &err) == 0) {
			ui_message("Helper installed - restarting the IIgs");
		} else {
			ui_message("Could not install the helper: %s", err);
		}
		ui_insert_auto(path);
		load_roms_init_memory();
		do_reset();
	}
	g_ui_pause = was_paused;
}

/* The bridge folder is made by itself: if none is set, use a folder in the
 * application's own data folder. */
void
ui_auto_bridge(void)
{
	char		*pref, path[1300];

	/* a saved bridge folder that is also the shared disk can't be used */
	if((ui_bridge_is_on() && !bridge_overlaps_share(g_bridge_root)) ||
			g_bridge_off || !g_slot7_scsi) {
		return;
	}
	pref = SDL_GetPrefPath("EmuBuddy", "Bridge");
	if(!pref) {
		return;
	}
	snprintf(path, sizeof(path), "%s", pref);
	SDL_free(pref);
	if(bridge_overlaps_share(path)) {
		return;
	}
	cfg_file_update_ptr(&g_bridge_root, path, 0);	/* this run; not saved */
}
