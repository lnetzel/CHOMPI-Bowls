# MIDI Output to DAW

Status: firmware implementation and host regressions complete; hardware
acceptance (including physical DIN OUT routing) remains pending.

## Problem and goal

Allow CHOMPI-BOWLS' physical 25-key performance to play MIDI instruments in a DAW such as Cubase, while BOWLS continues to make its own sound.

Transmit the local keyboard's note performance simultaneously over the USB MIDI device port and the 5-pin DIN MIDI output. Do not echo or thru MIDI received on either input. Keep the existing fixed MIDI channel 1 and note map.

## Recommended approach

### Output protocol and performance mapping

- Use MIDI channel 1 (status channel index 0), matching BOWLS MIDI input.
- Map physical key index 0..24 to MIDI note 48..72 (`note = 48 + key`) in every play mode. This matches the established note map, including Split's rim-selection keys 48..59 and strike keys 60..72.
- On a valid local musical key press, send Note On with velocity 127. The hardware has no per-key velocity sensor; 127 is the proposed fixed velocity so Cubase receives a full-strength note. On release, send Note Off for that note.
- Send MIDI only for local musical key presses/releases. SHIFT mode shortcuts are consumed controls, not notes, and produce no MIDI output. Rimming latch and sustain affect BOWLS' internal pad gate only: a physical key release still sends Note Off so a DAW cannot leave the corresponding note held. A local press/release in Split's rim zone is sent as the matching note gesture.
- Do not send parameter CCs, mode changes, MIDI clock, program changes, or other controls. Do not transmit incoming DIN/USB notes or CCs, and do not implement soft thru.
- Send each local note event to both enabled outputs. DIN and USB output failures/disconnects must not stall local audio or prevent the other output from progressing.

### Realtime-safe transport

- Keep event creation at the local key-event boundary, but do not perform blocking UART/USB transmission from the audio callback or control-scanning path.
- Use a bounded fixed-capacity handoff from local event processing to main-loop transport service. Preserve Note On/Off ordering. Ensure an output queue overflow cannot silently leave Cubase notes stuck: recover that output by sending Note Off for the locally tracked note set (or an equivalent narrowly scoped recovery) before resuming normal events; do not replay stale queued Note Ons after recovery.
- Use libDaisy's supported nonblocking/DMA send APIs and bound per-loop servicing. Keep USB device setup compatible with the existing USB MIDI input path. The current USB stack cannot also provide CDC serial logging; preserve the existing normal-build/CPU_METER distinction.
- Keep DIN and USB output servicing independent. A disconnected USB host or UART transmit error must not block audio, input parsing, or the other destination.

### Held-note and lifecycle behavior

- Track which local key Note Ons were actually queued per output. A later key release sends Note Off only for a note that output owns; shortcuts and other consumed keys must never generate stray Note Offs.
- Track transmitted local notes separately from `PlayModes` audio claims. Mode changes, MIDI input cleanup, or an internal pad latch transition must not erase the bookkeeping needed to send the eventual physical Note Off.
- On output queue recovery or transport reconnection, send targeted Note Off cleanup for notes that output may have left held, then resume with fresh events. Do not send stale events accumulated while a destination was unavailable.
- Preserve the existing MIDI input behavior and source ownership. In particular, received MIDI remains input-only even when both input and output are active.

## Affected files/components

- [code/src/MidiHandler.h](../code/src/MidiHandler.h): add bounded local-event fan-out and independent USB/DIN transmit service, preserving the existing receive queues and parser behavior.
- [code/src/chompi_main.cpp](../code/src/chompi_main.cpp): publish local musical key transitions to the MIDI output handoff; exclude consumed SHIFT shortcuts and all MIDI-originated events.
- [code/src/play_modes.h](../code/src/play_modes.h): use the existing local key-action boundary and provenance to distinguish musical key events from consumed shortcuts; avoid changing MIDI input ownership semantics.
- [code/src/MidiCcMap.h](../code/src/MidiCcMap.h): reuse the existing channel and note constants; add output helpers only if they remove meaningful duplication.
- [code/src/chompi_hardware.h](../code/src/chompi_hardware.h): verify the DIN TX pin is routed to a physical MIDI OUT connector and confirm USB path setup remains valid; change hardware configuration only if required.
- [code/tests/midi_test.cpp](../code/tests/midi_test.cpp) and [code/tests/Makefile](../code/tests/Makefile): extend the existing MIDI host tests for local output mapping, ordering, queue recovery, and no input echo.
- [midi.md](../midi.md): document MIDI Out and connection behavior, replacing the current receive-only description after implementation is verified.
- [README.md](../README.md): update the existing MIDI documentation link/connection note if needed.

## Step-by-step tasks, in order

1. Verify the board's DIN MIDI OUT connector is physically wired to the UART TX pin used by libDaisy; inspect the installed libDaisy MIDI send APIs and confirm USB MIDI supports transmit alongside the current receive path. Resolve any wiring/API mismatch before coding.
2. Add host-testable output event mapping and a bounded queue. Test keys 0..24 map exactly to notes 48..72 and that only local musical key events are eligible for output.
3. Integrate local key press/release publication. Verify SHIFT shortcuts, MIDI-originated notes/CCs, and non-note controls never enter the output stream. Preserve fixed velocity 127 and channel 1.
4. Add independent DIN and USB transmission in main-loop service using nonblocking transport APIs. Preserve per-output ordering and ensure failure/backpressure on one output cannot block the other or audio.
5. Add per-output active-note tracking and targeted recovery on queue overflow, transport errors, and USB disconnect/reconnect. Validate that queued stale Note Ons cannot survive recovery and that every transmitted Note On has a corresponding Note Off or explicit cleanup.
6. Document the output note map, fixed velocity, channel, both destinations, local-only behavior, Cubase routing, and the explicit lack of CC output/thru in `midi.md`; update the root guide link if needed.
7. Run focused MIDI host tests and all existing host regressions. Build firmware using the repository build skill and report SRAM, SRAM_EXEC, and SDRAM. Complete hardware acceptance with Cubase over USB, an external MIDI receiver over DIN, and both outputs simultaneously.

## Risks, open questions, memory/CPU/timing considerations

- **DIN output hardware:** current firmware initializes UART1 with TX on D14, but the firmware path alone does not prove a 5-pin MIDI OUT connector and its output circuitry are populated/routed on this hardware. This is a prerequisite for the requested DIN destination.
- **Fixed velocity:** the physical keyboard has no velocity sensing. This plan recommends velocity 127; confirm this choice before implementation if a lower fixed velocity or configurable velocity is desired.
- **USB mode conflict:** USB MIDI and USB CDC serial logging share libDaisy's global USB mode and cannot coexist in the current setup. USB output should follow the existing normal MIDI-build configuration; diagnostic CPU_METER builds continue with USB MIDI disabled unless their logging route changes separately.
- **Realtime deadline:** MIDI transmit must not block a 0.5 ms audio callback or key scanning. Queue capacity and main-loop drain budget should be measured under rapid key activity and simultaneous RX/TX.
- **Queue recovery:** losing a Note Off is musically worse than dropping an event. Per-output tracking and targeted Note Off recovery must cover overrun, TX failure, and USB reconnect without sending stale Note Ons or clearing unrelated notes on channel 1.
- **Note semantics:** BOWLS Rimming LOOP can keep its own pad gate open after physical release. MIDI output follows the physical key gesture, not the internal latch, to avoid hanging notes in Cubase. Verify this is the intended performance relationship in hardware.
- **Not included:** thru/echo, MIDI CC output for knobs/mode changes, configurable channels, velocity sensing/curves, MIDI clock, and outgoing preset/program selection.

## Testing and validation

### Host tests

1. Exhaust all 25 local key indices and assert channel-1 Note On/Off note numbers 48..72 with Note On velocity 127; invalid key indices produce no event.
2. Verify a local press/release preserves FIFO order, and shortcut-consumed presses do not enqueue notes or unmatched Note Offs.
3. Inject DIN/USB input notes and CCs while local notes are generated; assert no received message is emitted to either output (no echo/thru).
4. Test independent output queue dispatch, per-destination error handling, queue overflow recovery, and tracked-note cleanup without replaying stale Note Ons.
5. Test mode changes and Rimming latch/sustain transitions while a local key is held: transmitted Note Off occurs on physical release, independent of the internal pad gate and incoming MIDI cleanup.
6. Run `make -C code/tests midi-test` plus the existing play-mode and full host test targets.

### Hardware acceptance

1. Connect the panel USB-C port to a computer, select CHOMPI-BOWLS as the Cubase MIDI input, and verify all 25 physical keys trigger notes 48..72 on channel 1 with velocity 127 and release cleanly.
2. Repeat through the 5-pin DIN MIDI OUT into a hardware MIDI interface/receiver; verify the exact same notes and channel. Confirm the physical jack and TX level/cabling are correct.
3. Run both outputs simultaneously. Verify USB disconnect/reconnect and DIN TX error behavior do not interrupt BOWLS audio or the remaining output.
4. Hold/release notes across mode changes; exercise SHIFT shortcuts, Split rim-zone keys, and Rimming LOOP/sustain. Confirm no shortcut notes or stuck Cubase notes.
5. Send MIDI into BOWLS while monitoring its outputs; confirm incoming DIN/USB events are never echoed. Verify no CCs are emitted when changing panel controls.
6. Stress rapid key activity with simultaneous MIDI input and staged SD autosave. Confirm bounded event latency, no audio dropouts, and recovery from any deliberately induced queue pressure.
7. Build normal firmware and report SRAM, SRAM_EXEC, and SDRAM. Verify the CPU_METER build retains its documented USB serial behavior and that DIN output remains available there if the UART TX path is independent.
