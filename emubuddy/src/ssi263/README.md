# SSI-263 chip model

The speech chip of the Mockingboard (and the Phasor), from Tamas Geczy's **ssi263-speech**
(https://github.com/tgeczy/ssi263-speech, `src/csrc/`, commit afc4ddf6315e), MIT license (`LICENSE`).

These four files are unchanged copies. EmuBuddy uses them through `mbspeech.c`. To update them, copy the same
files from a newer commit and note it here.

- `ssi263.c`, `ssi263.h` -- the chip: registers, A/R request, synthesis
- `ssi263_defaults.h` -- the chip's parameters and its phoneme ROM (read from die photos)
- `ssi263dsp.c` -- the output filter at the host's sample rate
