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
| `-share <folder>` | a Mac/PC folder as a live ProDOS volume |
| `<img or folder>` | goes into the first free drive that fits |

Images: `.po .hdv .2mg .dsk .do .woz .dc .shk .sdk` and more.

## Slots

| Option | Cards |
|---|---|
| `-sl1 <card>` | `imagewriter`, `postscript` (also `laserwriter`), `modem`, `none` (printer port) |
| `-sl2 <card>` | `modem`, `imagewriter`, `postscript`, `none` (modem port) |
| `-sl3 <card>` | `uthernet2`, `voc`, `none` |
| `-sl4 <card>` | `mockingboard`, `uthernet2`, `none` |
| `-sl7 <card>` | `scsi`, `none` (`cffa2`, `hsscsi` also mean `scsi`) |

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
| `-scanlines <0-100>` | scanline strength |

## Other

| Option | |
|---|---|
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

## PostScript printer (`-sl1 postscript`)

The printer port can act as a PostScript printer. In GS/OS open the **DC Printer**
control panel, pick the printer port and the type **DC.PostScript**, then print.
Each job is converted by Ghostscript (`gs`, found in PATH or the usual install
folders; `EMUBUDDY_GS=<path>` overrides) and the pages appear in the Printer
window like ImageWriter pages. Ghostscript is not bundled.
