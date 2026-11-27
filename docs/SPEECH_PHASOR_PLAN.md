# Mockingboard speech and the Phasor: plan

Item 2 of `docs/CARDS_PLAN.md`. Written 2026-11-27. Nothing is built yet.

The Mockingboard in slot 4 plays music today: two 6522 VIAs, each driving one AY-3-8913 sound chip (`mockingboard.c`,
`sound.c`). Two things are missing that Apple II programs used:

- **Speech.** The Mockingboard Sound/Speech I and the Mockingboard C have sockets for the **SSI-263** speech chip
  (Silicon Systems; Votrax sold it as the SC-02). Early Mockingboard speech boards used the **Votrax SC-01**. Programs that talk on a
  Mockingboard stay silent in EmuBuddy, and some programs look for the speech chip to find the card at all.
- **The Phasor** (Applied Engineering, 1986): a Mockingboard-compatible card with **four** AY-3-8913 chips (two on each
  VIA), two speech-chip sockets, and three modes it switches between in software: Mockingboard, Phasor native and
  Echo+ (Street Electronics' card).

## 1. Decisions

| Question | Decision |
|---|---|
| How is the SSI-263 made to speak? | With **Tamas Geczy's chip model** (`github.com/tgeczy/ssi263-speech`, `src/csrc/ssi263.c`, C99, **MIT license**). It synthesizes every sample from the chip's phoneme ROM (read from die photos) and its filter model: no recordings. Its interface fits an emulator: `ssi263_write(chip, reg, value)`, `ssi263_request()` for the A/R line, `ssi263_run(chip, n, out)` for n samples at our rate. The files are added unchanged in `emubuddy/src/ssi263/` with the MIT notice, and the manual credits him. |
| Fallback if the chip model does not work out (speed, timing) | AppleWin's SSI-263 (`SSI263.cpp`, GPL 2 or later, so usable in GPL 3 EmuBuddy): it plays sampled phonemes (22,050 Hz). Coarser, but proven with Apple II software. |
| SC-01 (Votrax) | Later, optional (phase 4). MAME's `votrax.cpp` (Olivier Galibert, BSD-3-Clause) is the reference. Few programs need it. |
| Where | Slot 4, as today. Slots window, slot 4: **Empty / Mockingboard / Phasor / Uthernet II**. Speech chips: a setting "Speech chips: none / one / two" for both cards; default **one** (a Mockingboard C as most were sold). |
| GSSquared | It has the SSI-263 from the same chip model; no code of its own is needed from it. |

## 2. Facts (from AppleWin's `Mockingboard.cpp` and `SSI263.cpp`, which were checked on real cards)

### SSI-263 on the Mockingboard C

- Two sockets. The **first** speech chip answers at `$Cn40–$Cn47` and is wired to the **second** VIA's CA1; the
  **second** speech chip at `$Cn20–$Cn27`, wired to the **first** VIA's CA1. Writes there also reach the VIA that the
  same address selects. Register = address bits 2–0: 0 duration/phoneme, 1 inflection, 2 rate/inflection,
  3 control/articulation/amplitude, 4–7 filter frequency.
- The chip's A/R line (active low) tells the program that a phoneme is nearly done. It goes to the VIA's CA1, so the
  VIA raises an interrupt if the program enabled CA1. On the Mockingboard the chip cannot be read.
- Behaviour that is not in the data sheet (AppleWin GH#175, GH#1197): a phoneme repeats until it is changed or CTL=1;
  writes to registers 0, 1, 2 (and 3 with CTL=1) clear A/R, other writes do not; CTL=1 is power-down, also at power-on;
  CTL=0 starts the phoneme in the mode DR1:DR0. Ctrl-Reset does not reach the chip on a Mockingboard C.

### Phasor

- **Mode switch:** any access to the slot's device-select area `$C0n0–$C0nF` (slot 4: `$C0C0–$C0CF`). New mode =
  address bits 2–0, or-ed into the old mode unless address bit 3 is set: 0 Mockingboard, 5 Phasor native,
  7 Echo+. Power-on and reset: Mockingboard mode.
- **Which chips an address selects:** Mockingboard mode as today (A7 picks the VIA). Native mode: A7 and A4 pick the
  VIA(s): 0, 1, 2 or both. Echo+ mode: the second VIA at every 16-byte offset in `$Cnxx`.
- **Two AY-3-8913 per VIA:** in native and Echo+ mode the VIA's port B bits 4 and 3 (active low) select the first and
  second AY; in Mockingboard mode only the first. Bit 2 resets both AYs.
- **Clock:** native mode runs the AYs at twice the clock (about 2 MHz instead of 1.023 MHz).
- **Speech:** same addresses as the Mockingboard C; in native mode the speech chips can be read (bit 7 = A/R), and
  their A/R line also drives the IRQ line directly.
- No speech chips in Echo+ mode.

### EmuBuddy today

- `$C400–$C4FF` go to `mockingboard_read/write` (`moremem.c`), A7 picks one of two pairs (VIA + AY).
- `$C0C0–$C0C3` are taken by KEGS's built-in **Slinky RAM card**. With a Phasor in slot 4 these addresses must go to
  the Phasor's mode switch instead (the Phasor takes the whole `$C0C0–$C0CF`).
- `sound.c` mixes the two AYs into the stereo output.

## 3. Phases

### Phase 0: references and test disks (small) — done 2026-11-27

Kept outside the repository in `/Applications/Emulatoren/Apple IIgs disks/Mockingboard and Phasor/` (`software/`,
`docs/`, `reference/`).

- **mb-audit v1.61** (Tom Charlesworth, MIT): its 140K disk does not start from the 5.25" drive in a headless run, so
  its files were copied to an 800K ProDOS image (`software/mba800.po`) that starts as hard disk 1. Besides the audit it
  has `PLAYSC01ON263` (speaks SC-01 phrases through an SSI-263), `PH` and `BEARAGRUDGE` (Phasor music).
- **Slot 4 must be "Your Card"** in the IIgs Control Panel, or mb-audit finds no card. For headless runs:
  `reference/bram_slot4.txt` (battery RAM rows for EmuBuddy's config, ROM 03, slot 4 = Your Card, checksum fixed) and
  `reference/rtc_slot4` (the same 256 bytes as MAME's `nvram/apple2gs/rtc`).
- **Speed:** mb-audit finds the card only at **1 MHz**. At 2.8 MHz, 8 MHz or Unlimited it says "No Mockingboard
  detected", in EmuBuddy and in MAME alike: its timer-based detection was written for 8-bit Apple IIs. Run it with
  `-mhz 1` (MAME: clear bit 7 of `$C036` from Lua).
- **EmuBuddy today** (Mockingboard, 1 MHz): mb-audit finds a Mockingboard C in slot 4 with no speech chip, warns once
  (test 21.14), passes the reset tests (Ctrl-Reset skipped with Esc), and ends with **"All tests passed"**
  (`reference/emubuddy-mbaudit-mockingboard.png`).
- **MAME 0.262** (`/Applications/Emulatoren/mame`, IIgs, ROM 03, headless with `reference/gsrun.sh` and
  `reference/sched.lua`, which schedules snapshots and keys by emulated time): it finds the Mockingboard
  (Sound/Speech I, with an SSI-263) and the Phasor, but fails mb-audit's 6522 test 11.03 on both (expected `F1`, got
  `F0`) (`reference/both.png`). So MAME is no reference for the VIA details, where EmuBuddy is already better; it stays
  the reference for how speech and Phasor music should sound. MAME's IIe (`apple2ee`) did not start the disk headless.
- **Speech reference:** `reference/speech_mb.wav`, MAME's Mockingboard speaking phrase 1 of `PLAYSC01ON263`
  (around 16.0–17.5 s), recorded with MAME's sound output off (`-wavwrite`).
- **The chip model** (`tgeczy/ssi263-speech`, commit in `reference/chip/COMMIT`): `ssi263.c`, `ssi263.h`,
  `ssi263_defaults.h`, `ssi263dsp.c` build alone with `cc -O2 -std=c99 -Wall` without a warning. 30 seconds of
  continuous speech (1,220 phonemes, each started when A/R comes) cost **0.21 s of CPU, 0.7 % of one core per chip**
  (3.6 % unoptimised). Speed is no concern. `reference/chip/bench.c` and `bench.wav`.
- **More software** (Asimov): the four Phasor disks, "Mockingboard Sound-Speech Generator", "Mockingboard Disk 2
  Sound-Speech Dev", Ultima V patched for the Mockingboard on the IIgs; the Phasor manual v1.3 and the Mockingboard
  documents. Asimov also has Votrax SC-01 ROM dumps, needed only for phase 4.
- Open for phase 1: whether IIgs programs that use the Mockingboard at normal speed (Ultima V for the IIgs) work in
  EmuBuddy today; mb-audit cannot tell.

### Phase 1: the SSI-263 on the Mockingboard C (medium) — done 2026-11-27

- `emubuddy/src/ssi263/`: the chip model's four C files, unchanged, with its MIT `LICENSE` and a README naming the
  commit. `mbspeech.c` wires them to the card.
- Two sockets: the first chip takes writes with address bit 6 and drives CA1 of the second 6522; the second chip
  address bit 5, CA1 of the first 6522. Writes reach the chip and the 6522 the address selects, as on the card.
- Time: each chip makes its samples up to the emulated "now" on every write, on every `sound_play()`, and on a new
  event (`EV_MBSPEECH`, every 256 cycles while a chip is awake). When A/R comes, CA1 of its 6522 is flagged (negative
  edge, PCR bit 0 = 0) and the 6522's interrupt logic does the rest. Writes to registers 0–2 (and 3 with CTL=1) clear
  it. Ctrl-Reset does not reach the chips.
- **A card behaviour the chip model lacks:** on real cards a phoneme repeats, and after a write to register 1 or 2
  answers a request, the chip asks again when the phoneme ends again (AppleWin GH#1197; mb-audit test 41:06 checks
  it). The model keeps its request up instead. `mbspeech.c` writes the current phoneme again in that case (the model
  holds it until the current one ends), which drops the request and raises it at the end. The chip files stay
  unchanged.
- The 6522 now clears its CA1 flag when port A is read or written (register 1), as a real 6522 does.
- Mixing: speech on both channels, after the AY chips; level scaled with the IIgs volume.
- Setting `g_mb_speech` ("Mockingboard speech chips": 0 none, 1 one, 2 two; default one). `EMUBUDDY_SPEECHLOG=1`
  logs register writes and requests, `=2` also every mixed buffer.
- **Results** (mb-audit v1.61, 1 MHz): no chip — all tests passed (total `$32D`), as before; one chip — the chip is
  found ("U"), all tests passed (`$35F`), including the phrase-duration test; two chips — both found ("UU"), all
  tests passed (`$390`). MAME's 6522 fails earlier (test 11.03), so EmuBuddy now passes more of mb-audit than MAME.
- `PLAYSC01ON263` finds a real SSI-263 (it offers phrases A–G; in MAME it fell back to SC-01 phrases 1–7). Phrase A:
  26 phonemes, about 2 seconds of speech (`reference/emubuddy-ssi263-phrase-A.wav`).
- Not checked: how the speech sounds next to a real card (by ear: `reference/emubuddy-ssi263-phrase-A.wav`); IIgs
  programs that use the Mockingboard at normal speed (Ultima V for the IIgs).
- Seen on the way, not changed: EmuBuddy's `EMUBUDDY_SOUNDFILE` WAV keeps a stale length in its header when a
  headless run ends at its snapshot frame (`sound_file_close` is not called); the samples are all there.

### Phase 2: the Phasor (medium) — done 2026-11-27

- Card type in slot 4: `g_slot4_mockingboard` 0 empty, 1 Mockingboard, **2 Phasor** (config "Slot 4 Mockingboard",
  CLI `-sl4 phasor`). The Slots window still shows it as "Mockingboard" until phase 3, and keeps a Phasor a Phasor
  when its settings are applied.
- **Mode switch:** any access to `$C0C0–$C0CF` (`phasor_devsel`): address bit 3 clears the mode, bits 2–0 are or-ed
  in. Reset: Mockingboard mode. With the Phasor in slot 4 these addresses belong to it: the Slinky RAM card's
  `$C0C0–$C0C3` and an Uthernet II also set to slot 4 (the default config has one there, and `-sl4` on the command
  line did not move it) no longer see them. With a Mockingboard, both stay as before.
- **Addresses:** Mockingboard mode as before (A7 picks the 6522); native mode A7 and A4 (none, one or both 6522s; a
  read of both is the or of the two); Echo+ mode the second 6522 at every address. Nothing selected reads the
  floating bus.
- **Four AYs:** each 6522 has a second AY (`Mock_pair.ay8913_b`). In native and Echo+ mode ORB bits 4 and 3 (active
  low) select the first and second AY; in Mockingboard mode only the first answers. The Phasor's GAL in native mode,
  as AppleWin describes it from real cards: latching through the first AY deselects the second, latching through the
  second selects both; a write through the first also reaches the second if selected; a read of both is the or of
  the two; an AY that is not selected leaves the bus high (`$FF`). The reset bit resets both AYs in every mode.
- **Clock:** native mode runs all four AYs at twice the clock (tone, noise and envelope counters), Mockingboard and
  Echo+ mode at the normal clock.
- **Sound:** `sound.c` plays four AYs, the first 6522's two on the left, the second's on the right; in native and
  Echo+ mode each AY plays at 0.6 of the level so that two on one side do not clip. Mockingboard mode keeps today's
  level.
- **Speech on the Phasor:** Mockingboard mode as on the Mockingboard C. Native mode: the chips can be read (bit 7 =
  request; reading does not clear it), and A/R drives the IRQ line directly (`IRQ_PENDING_MBSPEECH`) instead of CA1.
  Echo+ mode: no speech writes, A/R reaches nothing. On a mode change a pending request moves to the new line.
- **Two speech-chip details found with mb-audit's Phasor tests** (they apply to the Mockingboard too):
  mode 00 (DR1:DR0 at CTL 1→0) only keeps A/R from the computer, while the phoneme runs in the previous mode and
  bit 7 still goes up; and CTL 1→0 starts the phoneme in register 0, so the request is down until it ends. The chip
  model gives no request in mode 00 and does not restart at CTL 1→0, so `mbspeech.c` hands it the previous mode and
  writes register 0 again there. The chip files stay unchanged.
- **Results** (mb-audit v1.61, 1 MHz): the Phasor is found ("P" for both 6522s, "Phasor mode=Native"), and with no
  speech chip `$363`, one `$401`, two `$49E`: **all tests passed**, including the AY tests of all four chips, the
  GAL tests and the Phasor speech tests (`reference/emubuddy-mbaudit-phasor.png`). The Mockingboard still passes with
  the same totals as in phase 1 (`$32D`, `$35F`, `$390`).
- **Music:** mb-audit's music test (menu D) plays in native mode with all four AYs sounding, each with its own
  volumes; 30 s recorded, peak 13,240, nothing clipped (`reference/emubuddy-phasor-music-test.wav`).
- `EMUBUDDY_SPEECHLOG` also logs the Phasor's mode switches.
- Not checked: the Phasor's own disks (`phasor1-4.dsk`) and programs that use Echo+ mode; how the music sounds next
  to a real Phasor (by ear).

### Phase 3: settings, Launcher and documents (small) — done 2026-11-27

- Slots window, slot 4 "Sound / Net": **Empty / Mockingboard / Phasor / Uthernet II**; a **Speech** button next to
  FujiNet On cycles None / One / Two (at once, no restart; saved in the config). The window is 620 pixels wide now
  (was 580), so that "Mockingboard" fits one of four buttons. New picture in the manual (`docs/manual/img/slots.png`).
- Command line: `-sl4 phasor` and `-mbspeech 0|1|2` (`-help`, `docs/CLI.md`).
- Launcher: `sl4 = phasor` as a fourth choice, a "Speech chips" row (`mbspeech = 0|1|2`, German "Sprachchips") passed
  on as `-mbspeech`; imported start scripts keep `-mbspeech`; the "Sound card" switch and the game facts count a
  Phasor as a sound card (the switch leaves a Phasor a Phasor).
- README (slots table, a paragraph, credits), the manual (slots table, a section "The Mockingboard and the Phasor",
  command-line table, credit for Tamas Geczy's chip model on the copyright page, index entries Phasor and speech
  chips); `docs/EmuBuddy-Manual.pdf` rebuilt (63 pages).
- Checked: `-showconfig` for the new options and a bad `-mbspeech 3` (exit code 2); the Slots window headless, with
  clicks: Phasor → Uthernet II → Apply, and Mockingboard → Phasor → Apply, and the Speech button; the Launcher tests
  (`test_profile`, `test_attract`, `test_jpeg`, `test_lang`, `test_library`) pass.
- Not checked: a picture of the Launcher's editor with the new row (the Launcher does not run headless).

### Phase 4: Votrax SC-01 (small–medium, optional) — done 2026-11-27

- **The chip:** `emubuddy/src/votrax/votrax.c`, a C port of MAME's `votrax.cpp` (Olivier Galibert, BSD-3-Clause,
  MAME commit ecbaa509ee94; `LICENSE` and `README.md` in the folder). The chip logic, ROM decoding, filters and
  constants are MAME's line for line; MAME's device, stream and timer became a struct, a sample loop and a tick
  counter, and the chip's rate (clock / 18) is averaged down to the host's. Built alone with `cc -std=c99 -Wall
  -Wextra` without a warning; 3 s of speech cost 7 ms of CPU.
- **The ROM:** the chip needs its 512-byte phoneme ROM, `sc01a.bin` (SHA-1 1d6da90b…, as MAME expects). EmuBuddy
  does not ship it, as with the High-Speed SCSI Card: the setting "SC-01 ROM" (`-sc01rom`), or `sc01a.bin` or MAME's
  `votrax.zip` next to the IIgs ROM. Without it there is no SC-01 and nothing changes. The SCSI card's zip reader
  moved to `romfile.c` (`rom_file_read`), which both cards use; the SCSI card was checked with its ROM as a file and
  as MAME's zip.
- **Wiring** (MAME's Mockingboard and AppleWin): when the first 6522's PCR sets CB2 to pulse output (`$B0`), a write
  to its port B is a phoneme (bits 5–0; inflection bits 7–6) and does not reach the AY; A/R goes to CB1 of that 6522
  (IFR bit 4, positive edge) and rises when the phoneme ends; a new phoneme clears the flag. The chip runs at
  1,022,727 Hz, in step with emulated time like the SSI-263s. It sits on both cards, next to the SSI-263s, as in
  AppleWin.
- **Settings:** "Mockingboard SC-01" (`g_mb_sc01`, on; `-sc01 on|off`) and "SC-01 ROM" (`-sc01rom`). The Slots window
  says on its last line whether the ROM was found (picture in the manual retaken). README, `docs/CLI.md`, the manual
  (a paragraph, command-line rows, credit, index) and the PDF updated.
- **Results** (mb-audit v1.61, 1 MHz, ROM found next to the IIgs ROM): mb-audit finds the SC-01 ("V") and passes all
  tests: Mockingboard with SC-01 and no, one or two SSI-263s `$32E`, `$360`, `$391`; Phasor with SC-01 and two
  `$49F`. With `-sc01 off` the totals are those of phase 1 (`$35F`).
- `PLAYSC01ON263` finds the SC-01 and offers phrases 1–7: phrase 1 is 24 phonemes and lasts **1.15 s, as in MAME's
  recording** (`reference/speech_mb.wav`). Its level was set to about the SSI-263's (peak 7,164 against 7,619;
  `reference/emubuddy-sc01-phrase-1.wav`).
- Not checked: programs that speak only through the SC-01 (Crimewave and others) and how it sounds next to a real
  chip; the Launcher has no field for the SC-01 ROM (`-sc01rom` can go in its extra options).

## 4. Risks and open questions

- **Speed of the chip model.** It filters at a fine rate and decimates to the host rate. If two chips cost too much on
  slower machines, run them only while they speak (power-down mode is silent) and measure in phase 0.
- **Timing.** Programs wait for A/R through the VIA interrupt; mb-audit checks this closely. The chip has to run in
  step with the emulated CPU, not with the audio buffer.
- **The phoneme ROM.** The chip model carries the SSI-263's phoneme ROM, read from die photos, under the project's MIT
  license. Mask-ROM contents of a 1980s chip are a grey area; the same is true of every SSI-263 emulation. Note it in
  the manual's credits; drop or replace it if a rights holder ever asks.
- **Slinky RAM card.** With the Phasor in slot 4, `$C0C0–$C0C3` no longer reach the Slinky card. Nobody is known to use
  EmuBuddy's Slinky card with a Mockingboard in slot 4; check that the IIgs Control Panel's RAM disk is not affected.
- **IIgs and 8-bit software.** Nearly all speech and Phasor software is for 8-bit Apple IIs and runs under ProDOS 8 or
  DOS 3.3 on the IIgs; as for the Mockingboard today, slot 4 must be set to "Your Card" in the Control Panel.

## 5. Size

| Phase | Size |
|---|---|
| 0 references | small |
| 1 SSI-263 | medium |
| 2 Phasor | medium |
| 3 settings and documents | small |
| 4 SC-01 | small–medium, optional |

## 6. Sources

- SSI-263 chip model: https://github.com/tgeczy/ssi263-speech (MIT; `src/csrc/ssi263.c`, `ssi263.h`, `docs/engine.md`)
- AppleWin: https://github.com/AppleWin/AppleWin, `source/Mockingboard.cpp`, `SSI263.cpp`, `MockingboardDefs.h`
  (GPL 2 or later) — the hardware facts in section 2
- mb-audit: https://github.com/tomcw/mb-audit (test disk, releases)
- MAME: `votrax.cpp` (BSD-3-Clause), the Mockingboard and Phasor devices, for reference runs
