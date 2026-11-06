/* EmuBuddy UI font: a small TrueType ('glyf' outline) reader and scanline
 * rasterizer for the embedded Shaston 640 font, so the UI needs no font
 * library. Glyphs are rasterized once at 16 px into an atlas texture.
 *
 * Supports: cmap format 4, simple and compound glyphs (offsets and simple
 * scales), quadratic curves. No hinting, no kerning. */
#include "ui.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern const unsigned char g_ui_font_ttf[];
extern const unsigned int g_ui_font_ttf_len;

#define PPEM		16
#define SS		4			/* vertical/horizontal subsamples */
#define ATLAS_W		512
#define ATLAS_H		256

static const unsigned char	*g_ttf = g_ui_font_ttf;
static size_t			g_len;
static unsigned	g_glyf, g_loca, g_hmtx, g_cmap4;
static int	g_locfmt, g_nglyphs, g_nhm, g_upem, g_asc_px;

typedef struct { float x0, y0, x1, y1; } Edge;
static Edge	*g_edges;
static int	g_nedges, g_capedges;

typedef struct {
	int	valid, has_bits;
	int	ax, ay, w, h;		/* atlas rect */
	int	ox, oy;			/* bitmap offset from pen / cell top */
	int	adv;
} Glyph;

static Glyph		*g_cache;
static SDL_Texture	*g_atlas;
static SDL_Renderer	*g_ren;
static int		g_pack_x, g_pack_y, g_pack_rowh;

static unsigned
u16(unsigned o)
{
	return (o + 2 <= g_len) ? (unsigned)((g_ttf[o] << 8) | g_ttf[o + 1]) : 0;
}

static int
s16(unsigned o)
{
	return (short)u16(o);
}

static unsigned
u32(unsigned o)
{
	return (u16(o) << 16) | u16(o + 2);
}

static unsigned
find_table(const char *tag)
{
	unsigned	n, i, o;

	n = u16(4);
	for(i = 0; i < n; i++) {
		o = 12 + i * 16;
		if(o + 16 <= g_len && memcmp(g_ttf + o, tag, 4) == 0) {
			return u32(o + 8);
		}
	}
	return 0;
}

static int
glyph_for_cp(unsigned cp)
{
	unsigned	segx2, i, end, start, delta, range, o;

	if(!g_cmap4 || cp > 0xffff) {
		return 0;
	}
	segx2 = u16(g_cmap4 + 6);
	for(i = 0; i < segx2; i += 2) {
		end = u16(g_cmap4 + 14 + i);
		if(cp <= end) {
			start = u16(g_cmap4 + 16 + segx2 + i);
			if(cp < start) {
				return 0;
			}
			delta = u16(g_cmap4 + 16 + 2 * segx2 + i);
			range = u16(g_cmap4 + 16 + 3 * segx2 + i);
			if(range == 0) {
				return (int)((cp + delta) & 0xffff);
			}
			o = g_cmap4 + 16 + 3 * segx2 + i + range +
							(cp - start) * 2;
			i = u16(o);
			return i ? (int)((i + delta) & 0xffff) : 0;
		}
	}
	return 0;
}

static void
add_edge(float x0, float y0, float x1, float y1)
{
	if(y0 == y1) {
		return;
	}
	if(g_nedges >= g_capedges) {
		g_capedges = g_capedges ? g_capedges * 2 : 512;
		g_edges = realloc(g_edges, g_capedges * sizeof(Edge));
	}
	g_edges[g_nedges].x0 = x0;
	g_edges[g_nedges].y0 = y0;
	g_edges[g_nedges].x1 = x1;
	g_edges[g_nedges].y1 = y1;
	g_nedges++;
}

typedef struct { float x, y; int on; } Pt;

/* Pixel space: y down, baseline at y = g_asc_px. */
static void
tr(float *px, float *py, float x, float y, const float *m)
{
	float	k = (float)PPEM / (float)g_upem;
	float	fx = m[0] * x + m[2] * y + m[4];
	float	fy = m[1] * x + m[3] * y + m[5];

	*px = fx * k;
	*py = (float)g_asc_px - fy * k;
}

static void
flatten_quad(float x0, float y0, float cx, float cy, float x1, float y1)
{
	int	i, n = 8;
	float	px = x0, py = y0, t, a, b, c, nx, ny;

	for(i = 1; i <= n; i++) {
		t = (float)i / n;
		a = (1 - t) * (1 - t);
		b = 2 * (1 - t) * t;
		c = t * t;
		nx = a * x0 + b * cx + c * x1;
		ny = a * y0 + b * cy + c * y1;
		add_edge(px, py, nx, ny);
		px = nx;
		py = ny;
	}
}

static void
emit_contour(Pt *p, int n, const float *m)
{
	int	i, start, ns;
	float	sx, sy, cx, cy, ex, ey, ax, ay, bx, by;
	Pt	*q;

	if(n < 2) {
		return;
	}
	/* Transform to pixel space first. */
	q = p;
	for(i = 0; i < n; i++) {
		tr(&q[i].x, &q[i].y, p[i].x, p[i].y, m);
	}
	if(q[0].on) {
		start = 0;
		sx = q[0].x; sy = q[0].y;
	} else if(q[n - 1].on) {
		start = n - 1;
		sx = q[n - 1].x; sy = q[n - 1].y;
	} else {
		start = -1;
		sx = (q[0].x + q[n - 1].x) / 2;
		sy = (q[0].y + q[n - 1].y) / 2;
	}
	/* Walk the points after 'start' (or all of them) and close. */
	cx = sx; cy = sy;
	ns = (start >= 0) ? n - 1 : n;
	for(i = 0; i < ns; i++) {
		int idx = (start >= 0) ? (start + 1 + i) % n : i;

		if(q[idx].on) {
			add_edge(cx, cy, q[idx].x, q[idx].y);
			cx = q[idx].x; cy = q[idx].y;
		} else {
			int nidx = (idx + 1) % n;
			bx = q[idx].x; by = q[idx].y;
			if(i == ns - 1) {
				ex = sx; ey = sy;
			} else if(q[nidx].on) {
				ex = q[nidx].x; ey = q[nidx].y;
				i++;
			} else {
				ex = (bx + q[nidx].x) / 2;
				ey = (by + q[nidx].y) / 2;
			}
			flatten_quad(cx, cy, bx, by, ex, ey);
			cx = ex; cy = ey;
		}
	}
	(void)ax; (void)ay;
	add_edge(cx, cy, sx, sy);
}

static unsigned
glyph_offset(int g, unsigned *len)
{
	unsigned	a, b;

	if(g < 0 || g >= g_nglyphs) {
		*len = 0;
		return 0;
	}
	if(g_locfmt) {
		a = u32(g_loca + g * 4);
		b = u32(g_loca + g * 4 + 4);
	} else {
		a = u16(g_loca + g * 2) * 2;
		b = u16(g_loca + g * 2 + 2) * 2;
	}
	*len = b - a;
	return g_glyf + a;
}

static void
load_glyph(int g, const float *m, int depth)
{
	unsigned	len, o, i, flags, nc, npts, p, ilen, c;
	int		n;
	unsigned char	*fl;
	Pt		*pts;
	unsigned	*ends;
	int		x, y, dx, comp;

	o = glyph_offset(g, &len);
	if(len < 10 || depth > 4) {
		return;
	}
	n = s16(o);
	if(n >= 0) {
		nc = (unsigned)n;
		ends = malloc((nc + 1) * sizeof(unsigned));
		for(i = 0; i < nc; i++) {
			ends[i] = u16(o + 10 + i * 2);
		}
		npts = nc ? ends[nc - 1] + 1 : 0;
		if(npts == 0 || npts > 4096) {
			free(ends);
			return;
		}
		ilen = u16(o + 10 + nc * 2);
		p = o + 12 + nc * 2 + ilen;
		fl = malloc(npts);
		pts = malloc(npts * sizeof(Pt));
		for(i = 0; i < npts; ) {
			flags = g_ttf[p++];
			fl[i++] = (unsigned char)flags;
			if(flags & 8) {
				unsigned rep = g_ttf[p++];
				while(rep-- && i < npts) {
					fl[i++] = (unsigned char)flags;
				}
			}
		}
		x = 0;
		for(i = 0; i < npts; i++) {
			if(fl[i] & 2) {
				dx = g_ttf[p++];
				x += (fl[i] & 16) ? dx : -dx;
			} else if(!(fl[i] & 16)) {
				x += s16(p);
				p += 2;
			}
			pts[i].x = (float)x;
			pts[i].on = fl[i] & 1;
		}
		y = 0;
		for(i = 0; i < npts; i++) {
			if(fl[i] & 4) {
				dx = g_ttf[p++];
				y += (fl[i] & 32) ? dx : -dx;
			} else if(!(fl[i] & 32)) {
				y += s16(p);
				p += 2;
			}
			pts[i].y = (float)y;
		}
		c = 0;
		for(i = 0; i < nc; i++) {
			emit_contour(pts + c, (int)(ends[i] + 1 - c), m);
			c = ends[i] + 1;
		}
		free(pts);
		free(fl);
		free(ends);
		return;
	}
	/* Compound glyph. */
	p = o + 10;
	do {
		float	cm[6], t[6];

		flags = u16(p);
		comp = (int)u16(p + 2);
		p += 4;
		float a1, a2;
		if(flags & 1) {
			a1 = (float)s16(p); a2 = (float)s16(p + 2); p += 4;
		} else {
			a1 = (float)(signed char)g_ttf[p];
			a2 = (float)(signed char)g_ttf[p + 1];
			p += 2;
		}
		if(!(flags & 2)) {
			a1 = a2 = 0;		/* point matching unsupported */
		}
		t[0] = 1; t[1] = 0; t[2] = 0; t[3] = 1; t[4] = a1; t[5] = a2;
		if(flags & 8) {
			t[0] = t[3] = s16(p) / 16384.0f; p += 2;
		} else if(flags & 0x40) {
			t[0] = s16(p) / 16384.0f; t[3] = s16(p + 2) / 16384.0f;
			p += 4;
		} else if(flags & 0x80) {
			t[0] = s16(p) / 16384.0f; t[1] = s16(p + 2) / 16384.0f;
			t[2] = s16(p + 4) / 16384.0f; t[3] = s16(p + 6) / 16384.0f;
			p += 8;
		}
		/* cm = t then m (apply component transform, then parent) */
		cm[0] = t[0] * m[0] + t[1] * m[2];
		cm[1] = t[0] * m[1] + t[1] * m[3];
		cm[2] = t[2] * m[0] + t[3] * m[2];
		cm[3] = t[2] * m[1] + t[3] * m[3];
		cm[4] = t[4] * m[0] + t[5] * m[2] + m[4];
		cm[5] = t[4] * m[1] + t[5] * m[3] + m[5];
		load_glyph(comp, cm, depth + 1);
	} while(flags & 0x20);
}

/* Rasterize the edges currently in g_edges into an 8-bit bitmap. */
static unsigned char *
raster(int *bx, int *by, int *bw, int *bh)
{
	float		minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
	int		i, w, h, sy, nx, j, k, wind;
	float		*cov, yc, xs[256];
	int		dirs[256];
	unsigned char	*out;

	if(g_nedges == 0) {
		return NULL;
	}
	for(i = 0; i < g_nedges; i++) {
		Edge *e = &g_edges[i];
		if(e->x0 < minx) minx = e->x0;
		if(e->x1 < minx) minx = e->x1;
		if(e->x0 > maxx) maxx = e->x0;
		if(e->x1 > maxx) maxx = e->x1;
		if(e->y0 < miny) miny = e->y0;
		if(e->y1 < miny) miny = e->y1;
		if(e->y0 > maxy) maxy = e->y0;
		if(e->y1 > maxy) maxy = e->y1;
	}
	*bx = (int)floorf(minx);
	*by = (int)floorf(miny);
	w = (int)ceilf(maxx) - *bx;
	h = (int)ceilf(maxy) - *by;
	if(w <= 0 || h <= 0 || w > 64 || h > 64) {
		return NULL;
	}
	cov = calloc((size_t)w * h, sizeof(float));
	for(sy = 0; sy < h * SS; sy++) {
		yc = *by + (sy + 0.5f) / SS;
		nx = 0;
		for(i = 0; i < g_nedges && nx < 256; i++) {
			Edge *e = &g_edges[i];
			float lo = e->y0 < e->y1 ? e->y0 : e->y1;
			float hi = e->y0 < e->y1 ? e->y1 : e->y0;

			if(yc < lo || yc >= hi) {
				continue;
			}
			xs[nx] = e->x0 + (yc - e->y0) * (e->x1 - e->x0) /
							(e->y1 - e->y0);
			dirs[nx] = (e->y1 > e->y0) ? 1 : -1;
			nx++;
		}
		/* insertion sort by x */
		for(j = 1; j < nx; j++) {
			float fx = xs[j];
			int fd = dirs[j];
			k = j - 1;
			while(k >= 0 && xs[k] > fx) {
				xs[k + 1] = xs[k];
				dirs[k + 1] = dirs[k];
				k--;
			}
			xs[k + 1] = fx;
			dirs[k + 1] = fd;
		}
		wind = 0;
		for(j = 0; j + 1 < nx; j++) {
			float xa, xb;
			int px;

			wind += dirs[j];
			if(wind == 0) {
				continue;
			}
			xa = xs[j] - *bx;
			xb = xs[j + 1] - *bx;
			if(xb <= xa) {
				continue;
			}
			for(px = (int)floorf(xa); px < (int)ceilf(xb); px++) {
				float l = xa > px ? xa : (float)px;
				float r = xb < px + 1 ? xb : (float)(px + 1);

				if(px >= 0 && px < w && r > l) {
					cov[(size_t)(sy / SS) * w + px] +=
							(r - l) / SS;
				}
			}
		}
	}
	out = malloc((size_t)w * h);
	for(i = 0; i < w * h; i++) {
		float v = cov[i];
		out[i] = (unsigned char)(v >= 1.0f ? 255 : v * 255.0f + 0.5f);
	}
	free(cov);
	*bw = w;
	*bh = h;
	return out;
}

static void
atlas_reset(void)
{
	int	i;

	g_pack_x = g_pack_y = g_pack_rowh = 0;
	for(i = 0; i < g_nglyphs; i++) {
		g_cache[i].valid = 0;
	}
}

static Glyph *
get_glyph(int gid)
{
	static const float	ident[6] = { 1, 0, 0, 1, 0, 0 };
	Glyph		*g;
	unsigned char	*bm;
	Uint32		*px;
	int		bx, by, bw, bh, i, hm;
	unsigned	adv;
	SDL_Rect	r;

	if(gid < 0 || gid >= g_nglyphs) {
		gid = 0;
	}
	g = &g_cache[gid];
	if(g->valid) {
		return g;
	}
	hm = gid < g_nhm ? gid : g_nhm - 1;
	adv = u16(g_hmtx + hm * 4);
	g->adv = (int)(adv * PPEM / (float)g_upem + 0.5f);
	g->has_bits = 0;
	g->valid = 1;
	g_nedges = 0;
	load_glyph(gid, ident, 0);
	bm = raster(&bx, &by, &bw, &bh);
	if(!bm) {
		return g;
	}
	if(g_pack_x + bw > ATLAS_W) {
		g_pack_x = 0;
		g_pack_y += g_pack_rowh;
		g_pack_rowh = 0;
	}
	if(g_pack_y + bh > ATLAS_H) {
		/* Atlas full: start over (cache refills on demand). */
		atlas_reset();
		g->valid = 1;
		g->adv = (int)(adv * PPEM / (float)g_upem + 0.5f);
	}
	px = malloc((size_t)bw * bh * sizeof(Uint32));
	for(i = 0; i < bw * bh; i++) {
		px[i] = ((Uint32)bm[i] << 24) | 0x00FFFFFF;
	}
	r.x = g_pack_x; r.y = g_pack_y; r.w = bw; r.h = bh;
	SDL_UpdateTexture(g_atlas, &r, px, bw * 4);
	free(px);
	free(bm);
	g->ax = g_pack_x; g->ay = g_pack_y; g->w = bw; g->h = bh;
	g->ox = bx; g->oy = by;
	g->has_bits = 1;
	g_pack_x += bw + 1;
	if(bh + 1 > g_pack_rowh) {
		g_pack_rowh = bh + 1;
	}
	return g;
}

int
ui_font_init(SDL_Renderer *r)
{
	unsigned	head, hhea, maxp, cmap, n, i, o, sub;

	g_ren = r;
	g_len = g_ui_font_ttf_len;
	head = find_table("head");
	hhea = find_table("hhea");
	maxp = find_table("maxp");
	cmap = find_table("cmap");
	g_glyf = find_table("glyf");
	g_loca = find_table("loca");
	g_hmtx = find_table("hmtx");
	if(!head || !hhea || !maxp || !cmap || !g_glyf || !g_loca || !g_hmtx) {
		return -1;
	}
	g_upem = (int)u16(head + 18);
	g_locfmt = s16(head + 50);
	g_nglyphs = (int)u16(maxp + 4);
	g_nhm = (int)u16(hhea + 34);
	g_asc_px = (int)((s16(hhea + 4) * PPEM) / (float)g_upem + 0.5f);
	g_cmap4 = 0;
	n = u16(cmap + 2);
	for(i = 0; i < n; i++) {
		o = cmap + 4 + i * 8;
		sub = cmap + u32(o + 4);
		if(u16(sub) == 4 && (u16(o) == 3 || u16(o) == 0)) {
			g_cmap4 = sub;
			if(u16(o) == 3 && u16(o + 2) == 1) {
				break;
			}
		}
	}
	if(!g_upem || !g_nglyphs || !g_nhm || !g_cmap4) {
		return -1;
	}
	g_cache = calloc((size_t)g_nglyphs, sizeof(Glyph));
	g_atlas = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STATIC, ATLAS_W, ATLAS_H);
	if(!g_atlas) {
		return -1;
	}
	{
		Uint32 *clear = calloc((size_t)ATLAS_W * ATLAS_H, 4);
		SDL_UpdateTexture(g_atlas, NULL, clear, ATLAS_W * 4);
		free(clear);
	}
	SDL_SetTextureBlendMode(g_atlas, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(g_atlas, SDL_SCALEMODE_NEAREST);
	return 0;
}

void
ui_font_shutdown(void)
{
	if(g_atlas) {
		SDL_DestroyTexture(g_atlas);
		g_atlas = NULL;
	}
	free(g_cache);
	g_cache = NULL;
	free(g_edges);
	g_edges = NULL;
	g_nedges = g_capedges = 0;
}

static unsigned
next_cp(const char **ps)
{
	const unsigned char	*s = (const unsigned char *)*ps;
	unsigned		c = *s;

	if(c < 0x80) {
		*ps += 1;
		return c;
	}
	if((c & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) {
		*ps += 2;
		return ((c & 0x1f) << 6) | (s[1] & 0x3f);
	}
	if((c & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 &&
						(s[2] & 0xc0) == 0x80) {
		*ps += 3;
		return ((c & 0x0f) << 12) | ((s[1] & 0x3f) << 6) |
							(s[2] & 0x3f);
	}
	*ps += 1;
	return '?';
}

int
ui_text_width(const char *s)
{
	int	w = 0;

	if(!g_atlas) {
		return 0;
	}
	while(*s) {
		w += get_glyph(glyph_for_cp(next_cp(&s)))->adv;
	}
	return w;
}

void
ui_draw_text(int x, int y, const char *s, Uint32 rgb)
{
	Glyph		*g;
	SDL_FRect	src, dst;
	int		pen = x;

	if(!g_atlas) {
		return;
	}
	SDL_SetTextureColorMod(g_atlas, (rgb >> 16) & 255, (rgb >> 8) & 255,
								rgb & 255);
	while(*s) {
		g = get_glyph(glyph_for_cp(next_cp(&s)));
		if(g->has_bits) {
			src.x = (float)g->ax; src.y = (float)g->ay;
			src.w = (float)g->w; src.h = (float)g->h;
			dst.x = (float)(pen + g->ox);
			dst.y = (float)(y + g->oy);
			dst.w = src.w; dst.h = src.h;
			SDL_RenderTexture(g_ren, g_atlas, &src, &dst);
		}
		pen += g->adv;
	}
}
