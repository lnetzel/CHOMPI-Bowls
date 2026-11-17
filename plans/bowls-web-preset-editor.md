# BOWLS Preset Editor

## Problem and goal

Presets can currently be edited on the SD card or from the instrument's
controls. Build a static web editor that can connect to CHOMPI-BOWLS over
USB MIDI, read and write the 15 SD-card preset slots, edit every stored
parameter, and apply named tuning systems.

The site must be easy to maintain and deploy without requiring a web
framework or a backend. The user confirmed that it should deploy from this
repository root at `https://lnetzel.github.io/CHOMPI-Bowls/`. Keep the
source in `web/` and publish its contents at the Pages artifact root.

## Recommended approach

- Implement the page as static HTML, CSS, and JavaScript modules, with no
  runtime framework or server. Keep all preset data in the browser and
  transfer it directly to the instrument.
- Use the Web MIDI API with SysEx permission for device identification and
  preset transfer. This requires a small firmware protocol addition:
  current firmware ignores SysEx and does not expose SD presets over MIDI.
- Add a versioned protocol for handshake, slot listing, preset read/write,
  acknowledgements, transfer sequencing, integrity checks, cancellation,
  and errors. Keep parsing and SD work in the main loop, not the audio
  callback. Validate a complete preset before writing it and use the
  existing staged storage path where possible.
- Keep the firmware's v11 JSON schema. The web model must preserve global
  controls and all per-key values in `strikes`, `rimming`, and `split`
  banks, including the 25 `pitch_hz` values in each mode.
- Provide intuitive controls for all stored values: a left-to-right pan
  slider, key/mode navigation, bounded numeric controls, slot status, and
  clear save/read feedback. Warn before overwriting a populated slot.
- Store tuning definitions as explicit data, not UI logic. Seed the Tibetan
  bowl set from `code/state examples/default.json`; include Sléndro, Pélog,
  harmonic series, 12-TET, and Indian 22-Shruti.
  Anchor at 99 Hz where appropriate and keep generated pitches within the
  firmware's 40-10,000 Hz range. Tuning selection is optional and does not
  alter the embedded factory preset.
- Use a warm, restrained visual style with non-religious decorative
  patterns. Keep the static site in a dedicated web directory and publish
  its contents at the root of the Pages artifact with a GitHub Actions
  workflow. Use relative asset paths that work under `/CHOMPI-Bowls/`.
  Include a plain-language setup and deployment guide.

After the user grants browser MIDI/SysEx permission, the page polls for
CHOMPI-BOWLS and connects automatically. Browsers require a user gesture
for initial access; show permission, connection, transfer, and error states.
Load the slot list on connection and refresh it while idle and after writes.

## Affected files and components

- Preset schema and JSON conversion: [BowlsPreset.h](../code/src/BowlsPreset.h)
- SD-card reads and staged writes: [PresetStorage.h](../code/src/PresetStorage.h)
- MIDI transport and event handling: [MidiHandler.h](../code/src/MidiHandler.h)
- Main-loop servicing and storage integration: [chompi_main.cpp](../code/src/chompi_main.cpp)
- Existing MIDI documentation: [midi.md](../midi.md)
- Current preset and build documentation: [README.md](../README.md)
- Firmware host-test targets: [Makefile](../code/tests/Makefile)
- New static app: `web/` (HTML, CSS, JavaScript modules, tuning data, and
  tests)
- New GitHub Pages workflow: `.github/workflows/deploy-pages.yml`
- New user guide: `web/README.md`

## Step-by-step tasks

1. **Define the transfer and tuning contracts.**
   - Specify SysEx commands, device identity/capability response, 7-bit
     payload encoding, chunk limits, sequence numbers, checksums, retries,
     timeouts, cancellation, and error codes.
   - Specify JSON slot operations and ensure writes use the v11 parser and
     existing staged SD-write behavior.
   - Select and document a reference definition for Sléndro, Pélog, and
     22-Shruti, and define how each tuning maps to 25 keys.
2. **Add host-testable SysEx codec and protocol logic.**
   - Keep framing, chunk validation, and transaction state independent of
     Daisy hardware where practical.
   - Cover malformed frames, out-of-order chunks, duplicate requests,
     checksum failures, maximum preset size, timeouts, and aborts.
3. **Implement firmware transfer support.**
   - Extend USB MIDI SysEx reception/transmission and dispatch commands in
     the main loop.
   - Add slot-list, read, and validated write operations using
     `PresetStorage`; preserve its audio-safe bounded servicing and staged
     writes.
   - Reject unsupported protocol/schema versions and return explicit
     errors for missing cards, invalid slots, full storage, and failed
     writes.
4. **Build the static editor and preset model.**
   - Parse, validate, and serialize schema v11 in JavaScript without
     dropping unknown or unedited fields.
   - Add global controls, mode/key editing for every per-key parameter,
     preset-slot operations, dirty-state indication, and import/export
     backup as a recovery path.
5. **Add tuning selection.**
   - Implement the six requested tuning sets as data and show the
     resulting pitches before applying them.
   - Applying a selected tuning fills all three mode pitch tables.
   - Keep manual per-key pitch editing available after a tuning is applied.
6. **Connect the editor to the device.**
   - Add automatic device discovery after the MIDI/SysEx permission action,
     reconnect after authorization, automatic slot listing and polling,
     progress, and
     recoverable transfer errors.
   - Gate write operations until the expected CHOMPI-BOWLS firmware
     handshake succeeds.
7. **Deploy and document.**
   - Add a Pages workflow that publishes the contents of `web/` at the
     deployment artifact root from the default branch.
   - Document the one-time GitHub Pages setting, supported browser and
     SysEx permission, local preview, deploy trigger, and how to confirm a
     successful deployment.
8. **Validate with the instrument.**
   - Test reads, edits, and writes across all 15 slots; reboot and verify
     saved data.
   - Test no card, invalid presets, interrupted transfers, disconnects,
     retries, and overwrite confirmation.

## Risks, open questions, and resource considerations

- Web MIDI and SysEx require a supported browser, a secure HTTPS origin,
  and user permission. Automatic connection cannot bypass the initial
  permission prompt; document the supported desktop browsers.
- Direct SD access requires a custom firmware protocol. Older firmware
  will not support the editor and must fail with a clear compatibility
  message.
- SysEx payloads are 7-bit encoded and slower than file access. Chunking,
  flow control, retries, and cancellation must be tested on the actual
  USB MIDI transport.
- SD writes must not block audio processing or leave a damaged slot after
  power loss. Reuse staged writes and avoid filesystem work in the audio
  callback.
- Keep transfer buffers bounded. Preset JSON is limited by the firmware's
  4 KB slot buffer; the current link already uses about 88% of SRAM_EXEC,
  so new firmware code must be measured after each build.
- Tuning systems can have multiple valid variants, especially Pélog and
  22-Shruti. Identify the selected reference data in the editor.
- The site will use the repository's Pages project URL,
  `https://lnetzel.github.io/CHOMPI-Bowls/`, which is separate from the
  GitHub Releases page.

## Testing and validation

- Add host tests for SysEx framing, transfer sequencing, checksum/error
  handling, timeout/retry behavior, and preset validation.
- Extend firmware preset/MIDI tests and run the relevant existing targets:
  `make -C code/tests midi-test` and
  `make -C code/tests modes-test`.
- Test all six tuning sets, pitch bounds, 99 Hz anchoring, applying to
  all mode tables, and subsequent individual pitch edits.
- Test browser-side schema round trips and validation without hardware.
- Manually test device discovery and every read/write path on the supported
  browser and physical CHOMPI hardware, including disconnect and SD-card
  failure cases.
- Trigger the GitHub Actions deployment and verify the site loads at the
  repository Pages URL with working relative assets and Web MIDI controls.
- Build firmware with the required toolchain and review SRAM_EXEC, SRAM,
  and SDRAM usage.
