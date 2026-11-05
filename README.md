# CHOMPI Bowls - Physical Modelling of Tibetan Singing Bowls

A small starting point for a new CHOMPI firmware. This is not a copy of TAPE: it contains only the BOOT_SRAM build setup, the STM32H750 memory layout, and a four-channel audio pass-through smoke test.

The example initializes the Daisy Seed and CHOMPI's external SAI codec, then routes each input channel to the matching output channel. It does not include instrument behavior, controls, LEDs, MIDI, SD-card support, presets, or battery management.

## Controls

All sound parameters and the active play mode autosave to
BOWLS/state.json on the SD card. Parameter bars are temporary ~500 ms
overlays on the 15 white keys; with no bar up, held keys light white,
ringing voices glow dim amber, and LOOP-latched notes glow yellow.

| Control | Click | Turn | SHIFT + Turn | Indication |
|---|---|---|---|---|
| **Encoder 1** - page 1 (default) | Next page | Per-bowl volume of the selected bowl\* (16 = unity, 32 = +6 dB; rides ringing voices live) | Pan the selected bowl left/right (live) | LED2 green; green bar / cyan pan bar from white key 8 |
| **Encoder 1** - page 2 | Next page | Resonance character: bowl profile 1-5 (two detent ticks per step) | - | LED2 orange; five 3-key orange sections |
| **Encoder 1** - page 3 | Next page (wraps to 1) | Per-bowl tuning of the selected bowl\* (16 = unison, 0 = 1/2x, 32 = 2x; next strike/selection) | - | LED2 violet; violet bar from white key 8 |
| **Encoder 2** | - | Strike hardness (global; soft felt -> hard mallet) | Brightness of the selected bowl\* (high-mode content, live; also brightens rimming) | LED3 amber; amber bar / yellow-white bar |
| **Encoder 3** | - | Decay of the selected bowl\*: ring time 0.25x .. 2x (live) | - | LED4 blue; blue bar |
| **Encoder 4** | - | Shimmer: paired-mode beating 0 .. 2x (16 = profile detune; live) | - | LED5 purple; centered purple bar |
| **Transport wheel** | - | Rim the current mode's wheel set (see Play modes): turn speed = loudness; **clockwise = friction texture, counter-clockwise = clean sustained tone**; the last direction sticks per key while the wheel is still. The wheel never starts a strike and never revives a stolen note | - | Wheel LEDs (LED6/LED7) follow the rub level |
| **Volume knob** | Switch page | Page 1: master volume. Page 2: temple-room reverb dry/wet mix | - | LED10 follows the page (green->amber->red gradient / pale ice) |
| **SHIFT** (KEY_26) | Hold + lower F#/G#/A# = play mode (below); hold + Black 6 (C#4) / Black 7 (D#4) = audio routing (below) | - | - | While held, the active mode's shortcut key glows blue and the active routing's selection key glows pink |
| **LOOP** (KEY_28) | Toggle the Rimming note latch (Rimming mode only) | - | - | LOOP LED and latched keys glow the same yellow while the latch is enabled |

\* The selected bowl = the last played/selected key **in the current
mode**. Pan, bowl volume, tuning, resonance character, brightness and
decay are stored independently for each key in each mode (75 editable
bowls in total); master volume, strike hardness, shimmer and reverb
mix are global. PLAY (KEY_27) and the mode toggle (SW_TOG) are
reserved.

### Play modes (keyboard)

The instrument has **five shared voices**; a sixth simultaneous note
steals the quietest voice (a stolen latched note is simply unlatched).
Switching modes never interrupts ringing voices or the reverb: outgoing
notes keep their sound, and the wheel only drives notes started in the
current mode. Black keys are playable everywhere.

- **Strikes** (SHIFT + lower F#): all 25 keys strike distinct,
  ascending bowls (99 Hz .. 1560 Hz). The wheel rims the currently
  sounding, physically held strikes.
- **Rimming** (SHIFT + lower G#): all 25 keys hold clean, sustained
  bowl pads - 10 s attack to ~90%, 10 s release to ~10%; the bowl's
  ring and the reverb can ring out longer. The wheel adds the
  direction-dependent rimming on top. **LOOP** toggles a note latch:
  enabling it captures the held sounding pads; a key press then toggles
  that note on or off (key-up never releases a latched note); disabling
  latch (or leaving Rimming) smoothly releases the latched-only notes.
  The latch is never saved.
- **Split** (SHIFT + lower A#): the original layout - keys C3-B3 select
  one of the 12 bowls for wheel rimming (up to two held at once; never
  strike), keys C4-B4 strike the same 12 bowls, and key C5 strikes an
  added higher bowl (1240.8 Hz).

A mode shortcut consumes its key through release - it never sounds a
note or moves the bowl editor target. With no SD card (or an
incompatible old state file) the instrument boots factory settings in
**Strikes**; the first successful save replaces the old file (old
saved settings are not imported).

### Audio routing (send-FX loop)

Two selectable output routings let the instrument run through an
external stereo effects unit without repatching. Selection is
session-only: the instrument boots to Normal and the routing is never
saved to the SD state.

- **Normal** (SHIFT + Black 6, C#4; boot default): the bowl bus x
  master volume goes to both the headphones and the master line out
  (outputs 2/3). AUX IN is not monitored.
- **Send FX** (SHIFT + Black 7, D#4): the line out carries only the
  bowl bus at a fixed level (pre-master-gain - the volume knob does
  NOT change the send), as a send to the external effects unit; the
  headphones carry only the AUX IN return, with the volume knob as
  the return level. The return never leaves through the line out, so
  a feedback loop is impossible. With nothing patched into AUX IN the
  headphones are silent - by design: the line out keeps sending the
  dry bowls.

Patch: line out -> FX input, FX output (set 100% wet) -> AUX IN.
While SHIFT is held, the active routing's selection key glows pink
(Black 6 = Normal, Black 7 = Send FX), next to the blue mode-shortcut
key; with SHIFT up the keyboard shows only the playing state.
Switching is instantaneous, like flipping a hardware routing switch -
a brief click is possible when switching mid-note. The routing combo
is consumed before note handling: it never sounds a bowl.

## Bowls and profiles

Split mode's 12 bowls (bowl_scale.h): the rim zone selects, the strike
zone strikes the same bowl, C5 strikes 1240.8 Hz. The full-key modes
share one 25-pitch table (99 .. 1560 Hz) that keeps the
measured/reference anchors and interpolates the in-between pitches in
frequency ratio - starting points, tuned by ear; they are NOT 25
measured bowls. Ratios are deliberately unequal - the bowls are NOT
tuned to a chromatic scale; "nearest note" is just orientation.
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