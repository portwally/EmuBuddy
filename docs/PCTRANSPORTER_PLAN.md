# PC Transporter: plan

A new card for `docs/CARDS_PLAN.md`. Written 2026-11-27. Nothing is built yet.

The **PC Transporter** (Applied Engineering, late 1980s) is an IBM PC-XT on an Apple II card. It has:

- a **NEC V30** CPU (an 8086 with the 80186's extra instructions) at 7.16 MHz;
- **768 KB of RAM**, which the Apple II side can also read and write;
- PC-XT chips, mostly inside custom ASICs: an 8253 timer, an 8237 DMA controller, a speaker;
- **CGA video**, shown on a CGA monitor or on the IIgs's analog RGB monitor;
- a PC floppy controller, the **TransDrive**, for 360 KB 5.25" and 720 KB 3.5" PC disks;
- **hard disk emulation:** the PC's hard disk is a file (`MSDOSVOL`) on a ProDOS volume.

The card has no ROM of its own. Apple II software, the PC Transporter system disk, sets everything up: it loads
the V30's BIOS into the card's RAM, starts the V30, and then serves the PC's disk and keyboard. The design team
(The Engineering Department) included the Apple /// designer Wendell Sander and ProDOS author Dick Huston.

EmuBuddy would run MS-DOS and PC programs (CGA games, Windows 3.0) on the IIgs, as owners of the card did.

## 1. Decisions (proposed)

| Question | Proposal |
|---|---|
| Reference for the card | **MAME's `src/devices/bus/a2bus/pc_xporter.cpp`** (R. Belmont, reverse-engineered by Peter Ferrie, **BSD-3-Clause**). Since its rework on 2026-09-24, MS-DOS and Windows 3.0 run on it. Its notes describe every register (`$C800–$CFFF`, `$CF20–$CF37`). A BSD-3 port into GPL 3 EmuBuddy works, as with the Votrax: license file in the folder, credit in the manual. |
| V30 CPU | Port **MAME's NEC core** (`src/devices/cpu/nec/`: `nec.cpp`, `necinstr.hxx`, `necea.h`, `necmodrm.h`, `necpriv.ipp`; BSD-3) to C in `emubuddy/src/v30/`. MAME's card runs DOS on this exact core, V30 quirks included. Most of it is already C-style macros; only the device and memory framework is replaced, as with `votrax.c`. |
| Fallback CPU core | A GPL-2-or-later or permissive C 8086/80186 core, e.g. PCem's or VirtualXT's. Use only if porting MAME's core turns out too large. Its license must be checked first. |
| PC chips | Written new in C, small: 8253 timer (3 counters), 8237 DMA (4 channels, as MAME's `PCXPORT_DMAC` variant), speaker, CGA 6845 + picture. MAME's 8259 is only a stub on this card: the Apple side software runs the interrupt controller and raises the V30's INT through `$CF30/$CF31`. |
| Slot | The card uses EmuBuddy's generic slot card interface (`slotcard.h`: `$C0n0`, `$Cs00`, `$C800–$CFFF`), so it can go in any slot 1–7. Slots window: offer it in **slot 4** (choices Empty / Mockingboard / Phasor / Uthernet II / PC Transporter) and in **slot 2** (instead of the modem port). The user decides the default; until then, Empty. |
| Picture | As with Second Sight (`secondsight_picture()`, `sdl_driver.c`): while the PC runs and its screen is selected, the window shows the CGA picture scaled to fit, like an RGB monitor switched to the card. A key and menu item switch between IIgs and PC screens. Owners did the same with a monitor switch box. |
| Keyboard | Nothing new: the IIgs keyboard stays the IIgs's. The Apple side driver reads it and passes keys to the PC through ports `$60–$64`. The modifier register `$CF36` is only needed on a IIe; on the IIgs the driver reads `$C025`. This must be checked in phase 3. |
| Hard disk | Nothing new in EmuBuddy: the Apple side driver reads and writes `MSDOSVOL` through ProDOS. The card only passes the requests (mailbox `$C822–$C82A`). |
| Floppies (TransDrive) | Last and optional: an Apple **ISM** (the MFM half of the later SWIM) at `$CF20–$CF27`, 16 MHz, 250 kbit/s MFM, with PC disk images (`.img`, 360 KB / 720 KB). EmuBuddy has only the IWM, so this is new work. Without it, programs reach the PC through the hard disk file, and PC files get onto it via the PC Transporter utilities or a disk tool. |
| Software | **Not shipped.** The PC Transporter system disks (1.30 and 2.0) are on the Apple II Documentation Project (`mirrors.apple2.org.za/.../Interface Cards/CPU/AE PC Transporter/`), with the manual (v1.31). DOS is the user's own; the tests use FreeDOS (GPL). Download only with the user's permission, into the reference folder. |

## 2. Facts (from MAME's `pc_xporter.cpp`, 2026-09-24)

### Memory and windows (Apple II side)

- The card takes the `$C800–$CFFF` space (any access to `$CFFF` releases it; reading `$Cs00` claims it).
- `$C800–$CBFF` = the V30's I/O ports `$000–$3FF`. The Apple side reads and writes the PC's I/O through them.
- `$CC00–$CDFF` and `$CE00–$CEFF`: two windows into the card's RAM. They are placed by `$CF05/$CF06` and `$CF0E/$CF0F`.
- `$CF00–$CF02` is a 24-bit RAM pointer. `$CF03` reads or writes at the pointer and increments it; `$CF04` does the same without incrementing.
- PC RAM `$A0000–$AFFFF` holds the BIOS that the Apple side loads. It is mirrored at `$F0000` so that the V30 can boot from it. `$B8000–$BFFFF` is the CGA picture memory. `$BF400` holds the routing table: one nibble per port, which tells whether the card or the Apple side answers that port.

### Control

- **`$CF30/$CF31`** is a clear/set pair:
  - writing a 1 to a bit of `$CF30` clears that bit, and writing a 1 to `$CF31` sets it;
  - bit 4 = V30 reset, bit 5 = V30 halt, bit 6 = V30 INT request, bit 7 = the card's IRQ to the Apple II enabled;
  - reading `$CF31` gives the transfer status instead: 8- or 16-bit access, read or write.
- **`$CF32`:** bit 7 reads 1 while the PC waits on the Apple side for an I/O port. Writing bit 7 tells the PC that the transfer is complete.
- **`$CF34/$CF35`:** the I/O port that the PC accessed.
- **I/O ports the Apple side answers:**
  1. the V30 stops on the access;
  2. the card raises the Apple's IRQ;
  3. the Apple side reads the port number and does the work (disk, keyboard, screen modes);
  4. the V30 goes on.

  This is the heart of the card, and it is also where its timing matters most.
- **Ports `$22–$27`:** disk requests (operation, drive, offset into `MSDOSVOL`, sector count). **`$C828–$C82A`:** a pointer to a ProDOS MLI call made for the PC.
- **`$CAC1–$CAC5`:** the date and time for the PC's clock, filled in by the Apple side.
- **`$C832/$C833`:** the CGA mode or 6845 register that the Apple side sets up.

### What MAME's card still lacks (TODO in the source)

- A wait loop at `$70B0–$70C5` for the V30's answer about an FPU.
- `$CF28–$CF2B`, `$CF33` and `$CF37` are not understood.

## 3. Phases

### Phase 0: reference

- Build a current MAME (newer than 2026-09-24; the installed 0.262 has the card from before its rework) in the reference folder, as for GSSquared. Never with `-sound coreaudio`.
- With the user's permission, download into `/Applications/Emulatoren/Apple IIgs disks/PC Transporter/reference/`:
  - the system disks (1.30: BIOS Drivers, MS-DOS Volume, Utilities; 2.0 System Disk);
  - the manual.
- In MAME (`apple2gs`, card in a free slot):
  - boot the 2.0 system disk;
  - set up a hard disk file;
  - start MS-DOS (the user's own copy);
  - record screenshots and a log of the card's register traffic (MAME's `LOG_PORTS`/`LOG_IRQ`), headless, on copies of the disks.
- Check whether the card works in MAME's IIgs at all. If it only works in the IIe, say so before phase 1.
- Write down in this file: which slot, which IIgs settings (slot set to "Your Card", speed), what the disk does at each step.

### Phase 0 results (2026-11-28, done)

- **Reference MAME:** git master `ef242a94` (2026-11-27), built only for the IIgs (`./a2gs`) and the IIe (`./a2e`) in
  `/Applications/Emulatoren/mame-master`, 4 minutes each. Tools and notes: `reference/README.txt` and `reference/tools/`
  in `/Applications/Emulatoren/Apple IIgs disks/PC Transporter/` (not to be published).
- **ROMs:** MAME master's IIgs also wants two keyboard microcontroller ROMs, and they're not on this Mac. Running with
  `-adb:0 hle_keyboard -adb:1 hle_mouse` avoids them. The IIgs ROM 3 must be the copy in Ample's folder: the copy in
  `Emulatoren/bios` has another checksum, and with it the machine hangs at start.
- **The card works in MAME's IIgs** with system disk 2.0. Its menu runs, `AEPC.SYSTEM` finds the card, the V30 runs, and the
  CGA screen says `DISK READ ERROR` because no disk holds MS-DOS yet. The 2.0 hard disk file `MSDOSVOL` (578 KB) is an empty
  volume formatted by DOS 3.3. The 1.30 one holds only the PC Transporter utilities. Neither has MS-DOS.
- **Slot 4:**
  - the slot must be set to "Your Card" in the IIgs Control Panel (BRAM `$24` = 1), otherwise the software says
    `PC TRANSPORTER CARD NOT FOUND`;
  - with the card in slot 4, the PC side warns `MOUSE CARD NOT FOUND`, because the IIgs mouse firmware lives in slot 4;
  - in slot 7 (the default "Your Card"), the mouse stays available.

  EmuBuddy should set slot 4 to "Your Card" itself when the card is chosen, as it does for the Mockingboard, and the
  manual should mention the mouse.
- **The system disk supports the IIgs:** it has `AEPCKEY.GS`, and its READ.ME lists fixes for ROM 03 and System 5.0.
- **Manual:** the card goes in any slot except 3. Disks for the PC can be:
  - PC floppies in drives on the card (TransDrive, Apple 3.5 Drive);
  - "ProDOS drives" (the IIgs's own drives, MS-DOS data in ProDOS blocks);
  - `MSDOSVOL` hard disk files on any ProDOS volume.
- **DOS:** the user has no MS-DOS, and MS-DOS can't be downloaded legally, so the tests use **FreeDOS 1.3** (GPL,
  `FD13-FloppyEdition.zip` from ibiblio). `reference/tools/mk360.py` makes a 360 KB boot floppy from its 720 KB boot
  disk: kernel, `COMMAND.COM`, `SYS`, `FORMAT`, `FDISK` and `MEM`.
- **FreeDOS runs on the card in MAME's IIgs:**
  - it boots from the card's 5.25" TransDrive to `A:\>`;
  - `SYS C:` and `FDISK /ACTIVATE:1` make the hard disk file bootable;
  - it then boots from `C:` with no floppy at all (`reference/shots/freedos-booted-from-C.png`).

  **The test disk for phases 2–4:** `reference/testdisks/AEPC 2.0 FreeDOS C.po`. It is the 2.0 system disk set up with
  A> = TransDrive 1 (5.25"), C> = `/AEPC/MSDOSVOL` (on, with FreeDOS) and D> = none.
- **Card software:**
  - **The stock 2.0 settings** make A> an Apple 3.5 Drive on the card's port, C> `/AE1/MSDOS2` (off) and D> `/AEPC/MSDOSVOL` (on).
  - **The Control Panel** opens when the space bar is pressed once while `AEPC.SYSTEM` starts. Its menus take a number and Return, and Esc goes up one level. In text fields the left arrow erases.
  - **Messages from the card's own PC BIOS** (`AEPCBIOS`), in `AEPC.SYSTEM`:
    - `LOADING FROM DISK`;
    - `DISK READ ERROR` (the boot read failed);
    - `BASIC NOT LOADED, RESET SYSTEM` (nothing bootable: no active partition made the MBR call INT 18h).
- **Things to watch in EmuBuddy:**
  - FreeDOS's 2-second "F5 to skip" pause never ended without a key. Either the PC's timer tick doesn't advance there, or it's MAME's doing; phase 3 checks the BIOS tick count (`0040:006C`).
  - MAME typed long command lines with lost characters.
  - MAME ran the IIgs with the card at about 450 % speed on this Mac.
- **Not checked:** CGA graphics modes, the IIgs's 3.5" drives as "ProDOS drives" for the PC, and Windows 3.0 (FreeDOS
  with Windows is not a fair test).

### Phase 1: the V30

- Port MAME's NEC core to `emubuddy/src/v30/` (C, with `LICENSE` and `README.md` like `votrax/`). Interface:
  - `v30_new` and `v30_reset`;
  - `v30_run(cycles)`;
  - memory and I/O callbacks;
  - `v30_set_irq`, `v30_set_halt`.
- Test without the card:
  - a small test program run headless;
  - instruction tests checked against MAME's own run;
  - if a free test set for the V20/V30 exists (e.g. the SingleStepTests suites), use it as test data only, never shipped, and check its license first.
- Done when the tests pass and 10 seconds of V30 code at 7.16 MHz cost under a second of CPU time.

### Phase 2: the card without the V30

- `pctransporter.c`, a `Slot_card`. It covers:
  - the 768 KB RAM and the `$C800–$CFFF` windows and pointer;
  - `$CF30–$CF37` (the V30 stays halted);
  - the card's IRQ to the IIgs;
  - a reset.
- Slots window choice, config key, CLI `-sl4 pctransporter` (and `-sl2`).
- Test: the system disk boots, finds the card, tests its memory and loads the BIOS into `$A0000`. Compare the card's RAM with MAME's (`EMUBUDDY_DUMPRAM`, or a card RAM dump).

### Phase 3: the PC runs

- Wire the V30 to the card:
  - RAM and the BIOS mirror at `$F0000`;
  - the routing table;
  - stopping the V30 on Apple-side ports, with `$CF32/$CF34/$CF35`;
  - INT through `$CF30/$CF31` and the vector at `$BFFFE`;
  - halt and reset.
- Run the V30 in slices next to the 65816, at 7.16 MHz in emulated time.
- Add the 8253, the 8237 and the speaker, mixed into `sound.c`.
- Test, from a hard disk file on a copy of a ProDOS volume:
  1. the PC's BIOS starts;
  2. MS-DOS boots to `A>` or `C>`;
  3. `DIR` lists the files.
- Read the result from the CGA text memory at `$B8000` (before the picture exists), and check keys through `EMUBUDDY_KEYS`.

### Phase 4: the CGA picture

- 6845 registers (`$CF2C/$CF2D`), mode and colour (`$CF2E/$CF2F`).
- Modes: 40/80-column text, 320×200 4-colour, 640×200 2-colour.
- `pctransporter_picture()` and the screen switch (key, menu, window title).
- Test:
  - DOS text, a CGA game and Windows 3.0 (if the user has it);
  - compare screenshots with MAME's;
  - add an `EMUBUDDY_` variable that saves the PC screen headless.

### Phase 5 (optional): TransDrive floppies

- The ISM at `$CF20–$CF27`, MFM encoding of `.img` images, two drives (360 KB and 720 KB).
- Drives window entries for them.
- Test: format, copy and boot a PC floppy image. Only if the user wants PC floppies.

### Phase 6: settings, Launcher, documents

- Launcher: the slot choice and a PC disk field.
- README, `docs/CLI.md`, the manual (section, credit for R. Belmont, Peter Ferrie and the MAME NEC core authors, index entry), PDF.
- Update `docs/CARDS_PLAN.md`.

## 4. Risks and open points

- **Size.** The V30 core port and the card logic are the largest new piece since SCSI. Phases 1–4 are the useful part; phase 5 can wait.
- **Timing between the two CPUs.** The PC stops on every Apple-side port and the IIgs answers through its IRQ. If the slices are too coarse, DOS will be slow or time out. MAME's `stall_v30` logic is the guide.
- **IIgs specifics.** The card was sold for the IIe and the IIgs. MAME's work was mostly done on the IIe. Things to check in phase 0:
  - the IIgs's speed;
  - the shadowing of `$C800` with INTC8ROM;
  - `$C025` for the modifiers.
- **Software.** EmuBuddy neither ships nor downloads the system disks or MS-DOS; the user supplies them. The manual says where they are found.
- **Not in MAME either:** the FPU socket (8087) and the parts of `$CF28–$CF37` that nobody understands yet.

## 5. Sources

- MAME, `src/devices/bus/a2bus/pc_xporter.cpp` and `src/devices/bus/isa/cga.cpp` (`isa8_cga_pcxport_device`), commit "a2bus/pc_xporter.cpp: Major rework to working status" of 2026-09-24 — https://github.com/mamedev/mame
- MAME NEC core, `src/devices/cpu/nec/` (BSD-3).
- Apple II Documentation Project, Interface Cards / CPU / AE PC Transporter: disk images (1.30, 2.0), User's Manual v1.31 — http://mirrors.apple2.org.za/Apple%20II%20Documentation%20Project/Interface%20Cards/CPU/AE%20PC%20Transporter/
- EmuBuddy: `slotcard.h`, `hsscsi.c` (a card with `$C800` space), `secondsight.c` (a second picture), `votrax/` (a ported BSD-3 model).
