# EmuBuddy command line

EmuBuddy can be started with options in the style of MAME's `apple2gs`
driver. Everything is also available in the app's menus; the command line is
for scripts, launchers and shortcuts.

```
EmuBuddy [apple2gs] [options] [disk image or folder ...]
```

Options apply to **that run only**: nothing from the run is written to the
config file, so your normal setup stays as it was. Add `-save` to keep them.

## Disks

| Option | Drive |
|---|---|
| `-flop1 <img>`, `-flop2 <img>` | 5.25" drives (slot 6, drive 1/2) |
| `-flop3 <img>`, `-flop4 <img>` | 3.5" drives (slot 5, drive 1/2) |
| `-hard1 <img>` … `-hard7 <img>` | SCSI hard disks (slot 7, drive 1–7) |
| `-cdrom <img>` | a CD image (`.iso`, `.cdr`, `.toast`, or a `.cue` sheet with its `.bin`; audio tracks play) in hard disk drive 7, the CD-ROM drive of the High-Speed SCSI Card (`-sl7 hsscsi`) |
| `-fujinet on\|off` | FujiNet: its devices follow the hard disks of the Hard Disk Card. Needs FujiNet-PC running on this computer. |
| `-fujinetport <n>` | the port FujiNet-PC connects to (default 1985) |
| `-fujinetpc <folder>` | the folder of FujiNet-PC: EmuBuddy starts it when FujiNet is on and stops it at the end |
| `-share <folder>` | a Mac/PC folder as a live ProDOS volume |
| `<img or folder>` | goes into the first free drive that fits |

Images: `.po .hdv .2mg .dsk .do .woz .dc .shk .sdk` and more.

## Slots

| Option | Cards |
|---|---|
| `-sl1 <card>` | `imagewriter`, `postscript` (also `laserwriter`), `modem`, `none` (printer port) |
| `-sl2 <card>` | `modem`, `imagewriter`, `postscript`, `fujinet` (the FujiNet as a card of its own: the IIgs can start up from it, and it works next to the High-Speed SCSI Card), `none` (modem port) |
| `-sl3 <card>` | `uthernet2`, `voc` (Video Overlay Card), `secondsight` (Second Sight VGA card), `none` |
| `-sl4 <card>` | `mockingboard`, `uthernet2`, `none` |
| `-sl7 <card>` | `hd` (the Hard Disk Card; `scsi` and `cffa2` mean the same), `hsscsi` (Apple II High-Speed SCSI Card), `none` |

The High-Speed SCSI Card needs its ROM, which EmuBuddy does not ship: `341-0803.bin` or MAME's `a2hsscsi.zip` next to
the IIgs ROM file, or `-hsscsirom <file>`. Without it, slot 7 keeps the Hard Disk Card. With this card a hard disk has
to be in its drive at startup; a CD (`-cdrom`, or dropped on the window) can go in at any time. The card moves its
data by true DMA; `-g_hsscsi_dma 0` turns that off (pseudo-DMA only, a little slower).

Slots 5 and 6 hold the built-in drives. As on a real IIgs, a card is only
used when its slot is set to "Your Card" in the IIgs Control Panel.

## Machine

| Option | |
|---|---|
| `-rom <file>` | IIgs ROM file (ROM 01 or ROM 03) |
| `-rompath <folder>` | find the ROM file in this folder |
| `-ramsize <n>M` | memory expansion, `0M`–`8M` |
| `-mhz <n>` | `1`, `2.8` (stock), `8`, `16`, `32`, `64`, `128` (ZipGS) or `max` |
| `-nothrottle` | as fast as possible (same as `-mhz max`) |

## Display

| Option | |
|---|---|
| `-window`, `-nowindow` | window or full screen |
| `-resolution <WxH>` | window size, e.g. `1408x1136` |
| `-scale <n>` | window size as a multiple of the IIgs screen (made smaller if it doesn't fit the screen) |
| `-crt` | curved CRT effect |
| `-screen <n>` | open on screen n: 1 is the main screen, 2 the next (the order the system lists them); the Launcher passes it for "Games on the Other Screen" |
| `-nostatus` | no status bar under the picture (the EmuBuddy Launcher passes it when a game's Status bar box is off) |
| `-scanlines <0-100>` | scanline strength |

## Other

| Option | |
|---|---|
| `-title <text>` | window title |
| `-profile <name>` | name of the Launcher profile; the window is titled `EmuBuddy - <name>` |
| `-quitshot <file.png>` | saves a picture of the IIgs screen to this file when EmuBuddy quits (at most 640 pixels wide; a screen of one colour is not saved). The Launcher uses it as the picture of a game that has no cover |
| `-validate` | check that the ROM, disks and folders named here exist, print the result and exit (0 = fine, 1 = something is missing) |
| `-showconfig` | print the settings this command line stands for and exit |
| `-bridge <folder>` | folder IIgs programs may read and write (host bridge) |
| `-appletalk on\|off` | AppleTalk for a IIgs whose slot 1 or 2 is set to AppleTalk (LocalTalk over UDP, see README, AppleTalk); on unless switched off |
| `-webgate <port>\|off` | the web gateway for IIgs browsers: another port than 6502, or off (see README, Internet) |
| `-skin iigs\|system` | accepted, no effect (macOS always uses its menu bar, other systems the drawn one) |
| `-save` | also save these settings to the config file |
| `-cfg <file>` | use another config file |
| `-h`, `-help` | list the options |

All KEGS options (`-<config variable> <value>`, e.g. `-g_limit_speed 0`)
still work and are passed through.

## Example

```
EmuBuddy apple2gs -rom ROM03 -hard1 System6.hdv -flop3 Game.2mg \
         -sl1 imagewriter -sl4 uthernet2 -mhz 16 -scale 2 -share ~/IIgsFiles
```

On macOS the program inside the app bundle is
`EmuBuddy.app/Contents/MacOS/EmuBuddy`.

Exit codes: 0 fine, 1 error while running (or `-validate` found a problem), 2 bad command line.

## Web gateway

With a network card, EmuBuddy starts the web gateway (GSGate, a Python 3 program that is part of EmuBuddy) when the
IIgs goes online. IIgs browsers use it as a proxy at `192.168.64.254`, port `6502`. Its log is
`gsgate-<port>.log` in the application data folder, subfolder `Gateway`. For development:
`EMUBUDDY_GATEWAY=<path to gsgate.py>` runs another copy, `EMUBUDDY_PYTHON=<path>` picks the Python,
`EMUBUDDY_NOGATEWAY=1` keeps it off. After changing the gateway, `tools/gateway/embed.py <folder>` puts the new
version into EmuBuddy.

## PostScript printer (`-sl1 postscript`)

The printer port can act as a PostScript printer. In GS/OS open the **DC Printer**
control panel, pick the printer port and the type **DC.PostScript**, then print.
Each job is converted by Ghostscript (`gs`, found in PATH or the usual install
folders; `EMUBUDDY_GS=<path>` overrides) and the pages appear in the Printer
window like ImageWriter pages. Ghostscript is not bundled.
Save PDF writes a vector PDF (sharp text, a few KB per page) when all pages came from PostScript jobs; ImageWriter pages are saved as bitmaps.

If Ghostscript is missing, EmuBuddy keeps the print job and shows a "Ghostscript Needed" window (Download Page / Try Again / Cancel). Install: macOS `brew install ghostscript`; Windows installer from ghostscript.com; Linux `sudo apt install ghostscript` (or your package manager). `EMUBUDDY_GS=none` pretends it is missing (testing).

## Window size

View > Window Size 1x / 2x / 3x / 4x resizes the window; sizes that don't fit the screen are greyed out. The choice is saved. `-scale n` does the same for one run (and steps down if it doesn't fit). Full Screen is F11.

## Screenshots and video

File menu: **Save Screenshot** (F10, the IIgs picture only, to your Desktop), **Save Window Screenshot**
(Shift-F10, with menu bar and windows) and **Record Video** (Ctrl-F10, MP4 with sound, to your Movies folder;
the status bar shows REC and the time). Recording needs FFmpeg (`brew install ffmpeg`, `winget install ffmpeg`
or `sudo apt install ffmpeg`); if it is missing, a window explains it. Quitting while recording saves the file.

## Keyboard layouts

EmuBuddy passes the character your keyboard makes to the IIgs, so a German (or any other) host keyboard types what its keys show, whatever layout is set in the IIgs Control Panel. Umlauts and other non-ASCII characters depend on the screen: in the desktop (super hi-res) they are typed the Mac way with Option dead keys (for example a-umlaut = Option-u then a), which the IIgs ROM turns into the real Mac Roman character; on the text screens they are sent as the IIgs national codes (German: a-umlaut = `{`, o-umlaut = `|`, u-umlaut = `}`, ss = `~`, A/O/U-umlaut = `[` `\` `]`), which the German character set shows as umlauts. Control and Open-Apple key combinations are unchanged.

## Dropping files

Drop a file from the Finder / Explorer on the window and it is copied into the shared folder; the IIgs sees it after a moment. If the front GS/OS Finder window shows the shared volume or one of its folders, the file goes into exactly that folder (EmuBuddy reads the window's path from the emulated RAM; tested with System 6.0.4). Otherwise it goes to the folder chosen in Disks > Drop Destination..., where "Drop into the front Finder window" can be switched off. ShrinkIt archives (.shk) are files, not disks. Disk images (.po .hdv .2mg .dsk .woz ...) are mounted in a drive instead.

## Menus

On macOS the menus are the macOS menu bar and the file and folder choosers are the system's own. On Windows and Linux
EmuBuddy draws its own dark menu bar and status bar, deliberately different from the white GS/OS menu bar inside the
emulator. The tool windows (Drives, Slots, Printer, Disk Browser, Drop Destination) are drawn by EmuBuddy everywhere.
