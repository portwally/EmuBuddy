EmuBuddy on Linux
=================

This download holds two programs: EmuBuddy (the Apple IIgs emulator) and
EmuBuddy-Launcher (a front end with profiles and skins). Unpack the archive
anywhere and start ./EmuBuddy or ./EmuBuddy-Launcher.

Libraries
---------

The SDL3 and libslirp libraries are in the archive, next to the programs
(libSDL3.so.0, libslirp.so.0), so nothing has to be installed for them.
Everything else belongs to a normal desktop system. The programs were built
on Ubuntu 24.04; a distribution much older than that may be too old for them.

If a program does not start, run it from a terminal. A line such as

    error while loading shared libraries: libglib-2.0.so.0: cannot open ...

names the library that is missing; install it with your package manager.

What else you need
------------------

- An Apple IIgs ROM file. It is Apple's copyrighted firmware and is not
  included. See README.md, which is next to this file.
- For the web gateway (HTTPS sites in IIgs browsers): Python 3. For pictures
  through the gateway also the Pillow library (Debian and Ubuntu:
  sudo apt install python3-pil).

README.md has the set-up, the keys and everything else.
