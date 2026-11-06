/* EmuBuddy Launcher: the library of ROM files and disk images found in the
 * folders the user names. Nothing is copied or moved; this only looks. */
#ifndef LAUNCHER_LIBRARY_H
#define LAUNCHER_LIBRARY_H

#define LIB_ROM		0
#define LIB_DISK	1
#define LIB_MAX_ITEMS	4000

typedef struct {
	char	path[1100];
	char	name[120];	/* file name */
	int	kind;		/* LIB_ROM, LIB_DISK */
	long	size;
	char	label[64];	/* "ROM 03 (256 KB)", "3.5\" 800 KB", "Hard disk 32 MB" */
	char	volume[16];	/* ProDOS volume name, if it could be read */
	int	writable;
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

#endif
