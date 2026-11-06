#include "library.h"
#include "prodos_img.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

void
lib_init(Library *l)
{
	l->items = NULL;
	l->n = l->cap = 0;
}

void
lib_free(Library *l)
{
	free(l->items);
	lib_init(l);
}

static int
has_ext(const char *name, const char *ext)
{
	size_t	n = strlen(name), e = strlen(ext);

	return n > e && !strcasecmp(name + n - e, ext);
}

int
lib_classify(const char *path, long size, char *label, int labelsize)
{
	static const char *disk_ext[] = { ".po", ".2mg", ".hdv", ".dsk", ".do", ".woz",
		".dc", ".img", ".2img", NULL };
	const char	*name = strrchr(path, '/');
	int		i, is_disk = 0;

	name = name ? name + 1 : path;
	for(i = 0; disk_ext[i]; i++) {
		if(has_ext(name, disk_ext[i])) {
			is_disk = 1;
		}
	}
	if(is_disk) {
		if(has_ext(name, ".woz")) {
			snprintf(label, (size_t)labelsize, "WOZ disk");
		} else if(size > 0 && size <= 143360 + 64) {
			snprintf(label, (size_t)labelsize, "5.25\" 140 KB");
		} else if(size >= 800 * 1024 && size <= 800 * 1024 + 64) {
			snprintf(label, (size_t)labelsize, "3.5\" 800 KB");
		} else if(size >= 1440 * 1024 && size <= 1440 * 1024 + 64) {
			snprintf(label, (size_t)labelsize, "3.5\" 1.4 MB");
		} else if(size >= 2 * 1024 * 1024) {
			snprintf(label, (size_t)labelsize, "Hard disk %ld MB", size / (1024 * 1024));
		} else {
			snprintf(label, (size_t)labelsize, "Disk %ld KB", size / 1024);
		}
		return LIB_DISK;
	}
	/* A ROM: 128 KB (ROM 01) or 256 KB (ROM 03) with a name that says so */
	if((size == 131072 || size == 262144) &&
			(strcasestr(name, "rom") || has_ext(name, ".bin") ||
			 !strchr(name, '.'))) {
		snprintf(label, (size_t)labelsize, "ROM %s (%ld KB)",
					size == 131072 ? "01" : "03", size / 1024);
		return LIB_ROM;
	}
	return -1;
}

static void
add(Library *l, const char *path, long size, int writable)
{
	LibItem		*it;
	char		label[64];
	int		kind = lib_classify(path, size, label, sizeof(label));
	const char	*b;

	if(kind < 0 || l->n >= LIB_MAX_ITEMS) {
		return;
	}
	for(int i = 0; i < l->n; i++) {		/* the same file from two folders */
		if(!strcmp(l->items[i].path, path)) {
			return;
		}
	}
	if(l->n >= l->cap) {
		l->cap = l->cap ? l->cap * 2 : 128;
		l->items = realloc(l->items, (size_t)l->cap * sizeof(LibItem));
	}
	it = &l->items[l->n++];
	memset(it, 0, sizeof(*it));
	snprintf(it->path, sizeof(it->path), "%s", path);
	b = strrchr(path, '/');
	snprintf(it->name, sizeof(it->name), "%s", b ? b + 1 : path);
	it->kind = kind;
	it->size = size;
	it->writable = writable;
	snprintf(it->label, sizeof(it->label), "%s", label);
	if(kind == LIB_DISK && !has_ext(it->name, ".woz") && size >= 512 * 8) {
		PImg		img;
		const char	*err;

		if(pimg_open(&img, path, &err) == 0) {
			snprintf(it->volume, sizeof(it->volume), "%s", img.volume);
			pimg_close(&img);
		}
	}
}

static void
walk(Library *l, const char *dir, int depth)
{
	DIR		*d = opendir(dir);
	struct dirent	*e;
	char		path[1200];
	struct stat	sb;

	if(!d) {
		return;
	}
	while((e = readdir(d)) != NULL) {
		if(e->d_name[0] == '.') {
			continue;
		}
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		if(stat(path, &sb) != 0) {
			continue;
		}
		if(S_ISDIR(sb.st_mode)) {
			if(depth < 4) {
				walk(l, path, depth + 1);
			}
		} else if(S_ISREG(sb.st_mode)) {
			add(l, path, (long)sb.st_size, access(path, W_OK) == 0);
		}
	}
	closedir(d);
}

static int
by_name(const void *a, const void *b)
{
	const LibItem *x = a, *y = b;

	if(x->kind != y->kind) {
		return x->kind - y->kind;
	}
	return strcasecmp(x->name, y->name);
}

void
lib_scan(Library *l, char folders[][1100], int nfolders)
{
	lib_free(l);
	for(int i = 0; i < nfolders; i++) {
		walk(l, folders[i], 0);
	}
	if(l->n) {
		qsort(l->items, (size_t)l->n, sizeof(LibItem), by_name);
	}
}

/* ---- checks ---- */

static unsigned long
file_hash(const char *path, long size)
{
	FILE		*f = fopen(path, "rb");
	unsigned long	h = 1469598103934665603UL ^ (unsigned long)size;
	unsigned char	buf[65536];
	size_t		n, i;

	if(!f) {
		return 0;
	}
	n = fread(buf, 1, sizeof(buf), f);
	for(i = 0; i < n; i++) {
		h = (h ^ buf[i]) * 1099511628211UL;
	}
	if(size > (long)sizeof(buf) * 2 && fseek(f, size - (long)sizeof(buf), SEEK_SET) == 0) {
		n = fread(buf, 1, sizeof(buf), f);
		for(i = 0; i < n; i++) {
			h = (h ^ buf[i]) * 1099511628211UL;
		}
	}
	fclose(f);
	return h;
}

void
lib_check(Library *l)
{
	unsigned long	*h = calloc((size_t)(l->n + 1), sizeof(unsigned long));
	int		i, j;

	if(!h) {
		return;
	}
	for(i = 0; i < l->n; i++) {
		LibItem *it = &l->items[i];

		it->flags = 0;
		it->note[0] = 0;
		h[i] = file_hash(it->path, it->size);
	}
	for(i = 0; i < l->n; i++) {
		LibItem *it = &l->items[i];

		for(j = 0; j < i; j++) {
			if(l->items[j].size == it->size && h[j] == h[i] && h[i] &&
					l->items[j].kind == it->kind) {
				it->flags |= LIB_DUP;
				snprintf(it->note, sizeof(it->note), "same as %s", l->items[j].name);
				break;
			}
		}
		if(it->kind == LIB_DISK) {
			const char *dot = strrchr(it->name, '.');
			long	data = it->size;
			int	is_2mg = dot && !strcasecmp(dot, ".2mg");
			int	prodos_ext = dot && (!strcasecmp(dot, ".po") || !strcasecmp(dot, ".hdv") || is_2mg);

			if(is_2mg) {
				data -= 64;
			}
			if(prodos_ext && !(it->flags & LIB_DUP)) {
				if(data % 512 != 0) {
					it->flags |= LIB_BAD;
					snprintf(it->note, sizeof(it->note), "size is not a whole number of blocks");
				} else if(!it->volume[0]) {
					it->flags |= LIB_BAD;
					snprintf(it->note, sizeof(it->note), "no ProDOS volume found");
				}
			}
		}
	}
	free(h);
}

/* ---- tags ---- */

void
lib_tags_apply(Library *l, const char *file)
{
	FILE	*f = fopen(file, "r");
	char	line[1400];

	if(!f) {
		return;
	}
	while(fgets(line, sizeof(line), f)) {
		char	*tab = strchr(line, '\t');

		line[strcspn(line, "\r\n")] = 0;
		if(!tab) {
			continue;
		}
		*tab = 0;
		for(int i = 0; i < l->n; i++) {
			if(!strcmp(l->items[i].path, line)) {
				snprintf(l->items[i].tags, sizeof(l->items[i].tags), "%s", tab + 1);
			}
		}
	}
	fclose(f);
}

void
lib_tags_set(Library *l, int index, const char *tags, const char *file)
{
	FILE	*f;
	char	(*keep)[1400] = calloc(LIB_MAX_ITEMS, 1400);
	int	nkeep = 0;
	char	line[1400];

	if(index < 0 || index >= l->n || !keep) {
		free(keep);
		return;
	}
	snprintf(l->items[index].tags, sizeof(l->items[index].tags), "%s", tags);
	f = fopen(file, "r");		/* keep the tags of files that are not in the library now */
	while(f && fgets(line, sizeof(line), f) && nkeep < LIB_MAX_ITEMS) {
		char	*tab = strchr(line, '\t');
		int	known = 0;

		line[strcspn(line, "\r\n")] = 0;
		if(!tab) {
			continue;
		}
		*tab = 0;
		for(int i = 0; i < l->n; i++) {
			if(!strcmp(l->items[i].path, line)) {
				known = 1;
			}
		}
		if(!known) {
			snprintf(keep[nkeep++], 1400, "%s\t%s", line, tab + 1);
		}
	}
	if(f) {
		fclose(f);
	}
	f = fopen(file, "w");
	if(f) {
		for(int i = 0; i < nkeep; i++) {
			fprintf(f, "%s\n", keep[i]);
		}
		for(int i = 0; i < l->n; i++) {
			if(l->items[i].tags[0]) {
				fprintf(f, "%s\t%s\n", l->items[i].path, l->items[i].tags);
			}
		}
		fclose(f);
	}
	free(keep);
}

int
lib_matches(const LibItem *it, const char *search)
{
	char	words[200], hay[600], *w, *save = NULL;

	if(!search || !search[0]) {
		return 1;
	}
	snprintf(hay, sizeof(hay), "%s %s %s %s", it->name, it->label, it->volume, it->tags);
	snprintf(words, sizeof(words), "%s", search);
	for(w = strtok_r(words, " ", &save); w; w = strtok_r(NULL, " ", &save)) {
		if(!strcasestr(hay, w)) {
			return 0;
		}
	}
	return 1;
}
