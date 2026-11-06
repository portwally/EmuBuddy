# EmuBuddy Launcher: plan

A separate app that goes with EmuBuddy. You keep many **profiles** (web apps, own projects, games, ...). Each profile holds the
whole machine set-up: ROM, RAM, speed, every slot, every drive, shared folder, window. Pressing Play starts EmuBuddy with a
MAME-style command line built from the profile. The launcher is a graphical front end with switchable skins.

## 1. Decisions to make first

| Question | Recommendation |
|---|---|
| Where does it live? | Same repository, folder `launcher/`, built by the same CI. One version number, one release, EmuBuddy and Launcher always match. |
| What is it written in? | C and SDL3, like EmuBuddy. The skins need full control of drawing (MacOS 1 title bars, magazine layouts), and it keeps one toolchain on macOS, Windows and Linux. A Qt or web UI would make the skins and the download larger. |
| What does it share with EmuBuddy? | The font and drawing helpers of the UI toolkit (`ui.c`), moved into a small shared folder `common/`. The profile format is plain text, so no code is shared for it. |
| Name | "EmuBuddy Launcher" (app name `EmuBuddy Launcher`, file `emubuddy-launcher`). |

## 2. The profile

A profile is one text file, `Profiles/<name>.emubuddy-profile`, readable and editable by hand, in the same `key = value`
style as `emubuddy.config`.

```
name        = Games
description = Disks and shared folder for games
cover       = cover.png          # optional picture for the skins
rom         = ~/Roms/ROM03
ramsize     = 8M
mhz         = 2.8
sl1         = postscript
sl2         = none
sl3         = uthernet2
sl4         = mockingboard
sl7         = scsi
hard1       = ~/Roms/apple2gs/System6.hdv
hard2       = ~/Roms/apple2gs/D_Board Games.hdv
flop3       =
share       = ~/IIgsGames
scale       = 3
crt         = 1
skin        = IIgs            # EmuBuddy's own skin: IIgs or System
extra       = -g_limit_speed 0
```

- Unknown keys are kept, so newer EmuBuddy options survive old launchers.
- Disks and the shared folder can be per profile, so "web apps" and "games" do not see each other's files.
- Each profile can have its own `-cfg` file (per-profile saved settings such as the IIgs Control Panel battery RAM), kept in
  `Profiles/<name>.data/`. Then the control-panel settings are separate per profile as well.

## 3. How it starts EmuBuddy

The launcher turns the profile into the command line documented in `docs/CLI.md`:

```
EmuBuddy apple2gs -rom ~/Roms/ROM03 -ramsize 8M -mhz 2.8 -sl1 postscript -sl3 uthernet2 -sl4 mockingboard
         -sl7 scsi -hard1 System6.hdv -hard2 "D_Board Games.hdv" -share ~/IIgsGames -scale 3 -crt
         -cfg "Profiles/Games.data/emubuddy.config"
```

- It is started as a child process; the launcher shows "Running" and can bring the window to the front or quit it.
- Quoting and paths are built per platform (macOS app bundle path, `EmuBuddy.exe`, Linux binary), found automatically or set
  once in Settings.
- **Copy command line** shows the exact line (for scripts and shortcuts), and **Create shortcut** writes a Dock/desktop/menu
  entry that starts a profile directly.

### What EmuBuddy needs for this (small changes on the emulator side)

1. Options that are missing from the CLI today: `-sl5` is not needed, but add `-bridge <folder>`, `-skin iigs|system`,
   `-keyboard <layout>`, `-title <text>` (window title, so each profile is recognisable in the Dock and the Window menu) and
   `-profile <name>` (shown in the About dialog and the status bar).
2. `-showconfig`: prints the effective settings as a command line, so the launcher and users can check what was used.
3. `-validate`: checks paths and cards and exits with a code and a message, so the launcher can show "ROM not found" instead of
   a window that closes.
4. A defined exit code and a one-line message on failure, and the launcher reads EmuBuddy's output to show errors.
5. Single-instance behaviour: launching a second profile while one is running asks "Quit the running one?" (the app can keep
   running in the menu bar today, so the launcher must handle this).

## 4. The launcher's screens

1. **Library**: all profiles as cards or a list, with cover, name, description, last played and play time. Search and sort.
2. **Profile editor**: sections Machine, Slots, Drives, Shared folder, Display, Advanced (extra options). The slots page
   mirrors EmuBuddy's own Slots window: pick a card per slot, see the notes (for example "needs IIgs Control Panel: Your Card").
3. **Disk picker**: drag disks and folders on a drive; the launcher reads the ProDOS name of an image (the `prodos_img` reader
   is plain C and can be shared) and shows it next to the file name.
4. **Settings**: path to EmuBuddy, default ROM folder, default profile, skin, language, "close the launcher after starting".
5. **Run page**: current profile, EmuBuddy's output, Quit/Reset buttons.
6. **First run**: finds EmuBuddy and a ROM, and creates three starter profiles (Web apps, Projects, Games) from templates.

Templates come with the launcher: "Internet" (Uthernet II + ZipGS, for web apps), "Development" (ImageWriter/PostScript printer,
shared folder, extra RAM), "Games" (Mockingboard, stock speed, scanlines).

## 5. Skins

A skin is a folder with a small description file, a font, colours and optionally pictures. The launcher draws all screens through
one set of widgets, so a new skin needs no code change. Skins that change only colours, fonts and pictures are easy; skins
that change the layout (Magazine, Shelf) need a layout script and come later.

| Skin | Idea |
|---|---|
| **Modern** | Dark, flat, rounded cards, large covers, soft shadows. Default. |
| **Retro** | The GS/OS look EmuBuddy already has: white windows, Shaston font, blue desktop. |
| **Magazine** | Cream paper, big headlines, columns, covers like a 1980s computer magazine page. Profiles are "articles". |
| **MacOS 1** | 1984 System 1: black and white, dithered desktop, thin title bars, Chicago-like font. |
| **MacOS 7** | System 7 Platinum: grey bevels, striped title bars, icons on a desktop, Charcoal-like font. |

My suggestions for more:

- **Shelf**: profiles as box art on a shelf (like a game library), with a spine view.
- **Cover Flow**: a row of covers you flip through.
- **Terminal / Amber**: green or amber phosphor, keyboard only, like an Apple II prompt (`]RUN GAMES`).
- **Apple II (1977)**: colour-bar logo, 40 columns, block characters.
- **NeXT / BeOS**: for the colour schemes alone, cheap to make once the engine exists.

Rule for all of them: skins never change what the app does, only how it looks. Keyboard control works in all skins.
The skin picker shows a live preview.

## 6. Build plan

**Phase 0: groundwork (about 1 week)**
- Decide name and folder, add `launcher/` to CMake and CI (macOS arm64, Windows, Linux x64/arm64, same as EmuBuddy).
- Move the font and drawing code of `ui.c` into `common/`, EmuBuddy keeps working unchanged.
- Add the missing EmuBuddy CLI options (section 3) with tests; update `docs/CLI.md`.

**Phase 1: profiles and launching (about 2 weeks)**
- Profile file reader and writer, the command-line builder, a test that builds the lines from sample profiles.
- Window with a plain list, an editor for all slots and drives, Play button, running state, error display.
- Profile storage in the user's application-data folder; Import/Export of a single profile file.

**Phase 2: the real front end (about 2 weeks)**
- Library with covers, search, templates, first-run flow, shortcuts, last played and play time.
- Skin engine with the Modern and Retro skins, live preview.

**Phase 3: more skins (about 1 week each)**
- MacOS 7, MacOS 1, Magazine, then the suggestions you pick.

**Phase 4: polish and release**
- App icon, About box, README section and screenshots, a combined download (EmuBuddy + Launcher), update check, translations
  (German first).

## 7. Decisions (from you)

1. **One download** with EmuBuddy and the Launcher together (the release also keeps the plain EmuBuddy build).
2. **The launcher also manages the library**: ROM files and disk images, not only profiles (section 8).
3. **Covers: your own pictures first.** A profile without one gets a generated cover (the skin's colours, the profile name and a
   standard system icon such as a disk, a globe for web apps, a wrench for projects, a joystick for games). No downloads.
4. **All skins**, including my suggestions: Modern, Retro, Magazine, MacOS 1, MacOS 7, Shelf, Cover Flow, Terminal/Amber,
   Apple II (1977), NeXT, BeOS. Layout skins (Magazine, Shelf, Cover Flow) share one layout engine.

## 8. Library of ROMs and disks

- **Library folders:** you name one or more folders (ROMs, disk images, shared folders). The launcher scans them and keeps an
  index; nothing is copied or moved.
- **ROMs:** recognised by size and checksum (ROM 01, ROM 03, known dumps), shown as "ROM 03 (256 KB)". A profile picks a ROM
  from the list instead of a path.
- **Disks:** each image is shown with file name, ProDOS volume name, size, type (800 KB, hard disk, 5.25") and where it is used
  (which profiles). Drag from the library onto a drive in the profile editor.
- **Checks:** missing files, duplicates, images that no profile uses, images that are write-protected or damaged.
- **Safe copies:** "Duplicate for this profile" makes a copy of a disk so a profile can change it without touching the original.
- **Search and tags** across profiles, ROMs and disks.

## 9. Updated build plan

Phases 0 to 2 are as in section 6. Phase 2 now also includes the library (section 8) and the generated covers. Phase 3 builds
all skins in this order: MacOS 7, Magazine, MacOS 1, Terminal/Amber, Shelf, Cover Flow, Apple II (1977), NeXT, BeOS. Phase 4 adds
the combined download (one installer or archive per platform with both apps).
