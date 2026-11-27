# Votrax SC-01-A chip model

The speech chip of the Mockingboard Sound/Speech I, from MAME's `src/devices/sound/votrax.cpp` and `votrax.h` by
Olivier Galibert (https://github.com/mamedev/mame, commit ecbaa509ee94), BSD-3-Clause license (`LICENSE`).

`votrax.c` is a C port. The chip logic, the ROM decoding, the filters and all their constants are MAME's, line for
line, with MAME's comments. What changed is the frame around them: MAME's device, sound stream and timer became a
plain struct, a sample loop and a tick counter, and the chip's own sample rate (clock / 18) is averaged down to the
host's rate. `votrax.h` is EmuBuddy's interface to it. EmuBuddy uses it through `mbspeech.c`.

The chip needs its 512-byte phoneme ROM, `sc01a.bin` (SHA-1 1d6da90b1807a01b5e186ef08476119a862b5e6d, as MAME
expects). EmuBuddy does not ship it: the user's file is read (the setting "SC-01 ROM", `-sc01rom`, or `sc01a.bin` or
MAME's `votrax.zip` next to the IIgs ROM).

To follow a newer MAME, compare its `votrax.cpp` with this port and carry the changes over; note the commit here.
