# Per-Bowl Direct-Hz Tuning

Status: awaiting approval.

## Problem and goal

Page 3 of Encoder 1 currently stores a 0–32 tuning ratio for each
mode/key. Replace that relative tuning layer with a direct fundamental
frequency in Hz. CHOMPI-BOWLS is a resonant-bowl instrument rather than a
semitone-based melody instrument, so the selected key should sound at its
stored frequency without converting from a musical interval.

Support direct values from **40 to 10,000 Hz**, keep Page 3 editing
per-mode/per-key values, retain application at the next strike/pad
selection, and make the value adjustable across the full range without
requiring thousands of encoder turns.

## Confirmed decisions

| Area | Decision |
|---|---|
| Stored tuning | Store the final fundamental frequency in Hz for each mode/key; remove the separate 0–32 tuning ratio. |
| Range | 40–10,000 Hz inclusive. |
| Normal turn | Change by about 1% of the current frequency per detent, with a minimum 1 Hz step. |
| SHIFT+turn | Change by exactly 1 Hz per detent. |
| Feedback | Keep the existing 15 white-key LEDs and map frequency position logarithmically; no numeric display is added. |
| Split paired keys | Keep independent final frequencies for each rim/strike key. Factory defaults start equal for each pair, as they do now. |
| Existing presets | No backward compatibility or migration. Reject files that do not match the new schema version; do not delete or rewrite them automatically. |
| MIDI CC 76 | Remove tuning support; CC 76 becomes unsupported/ignored. |

## Recommended approach

### 1. Represent direct frequencies for every mode/key

Keep the compiled factory values in
[bowl_scale.h](../code/src/bowl_scale.h) as the defaults. Represent runtime
and saved values as 25 frequencies for each of Strikes, Rimming, and Split.
Expand Split's current 13-entry shared table to 25 key entries: initialize
each rim/strike pair to the same factory Hz value, but allow each key to be
edited and persisted independently.

Make the runtime pitch lookup return the selected mode/key's stored Hz
directly. Remove the ratio multiplication from `KeyFundamental()` and
remove the tuning-ratio helper if no other caller remains. The existing
excitation boundaries stay unchanged: the edited value affects the next
strike, pad start, or rim selection, not an already-ringing voice.

Page 3 edits the selected mode/key's absolute frequency. Normal detents
apply an approximately 1% frequency change, with a 1 Hz minimum; SHIFT
detents apply exactly 1 Hz. Clamp both paths to 40–10,000 Hz, including
encoder increments that report multiple detents at once. Put the step and
logarithmic LED-position calculations in host-testable helpers rather than
leaving frequency arithmetic embedded in the hardware control loop.

Because Page 3 now changes an audio-visible value while audio is running,
publish the runtime value through a bounded 32-bit atomic representation
(for example, millihertz). Convert to/from Hz at the preset and excitation
boundaries; do not write a plain float table concurrently with audio
reads.

### 2. Update preset schema and factory template

Bump [BowlsPreset.h](../code/src/BowlsPreset.h) to a new schema version.
Remove the `tune` tick bank and serialize exactly 25 `pitch_hz` values for
each mode, including 25 Split values. Keep strict transactional parsing,
the existing 40–10,000 Hz bounds, and the current three-decimal
serialization precision.

Update the embedded
[default.json](../code/state%20examples/default.json) template to the new
schema and seed all mode/key frequencies from the factory tables. Ensure
the storage JSON capacity is measured against the complete new document;
change [PresetStorage.h](../code/src/PresetStorage.h) only if the existing
buffer no longer fits.

The current parser already rejects schema-version mismatches. Keep that
behavior: an old preset slot is reported as incompatible/empty and left
untouched. Do not migrate its base pitch and tune ratio, and do not write
over it except through an explicit save to that slot.

### 3. Rework Page 3 and remove MIDI ratio tuning

In [chompi_main.cpp](../code/src/chompi_main.cpp), route Page 3 turns to
the selected mode/key frequency. Preserve the current encoder page cycle,
selected-key behavior, overlay timing, and next-excitation application.
Replace the center/unison tuning bar with a logarithmic 40–10,000 Hz
position indicator across the 15 white-key LEDs. Keep the existing
frequency range endpoints visible at the ends of the bar.

Remove CC 76 from
[MidiCcMap.h](../code/src/MidiCcMap.h) and its `ApplyMidiCc()` handler.
Unsupported-CC filtering should then ignore it without queuing a tuning
event. Update the MIDI mapping documentation so CC 76 is no longer
described as a tuning control; do not introduce a replacement MIDI
frequency controller in this feature.

### 4. Keep saved and runtime state aligned

Update `SnapshotParams()` and `ApplyPreset()` so all 75 mode/key
frequencies round-trip through the new preset representation and are
published to the audio-visible runtime bank. Remove the obsolete tune
ticks from defaults, save/load, MIDI handling, and any stale comments.
Ensure strikes, rimming pads, split rim selection/reacquisition, and MIDI
note excitation all use the same direct-Hz lookup.

Update the earlier planning notes that still specify ratio tuning:
[encoder-1-per-bowl-pages.md](./encoder-1-per-bowl-pages.md),
[preset-bowl-pitches.md](./preset-bowl-pitches.md), and
[midi-input-and-cc-control.md](./midi-input-and-cc-control.md). Update the
user-facing tuning and MIDI documentation as well.

## Affected files / components

- [code/src/bowl_scale.h](../code/src/bowl_scale.h) — 25-key Split
  frequency representation, factory defaults, and direct mode/key lookup.
- [code/src/control_mappings.h](../code/src/control_mappings.h) — pure,
  testable normal/SHIFT step and logarithmic LED mapping helpers.
- [code/src/chompi_main.cpp](../code/src/chompi_main.cpp) — atomic runtime
  frequency bank, direct pitch lookup, preset snapshot/application, Page 3
  encoder handling and overlay.
- [code/src/BowlsPreset.h](../code/src/BowlsPreset.h) — new schema version,
  75 direct-Hz values, strict bounds, and removal of the tune tick bank.
- [code/state examples/default.json](../code/state%20examples/default.json)
  — matching embedded factory preset template.
- [code/src/MidiCcMap.h](../code/src/MidiCcMap.h) — remove CC 76 support.
- [code/src/PresetStorage.h](../code/src/PresetStorage.h) — measure the
  new schema against the existing bounded JSON buffers.
- [code/tests/preset_test.cpp](../code/tests/preset_test.cpp) — schema,
  defaults, bounds, round-trip precision, and incompatible-version cases.
- [code/tests/play_modes_test.cpp](../code/tests/play_modes_test.cpp),
  [code/tests/strike_test.cpp](../code/tests/strike_test.cpp), and
  [code/tests/pad_test.cpp](../code/tests/pad_test.cpp) — direct-frequency
  routing and per-key Split behavior.
- [code/tests/midi_test.cpp](../code/tests/midi_test.cpp) — CC 76 is
  unsupported and ignored.
- [code/tests/Makefile](../code/tests/Makefile) — update affected
  dependencies and focused test targets if needed.
- [README.md](../README.md), [midi.md](../midi.md), and the three planning
  notes listed above — remove stale ratio/CC 76 documentation.

## Step-by-step tasks

1. Add host-testable Hz step and logarithmic LED mapping helpers. Test
   low-, mid-, and high-frequency steps, both directions, SHIFT's exact
   1 Hz behavior, and the 40/10,000 Hz stops.
2. Expand `PitchTables` and factory initialization to 25 entries per mode.
   Verify Split's paired entries begin equal while remaining independently
   addressable.
3. Update the preset schema and embedded default template: remove `tune`,
   store all 75 direct frequencies, enforce the accepted range, and confirm
   the complete JSON still fits the storage buffers. Verify old schema
   versions are rejected transactionally and their files are not changed.
4. Replace runtime ratio ticks with an audio-safe atomic frequency bank.
   Wire preset application/snapshots and all excitation paths to direct
   per-mode/key lookup.
5. Change Page 3 normal and SHIFT turns to the confirmed frequency steps.
   Replace the center-detent overlay with the logarithmic LED indicator;
   preserve existing page cycling and selected-key behavior.
6. Remove CC 76 support and its tests/documentation. Confirm other CC
   mappings and unsupported-CC filtering are unchanged.
7. Reconcile the earlier ratio-based plans and user-facing documentation.
8. Run focused host tests, build firmware, review SRAM/SRAM_EXEC/SDRAM
   usage, then validate tuning and preset-slot behavior on hardware.

## Risks, open questions, memory/CPU/timing considerations

- Existing version-9 presets will no longer load. They remain on the card
  and are reported as incompatible until the user explicitly saves a new
  preset into that slot.
- Expanding Split from 13 to 25 floats adds 48 bytes to the pitch tables.
  Removing 75 tuning bytes partly offsets the larger JSON. Measure the
  actual serialized maximum and retain DMA-safe storage placement.
- Runtime frequency values are read only at excitation/selection time, not
  per sample. A 32-bit atomic load/store and Hz conversion should have
  negligible audio cost; no additional resonators or DSP loop work are
  introduced.
- The logarithmic LED position is approximate feedback, not a numeric Hz
  readout. Exact tuning relies on encoder steps and listening.
- 1% coarse steps are rounded and have a 1 Hz floor; test monotonic
  behavior around rounding transitions and both range endpoints.
- Factory defaults include some intentionally interpolated/design-choice
  frequencies. Preserve their current audible values exactly; changing the
  representation must not retune factory behavior.
- CC 76 senders will no longer change tuning. Make this explicit in MIDI
  documentation and verify the CC remains ignored rather than being
  interpreted as another controller.

## Testing and validation

### Host coverage

- Verify all 25 factory fundamentals in each mode remain unchanged.
- Verify Split paired frequencies start equal, then can be changed
  independently without altering the paired key or other modes.
- Round-trip all 75 frequencies at three-decimal precision; reject missing,
  malformed, non-finite, and out-of-range values without partially
  modifying the destination preset.
- Reject the previous schema version, and verify an incompatible slot
  remains unchanged until an explicit save.
- Test normal and SHIFT encoder steps near 40 Hz, around rounding
  thresholds, near 10 kHz, and at both stops; confirm the logarithmic
  LED mapping spans the full range monotonically.
- Verify local strikes, MIDI strikes, Rimming, and Split rim
  selection/reacquisition all use the selected key's direct frequency.
- Verify CC 76 is unsupported and ignored while adjacent supported CCs
  retain their current behavior.
- Run the focused existing host suites:
  `make -C code/tests modes-test midi-test strike-test`.

### Firmware and hardware

- Build with the repository firmware workflow and report SRAM,
  SRAM_EXEC, and SDRAM usage. Confirm JSON buffers remain DMA-safe.
- Confirm factory sound is unchanged in Strikes, Rimming, and Split.
- Exercise Page 3 normal/SHIFT tuning at the low and high ends, verify
  LED movement and end stops, and confirm changes apply at the next
  excitation rather than retuning an existing tail.
- Load a new-format preset, change frequencies in all modes including
  paired Split keys, explicitly save, reboot, and verify persistence.
- Boot with old-version files and confirm they are rejected without
  migration or automatic overwrite.
