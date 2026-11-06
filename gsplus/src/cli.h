/* EmuBuddy command line (MAME apple2gs style). See cli.c. */
#ifndef EMUBUDDY_CLI_H
#define EMUBUDDY_CLI_H

/* Translates EmuBuddy options into KEGS core arguments in place.
 * Returns 0, or -1 after printing an error. Exits after -help. */
int	emubuddy_cli(int *argc, char ***argv);
/* Shared folders and loose image files, inserted after start-up. */
int	emubuddy_cli_later_count(void);
const char *emubuddy_cli_later(int i);
/* Window size from -resolution / -scale (0 = not given). */
void	emubuddy_cli_window(int *w, int *h, int *scale);
/* 1 when settings came from the command line and must not be saved. */
extern int	g_config_session_only;

#endif
