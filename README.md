# CHOMPI-BOWLS

A five-voice singing-bowl instrument for CHOMPI hardware. It includes
three play modes, per-key tuning and sound controls, presets, MIDI, stereo
delay and reverb, and SD-card recording/playback.

## Controls

| Control | Action |
|---|---|
| Encoder 1 | Press to change page. Page 1: selected bowl volume; SHIFT+turn pans it. Page 2: resonance profile. Page 3: fundamental frequency. |
| Encoder 2 | Page 1: selected bowl brightness. |
| Encoder 3 | Selected bowl decay. |
| Encoder 4 | Press to change page. Page 1: strike hardness; page 2: paired-mode shimmer. |
| Transport wheel | Rims the active wheel set. Clockwise adds friction texture; counter-clockwise gives a clean sustained tone. |
| Volume encoder | Press to select master volume, delay, or reverb. On the delay page, SHIFT+turn changes time. |
| SHIFT + lower F# / G# / A# | Select Split / Strikes / Rimming. |
| SHIFT + Black 6 / Black 7 | Select Normal / Send FX routing. |
| SHIFT + white key | Load that preset slot. |
| PLAY | In Strikes, toggles the strike latch; SHIFT+PLAY changes its pattern. Hold for 2 s to enter or exit playback. |
| LOOP | Toggles the Rimming note latch. Hold with PLAY to arm or stop recording. |

Encoder 1 selects the key being edited. Per-key settings are independent
for every key in each mode. The encoder bars appear briefly after a turn;
with no bar displayed, held keys and ringing voices show the current play
state.

## Play modes

- **Strikes:** Every key strikes a different bowl. PLAY latches notes;
  SHIFT+PLAY selects the latch's retrigger pattern.
- **Rimming:** Every key opens a sustained pad. The wheel adds
  direction-dependent rimming. LOOP latches notes.
- **Split:** C3-B3 select 12 bowls for wheel rimming; C4-B4 strike those
  bowls, and C5 strikes the highest bowl.

Five voices are shared across modes. Mode changes do not stop ringing
voices. Black keys are playable in all modes.

## Presets and tuning

The firmware loads the embedded
[`default_slendro_inspired.json`](code/state%20examples/default_slendro_inspired.json)
factory preset at boot. Saved slots on the SD card are not loaded
automatically. White keys 1-15 map to `BOWLS/P01.json` through
`BOWLS/P15.json`; save, copy, and erase use SHIFT with Black A#4, G#4,
and F#4 respectively. Confirm a selection by releasing and pressing
SHIFT.

Changes remain in memory until saved. Presets use schema v12; firmware
continues to load existing v11 files with empty name and comment metadata.
Each per-key setting is stored as three arrays of 25 values (`strikes`,
`rimming`, `split`), ordered from C3 to C5. To edit a file, save the
slot, power off, edit the JSON, safely eject the card, and reboot.

Encoder 1 Page 3 tunes a key's fundamental from 40 to 10,000 Hz. Turn
changes it by about 1%; SHIFT+turn changes it by 1 Hz. The new pitch
applies when the bowl is next struck, started as a pad, or selected for
rimming. The panel and preset JSON are the tuning controls.

Example factory tunings are in [`code/state examples/`](code/state%20examples/).
The resonance profiles are Temple, Deep, Bright, Wide, and Thin.

The static [web preset editor](web/README.md) can edit saved SD-card slots
over USB MIDI and runs locally or on GitHub Pages.

## Recording, playback, and effects

- Hold PLAY+LOOP for 2 s to arm recording; hold them for 1 s to stop.
  Takes are written as 48 kHz stereo 16-bit WAV files in
  `BOWLS/RECORDINGS/`.
- Hold PLAY for 2 s to enter or exit playback. In playback, tap PLAY to
  pause/resume, LOOP to repeat, or a white key to select a take.
- The volume encoder cycles master, delay, and reverb pages. Delay time
  is 40-1000 ms and follows MIDI Clock when available.
- **Normal** sends the bowl mix to headphones and line out.
  **Send FX** sends dry bowls to line out and monitors AUX IN through the
  headphones. Patch line out to the external effect and its output to
  AUX IN.

## MIDI

MIDI input and output are available over DIN and USB, on channel 1.
Incoming notes follow the active play mode; local key presses send notes
48-72. Incoming messages are not echoed. See [midi.md](midi.md) for the
note map, CC assignments, and value ranges.

## Build

Build from an MSYS2 UCRT64 shell with GNU Arm Embedded Toolchain
10.3-2021.10 (GCC 10.3.1) and `make`. The Makefile expects libDaisy,
DaisySP, and coreJSON under `code/libs/`. Add them if needed:

```sh
git submodule add --force https://github.com/electro-smith/libDaisy.git code/libs/libDaisy
git submodule add --force https://github.com/electro-smith/DaisySP.git code/libs/DaisySP
git submodule add --force https://github.com/FreeRTOS/coreJSON.git code/libs/coreJSON
```

Pin the submodules to tested revisions, then build the libraries and firmware:

```sh
export PATH="/c/armgnu/10.3-2021.10/bin:$PATH"
make -C code/libs/libDaisy
make -C code/libs/DaisySP
make -C code/src -j4
```

The firmware binary is `code/src/build/CHOMPI_BOWLS_v1.0.bin`.

## Running the web editor

Start by running
```
python -m http.server 8000 --directory web
```



The CHOMPI name and marks are separate from the software license; see
[`TRADEMARKS.md`](TRADEMARKS.md).
