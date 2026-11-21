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
/* Window title from -title / -profile ("" = default). */
const char *emubuddy_cli_title(void);
/* 1 when the Launcher started this EmuBuddy */
int cli_from_launcher(void);
/* File from -quitshot: a PNG of the screen is saved there when EmuBuddy quits ("" = none). */
const char *emubuddy_cli_quitshot(void);
/* 1 when settings came from the command line and must not be saved. */
extern int	g_config_session_only;

#endif
