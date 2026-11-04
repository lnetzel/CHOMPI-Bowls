# CHOMPI Bowls - Physical Modelling of Tibetan Singing Bowls

A small starting point for a new CHOMPI firmware. This is not a copy of TAPE: it contains only the BOOT_SRAM build setup, the STM32H750 memory layout, and a four-channel audio pass-through smoke test.

The example initializes the Daisy Seed and CHOMPI's external SAI codec, then routes each input channel to the matching output channel. It does not include instrument behavior, controls, LEDs, MIDI, SD-card support, presets, or battery management.

## Controls

All parameters autosave to BOWLS/state.json on the SD card. Parameter
bars are temporary ~500 ms overlays on the 15 white keys; with no bar
up, held keys light white and ringing voices glow dim amber.

| Control | Click | Turn | SHIFT + Turn | Indication |
|---|---|---|---|---|
| **Encoder 1** - page 1 (default) | Next page | Per-bowl volume of the selected bowl\* (16 = unity, 32 = +6 dB; rides ringing voices live) | Pan the selected bowl left/right (live) | LED2 green; green bar / cyan pan bar from white key 8 |
| **Encoder 1** - page 2 | Next page | Resonance character: bowl profile 1-5 (two detent ticks per step) | - | LED2 orange; five 3-key orange sections |
| **Encoder 1** - page 3 | Next page (wraps to 1) | Per-bowl tuning of the selected bowl\* (16 = unison, 0 = 1/2x, 32 = 2x; next strike) | - | LED2 violet; violet bar from white key 8 |
| **Encoder 2** | - | Strike hardness (soft felt -> hard mallet) | Brightness (high-mode content, live; also brightens rimming) | LED3 amber; amber bar / yellow-white bar |
| **Encoder 3** | - | Decay: ring time 0.25x .. 2x (live) | - | LED4 blue; blue bar |
| **Encoder 4** | - | Shimmer: paired-mode beating 0 .. 2x (16 = profile detune; live) | - | LED5 purple; centered purple bar |
| **Transport wheel** | - (unused in v1) | Rim the selected bowl(s): turn speed = loudness; **clockwise = friction texture, counter-clockwise = clean sustained tone**; the last direction sticks per bowl while the wheel is still | - | Wheel LEDs (LED6/LED7) follow the rub level |
| **Volume knob** | Switch page | Page 1: master volume. Page 2: temple-room reverb dry/wet mix | - | LED10 follows the page (green->amber->red gradient / pale ice) |

\* The selected bowl = the last pressed rim or strike key.

### Keyboard

- Keys C3-B3 (bottom octave): rim zone - select a bowl for wheel
  rimming (up to two held at once); never strikes.
- Keys C4-B4: strike zone - strikes the same 12 bowls.
- Key C5: unassigned (silent). Black keys are playable in both zones.
- SHIFT (KEY_26) is a modifier only; PLAY, LOOP and the mode toggle
  are reserved in v1.

## Bowls and profiles

The 12 bowls (bowl_scale.h): the rim zone selects, the strike zone
strikes the same bowl. Ratios are deliberately unequal - the bowls are
NOT tuned to a chromatic scale; "nearest note" is just orientation.
Per-bowl tuning (encoder 1 page 3) adjusts each bowl +/-1 octave.

| Bowl | Fundamental | Nearest note | Keys (rim / strike) | Origin |
|---:|---:|---|---|---|
| 1 | 99.0 Hz | ~G2 | C3 / C4 | measured (23 cm bowl) |
| 2 | 146.0 Hz | ~D3 | C#3 / C#4 | design choice |
| 3 | 219.6 Hz | ~A3 | D3 / D4 | measured |
| 4 | 310.2 Hz | ~D#4 | D#3 / D#4 | measured |
| 5 | 390.0 Hz | ~G4 | E3 / E4 | design choice |
| 6 | 465.0 Hz | ~A#4 | F3 / F4 | measured (12 cm bowl) |
| 7 | 513.0 Hz | ~C5 | F#3 / F#4 | measured |
| 8 | 620.4 Hz | ~D#5 | G3 / G4 | 310.2 x 2 |
| 9 | 693.6 Hz | ~F5 | G#3 / G#4 | 86.7 x 8 |
| 10 | 780.0 Hz | ~G5 | A3 / A4 | 390 x 2 |
| 11 | 927.0 Hz | ~A#5 | A#3 / A#4 | measured (8 cm bowl) |
| 12 | 1026.0 Hz | ~C6 | B3 / B4 | 513 x 2 |

The five resonance characters (encoder 1, page 2; bowl_profile.h):

| # | Name | Character |
|---|---|---|
| 1 | Temple | The measured 180 mm reference bowl (ratios 1 / 2.8 / 5.2 / 8.1 / ...), long balanced ring |
| 2 | Deep | Large bowl: tighter ratios, slow beating, longest ring |
| 3 | Bright | Small bowl: wider ratios, quicker decay, strong highs |
| 4 | Wide | Pronounced paired-mode beating (shimmer) at every mode |
| 5 | Thin | Small high bowl: short, brilliant decay |

## Toolchain

Use GNU Arm Embedded Toolchain **10.3-2021.10** (GCC 10.3.1), plus `make`. This matches TAPE's known-good toolchain. Newer GCC versions can exceed the firmware's SRAM_EXEC link region. This starter does not use TEMPO's GCC 13.3 toolchain.

## Dependencies

In the new firmware repository, add the library repositories as submodules:

```powershell
git submodule add https://github.com/electro-smith/libDaisy.git code/libs/libDaisy
git submodule add https://github.com/electro-smith/DaisySP.git code/libs/DaisySP
```

Then pin both submodules to revisions that you have built and tested. The template intentionally does not vendor or modify either library.

## Build

After adding the library submodules, build both libraries and the firmware from an MSYS2 Bash shell. On this Windows setup, prepend ARM GNU 10.3 to `PATH`:

```sh
export PATH="/c/armgnu/10.3-2021.10/bin:$PATH"
make -C code/libs/libDaisy
make -C code/libs/DaisySP
make -C code/src -j4
```

The BOOT_SRAM binary is written to `code/src/build/CHOMPI_STARTER.bin`. For a new project, change `TARGET` in the Makefile and replace or extend `chompi_main.cpp`.

## Starting a new firmware

Use this directory as the root of a separate GitHub repository, enable **Template repository** in that repository's settings, then create new firmware repositories with **Use this template**. Keep the custom linker script unless you have revalidated the BOOT_SRAM memory layout and linker headroom for your firmware.

The CHOMPI name and marks are not covered by the software license. Rename your firmware and avoid implying it is an official CHOMPI Club release; see [TRADEMARKS.md](TRADEMARKS.md).