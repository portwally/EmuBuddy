/* EmuBuddy host bridge. See bridge.h and docs/BRIDGE.md.
 *
 * Everything the IIgs program asks for is checked against ONE folder, the
 * bridge folder: paths are relative to it, ".." and absolute paths are
 * refused, and the real path of every target must stay inside it. The
 * emulated software is not trusted. */
#include "bridge.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
# include <stdlib.h>
#else
# include <limits.h>
#endif

char	*g_bridge_root = "";
int	(*bridge_overlap_check)(const char *folder) = NULL;

static BridgeRd	g_rd;
static BridgeWr	g_wr;

void
bridge_set_memory(BridgeRd rd, BridgeWr wr)
{
	g_rd = rd;
	g_wr = wr;
}

/* ---- GS/OS error codes ---- */
#define E_OK		0x00
#define E_IO		0x27
#define E_NODEV		0x2f
#define E_BADPATH	0x40
#define E_BADREF	0x43
#define E_PATHNF	0x44
#define E_NOTFOUND	0x46
#define E_DUP		0x47
#define E_FULL		0x48
#define E_ACCESS	0x4e
#define E_PARM		0x53
#define E_NOMEM		0x54

/* ---- commands ---- */
#define C_INFO		0x0000
#define C_LIST		0x0010
#define C_MKDIR		0x0011
#define C_DELETE	0x0012
#define C_GETINFO	0x0013
#define C_OPEN		0x0020
#define C_READ		0x0021
#define C_WRITE		0x0022
#define C_CLOSE		0x0023

/* parameter block offsets */
#define B_CMD		0x00
#define B_STATUS	0x02
#define B_P1		0x04
#define B_P2		0x08
#define B_P3		0x0c
#define B_P4		0x10
#define B_R1		0x14
#define B_R2		0x18
#define B_R3		0x1c
#define B_R4		0x20

/* ---- IIgs memory access ---- */

static unsigned
mem8(unsigned long a)
{
	return g_rd ? g_rd(a & 0xffffff) & 0xff : 0;
}

static unsigned
mem16(unsigned long a)
{
	return mem8(a) | (mem8(a + 1) << 8);
}

static unsigned long
mem32(unsigned long a)
{
	return mem16(a) | ((unsigned long)mem16(a + 2) << 16);
}

static void
put8(unsigned long a, unsigned v)
{
	if(g_wr) {
		g_wr(a & 0xffffff, v & 0xff);
	}
}

static void
put16(unsigned long a, unsigned v)
{
	put8(a, v);
	put8(a + 1, v >> 8);
}

static void
put32(unsigned long a, unsigned long v)
{
	put16(a, (unsigned)(v & 0xffff));
	put16(a + 2, (unsigned)(v >> 16));
}

/* ---- names: host <-> ProDOS (the shared-folder convention) ----
 *   NAME,t$TT,a$AAAA  type and aux type;  NAME.txt = text;  neither = BIN. */

static int
hexval(int c)
{
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

void
bridge_decode_name(const char *host, char *out, int outsize, int *ftype, int *aux)
{
	char	tmp[512];
	int	len, i, j, v, n, have_t = 0, have_a = 0, t = 0x06, a = 0;

	snprintf(tmp, sizeof(tmp), "%s", host);
	len = (int)strlen(tmp);
	/* ",t$TT" and ",a$AAAA" anywhere in the name */
	for(i = 0; i + 3 < len; i++) {
		if(tmp[i] == ',' && (tmp[i + 1] == 't' || tmp[i + 1] == 'T' ||
				tmp[i + 1] == 'a' || tmp[i + 1] == 'A') &&
				tmp[i + 2] == '$') {
			v = 0;
			n = 0;
			for(j = i + 3; j < len && hexval(tmp[j]) >= 0 && n < 4; j++) {
				v = v * 16 + hexval(tmp[j]);
				n++;
			}
			if(n == 0) {
				continue;
			}
			if(tmp[i + 1] == 't' || tmp[i + 1] == 'T') {
				t = v & 0xff;
				have_t = 1;
			} else {
				a = v & 0xffff;
				have_a = 1;
			}
			memmove(tmp + i, tmp + j, (size_t)(len - j + 1));
			len -= (j - i);
			i--;
		}
	}
	/* cadius style: NAME#TTAAAA */
	if(!have_t && len > 7 && tmp[len - 7] == '#') {
		int ok = 1;

		for(i = len - 6; i < len; i++) {
			if(hexval(tmp[i]) < 0) {
				ok = 0;
			}
		}
		if(ok) {
			t = (hexval(tmp[len - 6]) << 4) | hexval(tmp[len - 5]);
			a = (hexval(tmp[len - 4]) << 12) | (hexval(tmp[len - 3]) << 8) |
				(hexval(tmp[len - 2]) << 4) | hexval(tmp[len - 1]);
			have_t = have_a = 1;
			tmp[len - 7] = 0;
			len -= 7;
		}
	}
	if(!have_t && len > 4 && !SDL_strcasecmp(tmp + len - 4, ".txt")) {
		t = 0x04;
		tmp[len - 4] = 0;
	}
	(void)have_a;
	snprintf(out, (size_t)outsize, "%s", tmp);
	if(ftype) *ftype = t;
	if(aux) *aux = a;
}

void
bridge_encode_name(const char *prodos, int ftype, int aux, char *out, int outsize)
{
	ftype &= 0xff;
	aux &= 0xffff;
	if(ftype == 0x04 && aux == 0) {
		snprintf(out, (size_t)outsize, "%s.txt", prodos);
	} else if(ftype == 0x06 && aux == 0) {
		snprintf(out, (size_t)outsize, "%s", prodos);
	} else {
		char	sfx[40] = "";

		if(ftype != 0x06) {
			snprintf(sfx, sizeof(sfx), ",t$%02X", ftype);
		}
		if(aux != 0) {
			size_t l = strlen(sfx);

			snprintf(sfx + l, sizeof(sfx) - l, ",a$%04X", aux);
		}
		snprintf(out, (size_t)outsize, "%s%s", prodos, sfx);
	}
}

/* ---- the folder jail ---- */

static int
name_ok(const char *s)
{
	size_t	i, n = strlen(s);

	if(n == 0 || n > 200 || !strcmp(s, ".") || !strcmp(s, "..")) {
		return 0;
	}
	for(i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];

		if(c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':' ||
								c == '*' || c == '?') {
			return 0;
		}
	}
	return 1;
}

/* Real path of p (symlinks resolved), or "" if it doesn't exist. */
static int
real_path(const char *p, char *out, size_t n)
{
#ifdef _WIN32
	char	*r = _fullpath(out, p, n);
#else
	char	tmp[PATH_MAX], *r = realpath(p, tmp);

	if(r) {
		snprintf(out, n, "%s", tmp);
	}
#endif
	return r != NULL;
}

/* Is child inside (or equal to) root? Both are real paths. */
static int
path_inside(const char *root, const char *child)
{
	size_t	n = strlen(root);

	if(strncmp(root, child, n) != 0) {
		return 0;
	}
	return child[n] == 0 || child[n] == '/' || (n > 0 && root[n - 1] == '/');
}

/* ---- directory scan with ProDOS name decoding ---- */

typedef struct {
	char	host[260];
	char	name[260];
	int	isdir, ftype, aux;
	unsigned long size;
} DirEnt;

typedef struct {
	DirEnt	*e;
	int	n, cap;
} DirList;

static SDL_EnumerationResult SDLCALL
scan_cb(void *ud, const char *dirname, const char *fname)
{
	DirList		*dl = ud;
	char		full[1500];
	SDL_PathInfo	info;
	DirEnt		*d;

	if(fname[0] == '.' || strlen(fname) > 250) {
		return SDL_ENUM_CONTINUE;
	}
	snprintf(full, sizeof(full), "%s%s%s", dirname,
			dirname[strlen(dirname) - 1] == '/' ? "" : "/", fname);
	if(!SDL_GetPathInfo(full, &info) || (info.type != SDL_PATHTYPE_FILE &&
					info.type != SDL_PATHTYPE_DIRECTORY)) {
		return SDL_ENUM_CONTINUE;
	}
	if(dl->n == dl->cap) {
		int	nc = dl->cap ? dl->cap * 2 : 64;
		DirEnt	*ne;

		if(nc > 8192) {
			return SDL_ENUM_CONTINUE;
		}
		ne = realloc(dl->e, sizeof(DirEnt) * (size_t)nc);
		if(!ne) {
			return SDL_ENUM_CONTINUE;
		}
		dl->e = ne;
		dl->cap = nc;
	}
	d = &dl->e[dl->n++];
	snprintf(d->host, sizeof(d->host), "%s", fname);
	d->isdir = info.type == SDL_PATHTYPE_DIRECTORY;
	d->size = (unsigned long)info.size;
	if(d->isdir) {
		snprintf(d->name, sizeof(d->name), "%s", fname);
		d->ftype = 0x0f;
		d->aux = 0;
	} else {
		bridge_decode_name(fname, d->name, sizeof(d->name), &d->ftype,
								&d->aux);
	}
	return SDL_ENUM_CONTINUE;
}

static int
cmp_ent(const void *a, const void *b)
{
	return SDL_strcasecmp(((const DirEnt *)a)->name, ((const DirEnt *)b)->name);
}

static void
scan_dir(const char *dir, DirList *dl)
{
	dl->e = NULL;
	dl->n = dl->cap = 0;
	SDL_EnumerateDirectory(dir, scan_cb, dl);
	if(dl->n > 1) {
		qsort(dl->e, (size_t)dl->n, sizeof(DirEnt), cmp_ent);
	}
}

/* ---- paths from the IIgs: u16 length + characters, '/' or ':' between names ---- */

#define MAX_COMPS	32

typedef struct {
	char	name[MAX_COMPS][256];
	int	n;
} Comps;

static int
read_path(unsigned long ptr, Comps *c)
{
	unsigned	len, i, k = 0;
	char		cur[256];

	c->n = 0;
	if(ptr == 0) {
		return 0;			/* no path: the root */
	}
	len = mem16(ptr);
	if(len > 1000) {
		return E_BADPATH;
	}
	for(i = 0; i <= len; i++) {
		int ch = (i < len) ? (int)mem8(ptr + 2 + i) : '/';

		if(ch == '/' || ch == ':') {
			if(k > 0) {
				cur[k] = 0;
				if(!strcmp(cur, ".")) {
					/* skip */
				} else if(!name_ok(cur) || c->n >= MAX_COMPS) {
					return E_BADPATH;
				} else {
					snprintf(c->name[c->n++], 256, "%s", cur);
				}
				k = 0;
			}
		} else {
			if(k + 1 >= sizeof(cur)) {
				return E_BADPATH;
			}
			cur[k++] = (char)ch;
		}
	}
	return 0;
}

typedef struct {
	char	root[1100];		/* real path of the bridge folder */
} Ctx;

static int
ctx_open(Ctx *cx)
{
	char	rr[1100];

	if(!g_bridge_root || !g_bridge_root[0]) {
		return E_NODEV;
	}
	if(!real_path(g_bridge_root, rr, sizeof(rr))) {
		return E_NODEV;
	}
	if(bridge_overlap_check && bridge_overlap_check(rr)) {
		return E_ACCESS;
	}
	snprintf(cx->root, sizeof(cx->root), "%s", rr);
	return 0;
}

/* Walk the first n components (existing folders only), return the host
 * directory in dir. */
static int
walk_dirs(Ctx *cx, const Comps *c, int n, char *dir, size_t dirsize)
{
	int	i, k;
	DirList	dl;
	char	next[1500], rp[1500];

	snprintf(dir, dirsize, "%s", cx->root);
	for(i = 0; i < n; i++) {
		int found = 0;

		scan_dir(dir, &dl);
		for(k = 0; k < dl.n; k++) {
			if(dl.e[k].isdir && !SDL_strcasecmp(dl.e[k].name, c->name[i])) {
				snprintf(next, sizeof(next), "%s/%s", dir, dl.e[k].host);
				found = 1;
				break;
			}
		}
		free(dl.e);
		if(!found) {
			return E_PATHNF;
		}
		if(!real_path(next, rp, sizeof(rp)) || !path_inside(cx->root, rp)) {
			return E_ACCESS;
		}
		snprintf(dir, dirsize, "%s", rp);
	}
	return 0;
}

/* Find an existing entry (file or folder) by its ProDOS name in dir. */
static int
find_entry(const char *dir, const char *name, DirEnt *out)
{
	DirList	dl;
	int	k, found = 0;

	scan_dir(dir, &dl);
	for(k = 0; k < dl.n; k++) {
		if(!SDL_strcasecmp(dl.e[k].name, name)) {
			*out = dl.e[k];
			found = 1;
			break;
		}
	}
	free(dl.e);
	return found;
}

/* ---- open files ---- */

#define MAX_HANDLES	8

typedef struct {
	SDL_IOStream	*io;
	int		write;
} Handle;
static Handle	g_h[MAX_HANDLES];

void
bridge_close_all(void)
{
	int	i;

	for(i = 0; i < MAX_HANDLES; i++) {
		if(g_h[i].io) {
			SDL_CloseIO(g_h[i].io);
			g_h[i].io = NULL;
		}
	}
}

/* ---- the commands ---- */

static int
cmd_info(unsigned long b)
{
	unsigned long	buf = mem32(b + B_P1);
	unsigned	size = (unsigned)mem32(b + B_P2), i, n;
	const char	*name;

	put32(b + B_R1, BRIDGE_VERSION);
	put32(b + B_R2, BRIDGE_MAX_XFER);
	if(!g_bridge_root || !g_bridge_root[0]) {
		return E_NODEV;
	}
	if(buf && size >= 3) {			/* the folder's name: u16 + chars */
		name = strrchr(g_bridge_root, '/');
		name = name ? name + 1 : g_bridge_root;
		n = (unsigned)strlen(name);
		if(n > size - 2) {
			n = size - 2;
		}
		put16(buf, n);
		for(i = 0; i < n; i++) {
			put8(buf + 2 + i, (unsigned char)name[i]);
		}
	}
	return E_OK;
}

static int
cmd_list(unsigned long b, Ctx *cx)
{
	Comps	c;
	char	dir[1500];
	DirList	dl;
	int	rc, i, count = 0;
	unsigned long out = mem32(b + B_P2), pos = 0;
	unsigned long size = mem32(b + B_P3), start = mem32(b + B_P4);

	if((rc = read_path(mem32(b + B_P1), &c)) != 0 ||
			(rc = walk_dirs(cx, &c, c.n, dir, sizeof(dir))) != 0) {
		return rc;
	}
	scan_dir(dir, &dl);
	put32(b + B_R2, 0);
	for(i = (int)start; i < dl.n; i++) {
		DirEnt	*d = &dl.e[i];
		size_t	nl = strlen(d->name);
		unsigned long len = 12 + nl;

		if(pos + len > size) {
			put32(b + B_R2, 1);		/* more entries */
			break;
		}
		put16(out + pos, (unsigned)len);
		put8(out + pos + 2, d->isdir ? 1 : 0);
		put8(out + pos + 3, (unsigned)d->ftype);
		put16(out + pos + 4, (unsigned)d->aux);
		put32(out + pos + 6, d->isdir ? 0 : d->size);
		put16(out + pos + 10, (unsigned)nl);
		{
			size_t k;

			for(k = 0; k < nl; k++) {
				put8(out + pos + 12 + k, (unsigned char)d->name[k]);
			}
		}
		pos += len;
		count++;
	}
	free(dl.e);
	put32(b + B_R1, (unsigned long)count);
	return E_OK;
}

static int
cmd_mkdir(unsigned long b, Ctx *cx)
{
	Comps	c;
	char	dir[1500], path[1800];
	DirEnt	e;
	int	rc;

	if((rc = read_path(mem32(b + B_P1), &c)) != 0 || c.n == 0) {
		return rc ? rc : E_BADPATH;
	}
	if((rc = walk_dirs(cx, &c, c.n - 1, dir, sizeof(dir))) != 0) {
		return rc;
	}
	if(find_entry(dir, c.name[c.n - 1], &e)) {
		return E_DUP;
	}
	snprintf(path, sizeof(path), "%s/%s", dir, c.name[c.n - 1]);
	return SDL_CreateDirectory(path) ? E_OK : E_IO;
}

static int
cmd_delete(unsigned long b, Ctx *cx)
{
	Comps	c;
	char	dir[1500], path[1800];
	DirEnt	e;
	int	rc;

	if((rc = read_path(mem32(b + B_P1), &c)) != 0 || c.n == 0) {
		return rc ? rc : E_BADPATH;
	}
	if((rc = walk_dirs(cx, &c, c.n - 1, dir, sizeof(dir))) != 0) {
		return rc;
	}
	if(!find_entry(dir, c.name[c.n - 1], &e)) {
		return E_NOTFOUND;
	}
	snprintf(path, sizeof(path), "%s/%s", dir, e.host);
	/* SDL_RemovePath only removes empty folders */
	return SDL_RemovePath(path) ? E_OK : E_ACCESS;
}

static int
cmd_getinfo(unsigned long b, Ctx *cx)
{
	Comps	c;
	char	dir[1500];
	DirEnt	e;
	int	rc;

	if((rc = read_path(mem32(b + B_P1), &c)) != 0) {
		return rc;
	}
	if(c.n == 0) {				/* the bridge folder itself */
		put32(b + B_R1, 1);
		put32(b + B_R2, 0);
		put32(b + B_R3, 0x0f);
		put32(b + B_R4, 0);
		return E_OK;
	}
	if((rc = walk_dirs(cx, &c, c.n - 1, dir, sizeof(dir))) != 0) {
		return rc;
	}
	if(!find_entry(dir, c.name[c.n - 1], &e)) {
		return E_NOTFOUND;
	}
	put32(b + B_R1, e.isdir ? 1 : 0);
	put32(b + B_R2, e.isdir ? 0 : e.size);
	put32(b + B_R3, (unsigned long)e.ftype);
	put32(b + B_R4, (unsigned long)e.aux);
	return E_OK;
}

static int
cmd_open(unsigned long b, Ctx *cx)
{
	Comps	c;
	char	dir[1500], path[1800], hostname[600];
	DirEnt	e;
	int	rc, h, mode = (int)mem32(b + B_P2), exists;
	SDL_IOStream *io;

	if((rc = read_path(mem32(b + B_P1), &c)) != 0 || c.n == 0) {
		return rc ? rc : E_BADPATH;
	}
	if(mode != 1 && mode != 2) {
		return E_PARM;
	}
	for(h = 0; h < MAX_HANDLES && g_h[h].io; h++) {
	}
	if(h == MAX_HANDLES) {
		return E_NOMEM;
	}
	if((rc = walk_dirs(cx, &c, c.n - 1, dir, sizeof(dir))) != 0) {
		return rc;
	}
	exists = find_entry(dir, c.name[c.n - 1], &e);
	if(mode == 1) {
		if(!exists || e.isdir) {
			return E_NOTFOUND;
		}
		snprintf(path, sizeof(path), "%s/%s", dir, e.host);
		io = SDL_IOFromFile(path, "rb");
		if(!io) {
			return E_ACCESS;
		}
		put32(b + B_R2, e.size);
		put32(b + B_R3, (unsigned long)e.ftype);
		put32(b + B_R4, (unsigned long)e.aux);
	} else {
		/* create or replace; the type goes into the host file name */
		if(exists) {
			if(e.isdir) {
				return E_DUP;
			}
			snprintf(path, sizeof(path), "%s/%s", dir, e.host);
			SDL_RemovePath(path);
		}
		bridge_encode_name(c.name[c.n - 1], (int)mem32(b + B_P3),
					(int)mem32(b + B_P4), hostname, sizeof(hostname));
		if(!name_ok(hostname)) {
			return E_BADPATH;
		}
		snprintf(path, sizeof(path), "%s/%s", dir, hostname);
		io = SDL_IOFromFile(path, "wb");
		if(!io) {
			return E_ACCESS;
		}
	}
	g_h[h].io = io;
	g_h[h].write = (mode == 2);
	put32(b + B_R1, (unsigned long)(h + 1));
	return E_OK;
}

static Handle *
get_handle(unsigned long id)
{
	if(id < 1 || id > MAX_HANDLES || !g_h[id - 1].io) {
		return NULL;
	}
	return &g_h[id - 1];
}

static int
cmd_read(unsigned long b)
{
	Handle		*h = get_handle(mem32(b + B_P1));
	unsigned long	buf = mem32(b + B_P2), count = mem32(b + B_P3), i;
	unsigned char	*tmp;
	size_t		got;

	if(!h || h->write) {
		return E_BADREF;
	}
	if(count > BRIDGE_MAX_XFER) {
		count = BRIDGE_MAX_XFER;
	}
	tmp = malloc(count ? count : 1);
	if(!tmp) {
		return E_NOMEM;
	}
	got = SDL_ReadIO(h->io, tmp, count);
	for(i = 0; i < got; i++) {
		put8(buf + i, tmp[i]);
	}
	free(tmp);
	put32(b + B_R1, (unsigned long)got);
	return E_OK;
}

static int
cmd_write(unsigned long b)
{
	Handle		*h = get_handle(mem32(b + B_P1));
	unsigned long	buf = mem32(b + B_P2), count = mem32(b + B_P3), i;
	unsigned char	*tmp;
	size_t		wr;

	if(!h || !h->write) {
		return E_BADREF;
	}
	if(count > BRIDGE_MAX_XFER) {
		return E_PARM;
	}
	tmp = malloc(count ? count : 1);
	if(!tmp) {
		return E_NOMEM;
	}
	for(i = 0; i < count; i++) {
		tmp[i] = (unsigned char)mem8(buf + i);
	}
	wr = SDL_WriteIO(h->io, tmp, count);
	free(tmp);
	put32(b + B_R1, (unsigned long)wr);
	return wr == count ? E_OK : E_FULL;
}

static int
cmd_close(unsigned long b)
{
	Handle	*h = get_handle(mem32(b + B_P1));
	int	ok;

	if(!h) {
		return E_BADREF;
	}
	ok = SDL_CloseIO(h->io);
	h->io = NULL;
	return ok ? E_OK : E_IO;
}

int
bridge_command(unsigned long block)
{
	Ctx	cx;
	int	rc;
	unsigned cmd = mem16(block + B_CMD);

	if(cmd == C_INFO) {
		rc = cmd_info(block);
	} else if(cmd == C_CLOSE) {
		rc = cmd_close(block);		/* always allowed */
	} else if(cmd == C_READ || cmd == C_WRITE) {
		/* open handles keep working; re-check the folder is still allowed */
		if((rc = ctx_open(&cx)) == 0) {
			rc = cmd == C_READ ? cmd_read(block) : cmd_write(block);
		}
	} else if((rc = ctx_open(&cx)) == 0) {
		switch(cmd) {
		case C_LIST: rc = cmd_list(block, &cx); break;
		case C_MKDIR: rc = cmd_mkdir(block, &cx); break;
		case C_DELETE: rc = cmd_delete(block, &cx); break;
		case C_GETINFO: rc = cmd_getinfo(block, &cx); break;
		case C_OPEN: rc = cmd_open(block, &cx); break;
		default: rc = E_PARM; break;
		}
	}
	put16(block + B_STATUS, (unsigned)rc);
	return rc;
}
