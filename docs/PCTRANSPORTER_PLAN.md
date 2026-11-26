# PC Transporter: plan

The Applied Engineering **PC Transporter** is a PC-XT on an Apple II card: an NEC V30 (8086-compatible, 80186
instructions) at 7.16 MHz, 768 KB of RAM, a PIT, a CGA video circuit with its own RGB output, and a floppy controller for
PC drives (the "TransDrive"). It works in the IIgs. The card has **no ROM**: a ProDOS 8 program on the Apple side loads
the PC BIOS into the card's RAM, starts the V30, and from then on does most of the "PC hardware" in 6502 code. Keyboard,
disks, the clock and the PC's interrupt controller are the Apple's job, reached through a mailbox. The PC's hard disk
is a file (`MSDOSVOL`) on a ProDOS volume.

This plan adds the card to EmuBuddy as a slot card, so the original Applied Engineering software runs unchanged and
MS-DOS boots inside the IIgs.

All facts below were read on 2026-11-26 from MAME's `src/devices/bus/a2bus/pc_xporter.cpp` (R. Belmont, with
reverse-engineering by Peter Ferrie; BSD-3-Clause) and the CGA variant in `src/devices/bus/isa/cga.cpp`, and from
EmuBuddy's own `slotcard.h`, `moremem.c` and `sim65816.c`.

**State (2026-11-26):** phases 1 and 2 are written (`pcxport.c`, libx86emu in `x86emu/`) and pass
`tests/test_pcxport.c`, which drives the card through its Apple registers and runs a small V30 program through the
mailbox, the repeated `IN` and an interrupt. They have **not yet met the Applied Engineering software**: that is the
next step, with the user's system disks.

## 1. Decisions

| Question | Decision |
|---|---|
| What is emulated? | The card's hardware only, as MAME does it. The Applied Engineering software (6502 driver, BIOS, utilities) runs as it is, from the user's own disks. Nothing of it ships with EmuBuddy. |
| Where does the knowledge come from? | MAME's driver: the register map, the mailbox protocol, the port routing table and the TransDrive wiring are all documented there. MAME's file is BSD-3-Clause, which may be used in GPLv3 EmuBuddy with its notice kept. EmuBuddy's card is written in C on the `Slot_card` interface; where code follows MAME closely, the file carries MAME's copyright line. |
| V30 core | **libx86emu** (`wfeldt/libx86emu`), vendored under `emubuddy/src/x86emu/`. Plain C, an MIT-style licence, callbacks for every memory and I/O access, runs N instructions and returns. It compiles cleanly with `gcc -Wall` (checked). It is a 386 core run in real mode: the 8086/V30 differences that matter (`PUSH SP`, shift counts, opcode `0F`, the NEC-only instructions) are handled in phase 2. MAME's own NEC core is the alternative if accuracy demands it, but it is C++ tied to MAME's device framework. |
| Which slot? | Slot 4 in the Slots window and on the command line (`-sl4 pcxport`, MAME's name); slots 1 and 2 through the settings file. Not 3 (80 columns), 5 and 6 (the drives) or 7 (the hard disks). The card takes the place of the Mockingboard or Uthernet II in its slot. The slot must be set to "Your Card" in the IIgs Control Panel, as on a real IIgs. |
| PC screen | The CGA output gets its own **PC Screen** window (menu Machine > PC Screen), as a second monitor on the desk. Text shown by the 6502 driver on the Apple screen needs nothing extra. |
| PC hard disk | The software's own way: an `MSDOSVOL` file on a ProDOS volume. That works with every EmuBuddy disk **and with shared folders**, so the PC's disk can be a plain file on the computer. |
| Floppy (TransDrive) | Last phase and optional. It needs an ISM (the MFM half of the SWIM), which EmuBuddy does not have. |
| Default | Off. The Slots window offers "PC Transporter" for slots 1–7. |

## 2. How the card works (from MAME)

**Apple side.** The card answers at `$Cn00` only to take `$C800-$CFFF` (reads give `$FF`, there is no firmware). Then:

| Apple address | What |
|---|---|
| `$C800-$CBFF` | The V30's I/O ports `0-3FF`, as dual-ported RAM. The V30 writes a port, the Apple reads it, and the other way. |
| `$CC00-$CDFF` | A two-page window into the card's RAM; base set by `$CF05` (bits 23–16) and `$CF06` (bits 15–8). |
| `$CE00-$CEFF` | A one-page window; base `$CF0E` / `$CF0F`. |
| `$CF00-$CF02` | A 24-bit pointer into the card's RAM. `$CF03` reads/writes there and counts up, `$CF04` without counting. |
| `$CF20-$CF27` | The ISM (TransDrive). |
| `$CF2C-$CF2F` | CGA: 6845 index, 6845 data, mode (`3D8`), colour (`3D9`). |
| `$CF30` / `$CF31` | Control, clear / set pairs. Bit 4 V30 RESET, bit 5 V30 HALT, bit 6 interrupt the V30, bit 7 card IRQ to the Apple enabled. Reading `$CF31` gives the transfer: bit 7 = the V30 read, bit 6 = 16-bit access. |
| `$CF32` | Bit 7 read: the V30 is waiting for the Apple. Write bit 7: the Apple is done, the V30 continues. |
| `$CF34-$CF35` | The port the V30 touched. |
| `$CF36` | Modifier keys (only for a IIe; a IIgs driver reads `$C025`). |
| other `$CFxx` | Plain registers/RAM. |

**V30 side.** RAM at `00000-BFFFF`. `A0000-AFFFF` holds the BIOS the Apple loaded and is mirrored read-only at
`F0000-FFFFF`, so the V30 starts there. `B8000-BFFFF` is the CGA screen memory. Writes the Apple makes through
`$CF03/$CF04` to `B0000-BFFFF` reach the CGA screen with MAME's address folding (see `write_c800`, `0x703`).

**The mailbox.** For every port the V30 touches, a table in the card's RAM at `BF400` (one nibble per port, written by
the Apple software) says whether the Apple must hear about it:
- 0: no, the dual-ported RAM is enough.
- even: yes, the V30 gets the value already there and stops until the Apple has seen it.
- odd, on a read: the V30 stops, the Apple works out the answer, and **the V30 runs the same `IN` again** once `$CF32`
  bit 7 is written.

When the V30 stops, `$CF32` bit 7 goes up, `$CF34/35` and `$CF31` describe the access, and if `$CF31` bit 7 was set the
card raises the Apple's IRQ. The PC's own interrupts come from the Apple: it puts the vector at `BFFFE` and sets
`$CF31` bit 6; the V30's interrupt acknowledge reads that byte.

## 3. What exists and what is missing

| Part | State today in EmuBuddy |
|---|---|
| Card with firmware page and `$C800` space | Exists: `Slot_card` in `slotcard.h`, `$C800` ownership in `moremem.c`. Used by the HS SCSI Card. Fits the PC Transporter as it is. |
| Card IRQ to the 65816 | Exists for other devices (`add_irq` / `remove_irq`, `IRQ_PENDING_*` in `defc.h`). Needs one new bit. |
| Time for a second CPU | Missing. The event list in `sim65816.c` (`add_event_entry`, `EV_*`) can carry a new `EV_PCXPORT` that runs the V30 for its share of each slice. |
| x86 CPU | Missing: libx86emu. |
| PIT, speaker | Missing. |
| CGA and a second window | Missing. The toolkit already draws several windows; the PC Screen is a texture in one. |
| ISM / PC floppy formats | Missing. |
| The PC Transporter software | Not included. The user supplies the Applied Engineering disks. |

## 4. Phases

**Phase 0 – Reference.** Get the Applied Engineering system disks and manual (Apple II Documentation Project). Run
them in MAME (`apple2gs -sl4 pcxport`) with `LOG_PORTS | LOG_IRQ` on, and keep the logs as the reference for every
later phase. Questions to answer there: what the IIgs driver shows on the Apple screen; whether MAME reaches the DOS
prompt today, and with which DOS.

Answered from MAME's source: the V30's whole I/O space 000–3FF is the mailbox, except the CGA at 3D0–3DF. The PIT,
the DMA controller and the interrupt controller are not on the V30's ports at all; ports 20h, 40h–43h and 60h–64h are
mailbox ports, so **the timer tick, the keyboard and the speaker are the 6502's work**, and the V30's interrupts all
come through `$CF31` bit 6. MAME's PIT8253 and PIC8259 are there only for the ISA bus it borrows for the CGA.

**Phase 1 – The card, without the V30.** *Written.* `pcxport.c`: 768 KB, the windows, the pointer, the registers,
`$C800-$CBFF`, the CGA registers (no picture yet) and an ISM with no drive on it. Slots window entry and `-sl4 pcxport`.
`$C4xx` now reaches a card with firmware in slot 4 (`moremem.c`). *Still to test:* the Applied Engineering installer
and its configuration program find the card and load the BIOS (compare the card's RAM with MAME's after loading).

**Phase 2 – The V30.** *Written.* libx86emu with one memory and I/O handler for the card, reset/halt from
`$CF30/$CF31`, level-triggered interrupts with the vector at `BFFFE` (taken before an instruction, also out of `HLT`),
the routing table at `BF400`, the stop-and-repeat of an `IN` (IP set back to the instruction's start), the Apple IRQ
(`IRQ_PENDING_PCXPORT`). `EV_PCXPORT` runs the V30 every 64 µs of IIgs time, 64 instructions a slice, which is about a
7 MHz V30. *Still to test:* MS-DOS boots from an `MSDOSVOL` to `A>`/`C>`, with the 6502 driver's text on the Apple
screen.

**Phase 3 – Timer and sound.** Probably nothing to do on the card: phase 0 found the PIT and the speaker port in the
mailbox, so the 6502 driver does them with the IIgs's own timers and sound. *Test:* `TIME`, a program that beeps, a game
that uses the timer; only if they fail does the card need more.

**Phase 4 – PC Screen.** A 6845 and the CGA mode/colour registers; text 40/80, 320×200 4 colours, 640×200 2 colours,
drawn from `B8000` into the PC Screen window at 60 Hz. Screenshot (F10) of the front window. *Test:* `MODE`, a CGA game,
GW-BASIC `SCREEN 1`/`SCREEN 2`.

**Phase 5 – TransDrive (optional).** An ISM and PC disk images (`.img`, 360 KB and 720 KB) in two new rows of the
Drives window. MAME gets the ISM from its SWIM1 core; EmuBuddy would need it written. *Test:* format, copy, `DISKCOPY`.

**Phase 6 – Manual and options.** A chapter in the User's Guide, the command-line options in `docs/CLI.md`, the card in
the README.

## 5. Risks

- **The software.** Without the Applied Engineering disks nothing can be tested. Which versions work on a IIgs must be
  checked in phase 0.
- **x86 accuracy.** libx86emu is a 386; a program that relies on 8086/V30 behaviour may differ. If that bites often,
  switch to a port of MAME's NEC core.
- **MAME's own state.** The driver is detailed but still has open points (its TODO: code at `$70B0-$70C5` waits for an
  FPU answer). Where MAME does not get the software to run, EmuBuddy will not either without new reverse-engineering.
- **Speed.** Two CPUs in one thread. The V30's share is small next to KEGS's work, but the stop-and-repeat traffic with
  the 65816 can be heavy; measure in phase 2.
