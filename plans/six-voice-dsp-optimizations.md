# Incremental DSP Optimizations for Six Voices

## Problem and goal

The firmware currently has five voices, each with eight resonator pairs.
Increasing the pool to six adds 20% more voice DSP when all voices are
sounding. Identify and implement the previously discussed optimizations as
separate, reviewable steps, preserving the current sound and letting the user
listen to a normal firmware build after each step before any further change.

Do not add firmware diagnostics, special CPU-meter builds, serial logging, or
other instrumentation as part of this work. The user's listening test is a
required acceptance gate between optimization steps.

## Recommended approach

- Keep each optimization in its own change. Run the relevant host tests, build
  the ordinary firmware, and then stop for the user to flash and listen.
- Do not begin the next optimization until the user says the current build
  still sounds right. If it does not, revert or revise only that step.
- Use the user's usual presets and playing style for listening, including
  strikes, sustained/rubbed sounds, and stereo movement where applicable.
- Increase the voice count only after the optimizations have been accepted.

## Affected files and components

- Per-voice resonator, swirl modulation, and sample loop:
  [bowl_voice.h](../code/src/bowl_voice.h)
- Compile-time voice pool and audio mixing/allocation:
  [chompi_main.cpp](../code/src/chompi_main.cpp)
- Existing DSP render and regression tests:
  [render_test.cpp](../code/tests/render_test.cpp) and
  [strike_test.cpp](../code/tests/strike_test.cpp)
- Voice allocation tests and host test targets:
  [play_modes_test.cpp](../code/tests/play_modes_test.cpp) and
  [Makefile](../code/tests/Makefile)

## Step-by-step tasks

1. **Reduce rotating-shimmer trigonometry.**
   - In `BowlVoice::UpdateLife`, replace the per-block sine/cosine
     recalculation for each pair with a phase recurrence or another bounded
     approach that retains the existing randomized starting phase, rate,
     stereo quadrature, depth, and smoothed gain behavior.
   - Bound accumulated floating-point drift, reusing the existing oscillator
     renormalization approach where appropriate.
   - Add a focused host test for repeatability and long-run phase/modulation
     bounds; run the existing render and strike tests.
   - Build the normal firmware and stop for the user's listening test,
     especially the shimmer's stereo motion and tone. Do not proceed without
     approval.

2. **Skip fully muted resonator pairs.**
   - Track whether both members of a pair are actually muted after coefficient
     updates. Skip the resonator arithmetic only for fully muted pairs; retain
     processing for a partially muted pair.
   - Preserve any oscillator, LFO, or other per-pair state that must continue
     advancing. Clear muted resonator state at the appropriate transition so
     an old tail cannot reappear when tuning makes that pair audible again.
   - Add focused host coverage for high notes with muted modes and for
     retuning across the audible/muted boundary; ensure active modes and tails
     remain unchanged.
   - Run the relevant render and strike tests, build the normal firmware, and
     stop for the user's listening test before proceeding.

3. **Hoist mode-constant work from the sample loop.**
   - Move per-mode values such as the brightness multiplier out of the
     per-sample loop when they are constant for the block.
   - Keep this change narrow. If the optimized build already hoists the
     calculation, or the source change would add complexity without removing
     repeated work, omit this step rather than making a cosmetic rewrite.
   - Verify output remains sample-identical where practical and run the render
     and strike tests. Build the normal firmware and wait for the user's
     listening approval.

4. **Increase the voice pool to six.**
   - Change the central voice-count constant from five to six; keep the shared
     allocator and all pool loops driven by that constant.
   - Extend allocator coverage for six entries if existing tests do not
     already cover that case.
   - Run the render, strike, and mode/allocation tests; build the normal
     firmware and stop for the user's six-voice overlap test. Confirm both
     that six notes can sound together and that the previously accepted
     five-voice sound is unchanged.

## Risks, open questions, and memory/CPU/timing considerations

- A recurrence can accumulate numerical drift or slightly alter the shimmer.
  Keep the original rates and depth, bound drift, and let the user judge the
  result before continuing.
- Muted-mode skipping must account for retuning, partial pairs, tails, and
  stateful modulation; an incorrect mute test could cause a pop or a stale
  resonance to return.
- The compiler may already hoist some invariant calculations at `-O3`; the
  third step is explicitly conditional on there being real repeated work.
- The voice-count increase raises the active voice DSP workload by about 20%.
  Code inspection alone cannot guarantee timing headroom, so do not promise
  six-voice reliability or trade away sound quality without the user's
  agreement.
- This plan uses ordinary firmware builds only. No diagnostic mode or added
  firmware telemetry is required.

## Testing and validation

- After each DSP change, run the focused host suites from the repository root:
  `make -C code/tests test strike-test`.
- After the voice-count change, also run
  `make -C code/tests modes-test` for allocation and mode behavior.
- Build the ordinary firmware after each accepted code step; do not use a
  CPU-meter build.
- After each build, the user flashes and listens before implementation moves
  to the next step. Reuse familiar presets and compare the same strikes,
  sustained notes, and stereo behavior. Treat any unwanted audible change as
  a stop condition, not something to mask with further changes.
