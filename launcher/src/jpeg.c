/* A small JPEG reader: baseline and progressive, 8 bit, Huffman coded, grey or
 * three components, any sampling. Enough for cover pictures; see jpeg.h. */
#include "jpeg.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	unsigned char	vals[256];
	int		mincode[17], maxcode[17], valptr[17];
	unsigned short	fast[512];	/* (length << 8) | value for codes up to 9 bits; 0 = a longer code */
	int		ok;
} Huff;

typedef struct {
	int		id, h, v, tq, td, ta;
	int		bw, bh;		/* blocks across and down, whole MCUs */
	int		sw, sh;		/* blocks when this component has a scan of its own */
	short		*coef;		/* bw * bh blocks of 64, natural order */
	int		pred;
	unsigned char	*pix;		/* bw*8 x bh*8 */
} Comp;

typedef struct {
	const unsigned char *d;
	size_t		n, pos;
	unsigned	bits;
	int		nbits, marker;	/* marker: the compressed data of this scan has ended */
	int		w, h, ncomp, progressive, hmax, vmax, mcux, mcuy, have_frame;
	unsigned short	qt[4][64];	/* natural order */
	Huff		dc[4], ac[4];
	Comp		c[3];
	int		restart, eobrun, bad;
	int		adobe;		/* -1 = no Adobe marker, else its colour transform byte */
} J;

static const unsigned char zz[64] = {
	0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
	12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

/* ---- bits ---- */

static void
fill(J *j)
{
	while(j->nbits <= 24) {
		unsigned b = 0;

		if(!j->marker && j->pos < j->n) {
			b = j->d[j->pos];
			if(b == 0xFF) {
				unsigned b2 = j->pos + 1 < j->n ? j->d[j->pos + 1] : 0xD9;

				if(b2 == 0) {
					j->pos += 2;		/* FF 00 is a plain FF */
				} else {
					j->marker = 1;		/* a marker: zeros from here on */
					b = 0;
				}
			} else {
				j->pos++;
			}
		}
		j->bits |= b << (24 - j->nbits);
		j->nbits += 8;
	}
}

static int
getbits(J *j, int n)
{
	unsigned v;

	if(n <= 0) {
		return 0;
	}
	if(j->nbits < n) {
		fill(j);
	}
	v = j->bits >> (32 - n);
	j->bits <<= n;
	j->nbits -= n;
	return (int)v;
}

static int
extend(int v, int n)
{
	return v < (1 << (n - 1)) ? v - (1 << n) + 1 : v;
}

/* ---- Huffman tables ---- */

static int
huff_build(Huff *h, const unsigned char *counts, const unsigned char *vals, int nvals)
{
	int	code = 0, k = 0, l, i, m;

	memset(h, 0, sizeof(*h));
	memcpy(h->vals, vals, (size_t)nvals);
	for(l = 1; l <= 16; l++) {
		h->valptr[l] = k;
		h->mincode[l] = code;
		for(i = 0; i < counts[l - 1]; i++, k++, code++) {
			if(code >= (1 << l)) {
				return -1;		/* more codes than fit */
			}
			if(l <= 9) {
				int first = code << (9 - l), cnt = 1 << (9 - l);

				for(m = 0; m < cnt; m++) {
					h->fast[first + m] = (unsigned short)((l << 8) | vals[k]);
				}
			}
		}
		h->maxcode[l] = counts[l - 1] ? code - 1 : -1;
		code <<= 1;
	}
	h->ok = 1;
	return 0;
}

static int
huff(J *j, const Huff *h)
{
	unsigned f;
	int	l;

	if(j->nbits < 16) {
		fill(j);
	}
	f = h->fast[j->bits >> 23];
	if(f) {
		l = (int)(f >> 8);
		j->bits <<= l;
		j->nbits -= l;
		return (int)(f & 255);
	}
	for(l = 10; l <= 16; l++) {
		int code = (int)(j->bits >> (32 - l));

		if(code <= h->maxcode[l] && code >= h->mincode[l]) {
			j->bits <<= l;
			j->nbits -= l;
			return h->vals[h->valptr[l] + code - h->mincode[l]];
		}
	}
	j->bad = 1;
	return 0;
}

/* ---- one block of coefficients ---- */

static void
block_base(J *j, Comp *c, short *b)
{
	const Huff *ha = &j->ac[c->ta];
	int	t = huff(j, &j->dc[c->td]), k;

	if(t > 15) {
		j->bad = 1;
		return;
	}
	c->pred += t ? extend(getbits(j, t), t) : 0;
	b[0] = (short)c->pred;
	for(k = 1; k < 64; k++) {
		int rs = huff(j, ha), r = rs >> 4, s = rs & 15;

		if(!s) {
			if(r != 15) {
				break;			/* end of block */
			}
			k += 15;
			continue;
		}
		k += r;
		if(k > 63) {
			j->bad = 1;
			break;
		}
		b[zz[k]] = (short)extend(getbits(j, s), s);
	}
}

static void
block_dc_first(J *j, Comp *c, short *b, int al)
{
	int	t = huff(j, &j->dc[c->td]);

	if(t > 15) {
		j->bad = 1;
		return;
	}
	c->pred += t ? extend(getbits(j, t), t) : 0;
	b[0] = (short)(c->pred * (1 << al));
}

static void
block_ac_first(J *j, Comp *c, short *b, int ss, int se, int al)
{
	const Huff *ha = &j->ac[c->ta];
	int	k;

	if(j->eobrun > 0) {
		j->eobrun--;
		return;
	}
	for(k = ss; k <= se; k++) {
		int rs = huff(j, ha), r = rs >> 4, s = rs & 15;

		if(!s) {
			if(r != 15) {			/* a run of blocks that end here */
				j->eobrun = (1 << r) - 1;
				if(r) {
					j->eobrun += getbits(j, r);
				}
				break;
			}
			k += 15;
		} else {
			k += r;
			if(k > 63) {
				j->bad = 1;
				break;
			}
			b[zz[k]] = (short)(extend(getbits(j, s), s) * (1 << al));
		}
	}
}

/* One more bit for the coefficients that are known already, and new ones of size 1. */
static void
block_ac_refine(J *j, Comp *c, short *b, int ss, int se, int al)
{
	const Huff *ha = &j->ac[c->ta];
	int	p1 = 1 << al, m1 = -p1, k = ss;

	if(j->eobrun <= 0) {
		for(; k <= se; k++) {
			int rs = huff(j, ha), r = rs >> 4, s = rs & 15;

			if(s) {
				s = getbits(j, 1) ? p1 : m1;
			} else if(r != 15) {
				j->eobrun = 1 << r;
				if(r) {
					j->eobrun += getbits(j, r);
				}
				break;
			}
			/* step over r coefficients that are still zero; the others get a bit */
			do {
				short *cf = &b[zz[k]];

				if(*cf != 0) {
					if(getbits(j, 1) && (*cf & p1) == 0) {
						*cf = (short)(*cf + (*cf >= 0 ? p1 : m1));
					}
				} else if(--r < 0) {
					break;
				}
				k++;
			} while(k <= se);
			if(s && k <= se) {
				b[zz[k]] = (short)s;
			}
		}
	}
	if(j->eobrun > 0) {
		for(; k <= se; k++) {
			short *cf = &b[zz[k]];

			if(*cf != 0 && getbits(j, 1) && (*cf & p1) == 0) {
				*cf = (short)(*cf + (*cf >= 0 ? p1 : m1));
			}
		}
		j->eobrun--;
	}
}

/* ---- a scan ---- */

/* Between restart intervals: find the RSTn marker. 0 = the scan is over. */
static int
next_restart(J *j)
{
	j->bits = 0;
	j->nbits = 0;
	for(;;) {
		unsigned m;

		if(j->pos + 1 >= j->n) {
			return 0;
		}
		if(j->d[j->pos] != 0xFF) {
			j->pos++;
			continue;
		}
		m = j->d[j->pos + 1];
		if(m == 0xFF) {
			j->pos++;
		} else if(m == 0) {
			j->pos += 2;
		} else if(m >= 0xD0 && m <= 0xD7) {
			j->pos += 2;
			j->marker = 0;
			return 1;
		} else {
			return 0;
		}
	}
}

static void
one_block(J *j, Comp *c, short *b, int ss, int se, int ah, int al)
{
	if(!j->progressive) {
		block_base(j, c, b);
	} else if(ss == 0) {
		if(ah == 0) {
			block_dc_first(j, c, b, al);
		} else if(getbits(j, 1)) {
			b[0] = (short)(b[0] | (1 << al));
		}
	} else if(ah == 0) {
		block_ac_first(j, c, b, ss, se, al);
	} else {
		block_ac_refine(j, c, b, ss, se, al);
	}
}

static int
scan(J *j, Comp **sc, int ns, int ss, int se, int ah, int al)
{
	int	nx = ns == 1 ? sc[0]->sw : j->mcux, ny = ns == 1 ? sc[0]->sh : j->mcuy;
	int	count = 0, mx, my, i, x, y;

	j->bits = 0;
	j->nbits = 0;
	j->marker = 0;
	j->eobrun = 0;
	for(i = 0; i < ns; i++) {
		sc[i]->pred = 0;
	}
	for(my = 0; my < ny; my++) {
		for(mx = 0; mx < nx; mx++) {
			if(j->restart && count == j->restart) {
				if(!next_restart(j)) {
					return 0;		/* cut short: keep what there is */
				}
				count = 0;
				j->eobrun = 0;
				for(i = 0; i < ns; i++) {
					sc[i]->pred = 0;
				}
			}
			if(ns == 1) {
				one_block(j, sc[0], sc[0]->coef + ((size_t)my * sc[0]->bw + mx) * 64, ss, se, ah, al);
			} else {
				for(i = 0; i < ns; i++) {
					Comp *c = sc[i];

					for(y = 0; y < c->v; y++) {
						for(x = 0; x < c->h; x++) {
							one_block(j, c, c->coef + ((size_t)(my * c->v + y) * c->bw +
									mx * c->h + x) * 64, ss, se, ah, al);
						}
					}
				}
			}
			count++;
			if(j->bad) {
				return -1;
			}
		}
	}
	return 0;
}

/* ---- from coefficients to pixels ---- */

static float	g_cos[8][8];		/* [x][u] = C(u)/2 * cos((2x+1) u pi / 16) */

static unsigned char
clamp(float v)
{
	int i = (int)floorf(v + 128.5f);

	return (unsigned char)(i < 0 ? 0 : (i > 255 ? 255 : i));
}

static void
idct(const short *b, const unsigned short *q, unsigned char *out, int stride)
{
	float	f[64], tmp[64];
	int	i, x, y, u, any = 0, rowany[8] = { 0 };

	for(i = 1; i < 64; i++) {
		if(b[i]) {
			any = 1;
			rowany[i >> 3] = 1;
		}
	}
	if(!any) {
		unsigned char v = clamp((float)b[0] * (float)q[0] / 8.0f);

		for(y = 0; y < 8; y++) {
			memset(out + y * stride, v, 8);
		}
		return;
	}
	rowany[0] = 1;
	for(i = 0; i < 64; i++) {
		f[i] = (float)b[i] * (float)q[i];
	}
	for(y = 0; y < 8; y++) {			/* along each row of coefficients */
		if(!rowany[y]) {
			memset(tmp + y * 8, 0, 8 * sizeof(float));
			continue;
		}
		for(x = 0; x < 8; x++) {
			float s = 0;

			for(u = 0; u < 8; u++) {
				s += g_cos[x][u] * f[y * 8 + u];
			}
			tmp[y * 8 + x] = s;
		}
	}
	for(x = 0; x < 8; x++) {			/* then down each column */
		for(y = 0; y < 8; y++) {
			float s = 0;

			for(u = 0; u < 8; u++) {
				s += g_cos[y][u] * tmp[u * 8 + x];
			}
			out[y * stride + x] = clamp(s);
		}
	}
}

static int
be16(const unsigned char *p)
{
	return (p[0] << 8) | p[1];
}

static Comp *
find_comp(J *j, int id)
{
	for(int i = 0; i < j->ncomp; i++) {
		if(j->c[i].id == id) {
			return &j->c[i];
		}
	}
	return NULL;
}

static int
frame(J *j, const unsigned char *p, int len)
{
	int	i;

	if(j->have_frame || len < 6 || p[0] != 8) {
		return -1;
	}
	j->h = be16(p + 1);
	j->w = be16(p + 3);
	j->ncomp = p[5];
	if(j->w < 1 || j->h < 1 || j->w > 10000 || j->h > 10000 || (long)j->w * j->h > 40000000L ||
			(j->ncomp != 1 && j->ncomp != 3) || len < 6 + 3 * j->ncomp) {
		return -1;
	}
	j->hmax = j->vmax = 1;
	for(i = 0; i < j->ncomp; i++) {
		Comp *c = &j->c[i];

		c->id = p[6 + 3 * i];
		c->h = p[7 + 3 * i] >> 4;
		c->v = p[7 + 3 * i] & 15;
		c->tq = p[8 + 3 * i];
		if(j->ncomp == 1) {
			c->h = c->v = 1;		/* one component: its factors mean nothing */
		}
		if(c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4 || c->tq > 3) {
			return -1;
		}
		j->hmax = c->h > j->hmax ? c->h : j->hmax;
		j->vmax = c->v > j->vmax ? c->v : j->vmax;
	}
	j->mcux = (j->w + 8 * j->hmax - 1) / (8 * j->hmax);
	j->mcuy = (j->h + 8 * j->vmax - 1) / (8 * j->vmax);
	for(i = 0; i < j->ncomp; i++) {
		Comp *c = &j->c[i];

		c->bw = j->mcux * c->h;
		c->bh = j->mcuy * c->v;
		c->sw = ((j->w * c->h + j->hmax - 1) / j->hmax + 7) / 8;
		c->sh = ((j->h * c->v + j->vmax - 1) / j->vmax + 7) / 8;
		c->coef = calloc((size_t)c->bw * c->bh * 64, sizeof(short));
		if(!c->coef) {
			return -1;
		}
	}
	j->have_frame = 1;
	return 0;
}

static int
tables(J *j, int marker, const unsigned char *p, int len)
{
	int	i;

	if(marker == 0xDB) {				/* quantization */
		while(len > 0) {
			int pq = p[0] >> 4, tq = p[0] & 15, need = 1 + 64 * (pq ? 2 : 1);

			if(tq > 3 || pq > 1 || len < need) {
				return -1;
			}
			for(i = 0; i < 64; i++) {
				j->qt[tq][zz[i]] = (unsigned short)(pq ? be16(p + 1 + 2 * i) : p[1 + i]);
			}
			p += need;
			len -= need;
		}
	} else if(marker == 0xC4) {			/* Huffman */
		while(len > 0) {
			int tc = p[0] >> 4, th = p[0] & 15, total = 0;

			if(len < 17 || tc > 1 || th > 3) {
				return -1;
			}
			for(i = 0; i < 16; i++) {
				total += p[1 + i];
			}
			if(total > 256 || len < 17 + total || huff_build(tc ? &j->ac[th] : &j->dc[th], p + 1, p + 17, total) != 0) {
				return -1;
			}
			p += 17 + total;
			len -= 17 + total;
		}
	} else if(marker == 0xDD) {			/* restart interval */
		if(len < 2) {
			return -1;
		}
		j->restart = be16(p);
	} else if(marker == 0xEE) {			/* Adobe: says if the three components are RGB */
		if(len >= 12 && !memcmp(p, "Adobe", 5)) {
			j->adobe = p[11];
		}
	}
	return 0;
}

static int
start_scan(J *j, const unsigned char *p, int len)
{
	Comp	*sc[3];
	int	ns, i, ss, se, ah, al;

	if(!j->have_frame || len < 1) {
		return -1;
	}
	ns = p[0];
	if(ns < 1 || ns > j->ncomp || len < 4 + 2 * ns) {
		return -1;
	}
	for(i = 0; i < ns; i++) {
		sc[i] = find_comp(j, p[1 + 2 * i]);
		if(!sc[i]) {
			return -1;
		}
		sc[i]->td = p[2 + 2 * i] >> 4;
		sc[i]->ta = p[2 + 2 * i] & 15;
		if(sc[i]->td > 3 || sc[i]->ta > 3) {
			return -1;
		}
	}
	ss = p[1 + 2 * ns];
	se = p[2 + 2 * ns];
	ah = p[3 + 2 * ns] >> 4;
	al = p[3 + 2 * ns] & 15;
	if(!j->progressive) {
		ss = 0;
		se = 63;
		ah = al = 0;
	}
	if(se > 63 || ss > se || al > 13 || (j->progressive && ss > 0 && ns != 1) ||
			(j->progressive && ss == 0 && se != 0)) {
		return -1;
	}
	for(i = 0; i < ns; i++) {			/* the tables this scan needs */
		int need_dc = !j->progressive || (ss == 0 && ah == 0), need_ac = !j->progressive || ss > 0;

		if((need_dc && !j->dc[sc[i]->td].ok) || (need_ac && !j->ac[sc[i]->ta].ok)) {
			return -1;
		}
	}
	return scan(j, sc, ns, ss, se, ah, al);
}

static void
release(J *j)
{
	for(int i = 0; i < 3; i++) {
		free(j->c[i].coef);
		free(j->c[i].pix);
	}
}

int
jpeg_is(const unsigned char *data, size_t len)
{
	return len > 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

unsigned char *
jpeg_decode(const unsigned char *data, size_t len, int *w, int *h)
{
	J		*j;
	unsigned char	*out = NULL;
	int		i, x, y, scans = 0, rgb;
	int		*xmap[3] = { NULL, NULL, NULL };

	if(!jpeg_is(data, len) || !(j = calloc(1, sizeof(*j)))) {
		return NULL;
	}
	j->d = data;
	j->n = len;
	j->pos = 2;
	j->adobe = -1;
	if(g_cos[0][0] == 0) {
		for(x = 0; x < 8; x++) {
			for(i = 0; i < 8; i++) {
				g_cos[x][i] = (float)((i == 0 ? 0.70710678118654752 : 1.0) / 2.0 *
						cos((2 * x + 1) * i * 3.14159265358979323846 / 16.0));
			}
		}
	}
	for(;;) {
		unsigned m;
		int	seglen;

		/* the next marker */
		while(j->pos + 1 < j->n && !(j->d[j->pos] == 0xFF && j->d[j->pos + 1] != 0 && j->d[j->pos + 1] != 0xFF)) {
			j->pos++;
		}
		if(j->pos + 1 >= j->n) {
			break;
		}
		m = j->d[j->pos + 1];
		j->pos += 2;
		if(m == 0xD9) {
			break;
		}
		if((m >= 0xD0 && m <= 0xD8) || m == 0x01) {
			continue;
		}
		if(j->pos + 2 > j->n) {
			break;
		}
		seglen = be16(j->d + j->pos);
		if(seglen < 2 || j->pos + (size_t)seglen > j->n) {
			break;
		}
		if(m == 0xC0 || m == 0xC1 || m == 0xC2) {
			j->progressive = m == 0xC2;
			if(frame(j, j->d + j->pos + 2, seglen - 2) != 0) {
				goto fail;
			}
		} else if(m == 0xC3 || (m >= 0xC5 && m <= 0xCF && m != 0xC8 && m != 0xCC)) {
			goto fail;				/* lossless, arithmetic, hierarchical */
		} else if(m == 0xDA) {
			const unsigned char *p = j->d + j->pos + 2;

			j->pos += (size_t)seglen;
			if(start_scan(j, p, seglen - 2) != 0) {
				goto fail;
			}
			scans++;
			continue;
		} else if(tables(j, (int)m, j->d + j->pos + 2, seglen - 2) != 0) {
			goto fail;
		}
		j->pos += (size_t)seglen;
	}
	if(!j->have_frame || !scans) {
		goto fail;
	}
	for(i = 0; i < j->ncomp; i++) {
		Comp	*c = &j->c[i];
		int	stride = c->bw * 8, bx, by;

		c->pix = malloc((size_t)stride * c->bh * 8);
		xmap[i] = malloc((size_t)j->w * sizeof(int));
		if(!c->pix || !xmap[i]) {
			goto fail;
		}
		for(by = 0; by < c->bh; by++) {
			for(bx = 0; bx < c->bw; bx++) {
				idct(c->coef + ((size_t)by * c->bw + bx) * 64, j->qt[c->tq],
						c->pix + (size_t)by * 8 * stride + bx * 8, stride);
			}
		}
		for(x = 0; x < j->w; x++) {
			xmap[i][x] = x * c->h / j->hmax;
		}
	}
	out = malloc((size_t)j->w * j->h * 4);
	if(!out) {
		goto fail;
	}
	rgb = j->ncomp == 3 && (j->adobe == 0 || (j->adobe < 0 && j->c[0].id == 'R' && j->c[1].id == 'G' && j->c[2].id == 'B'));
	for(y = 0; y < j->h; y++) {
		const unsigned char *r0 = j->c[0].pix + (size_t)(y * j->c[0].v / j->vmax) * j->c[0].bw * 8, *r1 = NULL, *r2 = NULL;
		unsigned char	*o = out + (size_t)y * j->w * 4;

		if(j->ncomp == 3) {
			r1 = j->c[1].pix + (size_t)(y * j->c[1].v / j->vmax) * j->c[1].bw * 8;
			r2 = j->c[2].pix + (size_t)(y * j->c[2].v / j->vmax) * j->c[2].bw * 8;
		}
		for(x = 0; x < j->w; x++, o += 4) {
			int a = r0[xmap[0][x]];

			if(j->ncomp == 1) {
				o[0] = o[1] = o[2] = (unsigned char)a;
			} else if(rgb) {
				o[0] = (unsigned char)a;
				o[1] = r1[xmap[1][x]];
				o[2] = r2[xmap[2][x]];
			} else {
				int cb = r1[xmap[1][x]] - 128, cr = r2[xmap[2][x]] - 128;
				int r = a + ((91881 * cr + 32768) >> 16);
				int g = a - ((22554 * cb + 46802 * cr + 32768) >> 16);
				int b = a + ((116130 * cb + 32768) >> 16);

				o[0] = (unsigned char)(r < 0 ? 0 : (r > 255 ? 255 : r));
				o[1] = (unsigned char)(g < 0 ? 0 : (g > 255 ? 255 : g));
				o[2] = (unsigned char)(b < 0 ? 0 : (b > 255 ? 255 : b));
			}
			o[3] = 255;
		}
	}
	*w = j->w;
	*h = j->h;
fail:
	for(i = 0; i < 3; i++) {
		free(xmap[i]);
	}
	release(j);
	free(j);
	return out;
}
