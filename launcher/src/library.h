/* EmuBuddy Launcher: the library of ROM files and disk images found in the
 * folders the user names. Nothing is copied or moved; this only looks. */
#ifndef LAUNCHER_LIBRARY_H
#define LAUNCHER_LIBRARY_H

#define LIB_ROM		0
#define LIB_DISK	1
#define LIB_MAX_ITEMS	4000
#define LIB_DUP		1
#define LIB_BAD		2

typedef struct {
	char	path[1100];
	char	name[120];	/* file name */
	int	kind;		/* LIB_ROM, LIB_DISK */
	long	size;
	char	label[64];	/* "ROM 03 (256 KB)", "3.5\" 800 KB", "Hard disk 32 MB" */
	char	volume[16];	/* ProDOS volume name, if it could be read */
	int	writable;
	int	flags;		/* LIB_DUP, LIB_BAD (set by lib_check) */
	char	note[120];	/* why it is flagged */
	char	tags[100];	/* comma separated, set by the user */
} LibItem;

typedef struct {
	LibItem	*items;
	int	n, cap;
} Library;

void	lib_init(Library *l);
void	lib_free(Library *l);
/* Scan the folders (and the folders below them, 4 levels). */
void	lib_scan(Library *l, char folders[][1100], int nfolders);
/* Classify one file. Returns -1 if it is neither a ROM nor a disk image. */
int	lib_classify(const char *path, long size, char *label, int labelsize);

/* Look at the files: marks copies of the same file (same size and content
 * at both ends) and disk images that look damaged. Reads the files, so do
 * it on request, not at every scan. */
void	lib_check(Library *l);
/* Tags: kept in a text file (path TAB tags). Apply after a scan; set saves. */
void	lib_tags_apply(Library *l, const char *file);
void	lib_tags_set(Library *l, int index, const char *tags, const char *file);
/* Does the item match the search words (all must be in name, label, volume
 * or tags)? Empty search matches everything. */
int	lib_matches(const LibItem *it, const char *search);

#endif
