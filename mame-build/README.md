# EmuBuddy MAME Build

Build a minimal MAME binary (~50–80MB instead of ~300MB+) that includes **only** Apple II family drivers with all expansion cards and peripherals.

## What's Included

### Machines (Drivers)

| Driver | Machine | Source |
|--------|---------|--------|
| `apple2` | Apple ][ | `apple/apple2.cpp` |
| `apple2p` | Apple ][+ | `apple/apple2.cpp` |
| `apple2e` | Apple //e | `apple/apple2e.cpp` |
| `apple2ee` | Apple //e Enhanced | `apple/apple2e.cpp` |
| `apple2ep` | Apple //e Platinum | `apple/apple2e.cpp` |
| `apple2c` | Apple //c | `apple/apple2e.cpp` |
| `apple2c0` | Apple //c (UniDisk) | `apple/apple2e.cpp` |
| `apple2c3` | Apple //c (Memory) | `apple/apple2e.cpp` |
| `apple2c4` | Apple //c (Rev 4) | `apple/apple2e.cpp` |
| `apple2cp` | Apple //c Plus | `apple/apple2e.cpp` |
| `apple2gs` | Apple IIGS (ROM 3) | `apple/apple2gs.cpp` |
| `apple2gsr0` | Apple IIGS (ROM 00) | `apple/apple2gs.cpp` |
| `apple2gsr1` | Apple IIGS (ROM 01) | `apple/apple2gs.cpp` |

### Expansion Cards (A2Bus Slot Devices)

**Disk Controllers:** Disk II, Disk II (WOZ), IWM, SuperDrive 3.5", CFFA2000 CompactFlash, SD Card

**Hard Drive/SCSI:** Apple SCSI, High-Speed SCSI, Corvus Hard Drive, CMS SCSI, SmartPort, Sider SASI, ZIP Technologies IDE

**Memory:** RamWorks III, RAMFactor, Slinky, RAMCard (3K/8K/128K/48-16K), Language Card

**Video/Display:** 80-Column Card (Standard + Extended), VideoTerm, UltraTerm, Videx, E-Z Color Graphics, Grafex

**Audio:** Mockingboard A/C, Phasor, Echo II Speech, SAM Speech, Mountain Computer Music System, ALF Music Card II, Noisemaker II

**Serial/Parallel I/O:** Super Serial Card, Parallel Printer, Grappler, MIDI Interface, IEEE-488, CCS 7710, Uniprint

**Coprocessors:** Z-80 SoftCard (CP/M), The Mill (6809), PC Transporter (8088)

**Input:** Mouse Card, 4Play Joystick, Wico Trackball, ComputerEyes Digitizer, Light Pen

**Network/Clock:** ThunderClock Plus, TimeMaster H.O., Uthernet Ethernet

**Other:** Applicard, Arcade Board, ProDOS ROM Drive, ROM Card, SNES MAX, Laser 128 cards

### Game I/O Devices
Analog Joystick, Paddles, Wico Joystick, Light Pen, ComputerEyes Camera

### Keyboards
Standard Apple II/IIe/IIGS keyboards, plus third-party variants

## Prerequisites

Install on macOS:

```bash
# Xcode command line tools
xcode-select --install

# SDL2 (graphics/audio backend)
brew install sdl2

# Python 3 (MAME build system)
brew install python3
```

## Quick Start

```bash
cd ~/Documents/EmuBuddy/mame-build

# Build the standalone binary (first build takes 15–30 min)
./build-emubuddy-mame.sh

# The binary will be at: mame/mameemubuddy
```

## Build Options

```bash
# Standalone binary (default)
./build-emubuddy-mame.sh

# Shared library for embedding (future libMAME integration)
./build-emubuddy-mame.sh --libmame

# Clean build artifacts
./build-emubuddy-mame.sh --clean
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `MAME_DIR` | `./mame` | Path to MAME source checkout |
| `JOBS` | Auto (CPU cores) | Parallel compilation jobs |

## How It Works

MAME's build system has a `SOURCES` filter that takes driver `.cpp` files and automatically resolves all dependencies — bus devices, slot cards, CPUs, sound chips, etc. By specifying just three driver files:

```
apple/apple2.cpp     →  Apple ][ and Apple ][+
apple/apple2e.cpp    →  Apple //e, //c, and all variants
apple/apple2gs.cpp   →  Apple IIGS (all ROM versions)
```

MAME's dependency resolver pulls in everything these machines reference:

- `src/devices/bus/a2bus/*` — All 88+ expansion card implementations
- `src/devices/bus/a2gameio/*` — Joystick, paddles, and game peripherals
- `src/devices/bus/a2kbd/*` — Keyboard interfaces
- `src/devices/bus/applepp/*` — Parallel printer bus
- All required CPUs (6502, 65C02, 65816, Z80, 6809, etc.)
- All required sound chips (AY-3-8910, ES5503, etc.)
- Disk controllers, IWM, SWIM, and associated floppy/hard drive support

The result is a fully functional Apple II emulator with every expansion card MAME supports, at a fraction of the full MAME binary size.

## After Building

Copy the binary to EmuBuddy's expected location:

```bash
# Copy to a convenient location
cp mame/mameemubuddy /usr/local/bin/mameemubuddy

# Or point EmuBuddy to it in Settings
```

## Verifying the Build

```bash
# List all included machines
./mame/mameemubuddy -listfull

# List available slot devices for IIGS
./mame/mameemubuddy apple2gs -listslots

# List available slot devices for IIe Enhanced
./mame/mameemubuddy apple2ee -listslots

# Test launch (requires ROMs)
./mame/mameemubuddy apple2gs -rompath /path/to/roms -window
```

## Updating MAME

To rebuild with a newer MAME version:

```bash
cd mame && git pull
cd .. && ./build-emubuddy-mame.sh --clean
./build-emubuddy-mame.sh
```
