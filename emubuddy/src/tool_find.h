/* EmuBuddy: find an external program (Ghostscript, FFmpeg) on this computer. */
#ifndef EMUBUDDY_TOOL_FIND_H
#define EMUBUDDY_TOOL_FIND_H

/* Looks for one of `names` (NULL-terminated list) in this order: the program
 * named by the environment variable env_var (the value "none" means "pretend
 * it is not installed", for tests), PATH, the usual Unix folders, and on
 * Windows win_dir + win_glob (for example "C:/Program Files/gs" and
 * "STAR/bin/gswin64c.exe"). Returns a malloc'ed path or NULL. */
char *tool_find(const char *env_var, const char *const *names,
				const char *win_dir, const char *win_glob);

#endif
