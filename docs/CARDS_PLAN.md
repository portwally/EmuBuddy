# More cards: plan

What EmuBuddy has today, which cards could come next, and what each one would bring. Written 2026-11-27, after the
Second Sight card (`docs/SECONDSIGHT_PLAN.md`). Nothing here is decided; the order in section 4 is a proposal.

## 1. What EmuBuddy has

| Slot | Cards and devices today |
|---|---|
| 1 Printer port | Modem, ImageWriter, PostScript printer, AppleTalk |
| 2 Modem port | Modem, FujiNet (as a card of its own), AppleTalk |
| 3 | Video Overlay Card (interlaced Super Hi-Res only), Second Sight VGA card, Uthernet II |
| 4 | Mockingboard (two AY-3-8913, up to two SSI-263 speech chips, Votrax SC-01 with its ROM), Phasor (four AY-3-8913), Uthernet II |
| 5, 6 | The built-in 3.5" and 5.25" drives |
| 7 | Hard Disk Card (seven hard disks), Apple II High-Speed SCSI Card with CD-ROM |
| Not a slot | ZipGS accelerator (8–128 MHz), memory expansion 0–8 MB |

## 2. AppleSqueezer GS

**What it is.** A modern IIgs accelerator by Niek van Suchtelen (v1 2023, v2 2024; sold in the Netherlands). It goes
into the memory expansion slot, not a regular slot. It is built from a Xilinx FPGA, DDR3 RAM and a 65816, and brings:

- speed: "matching or exceeding a TransWarp GS at 16 MHz";
- **14 MB of RAM**, so no other memory card is needed;
- a 224 MB RAM disk;
- an SD card slot: four 32 MB ProDOS drives, bootable since Core 14 (2026). Brutal Deluxe wrote its GS/OS driver,
  `ASSDDriver`;
- HDMI output (v2): Super Hi-Res, 3200-colour pictures and the Apple II text and graphics modes. It shows the IIgs's
  own picture, not new modes.

The FPGA core is updated from the IIgs with the AppleSqueezer control panel.

**What it would mean in EmuBuddy.** Most of it EmuBuddy already has in its own way:

| AppleSqueezer feature | In EmuBuddy |
|---|---|
| Speed | ZipGS emulation, up to 128 MHz, or Unlimited |
| HDMI output | The window shows the same picture |
| SD card, four 32 MB drives | Seven hard disks on the Hard Disk Card or the SCSI card, shared folders |
| 224 MB RAM disk | Hard disk images of any size |
| **14 MB of RAM** | **Missing in the settings**: the menus and `-ramsize` stop at 8 MB |
| Its control panel, `ASSDDriver`, programs that look for the card | Missing; they would find no card |

**Step 1, 14 MB of RAM (small) — done 2026-11-27.** EmuBuddy's core (from KEGS) could already give the IIgs up to
14 MB (memory up to bank `$DF`); for ROM 01 it patches the memory test so that the IIgs counts the RAM above 8 MB
(Geoff Weiss's patch, `config.c`); ROM 03 needs no patch. Only the settings stopped at 8 MB. Now:

- `-ramsize` takes `0M` to `14M`; the Launcher's memory choices are 0, 1, 2, 4, 8, 12 and 14 MB.
- Tested with GS/OS 6.1 on a clean hard disk: About This Apple IIgs shows 14,400K of total memory and about 13,200K
  available, on ROM 03 and on ROM 01. The user's own System 6 hard disk (`system.hdv`, which starts into a program
  launcher) starts with 14 MB on ROM 03. On ROM 01 that disk stops with a SysFail "Queue handle damaged" error, with
  8 MB as well, so that comes from the disk's setup and ROM 01, not from the memory size.
- `docs/CLI.md` and the manual describe it ("as much memory as an AppleSqueezer").
- Not tested: programs that fill the memory above 8 MB (large RAM disks, big documents).

**Step 2, the card itself (large; only with its documentation).** EmuBuddy would answer the card's registers so that
its control panel, the SD card driver and programs that look for the card see one. Its register interface is not
published. The ways to it are:

- ask Niek van Suchtelen for the register description;
- read it out of Brutal Deluxe's `ASSDDriver` (downloadable; Brutal Deluxe also publishes source of other products) and
  the control panel.

Without the description it would be guesswork. As with GSSquared's code, nothing may be copied without a license.
Value: low as long as EmuBuddy already has its own disks and speed. Do it only if users ask for it, or Niek offers the
documentation.

## 3. Other cards

| Card | What it is | What it would bring | Size | Notes |
|---|---|---|---|---|
| **More RAM (14 MB)** | see AppleSqueezer, step 1 | bigger GS/OS setups, RAM disks | small | done 2026-11-27 |
| **Mockingboard speech (SSI-263)** | the speech chip of the Mockingboard C/Sound II | talking programs and games written for it | medium | GSSquared has it, from Tamas Geczy's `ssi263-speech` (MIT license), so that can be used and credited ; done 2026-11-27 (`docs/SPEECH_PHASOR_PLAN.md`) |
| **Phasor** (Applied Engineering) | Mockingboard-compatible, 4 sound chips | games and demos with Phasor music | small–medium | extends `mockingboard.c`; done 2026-11-27 |
| **MIDI** (Apple MIDI Interface, Passport) | MIDI through the serial port | SynthLAB, MIDI Synth tool sets, sequencers playing on the host's synths | medium | new serial mode next to modem and printers; CoreMIDI / Windows MIDI / ALSA |
| **Uthernet (I)** (CS8900A) | the first Uthernet | older network programs (Contiki, early A2OSX) that know only this card | medium | Marinetti works with Uthernet II already |
| **LANceGS** (SMC 91C96) | an Ethernet card for the IIgs | programs that only know LANceGS | medium | little software |
| **CFFA 3000** | CompactFlash/USB disk card | its own firmware and menu; programs that look for it | medium | needs its firmware ROM; disks already work through the Hard Disk Card |
| **RamFAST/SCSI** (Sequential) | the popular IIgs SCSI card with cache | software that talks to it; its utilities | large | needs its ROM; the HS SCSI Card already gives a real SCSI bus and CD-ROM |
| **TransWarp GS** | the other IIgs accelerator | its control panel | medium | ZipGS already covers speed |
| **Video Overlay Card, the rest** | genlock, overlay, dissolve | nothing useful without a video source | small | not worth it |
| **VidHD** | modern HDMI card with extra wide modes | programs using VidHD's "SHR+" modes | medium–large | needs its register description from its maker |
| **CoGS** | open item from the FujiNet work | — | — | ask its author first (see `docs/FUJINET_PLAN.md`) |

Not useful in an emulator: clock cards (the IIgs has a clock), Super Serial Card (the IIgs has two serial ports), Disk
II controller cards (the IIgs has its own), RamWorks and other IIe memory cards, Z-80 cards (no IIgs software needs
them).

## 4. Proposed order

1. **14 MB of RAM**, the useful part of the AppleSqueezer — done.
2. **Mockingboard speech (SSI-263)** and **Phasor**: both extend the existing Mockingboard, and games use them.
   Plan: `docs/SPEECH_PHASOR_PLAN.md` — done 2026-11-27 (phases 0–4, with the Votrax SC-01).
3. **MIDI** through the serial port: brings the IIgs's music programs to real and software synthesizers.
4. **Uthernet (I)** only if a program that needs it turns up.
5. **The AppleSqueezer card itself**, VidHD, CFFA 3000, RamFAST: only with documentation from their makers or a
   user who needs them.

Each card gets a plan of its own, as for SCSI, FujiNet and Second Sight, before work starts.

## 5. Sources

- AppleSqueezer: https://www.applesqueezer.com (product page and FAQ), Brutal Deluxe's SD card driver page
  https://brutaldeluxe.fr/products/apple2gs/applesqueezer/, A2Central's v2 announcement
  https://a2central.com/2024/04/applesqueezer-gs-v2-card-released
- EmuBuddy: `config.c` (memory sizes and the 14 MB ROM patch), `cli.c` (`-ramsize`), `mockingboard.c`.
- GSSquared's README (Mockingboard speech: Tamas Geczy, MIT) — https://github.com/jawaidbazyar2/gssquared
