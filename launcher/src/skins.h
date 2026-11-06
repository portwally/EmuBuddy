/* EmuBuddy Launcher skins: a skin is a colour set and a window title style
 * for the shared UI toolkit (see UiTheme in emubuddy/src/ui.h). */
#ifndef LAUNCHER_SKINS_H
#define LAUNCHER_SKINS_H

#include "ui.h"

int		skin_count(void);
const UiTheme	*skin_get(int i);
int		skin_find(const char *name);	/* -1 if there is none */
enum { SKIN_LAYOUT_LIST, SKIN_LAYOUT_SHELF, SKIN_LAYOUT_FLOW };
enum { SKIN_DESK_PLAIN, SKIN_DESK_NEON };
const char	*skin_font(int i, int *ppem);
const char	*skin_font_fallback(int i, int *ppem);
int		skin_layout(int i);
int		skin_desktop(int i);
/* Make skin i the current one. */
void		skin_apply(int i);

#endif
