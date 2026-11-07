# Stereo BBD Delay

Status: **implemented**. The DSP port is algorithm-identical to the CHOMPI-PIPES `StereoBbdDelay` class; host delay/preset suites and the firmware build pass. Hardware audition, delay/reverb interaction checks, and worst-block CPU measurement remain. DSP reference: `C:\Dev\CHOMPI-Pipes\code\src\StereoBbdDelay.h`; its engine and UI integration are in `OrganEngine.h` and `chompi_main.cpp` in CHOMPI-PIPES.

## Problem and goal

Add the same warm, stereo BBD-inspired delay used by CHOMPI-PIPES to CHOMPI-BOWLS. Preserve the source DSP behavior and parameter mappings, place the delay before Bowls' existing reverb, expose it through the volume encoder, and keep the existing reverb control available one page later.

## Recommended approach

Port `StereoBbdDelay.h` without changing its DSP constants or processing algorithm. It is a standalone, header-only class with no Daisy dependencies and can be tested natively. Add only the Bowls-side initialization, control mapping, persistence, audio-chain, and LED integration needed to use it.

The effect's source behavior to retain:

| Property | Source implementation |
|---|---|
| Delay lines | Two independent, caller-owned float buffers, each 32,768 samples (128 KiB); total 256 KiB. Power-of-two wrapping and fractional linear interpolation. |
| Stereo image | Independent left/right delay reads with fixed +/-0.5% spread and slow independent modulation: left 0.5 ms at 0.25 Hz, right 0.7 ms at 0.37 Hz. No channel cross-feedback or mono summing; it is not ping-pong. |
| Delay time | 40-500 ms. Target changes slew over about 80 ms, producing the BBD-like pitch glide without clearing history. |
| Feedback | 35% default, up to 120%; repeats pass through a 2.8 kHz low-pass and 40 Hz DC-blocking high-pass. Feedback above about 100% self-oscillates. |
| Stability | Input plus feedback is bounded by the source cubic soft clipper (absolute output at most 2/3); there is no runaway loop. No added hiss or clock noise. |
| Mix | Linear complementary dry/wet crossfade, ramped over about 10 ms. Settled mix 0 is bit-exact dry; 1 is wet only. Lines, filters, and modulation continue at dry mix so echo tails/history survive mix changes and preset loads. |
| Boot state | Dry mix, 35% feedback, 250 ms inside the DSP. The source engine's saved default time position is 15/32, mapping to 256 ms; use that same control default when initialized from Bowls. |

### Audio placement

In [chompi_main.cpp](../code/src/chompi_main.cpp), process the stereo voice mix through the delay before the existing reverb send filter and `reverb.Process()` call:

```text
bowls/voices -> StereoBbdDelay -> existing reverb-send filter -> Reverb -> master/output routing
```

The reverb dry path must use the delay's output, not the original voice mix, so both delayed and undelayed signal reach the reverb in the intended order. Keep the existing reverb wet/dry control and output routing semantics unchanged. The delay must run continuously even at 0% mix. Do not clear the delay history when the user changes mix/time/feedback or loads a preset.

Allocate two `DSY_SDRAM_BSS` float arrays sized to `StereoBbdDelay::kBufferSize`, and keep only the class's small control/filter/LFO state in SRAM. Call `Init()` after `hw.Init()` has configured SDRAM and before audio starts; do not rely on global-constructor initialization of the buffers.

### Exact volume-encoder behavior

Keep the volume encoder's existing page 1 master-volume control. Add a Delay page 2 and move the current Reverb page to page 3:

| Volume encoder page | Unshifted turn | SHIFT + turn | LED/bar |
|---|---|---|---|
| 1: Master | Existing master volume behavior. | Existing behavior remains reserved/unchanged. | Existing green/amber/red master indication. |
| 2: Delay | Source combined position 0..32. Positions 0..16 raise wet mix from 0 to 50% while feedback stays at 35%. Positions above 16 hold mix at 50% and raise feedback from 35% to 120%; the upper range enters bounded self-oscillation. | Delay-time position 0..32 mapped linearly to 40..500 ms. The current delay read glides to the new time; it does not reset. | Yellow encoder LED and yellow white-key value bar, matching the source delay page. |
| 3: Reverb | Existing Reverb mix adjustment, unchanged. | No new Reverb parameter is introduced; preserve the current reserved behavior. | Existing pale-ice encoder LED and bar. |

Use the source's actual integer mapping: `DelayCombinedPercent(position)` scales 0..32 to 0..100, and `DelayTimeMs(position)` is `40 + ScaledInteger(position, 460)`. The UI comment in Pipes mentions 5 ms per click, but its code's 0..32 mapping yields roughly 14 ms per step; port the code behavior, not that stale comment. Keep the combined mix/feedback curve in sync with the source: 0..50 percent sets mix to 0..0.5 at 35% feedback; above 50 percent holds mix at 0.5 and linearly raises feedback to 1.2 at 100 percent.

## Persistence

Add `delay_mix` and `delay_time` positions to [BowlsPreset.h](../code/src/BowlsPreset.h), using the source's 0..32 ranges and defaults (0 and 15). Feedback is derived from the combined mix position and is not stored separately. Include both values in `SetDefaults()`, serialization, parsing/validation, and the `SnapshotParams()` / `ApplyPreset()` path in [chompi_main.cpp](../code/src/chompi_main.cpp). Every changed value must use the existing 1 s autosave debounce.

Increment the Bowls schema from v7 to v8. Since v7 has all existing Bowl fields but no delay values, migrate v7 by preserving its existing values and assigning delay defaults; continue rejecting unsupported/corrupt versions as a whole. Do not persist delay buffers, filters, LFO phases, or echo history. Preset loads must update control targets without clearing the live delay lines or interrupting existing tails.

## Affected files/components

| File/component | Planned changes |
|---|---|
| [chompi_main.cpp](../code/src/chompi_main.cpp) | SDRAM buffers and delay instance; initialize after hardware setup; insert delay before reverb; add delay/reverb volume pages, parameter overlays and LED colors; snapshot/apply delay settings. |
| [BowlsPreset.h](../code/src/BowlsPreset.h) | v8 additive schema with delay mix/time positions and v7 migration to defaults for the new fields. |
| `code/src/StereoBbdDelay.h` | Add an exact port of the sibling header; preserve implementation constants, API behavior, and host-testability. |
| [code/tests/Makefile](../code/tests/Makefile) and a focused delay test | Add a native test executable for the production delay class and preset migration/default serialization cases. |
| [README.md](../README.md) | Document the 3-page volume encoder, combined delay/feedback control, SHIFT delay-time control, and self-oscillation behavior. |
| [leds.h](../code/src/leds.h), [audio_routing.h](../code/src/audio_routing.h) | Reuse existing LED mappings and routing contract; no changes expected unless integration tests expose a need. |

Pipes-only distortion, modulation-mode delay modulation, MIDI CCs, preset banking, and rotary-speaker stages are not part of this port. Bowls has no corresponding UI or effect stages; the requested scope is the exact `StereoBbdDelay` DSP and its direct controls/persistence.

## Step-by-step tasks

1. Copy the sibling `StereoBbdDelay.h` implementation into `code/src/` exactly, keeping its no-Daisy, header-only design. Add native tests before integrating it.
2. Test the production DSP's exact dry bypass, wet-only endpoint, mix/feedback ramps, delay-time slew, stereo behavior, bounded output, and continued buffer/history processing at 0% mix.
3. Add two SDRAM delay buffers and initialize the class after `hw.Init()` and before `StartAudio()`. Confirm the buffers are neither stack-allocated nor cleared on preset application.
4. Insert delay processing between the bowl mix and the current reverb-send filtering/reverb stage. Keep the existing master gain, `Send FX` line-send behavior, AUX isolation, and reverb mix behavior intact.
5. Add volume page 2 for Delay and shift the existing Reverb page to page 3. Implement source-matching control curves and bars/LEDs, then update the top-level control-map comments.
6. Add the delay fields and v7-to-v8 migration in `BowlsPreset.h`; wire them through `SnapshotParams()`, `ApplyPreset()`, and the existing autosave flow.
7. Update the README, run delay/preset and existing host suites, build firmware, inspect memory/CPU headroom, and complete the hardware checks below.

## Risks and open questions

- The delay adds 256 KiB of SDRAM plus a 256-float sine table and scalar state in SRAM. Bowls already uses SDRAM for reverb and has constrained SRAM/SRAM_EXEC regions; verify actual section usage and stack headroom in the firmware build.
- The source hot path is designed for real-time use (no per-sample trig or allocation), but it adds two fractional reads, filters, and soft-clipping to every sample. Measure worst-block CPU load on hardware with the delay active and high feedback.
- Feedback above 100% is intentionally self-oscillating, although bounded. Confirm the transition and maximum-level behavior are musically useful with singing bowls and do not overload the downstream reverb/output path.
- Bowls' strike-darkening filter sits on the reverb send. Placing the delay before it means repeats feed the same existing filtered reverb input; verify that this is the desired interaction and does not make repeats unexpectedly dull.
- Delay is upstream of the reverb, so high feedback can continuously excite the reverb. Test long feedback and reverb tails together, including changes to either mix while audio is ringing.
- The added fields require schema migration. Confirm the v7 migration keeps every old value unchanged and that v1-v6 rejection behavior remains as designed.
- The source's DSP default is 250 ms while its engine preset default at position 15 maps to 256 ms. Keep the saved/UI default consistent with source engine behavior, and verify boot initialization applies that target without an audible startup glide.
- `Send FX` routes the post-reverb Bowl bus to the line output and AUX return only to headphones. Keep the delay in the Bowl bus before reverb; do not route AUX input through the delay or back to the line output.

## Testing and validation

### Host tests

- Add `delay_test.cpp` and a Makefile target. Assert bit-exact output at settled 0% mix; exact 100% wet endpoint; approximately 10 ms mix and feedback ramps; approximately 80 ms time slew; 40/500 ms bounds; left/right independence and spread; bounded soft-clipped writes during >100% feedback; and no history reset while mix is zero or controls change.
- Extend `preset_test.cpp` for v8 delay fields, default values, valid endpoints, invalid values, and v7 migration that preserves prior settings while defaulting only the new delay fields.
- Run `make -C code/tests modes-test test strike-test routing-test` plus the new delay and preset checks.

### Firmware and hardware checks

- Build with the repository firmware toolchain and report SRAM, SRAM_EXEC, and SDRAM usage. Confirm SDRAM buffers are initialized after SDRAM setup and no SRAM/stack region overflows.
- Compare the source and port at identical sample rate/settings with deterministic input: dry path, impulse timing, stereo spread, feedback decay, and bounded self-oscillation should agree within floating-point/compiler tolerance.
- Verify page cycling: master -> delay -> reverb -> master; page-2 yellow indication; main turn changes combined mix/feedback; SHIFT+turn changes time; page-3 reverb control and its existing pale-ice indication remain correct.
- On hardware, audition 0%, partial mix, 50% mix, feedback through and above 100%, minimum/maximum time, rapid retargets, and reverb interaction. Confirm no clicks, runaway output, routing/AUX feedback, or voice/DSP regressions; monitor peak CPU load.