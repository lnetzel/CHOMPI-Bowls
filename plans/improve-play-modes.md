# Improve Play Modes

Status: **implemented** (2026-11-05). All host suites pass
(`make -C code/tests modes-test` + `test` + `strike-test`; T0 rub
baseline sample-identical). Firmware builds clean; worst-case preset
serialization measured at 964 B incl. terminator (1536 B buffers).
Hardware checklist below remains to be exercised on the device.
Intended repository destination:
`plans/improve-play-modes.md`. Links below are relative to that destination.
Requirements: [improve_play_modes_requirement.md](../requirements/improve_play_modes_requirement.md).

## Problem and goal

The current instrument has a fixed split: 12 rim keys, 12 strike keys, and an
unused highest key. Add selectable **Split**, **Strikes**, and **Rimming**
performance modes without interrupting existing rings or reverb tails. Every
key becomes playable, with independently editable bowl settings for each mode.
Rimming also gains a sustained pad gesture and a LOOP-controlled note latch.

The firmware currently has **five**, not six, shared voices. Keep that
polyphony and the quietest-voice-stealing policy; 25 playable keys does not
mean 25 simultaneously sounding bowls.

## Confirmed decisions

| Area | Decision |
|---|---|
| Split | Keys 0–11 rim the existing 12 bowls; keys 12–23 strike the same 12; key 24 strikes a new higher bowl. Preserve Split's existing wheel selection and fallback behavior. |
| Strikes | All 25 keys strike distinct, ascending bowl pitches. The wheel rims currently sounding, physically held notes. |
| Rimming | All 25 keys produce distinct, ascending clean bowl pads while held; the wheel adds the existing direction-dependent rimming. |
| Pad envelope | Fixed **5 s attack to approximately 90%** and **10 s release to approximately 10%** of the envelope level at release. The resonator and room can ring longer. |
| Latch | LOOP toggles latch in Rimming only. Enabling it captures held, sounding pad notes; a key press toggles its note on or off immediately. Latch is never saved. |
| Latch release | Turning latch off or leaving Rimming smoothly releases latched-only notes; notes still physically held continue until their keys are released. |
| Voice stealing | Keep five shared voices and steal the quietest as today. If a latched note is stolen, automatically clear that note's latch; do not silently relatch or restart it. |
| Mode switching | The outgoing voices keep their sound and reverb. The wheel controls only notes started in the newly selected mode; outgoing wheel excitation fades naturally. |
| Bowl settings | Pan, bowl volume, tuning, and resonance character are independent for each physical key in each mode. Master volume, strike hardness, brightness, decay, shimmer, and reverb mix remain global. |
| Feedback | While SHIFT is held, highlight the selected mode key. Light the LOOP LED when latch is enabled and distinguish latched keys. No new storage-status LED or user-facing alert. |
| Persistence | Save the mode and all sound settings. A fresh install or incompatible old preset starts in **Strikes** with factory settings. No legacy-preset migration is required. |
| Existing file | Continue using `BOWLS/state.json`; the first successful new-format save replaces the old file. Old saved settings are not imported. |

## Recommended approach

### 1. Mode and pitch maps

Add a typed mode enum and fixed 25-key mode maps in
[bowl_scale.h](../code/src/bowl_scale.h), using keyboard pitch-order indices
from [chompi_hardware.h](../code/src/chompi_hardware.h):

| Selection | Shortcut | Behavior |
|---|---|---|
| Strikes | SHIFT + lower F# (key index 6) | Strike on all keys |
| Rimming | SHIFT + lower G# (key index 8) | Sustained pad on all keys |
| Split | SHIFT + lower A# (key index 10) | Rim 0–11; strike 12–24 |

For Split, retain the present 12 frequencies and pairings. Use **1240.8 Hz**,
the next higher frequency retained in the current scale's comments, for the
new highest strike key.

For each full-key mode, use the same 25 distinct, ascending starting
fundamentals, from key 0 to key 24 (Hz):

```text
99.0, 112.7, 128.3, 146.0, 167.3, 191.7, 219.6, 261.0, 310.2,
347.8, 390.0, 425.9, 465.0, 513.0, 564.1, 620.4, 693.6, 780.0,
850.3, 927.0, 1026.0, 1128.3, 1240.8, 1395.0, 1560.0
```

These are **proposed starting values**, not 25 measured bowls or a claim of a
universal singing-bowl scale. Preserve the existing measured/reference
anchors; the added intermediate pitches are design choices, interpolated in
frequency ratio and subject to listening. Keep the existing per-key tuning
control, which scales the complete modal structure. Add compile-time or
host-test checks for table size, valid mappings, and ascending full-mode
pitches.

### 2. Mode-aware keys, voices, and wheel

In [chompi_main.cpp](../code/src/chompi_main.cpp), route mode shortcuts before
musical key events. A shortcut consumes its key press through key release,
even if SHIFT is released first; it must not strike, select, toggle a latched
note, or change the bowl editor target. Selecting the already-active mode is
idempotent. Non-shortcut keys and existing SHIFT+encoder controls continue to
work.

Associate every voice and physical key-down with its **originating mode and
key**. Looking up an existing voice, applying live per-bowl edits, and routing
key-up must use that identity rather than the physical key alone. In
particular, changing modes must not retune, repan, reset, reprofile, or reuse a
still-ringing voice just because the same physical key exists in the new mode.
Keep the shared reverb running. Held outgoing pads retain their original
identity until release; do not reinterpret a held key as a new-mode note.

The mode change itself allocates no voice. A subsequent new note can still
steal the quietest of the five voices; that accepted polyphony limit is
distinct from the no-interruption guarantee for the mode change itself.
Notify the latch state machine when a latched Rimming voice is stolen so only
the stolen note's latch is cleared. A stolen held note is not automatically
restarted; it must be released and played again. Reserve a just-started pad
voice even while its slow attack is still inaudible, rather than treating it
as a free voice.

Keep the current Split wheel rules: at most two held rim-zone keys, otherwise
the last-selected rim key, including the existing reacquisition on wheel
movement. Distinguish that last-selected fallback from the new LOOP latch.
In Strikes, the wheel addresses currently sounding held strike voices; in
Rimming, it addresses currently sounding held or latched pad voices. Full-mode
wheel work is bounded by the five sounding voices. The wheel never creates a
strike or revives a stolen full-mode note. Track wheel level and character by
voice or by mode/key so old and new mode instances cannot affect one another.

On a mode change, stop applying new wheel turns to outgoing-mode notes. Let
their last wheel excitation fall away with the existing smooth release; do not
erase their resonator state. Already ringing notes remain unaffected except for
the explicitly chosen cessation of further outgoing wheel control and, where
applicable, the intentional pad release described below.

### 3. Pad and latch state

Extend [bowl_voice.h](../code/src/bowl_voice.h) with a per-voice clean-pad gate
and smooth envelope. Reuse the existing phase-continuous clean modal
excitation; do not create a second resonator bank or perform trigonometric
calculations per sample. Precompute coefficients so that from silence the
pad-drive envelope reaches 0.90 ± 0.005 after 5 s, and after note-off it
reaches 0.10 ± 0.005 of its note-off level after 10 s. Retrigger from the
current envelope value, not from a reset, and allow the bowl's natural ring
and reverb to decay beyond the excitation envelope.

Keep the wheel and pad drive independent: the pad provides the clean sustained
base, while clockwise and counter-clockwise wheel movement add the existing
friction and clean characters. Mix the drives with a bounded, smooth gain so
combining them cannot double the maximum excitation or introduce a click.
When pad drive is absent, preserve the existing wheel DSP path and its
accepted regression baseline. Include pad gate and release state in idle and
voice-allocation decisions without cutting off low-level resonator tails.

Implement a small, host-testable input/latch state machine, potentially in a
new `code/src/play_modes.h`, separate from hardware I/O:

1. With latch off, key-down opens the pad gate and key-up begins its release.
2. LOOP-on captures the currently held, sounding pad notes. A later key-down
   toggles that key's latch and gate; key-up alone does not release a latched
   note. A second key-down begins release immediately, even though the
   physical key is down.
3. LOOP-off clears latch flags. Previously active notes whose keys are
   physically held remain gated until release; latched-only notes enter the
   10 s release. Do not restart a note already toggled off.
4. Leaving Rimming performs the same latch cleanup and switches the LOOP LED
   off. Preserve physically held outgoing pad notes until their own key-up,
   even after the active mode changes.
5. A stolen latched note is unlatched. LOOP itself remains enabled until
   toggled off or Rimming is left. No latch, held-key, wheel, or envelope state
   is persisted.

### 4. Per-mode bowl settings and persistence

Replace the current single 24-key arrays and global `profile_index` with
three 25-key banks. Each entry contains pan, bowl volume, tuning, and profile.
Encoder 1 keeps its existing pages and controls; its target is the last
musically played/selected key for the current mode, never a shortcut key.
Keep a separate transient edit target per mode, initialized to key 0.

Pan and bowl volume continue to update matching ringing voices smoothly.
Tuning and resonance character take effect on the next strike or rim/pad
selection, as the current excitation-time behavior does. Global parameters
remain global and retain their existing live or next-excitation semantics.
Changing modes alone must not apply another bank's settings to old voices.
The profile becoming per-bowl is an intentional behavior change.

In [BowlsPreset.h](../code/src/BowlsPreset.h), introduce a new schema version
for the active mode, six global controls, and four flattened 75-entry arrays
(`pan`, `bvol`, `tune`, `profile`), indexed consistently by mode and key.
Initialize all entries to the existing defaults. Validate the mode, array
lengths, and value ranges; do not partially apply corrupt or unsupported
state. Deliberately do not import v1–v5 files. On fresh/unsupported state, use
factory defaults and Strikes; after a valid load, restore the saved mode.

The proposed flat schema's worst case is approximately **965 bytes plus the
terminator**, so the current 512-byte buffer is insufficient. Increase the
buffers in [PresetStorage.h](../code/src/PresetStorage.h) to a measured size
with headroom, initially **1536 bytes** each, and test actual worst-case
serialization. Verify multi-sector reads and writes, DMA-safe storage and
alignment, truncation detection, and the staged writer. Keep all filesystem,
serialization, and diagnostics out of the audio callback.

Preserve the 1 s autosave debounce. Capture coherent mode/bank snapshots
without blocking the audio callback, and ensure a newer change made during a
save is eventually written. Check every serialization and filesystem result;
retain unsaved state for retry, including after card reinsertion. Keep the
last committed file recoverable across an interrupted replacement, for
example with a synced temporary file and a checked backup/rename sequence.
Only completed saves can be expected to survive immediate power loss.

As requested, do not add a storage LED or user-facing alert. Expose explicit
storage failure status and emit a non-blocking serial diagnostic once per
failure/recovery rather than treating a failed save as successful. A missing
or failed SD card means edits exist only in RAM until they can be saved.

### 5. LED behavior and documentation

Update the mode-key and latched-key rendering in
[chompi_main.cpp](../code/src/chompi_main.cpp), using the existing mappings in
[leds.h](../code/src/leds.h). While SHIFT is held, make the active lower black
mode key distinguishable without losing existing white-key parameter bars.
Light the LOOP panel LED only while Rimming latch is enabled, and use a
distinct key color for latched notes. Remove an individual latch indication
when its voice is stolen. Keep existing held-key, ring-glow, overlay, wheel,
and CPU-meter behavior otherwise.

Update the control-map comments and [README.md](../README.md) to describe
the three shortcuts, 25-key layouts, wheel behavior, five-voice limit,
5 s/10 s pad, LOOP latch, independent settings, and SD-card persistence.

## Affected files and components

| File/component | Planned changes |
|---|---|
| [bowl_scale.h](../code/src/bowl_scale.h) | Split/full pitch tables, mode/key mappings, bounds |
| [chompi_main.cpp](../code/src/chompi_main.cpp) | Mode routing, voice ownership, wheel targets, banked settings, LEDs, snapshot/apply and control map |
| [bowl_voice.h](../code/src/bowl_voice.h) | Clean pad gate/envelope, bounded wheel mix, active/steal handling |
| `code/src/play_modes.h` | Proposed hardware-independent mode, key, and latch state machine |
| [BowlsPreset.h](../code/src/BowlsPreset.h) | New mode and per-key-bank schema, validation, serialization |
| [PresetStorage.h](../code/src/PresetStorage.h) | Larger DMA-safe buffers, checked multi-sector persistence and recovery |
| [chompi_hardware.h](../code/src/chompi_hardware.h), [leds.h](../code/src/leds.h) | Reuse existing key/button/LED mappings; change only if necessary |
| [code/tests](../code/tests/) | State-machine, preset, DSP, transition, and existing regression tests |
| [README.md](../README.md) | User-facing control and persistence documentation |

## Step-by-step tasks

1. Capture the current Split, wheel-direction, strike, and voice-steal
   behavior as regression baselines; preserve the accepted rimming fixture.
2. Add the mode enum, 25-key pitch maps, per-mode bowl banks, and a
   hardware-independent key/latch state model with exhaustive mapping tests.
3. Add the clean pad envelope and bounded wheel/pad mixing to `BowlVoice`;
   verify slow attack/release, note reuse, voice reservation, and unchanged
   wheel-only DSP.
4. Wire shortcut precedence, mode/key voice identity, note-off ownership,
   wheel targets, latch transitions, and quietest-voice steal notification.
   Implement the requested mode and latch LED feedback.
5. Implement the new preset format, coherent snapshots, appropriately sized
   storage, checked writes and recovery. Default incompatible/absent state to
   Strikes without importing old settings.
6. Add the complete host regressions and update the README and source control
   map. Run the existing rimming and strike suites alongside the new tests.
7. Build using the repository build skill, report SRAM/SRAM_EXEC/SDRAM, profile
   a clean `CPU_METER=1` build, and perform the hardware checklist below.

## Risks and memory/CPU/timing considerations

- **Five-voice limit:** a sixth held or latched pad steals a sounding voice and
  clears that note's latch. Mode switching alone must never cause a steal.
- **Sound continuity:** outgoing wheel input stops following the wheel on a
  mode change, and latched-only pads intentionally release. Those are gradual
  transitions; struck rings, held pads, and the reverb must not be reset.
- **Sound calibration:** the 25-pitch table and clean-pad gain are starting
  values. Listen across registers and profiles; do not present interpolated
  frequencies as measured bowl data.
- **CPU:** controls run every 24 samples at 48 kHz. Keep pad calculations
  bounded, avoid allocation or filesystem work in the callback, and measure
  five simultaneous pads plus wheel, reverb, LEDs, and autosave. Target peak
  callback load below 80%, as in the existing rimming plan; optimize rather
  than silently reducing the agreed five voices.
- **Memory and SD:** four 75-entry setting arrays, pad/latch state, and two
  larger JSON buffers increase SRAM; the 512→1536-byte buffers alone add
  2048 bytes. Measure actual SRAM, SRAM_EXEC, and SDRAM after the build.
  Exercise JSON larger than one 512-byte sector on the device.
- **Persistence boundary:** old presets are deliberately not imported; a new
  successful save replaces the existing state file. Changes not yet committed
  when power is removed may be lost, and storage failures have no visible
  panel warning by request.

## Testing and validation

### Host tests

1. For all 25 keys, assert the three mappings, Split's original 24 pitches,
   the added highest strike, distinct ascending full-mode frequencies, and the
   correct lower F#/G#/A# shortcuts. Test SHIFT/key press and release in both
   orders, same-mode selection, and shortcut suppression of note/latch/edit
   events.
2. Exercise momentary pads, LOOP capture of held notes, toggle-on/off on
   key-down, LOOP-off with a mix of held and latched-only notes, both exits
   from Rimming, key-up after a mode switch, and automatic unlatching on a
   sixth-note steal. Verify no stolen full-mode note spontaneously restarts.
3. Test Split's two-held/last-selected wheel behavior, held-only Strikes
   wheel, held-or-latched Rimming wheel, and current-mode-only wheel behavior
   after a mode switch. Exercise same-key voices in different modes and verify
   that live pan/volume changes only affect the matching mode/key.
4. At 48 kHz and 24-sample blocks, measure the specified 90%/10% pad envelope
   targets, partial attacks, retriggers, long sustain, and releases. Render
   low/middle/high pitches, all profiles, tuning extremes, five pads, and wheel
   directions; require finite, bounded output and no above-Nyquist instability.
   Retain the existing wheel-only baseline and clean/texture regression tests.
5. Compare deterministic stereo renders with and without a passive mode
   change: struck/ringing voices and physically held pads must remain
   sample-identical when no intentional release or wheel change occurs.
   For latched-note and wheel releases, verify envelope continuity and bound
   sample-to-sample jumps against a no-switch release reference, using the
   existing rimming suite's relative discontinuity threshold as a minimum.
6. Round-trip distinct settings for all 75 mode/key slots and all globals;
   verify mode persistence and that latch/performance state is absent. Test
   maximal JSON size including its terminator, malformed/truncated data,
   invalid mode and values, unsupported old versions, and a 512-byte-crossing
   read/write. Simulate write, sync, rename, card-removal/reinsertion, and
   interrupted-replacement failures; verify the last committed state remains
   recoverable and a newer pending snapshot is not lost.
7. Run the existing `make -C code/tests test` and `strike-test` targets plus
   dedicated mode/preset tests added to the same host Makefile. If an accepted
   baseline fixture is missing, report that explicitly rather than treating a
   skipped comparison as a pass.

### Firmware and hardware

1. Build normal (no CPU-meter) firmware via the build skill. Report the
   SRAM, SRAM_EXEC, and SDRAM usage table and measure peak audio callback load
   under the five-voice stress case; confirm no audible dropouts.
2. Play all 25 keys in each mode. Confirm Split's first 24 keys and wheel
   feel unchanged, key 25 strikes, full-mode pitches ascend, Strikes wheel
   rims held notes, and Rimming produces clean slow pads with directional
   wheel texture.
3. Exercise shortcut chords without sounding their keys; verify mode
   highlighting, LOOP and latched-key LEDs, capture/toggle behavior, held
   notes across mode changes, and automatic unlatching on voice steal.
4. Switch modes during strike tails, held pads, latched pads, wheel movement,
   and reverb tails. Listen for clicks, retuning, re-panning, or unexpected
   voice cutoffs, distinguishing normal five-voice steals from mode changes.
5. Give different pan, volume, tuning, and character values to the same key
   in all three modes; save, power-cycle after the save completes, and verify
   each bank and the last mode restore. Confirm latch is off after reboot,
   fresh/incompatible state boots in Strikes, and the first new save replaces
   the incompatible file as agreed.
