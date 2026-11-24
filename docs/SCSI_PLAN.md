# Apple II High-Speed SCSI Card: plan

Today slot 7 holds KEGS's hard-disk card: a small built-in ROM that hands every disk call straight to the emulator
(`smartport.c`). It is fast and simple, but it is not a SCSI card, so software that drives a real card does not work. Above
all, there is no CD-ROM.

This plan adds an emulation of Apple's **Apple II High-Speed SCSI Card** (1990, NCR 53C80, 32 KB ROM, 8 KB RAM) next to the
current card. The user picks the card in Slots. The current card stays the default.

## 1. Decisions

| Question | Decision |
|---|---|
| Which card? | **High-Speed SCSI Card** (ROM `341-0803`). GS/OS 6 supports it well. Its DIP switch SW4 turns off true DMA, so the first version can work with pseudo-DMA only. The older Apple II SCSI Card (`341-0437-A`, NCR 5380) has an unsolved GS/OS driver problem in MAME, so it is left out. Most of the code (5380 chip, SCSI devices) would carry over if it is ever wanted. |
| Which slot? | Slot 7, in place of the current card. Slots menu, slot 7: **Empty / Hard Disk Card / High-Speed SCSI Card**. "Hard Disk Card" is today's card under a clearer name. |
| Where does the card ROM come from? | The user supplies it, like the IIgs ROM. EmuBuddy accepts `341-0803.bin` (32 KB) or MAME's `a2hsscsi.zip`. EmuBuddy already has an unzip routine (`undeflate.c`). It searches next to the IIgs ROM first. Without a ROM, slot 7 falls back to the Hard Disk Card and shows a message. |
| Drives | The seven drives S7D1–S7D7 stay as they are in Drives. Each becomes a SCSI ID, so disks, shared folders, recent disks and drag-and-drop keep working. A drive row can be a hard disk or a CD-ROM. |
| CD images | `.iso`, `.cdr`, `.toast` (2048-byte sectors, ISO 9660 or HFS). Audio CDs (`.cue`/`.bin`) come in a later phase. |
| Speed | Pseudo-DMA moves every byte through the emulated 65816, so it is slower than the Hard Disk Card. EmuBuddy runs much faster than a real IIgs, so this should not be a problem. True DMA (phase 5) speeds it up. |

## 2. What exists and what is missing

| Part | State today |
|---|---|
| Slot I/O `$C0F0–$C0FF` | Exists. It is used the same way for Uthernet II (`moremem.c` io_read/io_write). |
| Slot ROM `$C700–$C7FF` | Exists (`g_rom_cards_ptr`). Today it holds the 256-byte hard-disk ROM. |
| **Expansion ROM `$C800–$CFFF`** | **Missing.** Reads return floating-bus values (`moremem.c` around lines 354 and 1450). The card keeps its ROM banks (`$CC00–$CFFF`) and RAM banks (`$C800–$CBFF`) here, so this has to be built first. |
| NCR 53C80 | Missing. |
| SCSI devices | Missing. Block read and write for the seven drives exist in `smartport.c` (`do_read_c7`/`do_write_c7`, including shared folders through `dynapro`). They write straight into IIgs memory, so they need a version that reads and writes a host buffer. |
| Interrupts | `add_irq`/`remove_irq` with `IRQ_PENDING_*` bits (`defc.h`). One new bit is needed. |
| Code that assumes slot 7 is SmartPort | `ui_drives.c`: startup disk moved to drive 1, shared folders, the boot helper in S7D1, and the "disk put in while running" check. Each has to be checked for both cards. |

## 3. Phases

Each phase ends in something that can be tested, and the Hard Disk Card keeps working after every phase.

### Phase 0: reference and test disks (small)
- Reference machine: the MAME inside Ample (`/Applications/Emulatoren/Ample.app/Contents/MacOS/mame64`), running
  `apple2gs -sl7 a2hsscsi` with the ROMs from `/Applications/Emulatoren/bios`. Use it to see which ID the card starts from,
  what the GS/OS drivers send, and what a working boot looks like.
- Test software already here:
  - Apple II High-Speed SCSI Utilities v2.0 (`Apple II disks/images/hardware/storage/a2_hs_scsi/`)
  - Apple II SCSI Card Utilities
  - a GS/OS 6.0.x system disk with the SCSI drivers (SCSI.Manager, SCSIHD.Driver, SCSICD.Driver)
  - a CD image for phase 4
- Record a reference trace in MAME of the card's first boot: the ROM's register accesses and SCSI commands.

**Done (2026-11-24).** Findings:
- MAME runs headless with `SDL_VIDEODRIVER=dummy`, `-video none`, `-sound none` and the input providers set to
  `none`. It then opens no window and doesn't take focus.
- **Startup ID:** the card starts up from the lowest SCSI ID that has a disk. IDs 0, 2 and 5 started Total Replay; a disk
  at ID 6 gave "Check startup device". So S7D1 = ID 0, S7D2 = ID 1, and so on.
- **MAME is not a GS/OS reference:**
  - ProDOS 8 starts fine.
  - GS/OS starts from the card, then loses its disk. The GTA disk asked for "System"; `system.hdv` said "may be damaged".
  - So GS/OS has to be checked against what the drivers expect, not against MAME.
- The register-level trace is left for phase 2, when there is a 53C80 to compare.

### Phase 1: expansion ROM space `$C800–$CFFF` (medium; the riskiest part)
- A general mechanism in `moremem.c`. A card can ask for the `$C800` space. Reading or writing its `$Cn00` page makes it
  the owner. Touching `$CFFF` releases it. This is the rule on real Apple II hardware.
- The IIgs rules stay as they are:
  - INTCXROM (`$C006/7`)
  - INTC8ROM and the slot 3 ROM (`$C00A/B`)
  - the slot ROM register `$C02D`
  - the Mockingboard's slot 4 handling
- Built differently from the first idea: card pages and `$C800–$CFFF` go through `io_read`/`io_write` instead of the page
  table. A card can switch banks without telling the page tables, and EmuBuddy is fast enough for it. The interface is
  `slotcard.h`.
- Test:
  - Boot GS/OS, ProDOS 8, a Mockingboard demo and a slot 3 program, before and after the change.
  - A small test program reads `$C800` with a fake card in slot 7.

**Done (2026-11-24).**
- Implemented as described: `slotcard.h`, `moremem.c`, plus `slot_cards_reset()` in `do_reset()`.
- `hsscsi.c` has the card's banks and registers, with the 53C80 still missing. `-g_slot7_scsi 2` puts it in slot 7, and
  `-g_hsscsi_rom` names its ROM (otherwise `341-0803.bin` next to the IIgs ROM).
- A BASIC test read the `$C700` page, both banks, `$C800` RAM, the release at `$CFFF` and taking the space back. Every
  value matched the ROM file.
- With the Hard Disk Card, Total Replay, GS/OS (to the Finder) and GTA still start.
- While testing, Total Replay crashed EmuBuddy (and the unchanged 1.2.0 code): asking SmartPort for the device info of
  unit 0 read drive −1. This came from KEGS. Fixed in `smartport.c`.

### Phase 2: NCR 53C80 and the SCSI bus (large)
- `ncr5380.c`: the 8 registers, arbitration and selection, the bus phases (command, data in/out, status, message in), DRQ
  for pseudo-DMA, the interrupt line, and bus reset.
- `scsi_disk.c`: a SCSI hard disk on top of the existing drive (`Disk`). It answers:
  - TEST UNIT READY, REQUEST SENSE, INQUIRY
  - READ CAPACITY, READ(6), READ(10), WRITE(6), WRITE(10)
  - MODE SENSE, START STOP UNIT, PREVENT/ALLOW MEDIUM REMOVAL, VERIFY
- `smartport.c`: split block read and write into a host-buffer version that both cards use, so shared folders (dynapro)
  work on SCSI too.
- Test: `tests/test_scsi.c`, built like `test_uthernet2`. It drives the chip registers the way the card ROM does, with no
  IIgs.

### Phase 3: the card (medium)
- `hsscsi.c`:
  - Registers `$C0n0–$C0nF`: 53C80, pseudo-DMA port, DMA control (`$C0nC`), resume and reset (`$C0nD`).
  - ROM bank (`$C0nE`, 32 × 1 KB at `$CC00`) and RAM bank (`$C0nF`, 8 × 1 KB at `$C800`).
  - The watchdog.
  - DIP switches: card ID 7, SW4 off (pseudo-DMA only).
- Load the ROM: `.bin`, or `341-0803.bin` out of `a2hsscsi.zip`.
- Map S7D1–S7D7 to SCSI IDs. Hard disk 1 must be the startup disk: use the ID the card starts from (found in phase 0).
- Goals:
  - ProDOS 8 and GS/OS 6.0.4 start from a SCSI hard disk.
  - The HS SCSI Utilities v2.0 list the drives.
  - A shared folder shows up.
  - Writing and reading back a large file gives the same file.

### Phase 4: CD-ROM (medium)
- `scsi_cd.c`: a read-only device with 2048-byte blocks. It answers:
  - INQUIRY with an Apple CD drive's name (to be checked: which names GS/OS's SCSICD driver accepts; MAME's `aplcdsc`
    has one that works)
  - READ TOC, READ SUB-CHANNEL, MODE SENSE/SELECT
  - eject
- Drives: a CD row with an eject button. Drop a `.iso` on it or anywhere on the emulator.
- GS/OS sees the disk change through the unit attention after a CD is swapped.
- Goal: GS/OS mounts an HFS CD and an ISO 9660 CD, and files copy from them.

### Phase 5: true DMA (small to medium, optional)
- Turn on SW4. The card's DMA engine moves bytes between the 53C80 and IIgs memory without the 65816: the `$C0n8–$C0nB`
  address and count, stopping at a page boundary, and stopping on interrupt.
- Compare speed with phase 3, and keep it only if GS/OS works the same.

### Phase 6: settings, Launcher and documents (medium)
- Slots: the slot 7 choice and the ROM file in the config (`emubuddy.config`).
- CLI:
  - `-sl7 hd | hsscsi | none`, where `scsi` keeps its current meaning (Hard Disk Card) for old profiles.
  - `-cdrom<n> <img>`
  - `-hsscsirom <file>`
- Launcher: slot 7 choice and CD-ROM drive in both game editors; profile keys and tests.
- Docs: README, `docs/CLI.md`, the manual (Slots, Drives, ROMs), German texts in `lang.c`.
- Builds and a quick check on Windows and Linux (CI).

### Phase 7 (later): CD audio
- PLAY AUDIO, PAUSE and stop for `.cue`/`.bin` images, mixed into EmuBuddy's sound.

## 4. Size

| Phase | New or changed lines | Sessions |
|---|---|---|
| 0 Reference | – | ½ |
| 1 `$C800` space | ~150 changed | 1 |
| 2 53C80 + hard disk | ~1100 | 2 |
| 3 Card | ~300 | 1–2 |
| 4 CD-ROM | ~400 | 1 |
| 5 True DMA | ~150 | ½ |
| 6 Settings and docs | ~300 | 1 |
| **Total** | **~2400** | **7–8** |

## 5. Risks and open questions

- **GS/OS driver timing.** The drivers may wait on DRQ, the watchdog or interrupts in ways MAME's notes don't cover. MAME
  is the reference to compare against.
- **Phase 1 touches the core memory map.** A mistake can break other slots or the slot 3 ROM. Test old software before
  moving on.
- **Startup ID.** Which SCSI ID the card starts from decides how S7D1–S7D7 map to IDs (phase 0).
- **Disks put in while running.** Today EmuBuddy tells GS/OS about a new disk itself (`ui_drives.c`). On SCSI that has
  to happen through the unit attention of the device, and GS/OS may only notice it on removable devices. The drives may
  need to say they are removable.
- **The ROM is Apple's code.** EmuBuddy does not ship it. The Settings text and the manual say where it comes from (a
  real card, or MAME's `a2hsscsi.zip`).
- **The Hard Disk Card stays the default.** Profiles and configs written today must start the same as before.
