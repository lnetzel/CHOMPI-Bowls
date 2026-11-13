# Preset Management Rehaul

Status: **planned**.
Intended repository destination: `plans/preset-management-rehaul.md`.
Links below are relative to that destination.

## Problem and goal

Replace the current single-state autosave system with 15 explicit preset
slots, selected by White Keys 1-15. Parameter edits stay in RAM and must
never trigger an SD-card save by themselves. Presets are persisted only
through the preset-management save interaction.

The slot manager will be implemented afresh in CHOMPI-BOWLS. Match
CHOMPI-PIPES' user-facing key combinations, LED feedback, and JSON format,
but do not reuse its preset-management implementation. The JSON in each
slot must represent the complete factory/default state shape, including
global values, active mode, all per-mode/per-key banks, and every other
persistent field in the embedded [default.json](../code/state%20examples/default.json)
template. Transient performance state remains transient unless it is part
of that template's persisted format.

## Confirmed decisions

| Area | Decision |
|---|---|
| Slots | White Keys 1-15 map to preset slots 1-15, left to right. |
| Interaction | Match the key-combo and LED behavior of CHOMPI-PIPES. |
| Implementation | New CHOMPI-BOWLS implementation; no preset-manager code port. |
| Persistence format | Use CHOMPI-PIPES' JSON format for saving, copying, and loading. |
| Parameter changes | Do not write to SD when parameters change; edits remain in RAM until explicitly saved. |
| Complete state | Store the full persistent state represented by the embedded `default.json` template in each preset. |
| Legacy state file | Remove all `BOWLS/state.json` load/write behavior. Do not migrate, read, overwrite, or delete an old `state.json`. |
| Empty-card boot | Use factory/default values in RAM; do not create or save a preset automatically. |
| Empty slot selection | Follow CHOMPI-PIPES' empty-slot behavior. Verify its exact behavior before implementation. |

## Recommended approach

### 1. Establish the compatibility contract

Before coding, inspect CHOMPI-PIPES' preset UI and JSON format as a
read-only reference. Record the exact key combinations, gesture timing,
slot-selection behavior, LED colors/animations/priorities, filenames, and
JSON schema needed for parity. This is behavioral and format discovery
only; implement the manager independently in CHOMPI-BOWLS.

Determine the complete persistent field set from the Bowls default
template and current preset model. Preserve the format and field semantics
required by CHOMPI-PIPES while ensuring no Bowls persistent fields are
omitted. Keep transient routing, latches, note/voice state, MIDI queues,
and recording/playback state out of presets unless Pipes' format or the
existing default template explicitly includes them.

### 2. Add explicit slot storage

Replace the single `BOWLS/state.json` persistence path with 15 independently
addressable preset files using CHOMPI-PIPES' naming and JSON conventions.
An empty slot remains empty until explicitly saved. At boot, load the
selected/appropriate preset according to Pipes' behavior; if no preset is
available, initialize from the embedded factory/default template without
writing it to the card.

Load and parse into a temporary preset object, then apply it only after
the complete document validates. Save and copy through staged, main-loop
FatFS work, retaining bounded operations, DMA-reachable member buffers,
explicit failure reporting, and filesystem leasing. Copying must preserve
the same complete JSON format as saving and loading.

### 3. Implement slot UI and state transitions

Recreate the Pipes key-combo and LED behavior for entering preset
management, selecting a slot, saving, copying, and cancelling. White Keys
1-15 represent slots only in the relevant preset-management interaction;
outside it, keep their existing play and parameter-overlay behavior.

Make every gesture edge-safe: require fresh presses where appropriate,
consume keys involved in a completed/cancelled combo, prevent held keys
from leaking actions between normal performance and preset-management
states, and define behavior for empty slots and SD-card failures.
Preserve the currently higher-priority recording/playback/SHIFT feedback
and coordinate LED ownership with the existing overlays.

### 4. Remove autosave and legacy state handling

Remove the parameter-change debounce and all calls that automatically
request a state save. Remove the old state-file load/write/repair flow,
including startup queuing of an initial state save. Keep explicit
save-status reporting for preset operations and keep a failed save from
appearing successful. A pre-existing `BOWLS/state.json` is left untouched
and is never consulted.

## Affected files/components

- [chompi_main.cpp](../code/src/chompi_main.cpp): remove autosave tracking
  and scheduling; add preset UI state, key handling, slot selection, and
  coordination with recording/playback controls.
- [PresetStorage.h](../code/src/PresetStorage.h): replace the single-state
  load/save interface with 15-slot storage operations while retaining
  main-loop-only FatFS access, filesystem leasing, bounded stages, and
  explicit errors.
- [BowlsPreset.h](../code/src/BowlsPreset.h): represent the complete
  persistent Bowls state in the CHOMPI-PIPES-compatible JSON format;
  preserve strict validation and transactional parsing.
- [leds.h](../code/src/leds.h): reuse the existing 15-white-key LED map and
  add only helpers needed to render preset-selection/save/copy feedback.
- [preset_test.cpp](../code/tests/preset_test.cpp): test full-state JSON
  compatibility, round trips, validation, and copy semantics.
- [code/tests/Makefile](../code/tests/Makefile): add/adjust focused test
  targets and dependencies for slot management.
- [README.md](../README.md): document slot numbering, key combos, LED
  indications, explicit-save behavior, and the lack of state.json
  migration.

## Step-by-step tasks

1. Inspect CHOMPI-PIPES' preset-management interaction and document the
   exact user-visible behavior and JSON contract. Compare the Bowls
   template and preset fields to identify any persistent data that must be
   added or retained.
2. Define slot filename/order mapping and boot selection behavior to match
   Pipes, including no-card, no-preset, and empty-slot cases. Ensure first
   boot uses factory/default values without creating a preset.
3. Implement host-testable JSON handling for complete Bowls state using
   the Pipes format. Validate transactionally and verify save/copy/load
   all produce and consume the same format.
4. Implement 15-slot SD operations as bounded main-loop stages. Integrate
   with the existing filesystem lease so preset reads/writes cannot
   collide with SD recording or playback; surface failures and preserve
   other pending slot operations.
5. Add the fresh preset UI state machine: match Pipes' key combos, timing,
   cancel behavior, empty-slot behavior, LED patterns, and feedback
   priority. Keep White Keys 1-15 reserved as slot selectors only while
   that interaction is active.
6. Remove `state.json` boot loading, migration/repair behavior, automatic
   first save, parameter dirty/debounce tracking, and autosave requests.
   Do not delete or inspect an existing legacy state file at runtime.
7. Add focused host tests and update user documentation. Build firmware,
   check memory use, and verify save/load/copy/select/empty/error behavior
   on hardware.

## Risks, open questions, memory/CPU/timing considerations

- The exact Pipes filenames, schema details, key-combo timing, and empty
  slot semantics must be confirmed in the first implementation step; do
  not infer them from the current Bowls state manager.
- The default template and `BowlsPreset` model must stay aligned. Missing
  a persistent global or per-key field would silently make presets
  incomplete even if round trips pass for the current model.
- Fifteen files require bounded directory scanning or equivalent
  deterministic lookup. Avoid probing excessive candidate filenames on
  the main loop, especially while recording or playing audio.
- SD access must remain outside the audio callback and serialized with
  `SdRecorder`/`SdPlayer`. Large JSON buffers and FatFS `FIL` objects must
  remain in DMA-accessible member storage, not on the DTCM stack.
- Slot load applies many values while audio is running. Define a safe
  application boundary so audio-visible state cannot be left partially
  updated; test active notes/tails and any parameters that affect current
  voices.
- Existing encoder overlays, keybed performance, playback mode, recorder
  gestures, save feedback, and SHIFT indication can contend for the same
  keys/LEDs. Explicit priority and transition handling are needed.
- With no card, edits still affect the current performance but are not
  persisted. An explicit save failure must be visible and must not clear
  or misrepresent the in-RAM state.
- Do not claim stronger power-loss guarantees than the retained staged
  write/rename scheme actually provides.

## Testing and validation

### Host coverage

- Round-trip a complete preset in the exact Pipes JSON format, covering
  all persistent globals, all 3 x 25 per-key banks, active mode, and
  every field represented by the factory template.
- Verify copy produces equivalent content and a copied preset can be
  loaded without format conversion.
- Reject malformed, truncated, unsupported-version, missing-field, wrong
  type, out-of-range, and oversized documents without partially changing
  the destination preset.
- Test slot index/name mapping for White Keys 1-15 and empty/populated
  slot cases.
- Test preset interaction state transitions, cancellation, key
  consumption, and LED state decisions without Daisy hardware where
  practical.
- Verify ordinary parameter changes never enqueue a save; explicit save
  is the only parameter-persistence trigger.
- Run the focused preset/UI tests and relevant existing play-mode,
  routing, MIDI, and SD helper tests with the existing
  `code/tests` Makefile.

### Firmware and hardware

- Build firmware and report SRAM, SRAM_EXEC, and SDRAM usage. Confirm JSON
  buffers and FatFS objects remain in DMA-accessible memory.
- Boot with no card, an empty card, 1 saved slot, and all 15 slots. Confirm
  factory defaults are not auto-saved and an existing `BOWLS/state.json`
  is neither loaded nor modified.
- Exercise every Pipes-matching key combo, keybed slot mapping, LED
  sequence/color, cancel path, empty slot, save, copy, and load.
- Change parameters without saving, reboot, and verify no implicit write
  occurred. Explicitly save, reboot, and confirm the complete state
  restores.
- Exercise preset operations alongside SD recording/playback, card
  removal/reinsertion, slow/full-card failures, and interrupted writes.
  Verify audio and watchdog responsiveness throughout.

Implementation begins only after this plan is approved.
