# EmuBuddy — Architecture Design Document

## Overview

EmuBuddy is a native macOS frontend for Apple II and Apple IIGS emulation, powered by MAME. It provides a polished, user-friendly experience for launching, configuring, and managing Apple II-family emulation sessions — replacing MAME's generic interface with one purpose-built for the Apple II ecosystem.

## Design Principles

1. **Apple II-first**: Every UI decision optimized for Apple II/IIGS workflows, not generic emulation
2. **Progressive disclosure**: Simple by default, powerful when needed
3. **Non-destructive**: Never modify the user's ROM or disk image files
4. **Subprocess-first, libMAME-ready**: Clean abstraction layer so MAME integration can evolve

---

## System Architecture

```
┌─────────────────────────────────────────────────────┐
│                    EmuBuddy.app                      │
│                                                      │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────┐ │
│  │   Library     │  │  Machine     │  │  Emulation │ │
│  │   Manager     │  │  Configurator│  │  Session   │ │
│  │              │  │              │  │  Manager   │ │
│  └──────┬───────┘  └──────┬───────┘  └──────┬─────┘ │
│         │                 │                  │       │
│  ┌──────┴─────────────────┴──────────────────┴─────┐ │
│  │              Core Services Layer                 │ │
│  │                                                  │ │
│  │  ┌────────────┐ ┌──────────┐ ┌───────────────┐  │ │
│  │  │ MAME       │ │ Config   │ │ Media         │  │ │
│  │  │ Bridge     │ │ Store    │ │ Scanner       │  │ │
│  │  └────────────┘ └──────────┘ └───────────────┘  │ │
│  │  ┌────────────┐ ┌──────────┐ ┌───────────────┐  │ │
│  │  │ Save State │ │ Metadata │ │ Input         │  │ │
│  │  │ Manager    │ │ Provider │ │ Manager       │  │ │
│  │  └────────────┘ └──────────┘ └───────────────┘  │ │
│  └──────────────────────┬──────────────────────────┘ │
│                         │                            │
│  ┌──────────────────────┴──────────────────────────┐ │
│  │           MAME Abstraction Layer                 │ │
│  │  ┌──────────────────┐  ┌──────────────────────┐ │ │
│  │  │ SubprocessEngine │  │ LibMAMEEngine (v2)   │ │ │
│  │  │ (Phase 1)        │  │ (Future)             │ │ │
│  │  └──────────────────┘  └──────────────────────┘ │ │
│  └─────────────────────────────────────────────────┘ │
└──────────────────────────┬──────────────────────────┘
                           │
                    ┌──────┴──────┐
                    │    MAME     │
                    │  (external) │
                    └─────────────┘
```

---

## Module Breakdown

### 1. MAME Abstraction Layer (`MAMEEngine`)

The critical design decision: a protocol-based abstraction that allows swapping between subprocess and embedded MAME.

```swift
protocol MAMEEngine {
    func launch(machine: MachineProfile, media: [MediaSlot: URL], config: MAMEConfig) async throws -> EmulationSession
    func terminate(session: EmulationSession) async
    func saveState(session: EmulationSession, slot: Int) async throws -> SaveState
    func loadState(session: EmulationSession, state: SaveState) async throws
    func swapMedia(session: EmulationSession, slot: MediaSlot, image: URL?) async throws
    func sendInput(session: EmulationSession, input: EmulatorInput) async
    var status: AsyncStream<EngineStatus> { get }
}
```

**Phase 1: `SubprocessMAMEEngine`**
- Launches MAME as a child process via `Process`
- Builds command-line arguments from `MachineProfile` + media + config
- Captures stdout/stderr for status and error reporting
- Manages the process lifecycle (launch, pause, resume, terminate)
- Communicates media swaps via MAME's built-in IPC (socket or named pipe)

**Phase 2: `LibMAMEEngine`** (future)
- Links against libMAME built as a dynamic library
- Direct API calls for video frame capture, audio routing, input injection
- Enables Metal shader pipeline for display post-processing
- Required for deep features like audio visualization

### 2. Machine Configurator (`MachineProfile`)

Represents a complete Apple II machine configuration.

```swift
struct MachineProfile: Codable, Identifiable {
    let id: UUID
    var name: String
    var machineType: MachineType          // apple2, apple2p, apple2e, apple2ee, apple2c, apple2gs
    var romVersion: ROMVersion?           // ROM 01, ROM 3 (IIGS only)
    var ramSize: RAMSize                  // 48K, 64K, 128K, 1MB–8MB
    var cpuSpeed: CPUSpeed                // normal, fast, warp
    var slots: [SlotNumber: SlotCard]     // slot 0–7 assignments
    var displaySettings: DisplaySettings
    var inputMapping: InputMapping
}

enum MachineType: String, Codable, CaseIterable {
    case apple2      = "apple2"
    case apple2Plus  = "apple2p"
    case apple2e     = "apple2e"
    case apple2eEnhanced = "apple2ee"
    case apple2c     = "apple2c"
    case apple2gs    = "apple2gs"
}

enum SlotCard: String, Codable, CaseIterable {
    case diskII          // Disk II controller
    case smartPort       // SmartPort / UniDisk 3.5
    case mockingboard    // Mockingboard A/C
    case superSerial     // Super Serial Card
    case parallel        // Parallel Printer Card
    case z80             // Z-80 SoftCard (CP/M)
    case mouse           // Apple Mouse Card
    case ramExpansion    // RAM expansion
    case empty
}
```

**Preset Profiles** ship with the app:
- Apple II+ (48K, Disk II in slot 6)
- Apple IIe Enhanced (128K, Disk II in slot 6, 80-col in slot 3)
- Apple IIc (fixed config)
- Apple IIGS ROM 01 (1MB RAM)
- Apple IIGS ROM 3 (4MB RAM, Mockingboard)

### 3. Library Manager

Manages the user's collection of disk images, ROMs, and metadata.

```swift
struct LibraryItem: Identifiable {
    let id: UUID
    var url: URL
    var mediaType: MediaType              // .dsk, .woz, .2mg, .po, .do, .nib, .hdv
    var title: String
    var publisher: String?
    var year: Int?
    var genre: Genre?
    var machineCompatibility: Set<MachineType>
    var copyProtected: Bool
    var bootSystem: BootSystem?           // ProDOS 8, GS/OS, DOS 3.3, Pascal
    var artworkURL: URL?
    var lastPlayed: Date?
    var playCount: Int
    var tags: Set<String>
}
```

**Key features:**
- Watches designated folders for new disk images (FSEvents)
- Identifies media format and extracts metadata from headers (2MG, WOZ)
- WOZ 2.0 metadata parsing (copy protection flags, required hardware)
- Integration point for ProBrowse (inspect disk contents from the library view)
- Artwork scraping from configurable sources
- Smart collections: "Recently Played", "IIGS Only", "Copy Protected", etc.

### 4. Media Scanner (`MediaScanner`)

Responsible for identifying and cataloging disk image files.

```swift
struct MediaScanner {
    func scan(directory: URL) async -> [ScannedMedia]
    func identify(file: URL) -> MediaType?
    func extractMetadata(file: URL) -> MediaMetadata?
}
```

**Supported formats:**
| Extension | Format | Notes |
|-----------|--------|-------|
| .dsk      | Raw sector dump | 140K (5.25") or 800K (3.5") |
| .do       | DOS-ordered sector dump | Same as .dsk with DOS 3.3 ordering |
| .po       | ProDOS-ordered sector dump | Standard for ProDOS volumes |
| .2mg      | 2IMG container | Has header with metadata, can contain any format |
| .woz      | WOZ 1.0/2.0 | Flux-level preservation, copy protection aware |
| .nib      | Nibblized format | Raw nibble data, no headers |
| .hdv      | Hard disk volume | ProDOS or HFS hard disk image |

### 5. Save State Manager

```swift
struct SaveState: Codable, Identifiable {
    let id: UUID
    var sessionId: UUID
    var machineProfile: MachineProfile
    var media: [MediaSlot: URL]
    var slot: Int
    var timestamp: Date
    var thumbnailURL: URL?
    var mameStateFile: URL
}
```

- Wraps MAME's native save state files
- Captures thumbnail screenshot at save time
- Stores full machine config so states are self-describing
- Per-game save slots (MAME supports numbered slots)

### 6. Display Settings

```swift
struct DisplaySettings: Codable {
    var filter: DisplayFilter             // sharp, scanlines, crt, composite
    var aspectRatio: AspectRatio          // pixel1to1, ratio4to3, stretch
    var windowMode: WindowMode            // windowed, fullscreen, fullscreenWindow
    var zoom: Int                         // 1x, 2x, 3x, 4x
    var colorMode: ColorMode              // color, green, amber, white (Apple II only)
}
```

**Phase 1:** Rely on MAME's built-in BGFX shaders, configured via command-line args.
**Phase 2:** Metal shader pipeline for custom CRT simulation, phosphor glow, and SHR-aware scaling.

### 7. Input Manager

```swift
struct InputMapping: Codable {
    var keyboardLayout: KeyboardLayout    // standard, custom
    var openAppleKey: KeyCode             // default: Option
    var closedAppleKey: KeyCode           // default: Command
    var joystickSource: JoystickSource    // keyboard, gamepad, mouse
    var gamepadMapping: GamepadMapping?
    var numpadAsJoystick: Bool
}
```

- Visual keyboard remapping UI
- MFi / game controller discovery via GameController framework
- Open Apple / Closed Apple key assignment (a common pain point)
- Joystick calibration for paddle-based games

---

## Data Flow

### Launching an Emulation Session

```
User selects disk image in Library
        │
        ▼
Library Manager resolves media URL + suggests MachineProfile
        │
        ▼
Machine Configurator builds final config (user can customize)
        │
        ▼
MAMEEngine.launch(machine:media:config:) called
        │
        ▼
SubprocessMAMEEngine builds MAME command line:
  mame apple2gs -rompath /path/to/roms
                -flop1 /path/to/disk.woz
                -sl6 diskiiwoz
                -ramsize 4M
                -speed 1.0
                -window
                -resolution 1120x750
        │
        ▼
Process launched, stdout/stderr monitored
        │
        ▼
EmulationSession object returned to UI
        │
        ▼
Session Manager tracks active session, enables disk swap / save state UI
```

### Disk Swap During Session

```
User drags new disk to virtual drive slot
        │
        ▼
MAMEEngine.swapMedia(session:slot:image:)
        │
        ▼
Phase 1: MAME UI automation (send keystrokes to MAME's internal menu)
Phase 2: libMAME direct API call
        │
        ▼
UI updates to show new disk in slot
```

---

## Persistence

| Data | Storage | Format |
|------|---------|--------|
| Machine profiles | ~/Library/Application Support/EmuBuddy/ | JSON |
| Library database | ~/Library/Application Support/EmuBuddy/ | SQLite (via GRDB or SwiftData) |
| Save states | ~/Library/Application Support/EmuBuddy/SaveStates/ | MAME .sta + metadata JSON |
| Thumbnails | ~/Library/Caches/EmuBuddy/ | PNG |
| MAME config | ~/Library/Application Support/EmuBuddy/mame/ | .ini files |
| User preferences | UserDefaults | Standard |

---

## Technology Stack

| Component | Technology |
|-----------|-----------|
| Language | Swift 5.9+ |
| UI Framework | SwiftUI (macOS 14+) |
| Data | SwiftData or GRDB (SQLite) |
| Async | Swift Concurrency (async/await, AsyncStream) |
| Process management | Foundation.Process |
| File monitoring | FSEvents via DispatchSource |
| Game controller | GameController.framework |
| Graphics (Phase 2) | Metal |
| Networking (metadata) | URLSession |
| Packaging | Xcode, notarized for distribution |

---

## Key Technical Decisions

### Why SwiftUI over AppKit?
SwiftUI provides faster iteration for the library browser, settings panels, and configuration views. The emulator display window may eventually need an NSViewRepresentable wrapper for Metal rendering, but the rest of the app benefits from SwiftUI's declarative model.

### Why subprocess before libMAME?
Building libMAME for macOS is non-trivial (MAME's build system, C++ interop via bridging headers or Swift/C++ interop). Subprocess gets us to a working product fast, and the `MAMEEngine` protocol means we can swap in libMAME later without touching the rest of the app.

### Why SQLite for the library?
Disk image collections can be large (thousands of items). SQLite gives us fast filtering, full-text search on titles, and efficient metadata queries. SwiftData wraps it nicely for SwiftUI integration.

### ROM management strategy
EmuBuddy does **not** bundle or distribute ROMs. On first launch, users point to their ROM directory. The app validates that required ROMs are present for each machine type and provides clear guidance on what's missing.

---

## Integration Points

### ProBrowse
- Embedded as a SwiftUI view or linked as a framework
- Shows directory listing of mounted disk images
- Accessible from the Library detail view ("Browse Contents")

### BitPast
- Screenshot capture → native Apple II format export
- SHR frame capture for IIGS (3200-color aware)
- Accessible from the emulation session toolbar

### ASIMOV Archive
- Optional integration for browsing/downloading disk images
- FTP client to ftp.apple.asimov.net
- Downloaded images automatically added to library
