# Second Sight VGA card: plan

Second Sight (Sequential Systems, about 1993, designed by Jawaid Bazyar) is a VGA card for the Apple IIgs and IIe. It has
its own CPU (Zilog Z180), a VGA chip (Oak OTI-087), 128 KB RAM, a 128 KB firmware EPROM, and 1 MB of video memory on
the IIgs. It drives a VGA monitor. While it is in "Apple II mode" it copies the IIgs's video memory writes and shows the
normal IIgs screen on the VGA monitor. In its own VGA modes it shows a picture of up to 1024×768.

The IIgs does not see the card's video memory. It sends **commands** through four registers in slot 3's I/O space
(`$C0B0–$C0BF`), and moves bulk data byte by byte through two of them.

This plan adds Second Sight to EmuBuddy as a slot-3 card, next to the Video Overlay Card (VOC) and Uthernet II, which
also use `$C0B0–$C0BF`. Only one of them can be in slot 3.

Note on the name: Second Sight is not a video *overlay* card. Apple's Video Overlay Card is a different card. EmuBuddy
emulates part of it (`voc.c`, interlaced Super Hi-Res only).

## 1. Decisions

| Question | Decision |
|---|---|
| Emulate the Z180, or the commands? | **The commands** (high-level emulation). The firmware runs on the Z180 and answers the IIgs's commands; EmuBuddy answers the same commands itself in C. No Z180 core and no card ROM are needed. Programs that upload their own Z180 code and start it ("Run Code", command `$0A`) will not work. Z180 emulation can be added later if such programs turn up (see 5). |
| Card ROM | Not needed and not shipped. If text modes need the card's fonts, see 5. |
| Where does the information come from? | The Sequential Systems documents (developer kit: low-level commands, VGA library, firmware, VGA registers), and standard VGA register documents for the OTI-087's IBM-compatible registers. |
| GSSquared | Jawaid Bazyar's GSSquared emulator has a Second Sight emulation. Its repository has **no license**, so none of its code may be copied into EmuBuddy (GPL-3). It is used only as a black box: run the same program in GSSquared and in EmuBuddy and compare the pictures. Asking Jawaid for permission to use his code, or for his documents, is an option and is the user's call. |
| GSSquared's own modes | GSSquared adds modes the real card never had (PPU, GPU, Host Text, GPU Text: SetMode emulation flag `$02–$05`). They are left out. EmuBuddy emulates the card as it was sold. |
| Slot | Slot 3. Slots window, slot 3: **Empty / Video Overlay Card / Second Sight / Uthernet II**. CLI `-sl3 secondsight`. Default stays Empty. |
| Display | When the card is in a VGA mode and its screen is on, EmuBuddy's window shows the card's picture instead of the IIgs screen, scaled to fit, like a monitor switched to the VGA input. In Apple II mode the window shows the normal IIgs screen. A key or menu item to show the IIgs screen while a VGA mode is on (a real user had two monitors) is a later option. |

## 2. What exists and what is missing

| Part | State today |
|---|---|
| Slot 3 I/O `$C0B0–$C0BF` | Exists: `moremem.c` hands reads and writes to `voc_devsel_read`/`voc_devsel_write` (`voc.c`). |
| Second Sight detection | `voc.c` returns `$00` at `$C0B8` with a comment that Second Sight returns `$01` there (jpeGS checks it). |
| Slot 3 choice | Slots window (`ui_slots.c`: Empty / VOC / Uthernet II), CLI (`cli.c`: `-sl3 voc|uthernet2|none`), config key `g_voc_enable`. |
| Window picture | One fixed 704×482 ARGB texture (`sdl_driver.c`), 640×400 picture area plus borders. |
| Command engine, VGA memory, VGA modes, VGA picture, palette, text modes | Missing. |

## 3. Facts about the card (confirmed in phase 0)

Sources: Sequential Systems' *Low-Level Command Interface & Firmware Documentation* (10 Aug 1995), *Programmer's
Library Documentation* (14 Jul 1995), the user manual, and Sequential's own library source `vgalow.asm` (Jawaid
Bazyar, 1994; fixed 26 Aug 1996) with `modes.h` and `SS.Modes`. Where these are stored: see phase 0 below.

### Registers

Fixed addresses in bank `$E0`, used by both Sequential's library and Brutal Deluxe's `VGA.Lib.s`:

| Address | Name | Use |
|---|---|---|
| `$C0B0` | WR_CMD | Write the command byte. |
| `$C0B1` | WR_DATA | Write argument bytes and upload data. |
| `$C0B2` | RD_DATA | Read returned bytes (GetStatus, GetVGAReg). |
| `$C0B8` | HANDSHAKE | Status the IIgs polls. |

The card answers at `$C0B0` whatever slot it is in: it uses the IIgs's `$C0Bx` select signal (M2B0, pin 35). On a
ROM 01 only slot 3 carries that signal; on a ROM 03 slots 1–6 do. So in EmuBuddy the card is tied to `$C0B0–$C0BF`, not
to a slot's own I/O range, and it shares that range with the VOC and with Uthernet II in slot 3. The manual says the
VOC and Second Sight work together in one machine, though both use M2B0; EmuBuddy still offers one at a time.

### Protocol (from `vgalow.asm`)

1. Write the command byte to `$C0B0`.
2. Wait until HANDSHAKE reads `$01` (card ready).
3. Write the argument bytes to `$C0B1`, low byte first for addresses and lengths.
4. Wait until HANDSHAKE reads `$00` (card has taken the arguments).
5. Long commands (SetMode, Clear Screen, Scroll Screen, ...): wait until HANDSHAKE reads `$A5` (done) or `$A6` (error).

Commands without arguments (Screen On, Screen Off) need no handshake. GetStatus: after `$01`, one dummy read of
`$C0B2`, then the status record byte by byte from `$C0B2`. Upload: after the arguments, for each 64 KB bank of data the
IIgs waits for `$01` and then writes the data bytes to `$C0B1` itself; the card does not read IIgs memory. The library
gives up on GetStatus after 65,535 polls without `$01` (that is how "no card" is detected).

### Status record (GetStatus)

| Offset | Value |
|---|---|
| `$00–$04` | `'G' 'S' 'V' 'G' 'A'` |
| `$05` | number of bytes that follow |
| `$06` | firmware version, `$11` = 1.1 |
| `$07` | 0 = emulating Apple II video, not 0 = not |
| `$08` | last mode set |
| `$09` | video memory: 0 = 512 KB, 1 = 1 MB |
| `$0A` | monitor: `$10` = VGA, `$00` = AppleColor RGB |

### Commands

| # | Command | Bytes (with command) | Arguments |
|---|---|---|---|
| `$00` | GetStatus | 1 | returns the status record |
| `$01` | SetMode | 3 | mode, emulation flag (0 = emulate Apple II video, 1 = don't) |
| `$02` | Upload Code/Data | 11 | flag (0 = Z180 memory, 1 = VGA memory), destination (3), length (3), IIgs source (3) |
| `$03` | Scroll Screen | 10 | source offset (3), destination (3), length (3): a copy within VGA memory, low 512 KB only |
| `$04` | Screen Off | 1 | |
| `$05` | Screen On | 1 | |
| `$06` | SetPalette | 4 | IIgs address of 256 × R,G,B (8 bits each) |
| `$07` | SetPaletteEntry | 5 | entry, R, G, B |
| `$08` | SetBorder | 2 | palette entry |
| `$09` | Run Code | 6 | CBR, CBAR, BBR, Z180 address (2) |
| `$0A` | Clear Screen | 8 | colour, address (3), length (3), low 512 KB only |
| `$0B` | SetShadow | 2 | 0 = shadow IIgs video writes, 4 = don't |
| `$0C` | SetVGAReg | 7 | index port (2), index, data port (2), value |
| `$0D` | GetVGAReg | 6 | index port (2), index, data port (2); returns the value |
| `$0E` | SetUserMode | 1 + 84 | an 84-byte register table (`vga_mode_rec`) |
| `$0F` | SetTextFont | 4 | 0 = ROM font, 1 = alternate ROM font, 2 = PC ANSI font, 3 = user font at Z180 `$F000`; not implemented in ROM 1.1 |

Upload Bitmap is documented but "not currently implemented" and has no command number of its own. Uploads cannot cross
the 512 KB boundary of video memory (the card switches 512 KB halves per transfer). The palette has full 8-bit R, G, B
(the DAC is a Brooktree Bt481), not the 6 bits of a PC VGA card.

### Video modes

Officially supported (`SS.Modes`, 1995). Some tables live only in the library and reach the card through SetUserMode:

| Mode | Picture | Monitor |
|---|---|---|
| `$01` | text 40×25 | VGA, RGB |
| `$03` | text 80×25 | VGA, RGB |
| `$13` | 320×200, 256 colours | VGA, RGB |
| `$53` | 640×480, 256 colours | VGA |
| `$5C` | 640×480, 32K colours | VGA |
| `$61` | 640×400, 256 colours | VGA, RGB |
| `$70` | 320×200, 32K colours | VGA |
| `$5B` | 640×400, 32K colours | RGB (library table) |
| `$FA`–`$FE` | Apple II modes: double hi-res 560×192, hi-res 280×192, text 40×24 and 80×24, Super Hi-Res 640×200 | both |

The library documentation also lists 800×600, 1024×768, 132-column text and 24-bit modes (`$5F`, `$59`, `$60`, `$50`,
`$51`, `$4F`), but says the list is not a promise. They come after the official modes, and only if a program uses them.
32K colours means 5-5-5 (or 5-6-5) bits per pixel.

## 4. Phases

### Phase 0: documents, test programs, reference (small) — done 2026-11-27

Everything found is kept outside the repository in `/Applications/Emulatoren/Apple IIgs disks/Second Sight/`
(not ours to publish):

- `docs/`: firmware command reference, programmer's library documentation, user manual, VGA register book, Oak OTI-087
  data book, schematic (PDFs from apple2online.com and Corbatech, all scans), and OCR text of the first three.
- `sources/`: Sequential's developer package `ss.source.082395` (library `vgalow.asm`, `vgalib.c`, `modes.h`,
  `SS.Modes`, firmware fonts and shadowing algorithms, test programs; **no Z180 firmware source**), the 1996 library
  fix (`sslib082696`), and Brutal Deluxe's `VGA.Lib.s`.
- `software/`: Cogito 2 (*The Return of Cogito*, Brutal Deluxe 1998), Third View, GView 0.25c, SView 1.0a5, ViewDL 2.0,
  the developer package disk, Spectrum 2.5.3 (all from the Asimov archive).
- `reference/`: how GSSquared was run, and its pictures.

Results:

- Section 3 is now confirmed from Sequential's documents and library source. The command numbers of the first draft
  were right.
- **Cogito 2** is the first test program. Without the card EmuBuddy shows "SecondSight not detected! Press a key to
  quit." In GSSquared it sets mode `$61` (640×400, 256 colours) with emulation off, uploads its picture line by line
  (640-byte uploads into VGA memory) and fades the palette in. So it covers detection, SetMode, Upload, SetPalette and
  SetTextFont, and the 256-colour picture.
- **GSSquared reference**: built from source (commit `e569985`) in the session's scratch folder, with a local
  screenshot hook (`reference/gssquared-e569985-local-shot.diff`, never part of EmuBuddy) and run without a window
  (`SDL_VIDEO_DRIVER=dummy`, software renderer, `reference/gsqmac.py`). The card's emulation logs each command, which
  is a useful second check.
- Not found: **jpeGS** and **Video640.Demo** (not on Asimov).
- Still missing per mode group: **text** (Spectrum 2.5.3 detects the card; to be tried in phase 4), **32K colours** and
  **24-bit**. The image viewers can show such images but need an image file and mouse use. Plan: write a small test
  program ourselves in phase 2/3 (Merlin 32 or gsasm, booted as `System/Start` like Cogito) that sets each mode and
  draws a known pattern, so every mode can be checked headless in both emulators.
- Test disk recipe: a copy of a clean GS/OS hard disk with the program put in place of `System/Start` (cadius
  `EXTRACTFILE` / `ADDFILE`, resource fork included); the IIgs then starts straight into the program.

### Phase 1: the card in slot 3 (small) — done 2026-11-27

- `secondsight.c`: config key `g_secondsight` (0/1, "Second Sight VGA card" in the config file). When it is on,
  `moremem.c` sends `$C0B0–$C0BF` to it instead of the VOC. Reset clears it (`video_reset`).
- The protocol of section 3 is in place already: commands are framed with their argument counts, the handshake runs
  `$01` → `$00` → (`$01`/`$00` per 64 KB of data) → `$A5`, GetStatus returns `GSVGA` and the status record (firmware
  1.1, 1 MB, VGA monitor), SetMode is remembered, GetVGAReg returns 0, Run Code answers `$A6`. Upload and palette data
  are counted and dropped for now. Arguments are also taken from `$C0B0` (Sequential's library sends some there) and
  data from `$C0B2` (16-bit stores write `$C0B1` and `$C0B2`). A value that should follow another one takes over after
  the IIgs has read the first one twice.
- `EMUBUDDY_SSLOG=1` logs every command with its arguments.
- Slot 3 choice: Slots window "Video / Net": Empty / Video Overlay / Second Sight / Uthernet II; CLI
  `-sl3 secondsight` (also `ss`). Instead of one new key (`g_slot3_card`) the three old keys stay, and every place that
  sets one clears the others.
- Result: Cogito 2 no longer says "SecondSight not detected!". It runs its whole start: GetStatus, two SetVGARegs,
  ClearScreen, SetPalette, SetMode `$61` with emulation off, 400 uploads of 640 bytes, and the palette fade
  (SetPalette over and over), without hanging. The window still shows the IIgs screen; the picture comes in phase 3.

### Phase 2: the command engine (medium) — done 2026-11-27

- The card's memory: 1 MB video memory and 128 KB Z180 RAM (allocated on the first command), the palette (256 × R,G,B
  with 8 bits each), border colour, SetShadow and SetTextFont values, and the VGA registers: miscellaneous output,
  feature control, sequencer, CRTC, graphics controller, attribute controller and the Oak extended registers
  (`$3DE/$3DF`).
- Every command does its work: Upload into video memory (staying inside the 512 KB half it starts in, as the card
  does) or into Z180 RAM; SetPalette (768 data bytes) and SetPaletteEntry; SetBorder; ClearScreen and ScrollScreen in
  the low 512 KB (ScrollScreen copies forwards, as the Z180's DMA would); SetVGAReg and GetVGAReg by index/data port
  pair; SetUserMode fills the registers from the 84-byte `vga_mode_rec` of `vga.h` (the 20 Oak registers at `$02–$06`,
  `$13–$15`, `$17`, `$19`, `$20–$22`, `$25`, `$30–$35`) and sets mode `$FF`. A dirty flag (`g_ss_dirty`) is set on every
  change for phase 3.
- Test hook: `EMUBUDDY_DUMPRAM=frame:prefix` now also writes `prefix.ssvram`, `.sssram` and `.ssstate` (palette, mode,
  flags, registers). `reference/ssrender.py` draws the video memory with the palette.
- Result: Cogito 2's video memory at frame 3000, drawn offline at 640×400 with its palette, is the same picture GSSquared
  shows (`reference/phase2-cogito2-vram-vs-gssquared.png`). No SetVGAReg went to an unknown port.

### Phase 3: VGA modes and the picture (large; the main part) — done 2026-11-27

- `secondsight_picture()` (core) turns video memory into an ARGB picture when the card shows a VGA mode of its own
  (emulation off, a known mode); only when something changed (`g_ss_dirty`). Screen Off gives a black picture.
- Modes: the numbered ones from a table — 8 bit `$13`, `$53`, `$61`, `$54`, `$56`, `$59`; 32K `$5A`, `$5B`, `$5C`,
  `$70`, `$60`; 24 bit `$5F`. Mode `$FF` (SetUserMode) is worked out from the registers: all the library's tables are
  32K colours, so 16 bits per pixel, bytes per line = CRTC `$13` × 16, height = vertical display end (CRTC `$12` and
  the overflow bits of `$07`) divided by the scan doubling of CRTC `$09`. That gives 640×480 for `$5C` and 320×200 for
  `$70`. The start address is CRTC `$0C/$0D` × 4 (not checked on a real card; every test program uses 0).
- Pixel formats: 8 bit through the palette; 32K is 5-5-5, red in bits 14–10, low byte first (confirmed by Sequential's
  viewer, `a3200.c`); 24 bit is taken as B, G, R as on PC cards — **not confirmed**, no known program shows 24-bit
  pictures (Sequential's viewer turns them into 32K).
- `sdl_driver.c`: `sdl_render_secondsight()` runs at the start of `sdl_render_framebuffer()`, so the plain path, the
  CRT effect, screenshots, recordings and the headless snapshots all get the card's picture. It fills the picture area
  with the border colour and draws the picture at 4:3 (every VGA mode fills a 4:3 monitor), with linear scaling. Its
  own texture is rebuilt when the size changes.
- Test program `SSTEST.SYSTEM` (ProDOS 8, built by `reference/sstest/mktest.py`, 17.7 KB): the library's 640×480×32K
  user table + SetMode `$FF`, then `$61` with SetPaletteEntry, then `$5F`, then back to emulation with SetMode `$60`;
  colour bars and a grey band in each. All four are right in EmuBuddy (`reference/sstest/emubuddy-phase3.png`).
- Cogito 2 shows its title picture in the window after start-up (`reference/phase3-cogito2-emubuddy.png`), as in
  GSSquared.
- GSSquared compared with the test program: the 32K user mode is the same; for `$5F` it reads R, G, B (red and blue
  swapped compared with EmuBuddy), and its `$61` colours after SetPaletteEntry look wrong, though the arguments follow
  Sequential's documentation and library (entry, R, G, B). EmuBuddy keeps to the documentation.

### Phase 4: text modes (medium) — done 2026-11-27

- Modes `$01` (40×25) and `$03` (80×25), emulation off: character and attribute bytes interleaved in video memory
  from the CRTC start address (`$0C/$0D` × 2), 25 rows of 40 or 80 cells. Drawn as 8×16 cells into a 640×400 picture
  (40 columns twice as wide), shown at 4:3 like the graphics modes.
- Attribute byte as on a PC VGA card: bits 3–0 foreground, 6–4 background, 7 blink (characters blink every 32
  frames). Colours are palette entries 0–15; reset loads the standard 16 VGA colours there. Sequential's documentation
  says programs must set the palette for text modes, and SetMode does not reload it.
- Cursor: CRTC `$0E/$0F` (cell), `$0A/$0B` (first and last scan line, `$0A` bit 5 hides it), blinking every 16
  frames. SetMode for a numbered mode now sets the start address to 0 and, for text modes, the VGA cursor shape (lines
  13–14), as loading the card's mode table would.
- Fonts (SetTextFont): font 3, the program's own font at Z180 RAM `$F000` (16 bytes a character, bit 7 left, the
  format of the firmware's `cfont.c`), is drawn exactly. The card's built-in fonts 0–2 are in its EPROM and have no
  license; EmuBuddy uses the IIgs character set it already has (`g_font_array`, from KEGS) instead, each of its 8 rows
  drawn twice, characters `$20–$7F` only. So font 2 (PC ANSI) has no line-drawing characters.
- Test: `SSTEST.SYSTEM` now also loads the 16 text colours, shows an 80×25 and a 40×25 screen with all foreground and
  background colours, a blinking word and the cursor at row 2, column 10 (`reference/sstest/emubuddy-phase4-*.png`).
  Right in EmuBuddy.
- Not confirmed: where a real card keeps the text in video memory. EmuBuddy assumes character/attribute pairs from
  offset 0 (the CPU's view of a VGA text buffer). GSSquared draws garbage for the same test, so it is no reference here
  (`reference/sstest/gssquared-text.png`). A real program that uses the text modes (Spectrum 2.5.3) is the next check.

### Phase 5: settings, Launcher and documents (medium) — done 2026-11-27

- Slots window row 3 "Video / Net": Empty / Overlay / Second Sight / Uthernet II ("Video Overlay" did not fit four
  buttons); new picture of the window in the manual (`docs/manual/img/slots.png`, taken headless).
- CLI `-sl3 secondsight` in `docs/CLI.md`; Launcher: `sl3 = secondsight` is a fourth choice for slot 3.
- README (slots table and a paragraph) and the manual (slots table, command-line table, a section "The Second Sight
  VGA card", index entry); `docs/EmuBuddy-Manual.pdf` rebuilt (62 pages).
- Left out: a menu item to show the IIgs screen while the card shows its own picture (real users had two monitors).
  Not needed by any known program.
- Launcher tests (`test_profile`, `test_attract`, `test_jpeg`, `test_lang`, `test_library`) pass.

### Phase 6: Apple II mode details (small, optional)

- On a real card, Apple II mode shows the IIgs screen on a VGA monitor with the card's own scaling (the "emulated"
  modes `$FA–$FE`: 560×192, 280×192, 40×24, 80×24, 640×400). In EmuBuddy the window already shows the IIgs screen, so
  this phase only matters if a program reads those modes back or the card's look differs visibly.

## 5. Risks and open questions

- **Command numbers**: settled in phase 0 (Upload Bitmap has no number of its own).
- **Programs that run Z180 code**: some programs (a sprite engine was mentioned on Apple II Infinitum) upload Z180
  code. These need a Z180 core and the card's firmware ROM, which EmuBuddy would not ship (the user supplies it, as for
  the SCSI card). Not planned; decide once such a program is found.
- **Fonts**: settled for now in phase 4 — the IIgs character set stands in for the card's EPROM fonts; a font
  the program uploads is exact. If a real card ROM ever turns up with a clear license, or the user supplies one, its
  fonts could be read from it.
- **Few programs**: little software used the card, so testing is limited to the programs in phase 0. Brutal Deluxe and
  Jawaid Bazyar may know of more.
- **Speed**: data moves one byte per IIgs access through `$C0B1`, as on the real card. EmuBuddy runs much faster than a
  real IIgs, so this should be fine.
- **GSSquared and permission**: see 1. Asking Jawaid is optional; this plan does not depend on his code.
- **Windows and Linux**: the second texture is plain SDL, but it must be checked on the CI builds.

## 6. Size

| Phase | Size |
|---|---|
| 0 documents and tests | small |
| 1 slot 3 | small |
| 2 command engine | medium |
| 3 VGA modes and picture | large |
| 4 text modes | medium |
| 5 settings and documents | medium |
| 6 Apple II mode details | small, optional |

About the size of the SCSI card work without its CD audio phase. Phase 3 carries most of the risk.

## 7. Sources

- GSSquared, documentation only: `Docs/Cards_SecondSight.md`, `Docs/SecondSight.md`
  (https://github.com/jawaidbazyar2/gssquared).
- apple2online.com, non-Apple hardware documents: Second Sight firmware, VGA manual, VGA registers, programmer's library.
- Corbatech rare documents list: Second Sight developer kit, low-level commands, VGA library
  (https://corbatech.sourceforge.net/appleII/raredoc.htm).
- comp.sys.apple2: the card's feature list (it shadows the IIgs video modes onto the VGA monitor).
- FreeVGA (standard VGA registers).
