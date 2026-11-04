# Direction-Controlled Rimming Texture

Status: **planned, not implemented**. Firmware changes require approval.

## Problem and goal

The current rimmed bowl has an audible friction texture. Provide a cleaner,
sustained singing sound as an alternative, selected directly by transport-wheel
direction:

| Wheel movement | Sound |
|---|---|
| Clockwise | Existing rim-rubbing sound, including its friction texture |
| Counter-clockwise | Clean sustained bowl tone, with no friction-noise excitation |
| Stopped | Excitation fades as today; the bowl rings down naturally |

Both directions retain speed-dependent intensity, silent rim-key selection,
the held-two-bowls/last-selected-bowl behavior, and existing wheel-level LEDs.
Preserve the five-voice pool, strike behavior, bowl tuning, panel parameters,
reverb, and preset format.

The listening goal is a clear, resonant bowl rather than a hissy or scratchy
texture. The user's reference to professional recordings is a listening target,
not evidence that those recordings have been analyzed or can be reproduced
exactly by this change.

## Recommended approach

### Separate excitation strength from excitation character

In [chompi_main.cpp](../code/src/chompi_main.cpp), `wheel_inc` currently loses
its sign when calculating the pump amount. Keep its absolute magnitude for the
existing level calculation, and use its sign separately to select the sound.
Verify which sign means physical clockwise on the device; do not assume it.
Keep that mapping local to the transport-wheel handling.

Store the most recently requested character per rim bowl, alongside
`rim_level_`. A nonzero wheel increment updates only the active rim set.
With two held bowls, both receive the same direction choice. Other bowls keep
their own character while fading; changing selection alone neither starts a
tone nor changes another bowl's tail.

Proposed transient-state behavior:

- Initialize to the existing textured character, with zero excitation.
- Keep the last direction while the wheel is stationary, including the
  excitation fade. Do not interpret zero movement as a direction change.
- Direction is a performance gesture, not a saved setting: no preset field,
  autosave event, extra control, or new LED mode.
- Pass the bowl's level and character together to its voice. Restore both
  when wheel activity reacquires a stolen rim voice.

### Add a genuinely clean tonal excitation

In [bowl_voice.h](../code/src/bowl_voice.h), the filtered random signal
`rub_exc` drives the resonators themselves. There is no separate dry friction
layer that can simply be muted. Setting it to zero would stop sustaining the
bowl, contrary to the requested behavior.

Keep the clockwise noise excitation, filtering, weights, and level mapping
unchanged. For counter-clockwise, introduce phase-continuous sinusoidal
excitation at the bowl's modal frequencies, driving the existing resonators
instead of supplying friction noise. Use the paired frequencies, not just one
unmodulated fundamental, so the clean sound retains bowl character and beating.

Start with the existing fundamental-dominant rub weights and live Brightness
mapping. Derive frequencies from the same profile, tuning, paired detune, and
wander values already used by the resonators. Do not alter shared profile data
or remove the existing stereo motion.

Normalize the tonal drive for resonator gain/decay: a sustained sine at resonance
cannot safely reuse the noise input amplitude. Calibrate bounded levels across
registers and decay settings, retaining headroom before the existing output
soft clip. Use inexpensive oscillator recurrences with coefficient updates
outside the per-sample loop, including drift control and above-band muting.
Do not introduce a second resonator bank or per-sample trigonometry.

### Make reversals and voice reuse smooth

Smooth the transition between excitation characters separately from the
existing rub-level slew. Start with an approximately 100 ms transition and
adjust from listening and discontinuity measurements. Reversal must not
retrigger a strike, reset ringing resonators, or briefly double the drive.
Maintain oscillator phase through a reversal; update tuning without phase jumps.

After a transition to clean mode, the friction-excitation contribution must
reach exactly zero, rather than remain as a small noise floor. Existing
textured resonance and reverb may still decay naturally. This is intentionally
not an instantaneous erasure of previously sounding texture.

Initialize all new state in `Init`. Integrate it with selection, strike reuse,
voice stealing, idle detection, and reallocation so a clean voice can start
from silence and cannot leak a previous bowl's tonal drive into another key.
Do not clear ringing state merely to change wheel direction.

## Affected files/components

| File/component | Planned role |
|---|---|
| [chompi_main.cpp](../code/src/chompi_main.cpp) | Wheel sign mapping, per-bowl character state, `ApplyRub`, and voice reacquisition; update control comments |
| [bowl_voice.h](../code/src/bowl_voice.h) | Clean tonal excitation, bounded normalization, smooth character transitions, oscillator lifecycle, and comments |
| [chompi_hardware.h](../code/src/chompi_hardware.h) | Read/verify encoder direction convention; no decoder change expected |
| [bowl_profile.h](../code/src/bowl_profile.h), [bowl_scale.h](../code/src/bowl_scale.h) | Reuse existing modal data and pitches unchanged |
| [BowlsPreset.h](../code/src/BowlsPreset.h), [PresetStorage.h](../code/src/PresetStorage.h) | Unchanged; direction is transient performance state |
| [Makefile](../code/src/Makefile) | Existing firmware and CPU-meter builds; no new firmware dependency expected |
| `code/tests/` (new, currently absent) | Minimal host DSP/control regression coverage and reproducible renders |

There is currently no `docs/` directory or `code/tests/` harness. During
implementation, reuse any harness added by related work rather than duplicating
it. Update the transport-wheel instructions in the repository README and source
comments when the feature is implemented.

## Step-by-step tasks, in order

1. Capture current dry clockwise-equivalent renders and the existing wheel
   level/selection behavior as regression baselines. Record sample rate,
   block size, profile, pitch, and parameter settings.
2. Verify physical wheel direction. Define the local direction-to-character
   mapping and per-bowl transient state, keeping pump magnitude unchanged.
3. Prototype and measure the clean modal excitation in the host renderer.
   Resolve gain normalization and numerical stability before hardware
   integration. Compare at matched sustained loudness, not peak normalization.
4. Add clean excitation and character smoothing to `BowlVoice`; integrate
   coefficient changes, idle handling, and all initialization/reuse paths.
5. Wire level and character through `ApplyRub` and rim-voice reacquisition.
   Preserve selection, two-bowl behavior, strike handling, and wheel LEDs.
6. Run host regressions and complete firmware/CPU-meter builds using the build
   skill. Check memory reports and worst-case callback load.
7. Audition clockwise, counter-clockwise, and reversals on the device, dry
   first and then with reverb. Adjust only clean-drive calibration and the
   transition as needed; document accepted settings and update instructions.

## Risks, open questions, memory/CPU/timing considerations

- **Sound quality:** replacing stochastic drive with coherent drive changes
  modal balance and buildup. A clean fundamental alone could sound like a
  test oscillator; retain paired bowl modes and evaluate all profiles.
- **Resonant gain:** long-decay resonators can amplify coherent input heavily.
  Prolonged-drive and parameter-extreme tests are required, not just short
  listening checks. Do not rely on output clipping to bound the new drive.
- **Reversal tails:** already-excited texture and room tails will not disappear
  immediately. The recommendation is to preserve them rather than hard-mute
  the bowl or reset the shared reverb.
- **Timing:** controls run once per 24-sample block at 48 kHz. Keep level
  pumping/fading unchanged and smooth new character changes per sample.
- **CPU:** budget up to 16 small oscillator states per clean rim voice, with
  up to two actively rimmed bowls and fading voices after selection changes.
  Measure the actual worst case across the five-voice pool, including rapid
  reversals and strikes; target peak callback utilization below 80%.
- **Memory:** expect a small fixed SRAM increase for oscillator/coefficient
  state and 12 bowl character choices, no sample buffers or SDRAM allocation.
  Record actual SRAM, SRAM_EXEC, and SDRAM changes from the build.
- **Open calibration items:** physical encoder sign, final transition time,
  and clean-mode gain/modal balance must be verified during implementation.
  No additional product decision is required to draft this plan.

## Testing and validation

Use a small host renderer around the platform-independent `BowlVoice` header
and targeted control tests. Keep generated audio outside committed source.

- **Clockwise regression:** with fixed initial state and parameters, compare
  output against the baseline. Keep textured-only output sample-identical
  where feasible; separate any intentional transition-only differences.
  New oscillator logic must not consume the shared noise RNG.
- **Clean from silence:** select without striking, then drive counter-clockwise.
  The bowl must bloom, sustain, and decay after stopping. Assert zero
  friction-path contribution once the character transition completes.
- **Spectral target:** for steady dry clean renders, measure energy outside
  windows around all active paired modal frequencies. Use windows wide enough
  for existing wander and stereo modulation; document FFT/window settings.
  Initial acceptance target: off-modal energy at least 30 dB below modal
  energy, with no audible hiss in level-matched listening. Test after startup
  and previous textured tails have settled.
- **Coverage:** all 12 pitches and five profiles, minimum/default/maximum
  Brightness, Decay, Shimmer, tuning, and bowl gain; include sustained 60-second
  drives at representative extremes. Require finite, bounded output and no
  growing oscillator error or clipping introduced by clean excitation.
- **Transitions:** reverse repeatedly at slow and fast rates, stop, restart,
  and change live parameters. Check for clicks, pumping spikes, phase resets,
  stale drive, and unexpected changes to level or pitch.
- **Selection and stealing:** latched bowl, two held bowls, a newly selected
  silent bowl, a third selection, and strike-induced stealing/reacquisition.
  Direction changes must affect only the current rub set.
- **Preservation:** strike-zone sound, per-key settings, wheel LEDs, pan,
  reverb, saving/rebooting, and natural ring-down remain intact.
- **Hardware acceptance:** confirm clockwise is unchanged and counter-clockwise
  is audibly clean on headphones and line out. Exercise the full five-voice
  pool with reverb and CPU metering; reject audible dropouts or memory overflow.

This planning change adds no firmware implementation or test harness.
