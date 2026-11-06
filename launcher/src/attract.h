/* EmuBuddy Launcher: import from Attract-Mode (the ~/.attract folder).
 * An Attract-Mode "emulator" (emulators/NAME.cfg) says where the game files
 * and artwork are; its romlist (romlists/NAME.txt) lists the games. In the
 * common set-up every game is a small shell script that starts MAME (or any
 * emulator) with options like -ramsize 8M -hard1 disk.hdv; those options are
 * the same ones EmuBuddy understands, so the script is read to fill a profile.
 * Nothing in the Attract-Mode folder is changed. */
#ifndef LAUNCHER_ATTRACT_H
#define LAUNCHER_ATTRACT_H

#include "profile.h"

#define ATTRACT_MAX_EMUS	64

/* The names of the emulators found in the emulators folder (the .cfg files, sorted). */
int	attract_list_emulators(const char *dir, char names[][100], int max);
/* Build profiles for the games in the romlist of that emulator. Returns how
 * many were made (at most max), or -1 with a reason in err. */
int	attract_import(const char *dir, const char *emulator, Profile *out, int max, char *err, int errsize);

#endif
