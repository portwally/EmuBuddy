/* EmuBuddy: record the emulator picture and sound to an MP4 file.
 *
 * The frames (one per emulated 1/60 s) go to FFmpeg through a pipe and are
 * encoded as H.264; the sound is written to a temporary raw file. When the
 * recording stops, FFmpeg adds the sound (AAC) and the MP4 goes to the Movies
 * folder. FFmpeg is a separate program that is not shipped with EmuBuddy: if it
 * is missing, a window explains how to get it (like the Ghostscript one).
 *
 * Time is emulated time, so the video stays in sync with the sound even when
 * the emulator runs faster than a real IIgs. */
#include "ui.h"
#include "pathutil.h"
#include "tool_find.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int	g_preferred_rate;

#define REC_FPS		60

static char		*g_ff;
static int		g_ff_searched, g_active, g_w, g_h, g_rate;
static long		g_nframes;
static long		g_abytes;		/* sound bytes written so far */
static SDL_Process	*g_enc;
static SDL_IOStream	*g_in;
static FILE		*g_afile;
static char		g_vpath[1200], g_apath[1200], g_out[1200];
static unsigned char	*g_buf;

static UiWindow		g_ff_win;
static int		g_ff_tried;

/* ---- "FFmpeg needed" window ---- */

static void
ff_draw(UiWindow *win, int cx, int cy)
{
	const char	*plat = SDL_GetPlatform(), *how;
	int		by = win->h - UI_TITLE_H - 38;

	if(!strcmp(plat, "macOS")) {
		how = "In Terminal:  brew install ffmpeg";
	} else if(!strcmp(plat, "Windows")) {
		how = "In a console:  winget install ffmpeg";
	} else {
		how = "In a terminal: sudo apt install ffmpeg";
	}
	ui_draw_text(cx + 16, cy + 14, "Recording video needs FFmpeg.", UI_BLACK);
	ui_draw_text(cx + 16, cy + 34, "It is free. Install it, then choose", UI_BLACK);
	ui_draw_text(cx + 16, cy + 52, "Record Video again.", UI_BLACK);
	ui_draw_text(cx + 16, cy + 82, how, UI_BLACK);
	if(g_ff_tried) {
		ui_draw_text(cx + 16, cy + 108, "Not found yet.", UI_BAD);
	}
	if(ui_button(win, cx, cy, 12, by, 120, 26, "Download Page", 1)) {
		SDL_OpenURL("https://ffmpeg.org/download.html");
	}
	if(ui_button(win, cx, cy, 142, by, 100, 26, "Try Again", 1)) {
		free(g_ff);
		static const char *names[] = { "ffmpeg", "ffmpeg.exe", NULL };
		g_ff = tool_find("EMUBUDDY_FFMPEG", names, "C:/Program Files/ffmpeg",
							"bin/ffmpeg.exe");
		if(g_ff) {
			ui_window_close(win);
			ui_message("FFmpeg found - choose Record Video");
		} else {
			g_ff_tried = 1;
		}
	}
	if(ui_button(win, cx, cy, win->w - 2 - 12 - 90, by, 90, 26, "Close", 1)) {
		ui_window_close(win);
	}
}

static void
need_ffmpeg(void)
{
	g_ff_tried = 0;
	if(g_ff_win.open) {
		return;
	}
	snprintf(g_ff_win.title, sizeof(g_ff_win.title), "FFmpeg Needed");
	g_ff_win.w = 360;
	g_ff_win.h = 200;
	g_ff_win.closable = 0;
	g_ff_win.modal = 0;
	g_ff_win.draw = ff_draw;
	ui_window_center(&g_ff_win);
	ui_window_open(&g_ff_win);
}

/* ---- recording ---- */

/* 1 from Record Video until the file is finished. */
int
rec_active(void)
{
	return g_enc != NULL;
}

double
rec_seconds(void)
{
	return (double)g_nframes / REC_FPS;
}

static void
make_paths(void)
{
	char		*pref = SDL_GetPrefPath("EmuBuddy", "EmuBuddy");
	const char	*mov = SDL_GetUserFolder(SDL_FOLDER_VIDEOS);
	char		stamp[64];
	time_t		t = time(NULL);
	struct tm	*tm = localtime(&t);

	if(tm) {
		strftime(stamp, sizeof(stamp), "%Y-%m-%d %H%M%S", tm);
	} else {
		snprintf(stamp, sizeof(stamp), "%ld", (long)t);
	}
	snprintf(g_vpath, sizeof(g_vpath), "%srec-video.mp4", pref ? pref : "");
	snprintf(g_apath, sizeof(g_apath), "%srec-audio.raw", pref ? pref : "");
	if(!mov) {
		mov = SDL_GetUserFolder(SDL_FOLDER_HOME);
	}
	snprintf(g_out, sizeof(g_out), "%sEmuBuddy Recording %s.mp4",
						mov ? mov : "", stamp);
	SDL_free(pref);
}

/* Starts recording frames of w x h pixels (made even). Returns 0 if it
 * started; -1 if FFmpeg is missing (the help window is shown) or failed. */
int
rec_start(int w, int h)
{
	static const char *names[] = { "ffmpeg", "ffmpeg.exe", NULL };
	char		size[32], rate[16];
	const char	*args[32];
	int		n = 0;
	SDL_PropertiesID props;

	if(g_active) {
		return 0;
	}
	if(!g_ff_searched) {
		g_ff_searched = 1;
		g_ff = tool_find("EMUBUDDY_FFMPEG", names, "C:/Program Files/ffmpeg",
							"bin/ffmpeg.exe");
	}
	if(!g_ff) {
		need_ffmpeg();
		return -1;
	}
	g_w = w & ~1;
	g_h = h & ~1;
	make_paths();
	snprintf(size, sizeof(size), "%dx%d", g_w, g_h);
	snprintf(rate, sizeof(rate), "%d", REC_FPS);
	args[n++] = g_ff;
	args[n++] = "-y";
	args[n++] = "-loglevel";
	args[n++] = "error";
	args[n++] = "-f";
	args[n++] = "rawvideo";
	args[n++] = "-pix_fmt";
	args[n++] = "rgba";
	args[n++] = "-s";
	args[n++] = size;
	args[n++] = "-framerate";
	args[n++] = rate;
	args[n++] = "-i";
	args[n++] = "-";
	args[n++] = "-c:v";
	args[n++] = "libx264";
	args[n++] = "-preset";
	args[n++] = "veryfast";
	args[n++] = "-crf";
	args[n++] = "18";
	args[n++] = "-pix_fmt";
	args[n++] = "yuv420p";
	args[n++] = g_vpath;
	args[n] = NULL;
	props = SDL_CreateProperties();
	SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
							(void *)args);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
							SDL_PROCESS_STDIO_APP);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
							SDL_PROCESS_STDIO_NULL);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
							SDL_PROCESS_STDIO_NULL);
	g_enc = SDL_CreateProcessWithProperties(props);
	SDL_DestroyProperties(props);
	if(!g_enc || !(g_in = SDL_GetProcessInput(g_enc))) {
		ui_message("Could not start FFmpeg: %s", SDL_GetError());
		if(g_enc) {
			SDL_DestroyProcess(g_enc);
			g_enc = NULL;
		}
		return -1;
	}
	g_afile = fopen(g_apath, "wb");
	g_abytes = 0;
	g_rate = g_preferred_rate > 0 ? g_preferred_rate : 48000;
	g_buf = malloc((size_t)g_w * g_h * 4);
	g_nframes = 0;
	g_active = 1;
	ui_message("Recording video - choose Record Video again to stop");
	return 0;
}

/* One frame: tightly packed RGBA, g_w x g_h (the caller scales it). */
void
rec_frame(const unsigned char *rgba)
{
	size_t	n = (size_t)g_w * g_h * 4;

	if(!g_active) {
		return;
	}
	/* The core stops sending sound while the IIgs is silent. Fill the gap with
	 * silence so the sound track is as long as the video and stays in sync. */
	if(g_afile) {
		long	perframe = (long)g_rate * 4 / REC_FPS;
		long	need = ((long)((g_nframes + 1) * (double)g_rate * 4 /
							REC_FPS)) & ~3L;
		static const unsigned char zeros[4096];

		while(need - g_abytes >= perframe / 2 + 4) {
			long chunk = need - g_abytes;

			if(chunk > (long)sizeof(zeros)) {
				chunk = sizeof(zeros);
			}
			chunk &= ~3L;
			fwrite(zeros, 1, (size_t)chunk, g_afile);
			g_abytes += chunk;
		}
	}
	/* A pipe can take part of a frame at a time: keep writing the rest. */
	size_t	off = 0;
	int	idle = 0;

	while(off < n) {
		size_t w = SDL_WriteIO(g_in, rgba + off, n - off);

		if(w > 0) {
			off += w;
			idle = 0;
		} else if(++idle > 2000) {		/* FFmpeg is not reading (2 s) */
			ui_message("FFmpeg stopped - recording ended");
			g_active = 0;		/* rec_stop() cleans up */
			return;
		} else {
			SDL_Delay(1);
		}
	}
	g_nframes++;
}

void
rec_get_size(int *w, int *h)
{
	*w = g_w;
	*h = g_h;
}

/* Sound that the emulator produced (stereo 16-bit at g_preferred_rate). */
void
rec_audio(const unsigned char *pcm, int n)
{
	if(g_active && g_afile) {
		fwrite(pcm, 1, (size_t)n, g_afile);
		g_abytes += n;
	}
}

static int
run_ffmpeg_wait(const char **args)
{
	SDL_Process	*p = SDL_CreateProcess(args, false);
	int		code = -1;

	if(!p) {
		return -1;
	}
	SDL_WaitProcess(p, true, &code);
	SDL_DestroyProcess(p);
	return code;
}

/* Finish the file: close the pipe, wait for the encoder, add the sound. */
void
rec_stop(void)
{
	int	code = -1, ok = 0;
	char	rate[16];

	if(!g_enc) {
		return;
	}
	g_active = 0;
	if(g_afile) {
		fclose(g_afile);
		g_afile = NULL;
	}
	SDL_CloseIO(g_in);			/* end of input: FFmpeg finishes the file */
	g_in = NULL;
	SDL_WaitProcess(g_enc, true, &code);
	SDL_DestroyProcess(g_enc);
	g_enc = NULL;
	free(g_buf);
	g_buf = NULL;
	if(code == 0 && g_nframes > 0) {
		SDL_PathInfo	ai;
		const char	*a[40];
		int		n = 0;

		snprintf(rate, sizeof(rate), "%d", g_rate);
		if(SDL_GetPathInfo(g_apath, &ai) && ai.size > 0) {
			a[n++] = g_ff; a[n++] = "-y"; a[n++] = "-loglevel";
			a[n++] = "error"; a[n++] = "-i"; a[n++] = g_vpath;
			a[n++] = "-f"; a[n++] = "s16le"; a[n++] = "-ar";
			a[n++] = rate; a[n++] = "-ac"; a[n++] = "2";
			a[n++] = "-i"; a[n++] = g_apath; a[n++] = "-c:v";
			a[n++] = "copy"; a[n++] = "-c:a"; a[n++] = "aac";
			a[n++] = "-b:a"; a[n++] = "160k"; a[n++] = "-shortest";
			a[n++] = "-movflags"; a[n++] = "+faststart";
			a[n++] = g_out; a[n] = NULL;
			ok = (run_ffmpeg_wait(a) == 0);
		} else {
			ok = SDL_CopyFile(g_vpath, g_out);
		}
	}
	SDL_RemovePath(g_vpath);
	SDL_RemovePath(g_apath);
	if(ok) {
		ui_message("Saved to Movies: %s", path_base(g_out));
		printf("Recording saved: %s (%.1f s)\n", g_out, rec_seconds());
	} else {
		ui_message("Recording failed");
	}
}
