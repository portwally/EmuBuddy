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
- **Startup ID:** with one disk at a time, IDs 0, 2 and 5 started Total Replay in MAME, and a disk at ID 6 gave "Check
  startup device". From this I took "the lowest ID". **That was wrong** (corrected in phase 3): the firmware looks for
  devices from ID 6 downwards and starts up from the first one, so from the highest ID that has a disk.
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

**Done (2026-11-24).**
- `scsi.h`: the interface between card, chip and devices. None of it knows about the IIgs.
- `ncr5380.c`: the chip and the bus. The targets answer at once. It handles:
  - arbitration and selection
  - IDENTIFY and the LUN
  - REQ/ACK by programmed I/O and by pseudo-DMA
  - the interrupt on a phase change in DMA mode, and the bus reset
- `scsi_disk.c`: the hard disk with the commands listed above, plus REQUEST SENSE data and LUNs other than 0.
- `smartport_read_block`/`smartport_write_block` in `smartport.c` work on a plain buffer. The SmartPort card uses them
  too.
- A drive without a disk does not answer selection, so it looks like an unplugged device. Whether GS/OS then notices a
  disk put in later is left for phase 3.
- `tests/test_scsi.c` covers:
  - selection of empty IDs
  - INQUIRY, READ CAPACITY and MODE SENSE
  - READ(6)/(10) and WRITE(10) by DMA
  - reading past the end, with REQUEST SENSE afterwards
  - write protection, an unknown command, and a bus reset
  All tests pass. They drive the chip the way MAME's card does, not yet with the real ROM; phase 3 shows whether
  Apple's firmware agrees.

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

**Done (2026-11-24).** With Apple's ROM `341-0803`:
- `hsscsi.c` passes the registers to the 53C80, with the pseudo-DMA port, the watchdog bit and the chip reset.
  `-sl7 hsscsi` picks the card.
- **Drive order:** S7D1 = ID 6, S7D2 = ID 5, down to S7D7 = ID 0. The firmware starts up from the highest ID with a
  disk, and it numbers the drives in the same order. With three drives it started from S7D1.
- **One fault found in the chip:** the firmware leaves the DMA mode bit on while it takes the status byte by hand. The
  chip ignored that ACK, so the second command (INQUIRY) never ended. Fixed in `ncr5380.c`; `test_scsi` has a test for it
  that fails with the old code.
- The ROM loads from `341-0803.bin` or from a zip with that file in it (MAME's `a2hsscsi.zip`, stored or deflated).
  EmuBuddy's zip reader for disks leaves out files under 140 KB, so `hsscsi.c` reads the zip directory itself.
- Results:
  - ProDOS 8: Total Replay starts.
  - GS/OS 6.0.x: `system.hdv` starts to the Finder, and Grand Theft Apple starts. About 1,300 commands each, none
    failed. GS/OS uses its own SCSI driver (READ(10)/WRITE(10)) and wrote to the disk. MAME loses the disk here.
  - Three drives: the Finder shows the startup disk, a second hard disk and a shared folder.
  - Apple's High-Speed SCSI Utilities v2.0 (from floppy) list both drives with size, name and SCSI ID, in "HD SC
    Partition" and in "SCSI Verify".
  - Writing: a BASIC program wrote 40 blocks through the firmware and read them back with no difference. On the Mac
    the 40 blocks hold the pattern and no other block of the image changed.
  - With the Hard Disk Card, Total Replay, GS/OS and GTA start as before.
- **A disk put in while the IIgs runs** is not seen: GS/OS sent no command at all to the new ID. The firmware and GS/OS
  look for devices once, at startup, as on a real SCSI bus. (The Hard Disk Card shows the new disk in the Finder at
  once.) EmuBuddy now says "Restart the IIgs to see …" in that case. A drive that says it is removable could change
  this; that belongs to phase 4, where the CD-ROM needs the same parts.
- `EMUBUDDY_SCSI_TRACE=1` prints every SCSI command, `=2` also every register access.
- Not done here, left for phase 6: the third choice in the Slots window (it keeps a High-Speed SCSI Card that was set
  elsewhere, but cannot pick one), the message when the ROM is missing, and the Launcher.
- Not tested: a large file copied in the Finder, a disk taken out while GS/OS uses it, and ROM 01.

### Phase 4: CD-ROM (medium)
- `scsi_cd.c`: a read-only device with 2048-byte blocks. It answers:
  - INQUIRY with an Apple CD drive's name (to be checked: which names GS/OS's SCSICD driver accepts; MAME's `aplcdsc`
    has one that works)
  - READ TOC, READ SUB-CHANNEL, MODE SENSE/SELECT
  - eject
- Drives: a CD row with an eject button. Drop a `.iso` on it or anywhere on the emulator.
- GS/OS sees the disk change through the unit attention after a CD is swapped.
- To decide here: should hard disk drives also say they are removable, so that a disk put in or swapped while GS/OS runs
  shows up without a restart? Check first that the firmware and ProDOS 8 cope with a drive that has no disk.
- Goal: GS/OS mounts an HFS CD and an ISO 9660 CD, and files copy from them.

**Done (2026-11-24).**
- `scsi_cd.c`: the drive says it is a Sony CDU-8002 (the AppleCD SC). 2048-byte blocks, or 512 after MODE SELECT, which
  is what GS/OS's driver asks for. It answers READ(6)/(10), READ CAPACITY, MODE SENSE/SELECT, READ TOC, eject (START STOP
  and Apple's `0xC0`) and Apple's own commands `0xC1`, `0xC2`, `0xCC` for a disc with one data track.
- Details the Apple software needs (the first two from MAME's `nscsi/cd.cpp`, the rest found here):
  - An empty drive answers NOT READY with code `0xB0`, not the usual `0x3A`. With `0x3A`, GS/OS offered to initialize the
    missing disc.
  - The commands `0xC0`–`0xCE` are 10 bytes long.
  - GS/OS's driver asks REQUEST SENSE again and again to see the drive's state, with no failed command before it. So a
    new disc and an empty drive are reported there too.
  - **The size the drive reports.** `SCSICD.Driver` asks READ CAPACITY once, when it starts. Only if the answer is
    more than 65,535 blocks does it allow itself READ(10) (it sets bits in its list of allowed commands, at `$1440` in
    the driver). Later it reads with READ(10) only. A disc under 32 MB, or a drive that was empty at startup, could
    then never be read: the driver refused its own read with error `$21` before it reached the bus, and GS/OS said
    "can't read this disk". So a small image reports 32 MB (reading past its end gives zeroes), and the empty drive
    answers READ CAPACITY with the size of a 74-minute disc. A real drive may behave differently here; this is what
    makes Apple's driver work.
- `hsscsi.c`: a drive with a `.iso`, `.cdr` or `.toast` image is a CD-ROM drive and stays one when the disc comes out.
  S7D7 is a CD-ROM drive also when it is empty at startup. A CD image goes in whole, without the partition chooser, and
  is write protected. A dropped CD image goes into an empty CD-ROM drive.
- `test_scsi` covers the empty drive, a new disc, the reported sizes, both block sizes, writing refused, the disc change
  in the sense data, and eject.
- **Works** (GS/OS 6.0.x, ROM 03):
  - A CD with an Apple partition map (the System 7 CD, HFS) shows up in the Finder with a CD icon.
  - A CD without a partition map (a ProDOS volume) shows up too.
  - A CD dropped on EmuBuddy while GS/OS runs goes into S7D7 and shows up in the Finder without a restart.
  - The firmware and GS/OS start normally with an empty CD-ROM drive on the bus.
  - With the Hard Disk Card, Total Replay and GS/OS start as before.
- **Plain ISO 9660 works; the test CD was at fault.** macOS (`hdiutil makehybrid -iso`) fills the volume name in
  the volume descriptor with zero bytes, not with spaces as ISO 9660 says. GS/OS's `HS.FST` mounts such a disc, but
  the Finder shows no icon for it, with the Hard Disk Card as well. With the name filled with spaces, the same CD
  shows up as TESTISO on both cards (on the SCSI card with a CD icon). So EmuBuddy hands the name to the IIgs filled
  with spaces (`smartport_fix_iso_name` in `smartport.c`, for `.iso`, `.cdr` and `.toast` images on both cards). The
  image file is not changed. Tested with the unchanged macOS image on both cards.
- How the driver problem was found: a memory dump of the working and the failing case, a small 65816 disassembler,
  and temporary breakpoint output at the driver's error exits. `SCSICD.Driver` loads at `$03/0000` on the test system;
  its read routine is at `$14AF`.
- Not tested: copying files from the CD, eject from the Finder, taking a disc out and putting another in, the Drives
  window with a CD, ProDOS 8 with a CD, starting up from a CD, and ROM 01.

### Phase 5: true DMA (small to medium, optional)
- Turn on SW4. The card's DMA engine moves bytes between the 53C80 and IIgs memory without the 65816: the `$C0n8–$C0nB`
  address and count, stopping at a page boundary, and stopping on interrupt.
- Compare speed with phase 3, and keep it only if GS/OS works the same.

**Done (2026-11-24).** True DMA is on by default.
- `hsscsi.c`: the DMA engine as in MAME's `a2hsscsi.cpp`. While the 53C80 asks for bytes, the card moves them between
  the chip and IIgs memory, at the address in `$C0n8/9` and in the bank of the IIgs's DMA register `$C037` (which
  `moremem.c` now keeps). Address and count both count up; the count is loaded as a negative number. At the end of
  the count the chip gets EOP (`ncr5380_eop`); at the end of the bank the card stops and waits for `$C0nD`.
- Setting `g_hsscsi_dma` (switch SW4): 1 true DMA (default), 0 pseudo-DMA only. No place in the windows yet;
  `-g_hsscsi_dma 0` on the command line.
- The firmware and GS/OS use it at once: 579 DMA transfers during a GS/OS start, most of 512 bytes. The firmware
  also lets the card write into its own RAM at `$C9xx` this way.
- Speed, GS/OS start to the Finder at 2.8 MHz: about 70 s with true DMA, about 75 s with pseudo-DMA (measured in
  steps of 5 s). At unlimited speed there is no difference to see. A transfer takes no IIgs time at all, which is
  faster than the real card.
- Tested with true DMA: Total Replay; GS/OS with the System 7 CD; Grand Theft Apple; an ISO CD dropped in while GS/OS
  runs; 6 blocks written through the firmware and read back with no difference, right on the Mac and no other block
  changed. With the Hard Disk Card, Total Replay and GS/OS start as before.
- Not tested: a transfer that crosses the end of a bank (the stop and resume), and interrupts during DMA (not
  built, as in MAME).

### Phase 6: settings, Launcher and documents (medium)
- Slots: the slot 7 choice and the ROM file in the config (`emubuddy.config`).
- CLI:
  - `-sl7 hd | hsscsi | none`, where `scsi` keeps its current meaning (Hard Disk Card) for old profiles.
  - `-cdrom<n> <img>`
  - `-hsscsirom <file>`
- Launcher: slot 7 choice and CD-ROM drive in both game editors; profile keys and tests.
- Docs: README, `docs/CLI.md`, the manual (Slots, Drives, ROMs), German texts in `lang.c`.
- Builds and a quick check on Windows and Linux (CI).

**Done (2026-11-24), without the Windows and Linux check.**
- Slots window: slot 7 offers Empty, Hard Disk Card and HS SCSI Card. Without the card's ROM, slot 7 keeps the Hard
  Disk Card and the status bar says "No SCSI card ROM: Hard Disk Card used" (after Apply, and a few seconds after a
  start).
- Slots window, the card's ROM: the last line says whether the ROM file is there, and **SCSI ROM...** picks it (a
  wrong file is refused). EmuBuddy does not ship or download the ROM: it is Apple's, with no licence to pass it on.
  Ample downloads its ROMs from `https://www.callapple.org/roms/`; EmuBuddy would do that only with their permission.
- CLI: `-sl7 hd | hsscsi | none` (`scsi` and `cffa2` still mean `hd`), `-cdrom <img>` (hard disk drive 7) and
  `-hsscsirom <file>`. Built differently from the plan: one `-cdrom`, not `-cdrom<n>`, because S7D7 is the CD-ROM
  drive.
- Drives window: a CD-ROM drive is marked CD instead of HD. Messages say "hard disk card" where they said "SCSI card".
- Launcher: `sl7` can be `hsscsi`; new profile keys `cdrom` (a field in the editor) and `hsscsirom` (no field: the
  ROM is found next to the IIgs ROM). `test_profile` covers them.
- Documents: README, `docs/CLI.md`, and the manual with a new section "The High-Speed SCSI Card and CD-ROMs" (60
  pages). The three pictures of the Slots window got the new slot 7 row.
- Tested: the Slots and Drives windows by picture, a start without the ROM, starts with `-sl7 hd`, `-sl7 scsi` and
  `-sl7 hsscsi` with `-cdrom` and `-hsscsirom`; `test_scsi`, `test_profile` and `test_lang` pass.
- Not done or not tested:
  - Windows and Linux builds (CI) have not run with these changes.
  - The Launcher's editor was not looked at on screen with the new field and choice.
  - The picture of the Slots window with AppleTalk in the manual still shows the window without the ROM line.
  - When the ROM is missing, the saved setting becomes Hard Disk Card.

### Phase 7: CD audio
- PLAY AUDIO, PAUSE and stop for `.cue`/`.bin` images, mixed into EmuBuddy's sound.

**Done (2026-11-24).**
- `cdimage.c`: reads a `.cue` sheet and its files. Track kinds MODE1/2048, MODE1/2352, MODE2/2352 and AUDIO; files
  BINARY and WAVE; several files in one sheet. It knows nothing about the IIgs.
- `scsi_cd.c`: the table of contents with tracks, and the Apple CD SC's audio commands as in MAME's `nscsi/cd.cpp`:
  AUDIO TRACK SEARCH, PLAY, PAUSE, STOP, STATUS, SCAN (only moves the position) and CONTROL (the volume), plus READ
  TOC and READ Q SUBCODE. Reading an audio track as data gives "illegal mode for this track" (`0x64`): GS/OS's driver
  takes that for "no data disc" and stays quiet. With zeroes for data, GS/OS offered to initialize an audio CD.
- `hsscsi.c` opens the CD of a `.cue` in a drive and hands the audio to `sound.c` (`hsscsi_audio_mix`), which adds it
  to the output. 44,100 Hz is brought to the output rate by taking the nearest sample.
- A `.cue` needs the High-Speed SCSI Card; with the Hard Disk Card EmuBuddy says so.
- **Two faults found on the way, both outside audio:**
  - **The list of commands in the INQUIRY data had no end mark.** `SCSICD.Driver` reads from bytes 38 on which
    commands the drive knows (records of a number and two words, ended by `0xFF`; at `$1322` in the driver). Without
    the end mark it fell back to a list of its own, with no audio commands and with READ(10) only for discs over 32 MB.
    With the list complete, the driver allows READ(10) by itself; the size the drive reports (phase 4) is kept anyway.
  - **The chip mixed its output register into the status byte.** Apple's firmware reads the status with "assert data
    bus" still on. For a command whose last byte is not zero (the audio commands end in `0x80`), the status came out as
    nonsense, the driver reported an error and CD Remote gave up. `ncr5380.c` no longer drives the bus in an input
    phase; `test_scsi` has a test for it.
- Tested:
  - `test_scsi`: a test CD written by the test itself (a data track, an audio track in the same file, an audio track
    in a WAVE file): tracks, table of contents, play, pause, the end of a track, the volume.
  - GS/OS with **CD Remote** (from the System 6.0.4 disks, added to a copy of the test system): it shows the tracks,
    PLAY starts track 1 of an audio CD, its window says PLAY and the time runs.
  - The sound output was recorded (`EMUBUDDY_SOUNDFILE=<file.wav>`, a new test aid): the 440 Hz tone of the test CD is
    there at its full level.
  - A CD with a data track and audio tracks mounts its data track (TESTISO) as before.
  - Total Replay, GS/OS with the System 7 CD, Grand Theft Apple, a CD dropped in while GS/OS runs, and both start-ups
    with the Hard Disk Card work as before.
- Not tested: the other buttons of CD Remote on screen (pause, next track, scan, eject), games that play CD audio, a
  real CD image from the net (only CDs made here), ProDOS 8 software, and how the nearest-sample conversion sounds.
- Not built: AUDIO SCAN as fast play, index marks and gaps before tracks (INDEX 00, PREGAP), and subchannel data.

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
- **Startup ID.** Settled in phase 3: the highest ID with a disk, so S7D1 = ID 6.
- **Disks put in while running.** Settled in part in phase 3: GS/OS does not notice a new hard disk, and EmuBuddy tells
  the user to restart. Whether the drives should say they are removable is open (phase 4).
- **The ROM is Apple's code.** EmuBuddy does not ship it. The Settings text and the manual say where it comes from (a
  real card, or MAME's `a2hsscsi.zip`).
- **The Hard Disk Card stays the default.** Profiles and configs written today must start the same as before.
