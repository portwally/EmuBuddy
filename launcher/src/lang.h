/* EmuBuddy Launcher: the texts in German. The program is written in English;
 * T("...") gives the German text when German is switched on and the table in
 * lang.c has one, else the English text itself. No SDL here. */
#ifndef LAUNCHER_LANG_H
#define LAUNCHER_LANG_H

const char	*lang_tr(const char *english);
#define T(s)	lang_tr(s)

void		lang_set_german(int on);
int		lang_german(void);
/* 1: the font in use has no umlauts, write ae oe ue ss instead. */
void		lang_set_plain(int on);
/* For the tests: the number of entries, and entry i. */
int		lang_count(void);
void		lang_entry(int i, const char **english, const char **german);

#endif
