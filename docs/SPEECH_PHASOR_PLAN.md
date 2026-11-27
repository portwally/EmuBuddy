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

### Phase 0: references and test disks (small)

- Test disk: Tom Charlesworth's **mb-audit** (v1.61, `.po`, from its GitHub releases): it checks the VIAs, AYs, SSI-263
  and SC-01 detection, interrupts and the Phasor, card by card, on real hardware. Run it on the Mockingboard as it is
  today to see what already passes.
- Programs that speak through the SSI-263, and Phasor music: collect a few from the Asimov archive (Mockingboard and
  Phasor utility/demo disks, games that list Mockingboard speech) and record what they do.
- Reference emulator on the Mac: MAME (`/Applications/Emulatoren/mame`) has the Mockingboard Sound/Speech I (with an SSI-263, `ssi263hle`), the Phasor and the Echo Plus as IIgs slot cards; run them headless as in
  the SCSI work, with the same disks, and record the sound (`-wavwrite`).
- Read the chip model's `docs/engine.md` and build `ssi263.c` alone; measure its CPU cost per second of speech.

### Phase 1: the SSI-263 on the Mockingboard C (medium)

- `emubuddy/src/ssi263/` with the MIT files; one chip object per socket (`ssi263_new` at the emulator's sound rate).
- Writes to `$C420–$C427` and `$C440–$C447` (and their mirrors per AppleWin) go to the chip, as well as to the VIA.
- Time: the chip advances with emulated time, not with the host's audio. On each register write and each sound
  buffer, run it up to the current cycle; its output goes into the mix. `ssi263_request()` drives the VIA's CA1
  edge; the VIA's interrupt logic (already there) does the rest. At high emulated speeds the speech must still keep its
  own pace (it is tied to emulated time, so it follows the "MHz" setting as the AYs do; check what this does at
  Unlimited).
- Mixing: speech on both channels (or chip 1 left, chip 2 right, as the card's outputs are wired; check with MAME).
- Save state, reset (CTL behaviour above), and the "Speech chips" setting.
- Done when: mb-audit's SSI-263 tests pass and a speaking program says its words in EmuBuddy as in MAME.

### Phase 2: the Phasor (medium)

- Card type in slot 4: Mockingboard or Phasor (`g_slot4_mockingboard`: 0, 1 Mockingboard, 2 Phasor).
- Each pair gets a second AY; the port-B chip selects, the address decoding of the three modes, the doubled clock in
  native mode, and `$C0C0–$C0CF` for the mode switch (Slinky off while the Phasor is in).
- `sound.c` mixes four AYs (twelve channels), first VIA's AYs left, second's right.
- Speech: the same chips as phase 1; readable in native mode; A/R also on the IRQ line.
- Done when: mb-audit's Phasor tests pass and Phasor music plays with all four chips.

### Phase 3: settings, Launcher and documents (small)

- Slots window slot 4: Empty / Mockingboard / Phasor / Uthernet II, and the speech-chip setting; CLI `-sl4 phasor`,
  `-mbspeech 0|1|2`; Launcher keys; README, `docs/CLI.md`, the manual (credit for the chip model), PDF rebuilt.

### Phase 4: Votrax SC-01 (small–medium, optional)

- The original Mockingboard speech chip, on the first VIA's port B with its own A/R on CB1 (AppleWin's notes from
  Crimewave). A port of MAME's `votrax.cpp` (BSD-3) in C. Only if programs that need it turn up in phase 0.

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
