# Preset Bowl Pitches

> Superseded for pitch representation by [per-bowl-direct-hz-tuning.md](./per-bowl-direct-hz-tuning.md): presets store final per-mode/per-key Hz values, including independent Split pairs.

Status: **planned**.
Intended repository destination: `plans/preset-bowl-pitches.md`.
Links below are relative to that destination.

Important: Pause and warn if implementation uses more than 100 credits

## Problem and goal

Base bowl frequencies are currently compiled into
[bowl_scale.h](../code/src/bowl_scale.h). The preset saves relative tuning
positions but cannot change those base frequencies. Allow users to edit the
base pitch of every bowl in every performance mode directly on the SD card,
outside the CHOMPI UI.

Continue using `BOWLS/state.json`. When that file or the SD card is missing,
preserve today's boot pitches and all other factory defaults. No backward
compatibility with existing preset files is required.

## Confirmed decisions

| Area | Decision |
|---|---|
| Pitch units | Store base frequencies in Hz, not MIDI note numbers or tuning ticks. |
| Relative tuning | Keep the existing UI/MIDI tuning control: final fundamental = saved base Hz multiplied by the existing 0.5x-2x tuning ratio. Tick 16 remains unison. |
| Strikes | Independently editable table of 25 base pitches. |
| Rimming | Independently editable table of 25 base pitches, initially identical to Strikes. |
| Split | One shared table of 13 base pitches: 12 rim/strike pairs and the extra top strike. |
| Split parameters | Only base pitch is shared within a pair. Existing relative tuning, pan, volume, profile, brightness, and decay remain per-mode/per-key. |
| Defaults | Use the current compiled frequency tables exactly; do not retune the factory sound. |
| Compatibility | New schema only; reject legacy presets rather than migrating them. |
| UI scope | No new pitch editor, encoder page, or MIDI CC. |

## Recommended approach

### 1. Named frequency tables in schema v9

Bump the preset schema from v8 to v9. Add a `pitch_hz` object containing
`strikes` (25 numbers), `rimming` (25 numbers), and `split` (13 numbers).
Keep all existing global fields and six 75-entry control banks.

This is 63 stored frequencies, not 75. Named arrays make offline editing
easier than a flattened bank with implicit mode offsets. The following is
the new object's factory-default content, to be included alongside the
existing fields in the complete preset:

```json
"pitch_hz": {
  "strikes": [
    99.0, 112.7, 128.3, 146.0, 167.3, 191.7, 219.6, 261.0,
    310.2, 347.8, 390.0, 425.9, 465.0, 513.0, 564.1, 620.4,
    693.6, 780.0, 850.3, 927.0, 1026.0, 1128.3, 1240.8,
    1395.0, 1560.0
  ],
  "rimming": [
    99.0, 112.7, 128.3, 146.0, 167.3, 191.7, 219.6, 261.0,
    310.2, 347.8, 390.0, 425.9, 465.0, 513.0, 564.1, 620.4,
    693.6, 780.0, 850.3, 927.0, 1026.0, 1128.3, 1240.8,
    1395.0, 1560.0
  ],
  "split": [
    99.0, 146.0, 219.6, 310.2, 390.0, 465.0, 513.0,
    620.4, 693.6, 780.0, 927.0, 1026.0, 1240.8
  ]
}
```

Strikes/Rimming array index 0 maps to C3 and index 24 to C5. Split index
0-11 maps to rim keys 0-11 and strike keys 12-23 respectively; index 12
maps only to strike key 24 (C5). Changing one Split entry changes the base
pitch for both paired keys. Their final pitches can still differ if their
existing relative tuning positions differ.

Do not require ascending or distinct user pitches: duplicated pitches and
arbitrary key ordering are valid. Keep ascending checks for factory tables
only.

### 2. Defaults and runtime pitch lookup

Keep the compiled scales as the single source of factory defaults. Add a
small host-testable pitch-table type and mode/key lookup in
[bowl_scale.h](../code/src/bowl_scale.h); reuse it in the preset and runtime
state instead of duplicating Split's pairing rules.

Initialize both runtime parameters and `BowlsPreset::SetDefaults()` from
these same constants. Restore loaded tables before audio starts and copy
them into every autosave snapshot.

Change `KeyFundamental()` to use the runtime base-frequency lookup, then
apply the existing tuning ratio. This already feeds local and MIDI strikes,
Rimming pads, Split rim selection, and Split rim reacquisition. Verify each
route uses the saved frequencies rather than retaining a hard-coded lookup.

Base tables are boot-loaded and immutable while audio runs, so ordinary
float storage is sufficient; no atomic float bank or per-sample work is
needed. Existing UI/MIDI relative tuning continues to apply on the next
excitation, not by abruptly retuning an existing tail.

### 3. Strict parsing and reliable persistence

Require the new object and all three arrays. Parse into scratch state and
apply only after the entire document passes validation. Reject missing
tables, wrong types, short or oversized arrays, malformed entries, and
unsupported versions. Do not default a missing frequency inside an otherwise
v9 file.

Use bounded numeric parsing that consumes each complete JSON number token,
checks overflow and finiteness, and accepts integer, decimal, and scientific
notation. The existing integer-array parser is not suitable for float
frequencies or exact-length validation; add a dedicated helper rather than
loosening existing tick-bank semantics.

Recommended supported base range: **40-10000 Hz inclusive**. With the
unchanged tuning multiplier this gives fundamentals of 20-20000 Hz, within
the current 48 kHz engine's 20 Hz to 0.45x sample-rate resonator range.
Reject out-of-range frequencies rather than silently clamping them.
Higher profile partials may still be muted by existing DSP frequency guards,
as they are today. Validate the proposed endpoints with every profile before
finalizing this documented range.

Serialize frequencies with three fractional decimal places, giving a
maximum save-rounding error of 0.0005 Hz. Accept higher input precision but
document its rounding on the next save. Require repeat save/load cycles to
be stable at the chosen precision.

Increase storage buffers only after measuring the complete worst-case v9
document. Start with a **4096-byte** budget, including the terminator, and
test both serialized output and reasonably formatted offline input.
Preserve member-buffer SRAM placement required by SDMMC DMA. Detect
oversized files explicitly instead of parsing a truncated prefix.

Keep the existing staged main-loop writer and SD filesystem leasing. Route
serialization/buffer failures through the existing failed-save diagnostic
event instead of silently dropping the save request. This addresses the
current zero-length serialization path as part of enlarging the schema.
Keep existing boot diagnostics for absent, unreadable, or rejected files.

Retain today's file-creation behavior: if the file is missing, play using
factory defaults; the next ordinary control change requests the first save,
which now includes every default pitch. Do not add automatic boot writes.

## Affected files/components

- [bowl_scale.h](../code/src/bowl_scale.h): shared factory initialization,
  25/25/13 pitch tables, and Split-pair lookup.
- [BowlsPreset.h](../code/src/BowlsPreset.h): schema v9, pitch data,
  strict numeric parsing, serialization, and default initialization.
- [PresetStorage.h](../code/src/PresetStorage.h): measured buffer capacity,
  explicit oversized-input handling, and serialization failure reporting.
- [chompi_main.cpp](../code/src/chompi_main.cpp): runtime base tables,
  `KeyFundamental()`, boot application, and autosave snapshots.
- [preset_test.cpp](../code/tests/preset_test.cpp): complete v9 round trips,
  validation, precision, defaults, and size assertions; replace v7 migration
  expectations with legacy rejection.
- [play_modes_test.cpp](../code/tests/play_modes_test.cpp): customized pitch
  lookup and shared Split mappings without changing note-claim behavior.
- [strike_test.cpp](../code/tests/strike_test.cpp) and
  [pad_test.cpp](../code/tests/pad_test.cpp): custom-pitch DSP validation.
- [Makefile](../code/tests/Makefile): update affected header dependencies so
  changing shared defaults rebuilds preset tests.
- [README.md](../README.md) and [midi.md](../midi.md): new schema, offline
  editing procedure, key mappings, and interaction with relative tuning.

## Step-by-step tasks

1. Add the shared pitch-table representation and lookup with factory defaults
   copied from the existing constants. Keep legacy factory lookup available
   where tests need to compare unchanged defaults.
2. Extend the preset to v9 with the three named frequency arrays. Remove
   legacy migration, retaining existing non-pitch fields and behavior.
3. Implement bounded strict parsing and deterministic float serialization.
   Measure the complete document and set the shared storage/test size budget.
4. Add runtime pitch state initialized from the shared defaults. Wire boot
   application, every pitch excitation route, and every autosave snapshot.
5. Handle oversized input and serialization failures explicitly while
   preserving staged writes, DMA-safe buffers, retries, and filesystem leases.
6. Add the focused host tests below and update stale schema comments and
   test dependency declarations.
7. Document how to generate a new file, edit it while CHOMPI is powered off,
   safely eject the card, and reboot. Include complete key/index mapping and
   warn that old presets are rejected and may be replaced on the next save.
8. Run focused host validation, then build firmware and check memory usage.
   Exercise the SD-card and audible behavior on hardware.

## Risks, open questions, memory/CPU/timing considerations

- Scope decisions are confirmed. The recommended range and precision need
  implementation-time DSP and serialization validation, not new UI design.
- Each 63-float table occupies 252 bytes before any surrounding padding.
  Account for runtime state, preset snapshots, parser scratch state, and
  boot/main-loop stack temporaries rather than counting only one table.
  Two buffers enlarged from 2048 to 4096 bytes add 4096 bytes of SRAM.
- Boot load and saves grow in size; all JSON and SD work must remain outside
  the audio callback. Pitch lookup adds only a table access before the existing
  tuning calculation, with no extra resonators or per-sample processing.
- Wide custom pitches can change timbre and loudness as partials enter or
  leave the engine's supported band. Confirm finite, bounded rendering;
  do not promise identical timbre across the entire supported range.
- Autosave must preserve edited base tables when any unrelated control
  changes. Otherwise a successful load followed by a knob turn could erase
  the user's tuning.
- Offline changes are read only at boot. Card reinsertion does not reload
  settings and a later autosave can overwrite edits made while powered on.
  Document power-off editing rather than adding hot reload.
- The existing writer's power-loss properties are not strengthened by this
  feature. Preserve its behavior without claiming a new atomicity guarantee.

## Testing and validation

### Host coverage

- Round-trip distinct fractional values in all 63 entries, all six control
  banks, every global field, and each active mode.
- Verify all factory pitch lookups exactly match today's defaults, including
  Split's 1240.8 Hz top strike and every rim/strike pair.
- Change Strikes without changing Rimming; change one Split entry and verify
  both paired keys use it, while their relative tuning remains independent.
- Verify effective pitch at tuning ticks 0, 16, and 32 and representative
  intermediate positions. Exercise local and MIDI excitation routing.
- Save after a non-pitch control change, reboot/load, and confirm edited
  tables survive within 0.0005 Hz. Repeat cycles to check precision stability.
- Reject legacy v7/v8 and future versions, absent tables, wrong types,
  arrays of 0/12/14/24/26 entries as applicable, strings/null/booleans,
  overflow, malformed numbers, and frequencies outside the supported range.
  Check that failed parsing leaves a sentinel destination unchanged.
- Accept boundary values, scientific notation, JSON whitespace/newlines,
  duplicates, and non-ascending pitches.
- Measure maximum valid serialization and corrupt tick-bank size cases;
  ensure output plus terminator fits. Reject undersized output buffers and
  oversized files without reporting success or partially applying settings.
- Render custom-pitch strikes and pads across all profiles and tuning
  endpoints; assert finite, bounded samples. Preserve factory DSP regressions.

Use the existing MSYS2 UCRT64 host runner, combining the relevant targets:
`make -C code/tests modes-test midi-test strike-test`.
Run the existing render `test` target if DSP code changes or regressions
require it. No new host test framework is needed.

### Firmware and hardware

- Build through the repository build workflow and report SRAM, SRAM_EXEC,
  and SDRAM usage. Confirm DMA buffers remain in accessible SRAM and stack
  growth is safe.
- Boot with no card and with a card lacking the file: hear the current factory
  pitches, retain existing diagnostics, and confirm the first normal autosave
  creates a complete v9 preset.
- Boot a valid edited file and audition all three modes, Split pairings,
  local keys, MIDI notes, wheel rimming, and relative tuning controls.
- Change an unrelated control, wait for a successful autosave, reboot, and
  verify custom pitches persist.
- Boot invalid, legacy, truncated, and oversized files: retain factory state
  and report the existing rejected/read-failure diagnostic.
- Exercise save failure/recovery and concurrent SD audio operations with
  the larger document; verify no audio stalls or watchdog resets.

Implementation begins only after this plan is approved.
