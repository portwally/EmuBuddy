/**********************************************************************/
/*                    GSplus - Apple //gs Emulator                    */
/*                    Based on KEGS by Kent Dickey                    */
/*      This code is covered by the GNU GPL v3                        */
/**********************************************************************/

/* SDL3 display/input driver and program entry point.
 *
 * KEGS has no platform-independent main(): each display driver owns the entry
 * point and the run loop, calling into the core. This file is the SDL3
 * equivalent of xdriver.c (X11) / windriver.c (Win32). The loop is:
 *
 *     parse_argv() -> kegs_init() -> sdl_video_init()
 *     while(running) { run_16ms(); poll input; push framebuffer }
 *
 * The core hands us framebuffers as Kimage objects. Each frame we ask the core
 * to copy its changed rectangles into our pixel buffer (video_out_data) and we
 * upload those to an SDL streaming texture.
 *
 * MILESTONE STATUS (Phase 2a): video + window + quit work. Keyboard/mouse input
 * (2b) and real audio (2c) are TODO; see the marked spots below.
 */

#include <SDL3/SDL.h>

#include "defc.h"
#include "protos_sdl.h"

#ifdef _WIN32
# include <sys/stat.h>
/* mingw/Windows has no lstat(). The core declares it (defc.h) and the native
 * Windows driver (windriver.c, which the SDL build excludes) provides this
 * shim -- so we supply it here. Windows has no POSIX symlinks, so stat() does. */
int
lstat(const char *path, struct stat *bufptr)
{
	return stat(path, bufptr);
}
#endif

/* Each KEGS driver defines its own private "window info" wrapper around a core
 * Kimage. Ours holds the SDL window/renderer/texture plus the scratch pixel
 * buffer the core fills. */
typedef struct {
	Kimage		*kimage_ptr;
	SDL_Window	*window;
	SDL_Renderer	*renderer;
	SDL_Texture	*texture;
	word32		*data;			/* dest buffer for video_out_data() */
	int		active;
	int		width_req;		/* current logical width  (pixels) */
	int		main_height;		/* current logical height (pixels) */
	int		pixels_per_line;	/* stride of data[]       (pixels) */
} Window_info;

static Window_info g_mainwin_info;

/* Up-front buffer size: large enough for every IIgs video mode (incl. borders
 * and scaling headroom). The actual window is sized to the current mode. */
#define SDL_MAX_WIDTH	1280
#define SDL_MAX_HEIGHT	1024

static int g_quit_requested = 0;

/* (Re)create the streaming texture at the given size. ARGB8888 matches what
 * video_out_data() writes when mdepth == 32. */
static void
sdl_create_texture(Window_info *win, int w, int h)
{
	if(win->texture) {
		SDL_DestroyTexture(win->texture);
		win->texture = NULL;
	}
	/* STATIC (not STREAMING) access: we update changed rectangles with
	 * SDL_UpdateTexture and keep the rest of the frame intact. Static textures
	 * are the documented target for SDL_UpdateTexture and preserve their
	 * contents between frames; partial SDL_UpdateTexture on a streaming texture
	 * is unreliable on the D3D11 backend (Windows showed a black window). */
	win->texture = SDL_CreateTexture(win->renderer, SDL_PIXELFORMAT_ARGB8888,
					SDL_TEXTUREACCESS_STATIC, w, h);
	if(!win->texture) {
		printf("SDL_CreateTexture failed: %s\n", SDL_GetError());
		return;
	}
	/* Opaque copy (ignore the texture's alpha) and crisp nearest-neighbour. */
	SDL_SetTextureBlendMode(win->texture, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(win->texture, SDL_SCALEMODE_NEAREST);
}

static void
sdl_video_init(void)
{
	Kimage	*km;
	int	w, h;

	if(!SDL_Init(SDL_INIT_VIDEO)) {
		printf("SDL_Init failed: %s\n", SDL_GetError());
		exit(1);
	}

	km = video_get_kimage(0);		/* 0 = main window, 1 = debugger */
	w = video_get_x_width(km);
	h = video_get_x_height(km);

	g_mainwin_info.kimage_ptr = km;
	g_mainwin_info.width_req = w;
	g_mainwin_info.main_height = h;
	g_mainwin_info.pixels_per_line = w;
	g_mainwin_info.active = 1;
	g_mainwin_info.data = calloc((size_t)SDL_MAX_WIDTH * SDL_MAX_HEIGHT,
					sizeof(word32));

	video_update_scale(km, w, h, 1);

	g_mainwin_info.window = SDL_CreateWindow("GSplus (SDL3)", w, h,
				SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if(!g_mainwin_info.window) {
		printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
		exit(1);
	}
	g_mainwin_info.renderer = SDL_CreateRenderer(g_mainwin_info.window, NULL);
	if(!g_mainwin_info.renderer) {
		printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
		exit(1);
	}
	printf("SDL renderer backend: %s\n",
		SDL_GetRendererName(g_mainwin_info.renderer));

	/* Preserve the IIgs aspect ratio, letterboxing as the window resizes. */
	SDL_SetRenderLogicalPresentation(g_mainwin_info.renderer, w, h,
					SDL_LOGICAL_PRESENTATION_LETTERBOX);

	sdl_create_texture(&g_mainwin_info, w, h);

	/* Mark this window active so the core renders into it. The X11 and Win32
	 * drivers do this in their window-create routines; without it kimage->active
	 * stays 0 and video_get_active() makes us skip every frame (black screen). */
	video_set_active(km, 1);

	/* Force the core to output the entire screen on the first frame. Without
	 * this, static content (e.g. the no-ROM config panel) is drawn once during
	 * kegs_init -- before this window/texture existed -- and never produces
	 * dirty rectangles again, leaving the texture black. The X11 and Win32
	 * drivers do the same on window expose. */
	video_set_x_refresh_needed(km, 1);
}

/* --------------------------------------------------------------------------
 * Keyboard mapping: SDL3 physical scancode -> Apple IIgs ADB raw keycode.
 *
 * We key off SDL_Scancode (the physical key position, layout-independent and
 * stable across SDL versions) rather than the layout-dependent keycode. The
 * ADB codes are exactly those the core expects (taken from xdriver.c's
 * g_x_a2_key_to_xsym table); they feed the same adb_physical_key_update() the
 * X11 and native-mac drivers use.
 *
 * Modifier keys appear twice (left/right) mapped to the same ADB code, matching
 * the IIgs which has a single code per modifier. GUI/⌘ -> Open-Apple (Command,
 * 0x37); Alt/Option -> Option (0x3a) -- consistent on macOS and PC.
 * ------------------------------------------------------------------------- */
struct sdl_key_map {
	SDL_Scancode	sc;
	int		a2;
};

static const struct sdl_key_map g_sdl_key_map[] = {
	{ SDL_SCANCODE_ESCAPE, 0x35 },
	{ SDL_SCANCODE_F1, 0x7a }, { SDL_SCANCODE_F2, 0x78 },
	{ SDL_SCANCODE_F3, 0x63 }, { SDL_SCANCODE_F4, 0x76 },
	{ SDL_SCANCODE_F5, 0x60 }, { SDL_SCANCODE_F6, 0x61 },
	{ SDL_SCANCODE_F7, 0x62 }, { SDL_SCANCODE_F8, 0x64 },
	{ SDL_SCANCODE_F9, 0x65 }, { SDL_SCANCODE_F10, 0x6d },
	{ SDL_SCANCODE_F11, 0x67 }, { SDL_SCANCODE_F12, 0x6f },
	{ SDL_SCANCODE_F13, 0x69 }, { SDL_SCANCODE_F14, 0x6b },
	{ SDL_SCANCODE_F15, 0x71 }, { SDL_SCANCODE_PAUSE, 0x7f },

	{ SDL_SCANCODE_GRAVE, 0x32 },
	{ SDL_SCANCODE_1, 0x12 }, { SDL_SCANCODE_2, 0x13 },
	{ SDL_SCANCODE_3, 0x14 }, { SDL_SCANCODE_4, 0x15 },
	{ SDL_SCANCODE_5, 0x17 }, { SDL_SCANCODE_6, 0x16 },
	{ SDL_SCANCODE_7, 0x1a }, { SDL_SCANCODE_8, 0x1c },
	{ SDL_SCANCODE_9, 0x19 }, { SDL_SCANCODE_0, 0x1d },
	{ SDL_SCANCODE_MINUS, 0x1b }, { SDL_SCANCODE_EQUALS, 0x18 },
	{ SDL_SCANCODE_BACKSPACE, 0x33 },

	{ SDL_SCANCODE_INSERT, 0x72 }, { SDL_SCANCODE_HOME, 0x73 },
	{ SDL_SCANCODE_PAGEUP, 0x74 },

	{ SDL_SCANCODE_TAB, 0x30 },
	{ SDL_SCANCODE_Q, 0x0c }, { SDL_SCANCODE_W, 0x0d },
	{ SDL_SCANCODE_E, 0x0e }, { SDL_SCANCODE_R, 0x0f },
	{ SDL_SCANCODE_T, 0x11 }, { SDL_SCANCODE_Y, 0x10 },
	{ SDL_SCANCODE_U, 0x20 }, { SDL_SCANCODE_I, 0x22 },
	{ SDL_SCANCODE_O, 0x1f }, { SDL_SCANCODE_P, 0x23 },
	{ SDL_SCANCODE_LEFTBRACKET, 0x21 }, { SDL_SCANCODE_RIGHTBRACKET, 0x1e },
	{ SDL_SCANCODE_BACKSLASH, 0x2a },
	{ SDL_SCANCODE_DELETE, 0x75 }, { SDL_SCANCODE_END, 0x77 },
	{ SDL_SCANCODE_PAGEDOWN, 0x79 },

	{ SDL_SCANCODE_CAPSLOCK, 0x39 },
	{ SDL_SCANCODE_A, 0x00 }, { SDL_SCANCODE_S, 0x01 },
	{ SDL_SCANCODE_D, 0x02 }, { SDL_SCANCODE_F, 0x03 },
	{ SDL_SCANCODE_G, 0x05 }, { SDL_SCANCODE_H, 0x04 },
	{ SDL_SCANCODE_J, 0x26 }, { SDL_SCANCODE_K, 0x28 },
	{ SDL_SCANCODE_L, 0x25 }, { SDL_SCANCODE_SEMICOLON, 0x29 },
	{ SDL_SCANCODE_APOSTROPHE, 0x27 }, { SDL_SCANCODE_RETURN, 0x24 },

	{ SDL_SCANCODE_LSHIFT, 0x38 }, { SDL_SCANCODE_RSHIFT, 0x38 },
	{ SDL_SCANCODE_Z, 0x06 }, { SDL_SCANCODE_X, 0x07 },
	{ SDL_SCANCODE_C, 0x08 }, { SDL_SCANCODE_V, 0x09 },
	{ SDL_SCANCODE_B, 0x0b }, { SDL_SCANCODE_N, 0x2d },
	{ SDL_SCANCODE_M, 0x2e }, { SDL_SCANCODE_COMMA, 0x2b },
	{ SDL_SCANCODE_PERIOD, 0x2f }, { SDL_SCANCODE_SLASH, 0x2c },
	{ SDL_SCANCODE_UP, 0x3e },

	{ SDL_SCANCODE_LCTRL, 0x36 }, { SDL_SCANCODE_RCTRL, 0x36 },
	{ SDL_SCANCODE_LALT, 0x3a }, { SDL_SCANCODE_RALT, 0x3a },  /* Option */
	{ SDL_SCANCODE_LGUI, 0x37 }, { SDL_SCANCODE_RGUI, 0x37 },  /* Open-Apple */
	{ SDL_SCANCODE_SPACE, 0x31 },
	{ SDL_SCANCODE_LEFT, 0x3b }, { SDL_SCANCODE_DOWN, 0x3d },
	{ SDL_SCANCODE_RIGHT, 0x3c },

	/* Numeric keypad */
	{ SDL_SCANCODE_NUMLOCKCLEAR, 0x47 }, { SDL_SCANCODE_KP_EQUALS, 0x51 },
	{ SDL_SCANCODE_KP_DIVIDE, 0x4b }, { SDL_SCANCODE_KP_MULTIPLY, 0x43 },
	{ SDL_SCANCODE_KP_7, 0x59 }, { SDL_SCANCODE_KP_8, 0x5b },
	{ SDL_SCANCODE_KP_9, 0x5c }, { SDL_SCANCODE_KP_MINUS, 0x4e },
	{ SDL_SCANCODE_KP_4, 0x56 }, { SDL_SCANCODE_KP_5, 0x57 },
	{ SDL_SCANCODE_KP_6, 0x58 }, { SDL_SCANCODE_KP_PLUS, 0x45 },
	{ SDL_SCANCODE_KP_1, 0x53 }, { SDL_SCANCODE_KP_2, 0x54 },
	{ SDL_SCANCODE_KP_3, 0x55 }, { SDL_SCANCODE_KP_0, 0x52 },
	{ SDL_SCANCODE_KP_PERIOD, 0x41 }, { SDL_SCANCODE_KP_ENTER, 0x4c },

	{ SDL_SCANCODE_UNKNOWN, -1 }		/* terminator */
};

static int
sdl_scancode_to_a2code(SDL_Scancode sc)
{
	int	i;

	for(i = 0; g_sdl_key_map[i].a2 >= 0; i++) {
		if(g_sdl_key_map[i].sc == sc) {
			return g_sdl_key_map[i].a2;
		}
	}
	return -1;
}

/* Translate SDL's modifier state into the IIgs c025 modifier register
 * (bit0 = shift, bit1 = control, bit2 = caps lock), as x_update_modifier_state
 * does for X11. */
static void
sdl_update_modifiers(Window_info *win)
{
	SDL_Keymod mod = SDL_GetModState();
	word32	c025_val = 0;

	if(mod & SDL_KMOD_SHIFT) { c025_val |= 1; }
	if(mod & SDL_KMOD_CTRL)  { c025_val |= 2; }
	if(mod & SDL_KMOD_CAPS)  { c025_val |= 4; }
	adb_update_c025_mask(win->kimage_ptr, c025_val, 7);
}

static void
sdl_handle_key(Window_info *win, SDL_Scancode sc, int is_up, int repeat)
{
	int	a2code;

	if(repeat) {
		return;		/* the IIgs ADB does its own key repeat */
	}
	sdl_update_modifiers(win);

	a2code = sdl_scancode_to_a2code(sc);
	if(a2code >= 0) {
		adb_physical_key_update(win->kimage_ptr, a2code, 0, is_up);
	}
}

/* Mouse button index -> IIgs button mask. SDL buttons are 1=left, 2=middle,
 * 3=right; (1 << b) >> 1 gives left=1, middle=2, right=4 (same as xdriver). */
static int
sdl_button_mask(int sdl_button)
{
	return (1 << sdl_button) >> 1;
}

static void
sdl_poll_events(void)
{
	SDL_Event ev;
	Window_info *win = &g_mainwin_info;
	int	mask, mx, my;

	while(SDL_PollEvent(&ev)) {
		switch(ev.type) {
		case SDL_EVENT_QUIT:
			g_quit_requested = 1;
			break;
		case SDL_EVENT_WINDOW_EXPOSED:
		case SDL_EVENT_WINDOW_SHOWN:
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		case SDL_EVENT_WINDOW_RESTORED:
			/* Repaint the whole screen when the window (re)appears or resizes. */
			video_set_x_refresh_needed(g_mainwin_info.kimage_ptr, 1);
			break;
		case SDL_EVENT_KEY_DOWN:
			sdl_handle_key(win, ev.key.scancode, 0, ev.key.repeat);
			break;
		case SDL_EVENT_KEY_UP:
			sdl_handle_key(win, ev.key.scancode, 1, 0);
			break;
		case SDL_EVENT_MOUSE_MOTION:
			/* Map window coords through the renderer's logical
			 * presentation, then into the IIgs framebuffer. */
			SDL_ConvertEventToRenderCoordinates(win->renderer, &ev);
			mx = video_scale_mouse_x(win->kimage_ptr, (int)ev.motion.x, 0);
			my = video_scale_mouse_y(win->kimage_ptr, (int)ev.motion.y, 0);
			adb_update_mouse(win->kimage_ptr, mx, my, 0, 0);
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			SDL_ConvertEventToRenderCoordinates(win->renderer, &ev);
			mask = sdl_button_mask(ev.button.button);
			mx = video_scale_mouse_x(win->kimage_ptr, (int)ev.button.x, 0);
			my = video_scale_mouse_y(win->kimage_ptr, (int)ev.button.y, 0);
			adb_update_mouse(win->kimage_ptr, mx, my,
				(ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? mask : 0,
				mask & 7);
			break;
		default:
			break;
		}
	}
}

static void
sdl_update_display(Window_info *win)
{
	Change_rect rect;
	int	i, w, h;
	word32	*src;
	SDL_Rect r;

	if(!win->renderer) {
		return;
	}
	if(!video_get_active(win->kimage_ptr)) {
		return;
	}

	/* If the IIgs changed video mode, the logical size changes: resize the
	 * window and rebuild the texture to match. */
	if(video_change_aspect_needed(win->kimage_ptr, win->width_req,
						win->main_height)) {
		w = video_get_x_width(win->kimage_ptr);
		h = video_get_x_height(win->kimage_ptr);
		win->width_req = w;
		win->main_height = h;
		win->pixels_per_line = w;
		video_update_scale(win->kimage_ptr, w, h, 1);
		SDL_SetWindowSize(win->window, w, h);
		SDL_SetRenderLogicalPresentation(win->renderer, w, h,
					SDL_LOGICAL_PRESENTATION_LETTERBOX);
		sdl_create_texture(win, w, h);
	}

	/* Ask the core for each changed rectangle (it writes pixels into our
	 * buffer) and upload that rectangle to the texture. */
	int dbg_rects = 0, dbg_upd_ok = 1;	/* DIAG */
	for(i = 0; i < MAX_CHANGE_RECTS; i++) {
		if(!video_out_data(win->data, win->kimage_ptr,
					win->pixels_per_line, &rect, i)) {
			break;
		}
		r.x = rect.x;
		r.y = rect.y;
		r.w = rect.width;
		r.h = rect.height;
		src = win->data + (size_t)rect.y * win->pixels_per_line + rect.x;
		if(!SDL_UpdateTexture(win->texture, &r, src,
				win->pixels_per_line * (int)sizeof(word32))) {
			dbg_upd_ok = 0;		/* DIAG */
		}
		dbg_rects++;			/* DIAG */
	}

	bool dbg_c = SDL_RenderClear(win->renderer);		/* DIAG */
	bool dbg_t = SDL_RenderTexture(win->renderer, win->texture, NULL, NULL); /* DIAG */
	bool dbg_p = SDL_RenderPresent(win->renderer);		/* DIAG */
	{ static int df = 0;					/* DIAG */
	  if(df++ < 4) {
		int ow = 0, oh = 0;
		SDL_GetRenderOutputSize(win->renderer, &ow, &oh);
		printf("DIAG frame %d: rects=%d updOK=%d clear=%d tex=%d present=%d "
			"texsz=%dx%d out=%dx%d err='%s'\n", df, dbg_rects, dbg_upd_ok,
			dbg_c, dbg_t, dbg_p, win->width_req, win->main_height,
			ow, oh, SDL_GetError());
	  } }
}

int
main(int argc, char **argv)
{
	int	mdepth = 32;		/* ARGB8888 -> 32-bit pixels */
	int	ret;

	ret = parse_argv(argc, argv, 1);
	if(ret) {
		printf("parse_argv ret: %d, stopping\n", ret);
		exit(1);
	}

	ret = kegs_init(mdepth, SDL_MAX_WIDTH, SDL_MAX_HEIGHT, 0);
	if(ret) {
		printf("kegs_init ret: %d, stopping\n", ret);
		exit(1);
	}

	sdl_video_init();

	/* Main loop: run_16ms() runs one video frame's worth of CPU + video. */
	while(!g_quit_requested) {
		ret = run_16ms();
		if(ret != 0) {
			printf("run_16ms returned: %d\n", ret);
			break;
		}
		sdl_poll_events();
		sdl_update_display(&g_mainwin_info);
	}

	sdl_snd_shutdown();
	SDL_Quit();
	return 0;
}
