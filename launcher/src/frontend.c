#include "frontend.h"
#include "ui.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static SDL_Renderer	*g_r;
static FeData		g_d;
static UiFont		*g_small, *g_mid, *g_big;
static float		g_pos;			/* animated position in the row */
static int		g_info, g_pad_hints;
static char		g_search[64];
static Uint64		g_last;
static float		g_bg_fade = 1;		/* 0..1, 1 = the current background is fully in */
static char		g_bg_cur[1200], g_bg_prev[1200];

/* ---- artwork: finding and caching pictures ---- */

#define CACHE_MAX	48

typedef struct {
	char		path[1200];
	SDL_Texture	*tex, *blur;
	int		w, h;
	int		failed;
	Uint64		used;
} Pic;

static Pic	g_cache[CACHE_MAX];
static Uint64	g_tick;
static int	g_loaded_this_frame;

static int
name_eq(const char *a, const char *b)
{
	return !strcasecmp(a, b);
}

/* dir/<name>.png (or .jpg) found without caring about upper/lower case */
static int
find_in(const char *dir, const char *name, char *out, int outsize)
{
	static const char *exts[] = { ".png", ".PNG", ".bmp", NULL };
	DIR		*d = opendir(dir);
	struct dirent	*e;
	int		found = 0;

	if(!d) {
		return 0;
	}
	while((e = readdir(d)) != NULL && !found) {
		const char *dot = strrchr(e->d_name, '.');
		char	stem[300];
		int	ok = 0, i;

		if(!dot || e->d_name[0] == '.') {
			continue;
		}
		for(i = 0; exts[i]; i++) {
			if(!strcasecmp(dot, exts[i])) {
				ok = 1;
			}
		}
		if(!ok) {
			continue;
		}
		snprintf(stem, sizeof(stem), "%.*s", (int)(dot - e->d_name), e->d_name);
		if(name_eq(stem, name)) {
			snprintf(out, (size_t)outsize, "%s/%s", dir, e->d_name);
			found = 1;
		}
	}
	closedir(d);
	return found;
}

int
fe_find_art(const Profile *p, const char *root, int kind, char *out, int outsize)
{
	static const char *tile_dirs[] = { "boxart", "flyer", "snap", "video", "screenshots", "marquee", NULL };
	static const char *logo_dirs[] = { "wheel", "logo", NULL };
	const char	*roots[2], *name = profile_get(p, "art_name")[0] ? profile_get(p, "art_name") : profile_name(p);
	const char	*own_dirs = profile_get(p, kind == 0 ? "art_tile" : "art_logo");
	char		own[1200], full[1300], dir[1400], list[1300], *tok, *save = NULL;
	int		r, i;

	if(kind == 0 && profile_get(p, "cover")[0]) {		/* the cover chosen in the editor */
		profile_expand_path(profile_get(p, "cover"), out, outsize);
		return 1;
	}
	if(own_dirs[0]) {					/* folders named exactly (from an import) */
		snprintf(list, sizeof(list), "%s", own_dirs);
		for(tok = strtok_r(list, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
			profile_expand_path(tok, dir, sizeof(dir));
			if(find_in(dir, name, out, outsize) ||
					(strcmp(name, profile_name(p)) != 0 && find_in(dir, profile_name(p), out, outsize))) {
				return 1;
			}
		}
	}
	own[0] = 0;
	profile_expand_path(profile_get(p, "artwork"), own, sizeof(own));
	roots[0] = own;
	profile_expand_path(root ? root : "", full, sizeof(full));
	roots[1] = full;
	for(r = 0; r < 2; r++) {
		const char **dirs = kind == 0 ? tile_dirs : logo_dirs;

		if(!roots[r][0]) {
			continue;
		}
		for(i = 0; dirs[i]; i++) {
			snprintf(dir, sizeof(dir), "%s/%s", roots[r], dirs[i]);
			if(find_in(dir, name, out, outsize)) {
				return 1;
			}
		}
	}
	return 0;
}

/* a few passes of a 3x3 box blur make a soft, round blur (about a gaussian) */
static void
box_blur(SDL_Surface *sf, int passes)
{
	int	w = sf->w, h = sf->h, x, y, k, c;
	Uint8	*tmp = malloc((size_t)w * h * 4);

	if(!tmp) {
		return;
	}
	for(k = 0; k < passes; k++) {
		for(y = 0; y < h; y++) {
			for(x = 0; x < w; x++) {
				for(c = 0; c < 4; c++) {
					int sum = 0, cnt = 0;

					for(int dy = -1; dy <= 1; dy++) {
						for(int dx = -1; dx <= 1; dx++) {
							int xx = x + dx, yy = y + dy;

							if(xx >= 0 && xx < w && yy >= 0 && yy < h) {
								sum += ((Uint8 *)sf->pixels)[yy * sf->pitch + xx * 4 + c];
								cnt++;
							}
						}
					}
					tmp[(y * w + x) * 4 + c] = (Uint8)(sum / cnt);
				}
			}
		}
		for(y = 0; y < h; y++) {
			memcpy((Uint8 *)sf->pixels + y * sf->pitch, tmp + y * w * 4, (size_t)w * 4);
		}
	}
	free(tmp);
}

static Pic *
pic_get(const char *path)
{
	int	i, oldest = 0;
	Pic	*p;

	if(!path[0]) {
		return NULL;
	}
	for(i = 0; i < CACHE_MAX; i++) {
		if(!strcmp(g_cache[i].path, path)) {
			g_cache[i].used = ++g_tick;
			return g_cache[i].failed ? NULL : (g_cache[i].tex ? &g_cache[i] : NULL);
		}
	}
	if(g_loaded_this_frame >= 1) {
		return NULL;			/* one picture per frame keeps scrolling smooth */
	}
	g_loaded_this_frame++;
	for(i = 0; i < CACHE_MAX; i++) {
		if(g_cache[i].used < g_cache[oldest].used) {
			oldest = i;
		}
	}
	p = &g_cache[oldest];
	if(p->tex) {
		SDL_DestroyTexture(p->tex);
	}
	if(p->blur) {
		SDL_DestroyTexture(p->blur);
	}
	memset(p, 0, sizeof(*p));
	snprintf(p->path, sizeof(p->path), "%s", path);
	p->used = ++g_tick;
	{
		SDL_Surface *sf = SDL_LoadSurface(path), *sm;

		if(!sf) {
			p->failed = 1;
			return NULL;
		}
		if(sf->w > 960) {		/* big screen shots: keep a smaller copy */
			sm = SDL_ScaleSurface(sf, 960, sf->h * 960 / sf->w, SDL_SCALEMODE_LINEAR);
			if(sm) {
				SDL_DestroySurface(sf);
				sf = sm;
			}
		}
		p->w = sf->w;
		p->h = sf->h;
		p->tex = SDL_CreateTextureFromSurface(g_r, sf);
		if(p->tex) {
			SDL_SetTextureScaleMode(p->tex, SDL_SCALEMODE_LINEAR);
		}
		sm = SDL_ScaleSurface(sf, 48, 27, SDL_SCALEMODE_LINEAR);	/* the blur: tiny, blurred, drawn big */
		if(sm) {
			SDL_Surface *cv = SDL_ConvertSurface(sm, SDL_PIXELFORMAT_RGBA32);

			if(cv) {
				SDL_DestroySurface(sm);
				sm = cv;
				box_blur(sm, 3);
			}
			p->blur = SDL_CreateTextureFromSurface(g_r, sm);
			if(p->blur) {
				SDL_SetTextureScaleMode(p->blur, SDL_SCALEMODE_LINEAR);
			}
			SDL_DestroySurface(sm);
		}
		SDL_DestroySurface(sf);
	}
	if(!p->tex) {
		p->failed = 1;
		return NULL;
	}
	return p;
}

/* ---- little drawing helpers ---- */

static void
rgba(Uint32 c, int a)
{
	SDL_SetRenderDrawColor(g_r, (c >> 16) & 255, (c >> 8) & 255, c & 255, (Uint8)a);
}

static void
fill(float x, float y, float w, float h, Uint32 c, int a)
{
	SDL_FRect r = { x, y, w, h };

	SDL_SetRenderDrawBlendMode(g_r, SDL_BLENDMODE_BLEND);
	rgba(c, a);
	SDL_RenderFillRect(g_r, &r);
}

/* a vertical gradient from alpha a0 at the top to a1 at the bottom */
static void
vgrad(float x, float y, float w, float h, Uint32 c, int a0, int a1)
{
	SDL_Vertex v[4];
	int	idx[6] = { 0, 1, 2, 2, 1, 3 };
	SDL_FColor top = { ((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, a0 / 255.0f };
	SDL_FColor bot = top;

	bot.a = a1 / 255.0f;
	v[0].position = (SDL_FPoint){ x, y };
	v[1].position = (SDL_FPoint){ x + w, y };
	v[2].position = (SDL_FPoint){ x, y + h };
	v[3].position = (SDL_FPoint){ x + w, y + h };
	v[0].color = top; v[1].color = top; v[2].color = bot; v[3].color = bot;
	for(int i = 0; i < 4; i++) {
		v[i].tex_coord = (SDL_FPoint){ 0, 0 };
	}
	SDL_SetRenderDrawBlendMode(g_r, SDL_BLENDMODE_BLEND);
	SDL_RenderGeometry(g_r, NULL, v, 4, idx, 6);
}

static void
text(UiFont *f, int x, int y, const char *s, Uint32 c, int a)
{
	ui_font_set(f);
	ui_draw_text_a(x, y, s, c, a);
	ui_font_set(NULL);
}

static int
text_w(UiFont *f, const char *s)
{
	int w;

	ui_font_set(f);
	w = ui_text_width(s);
	ui_font_set(NULL);
	return w;
}

/* cover-fit a picture into a rectangle */
static void
draw_pic_cover(Pic *p, float x, float y, float w, float h, int alpha)
{
	float	sa = (float)p->w / (float)p->h, da = w / h;
	SDL_FRect src = { 0, 0, (float)p->w, (float)p->h }, dst = { x, y, w, h };

	if(sa > da) {			/* picture is wider: crop the sides */
		src.w = p->h * da;
		src.x = (p->w - src.w) / 2;
	} else {
		src.h = p->w / da;
		src.y = (p->h - src.h) / 2;
	}
	SDL_SetTextureAlphaMod(p->tex, (Uint8)alpha);
	SDL_RenderTexture(g_r, p->tex, &src, &dst);
	SDL_SetTextureAlphaMod(p->tex, 255);
}

/* picture fitted inside (letterboxed), for logos */
static void
draw_pic_fit(Pic *p, float x, float y, float w, float h, int alpha)
{
	float	s = SDL_min(w / p->w, h / p->h);
	SDL_FRect dst = { x + (w - p->w * s) / 2, y + (h - p->h * s) / 2, p->w * s, p->h * s };

	SDL_SetTextureAlphaMod(p->tex, (Uint8)alpha);
	SDL_RenderTexture(g_r, p->tex, NULL, &dst);
	SDL_SetTextureAlphaMod(p->tex, 255);
}

/* ---- setup ---- */

int
fe_init(SDL_Renderer *r, const char *fonts_dir)
{
	char	path[1400];

	g_r = r;
	snprintf(path, sizeof(path), "%s/Figtree-SemiBold.ttf", fonts_dir);
	g_small = ui_font_open(path, 19);
	g_mid = ui_font_open(path, 28);
	snprintf(path, sizeof(path), "%s/Figtree-Bold.ttf", fonts_dir);
	g_big = ui_font_open(path, 50);
	return g_small && g_mid && g_big ? 0 : -1;
}

void
fe_shutdown(void)
{
	for(int i = 0; i < CACHE_MAX; i++) {
		if(g_cache[i].tex) {
			SDL_DestroyTexture(g_cache[i].tex);
		}
		if(g_cache[i].blur) {
			SDL_DestroyTexture(g_cache[i].blur);
		}
	}
	memset(g_cache, 0, sizeof(g_cache));
	ui_font_close(g_small);
	ui_font_close(g_mid);
	ui_font_close(g_big);
	g_small = g_mid = g_big = NULL;
}

void
fe_set_data(const FeData *d)
{
	g_d = *d;
}

int
fe_info_open(void)
{
	return g_info;
}

void
fe_set_pad_hints(int on)
{
	g_pad_hints = on;
}

/* ---- the view ---- */

static float	g_tx[64], g_ty[64], g_tw[64], g_th[64];	/* tile rectangles of the last frame */
static int	g_ti[64];				/* which profile each one is */
static int	g_ntiles;

static Uint32
hue_color(const char *name)
{
	unsigned h = 0;
	float	hh, r, g, b;
	int	i;

	for(const char *p = name; *p; p++) {
		h = h * 31 + (unsigned char)*p;
	}
	hh = (float)(h % 360) / 60.0f;
	i = (int)hh;
	r = i == 0 || i == 5 ? 1 : i == 1 ? 1 - (hh - i) : i == 4 ? hh - i : 0;
	g = i == 1 || i == 2 ? 1 : i == 0 ? hh - i : i == 3 ? 1 - (hh - i) : 0;
	b = i == 3 || i == 4 ? 1 : i == 2 ? hh - i : i == 5 ? 1 - (hh - i) : 0;
	return ((Uint32)(60 + r * 90) << 16) | ((Uint32)(60 + g * 90) << 8) | (Uint32)(70 + b * 100);
}

/* friendly facts instead of settings */
static void
facts(const Profile *p, char *out, int n)
{
	char	t[200];
	int	hard = 0, flop = 0;

	out[0] = 0;
	if(profile_get(p, "ramsize")[0]) {
		snprintf(t, sizeof(t), "%s memory", profile_get(p, "ramsize"));
		SDL_strlcat(out, t, (size_t)n);
	}
	if(profile_get(p, "mhz")[0]) {
		snprintf(t, sizeof(t), "%s%s MHz", out[0] ? "  -  " : "", profile_get(p, "mhz"));
		SDL_strlcat(out, t, (size_t)n);
	}
	if(!strcmp(profile_get(p, "sl3"), "uthernet2") || !strcmp(profile_get(p, "sl4"), "uthernet2")) {
		SDL_strlcat(out, "  -  Internet", (size_t)n);
	}
	if(!strcmp(profile_get(p, "sl4"), "mockingboard")) {
		SDL_strlcat(out, "  -  Sound card", (size_t)n);
	}
	if(!strcmp(profile_get(p, "sl1"), "imagewriter") || !strcmp(profile_get(p, "sl1"), "postscript")) {
		SDL_strlcat(out, "  -  Printer", (size_t)n);
	}
	for(int i = 1; i <= 7; i++) {
		char k[16];

		snprintf(k, sizeof(k), "hard%d", i);
		hard += profile_get(p, k)[0] != 0;
	}
	for(int i = 1; i <= 4; i++) {
		char k[16];

		snprintf(k, sizeof(k), "flop%d", i);
		flop += profile_get(p, k)[0] != 0;
	}
	if(hard + flop) {
		snprintf(t, sizeof(t), "%s%d disk%s", out[0] ? "  -  " : "", hard + flop, hard + flop == 1 ? "" : "s");
		SDL_strlcat(out, t, (size_t)n);
	}
}

static int
search_match(const char *name)
{
	return !g_search[0] || strcasestr(name, g_search) != NULL;
}

static void
draw_tile(const Profile *p, float x, float y, float w, float h, float sel, int alpha)
{
	char	art[1300];
	Pic	*pic = NULL;
	float	shadow = 10 + 14 * sel;

	fill(x - 6, y + 6, w + 12, h + 4 + shadow * 0.4f, 0x000000, (int)(alpha * 0.28f));
	if(fe_find_art(p, g_d.artwork_root, 0, art, sizeof(art))) {
		pic = pic_get(art);
	}
	if(pic) {
		draw_pic_cover(pic, x, y, w, h, alpha);
	} else {
		Uint32 c = hue_color(profile_name(p));
		char	ini[2] = { profile_name(p)[0], 0 };

		fill(x, y, w, h, c, alpha);
		vgrad(x, y, w, h, 0x000000, 0, (int)(alpha * 0.5f));
		text(g_big, (int)(x + w / 2 - text_w(g_big, ini) / 2), (int)(y + h / 2 - 30), ini, 0xFFFFFF, (int)(alpha * 0.9f));
	}
	if(sel > 0.5f) {			/* a bright frame on the selected one */
		rgba(0xFFFFFF, (int)(alpha * 0.9f * sel));
		for(int i = 0; i < 3; i++) {
			SDL_FRect b = { x - i, y - i, w + 2 * i, h + 2 * i };

			SDL_RenderRect(g_r, &b);
		}
	}
}

void
fe_draw(int W, int H)
{
	Uint64	now = SDL_GetTicks();
	float	dt = g_last ? (now - g_last) / 1000.0f : 0.016f;
	int	n = g_d.count ? *g_d.count : 0, sel = g_d.sel ? *g_d.sel : 0, i;
	float	u = H / 720.0f, tile_h = 230 * u, tile_w = tile_h * 1.5f, gap = 30 * u;
	char	art[1300];

	g_last = now;
	g_loaded_this_frame = 0;
	if(dt > 0.1f) {
		dt = 0.1f;
	}
	SDL_SetRenderDrawBlendMode(g_r, SDL_BLENDMODE_BLEND);
	rgba(0x0A0B10, 255);
	SDL_RenderClear(g_r);
	if(n == 0) {
		text(g_mid, W / 2 - 220, H / 2 - 20, "No games yet. Press Space to add one.", 0xFFFFFF, 220);
		return;
	}
	if(sel >= n) {
		sel = n - 1;
	}
	/* the position follows the selection smoothly */
	g_pos += ((float)sel - g_pos) * SDL_min(1.0f, dt * 11.0f);
	if(fabsf(g_pos - sel) < 0.002f) {
		g_pos = (float)sel;
	}

	/* blurred background of the selected cover, fading when it changes */
	if(!fe_find_art(&g_d.list[sel], g_d.artwork_root, 0, art, sizeof(art))) {
		art[0] = 0;
	}
	if(strcmp(art, g_bg_cur) != 0) {
		snprintf(g_bg_prev, sizeof(g_bg_prev), "%s", g_bg_cur);
		snprintf(g_bg_cur, sizeof(g_bg_cur), "%s", art);
		g_bg_fade = 0;
	}
	g_bg_fade = SDL_min(1.0f, g_bg_fade + dt * 3.0f);
	for(i = 0; i < 2; i++) {
		const char *path = i == 0 ? g_bg_prev : g_bg_cur;
		Pic	*bg = pic_get(path);
		SDL_FRect all = { 0, 0, (float)W, (float)H };
		float	a = i == 0 ? 1.0f : g_bg_fade;

		if(bg && bg->blur) {
			SDL_SetTextureAlphaMod(bg->blur, (Uint8)(255 * a));
			SDL_RenderTexture(g_r, bg->blur, NULL, &all);
			SDL_SetTextureAlphaMod(bg->blur, 255);
		}
	}
	fill(0, 0, (float)W, (float)H, 0x000000, 105);			/* darken */
	vgrad(0, (float)H * 0.55f, (float)W, (float)H * 0.45f, 0x000000, 0, 235);	/* fade into the bottom */
	vgrad(0, 0, (float)W, (float)H * 0.25f, 0x000000, 170, 0);

	/* the logo of the selected game, else its name */
	{
		Pic	*logo = NULL;
		char	lp[1300];
		const char *nm = profile_name(&g_d.list[sel]);

		if(fe_find_art(&g_d.list[sel], g_d.artwork_root, 1, lp, sizeof(lp))) {
			logo = pic_get(lp);
		}
		if(logo) {
			draw_pic_fit(logo, W / 2 - 200 * u, 40 * u, 400 * u, 130 * u, 255);
		} else {
			text(g_big, W / 2 - text_w(g_big, nm) / 2, (int)(70 * u), nm, 0xFFFFFF, 255);
		}
	}

	/* the row of covers */
	g_ntiles = 0;
	{
		float	cy = H * 0.50f;

		for(int pass = 0; pass < 2; pass++) {
			for(i = 0; i < n; i++) {
				float	d = (float)i - g_pos, ad = fabsf(d), s, x, y, w, h;
				int	alpha;

				if(ad > 4.5f || (pass == 0) == (ad < 0.5f)) {
					continue;		/* far ones first, the selected last */
				}
				s = 1.0f + 0.32f * SDL_max(0.0f, 1.0f - ad);		/* the one in the middle grows */
				w = tile_w * s;
				h = tile_h * s;
				x = W / 2.0f + d * (tile_w + gap) - w / 2 + (d > 0 ? 1.0f : -1.0f) * SDL_min(1.0f, ad) * 0.16f * tile_w;
				y = cy - h / 2;
				alpha = (int)(255 * (1.0f - SDL_min(1.0f, ad / 5.0f) * 0.7f));
				if(!search_match(profile_name(&g_d.list[i]))) {
					alpha = alpha / 4;
				}
				draw_tile(&g_d.list[i], x, y, w, h, SDL_max(0.0f, 1.0f - ad), alpha);
				if(g_ntiles < 64) {
					g_tx[g_ntiles] = x; g_ty[g_ntiles] = y; g_tw[g_ntiles] = w; g_th[g_ntiles] = h;
					g_ti[g_ntiles] = i;
					g_ntiles++;
				}
				if(ad < 0.6f) {
					const char *nm = profile_name(&g_d.list[i]);

					text(g_mid, (int)(x + w / 2 - text_w(g_mid, nm) / 2), (int)(y + h + 22 * u), nm, 0xFFFFFF, 255);
				}
			}
		}
	}
	/* a short line under the title, the hints and the counter */
	{
		const char *desc = profile_get(&g_d.list[sel], "description");
		char	f[300], cnt[40];

		facts(&g_d.list[sel], f, sizeof(f));
		if(desc[0]) {
			text(g_small, W / 2 - text_w(g_small, desc) / 2, (int)(H * 0.80f), desc, 0xFFFFFF, 220);
		}
		if(f[0]) {
			text(g_small, W / 2 - text_w(g_small, f) / 2, (int)(H * 0.80f + 28 * u), f, 0xB8BCC8, 220);
		}
		snprintf(cnt, sizeof(cnt), "%d / %d", sel + 1, n);
		text(g_small, W - 30 - text_w(g_small, cnt), (int)(24 * u), cnt, 0xB8BCC8, 255);
		if(g_search[0]) {
			char s[100];

			snprintf(s, sizeof(s), "Search: %s", g_search);
			text(g_small, 30, (int)(24 * u), s, 0xFFD060, 255);
		}
		{
			const char *hint = g_pad_hints ? "A  play        X  details        Y  menu        B  back        L / R  jump" :
				"Return  play        Tab  details        Space  menu        type to search";

			text(g_small, W / 2 - text_w(g_small, hint) / 2, (int)(H - 44 * u), hint, 0x9AA0B0, 255);
		}
	}

	if(g_info) {				/* the details page over everything */
		const Profile *p = &g_d.list[sel];
		const char *desc = profile_get(p, "description");
		char	f[300], line[900];
		Pic	*pic = NULL;
		float	px = W * 0.08f, py = H * 0.12f, pw = W * 0.84f, ph = H * 0.74f;

		fill(0, 0, (float)W, (float)H, 0x000000, 170);
		fill(px, py, pw, ph, 0x14161D, 245);
		if(fe_find_art(p, g_d.artwork_root, 0, art, sizeof(art))) {
			pic = pic_get(art);
		}
		if(pic) {
			draw_pic_cover(pic, px + 24 * u, py + 24 * u, pw * 0.5f, pw * 0.5f * 0.66f, 255);
		}
		{
			float tx = px + pw * 0.5f + 56 * u, ty = py + 30 * u;

			text(g_big, (int)tx, (int)ty, profile_name(p), 0xFFFFFF, 255);
			ty += 80 * u;
			if(desc[0]) {
				text(g_small, (int)tx, (int)ty, desc, 0xD0D4E0, 255);
				ty += 40 * u;
			}
			facts(p, f, sizeof(f));
			text(g_small, (int)tx, (int)ty, f, 0xFFD060, 255);
			ty += 50 * u;
			if(profile_get(p, "scale")[0]) {
				snprintf(line, sizeof(line), "Window %sx", profile_get(p, "scale"));
				text(g_small, (int)tx, (int)ty, line, 0x9AA0B0, 255);
			}
		}
		text(g_small, (int)(px + 24 * u), (int)(py + ph - 44 * u),
			g_pad_hints ? "A  play        B  back" : "Return  play        E  edit        Esc  back", 0x9AA0B0, 255);
	}
}

/* ---- input ---- */

static void
move(int step)
{
	int	n = g_d.count ? *g_d.count : 0;

	if(g_d.sel && n) {
		int s = *g_d.sel + step;

		*g_d.sel = s < 0 ? 0 : (s >= n ? n - 1 : s);
	}
}

static void
search_apply(void)
{
	int	n = g_d.count ? *g_d.count : 0;

	if(!g_search[0] || !g_d.sel) {
		return;
	}
	for(int i = 0; i < n; i++) {
		if(search_match(profile_name(&g_d.list[i]))) {
			*g_d.sel = i;
			return;
		}
	}
}

int
fe_key(SDL_Keycode k, SDL_Keymod mod)
{
	int	n = g_d.count ? *g_d.count : 0;

	(void)mod;
	switch(k) {
	case SDLK_LEFT: move(-1); return 1;
	case SDLK_RIGHT: move(1); return 1;
	case SDLK_UP: move(-5); return 1;
	case SDLK_DOWN: move(5); return 1;
	case SDLK_HOME: move(-100000); return 1;
	case SDLK_END: move(100000); return 1;
	case SDLK_RETURN:
		if(n && g_d.play) {
			g_d.play();
		}
		return 1;
	case SDLK_TAB:
		g_info = !g_info;
		return 1;
	case SDLK_SPACE:
		if(g_d.overlay) {
			g_d.overlay();
		}
		return 1;
	case SDLK_E:
		if(g_info && g_d.edit) {
			g_d.edit();
			return 1;
		}
		return 0;
	case SDLK_ESCAPE:
		if(g_info) {
			g_info = 0;
		} else {
			g_search[0] = 0;
		}
		return 1;
	case SDLK_BACKSPACE:
		if(g_search[0]) {
			g_search[strlen(g_search) - 1] = 0;
			search_apply();
			return 1;
		}
		return 0;
	}
	return 0;
}

int
fe_text(const char *t)
{
	size_t	n = strlen(g_search);

	if(g_info || !t[0] || (unsigned char)t[0] < 32 || (t[0] == ' ' && !g_search[0])) {
		return 0;
	}
	if(n + strlen(t) < sizeof(g_search)) {
		memcpy(g_search + n, t, strlen(t) + 1);
		search_apply();
		return 1;
	}
	return 0;
}

int
fe_click(float x, float y, int clicks)
{
	if(g_info) {
		g_info = 0;
		return 1;
	}
	for(int i = g_ntiles - 1; i >= 0; i--) {	/* the later ones are drawn in front */
		if(x >= g_tx[i] && x < g_tx[i] + g_tw[i] && y >= g_ty[i] && y < g_ty[i] + g_th[i]) {
			if(g_d.sel) {
				int was = *g_d.sel;

				*g_d.sel = g_ti[i];
				if((clicks >= 2 || was == g_ti[i]) && g_d.play && clicks >= 2) {
					g_d.play();
				}
			}
			return 1;
		}
	}
	return 0;
}

void
fe_wheel(float dy)
{
	if(dy > 0.3f) {
		move(-1);
	} else if(dy < -0.3f) {
		move(1);
	}
}
