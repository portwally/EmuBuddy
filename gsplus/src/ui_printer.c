/* EmuBuddy: the Printer window. Pages finished by the virtual ImageWriter II
 * (imagewriter.cpp -> printer_page_done) are kept here, shown as a preview,
 * and can be saved as PDF (to the Desktop) or sent to the default printer. */
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void	printer_eject_now(void);
int	printer_ps_save_pdf(const char *pdf_path);
void	printer_ps_forget_jobs(void);
SDL_Renderer *ui_renderer(void);

#define MAX_PAGES	99

typedef struct {
	int		w, h, dpi, color, ps;	/* ps: came from a PostScript job */
	unsigned char	*rgb;		/* w*h*3 */
	SDL_Texture	*tex;
} Page;

static Page	g_pages[MAX_PAGES];
static int	g_npages, g_cur;
static UiWindow	g_printer_win;

/* ---- pages from the printer ---- */

void
printer_page_done(const unsigned char *pixels, int w, int h, int pitch,
				const unsigned char *pal, int dpi)
{
	Page	*p;
	int	x, y, color = 0;

	if(g_npages == MAX_PAGES) {
		ui_message("Printer: page limit reached - clear some pages");
		return;
	}
	p = &g_pages[g_npages];
	p->rgb = malloc((size_t)w * h * 3);
	if(!p->rgb) {
		return;
	}
	for(y = 0; y < h; y++) {
		const unsigned char *s = pixels + (size_t)y * pitch;
		unsigned char *d = p->rgb + (size_t)y * w * 3;

		for(x = 0; x < w; x++) {
			const unsigned char *c = pal + s[x] * 3;

			d[0] = c[0];
			d[1] = c[1];
			d[2] = c[2];
			if(c[0] != c[1] || c[1] != c[2]) {
				color = 1;
			}
			d += 3;
		}
	}
	p->w = w;
	p->h = h;
	p->dpi = dpi;
	p->color = color;
	p->ps = 0;
	p->tex = NULL;
	g_cur = g_npages++;
	ui_message("Printed page %d", g_npages);
	if(!g_printer_win.open) {
		ui_printer_toggle();
	}
}

/* A finished page that is already RGB (from the PostScript printer). */
void
printer_page_done_rgb(const unsigned char *rgb, int w, int h, int dpi)
{
	Page	*p;
	size_t	n = (size_t)w * h * 3, i;
	int	color = 0;

	if(g_npages == MAX_PAGES) {
		ui_message("Printer: page limit reached - clear some pages");
		return;
	}
	p = &g_pages[g_npages];
	p->rgb = malloc(n);
	if(!p->rgb) {
		return;
	}
	memcpy(p->rgb, rgb, n);
	for(i = 0; i < n; i += 3) {
		if(rgb[i] != rgb[i + 1] || rgb[i + 1] != rgb[i + 2]) {
			color = 1;
			break;
		}
	}
	p->w = w;
	p->h = h;
	p->dpi = dpi;
	p->color = color;
	p->ps = 1;
	p->tex = NULL;
	g_cur = g_npages++;
	ui_message("Printed page %d", g_npages);
	if(!g_printer_win.open) {
		ui_printer_toggle();
	}
}

static void
clear_pages(void)
{
	int	i;

	for(i = 0; i < g_npages; i++) {
		free(g_pages[i].rgb);
		if(g_pages[i].tex) {
			SDL_DestroyTexture(g_pages[i].tex);
		}
	}
	memset(g_pages, 0, sizeof(g_pages));
	printer_ps_forget_jobs();
	g_npages = 0;
	g_cur = 0;
}

/* ---- PDF writer: one image per page, RunLength compressed ---- */

static size_t
rle_encode(const unsigned char *in, size_t n, unsigned char *out)
{
	size_t	i = 0, o = 0, run, lit;

	while(i < n) {
		run = 1;
		while(i + run < n && run < 128 && in[i + run] == in[i]) {
			run++;
		}
		if(run >= 2) {
			out[o++] = (unsigned char)(257 - run);
			out[o++] = in[i];
			i += run;
			continue;
		}
		lit = 0;
		while(i + lit < n && lit < 128 &&
				!(i + lit + 1 < n && in[i + lit] == in[i + lit + 1])) {
			lit++;
		}
		if(lit == 0) {
			lit = 1;
		}
		out[o++] = (unsigned char)(lit - 1);
		memcpy(out + o, in + i, lit);
		o += lit;
		i += lit;
	}
	out[o++] = 128;				/* end of data */
	return o;
}

static int
write_pdf_bitmap(const char *path)
{
	FILE	*f = fopen(path, "wb");
	long	*off;
	int	nobj = 2 + 3 * g_npages, i, k;

	if(!f) {
		return -1;
	}
	off = calloc((size_t)nobj + 1, sizeof(long));
	fprintf(f, "%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n");
	off[1] = ftell(f);
	fprintf(f, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
	off[2] = ftell(f);
	fprintf(f, "2 0 obj\n<< /Type /Pages /Count %d /Kids [", g_npages);
	for(i = 0; i < g_npages; i++) {
		fprintf(f, " %d 0 R", 3 + 3 * i);
	}
	fprintf(f, " ] >>\nendobj\n");
	for(i = 0; i < g_npages; i++) {
		Page		*p = &g_pages[i];
		int		po = 3 + 3 * i, co = po + 1, io = po + 2;
		double		pw = p->w * 72.0 / p->dpi, ph = p->h * 72.0 / p->dpi;
		size_t		n = (size_t)p->w * p->h * (p->color ? 3 : 1);
		unsigned char	*raw = malloc(n), *enc = malloc(n + n / 128 + 16);
		size_t		elen;
		char		content[128];

		if(!raw || !enc) {
			free(raw);
			free(enc);
			fclose(f);
			free(off);
			return -1;
		}
		if(p->color) {
			memcpy(raw, p->rgb, n);
		} else {
			for(k = 0; k < p->w * p->h; k++) {
				raw[k] = p->rgb[k * 3];
			}
		}
		elen = rle_encode(raw, n, enc);
		snprintf(content, sizeof(content), "q %.2f 0 0 %.2f 0 0 cm /Im0 Do Q",
								pw, ph);
		off[po] = ftell(f);
		fprintf(f, "%d 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
			"%.2f %.2f] /Resources << /XObject << /Im0 %d 0 R >> >> "
			"/Contents %d 0 R >>\nendobj\n", po, pw, ph, io, co);
		off[co] = ftell(f);
		fprintf(f, "%d 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\n"
			"endobj\n", co, (int)strlen(content), content);
		off[io] = ftell(f);
		fprintf(f, "%d 0 obj\n<< /Type /XObject /Subtype /Image /Width %d "
			"/Height %d /ColorSpace /%s /BitsPerComponent 8 "
			"/Filter /RunLengthDecode /Length %ld >>\nstream\n", io,
			p->w, p->h, p->color ? "DeviceRGB" : "DeviceGray",
			(long)elen);
		fwrite(enc, 1, elen, f);
		fprintf(f, "\nendstream\nendobj\n");
		free(raw);
		free(enc);
	}
	{
		long xref = ftell(f);

		fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", nobj + 1);
		for(i = 1; i <= nobj; i++) {
			fprintf(f, "%010ld 00000 n \n", off[i]);
		}
		fprintf(f, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n"
						"%%%%EOF\n", nobj + 1, xref);
	}
	free(off);
	return fclose(f) == 0 ? 0 : -1;
}

/* All pages from PostScript jobs: let Ghostscript write a vector PDF.
 * Otherwise (ImageWriter pages, or a mix) pages go in as bitmaps. */
static int
write_pdf(const char *path)
{
	int	i, all_ps = g_npages > 0;

	for(i = 0; i < g_npages; i++) {
		if(!g_pages[i].ps) {
			all_ps = 0;
		}
	}
	if(all_ps && printer_ps_save_pdf(path) == 0) {
		return 0;
	}
	return write_pdf_bitmap(path);
}

static void
save_pdf_to_desktop(void)
{
	const char	*desk = SDL_GetUserFolder(SDL_FOLDER_DESKTOP);
	char		path[1024], stamp[64];
	time_t		t = time(NULL);
	struct tm	*tm = localtime(&t);

	if(!desk) {
		desk = SDL_GetUserFolder(SDL_FOLDER_HOME);
	}
	if(tm) {
		strftime(stamp, sizeof(stamp), "%Y-%m-%d %H%M%S", tm);
	} else {
		snprintf(stamp, sizeof(stamp), "%ld", (long)t);
	}
	snprintf(path, sizeof(path), "%sEmuBuddy Print %s.pdf", desk ? desk : "",
								stamp);
	if(write_pdf(path) == 0) {
		ui_message("Saved to Desktop: EmuBuddy Print %s.pdf", stamp);
	} else {
		ui_message("Could not save the PDF");
	}
}

static void
print_pages(void)
{
	char	*pref = SDL_GetPrefPath("EmuBuddy", "EmuBuddy");
	char	path[1024];

	snprintf(path, sizeof(path), "%sprint.pdf", pref ? pref : "");
	SDL_free(pref);
	if(write_pdf(path) != 0) {
		ui_message("Could not prepare the print job");
		return;
	}
#ifdef _WIN32
	{
		char url[1100];

		snprintf(url, sizeof(url), "file:///%s", path);
		SDL_OpenURL(url);
		ui_message("Opened the PDF - print it from there");
	}
#else
	{
		const char	*args[] = { "lp", path, NULL };
		SDL_Process	*proc = SDL_CreateProcess(args, false);

		if(proc) {
			SDL_DestroyProcess(proc);
			ui_message("Sent %d page%s to the default printer", g_npages,
						g_npages == 1 ? "" : "s");
		} else {
			ui_message("Printing failed: no 'lp' command");
		}
	}
#endif
}

static void
open_in_viewer(void)
{
	char	*pref = SDL_GetPrefPath("EmuBuddy", "EmuBuddy");
	char	path[1024], url[1100];

	snprintf(path, sizeof(path), "%sprint.pdf", pref ? pref : "");
	SDL_free(pref);
	if(write_pdf(path) != 0) {
		ui_message("Could not write the PDF");
		return;
	}
	snprintf(url, sizeof(url), "file://%s", path);
	SDL_OpenURL(url);
}

/* ---- window ---- */

#define PREV_W	230
#define PREV_H	298

static void
printer_draw(UiWindow *win, int cx, int cy)
{
	int	cw = win->w - 2, px = (cw - PREV_W) / 2, py = 8, by;
	char	label[48];
	int	have = g_npages > 0;

	ui_fill(cx + px + 3, cy + py + 3, PREV_W, PREV_H, UI_GREY);	/* shadow */
	ui_fill(cx + px, cy + py, PREV_W, PREV_H, UI_WHITE);
	ui_frame(cx + px, cy + py, PREV_W, PREV_H, UI_BLACK);
	if(have) {
		Page *p = &g_pages[g_cur];

		if(!p->tex) {
			p->tex = SDL_CreateTexture(ui_renderer(),
					SDL_PIXELFORMAT_RGB24,
					SDL_TEXTUREACCESS_STATIC, p->w, p->h);
			if(p->tex) {
				SDL_UpdateTexture(p->tex, NULL, p->rgb, p->w * 3);
				SDL_SetTextureScaleMode(p->tex,
						SDL_SCALEMODE_LINEAR);
			}
		}
		if(p->tex) {
			SDL_FRect d;

			d.x = (float)(cx + px + 1);
			d.y = (float)(cy + py + 1);
			d.w = (float)(PREV_W - 2);
			d.h = (float)(PREV_H - 2);
			SDL_RenderTexture(ui_renderer(), p->tex, NULL, &d);
		}
		snprintf(label, sizeof(label), "Page %d of %d", g_cur + 1,
								g_npages);
	} else {
		snprintf(label, sizeof(label), "No pages printed yet");
		ui_draw_text(cx + px + 20, cy + py + PREV_H / 2 - 20,
			"Print from the IIgs to the", UI_GREY);
		ui_draw_text(cx + px + 20, cy + py + PREV_H / 2 - 2,
			"ImageWriter in slot 1.", UI_GREY);
	}
	by = py + PREV_H + 10;
	if(ui_button(win, cx, cy, 10, by, 36, 24, "<", have && g_cur > 0)) {
		g_cur--;
	}
	if(ui_button(win, cx, cy, cw - 10 - 36, by, 36, 24, ">",
						have && g_cur < g_npages - 1)) {
		g_cur++;
	}
	ui_draw_text(cx + (cw - ui_text_width(label)) / 2, cy + by + 4, label,
								UI_BLACK);
	by += 32;
	if(ui_button(win, cx, cy, 10, by, 92, 24, "Save PDF", have)) {
		save_pdf_to_desktop();
	}
	if(ui_button(win, cx, cy, 108, by, 64, 24, "Print", have)) {
		print_pages();
	}
	if(ui_button(win, cx, cy, 178, by, 64, 24, "Open", have)) {
		open_in_viewer();
	}
	if(ui_button(win, cx, cy, 248, by, 64, 24, "Clear", have)) {
		clear_pages();
	}
	if(ui_button(win, cx, cy, cw - 10 - 66, by, 66, 24, "Eject", 1)) {
		printer_eject_now();	/* finish a page left half-printed */
	}
}

int
ui_printer_is_open(void)
{
	return g_printer_win.open;
}

void
ui_printer_toggle(void)
{
	if(g_printer_win.open) {
		ui_window_close(&g_printer_win);
		return;
	}
	snprintf(g_printer_win.title, sizeof(g_printer_win.title), "Printer");
	g_printer_win.w = 400;	/* fits the 80-column text screen too */
	g_printer_win.h = UI_TITLE_H + 8 + PREV_H + 10 + 32 + 24 + 12;
	g_printer_win.closable = 1;
	g_printer_win.modal = 0;
	g_printer_win.draw = printer_draw;
	if(g_printer_win.x == 0 && g_printer_win.y == 0) {
		ui_window_center(&g_printer_win);
	}
	ui_window_open(&g_printer_win);
}
