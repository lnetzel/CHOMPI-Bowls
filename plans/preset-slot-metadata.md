# Preset Slot Names and Comments

## Problem and goal

Presets in the web editor need human-readable per-slot metadata: a `name` and
a `comment`. The editor must read, edit, and save both fields alongside the
existing preset values. Firmware does not use this metadata for playback or
controls, but it must preserve it when loading and saving presets. Copying a
preset to another slot is the one firmware operation that changes metadata:
the destination inherits the comment and receives `-copy` appended to the
source name; the source remains unchanged.

## Recommended approach

- Add `name` and `comment` to the preset JSON and in-memory model, and expose
  them as per-slot text fields in the existing web editor.
- Evolve the preset schema while continuing to load existing v11 files with
  empty metadata. Serialize the new schema with both metadata fields present.
  Keep the SysEx transfer protocol unchanged; it already transfers complete
  preset JSON documents.
- Keep firmware metadata opaque during playback and parameter changes.
  Explicitly preserve the selected slot's metadata when firmware saves new
  parameter values over an existing slot. New/empty slots start with empty
  metadata.
- On a slot copy, copy the complete preset and comment, then append `-copy`
  to the destination name on every copy. Never change the source slot. Limit
  names to 48 UTF-8 bytes and comments to 128 UTF-8 bytes in firmware and the
  web UI; reject a copy whose suffix would exceed the name limit.
- Use fixed-size metadata storage in the firmware model to keep memory bounded.
  Choose limits after measuring the additional memory cost and ensuring
  serialized presets remain below the existing 4 KB slot limit.

## Affected files and components

- Preset fields, schema conversion, and slot copy: [BowlsPreset.h](../code/src/BowlsPreset.h)
- Staged SD-card serialization and loading: [PresetStorage.h](../code/src/PresetStorage.h)
- Hardware preset snapshots and save/copy actions: [SnapshotParams](../code/src/chompi_main.cpp#L2677) and [PresetShiftConfirm](../code/src/chompi_main.cpp#L2805)
- Existing browser editor and per-slot form: [web/](../web/)
- Preset schema and slot-operation tests: [preset_test.cpp](../code/tests/preset_test.cpp)
- Firmware test targets: [Makefile](../code/tests/Makefile)
- Existing editor design and transfer context: [bowls-web-preset-editor.md](./bowls-web-preset-editor.md)

## Step-by-step tasks

1. **Define the metadata contract.**
   - Use 48-byte names and 128-byte comments, validate UTF-8 byte lengths, and
     JSON-escape strings for 7-bit-safe SysEx transfer.
   - Append another `-copy` on each copy. Reject suffix overflow without
     changing the destination and report it through firmware feedback.
2. **Extend the firmware preset schema.**
   - Add bounded `name` and `comment` fields and initialize both to empty.
   - Update parsing to load v11 files with empty metadata and parse the new
     schema's metadata fields; update serialization to always emit them.
   - Ensure malformed, oversized, or invalidly encoded strings are rejected
     without partially changing the destination preset.
   - Preserve metadata through pitch fallback repair, SD-card reads/writes,
     and the existing SysEx read/write path.
3. **Preserve metadata in firmware slot actions.**
   - When saving control parameters over an occupied destination slot, retain
     that slot's name and comment; leave metadata empty for a new slot.
   - Update the slot-copy operation to append `-copy` only to the destination
     name and leave the copied comment and source slot unchanged.
   - Keep ordinary encoder/performance edits from dirtying or rewriting
     preset-slot metadata.
4. **Update the web editor.**
   - Add name and comment text fields to each selected slot.
   - Populate both fields on read and include both in save/write JSON without
     losing them when other preset values are edited.
   - Enforce the firmware's metadata limits and show a useful validation
     message when a value is too long.
5. **Add focused tests.**
   - Cover v11-to-current-schema loading, metadata JSON round trips including
     quotes and backslashes, empty fields, and rejected over-limit strings.
   - Cover metadata retention when firmware parameters are saved, and verify
     copy suffixing, comment copying, source immutability, and suffix overflow.
   - Add browser-side tests for loading, editing, and serializing metadata with
     the rest of a preset.
6. **Validate end to end.**
   - Run the focused firmware preset tests and existing preset transfer tests.
   - Run browser tests and check read/edit/write/read round trips through the
     editor, including presets with empty and non-empty metadata.
   - Build firmware and measure SRAM/SDRAM usage after adding per-slot text
     storage. Verify legacy v11 presets still load and remain editable.

## Risks, open questions, and memory/CPU/timing considerations

- Each `BowlsPreset` is cached for all 15 slots, so fixed name/comment buffers
  multiply their capacity by 15 and may increase memory pressure. Determine
  limits using a firmware build and keep them consistent with the 4 KB JSON
  limit.
- The schema parser currently accepts only its current version. Legacy v11
  acceptance must be explicit, and the web editor must not strip metadata
  during an edit/save cycle.
- UTF-8 limits should be measured in bytes, not browser character count, so
  multibyte text cannot overrun firmware buffers. JSON escaping also affects
  serialized size.
- Repeated copies append another `-copy`; at the name limit the copy is
  rejected with firmware LED/log feedback rather than silently truncating.
- Metadata is not used by audio processing. Keep string validation and JSON
  work out of the audio callback, preserving the existing staged storage path.

## Testing and validation

- Extend `code/tests/preset_test.cpp` for schema compatibility, string
  validation/escaping, metadata retention on save, and copy semantics.
- Run `make -C code/tests modes-test` and
  `make -C code/tests midi-transfer-test`.
- Add and run browser-side tests for per-slot metadata round trips and
  validation, using the editor's existing test setup.
- Manually verify editor read/edit/write/read behavior on hardware, firmware
  preset copying, reboot persistence, and compatibility with pre-existing v11
  preset files.
- Build firmware and record SRAM, SRAM_EXEC, and SDRAM usage to ensure metadata
  storage fits the device.
