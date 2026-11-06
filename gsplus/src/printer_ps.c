/* EmuBuddy: PostScript printer on a serial (SCC) port. Serial mode 5
 * (g_serial_cfg[port] == 5).
 *
 * The GS/OS "DC.PostScript" printer driver (DC Printer control panel: port
 * "Printer Port", type DC.PostScript) sends a PostScript job as plain bytes.
 * We collect them, cut them into jobs (Ctrl-D, or a pause), answer the
 * driver's printer queries, and let Ghostscript turn each job into page
 * bitmaps for the Printer window. Ghostscript runs as a separate process so
 * the emulator never waits for it.
 *
 * Environment: EMUBUDDY_PSDIR=<dir> also keeps every raw job there as
 * jobNNN.ps (for debugging), EMUBUDDY_GS=<path> picks the gs program. */
#include "defc.h"
#include "scc.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern Scc	g_scc[2];
extern word32	g_vbl_count;

void	printer_page_done_rgb(const unsigned char *rgb, int w, int h, int dpi);

#define PS_DPI		144
#define PS_IDLE_VBL	75		/* 1.25 s without data ends a job */
#define PS_MAX_JOB	(32 * 1024 * 1024)
#define PS_MAX_QUEUE	8
#define PS_GS_TIMEOUT	(120 * 60)	/* vbls */

static unsigned char	*g_buf;
static size_t		g_len, g_cap;
static word32		g_last_vbl;
static int		g_job_no;

static unsigned char	g_reply[4096];	/* answers waiting to be read */
static int		g_reply_len, g_reply_pos;

static char		*g_gs;		/* path of the gs program */
static int		g_gs_searched, g_gs_warned;
static char		*g_tmp;		/* scratch folder */

typedef struct {
	char	path[1100];
	int	no;
	int	own;		/* we made the file, so we may delete it */
} PsJob;
static PsJob		g_queue[PS_MAX_QUEUE];
static int		g_nqueue;
static SDL_Process	*g_proc;
static PsJob		g_cur;
static word32		g_proc_start;
#define PS_MAX_KEEP	99
static PsJob		g_keep[PS_MAX_KEEP];	/* jobs whose pages are in the window */
static int		g_nkeep;

/* ---- finding Ghostscript ---- */

static int
is_file(const char *p)
{
	SDL_PathInfo	info;

	return SDL_GetPathInfo(p, &info) && info.type == SDL_PATHTYPE_FILE;
}

static char *
join_try(const char *dir, size_t n, const char *name)
{
	char	buf[1100];

	snprintf(buf, sizeof(buf), "%.*s/%s", (int)n, dir, name);
	return is_file(buf) ? strdup(buf) : NULL;
}

static void
find_gs(void)
{
	const char	*env, *path, *p, *q;
	static const char *names[] = { "gs", "gswin64c.exe", "gswin32c.exe" };
	static const char *dirs[] = { "/opt/homebrew/bin", "/usr/local/bin",
				"/opt/local/bin", "/usr/bin", "/snap/bin", NULL };
	int		i, j;
	char		*found;

	g_gs_searched = 1;
	env = getenv("EMUBUDDY_GS");
	if(env && is_file(env)) {
		g_gs = strdup(env);
		return;
	}
	path = getenv("PATH");
	for(p = path; p && *p; p = *q ? q + 1 : q) {
		q = strchr(p, ':');
#ifdef _WIN32
		q = strchr(p, ';');
#endif
		if(!q) {
			q = p + strlen(p);
		}
		for(j = 0; j < 3; j++) {
			if((found = join_try(p, (size_t)(q - p), names[j]))) {
				g_gs = found;
				return;
			}
		}
	}
	for(i = 0; dirs[i]; i++) {
		if((found = join_try(dirs[i], strlen(dirs[i]), "gs"))) {
			g_gs = found;
			return;
		}
	}
#ifdef _WIN32
	{
		int	n = 0;
		char	**list = SDL_GlobDirectory("C:/Program Files/gs", "*/bin/gswin64c.exe",
							SDL_GLOB_CASEINSENSITIVE, &n);

		if(list && n > 0) {
			char	buf[1100];

			snprintf(buf, sizeof(buf), "C:/Program Files/gs/%s", list[n - 1]);
			g_gs = strdup(buf);
		}
		SDL_free(list);
	}
#endif
}

/* ---- scratch folder ---- */

static const char *
tmp_dir(void)
{
	if(!g_tmp) {
		char	*pref = SDL_GetPrefPath("EmuBuddy", "EmuBuddy");
		char	buf[1100];

		snprintf(buf, sizeof(buf), "%sps-tmp", pref ? pref : "");
		SDL_free(pref);
		SDL_CreateDirectory(buf);
		g_tmp = strdup(buf);
	}
	return g_tmp;
}

/* ---- running Ghostscript ---- */

/* Next number in a PPM header, skipping white space and # comments. */
static int
ppm_num(FILE *f)
{
	int	c, v = 0, got = 0;

	for(;;) {
		c = fgetc(f);
		if(c == '#') {
			while(c != '\n' && c != EOF) {
				c = fgetc(f);
			}
		} else if(c >= '0' && c <= '9') {
			v = v * 10 + (c - '0');
			got = 1;
		} else if(got || c == EOF) {
			return got ? v : -1;	/* the char after the number is eaten */
		}
	}
}

static int
read_ppm(const char *path, unsigned char **rgb, int *w, int *h)
{
	FILE	*f = fopen(path, "rb");
	int	maxv;
	size_t	n;

	*rgb = NULL;
	if(!f) {
		return -1;
	}
	if(fgetc(f) != 'P' || fgetc(f) != '6' || (*w = ppm_num(f)) <= 0 ||
			(*h = ppm_num(f)) <= 0 || (maxv = ppm_num(f)) != 255 ||
			*w > 20000 || *h > 20000) {
		fclose(f);
		return -1;
	}
	n = (size_t)*w * *h * 3;
	*rgb = malloc(n);
	if(!*rgb || fread(*rgb, 1, n, f) != n) {
		free(*rgb);
		*rgb = NULL;
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

static int
collect_pages(void)
{
	char		path[1200];
	unsigned char	*rgb;
	int		i, w, h, got = 0;

	for(i = 1; i < 500; i++) {
		snprintf(path, sizeof(path), "%s/page-%03d.ppm", tmp_dir(), i);
		if(read_ppm(path, &rgb, &w, &h) != 0) {
			break;
		}
		printer_page_done_rgb(rgb, w, h, PS_DPI);
		free(rgb);
		SDL_RemovePath(path);
		got++;
	}
	if(!got) {
		ui_message("PostScript job %d gave no pages (Ghostscript could "
					"not read it)", g_cur.no);
	}
	return got;
}

static void
start_next(void)
{
	char		out[1200], res[24];
	const char	*args[16];
	int		n = 0, i;

	if(g_proc || g_nqueue == 0) {
		return;
	}
	if(!g_gs_searched) {
		find_gs();
	}
	g_cur = g_queue[0];
	for(i = 1; i < g_nqueue; i++) {
		g_queue[i - 1] = g_queue[i];
	}
	g_nqueue--;
	if(!g_gs) {
		if(!g_gs_warned) {
			g_gs_warned = 1;
			ui_message("PostScript printing needs Ghostscript "
				"(ghostscript.com, or: brew install ghostscript)");
		}
		if(g_cur.own) {
			SDL_RemovePath(g_cur.path);
		}
		return;
	}
	snprintf(out, sizeof(out), "-sOutputFile=%s/page-%%03d.ppm", tmp_dir());
	snprintf(res, sizeof(res), "-r%d", PS_DPI);
	args[n++] = g_gs;
	args[n++] = "-q";
	args[n++] = "-dNOPAUSE";
	args[n++] = "-dBATCH";
	args[n++] = "-dSAFER";
	args[n++] = "-dTextAlphaBits=4";
	args[n++] = "-dGraphicsAlphaBits=4";
	args[n++] = "-sDEVICE=ppmraw";
	args[n++] = res;
	args[n++] = out;
	args[n++] = g_cur.path;
	args[n] = NULL;
	g_proc = SDL_CreateProcess(args, false);
	g_proc_start = g_vbl_count;
	if(!g_proc) {
		ui_message("Could not start Ghostscript: %s", SDL_GetError());
		if(g_cur.own) {
			SDL_RemovePath(g_cur.path);
		}
	}
}

static void
poll_gs(void)
{
	int	code = 0;

	if(!g_proc) {
		start_next();
		return;
	}
	if(SDL_WaitProcess(g_proc, false, &code)) {
		SDL_DestroyProcess(g_proc);
		g_proc = NULL;
		if(collect_pages() > 0 && g_nkeep < PS_MAX_KEEP) {
			g_keep[g_nkeep++] = g_cur;	/* kept for a vector PDF */
		} else if(g_cur.own && !getenv("EMUBUDDY_PSDIR")) {
			SDL_RemovePath(g_cur.path);
		}
		start_next();
	} else if(g_vbl_count - g_proc_start > PS_GS_TIMEOUT) {
		SDL_KillProcess(g_proc, true);
	}
}

/* ---- printer queries ---- */

static void
reply(const char *s)
{
	size_t	n = strlen(s);

	if(g_reply_pos == g_reply_len) {
		g_reply_pos = g_reply_len = 0;
	}
	if(g_reply_len + n > sizeof(g_reply)) {
		return;
	}
	memcpy(g_reply + g_reply_len, s, n);
	g_reply_len += (int)n;
}

/* A query job asks the printer questions and waits for the answers, one
 * per line. Anything that isn't a query is a document. Returns 1 if the job
 * was a query. */
static int
answer_query(const char *job, size_t len)
{
	static const char *fonts[] = { "Courier", "Courier-Bold",
		"Courier-Oblique", "Courier-BoldOblique", "Helvetica",
		"Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique",
		"Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic",
		"Symbol", NULL };
	int	i, q = 0;
	char	*txt = malloc(len + 1);

	if(!txt) {
		return 0;
	}
	memcpy(txt, job, len);
	txt[len] = 0;
	if(strstr(txt, "%%Page:") || strstr(txt, "%%EndProlog")) {
		free(txt);
		return 0;		/* a document, even if it asks as well */
	}
	if(strstr(txt, "%%?BeginPrinterQuery") ||
			strstr(txt, "statusdict begin revision")) {
		reply("50.0\r\n2010.0\r\n(LaserWriter II NT)\r\n");
		q = 1;
	}
	if(strstr(txt, "%%?BeginFontListQuery") || strstr(txt, "FontDirectory")) {
		for(i = 0; fonts[i]; i++) {
			reply(fonts[i]);
			reply("\r\n");
		}
		q = 1;
	}
	if(q && getenv("EMUBUDDY_PSDIR")) {
		printf("PostScript printer: answered a query (%d bytes)\n", (int)len);
	}
	free(txt);
	return q;
}

/* ---- job handling ---- */

static void
finish_job(void)
{
	FILE	*f;
	PsJob	*j;
	size_t	i;
	const char *keep = getenv("EMUBUDDY_PSDIR");

	/* skip jobs that are only white space */
	for(i = 0; i < g_len && (g_buf[i] <= ' '); i++) {
	}
	if(i == g_len) {
		g_len = 0;
		return;
	}
	g_job_no++;
	if(answer_query((const char *)g_buf, g_len)) {
		g_len = 0;
		return;
	}
	if(g_nqueue >= PS_MAX_QUEUE) {
		ui_message("Printer is busy - PostScript job dropped");
		g_len = 0;
		return;
	}
	j = &g_queue[g_nqueue];
	j->no = g_job_no;
	j->own = 1;
	if(keep) {
		snprintf(j->path, sizeof(j->path), "%s/job%03d.ps", keep, g_job_no);
	} else {
		snprintf(j->path, sizeof(j->path), "%s/job%03d.ps", tmp_dir(), g_job_no);
	}
	f = fopen(j->path, "wb");
	if(f) {
		fwrite(g_buf, 1, g_len, f);
		fclose(f);
		g_nqueue++;
		start_next();
	}
	g_len = 0;
}

void
scc_ps_open(int port)
{
	g_scc[port].cur_state = 5;
	g_len = 0;
	printf("Serial port %d: PostScript printer\n", port);
}

void
scc_ps_empty_writebuf(dword64 dfcyc, int port)
{
	Scc	*scc_ptr = &(g_scc[port]);
	int	rdptr;
	byte	c;
	(void)dfcyc;

	while((rdptr = scc_ptr->out_rdptr) != scc_ptr->out_wrptr) {
		c = scc_ptr->out_buf[rdptr];
		scc_ptr->out_rdptr = (rdptr + 1) & (SCC_OUTBUF_SIZE - 1);
		g_last_vbl = g_vbl_count;
		if(c == 0x04) {			/* Ctrl-D ends a job */
			finish_job();
			continue;
		}
		if(g_len + 1 > g_cap) {
			size_t	ncap = g_cap ? g_cap * 2 : 65536;
			unsigned char *nb;

			if(ncap > PS_MAX_JOB) {
				g_len = 0;	/* runaway job: start over */
				continue;
			}
			nb = realloc(g_buf, ncap);
			if(!nb) {
				continue;
			}
			g_buf = nb;
			g_cap = ncap;
		}
		g_buf[g_len++] = c;
	}
}

void
scc_ps_fill_readbuf(dword64 dfcyc, int port, int space_left)
{
	while(g_reply_pos < g_reply_len && space_left > 0) {
		scc_add_to_readbuf(dfcyc, port, g_reply[g_reply_pos++]);
		space_left--;
	}
}

/* Once per frame: end a job after a pause, and check on Ghostscript. */
void
printer_ps_vbl(void)
{
	static int	tested;

	/* test hook: EMUBUDDY_PSFILE=<job.ps> prints that file once */
	if(!tested && g_vbl_count > 120 && getenv("EMUBUDDY_PSFILE")) {
		tested = 1;
		snprintf(g_queue[0].path, sizeof(g_queue[0].path), "%s",
						getenv("EMUBUDDY_PSFILE"));
		g_queue[0].no = ++g_job_no;
		g_queue[0].own = 0;
		g_nqueue = 1;
	}
	if(g_len && g_vbl_count - g_last_vbl >= PS_IDLE_VBL) {
		finish_job();
	}
	if(g_proc || g_nqueue) {
		poll_gs();
	}
}

/* The window's pages were cleared: the job files are no longer needed. */
void
printer_ps_forget_jobs(void)
{
	int	i;

	for(i = 0; i < g_nkeep; i++) {
		if(g_keep[i].own && !getenv("EMUBUDDY_PSDIR")) {
			SDL_RemovePath(g_keep[i].path);
		}
	}
	g_nkeep = 0;
}

/* Write all kept jobs as one vector PDF (sharp text, small files). Waits for
 * Ghostscript, which takes a moment. Returns 0 on success. */
int
printer_ps_save_pdf(const char *pdf_path)
{
	const char	*args[PS_MAX_KEEP + 12];
	char		out[1200];
	SDL_Process	*proc;
	int		n = 0, i, code = -1;

	if(g_nkeep == 0 || !g_gs) {
		return -1;
	}
	snprintf(out, sizeof(out), "-sOutputFile=%s", pdf_path);
	args[n++] = g_gs;
	args[n++] = "-q";
	args[n++] = "-dNOPAUSE";
	args[n++] = "-dBATCH";
	args[n++] = "-dSAFER";
	args[n++] = "-sDEVICE=pdfwrite";
	args[n++] = out;
	for(i = 0; i < g_nkeep; i++) {
		args[n++] = g_keep[i].path;
	}
	args[n] = NULL;
	proc = SDL_CreateProcess(args, false);
	if(!proc) {
		return -1;
	}
	SDL_WaitProcess(proc, true, &code);
	SDL_DestroyProcess(proc);
	return code == 0 ? 0 : -1;
}
