# Six-Voice Polyphony

Status: **parked** (2026-11-07, user decision). The five-voice hardware
configuration remains the supported default. Do not spend further effort on
six-voice investigation unless the user reopens it.

## Problem and goal

The firmware currently has five shared voices. With long bowl decays, a new
note can steal a still-musically-important tail even in sparse playing.
One more independent voice would improve overlapping phrases, drones and
ensemble textures.

An earlier six-voice experiment produced audible artifacts despite the CPU
indicator not appearing overloaded, and the instrument returned to five.
Treat this listening observation as evidence of a problem, not proof of
CPU exhaustion. The cause remains unconfirmed.

**Primary goal:** six genuinely independent, clean-sounding voices with
measured real-time headroom, preserving the accepted bowl sound and UI.

**Non-goals for this iteration:**

- More than six voices, unless considered in a separately approved follow-up.
- New modes, damping gestures, dynamics controls, effects or preset schema.
- Unifying Split's paired strike/rim voice identities. That could improve
  effective polyphony, but changes instrument identity, settings and
  retrigger behavior; it is not a substitute for an actual sixth voice.
- Reducing modal richness, shortening tails or changing sample rate to
  claim a higher voice count.

## Recommended approach

### Diagnose before optimizing

#### Prerequisite: Rimming attack on occupied-voice reuse

The player reports that, with all five voices occupied in Rimming mode,
some new notes appear to skip the 10-second attack. Investigate and fix
this at five voices before evaluating a sixth, so an existing allocation
artifact is not confused with a processing-capacity problem.

Code inspection identifies a likely mechanism, not yet a reproduced
diagnosis: `StartPadKey` allocates a voice and calls `SelectForRub`, which
resets the pad envelope but preserves the previous resonator energy and
retunes it to the new pitch. Thus a freshly zeroed excitation envelope
does not guarantee a fresh audible attack. Previous wheel smoothing and
any unfinished strike burst also need checking on identity changes.

Required behavior and investigation:

- Distinguish a different bowl taking over a voice from reopening the
  same bowl's gate. Do not blanket-reset all selections or gate changes.
- A different bowl must not inherit audible old-bowl energy, wheel
  excitation or a strike burst that bypasses its intended slow entrance.
  Keep intentional wheel drive available as a separate performance input.
- Design a bounded, click-free handoff before retuning/reinitializing a
  stolen voice. A hard state clear alone can click; retaining and retuning
  the old state can make the new pitch jump in. Measure any fade/handoff
  delay and its CPU/memory cost; do not assume an extra full voice is free.
- Preserve intended same-bowl continuation and existing Split behavior.
  Verify the production pad-start path: the existing direct gate-reopen
  test alone does not establish how `StartPadKey` behaves.
- Preserve precise latch/held-state cleanup for the stolen identity,
  including rapid additional key presses during any handoff.
- Add an audio-output regression reproducing saturated-pool pad starts,
  then verify the fix on hardware. Envelope values alone are insufficient.

Establish reproducible five- and six-voice builds from the same source,
toolchain and settings. Distinguish:

1. Audio deadline failures or interrupt/scheduling interference.
2. Excess signal level and nonlinear output distortion.
3. Voice stealing, retrigger damping or other state transitions.
4. Numerical or DSP defects specific to the additional active voice.

Keep the five-voice build as the reference until six passes the hardware
gates. Do not silently substitute fewer active voices under load.

### Measure the complete real-time workload

The current audio quantum is 24 samples at 48 kHz: **500 microseconds**.
The existing CPU measurement ends before its callback-side serial print,
so that print's cost is not included in the reported measurement.
The LED is an indication, not proof that no deadline was missed.

- Remove diagnostic printing from the audio callback; publish bounded
  counters/snapshots and print at a limited rate from the main loop.
- Use an existing platform cycle counter/timing facility if suitable.
  Measure callback elapsed time, interval between callback entries and
  the expensive phases: controls, voice rendering and effects/output.
- Preserve lifetime maxima and over-budget counts until an explicit
  diagnostic reset; do not rely solely on a decaying LED peak.
- Inspect the libDaisy audio/DMA path before defining missed-deadline
  instrumentation. An elapsed callback above 500 us is an over-budget
  callback, not a complete detector for late entry or DMA underruns.
  Use available DMA/error flags or a verified completion-deadline
  measurement where possible, and document detection limitations.
- Keep collection allocation-free and bounded, with safe ISR/main-loop
  handoff. Do not disable interrupts around logging or bulk copies.
- Measure diagnostic overhead and confirm the release build as well.

### Separate processing capacity from mix headroom

The stereo voice sum and reverb feed a nonlinear output soft clip.
More voices can increase distortion without increasing the CPU load
enough to indicate overload.

- Measure pre-master bowl-bus peaks, reverb return peaks and pre-clip
  output peaks, with finite-value checks.
- Compare five and six with the same per-voice settings, then compare
  externally level-matched recordings for listening.
- For a headroom experiment, apply a known attenuation before any
  suspected overloaded stage. Turning down the final volume alone
  cannot diagnose an earlier nonlinear stage.
- If gain staging is the cause, propose the smallest fixed correction
  and document its loudness effect before accepting it. No automatic
  voice-count gain pumping or concealed sound changes.

### Optimize only demonstrated costs

The per-block stereo movement in `BowlVoice::UpdateLife` currently uses
sine/cosine calculations for every mode pair of every active voice.
Wandering detune can also trigger coefficient recalculation; sustained
clean pads/rimming add oscillator work. These are candidates, not
established bottlenecks.

Profile representative and worst-case paths first. Prefer equivalent
math, invariant hoisting and removal of redundant work. If measurements
justify changing LFO evaluation or coefficient scheduling, bound the
resulting timing, phase and audio error with regression tests.

Do not prune audible modes, truncate tails or change modulation depth
and rate as an unannounced optimization. Preserve the 24-sample block
initially; larger blocks trade latency for overhead and require a
separate decision if simpler measures cannot meet the target.

## Affected files/components

| Component | Planned responsibility |
|---|---|
| [chompi_main.cpp](../code/src/chompi_main.cpp) | Diagnostic handoff, phase timing, headroom measurements, five/six comparison, final voice-count change and stealing integration. |
| [bowl_voice.h](../code/src/bowl_voice.h) | Investigate inherited resonator/excitation state on pad steals; implement a validated handoff and measured optimizations. |
| [Makefile](../code/src/Makefile) | Reproducible diagnostic and release builds; narrowly scoped build-time voice-count override if needed, not a saved user setting. |
| [chompi_hardware.h](../code/src/chompi_hardware.h) | Inspect control/audio initialization and timing; change only if measurements establish a relevant issue. |
| [play_modes.h](../code/src/play_modes.h) | Verify latch/held-state cleanup after steals; preserve existing behavior. |
| [PresetStorage.h](../code/src/PresetStorage.h) | Exercise autosave under load; alter only if a coupled timing problem is demonstrated. |
| [host tests](../code/tests/Makefile) | Add deterministic multi-voice coverage and run existing sound/state regressions. |
| [libDaisy audio implementation](../code/libs/libDaisy/src/hid/audio.cpp) | Verify actual callback/DMA scheduling and available error signals before claiming deadline detection; confirm path during implementation. |
| [linker layout](../code/src/chompi_sram.lds) | Check SRAM and executable-region capacity; do not relocate hot data speculatively. |
| [README](../README.md) | Update the released voice count, stealing explanation and diagnostic instructions only after acceptance. |

Prefer extending existing tests; add a focused host polyphony test only
where existing runners cannot express the required integration coverage.

## Step-by-step tasks, in order

1. **Capture the reference.** Record revision, compiler, build flags,
   settings, pitches and exact gesture sequences. Preserve an identifiable
   five-voice binary and recordings. Confirm what can be recovered about
   the earlier experiment without assuming it matches today's DSP.
   Reproduce and record the reported Rimming attack problem before fixing it.
   **Before step 2**, implement and validate the occupied-voice pad-attack
   fix at five voices. Keep both the original and corrected five-voice
   references; use the corrected one for the capacity comparison.
2. **Implement bounded diagnostics.** Move callback logging to the main
   loop, add timing/headroom counters and verify measurement overhead.
   Establish what timing faults can actually be detected.
3. **Reproduce the difference.** Compare five and six using identical
   sequences, both diagnostics enabled and disabled. Include six held
   bowls and sequential strikes with overlapping tails; separate the
   sixth-note start from steady-state rendering.
4. **Classify artifacts.** Correlate recordings with timing, peak levels
   and steal events. Use controlled attenuation and reverb isolation
   experiments; disabling an effect is diagnostic, not the final solution.
5. **Address the measured cause.** Make one coherent optimization/fix at a
   time, repeat the comparison, and retain only changes with evidence of
   benefit and preserved musical behavior.
6. **Validate six-voice integration.** Exercise all modes, seventh-note
   steals, latch cleanup, outgoing-mode tails, live edits and autosave.
7. **Choose the release result.** Set six as the default only after the
   tests below pass. Update related documentation and report timing,
   memory, headroom and listening results. If blocked, retain five and
   report the measured limiting case and next decision explicitly.

## Risks, open questions, memory/CPU/timing considerations

- The earlier artifact's character and cause are not established. Obtain
  listening confirmation from the player, preferably with matched clips.
- Six active voices add 20% to voice-dependent work relative to five, not
  necessarily 20% to the whole callback. Idle voices do not represent
  the sustained worst case.
- Simultaneous new notes, coefficient updates and live shimmer/decay
  changes can cost more than steady ringing. Test clustered events.
- Main-loop SD/LED/logging work can interfere through interrupts, DMA
  or memory contention even if FatFS is outside the callback.
- Account for the extra `BowlVoice` state and diagnostic storage. Report
  SRAM, SRAM_EXEC and SDRAM usage for every firmware build using the
  repository build workflow; ensure all regions link with headroom.
- Keep the known-good compiler and optimization flags. Host benchmark
  speed is not evidence of STM32 real-time capacity.
- Compiler or approximation changes may alter sound or numerical
  stability. Prefer existing bit-exact checks; quantify any necessary
  deviation rather than simply regenerating baselines.
- Proposed timing gate: maximum complete callback elapsed time at or
  below **400 us** in the defined stress runs (20% quantum reserve),
  with zero observed over-budget callbacks or verified deadline/error
  events. Record late-entry evidence separately; this reserve is not a
  guarantee against every possible external delay.

## Testing and validation

### Host regression

Run the existing related targets from the configured MSYS2 toolchain:

```text
make -C code/tests test strike-test modes-test routing-test
```

In PowerShell use the corresponding Windows path argument or the existing
repository build workflow. Do not replace missing baseline checks with
an unconditional pass; report any unavailable reference as a validation gap.

Add deterministic five/six-voice renders that cover:

- Six distinct contributions before a seventh-note steal; no early steal.
- Repeated strikes, long overlapping tails and quiet tail survival.
- Saturate five voices with mature pads, start a different sixth pad with
  the wheel stopped, and measure its isolated rendered output through the
  actual allocation/start path. Compare its initial level and attack
  progression against a fresh pad at the same pitch/settings, allowing
  only the explicitly bounded handoff interval.
- Repeat pad takeover after clean/textured wheel drive, during a strike
  burst and from outgoing-mode voices. Check both channels, transition
  discontinuities and absence of old-pitch/new-pitch energy carryover;
  test same-bowl reopening separately to protect intended continuation.
- Clean pads, textured rimming and direction crossfades.
- Extreme supported tuning/profile/decay/brightness/shimmer combinations.
- Finite output, measured peaks/RMS and discontinuity checks relative to
  the same input sequence before optimization.
- Seventh-note quietest-voice stealing and exact stolen-note latch
  cleanup through the production allocation path. Extract only a small
  testable helper if necessary; do not test an unrelated duplicate allocator.

### Hardware stress and listening

Use the same settings for five/six comparisons; capture at a safe recording
level and level-match outside the firmware for listening.

| Scenario | Required observation |
|---|---|
| Six overlapping strikes and clustered note starts | Sixth note enters without stealing; no crackle, dropout or new harsh distortion. |
| Six clean held/latched pads | Stable sustained sound and timing through at least 10 minutes. |
| Both wheel directions, reversals, stop/restart | Clean transitions under the maximum reachable wheel-driven load in each mode. |
| Seventh note, re-hits and mode changes | Intended steal/retrigger only; no stuck pad, ghost relatch or unexpected tail interruption. |
| Occupied-pool Rimming pad starts, at five and six voices | Every different new bowl enters through its slow attack without inherited loud onset; stolen-note handoff is click-free, including rapid replacement. |
| Live parameter changes and autosave | No deadline faults or audible artifacts while saves and LED updates run. |
| Long high-decay tails and extreme supported settings | No instability or new noise; intended tail length preserved. |
| Normal/Send FX, reverb and supported gain extremes | Headroom behavior understood; no new six-voice artifact relative to the reference. |

Run each demanding steady-state case for at least 60 seconds and a combined
performance/autosave soak for at least 10 minutes. Repeat the worst cases
without diagnostic printing/instrumentation in the final six-voice release.

Acceptance requires passing host regressions, the timing gate, successful
memory/link checks, and player-confirmed clean hardware recordings.
Document tested settings and limits; do not claim hardware validation from
host tests or a successful build alone.

## Implementation record (2026-11-07)

- The production pad-start operation distinguishes same-identity reopening
  from a different bowl taking over. A takeover keeps the old tuning active
  while the outgoing state fades for 240 samples (5 ms), suppresses old
  excitation, clears resonators, then applies the pending pitch/profile/
  decay/shimmer before the new pad attack begins. Rapid replacement updates
  the pending identity; same-bowl reopening keeps the current envelope.
- The allocator now lives in `code/src/voice_pool.h`, shared by firmware and
  host tests. `VOICE_COUNT=5` or `VOICE_COUNT=6` selects the pool size at
  build time; the default is still five.
- For comparable variants, run from `code/src` with the same compiler and
  optimization flags: clean before each build, then use
  `make -j4 DEBUG=0 OPT=-O3 CPU_METER=1 VOICE_COUNT=5` and repeat with
  `VOICE_COUNT=6`. Release builds omit `CPU_METER`; clean again when changing
  either build variable because the Makefile does not track define changes.
- `CPU_METER=1` enables bounded callback-entry, phase, bus/reverb/output peak,
  and finite-value diagnostics. Serial output is rate-limited in the main
  loop. Maxima and saturating event counts persist until reboot.
- `DIAG_LED_ONLY=1` suppresses the numeric audio serial report. In this
  image, Shift is the callback-load bar (green to red); encoder 1-4 LEDs show
  controls/voices/effects/output phase maxima against 500 us; the white-key
  LEDs show bus/reverb/pre-clip peaks as red/green/blue bars; wheel-left red
  latches any callback over 400 us; wheel-right blue latches any entry gap
  over 500 us; Shift blue indicates non-finite output. The Play LED flashes
  white on a voice steal. Reboot to clear latched diagnostics.
- libDaisy dispatches the application callback from SAI half/full DMA events,
  but its current SAI source exposes no audio-underrun/error counter. The
  measured entry interval is therefore a scheduling proxy, not a verified
  DMA-deadline detector. Application callback timing excludes libDaisy's
  sample-format conversion and DMA handling around that callback.
- Host coverage exercises five/six pad renders, allocator/latch decisions,
  saturated-pool rapid replacement against a fresh-pad attack progression,
  both wheel characters, and strike-burst takeover. It does not replace
  hardware checks of those transitions, long soak, or listening.
- The player confirms the five-voice configuration works on hardware; use it
  as the known-good comparison reference. This does not establish six-voice
  timing or listening acceptance.
- The player reports audible digital artifacts with six voices on hardware
  (2026-11-07). The player describes the noise as sample-rate-reduction-like.
  It is heard in Rimming when voice six is enabled, but not in Striking. This
  fails the six-voice listening gate; cause remains unclassified, so do not
  infer CPU exhaustion or promote six as the default.
- During the six-voice LED-only diagnostic run, the player observed wheel-left
  red, wheel-right blue, Shift red, the first white-key LED white, and the
  second white-key LED red. The latched wheel indicators mean at least one
  callback exceeded 400 us and at least one callback-entry interval exceeded
  500 us. This fails the proposed timing gate; the entry interval is only a
  scheduling proxy, and the observation does not establish that timing caused
  the audible artifacts. The key colors are peak-bar data, not event counts.
- The player reports the five-voice diagnostic sounds clean, while the
  six-voice diagnostic produces repeated digital noise, usually when the
  sixth voice starts (2026-11-07). This strengthens the association with
  enabling the sixth voice but does not classify the cause. The five-voice
  full diagnostic also showed the timing warning LEDs, so compare with a
  lower-overhead timing-only build. Test the sixth-note start separately from
  six-voice steady state and correlate it with timing.
- The player confirms timing-warning LEDs appeared on both five- and
  six-voice versions; the major audible difference is Rimming with voice six.
  The player chose to stop pursuing six-voice polyphony to avoid further
  investigation cost. Preserve these findings; do not run the proposed
  timing-only comparison or continue diagnosis without renewed approval.
- Next diagnostic sequence: compare five and six held Rimming pads with the
  wheel stopped, note whether noise is immediate at the sixth pad or starts
  only in steady state, then compare six Striking voices. Record the latched
  timing indicators at each point; identical warnings do not by themselves
  establish the cause of the different sound.
- Software verification on 2026-11-07: all required host targets passed
  (`make test strike-test modes-test routing-test`), and both five/six
  diagnostic and release builds linked with Arm GNU 10.3.1. Diagnostic maxima are not yet
  validated on hardware; elapsed callback timing also excludes libDaisy's
  sample conversion and DMA handling as noted above.
- Linked usage, in bytes: five diagnostic SRAM_EXEC/SRAM/SDRAM =
  166832/39536/2755412; six serial-report diagnostic =
  167424/40848/2755412; five LED-only diagnostic =
  167360/39528/2755412; six LED-only diagnostic = 168000/40840/2755412;
  five timing-only LED diagnostic = 165760/39528/2755412; six timing-only LED
  diagnostic = 166368/40840/2755412; five release = 164160/39460/2755412;
  six release = 164784/40772/2755412.
  The final default firmware artifact is the five-voice release build.
- Current serial-free comparison images are
  `code/src/build/CHOMPI_BOWLS_v1.0_5VOICE_TIMING_DIAG.bin` and
  `code/src/build/CHOMPI_BOWLS_v1.0_6VOICE_TIMING_DIAG.bin`; both use
  `CPU_METER=1 DIAG_LED_ONLY=1 DIAG_TIMING_ONLY=1` with their respective
  voice-count overrides. The default five-voice release is
  `code/src/build/CHOMPI_BOWLS_v1.0.bin`.
