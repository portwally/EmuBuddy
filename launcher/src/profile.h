/* EmuBuddy Launcher: a profile is a text file of "key = value" lines (see
 * docs/LAUNCHER_PLAN.md). This module reads and writes it and builds the
 * EmuBuddy command line from it. No SDL here, so it is easy to test. */
#ifndef LAUNCHER_PROFILE_H
#define LAUNCHER_PROFILE_H

#define PROFILE_MAX_KEYS	64
#define PROFILE_MAX_ARGS	128

typedef struct {
	char	key[32];
	char	value[600];
} ProfileKV;

typedef struct {
	char		path[1100];	/* the file ("" = not saved yet) */
	ProfileKV	kv[PROFILE_MAX_KEYS];	/* in file order; unknown keys are kept */
	int		n;
	char		comments[PROFILE_MAX_KEYS][200]; /* line before each key */
} Profile;

void	profile_init(Profile *p);
/* 0 ok, -1 can't read */
int	profile_load(Profile *p, const char *path);
int	profile_save(const Profile *p, const char *path);
/* "" if not set */
const char *profile_get(const Profile *p, const char *key);
void	profile_set(Profile *p, const char *key, const char *value);
/* The name of the profile: the key "name", else the file name. */
const char *profile_name(const Profile *p);

/* The EmuBuddy command line for a profile. argv[0] is the program. Strings
 * are stored in buf. Returns the number of arguments, or -1 if the profile
 * has a bad value (the reason is in err). cfgdir is where the profile's own
 * emubuddy.config lives ("" = none: use EmuBuddy's normal one). */
int	profile_build_args(const Profile *p, const char *exe, const char *cfgdir,
		char *buf, int bufsize, char **argv, int maxargs,
		char *err, int errsize);
/* The same as one line for a shell (arguments with spaces are quoted). */
void	profile_cmdline(char **argv, int argc, char *out, int outsize);
/* ~ at the start of a path becomes the home folder. */
void	profile_expand_path(const char *in, char *out, int outsize);

/* strcasestr is not everywhere (Windows); this one is. */
const char *eb_strcasestr(const char *hay, const char *needle);

#endif
