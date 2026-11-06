# Plan: EmuBuddy, a cross-platform Apple IIgs emulator with a GS/OS-style UI

Goal: **EmuBuddy**, an Apple IIgs emulator for macOS, Windows and Linux, driven
entirely by graphics. It is built on the GSplus/KEGS 1.38 core and its SDL3
front-end. Every window, menu and button is drawn by EmuBuddy itself in one
look: the **white Apple IIgs GS/OS skin** from Wally's Manager. No Apple,
Microsoft or GTK window chrome, and no other skins for now.

## Decisions

- **Name:** EmuBuddy. Bundle ID `com.portwally.EmuBuddy` (macOS).
- **Platforms:** macOS Apple Silicon only (no Intel), Windows x64, Linux x86_64.
  Windows/Linux arm64 can follow later.
- **Menu bar:** drawn by EmuBuddy inside the window, identical on every OS.
- **Acceleration:** yes, see Phase 1 ("Speed and acceleration").
- **Front-end:** the existing **SDL3 driver** (`sdl_driver.c`). The Swift/AppKit
  driver is dropped. It builds today but is Mac-only.
- **UI:** a small widget toolkit written in C, drawn with the SDL renderer on top
  of the emulator picture. One skin: GS/OS white (palette and window style taken
  from `Skin.iigs` in Wally's Manager).
- **Networking:** Uthernet card plus the standard Marinetti TCP/IP stack, through
  libslirp. Uthernet II afterwards. The Sweet16 Link Layer is not supported.
- **Repo:** local only, in `~/Documents/EmuBuddy`. Created as a clone of
  `~/Documents/gsplus` so the KEGS/GSplus history stays. `origin` is renamed
  `gsplus` so later fixes can be merged. Nothing is pushed anywhere.
- **Build:** CMake + Ninja, as GSplus does today.

## Requirements

1. **Graphical UI only.** No F4 text panel, no command-line flags. The emulated
   IIgs still gets normal keyboard and mouse input.
2. **Printer output** (ImageWriter II → preview → print or PDF).
3. **Configurable slots** (which card is in slots 1–7).
4. **Ethernet** for internet through Marinetti.
5. **Drag and drop** between the desktop (Finder/Explorer/file manager) and the
   emulator, both directions.
6. **Easy disk swapping** while the emulator runs.

## What the codebase already has

| Area | Status | Where |
|---|---|---|
| SDL3 front-end | Window, renderer, keyboard, mouse, gamepad, audio, fullscreen, CRT/scanline effects, screenshots, **file drops** (`SDL_EVENT_DROP_FILE`). Builds on macOS, Linux, Windows with CMake. | `sdl_driver.c`, `sdl_snd_driver.c`, `CMakeLists.txt` |
| Config menu | F4 text menu drawn into the emulated screen. **Replaced.** `config.c` stays as the data model and `config.kegs` as the hidden settings file. | `config.c` |
| Disk mount/eject | `insert_disk()`, eject, create new image, slot guess for drops | `config.c`, `iwm.c`, `smartport.c` |
| Mac/PC folder as ProDOS volume | **Read/write.** Guest writes go back to host files. Types via AppleSingle or name suffixes. | `dynapro.c`, `dyna_*.c`, `applesingle.c` |
| Serial ports (slots 1/2) | Real device, virtual modem, TCP | `scc.c`, `scc_socket_driver.c` |
| Mockingboard | Slot 4 | `mockingboard.c` |
| Printer | None | — |
| Ethernet | None | — |
| Slot card selection | None (fixed layout) | `moremem.c` |

The core loop looks single-threaded (events, emulation and drawing in one
loop; only the debugger reads stdin on a thread). **Verify this in Phase 1.** If
true, UI actions can call the core directly between 16 ms emulation slices, with
no command queue.

## How the UI works

- One SDL window. The emulator picture fills it. UI windows, menus and dialogs
  float on top of it, like the desktop in Wally's Manager. The emulator keeps
  running behind them (pausable).
- **Mouse routing:** a click on a UI window goes to the UI, anything else to the
  emulator. The emulator's captured/hidden pointer is released while the pointer
  is over UI. **Keyboard routing:** keys go to the UI only when a UI text field has
  focus or a modal dialog is open.
- **Toolkit widgets (C):** window (striped title bar, close box, zoom box, grow
  box, drag), menu bar with pull-down menus, button, checkbox, radio, popup
  menu, scrolling list, scroll bar, tabs, text field (only where unavoidable:
  names, IP, ports), alert/confirm dialog, progress bar, drive-light and status
  widgets, and an in-skin **file browser** for Open/Save.
- **Skin:** palette from `Skin.iigs`: white windows, black 1 px border,
  horizontal-stripe title bar, grey shadow, black selection, green/orange/red for
  good/warn/bad. Menu bar white with an Apple-style logo (we use our own EmuBuddy
  mark, not Apple's logo).
- **Font:** Shaston 640 (the GS/OS look) rendered with `stb_truetype` (public
  domain) into a glyph atlas. Its Kreative license allows free redistribution
  with the license text and credit, forbids selling it and modifying it. Fine
  for a free app; **a paid release would need another font**.
- **Scaling:** UI scales in whole steps (1×, 2×, 3×) for sharp edges, including
  high-DPI displays.
- **Native pieces we still use:** the OS file-open sheet is not needed (our own
  browser), but Phase 6 needs small native code per OS for dragging files **out**,
  and printing opens the OS viewer/printer.

## Phases

### Phase 0: Repo and foundation (S)
**Status: done except the TODO.md fixes. Repo, rename, F4 text panel replaced by a "ROM Required" window and a Choose ROM File menu item.**
- Clone gsplus → `~/Documents/EmuBuddy`. Rename app, bundle ID, icon, window title.
- Remove the F4 key path, the Swift target and command-line flag parsing. The
  debugger stays reachable from a hidden menu only.
- First-run flow in our own UI: ROM picker (drop a ROM file or browse), size check
  (128 KB ROM 01, 256 KB ROM 03), ROM copied to the per-user data folder
  (`~/Library/Application Support/EmuBuddy`, `%APPDATA%\EmuBuddy`, `~/.local/share/EmuBuddy`).
- Fix the TODO.md release blockers that touch us: lost modifier keys on focus
  loss, window position persistence, drag-drop slot guess for 5.25" WOZ.

### Ethernet spike (M), right after Phase 0
Port `cs8900.c` + `rawnet` from legacy GSplus, add a libslirp backend, run
Marinetti with the Uthernet link layer from your `internet.2mg`. Success = it gets an
IP by DHCP and loads a page. Headless (no UI work needed). This is the riskiest
part, so it goes first.

### Phase 1: UI toolkit and main window (L)
**Status, slice 1 done:** toolkit core (windows, menu bar, status bar, button, checkbox, list, file browser), embedded Shaston font with own TrueType reader, File/Machine/Speed/View/Help menus, pause, reset, speed incl. Unlimited, About. **Still open in Phase 1:** toolbar strip, drive lights in the status bar, paste/copy text, debugger menu, keyboard shortcuts, Open Recent.
- Toolkit core: draw primitives, glyph atlas, event routing, window manager,
  widgets above. Unit-testable layout code kept separate from SDL calls.
- **Menu bar** (File, Machine, Disks, View, Window, Help): Open Disk…, Recent
  Disks, New Blank Disk…, Reset, Restart, Pause, Speed, Fullscreen, Screenshot.
- **Toolbar strip** under the menu: Power/Restart, Pause/Play, Mute, Speed
  (1 MHz / 2.8 MHz / unlimited), Drives, Printer.
- **Status bar:** drive lights per drive, speed %, network light, printer light.
- Paste text into the IIgs, copy screen text.
- Verify the single-thread assumption (see above).

**Speed and acceleration.** The core already has speed modes (`g_limit_speed`:
unlimited, 1.0 MHz, 2.8 MHz, ZipGS) and a ZipGS accelerator emulation
(`g_zip_speed_mhz`: 8, 16, 32, 64, 128 MHz). Today they are only reachable in
the F4 text menu. Work:
- **Speed** popup in the toolbar and Machine menu: 1 MHz · 2.8 MHz (stock IIgs)
  · ZipGS with a second popup for 8/16/32/64/128 MHz · Unlimited.
- **Turbo hold-key**: hold a key to run unlimited, release to go back.
- Live "speed %" readout in the status bar (the core already computes it).
- Speed is saved per disk set, so a game can open at 1 MHz and a compiler at
  128 MHz.
- **Maximum = "Unlimited"**: no fixed cap, the emulator runs as fast as the
  computer allows. Show the real achieved speed in MHz (for example "412 MHz")
  in the status bar, not just a percentage. Phase 1 measures how high this
  goes on an Apple Silicon Mac and checks that the screen, sound and
  disk/network timing stay stable at that speed (sound is muted or
  resampled above 128 MHz). Work on the emulation's hot loop (`engine_c.c`)
  to raise the ceiling is optional later.
- Rendering is already hardware-accelerated through the SDL renderer. The
  emulation itself runs on one CPU core, so top speed depends on the machine.
  Unlimited speed will be measured on a real Windows/Linux/Apple Silicon run.

### Phase 2: Drives window and disk swapping (M)
**Status: done except disk sets.** Drives window (S5 3.5", S6 5.25", S7 = SCSI card with 7 hard drives), click a drive to choose a disk, drop on a row, drop anywhere to auto-place, lock toggle, eject, Eject All, recent disks in the Disks menu, part chooser for multi-partition images, `.hdv` always goes to the hard disk slot. **Open:** disk sets (needs a text field for the name), drag a row out to eject, drive activity lights.
**SCSI note:** slot 7 is the core's SmartPort/ProDOS block controller shown as a 7-drive SCSI card. A real Apple High-Speed SCSI card ROM is not emulated; GS/OS sees seven hard disks either way.
- **Drives** window: one row per drive: S5D1–2 (3.5"), S6D1–2 (5.25"), S7D1–12
  (SmartPort). Icon by type, image name, write-protect badge, eject button.
- **Drop an image onto a row** to insert it there. Drop on the emulator picture
  to auto-place it (size guess + WOZ type sniff). SDL gives drop position events,
  so hovering highlights the target row.
- Eject by button, row context menu, or dragging the image row off the window.
- Recent disks per drive. **Disk sets**: save the current drive layout under a
  name and restore it in one click (replaces Sweet16 "Emupack").
- New Blank Disk dialog (size and type) using the existing image-creation code.

### Phase 3: Slot configuration (M–L, core work)
- **Slots** window with a popup per slot:
  - Slot 1: Printer (ImageWriter II) · Serial · Empty
  - Slot 2: Modem · Serial · Empty
  - Slot 3: Uthernet · Empty (disables VOC, which also uses slot 3)
  - Slot 4: Mockingboard · Empty
  - Slots 5/6/7: built-in 3.5", 5.25" and SmartPort drives, or Empty
- Core: a per-slot card table in `moremem.c` that dispatches `$C0n0–$C0nF` I/O
  and `$Cn00` ROM space, without slowing the hot path.
- Keep the IIgs slot register (`$C02D`) and battery-RAM Control Panel settings in
  step with the choice. Slot changes ask for a restart.

### Phase 4: Printer (S–M)
- Port the ImageWriter II emulation from legacy GSplus/GSport (`imagewriter.cpp`,
  `scc_imagewriter.c`, `printer.cpp`, charmaps; GPL v2+). Attach it to the SCC
  printer port and as a slot 1 card.
- New output layer: render the page to a bitmap in memory, then show it in a
  **Printer window** (page thumbnails, Next/Prev, Clear).
- **Save as PDF…** with a small built-in PDF writer (image per page), and
  **Print…** which saves a temp PDF and hands it to the OS: `open -a Preview` /
  `lp` on macOS, `ShellExecute "print"` on Windows, `lp`/`xdg-open` on Linux.

### Phase 5: Ethernet (M–L)
- Slot 3 **Uthernet (CS8900a)** with a libslirp backend, as proven by the spike.
  libslirp is BSD-licensed and builds on all three platforms (needs glib: bundle
  it, or build static in CI).
- **Network** window: on/off, status, optional port forwards (to run a server on
  the IIgs), packet light in the status bar.
- Then **Uthernet II (W5100)** modelled on AppleWin's `Uthernet2.cpp` (check its
  license before copying). Unverified: whether Marinetti's Uthernet II link
  layer uses MACRAW or the card's hardware sockets, so start with MACRAW on
  socket 0.
- Marinetti's "LAN Slot" must be 3. Cards have no firmware, so nothing
  auto-detects it.

### Phase 6: Files between desktop and emulator (M–L)
1. **Shared Folder drive** (works on every OS, builds on `dynapro.c`): pick a host
   folder, it mounts as a live ProDOS volume in slot 7. Files put in the folder
   appear in GS/OS and the other way round.
2. **Drop in** (all OSes, SDL events): disk images mount, other files are copied
   into the Shared Folder and the volume is re-scanned.
3. **Drag out** to the desktop: SDL has no outbound drag, so this needs a small
   native piece per OS: macOS first (`NSDraggingSource`), then Windows (OLE
   `DoDragDrop`), Linux best effort (XDND on X11, Wayland data-device). Until
   an OS has it, a **Save to Desktop** button in the Disk Browser does the same.
4. **Disk Browser** window: open a ProDOS (later HFS) image, list files, drag out
   or drop in. Writes are **refused while the image is mounted** in a running
   drive, to avoid corruption.

### Phase 7: Polish and packaging (M)
- Settings windows in the same skin: General (ROM, startup disk set, pause when
  in the background), Display (scale, CRT/scanline effects, 3200-colour), Sound,
  Input (gamepad, keyboard, mouse capture key).
- Help window instead of a PDF. About box.
- Packaging: macOS `.dmg` (arm64, signed + notarized), Windows `.zip` with
  runtime DLLs (fix the TODO.md DLL blocker), Linux `.tar.gz` and later AppImage.

## Order and rough size

| # | Phase | Size |
|---|---|---|
| 0 | Repo, rename, remove F4/CLI, ROM first-run | S |
| — | **Ethernet spike** (Marinetti gets an IP) | M |
| 1 | UI toolkit, menu bar, toolbar, status bar | L |
| 2 | Drives window, disk swapping, disk sets | M |
| 3 | Slot configuration | M–L |
| 4 | Printer | S–M |
| 5 | Ethernet: Uthernet + window, then Uthernet II | M–L |
| 6 | Desktop ↔ IIgs files | M–L |
| 7 | Settings, help, packaging | M |

## Code to reuse

GPL v2-or-later sources can be combined with GPL v3 KEGS. Check each file header.

| Feature | Source | Files | Notes |
|---|---|---|---|
| UI look | Wally's Manager | `Skins/Skin.swift` (`Skin.iigs` palette), `Skins/RetroControls.swift` (title bar, close/zoom/grow boxes, bevels) | Re-implemented in C, not copied. |
| ImageWriter II | legacy GSplus (`github.com/digarok/gsplus-legacy-archive`, tag `v0.14`), GSport (`github.com/david-schmidt/gsport`) | `imagewriter.cpp/.h`, `scc_imagewriter.c`, `printer.cpp/.h`, `printer_charmaps.h` | DOSBox-derived. Replace its BMP/PNG/PS/GDI output with our page bitmap. |
| Uthernet (CS8900a) | legacy GSplus | `src/rawnet/cs8900.c/.h`, `rawnet.c/.h`, `rawnetsupp.c/.h`, `rawnetarch.c/.h` | VICE-derived. Drop the pcap/TAP/vmnet backends, add `rawnetarch_slirp.c`. |
| slirp wiring | AppleWin Linux port (`github.com/audetto/AppleWin`) | `source/linux/network/slirp2.cpp` | Reference for libslirp with an emulated card. |
| Uthernet II (W5100) | AppleWin | `source/Uthernet2.cpp` | Reference or copy after license check. |
| libslirp | `gitlab.freedesktop.org/slirp/libslirp` | — | BSD. Depends on glib. |

Why slirp: pcap needs root or BPF permissions, and macOS `vmnet` needs a root
helper or an entitlement Apple doesn't give normal apps (legacy GSplus and Ample
both fell back to a root helper). slirp needs nothing and works on all three OSes.
Sweet16 used it too.

## Risks

- **UI toolkit size.** Building widgets, text and a file browser in C is the biggest
  new piece. Keep the first version small and grow it per phase.
- **Ethernet** has to match Marinetti's link layer exactly, plus the libslirp/glib
  packaging on three OSes.
- **Slot table** touches the hottest part of the core (`moremem.c`). Speed must
  not drop.
- **Mouse/keyboard routing** between UI and emulator (captured pointer, key focus)
  is easy to get subtly wrong.
- **Drag-out** is per-OS native code, and Wayland is the weakest.
- **Disk Browser writes** can corrupt images. Read-only first, writes only to
  unmounted images.
- **Licenses:** the result is GPLv3, so source must be published if EmuBuddy is
  ever shared. Shaston font: free use only, see above. Don't reuse Sweet16 art.

## Open questions

None open. Next step is Phase 0.
