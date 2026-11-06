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
