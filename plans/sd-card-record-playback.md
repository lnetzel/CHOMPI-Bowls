# SD Card Recording and Playback

Status: **planned**. Reference implementation: `C:\local dev\CHOMPI-Pipes\code\src\SdRecorder.h`, `SdPlayer.h`, and the corresponding control and LED handling in `chompi_main.cpp`.

## Problem and goal

Add WAV recording of the rendered Bowl audio and playback of saved takes from the SD card. Reuse the sibling firmware's transport gestures, take selection, and status LEDs while keeping Bowls' existing performance controls and saved preset behavior intact.

## Confirmed interaction and LED behavior

| State / action | Control | Keybed LEDs | Panel LEDs |
|---|---|---|---|
| Arm a recording | In normal operation, hold PLAY + LOOP for 2 s. A fresh press of either key starts the gesture; a chord already held at boot cannot trigger it. | Red fill from left to right during the hold. | No new arm indicator. |
| Countdown | After the arm hold, wait 4 s; audio and normal bowl playing continue. A fresh PLAY or LOOP press cancels. | First four white keys start white; one turns red per second, left to right. | None. |
| Recording | Begins when the countdown completes. A fresh PLAY + LOOP chord held for 1 s requests a clean stop. Releasing early cancels; a hold longer than 400 ms consumes the gesture rather than triggering another action. | Existing keybed behavior resumes. | SHIFT LED blinks red at 2 Hz while recording, following the sibling's priority rule that SHIFT feedback wins while SHIFT is held. A recorder error blinks it red rapidly for about 2 s. |
| Enter playback | Hold PLAY alone for 2 s. LOOP joining the hold cancels playback entry so the record chord can take over. Entry releases current notes/latches, closes any recording cleanly, then scans takes. | Blue fill during the hold, then a blue chase while stopping/scanning. | PLAY LED is blue once playback mode is active. |
| Playback | Tap PLAY to pause/resume. Tap LOOP to toggle looping the current take. Press any populated white-key slot to select that take; keys do not play bowls in this mode. At EOF, advance to the next take using the sibling's slot order. | Newest take is slot 0 (leftmost/C3), up to 15 takes map left to right. Unselected slots are dim white; selected is solid blue when paused and blinks blue at 250 ms when playing. Empty slots are dark. A take that fails validation/open flashes red for 500 ms. | PLAY LED is solid blue when paused and blinks blue when playing. LOOP LED is blue while take-loop is enabled. |
| Exit playback | Hold PLAY alone for 2 s, or restart the firmware. | Blue exit-progress fill; ordinary keybed view resumes after exit. | Restore normal panel indications. |
| Missing card / no takes / fatal player error | No alternate control. | Record refusal flashes the first four white keys red for about 1 s. No takes/card flashes all 15 white keys red for about 1 s; a fatal player error uses about 2 s. | Recorder errors use the rapid red SHIFT-LED flash. |

In Bowls, LOOP already toggles the Rimming latch (yellow LOOP LED and yellow latched-key LEDs), while PLAY is reserved. Keep that existing latch behavior and color in normal operation. Defer the LOOP click until button release so a completed PLAY+LOOP recording chord cannot toggle the latch; suppress both normal actions for any accepted or swallowed transport gesture. In playback mode, LOOP controls take looping and its LED is blue, as in Pipes. The playback and recording LED patterns above use the existing Bowls panel assignments in `leds.h`; give transient record/player status the corresponding priority over ordinary status without breaking SHIFT modifier feedback, save-status flashes, or a CPU-meter build.

## Recommended approach

Port the Pipes recorder/player state-machine design, not its hardware or storage initialization. The audio callback must only convert/copy bounded audio blocks through caller-owned SPSC rings; it must never call FatFS. Keep every `FIL`, `FILINFO`, path, header, and bounce buffer in global-instance member storage in DMA-reachable SRAM, not on the DTCM stack. Run all filesystem work from the existing main loop, staged and serialized with `PresetStorage::Service()` through the already-mounted FatFS volume.

Use the sibling's canonical 48 kHz, stereo, 16-bit PCM WAV header and -6 dB record level. Tap the final stereo Bowl bus before master gain so takes do not depend on the volume knob; inject playback after the Bowl reverb stage and before master gain, avoiding re-recording playback or applying the reverb twice. Temporarily use Normal output routing while playback is active and restore the user's routing on exit, matching Pipes' isolated playback behavior. Release live Bowl notes on entry and allow existing resonator/reverb tails to decay naturally.

The recorder should create a new numbered take without overwriting existing takes (1..999), sync periodically, drain its tail and patch WAV sizes on close, and fail visibly on ring overflow, full card, or write error. The player should scan downward for the newest 15 takes, fill its SDRAM ring in bounded main-loop work, support pause/resume and current-take looping, skip stale/invalid files with a red slot flash, and advance through the take window at EOF. Use a single shared filesystem owner and ensure preset save, recorder, and player operations cannot run concurrently.

Recommended directory: `BOWLS/RECORDINGS/`, with the sibling's `Recording NN.wav` naming and compatible WAV format. This keeps Bowls recordings separate from `PIPES/RECORDINGS/` while allowing WAV-level interoperability if files are moved. Confirm whether users instead want both firmwares to share one recording directory before implementation.

## Affected files/components

| File/component | Planned changes |
|---|---|
| [chompi_main.cpp](../code/src/chompi_main.cpp) | Global SDRAM record/play rings and buffers; PLAY/LOOP hold/chord state; audio tap and playback mix; modal playback key handling; staged-service coordination; transport LEDs and README-aligned control comments. |
| [PresetStorage.h](../code/src/PresetStorage.h) | Reuse its mounted SD/FatFS context and card-presence state; coordinate filesystem access only. Do not initialize a second SDMMC/FatFS interface or alter preset schema/serialization. |
| [chompi_hardware.h](../code/src/chompi_hardware.h) | Existing `PlayPressed/Clicked/Released` and `LoopPressed/Clicked/Released` signals should suffice; change only if implementation finds an edge/boot-baseline gap. |
| [leds.h](../code/src/leds.h) | Reuse existing PTH indices: SHIFT 0, PLAY 7, LOOP 8; reuse the 15 white-key SMT mapping. No hardware mapping change expected. |
| `code/src/SdRecorder.h`, `code/src/SdPlayer.h` | Add target-only FatFS state machines and host-testable ring, WAV, take-name/window, and conversion helpers based on Pipes. |
| [code/tests/Makefile](../code/tests/Makefile) and a focused SD host test | Exercise ring behavior, WAV header round-trip/validation, take numbering/window/slot advance, and overflow/edge cases without hardware dependencies. |
| [README.md](../README.md) | Document the PLAY/LOOP gestures, recording and playback LED meanings, key-to-take mapping, file format/location, and failure behavior. |

## Step-by-step tasks

1. Extract the pure host-testable pieces from the Pipes headers and add a focused test target before integrating FatFS. Verify that ring capacities satisfy the power-of-two and whole-stereo-frame requirements.
2. Add the recorder and player classes with the Pipes ISR/main-loop boundary, bounded service chunks, clean close/header patch, scan/open/validation, pause/resume, loop, take advance, and fatal/skip states. Keep large buffers in global instance storage and the audio rings in SDRAM.
3. Reuse Bowls' existing FatFS mount. Add a single main-loop service policy so preset save, record, and playback file operations are serialized; make recorder close complete before playback scan/open. Check card presence before arming and reject requests cleanly when absent.
4. Add transport state handling in `chompi_main.cpp`: 2 s PLAY+LOOP arm, 4 s countdown, fresh 1 s chord stop, 2 s PLAY-only playback entry/exit, PLAY pause/resume, LOOP take-loop, and white-key take selection. Baseline held controls at boot and consume cancelled/completed gestures so no latch action leaks through.
5. Add the pre-master -6 dB record tap and post-reverb/pre-master playback injection. Release live notes on playback entry, preserve the saved routing and restore it on exit, and keep the player's audio path silent while paused/stopped/underflowing.
6. Render the documented keybed and PLAY/LOOP/SHIFT LED states, including no-card, empty-window, skipped-file, and fatal-error feedback. Preserve Bowls' ordinary yellow LOOP latch indication outside playback and define status-vs-SHIFT/save-flash/CPU-meter priority.
7. Update the control-map comments and README. Build the firmware, review SRAM/SRAM_EXEC/SDRAM headroom, then exercise the hardware checklist below.

## Risks and open questions

- Two 1 MiB SDRAM rings (record and playback) match Pipes' approximately 5.5 s of stereo slack each. Confirm SDRAM and SRAM usage in the firmware build; recorder/player FatFS bounce buffers, take scan state, and FIL objects consume SRAM.
- SD card latency is unbounded in practice. Keep FatFS out of the audio callback, keep each service pass bounded, and ensure the watchdog/main loop remain live during slow cards. Ring overflow aborts a recording rather than overwriting audio; playback underrun must produce silence.
- PresetStorage currently owns SD initialization/mount and services saves. Direct `f_*` calls from the new modules are valid only after that mount and must not overlap each other or the preset writer. The card-removal/reinsertion behavior for an active recording or playback needs explicit hardware verification.
- The proposed separate `BOWLS/RECORDINGS/` path avoids accidentally mixing product takes. Decide whether cross-firmware take sharing is desired before freezing the path.
- The proposed post-reverb capture records the audible Bowl bus; playback after reverb avoids doubling the room effect. Verify this tap point against the desired sound and `Send FX` routing behavior on hardware.
- Countdown keybed LEDs temporarily cover the white-key overlay while notes continue. Playback takes over the keybed, so encoder value overlays and bowl key events must be suppressed until exit.
- A power loss can leave a partial recording. The recorder should patch and close on orderly stop/error where possible; recovery of an interrupted file is not guaranteed. Never overwrite an earlier take.
- There is no local SD-card hardware test in `code/tests`; host tests validate pure logic, while real card behavior, DMA placement, button timing, and LED priority require device tests.

## Testing and validation

### Host tests

- Add a test target to `code/tests/Makefile` for ring push/pop order, wraparound, whole-block refusal/overflow, stereo conversion, WAV header fields and round-trip parsing, 1..999 filenames, newest-15 ordering, invalid slot handling, next-slot wrap, and bounded rescan logic.
- Run the new focused target and the existing `make -C code/tests modes-test test strike-test routing-test` suites to catch regressions in play modes, audio routing, rendering, strikes, and presets.
- Ensure host-test macros exclude target-only FatFS code, matching the sibling pattern.

### Firmware and hardware checks

- Build with the repository firmware toolchain and report SRAM, SRAM_EXEC, and SDRAM usage; confirm no overflow of the 128 KiB DTCM stack/data region or the configured SRAM regions.
- Confirm boot with and without a card; refusal and error indications; arm/cancel/countdown/start/stop gesture timing; record while playing; new numbering without overwrite; and a clean header/data length after stop.
- Confirm newest-15 key mapping, pause/resume LEDs, LOOP playback, EOF advance/wrap, invalid take skip, playback entry/exit, routing restoration, and that no live notes/latch or normal LOOP action leaks into playback/record gestures.
- Test slow/full cards, removal during record and playback, ring overflow/underrun, preset saves near a recording start/stop, and repeated entry/exit. Verify audio remains click-free and the main loop/watchdog stays responsive.