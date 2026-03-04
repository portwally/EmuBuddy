# EmuBuddy — Feature Roadmap

## Phase 1: MVP — "It Boots" (Weeks 1–4)

**Goal:** Launch any Apple II or IIGS disk image with one click.

### Core
- [ ] MAME subprocess launcher with basic command-line builder
- [ ] `MAMEEngine` protocol + `SubprocessMAMEEngine` implementation
- [ ] ROM directory setup (first-run wizard validates required ROMs)
- [ ] Basic machine presets: Apple IIe Enhanced, Apple IIGS ROM 3

### Library
- [ ] Folder scanning for disk images (.dsk, .po, .do, .2mg, .woz, .nib, .hdv)
- [ ] Media type identification from file headers
- [ ] List view of scanned items with title, format, size, date
- [ ] Double-click or "Play" button to launch in MAME

### UI
- [ ] Main window with sidebar (Library / Machines / Settings)
- [ ] Library browser (list view)
- [ ] Machine preset selector
- [ ] Basic preferences: MAME binary path, ROM directory, disk image folders
- [ ] Emulation session status bar (running/stopped)

### Infrastructure
- [ ] Xcode project setup (SwiftUI, macOS 14+)
- [ ] App sandbox configuration (file access for ROMs + disk images)
- [ ] Basic error handling (MAME not found, ROM missing, launch failure)

**Exit criteria:** User can point EmuBuddy at a folder of disk images, pick one, and play it in MAME with zero command-line interaction.

---

## Phase 2: "Home Sweet Home" (Weeks 5–8)

**Goal:** A proper library experience with metadata, artwork, and machine configuration.

### Library Enhancements
- [ ] SQLite/SwiftData database for library persistence
- [ ] Grid view with cover art thumbnails
- [ ] Metadata editing (title, publisher, year, genre, tags)
- [ ] Smart collections: Recently Played, Favorites, By Machine Type
- [ ] Search and filter (by title, format, genre, tags)
- [ ] WOZ 2.0 metadata parsing (copy protection flag, required hardware)
- [ ] Boot system detection (DOS 3.3, ProDOS 8, GS/OS, Pascal)

### Machine Configuration
- [ ] Custom machine profile creation and editing
- [ ] Visual slot configurator (drag-and-drop cards into slots 0–7)
- [ ] RAM size configuration (especially IIGS 1MB–8MB)
- [ ] CPU speed control (normal / fast / warp)
- [ ] Profile duplication and management

### Display
- [ ] Display filter presets via MAME BGFX: sharp, scanlines, CRT
- [ ] Aspect ratio options: 1:1 pixel, 4:3, stretch
- [ ] Zoom levels: 1x, 2x, 3x, 4x
- [ ] Fullscreen toggle
- [ ] Monochrome modes: green, amber, white phosphor

### Media Management
- [ ] Dual floppy drive support (Disk 1 / Disk 2 in UI)
- [ ] Hard disk image mounting
- [ ] Recently used disks list per machine profile

**Exit criteria:** User has a visually appealing library they enjoy browsing, can configure machines to their liking, and the display looks great.

---

## Phase 3: "Power User" (Weeks 9–12)

**Goal:** Save states, input customization, disk swapping, and session management.

### Save States
- [ ] Save/load state from toolbar or keyboard shortcut
- [ ] Save state browser with thumbnails and timestamps
- [ ] Per-game save slots (1–10)
- [ ] Auto-save on quit (optional)

### Input
- [ ] Visual keyboard remapping UI
- [ ] Open Apple / Closed Apple key reassignment
- [ ] Game controller (MFi / HID) discovery and mapping
- [ ] Numeric keypad → joystick toggle
- [ ] Joystick/paddle calibration
- [ ] Per-game input profiles

### Disk Swap
- [ ] Hot-swap floppy disk during emulation session
- [ ] Drag-and-drop disk images onto drive slots
- [ ] "Insert Disk" panel accessible via toolbar or hotkey
- [ ] Multi-disk game support (queue of disks with swap prompts)

### Session Management
- [ ] Multiple simultaneous sessions (multiple MAME instances)
- [ ] Session history log
- [ ] Quick resume last session on app launch

**Exit criteria:** User can comfortably play multi-disk games, save progress, and use game controllers.

---

## Phase 4: "Integration" (Weeks 13–16)

**Goal:** Connect EmuBuddy with your existing tools and the Apple II ecosystem.

### ProBrowse Integration
- [ ] Embedded disk browser panel (show directory listing of mounted images)
- [ ] "Browse Contents" button on library items
- [ ] File preview for BASIC programs, text files, binary info

### BitPast Integration
- [ ] Screenshot capture from active session
- [ ] Export screenshot as native Apple II format (HGR, DHGR, SHR)
- [ ] 3200-color SHR awareness for IIGS captures

### ASIMOV Archive
- [ ] Browse ASIMOV FTP archive from within EmuBuddy
- [ ] Download disk images directly to library
- [ ] Auto-catalog downloaded images

### Metadata Enrichment
- [ ] Artwork scraping from configurable sources
- [ ] Bulk metadata import from CSV/JSON
- [ ] Community metadata sharing (export/import library databases)

**Exit criteria:** EmuBuddy feels like the center of the user's Apple II hobby, not just an emulator launcher.

---

## Phase 5: "Polish & Platform" (Weeks 17–20)

**Goal:** macOS-native polish and deep system integration.

### macOS Integration
- [ ] Quick Look plugin for .2mg/.dsk/.woz files (show disk info)
- [ ] Spotlight indexing of library metadata
- [ ] Dock badge / progress indicator for long operations
- [ ] Menu bar integration (quick launch recent sessions)
- [ ] Drag-and-drop disk images onto Dock icon to launch
- [ ] Touch Bar support (if applicable)

### Advanced Display (Metal)
- [ ] Metal shader pipeline for CRT simulation
- [ ] Phosphor glow / bloom effect
- [ ] Composite artifact color simulation
- [ ] SHR-specific scaling for IIGS 320/640 modes

### Audio
- [ ] Mockingboard audio visualization (oscilloscope waveform)
- [ ] Ensoniq ES5503 (IIGS) audio visualization
- [ ] Volume controls per audio source
- [ ] Audio recording / export

### Disk Image Creation
- [ ] Create blank ProDOS volumes from UI
- [ ] Format and assign to a slot
- [ ] Import files to disk images (via ProBrowse)

**Exit criteria:** EmuBuddy is a polished, first-class macOS citizen that feels native and delightful.

---

## Phase 6: "libMAME" (Future)

**Goal:** Deeper MAME integration for advanced features.

- [ ] Build MAME as libMAME shared library for macOS
- [ ] `LibMAMEEngine` conforming to `MAMEEngine` protocol
- [ ] Direct video frame capture (bypass subprocess window management)
- [ ] Direct audio routing (for visualization and recording)
- [ ] Direct input injection (lower latency)
- [ ] Custom OSD (on-screen display) rendered via Metal
- [ ] Embedded MAME debugger UI

---

## Phase 7: "Community" (Future)

**Goal:** Share and connect.

- [ ] iCloud sync for save states and machine profiles
- [ ] Export/import library databases for sharing
- [ ] Plugin architecture for community extensions
- [ ] Potential iOS companion app (Handoff / Continuity)

---

## Priority Matrix

| Feature | Impact | Effort | Priority |
|---------|--------|--------|----------|
| MAME subprocess launch | Critical | Low | P0 |
| Library folder scan | Critical | Low | P0 |
| Machine presets | Critical | Medium | P0 |
| ROM validation wizard | High | Low | P0 |
| Library database + grid view | High | Medium | P1 |
| Visual slot configurator | High | High | P1 |
| Save states | High | Medium | P1 |
| Display filters (BGFX) | Medium | Low | P1 |
| Input remapping | Medium | Medium | P2 |
| Disk swap mid-session | High | High | P2 |
| ProBrowse integration | Medium | Medium | P2 |
| BitPast integration | Medium | Medium | P2 |
| ASIMOV browser | Medium | High | P3 |
| Metal shader pipeline | Medium | Very High | P3 |
| Quick Look plugin | Low | Medium | P3 |
| libMAME integration | High | Very High | P4 |
