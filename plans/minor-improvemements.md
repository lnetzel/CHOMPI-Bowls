# Minor Improvements: MIDI Strike Velocity and Rimming Latch Across Modes

Status: **planned**.
Intended repository destination: `plans/minor-improvemements.md`.
Links below are relative to that destination.

Important: Pause and warn if implementation uses more than 100 credits

## Problem and goal

This plan contains three related performance improvements.

1. MIDI strike velocity currently scales strike amplitude, while the global
   Strike hardness control (encoder 2 / CC 78) independently sets strike
   hardness. Make velocity expressive in both dimensions: higher MIDI velocity
   should produce a harder, brighter strike and a louder one; lower velocity
   should produce a softer, quieter strike.
2. The Rimming LOOP latch currently clears when leaving Rimming, releasing
   latched-only notes. Preserve the active latch and its notes when switching
   from Rimming to Strikes, and protect those latched voices from being stolen
   by Strikes-mode notes. Preserve existing latch cleanup when switching to
   Split.
3. Increase the maximum strike ring duration from about 30 seconds to around
   45 seconds by extending the per-bowl decay control's maximum multiplier
   from 2× to 3×.

For MIDI strikes, velocity directly overrides the global hardness setting for
that strike. The global control continues to set hardness for local keyboard
strikes. Keep velocity's existing amplitude scaling, and do not let the saved
per-bowl volume be changed by MIDI performance.

When switching from Rimming to Strikes, retain the Rimming latch state and keep
its latched voices sounding. Those voices are reserved against voice stealing
by Strike-mode notes. If all five voices are occupied by protected latched
Rimming notes, ignore a new Strike-mode note rather than stealing a latched
voice. Switching to Split retains today's behavior: leaving Rimming disables
the latch and releases latched-only pads. The latch remains transient and is
not persisted.

For the longer ring, preserve the current 0.25× minimum and raise the decay
control's upper endpoint to 3×. The Deep profile's 15-second lowest-mode T60
will then reach approximately 45 seconds at maximum decay. This expands the
maximum for every profile; it does not change the underlying profile T60 values.

## Recommended approach

Separate strike hardness/timbre from strike amplitude in the strike path.
Currently, `Strike()` uses `strike_norm` for both the strike's intensity
baseline and its timbral shaping, while the MIDI caller passes velocity as the
amplitude factor. Give the strike path distinct inputs for hardness and
amplitude, keeping local keyboard behavior unchanged and making MIDI velocity
the hardness input for MIDI strikes.

Map MIDI velocity 1-127 linearly to normalized hardness 0-1 and retain the
existing normalized velocity amplitude scaling. Keep the existing velocity-0
Note On behavior as Note Off. Do not change pad or rim-selection velocity
handling, CC 78's local behavior, or saved sound parameters.

At velocity 127, MIDI strikes should use the hardest strike timbre and the
existing full MIDI velocity gain. At velocity 1, they should use the softest
timbre and the existing minimum MIDI velocity gain. Validate intermediate
velocities for a smooth timbral and loudness progression; avoid introducing
discontinuities or clipping.

Extend the per-bowl decay mapping from 0.25×–2× to 0.25×–3×, keeping the
existing exponential control curve and live-update behavior. Update the
firmware control display and user documentation to state the new range.
Confirm the default setting and intermediate encoder/CC values remain
intentional with the revised curve; the 3× endpoint should produce about 45 s
T60 for the Deep profile's longest mode.

## Affected files/components

- [`chompi_main.cpp`](../code/src/chompi_main.cpp) — `StrikeKey()` selects
  hardness and amplitude inputs for local versus MIDI-triggered strikes.
- [`bowl_voice.h`](../code/src/bowl_voice.h) — separate strike hardness from
  amplitude shaping in `BowlVoice::Strike()` while preserving the local
  keyboard's current sound; confirm the longer T60 remains finite and stable.
- [`chompi_main.cpp`](../code/src/chompi_main.cpp) — update the decay tick
  conversion and user-facing control range from 2× to 3×.
- [`README.md`](../README.md) and [`midi.md`](../midi.md) — document the
  extended decay range and approximate longest ring.
- [`play_modes.h`](../code/src/play_modes.h) — existing MIDI velocity claims
  and note routing; verify the strike velocity reaches `StrikeKey()` without
  changing shared pad/rim claim semantics, and preserve latch state only for
  Rimming-to-Strikes transitions.
- [`chompi_main.cpp`](../code/src/chompi_main.cpp) — protect latched Rimming
  voices in the shared voice allocator while in Strikes mode; handle the
  no-available-voice case without stealing a protected voice.
- [`midi_test.cpp`](../code/tests/midi_test.cpp) and
  [`strike_test.cpp`](../code/tests/strike_test.cpp) — cover velocity routing,
  hardness, amplitude, and local-strike regressions.
- [`play_modes_test.cpp`](../code/tests/play_modes_test.cpp) and the strike /
  voice-allocation test coverage — verify latch transitions, protected voices,
  and the full-pool behavior.
- [`strike_test.cpp`](../code/tests/strike_test.cpp) — cover the 3× decay
  endpoint for finite, bounded output and validate the longest-mode decay.
- [`midi-input-and-cc-control.md`](./midi-input-and-cc-control.md) and
  [`README.md`](../README.md) — update MIDI velocity and cross-mode latch
  behavior documentation.

## Step-by-step tasks

1. Trace the existing velocity path from MIDI Note On through shared play-mode
   claims to `StrikeKey()`. Confirm local keyboard strikes still use full
   velocity and the global hardness setting.
2. Refactor the strike API so hardness and amplitude are independent inputs.
   Preserve the local keyboard's current strike response and existing
   velocity-dependent MIDI amplitude behavior.
3. For MIDI strikes, map velocity to hardness and use that value in timbral
   shaping, independently of the global hardness control. Keep the global
   control's local-strike behavior unchanged.
4. Leave MIDI pad/rim performance gain, per-bowl saved volume, velocity-zero
   Note Off, and all non-strike note semantics unchanged.
5. Add host-test coverage for velocity endpoints and intermediate values,
   independence from global hardness for MIDI strikes, local hardness
   behavior, and preservation of the MIDI loudness scaling.
6. Update MIDI-facing documentation and run the focused MIDI and strike tests,
   then the relevant broader host regression suite.
7. Change mode-exit latch handling so Rimming-to-Strikes leaves LOOP enabled,
   preserves latched claims and pad gates, and does not clear their deferred
   state. Keep Rimming-to-Split cleanup as it is today; confirm returning to
   Rimming still finds the original latched notes active.
8. Update voice allocation so a Strike-mode note cannot take a voice occupied
   by a latched Rimming note. Continue normal stealing among unprotected
   voices; if every voice is protected, drop the incoming strike cleanly.
   Ensure ordinary Rimming latch toggles and note release still work after
   returning to Rimming.
9. Keep latch feedback observable while preserved in Strikes mode, and update
   the README / mode behavior documentation to distinguish the Strikes
   transition from the existing Split cleanup.
10. Add state-machine and voice-allocation tests for Rimming-to-Strikes latch
    preservation, protected voices, all-five-protected note rejection,
    returning to Rimming, and Rimming-to-Split cleanup.
11. Raise the per-bowl decay conversion's maximum from 2× to 3× while retaining
    the 0.25× minimum and live updates. Review the revised tick-to-multiplier
    curve, including its default and midpoint, and document the new behavior.
12. Confirm the Deep profile's longest mode reaches about 45 seconds at 3×,
    add host regression coverage for decay bounds and stable rendering at the
    maximum, and update the README and MIDI CC documentation.

## Risks, open questions, memory/CPU/timing considerations

- Hardness changes the excitation spectrum and contact behavior, not just
  perceived loudness. Verify the low-velocity sound remains audible and the
  high-velocity sound remains musically useful across the bowl range.
- MIDI strike timbre will no longer follow encoder 2 / CC 78; this is
  intentional because velocity overrides global hardness for MIDI strikes.
  Local keyboard strikes remain governed by the global control.
- Preserved latched Rimming notes consume the shared five-voice budget while
  Strikes mode is active. When they occupy the entire pool, new strikes are
  intentionally dropped rather than interrupting the latch.
- Keep mode transitions precise: Rimming-to-Strikes preserves latch state;
  Rimming-to-Split clears it as today. Held-but-not-latched Rimming notes
  retain their current release behavior.
- If latch status is visibly indicated outside Rimming, ensure the LOOP LED
  and latched-key indication accurately reflect the preserved state without
  making LOOP toggles active in Strikes or Split.
- Extending the decay multiplier affects every per-bowl profile and changes
  the mapping of saved decay ticks if the existing exponential curve is
  rescaled. Backwards compatibility with existing presets is not required;
  review the revised tick mapping and check CPU/state stability at 45-second
  T60.
- The new per-note mapping should require only scalar arithmetic and existing
  strike parameters. It should add no per-sample work, memory allocation, or
  persistent state.
- Keep MIDI amplitude scaling independent so hardness is not accidentally
  doubled into loudness. Check peak level and clipping at velocity 127.

## Testing and validation

- Extend `code/tests/strike_test.cpp` to verify that harder strikes change
  timbral excitation independently of amplitude, and that local strikes retain
  their current global-hardness response.
- Extend `code/tests/midi_test.cpp` or the existing MIDI integration coverage
  to verify velocity 1, intermediate velocities, and 127 reach the intended
  strike hardness and existing MIDI amplitude scale. Confirm changing CC 78
  does not alter MIDI strike hardness.
- Verify velocity-zero Note On remains Note Off, and pads/rim selections retain
  their current velocity-gain behavior.
- Extend `code/tests/play_modes_test.cpp` to verify Rimming-to-Strikes retains
  LOOP and latched-only pad gates, returning to Rimming preserves them, and
  Rimming-to-Split still clears the latch and releases latched-only pads.
- Test voice allocation with mixed normal and latched voices: Strikes may use
  or steal only unprotected voices, and a new strike is rejected when all five
  are protected. Confirm a protected voice is not modified or retagged.
- Verify latch visual feedback and normal latch release/toggle behavior after
  returning to Rimming; ensure held, non-latched notes retain existing mode
  exit behavior.
- Test the decay mapping at minimum, default, and maximum ticks; confirm the
  maximum maps to 3× and the Deep profile's longest mode is approximately
  45-second T60. Render at maximum decay to check finite output and normal
  voice-idle/tail behavior.
- Listen across low, middle, and high bowl pitches for monotonic hardness and
  loudness, usable low-velocity strikes, and absence of clipping.
- Run the focused MIDI and strike tests, then `make -C code/tests test` (and
  the firmware build if available) to catch regressions.
