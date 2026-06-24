# GSplus Backlog

Planned and possible work for GSplus. Everything below targets the SDL3 build
(the focus — see the README). Items marked **(core)** already exist in the KEGS
core and mainly need wiring into the SDL driver; everything else is new.

Effort: **S** small · **M** medium · **L** large. Priority: High / Med / Low.

---

## Done

- Foundation: SDL3 driver (video/input/audio), CMake build, cross-platform CI,
  self-contained packaging (macOS `.dmg`, Linux `.tar.gz`, Windows `.zip`),
  version-from-tag.
- **F1 — Window & display options**: `-fullscreen`, `-borderless`, `-noaspect`,
  `-highdpi`, `-novsync`, `-nohwaccel`, window position; F11 fullscreen toggle.
- **F2 — Scanline simulator**: `-scanline <0-100>`, Shift+F11 toggle.
- **F3 — Drag-and-drop**: drop a disk image on the window to mount it (slot
  guessed from file size).

---

## Legacy gsplus features (original roadmap)

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **Screenshots** | Med | M | `-ssdir` + hotkey. BMP (no dep) or PNG (adds SDL3_image). |
| **Clipboard** copy/paste | Med | M | Wire SDL clipboard to the core's copy/paste hooks **(core)**. |

## Input

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **Gamepad/controller** support | High | M | SDL3 gamepad → joystick/paddles **(core)**. SDL driver has none yet. |
| **Mouse capture** toggle | Med | S | Grab/release the pointer via a hotkey. |
| Configurable **key remapping** | Low | M | Currently a fixed scancode→ADB table. |

## Display

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **Integer / pixel-perfect scaling** toggle | Med | S | Crisp 1×/2×/3× vs. letterbox stretch. |
| Linear-filter option | Low | S | Smooth-scaling alternative to nearest. |

## Runtime / system

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **Fast-forward / turbo** hotkey | High | S | Hold to uncap speed. |
| **Pause/resume** hotkey | Med | S | |
| **Save states / snapshots** | Med | L | Full IIgs machine state; KEGS has none. Stretch. |

## Audio

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **Volume / mute** hotkey | Med | S | |
| Audio device selection | Low | S | |

## Quality of life

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **Recent disks/ROMs** menu | Med | M | |
| Window title shows **mounted disk** | Low | S | |
| Modern **overlay config menu** (vs. F4 text panel) | Low | L | Big polish, big effort. |

## Packaging / distribution

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **Linux AppImage** (self-contained) | Med | M | The one non-self-contained download today. |
| **Code signing** (Windows/macOS) | Low | M | Needs a cert / Azure Trusted Signing. |
| Local **mingw cross-compile** for fast Windows iteration | Low | S | Catch Win compile/link errors on macOS in seconds. |

---

## Suggested order

Gamepad → Fast-forward → Screenshots → Clipboard.

## Dagen's wishlist

_(personal "selfish feature" ideas — add freely)_
