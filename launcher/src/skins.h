/* EmuBuddy Launcher skins: a skin is a colour set and a window title style
 * for the shared UI toolkit (see UiTheme in emubuddy/src/ui.h). */
#ifndef LAUNCHER_SKINS_H
#define LAUNCHER_SKINS_H

#include "ui.h"

int		skin_count(void);
const UiTheme	*skin_get(int i);
int		skin_find(const char *name);	/* -1 if there is none */
/* Make skin i the current one. */
void		skin_apply(int i);

#endif
