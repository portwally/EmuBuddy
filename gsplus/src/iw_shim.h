/* EmuBuddy: small stand-ins for the parts of SDL 1.2 and FreeType that the
 * legacy ImageWriter emulation (imagewriter.cpp, from GSport/GSplus, GPL v2+)
 * uses. The page is an 8-bit indexed buffer with a 256-colour palette; text
 * is drawn from the Apple IIgs character set (kegsfont.h) as printer dots,
 * so no font files or font library are needed.
 *
 * C++ only: included from imagewriter.h. */
#ifndef EMUBUDDY_IW_SHIM_H
#define EMUBUDDY_IW_SHIM_H

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define HAVE_SDL 1

/* ---------------- SDL 1.2 stand-ins ---------------- */
typedef struct { unsigned char r, g, b, unused; } SDL_Color;
typedef struct { int ncolors; SDL_Color colors[256]; } SDL_Palette;
typedef struct { SDL_Palette *palette; } SDL_PixelFormat;
typedef struct {
	int		w, h, pitch;
	void		*pixels;
	SDL_PixelFormat	*format;
} SDL_Surface;
typedef struct { short x, y; unsigned short w, h; } SDL_Rect;

#define SDL_SWSURFACE		0
#define SDL_INIT_EVERYTHING	0

static inline int SDL_Init(int f) { (void)f; return 0; }
static inline SDL_Surface *
SDL_CreateRGBSurface(int flags, int w, int h, int depth, int a, int b, int c,
								int d)
{
	SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
	(void)flags; (void)depth; (void)a; (void)b; (void)c; (void)d;
	s->w = w;
	s->h = h;
	s->pitch = w;
	s->pixels = calloc((size_t)w * (size_t)h, 1);
	s->format = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
	s->format->palette = (SDL_Palette *)calloc(1, sizeof(SDL_Palette));
	s->format->palette->ncolors = 256;
	return s;
}
static inline void
SDL_FreeSurface(SDL_Surface *s)
{
	if(s) {
		free(s->pixels);
		free(s->format->palette);
		free(s->format);
		free(s);
	}
}
static inline int SDL_LockSurface(SDL_Surface *s) { (void)s; return 0; }
static inline void SDL_UnlockSurface(SDL_Surface *s) { (void)s; }
/* Index 0 is paper white in the ImageWriter palette. */
static inline unsigned SDL_MapRGB(SDL_PixelFormat *f, int r, int g, int b)
{
	(void)f; (void)r; (void)g; (void)b;
	return 0;
}
static inline int
SDL_FillRect(SDL_Surface *s, SDL_Rect *r, unsigned c)
{
	int y;

	for(y = r->y; y < r->y + r->h && y < s->h; y++) {
		memset((unsigned char *)s->pixels + (size_t)y * s->pitch + r->x,
						(int)c, (size_t)r->w);
	}
	return 0;
}

/* ---------------- FreeType stand-ins ---------------- */
typedef long FT_Fixed;
typedef unsigned FT_UInt;
typedef struct { FT_Fixed xx, xy, yx, yy; } FT_Matrix;
typedef struct {
	int		rows, width, pitch;
	unsigned char	*buffer;
} FT_Bitmap;
typedef struct { long x, y; } FT_Vector;
typedef struct {
	FT_Bitmap	bitmap;
	int		bitmap_left, bitmap_top;
	FT_Vector	advance;
} FT_GlyphSlotRec;
typedef struct { long ascender, height; } FT_Size_Metrics;
typedef struct { FT_Size_Metrics metrics; } FT_SizeRec;
typedef struct {
	FT_GlyphSlotRec	*glyph;
	FT_SizeRec	*size;
	double		dot_w, dot_h;	/* printer dot pitch in pixels */
	double		shear;		/* italic slant (x per y) */
	FT_GlyphSlotRec	slot;
	FT_SizeRec	sizerec;
} FT_FaceRec;
typedef FT_FaceRec *FT_Face;
typedef int FT_Library;

#define FT_LOAD_DEFAULT		0
#define FT_RENDER_MODE_NORMAL	0

/* The IIgs text font: 8 rows of 7 dots, bit 6 is the leftmost dot; normal
 * characters are at 0x80 + ASCII. Defined in iw_font.c. */
extern "C" const unsigned char g_iw_font[256][8];

static inline int FT_Init_FreeType(FT_Library *l) { *l = 1; return 0; }
static inline int FT_Done_FreeType(FT_Library l) { (void)l; return 0; }
static inline int
FT_New_Face(FT_Library l, const char *name, long idx, FT_Face *f)
{
	(void)l; (void)name; (void)idx;
	*f = (FT_Face)calloc(1, sizeof(FT_FaceRec));
	(*f)->glyph = &(*f)->slot;
	(*f)->size = &(*f)->sizerec;
	return 0;
}
static inline int
FT_Done_Face(FT_Face f)
{
	if(f) {
		free(f->slot.bitmap.buffer);
		free(f);
	}
	return 0;
}
/* Size in 1/64 points at dpi. A 10 point character cell is 1/10 inch wide
 * (10 cpi), 8 dots across including the gap; 9 dot rows per em. */
static inline int
FT_Set_Char_Size(FT_Face f, long w64, long h64, unsigned hdpi, unsigned vdpi)
{
	double em_w = (double)w64 / 64.0 * hdpi / 72.0;
	double em_h = (double)h64 / 64.0 * vdpi / 72.0;

	f->dot_w = em_w * 0.72 / 8.0;
	f->dot_h = em_h / 9.0;
	f->shear = 0;
	f->sizerec.metrics.ascender = (long)(em_h * 7.0 / 9.0 * 64.0);
	f->sizerec.metrics.height = (long)(em_h * 64.0);
	return 0;
}
static inline void
FT_Set_Transform(FT_Face f, FT_Matrix *m, FT_Vector *d)
{
	(void)d;
	f->shear = m ? (double)m->xy / 65536.0 : 0.0;
}
/* Characters arrive as Unicode from the charmap tables; the IIgs font only
 * has ASCII, so others become their closest ASCII or '?'. */
static inline FT_UInt
FT_Get_Char_Index(FT_Face f, unsigned long cp)
{
	(void)f;
	if(cp >= 0x20 && cp < 0x7f) {
		return (FT_UInt)cp;
	}
	switch(cp) {
	case 0xA3: return '#';			/* pound */
	case 0xA7: return 'S';
	case 0xB0: return 'o';
	case 0xE0: case 0xE1: case 0xE2: case 0xE4: return 'a';
	case 0xE7: return 'c';
	case 0xE8: case 0xE9: case 0xEA: return 'e';
	case 0xF1: return 'n';
	case 0xF2: case 0xF3: case 0xF4: case 0xF6: return 'o';
	case 0xF9: case 0xFA: case 0xFC: return 'u';
	case 0xC4: return 'A';
	case 0xD6: return 'O';
	case 0xDC: return 'U';
	case 0xDF: return 's';
	}
	return '?';
}
/* Render one character as round-ish printer dots into glyph->bitmap. */
static inline int
FT_Load_Glyph(FT_Face f, FT_UInt ch, int flags)
{
	const unsigned char *rows = g_iw_font[0x80 + (ch & 0x7f)];
	double	dw = f->dot_w, dh = f->dot_h;
	int	r, c, used = 0, w, h, x, y, x0, y0;
	double	dotw = dw * 1.25 + 0.5, doth = dh * 1.25 + 0.5;
	FT_GlyphSlotRec *g = &f->slot;
	(void)flags;

	for(r = 0; r < 8; r++) {
		for(c = 0; c < 7; c++) {
			if(rows[r] & (0x40 >> c)) {
				if(c + 1 > used) used = c + 1;
			}
		}
	}
	w = (int)ceil(8 * dw + fabs(f->shear) * 9 * dh) + 2;
	h = (int)ceil(9 * dh) + 2;
	free(g->bitmap.buffer);
	g->bitmap.buffer = (unsigned char *)calloc((size_t)w * h, 1);
	g->bitmap.width = w;
	g->bitmap.rows = h;
	g->bitmap.pitch = w;
	for(r = 0; r < 8; r++) {
		for(c = 0; c < 7; c++) {
			if(!(rows[r] & (0x40 >> c))) {
				continue;
			}
			/* rows 0-6 sit above the baseline, row 7 is the descender */
			x0 = (int)(c * dw + (7 - r) * dh * f->shear);
			y0 = (int)(r * dh);
			for(y = 0; y < (int)doth; y++) {
				for(x = 0; x < (int)dotw; x++) {
					if(x0 + x < w && y0 + y < h) {
						g->bitmap.buffer[(y0 + y) * w +
							x0 + x] = 255;
					}
				}
			}
		}
	}
	g->bitmap_left = 0;
	g->bitmap_top = (int)(7 * dh);
	/* proportional advance: used columns plus one dot of gap */
	g->advance.x = (long)(((used ? used : 4) + 1) * dw * 64.0);
	return 0;
}
static inline int FT_Render_Glyph(FT_GlyphSlotRec *g, int mode)
{
	(void)g; (void)mode;
	return 0;
}

#endif
