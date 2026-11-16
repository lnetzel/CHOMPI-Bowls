# CHOMPI Bowls - Physical Modelling of Tibetan Singing Bowls

A five-voice modal singing-bowl instrument for CHOMPI hardware, with strikes, sustained rimming pads, split play mode, stereo reverb, presets, SD-card recording/playback, and MIDI input/output (DIN + USB).

The firmware uses the BOOT_SRAM build setup and STM32H750 memory layout. It does not include battery management.

## Controls

Sound parameters and the active play mode are saved only when you
explicitly save a preset. Parameter bars are temporary ~500 ms overlays
on the 15 white keys; with no bar up, held keys light white, ringing
voices glow dim amber, and latched notes glow yellow.

| Control | Click | Turn | SHIFT + Turn | Indication |
|---|---|---|---|---|
| **Encoder 1** - page 1 (default) | Next page | Per-bowl volume of the selected bowl\* (16 = unity, 32 = +6 dB; rides ringing voices live) | Pan the selected bowl left/right (live) | LED2 green; green bar / cyan pan bar from white key 8 |
| **Encoder 1** - page 2 | Next page | Resonance character: bowl profile 1-5 (two detent ticks per step) | - | LED2 orange; five 3-key orange sections |
| **Encoder 1** - page 3 | Next page (wraps to 1) | Set the selected bowl's fundamental directly in Hz; normal turn ~= 1%, SHIFT+turn = 1 Hz; applies at next strike/selection | 1 Hz finer adjustment | LED2 violet; logarithmic bar from 40 to 10,000 Hz |
| **Encoder 2** | - | Strike hardness (global; soft felt -> hard mallet) | Brightness of the selected bowl\* (high-mode content, live; also brightens rimming) | LED3 amber; amber bar / yellow-white bar |
| **Encoder 3** | - | Decay of the selected bowl\*: 0.25x .. 3x profile decay (live; tick 16 = 0.87x) | - | LED4 blue; blue bar |
| **Encoder 4** | - | Shimmer: paired-mode beating 0 .. 2x (16 = profile detune; live) | - | LED5 purple; centered purple bar |
| **Transport wheel** | - | Rim the current mode's wheel set (see Play modes): turn speed = loudness; **clockwise = friction texture, counter-clockwise = clean sustained tone**; the last direction sticks per key while the wheel is still. The wheel never starts a strike and never revives a stolen note | - | Wheel LEDs (LED6/LED7) follow the rub level |
| **Volume knob** | Switch page | Page 1: master volume. Page 2: delay mix/feedback. Page 3: temple-room reverb dry/wet mix | Page 2: delay time (40-1000 ms; factory default 500 ms) | LED10 follows the page (green->amber->red / yellow / pale ice); keybar shows the selected delay-time position |
| **SHIFT** (KEY_26) | Hold + lower F#/G#/A# = play mode (below); hold + Black 6 (C#4) / Black 7 (D#4) = audio routing (below); hold + a white key = load that preset slot | - | - | While held, every saved slot glows white to show it can be loaded; empty slots stay off (all white keys glow red if no card is available). The active mode's shortcut key glows blue and the active routing's selection key glows pink. Preset shortcuts glow red (erase), green (copy), and blue (save); only the active shortcut stays lit during selection |
| **Preset save** | SHIFT + Black A#4 enters save selection; choose a white-key slot, then release and press SHIFT to confirm. Press Black A#4 to cancel. | - | - | Empty slots blink dim grey; filled slots are purple; selected slot is blue; save animation is purple |
| **Preset copy** | SHIFT + Black G#4 enters copy; choose a filled source, then a different destination, release and press SHIFT to confirm. Press Black G#4 to cancel. | - | - | Source is green; selected destination is blue |
| **Preset erase** | SHIFT + Black F#4 enters erase selection; choose a filled slot, then release and press SHIFT to confirm. Press Black F#4 to cancel. | - | - | Selected slot is red |
| **PLAY** (KEY_27) | In Strikes, tap to toggle the Strike note latch; SHIFT+PLAY cycles its retrigger pattern; in playback, tap to pause/resume; hold alone for 2 s to enter or exit playback | - | - | Strike pattern color is dim while the latch is off and bright while on: blue = Duration Loop, green = Bottom Pair, orange = Top Pair, purple = Middle Pair. Blue in playback, blinking while playing |
| **LOOP** (KEY_28) | Tap to toggle the Rimming note latch (retained in Strikes, cleared on entry to Split); hold with PLAY for recording gestures | - | - | Yellow for the Rimming latch; blue while take looping is enabled |

\* The selected bowl = the last played/selected key **in the current
mode**. Pan, bowl volume, direct fundamental frequency, resonance
character, brightness and decay are stored independently for each key in
each mode (75 editable key/mode settings in total); master volume,
strike hardness, shimmer and reverb mix are global. The mode toggle
(SW_TOG) is reserved.

### Play modes (keyboard)

The instrument has **five shared voices**; a sixth simultaneous note
steals the quietest unprotected voice. Latched Rimming and Strike voices
are protected in Strikes; if all five voices are protected, incoming
strikes are dropped rather than stealing a latch.
Switching modes never interrupts ringing voices or the reverb: outgoing
notes keep their sound, and the wheel only drives notes started in the
current mode. Black keys are playable everywhere.

- **Strikes** (SHIFT + lower G#): all 25 keys strike distinct,
  ascending bowls (99 Hz .. 1560 Hz). Tap **PLAY** to toggle the Strike
  latch: enabling it captures held strikes, and later key presses toggle
  individual notes on/off. A latched bowl retriggers when its resonator
  tail ends, so shorter decay settings produce more frequent strikes.
  **SHIFT+PLAY** cycles Duration Loop (blue), Bottom Pair (green), Top
  Pair (orange), and Middle Pair (purple). The PLAY LED uses a dim color
  when the latch is off and a bright color when it is on. Latched voices
  are protected from stealing. The wheel rims currently sounding,
  physically held strikes.
- **Rimming** (SHIFT + lower A#): all 25 keys hold clean, sustained
  bowl pads - 10 s attack to ~90%, 10 s release to ~10%; the bowl's
  ring and the reverb can ring out longer. The wheel adds the
  direction-dependent rimming on top. **LOOP** toggles a note latch:
  enabling it captures the held sounding pads; a key press then toggles
  that note on or off (key-up never releases a latched note). Switching
  to Strikes preserves the latch, pad gates and yellow key feedback;
  returning to Rimming resumes them. Disabling the latch or switching
  to Split smoothly releases latched-only notes. The latch is never saved.
- **Split** (SHIFT + lower F#): the original layout - keys C3-B3 select
  one of the 12 bowls for wheel rimming (up to two held at once; never
  strike), keys C4-B4 strike the same 12 bowls, and key C5 strikes an
  added higher bowl (1240.8 Hz).

A mode shortcut consumes its key through release - it never sounds a
note or moves the bowl editor target. White Keys 1-15 map left to right
to `BOWLS/P01.json` through `BOWLS/P15.json`. SHIFT+white-key loads a
populated slot; an empty slot leaves the current sound unchanged. At
boot, the embedded `code/state examples/default_slendro_inspired.json`
template is loaded as the factory sound and the 15 saved slots are cached, but no slot is
automatically loaded or created. Parameter changes remain in RAM until
you explicitly save; copying and saving also write only their selected
destination slot.
The original `code/state examples/default.json` remains available as the
untuned example template. The separate
`code/state examples/default_pelog_selisir_inspired.json` file provides an
approximate five-tone Balinese Pelog Selisir-inspired tuning.

Slots written by firmware affected by the disabled float-printf issue
contain invalid `pitch_hz` values. On load, their other settings are
preserved and the factory pitch tables are used; explicitly saving the
slot again rewrites it as valid JSON.

## Direct-Hz tuning and preset editing

Encoder 1 Page 3 sets the selected mode/key's final fundamental directly
in Hz. Normal turns change it by about 1% (minimum 1 Hz); SHIFT+turn
changes it by exactly 1 Hz. The range is 40-10,000 Hz. The LED bar maps
that range logarithmically, so it indicates approximate position rather
than displaying an exact number. Changes apply at the next strike, pad
start, or rim selection. MIDI CC 76 is unsupported; tuning is adjusted
from the panel or by editing a saved preset.

Final frequencies are stored in the `pitch_hz` object in each v11 preset.
The per-key settings (`pan`, `bvol`, `profile`, `bright`, and `decay`) use
the same mode-keyed layout: `strikes`, `rimming`, and `split` arrays of 25
keys each. To edit a preset, save a slot, power off, and edit its
`BOWLS/Pxx.json` file on a computer. Keep all v11 fields and array lengths
intact. Safely eject
the card before reinserting it, then reboot. The saved slots are read at
boot; the embedded factory template remains the boot sound until you
explicitly load a slot with SHIFT+white key. Never edit the card while
the instrument is on. Earlier schema versions are rejected and left
untouched; save a new-format preset explicitly to replace an old slot.

`strikes`, `rimming`, and `split` each contain 25 final frequencies,
indexed by key from C3 (index 0) through C5 (index 24). Split rim and
strike keys start at equal factory frequencies for each pair, but are
independently tunable and saved. Keys in order are:

| Index | Key | Index | Key | Index | Key |
|---:|---|---:|---|---:|---|
| 0 | C3 | 9 | A3 | 18 | F#4 |
| 1 | C#3 | 10 | A#3 | 19 | G4 |
| 2 | D3 | 11 | B3 | 20 | G#4 |
| 3 | D#3 | 12 | C4 | 21 | A4 |
| 4 | E3 | 13 | C#4 | 22 | A#4 |
| 5 | F3 | 14 | D4 | 23 | B4 |
| 6 | F#3 | 15 | D#4 | 24 | C5 |
| 7 | G3 | 16 | E4 |  |  |
| 8 | G#3 | 17 | F4 |  |  |

The engine accepts duplicate pitches and any key ordering. Frequencies
outside 40-10,000 Hz, missing tables, wrong array lengths, and unsupported
schemas make that slot unavailable; factory defaults remain active until
a valid slot is loaded.

### SD recording and playback

- Hold **PLAY + LOOP** for 2 s to arm a recording. A 4 s countdown follows while performance continues; a fresh press of either key cancels it. Recording captures at -6 dB before master volume - always the signal you hear in the headphones: the stereo Bowl bus including reverb in Normal routing, or the AUX IN return from the external effects unit in Send FX routing.
- While recording, hold **PLAY + LOOP** for 1 s to stop cleanly. The file is finalized after queued audio is written.
- Takes are saved without overwriting as `BOWLS/RECORDINGS/Recording NN.wav` (numbered 01-999), 48 kHz stereo 16-bit PCM WAV. Preset saves and recording/playback file work are serialized.
- Hold **PLAY** alone for 2 s to enter playback. This releases held pad gates, scans for the newest 15 takes, and temporarily selects Normal output routing. Hold PLAY alone for 2 s again to exit and restore the previous routing.
- In playback, tap **PLAY** to pause/resume, tap **LOOP** to repeat the selected take, and press a populated white-key slot to select a take. The newest take is the leftmost slot; takes continue left to right. At the end, playback advances to the next take.
- Recording arm fills the keybed red; the countdown turns its first four keys from white to red. Playback uses dim white for available takes and blue for the selection; the selection blinks while playing. SHIFT blinks red while recording and flashes rapidly after a recorder error. A missing card, empty library, invalid take, or player error is shown in red on the keybed.

### Stereo delay

The volume knob cycles **Master -> Delay -> Reverb**. On the Delay page, turning the encoder moves a combined control: the first half raises the wet mix from 0 to 50% at 35% feedback; the second half holds mix at 50% and raises feedback to a bounded 120%, entering self-oscillation above 100%. Hold SHIFT and turn to change delay time from 40 to 1000 ms; changes glide smoothly without clearing echoes. With incoming MIDI Clock, this control selects a clock-pulse divider referenced to the existing delay-time curve at 120 BPM. The latched Strike arpeggio also follows MIDI Clock, and its wheel adjusts the number of pulses between strikes. Clock sync expires after 500 ms without a pulse. Without clock, the delay and arpeggio keep their usual free-running settings. The delay runs at 0% mix so history survives later mix changes and preset loads. Delay positions are included in each v11 preset.

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
SD recording in Send FX captures the AUX IN return, so a take records
the processed sound you hear.
While SHIFT is held, the active routing's selection key glows pink
(Black 6 = Normal, Black 7 = Send FX), next to the blue mode-shortcut
key; with SHIFT up the keyboard shows only the playing state.
Switching is instantaneous, like flipping a hardware routing switch -
a brief click is possible when switching mid-note. The routing combo
is consumed before note handling: it never sounds a bowl.

### MIDI input and output

The instrument plays from an external keyboard, sequencer, DAW or MIDI
controller over the hardware MIDI input (DIN) and the USB MIDI device
port (USB-C), simultaneously, on fixed channel 1 - with the same
musical behavior as the local keyboard (all three play modes, velocity,
sustain pedal, and MIDI rimming on CC 1). Every saved sound parameter
has a CC, and a CC 81/82 selector scheme reaches each of the 75
per-bowl slots without playing a note. The physical keybed also sends
notes 48-72 at fixed velocity 127 to both MIDI outputs; incoming MIDI
is never echoed, and no CCs are sent. USB MIDI output is disabled in
CPU_METER diagnostic builds. The full note map, CC table, value
conversions and examples live in [midi.md](midi.md).

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