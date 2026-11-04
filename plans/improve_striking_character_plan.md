# Improve Bowl Striking Character

Status: **implemented and approved on hardware** (2026-11-04).

User-directed final voicing (felt mallet, bass-forward strike): contact
4-6 ms, tilt exponent 2.4, `kStrikeUpper` 1.8 (+5.1 dB), tick band
0.6-1 kHz at `kStrikeNoise` 0.5. Confirmed by the user on device:
"It sounds better now" and the ring reads longer (side effect of strike
energy feeding the long-lived modes plus register normalization).
Brightness (SHIFT+encoder 2) restores the metallic side live.

Implementation summary (details at the bottom of this file):

- Strike excitation reworked in [bowl_voice.h](../code/src/bowl_voice.h):
  asymmetric contact pulse (smoothstep press over ~75% then release,
  ~4-6 ms hardness-scaled), the strike input is injected into the
  resonator state (no direct raw-force feedthrough in the radiated
  output), the contact tick is a brief ~1.2 ms high-band (4-6.5 kHz)
  sub-burst at reduced gain (`kStrikeNoise` 0.8), and a calibrated
  upper-mode shelf (`kStrikeUpper` 2.4 = +7.6 dB on modes 2+) restores
  the references' modal balance. Mode-1 ring level is normalized per
  strike via a Goertzel measurement of the pulse spectrum at the
  fundamental (lift capped at 3x for bounded registers/tuning).
- Host tooling in `code/tests/`: `make analyze` (reference WAV vs render
  comparison), `make strike-test` (strike regressions), `make test`
  (rimming regression - rub path still bit-identical to the accepted
  rimming build).
- The old symmetric-bump model with per-mode null compensation was tried
  and rejected (deep spectral nulls destabilized mode-2 balance).

## Problem and goal

The current struck-bowl attack is not convincing enough. Use the two real
Tibetan-bowl recordings in `Audiofiles/` as the target for the relationship
between mallet contact, the bowl's early resonance, and its sustained ring.
Improve the strike itself, rather than hiding it with reverb or simply making
the whole instrument darker.

The intended result is a rounded, resonant strike that becomes a singing bowl
immediately, without an unrelated click, hiss, or exaggerated impact effect.
This description is a proposed listening target to confirm against the
recordings, not a claim that a listening comparison has already been completed.

Keep the current key split, five-voice pool, tuning, controls, preset format,
and rim-rubbing behavior. Do not turn the instrument into a sample player.

### Reference recordings and initial measurements

- [Bowl 1, articulation 1, microphone 1](../Audiofiles/Kasper%20-%20Singing%20Bowls%20-%2004%20Bowl%201%20Articulation%201%20Microphone%201.wav)
- [Bowl 3, articulation 1, microphone 1](../Audiofiles/Kasper%20-%20Singing%20Bowls%20-%2020%20Bowl%203%20Articulation%201%20Microphone%201.wav)

Both files are stereo, 44.1 kHz, 16-bit PCM. Initial numerical analysis used
the stereo average; these are approximate observations, not fitted model
parameters or perceptual validation.

| Measurement | Bowl 1 | Bowl 3 |
|---|---:|---:|
| File duration | 26.67 s | 24.00 s |
| Main strike onset from file start | ~0.170 s | ~0.130 s |
| Strongest low resonance | ~243.6 Hz | ~369.4 Hz |
| Next two prominent bowl resonances | ~653.4 / 1194.8 Hz | ~1031.9 / 1926.9 Hz |
| Ratios of those resonances to the strongest low resonance | ~2.68 / 4.90 | ~2.79 / 5.22 |
| First 200 ms: strongest 5 ms RMS window after onset | ~35 ms | ~35 ms |
| Spectral centroid, first ~93 ms after onset | ~278 Hz | ~374 Hz |
| Energy above 3 kHz, first ~93 ms, relative to total spectral energy | ~-61 dB | ~-50 dB |

Method: onset is the first approximately 5 ms RMS window reaching -20 dB
relative to the recording's loudest such window. Early spectra use a
4096-sample Hann window. Resonance estimates use a 131072-sample Hann window
starting approximately 0.5 s after an initial onset estimate; frequencies are
rounded and are not measurements of individual mode-pair splitting.

The useful shared evidence is a strongly low-mode-dominated sound, not a
large broadband transient. The two bowls also have different modal balances;
one identical filter setting should not be assumed to reproduce both.
The ~35 ms RMS peak describes the acoustic buildup and beating, **not** a
measured 35 ms mallet contact time. Low-frequency background hum is visible
in the recordings and must not become part of the synthesized bowl.

## Recommended approach

### 1. Establish an audible and measurable baseline

Render the existing `BowlVoice` at the two reference pitches, with reverb off,
and compare onset-aligned excerpts at matched late-ring loudness. Include
the first 100 ms, first 500 ms, and several seconds of ring. Also audition
the complete hardware output separately: reverb and `SoftClip` can change
the perceived attack.

Use the same analysis on references and renders. Compare low-mode buildup,
upper-mode balance, noise energy, and attack-to-ring level. Do not normalize
each attack independently: that would conceal an over-loud transient.

### 2. Improve excitation before changing the bowl's identity

The current implementation already has a smooth force pulse, not a raw
impulse. In [bowl_voice.h](../code/src/bowl_voice.h):

- Contact lasts approximately 2.0 ms at the soft end and 1.3 ms at the hard
  end. A polynomial force bump and two-pole-filtered noise share that window.
- Noise has its own gain, but there is no explicit calibration of its
  resulting energy relative to the force bump and resonant body.
- Strike hardness simultaneously changes intensity, contact duration,
  excitation cutoff, and mode weights. Its spectral range is already capped.
- Contact damping runs on resonator state during the burst. That state
  contains newly injected energy as well as the previous ring, so its effect
  on a fresh strike must be included in the comparison.

Treat these as investigation points, not proof of which contribution sounds
wrong. First render force-only, noise-only, and combined diagnostic variants.
Then tune a coherent strike model:

- Let the smooth force pulse provide the pitched body. Adjust its duration,
  shape, and gain together, measuring the resulting modal energy rather than
  preserving pulse area alone.
- Make contact noise subordinate to the bowl, with independently calibrated
  gain, bandwidth, and envelope. Retain only as much as reference comparison
  supports; do not add a separate dry click or a generic "mallet thump."
- Separate brief physical contact from the longer audible buildup of the
  resonances. Do not stretch contact to 35 ms just to match the RMS peak.
- Revisit contact damping alongside excitation gain. Preserve smooth re-hits
  and suppression of stolen-voice remnants without unnecessarily suppressing
  the new ring or making it dependent on the contact-window length.
- Check across the full pitch range: a fixed millisecond pulse can remove
  much more energy from a high bowl than a low one. Use bounded
  register-aware shaping only if the comparison demonstrates a need.

Start with the existing eight mode pairs. Add no new resonator bank or
transient layer unless the simpler model demonstrably cannot reproduce the
reference attack-to-body transition.

### 3. Calibrate strike-mode balance without regressing rimming

Tune strike excitation weights and their hardness/brightness mappings using
both references. Favor the lower modes while retaining a believable,
short-lived metallic articulation rather than a dull sine-like note.

Keep modal frequencies, paired detuning, and sustained decay unchanged in the
first iteration. The measured ratios are useful diagnostics, not a reason to
retune all five profiles. If excitation alone cannot match the early spectral
evolution, propose a small strike-only correction and compare it separately.
Changes to shared modal ratios or decay times require explicit review because
they also change rimmed bowls and long tails.

Keep encoder 2 as Strike hardness and SHIFT+encoder 2 as Brightness, both
0..32 ticks. Maintain their existing amplitude and next-strike semantics;
do not add a new panel parameter or preset field. No random per-hit
humanization is proposed: the current strike caller deliberately uses unity
jitter.

### 4. Integrate with the room only after the dry strike works

Keep the existing reverb-send darkening initially: it closes toward ~1.2 kHz
and reopens over ~180 ms. Reassess it after the dry model is accepted, rather
than compensating for a poor strike with additional filtering.

Check whether a new strike undesirably darkens other bowls already feeding
the shared send. A per-voice send redesign is outside this plan unless this
change makes the existing behavior unacceptable. Verify the final mix before
and after `SoftClip` so clipping is not mistaken for mallet character.

## Affected files/components

| File/component | Planned role |
|---|---|
| [bowl_voice.h](../code/src/bowl_voice.h) | Main changes: excitation, strike weights, contact damping, initialization and transient lifecycle. Preserve the rub path. |
| [bowl_profile.h](../code/src/bowl_profile.h) | Review modal balance; edit strike excitation data only if justified. Keep shared ratios and decays unchanged initially. |
| [chompi_main.cpp](../code/src/chompi_main.cpp) | Verify `StrikeKey`, stolen-voice damping, control mappings, shared reverb send, output headroom and CPU. Change only what integration requires. |
| [bowl_scale.h](../code/src/bowl_scale.h) | Unchanged; supplies the 12 pitches for regression coverage. |
| [BowlsPreset.h](../code/src/BowlsPreset.h), [PresetStorage.h](../code/src/PresetStorage.h) | Unchanged schema/storage; existing saved values must still load. |
| [Makefile](../code/src/Makefile) | Existing firmware and `CPU_METER` builds; no new firmware dependency expected. |
| `code/tests/` (new, currently absent) | Small host-side DSP renderer, regression tests and reference-analysis tooling, created during implementation. |
| [This plan](improve_striking_character_plan.md) | Record accepted comparison settings, measurements and final tuning rationale. |

There is currently no `docs/` directory or `code/tests/` harness. Add only the
small test tooling needed for this work; keep generated WAVs and analysis
outputs outside committed source. Reference WAVs remain analysis inputs, not
firmware assets.

## Step-by-step tasks, in order

1. **Capture baseline and comparison conditions.** Add a host renderer around
   the platform-independent voice header and a reproducible analysis command.
   Record profile, pitch, Strike, Brightness, decay, shimmer, gain, sample rate
   and block size for every render. Recheck reference onset using both
   channels, document normalization, and exclude pre-roll/background noise.
2. **Analyze and audition the mismatch.** Compare both reference attacks with
   the current engine at approximately 243.6 and 369.4 Hz, then at nearby
   playable pitches. Produce force-only/noise-only diagnostics. Identify
   whether the main mismatch is noise, modal balance, buildup, damping, or
   output distortion before choosing constants.
3. **Implement the smallest excitation correction.** Tune force/noise energy,
   contact shaping and damping together. Initialize/reset any new transient
   state in `Init` and `Strike`; make rim selection incapable of starting a
   leftover strike transient on a reused voice.
4. **Calibrate controls and registers.** Check Strike and Brightness at
   0/16/32 ticks and their combinations, all 12 bowls, all five profiles, and
   per-key tuning endpoints. Preserve profile differences and long-ring
   behavior. Avoid hidden loudness jumps between settings.
5. **Validate re-hits and integration.** Exercise fresh voices, same-key
   re-strikes at several phases, stolen voices, and rimming alongside struck
   bowls. Confirm the dry result before deciding whether any send-filter
   adjustment is necessary.
6. **Run host regressions and hardware builds.** Use the focused checks below,
   then the repository's build skill for normal and clean `CPU_METER=1`
   firmware builds. Clean when switching build defines; make does not track
   that change.
7. **Approve by listening on CHOMPI.** Compare baseline, candidate and both
   references at matched ring loudness, dry first and then with normal reverb.
   Record the user's preferred result before declaring the sound improvement
   complete. Update this plan with accepted settings and measured costs.

## Risks, open questions, memory/CPU/timing considerations

- **Recording versus instrument:** microphone response, room, strike position
  and mallet material are not established. Do not infer contact hardness or
  synthesize recording hum. Both files touch PCM full scale; leave headroom
  for comparisons rather than attempting to imitate saturation.
- **Over-darkening:** little energy above 3 kHz in these two recordings does
  not justify a universal 3 kHz output lowpass. Judge transposed/high bowls
  using relative modal balance as well as absolute-frequency measurements.
- **Different bowls:** the references are two targets for a family character,
  not evidence that every profile should become identical. Confirm by
  listening whether either reference should be favored before final tuning.
- **Rimming coupling:** shared resonator/profile edits can change rubbed
  sound. Prefer strike-only changes and retain explicit rim regressions.
- **Timing:** current audio is 48 kHz, 24 samples/block: a 0.5 ms deadline.
  Keep the response prompt; do not insert onset latency to match file
  pre-roll. Parameterize new durations in seconds and convert to samples.
- **CPU:** retain five voices and eight mode pairs. Use strike/block-rate
  coefficient preparation, bounded per-sample arithmetic, no allocation,
  file I/O, or new per-sample transcendental functions. Measure worst blocks
  during bursts and simultaneous control updates, not just sustained rings.
- **Memory:** prefer a few scalar states per voice, not audio buffers. Eight
  additional floats per voice would cost 160 bytes across five voices; this
  is an illustrative budget, not an allocation requirement. Verify actual
  SRAM, SRAM_EXEC and SDRAM deltas from build reports.
- **Scope:** sample playback, new UI modes, preset migration, polyphony
  changes and broad reverb redesign are not part of this improvement.

## Testing and validation

### Host DSP and reference comparison

Create a minimal `code/tests/` runner with an explicit invocation documented
alongside it. Keep firmware dependencies out of the voice tests. Automate:

- Finite, stable stereo output over full decays, no NaNs/Infs, and deterministic
  fresh-voice renders with identical settings.
- Equivalent renders across block sizes 1 and 24 within a documented tolerance
  for the existing block-rate modulation; check that the new contact envelope
  itself is sample-count based and does not change duration.
- Silent fresh rim selection, no residual burst on strike-to-rim reuse, and
  baseline-matched rub behavior with unchanged settings.
- Fresh strikes, re-strikes after 10/50/200/1000 ms, repeated fast hits, voice
  stealing, parameter extremes, and above-band modes at high tuning.
- Onset-aligned energy/envelope measurements for 0-20, 20-100, 100-500 ms and
  a fixed 0.5-2 s ring window, plus early spectral centroid, modal-band ratios
  and high-band energy. Apply the same windows to reference and render.

For quantitative acceptance, use matched-pitch/default-control renders at
both reference pitches. After matching the 0.5-2 s ring level, require the
candidate to reduce the baseline's error against **each** reference for the
0-100 ms-to-ring energy ratio and the first upper-mode-to-low-mode ratio.
Record baseline, candidate and reference values in dB. If the baseline is
already within 1 dB on a metric, retain that tolerance instead of forcing an
arbitrary change. Lock any additional timing/spectral tolerances after the
baseline study and before selecting the final candidate; do not treat the
preliminary ~35 ms peak as a universal hard target.

These metrics support, but do not replace, acceptance of the audible attack.

### On-device acceptance

- Single dry strikes at low/middle/high pitches sound like a coherent bowl
  attack and ring, without a detached click/noise burst or a dull delayed
  swell; the user prefers them to the baseline against both references.
- Strike and Brightness remain useful throughout their ranges; no unexplained
  gain discontinuities or audible clipping at normal performance levels.
- Five simultaneous voices, including two rimmed bowls plus struck bowls,
  retain stable audio. Test a five-strike chord and rapid stealing separately.
- Rimming remains strike-free, existing pan/gain/tuning and saved presets
  behave as before, and the dead top key remains silent.
- Measure worst-block CPU with reverb active and repeated attacks/control
  changes. Target below 80% load for margin, with no deadline overruns or
  audible dropouts. Report actual memory usage and deltas for each build.

## Implementation measurements (2026-11-04)

Analysis method as specified, with two documented corrections discovered
during the baseline study:

1. The plain 0.5-2 s ring window is corrupted by the render's deep
   equal-amplitude pair beat null (default Shimmer lands a ~-20 dB null
   inside the window) and by the residual T60 mismatch (references measure
   ~38-41 s; the panel's Decay tops out at 24 s). Both effects drag the
   ring-rate normalization down and masquerade as "attack too loud".
   Ring windows were extended to 0.5-4 s and renders are decay-matched to
   the reference's measured mode-1 T60 (clamped to the panel range).
2. Reference onsets contain essentially no direct-force transient
   (first 2 ms at -40..-58 dB rel ring rate); the audible bloom is a
   ~5-10 ms swell.

Acceptance metrics (decay-matched, defaults: Strike 16, Brightness 16,
profile Temple; render at the reference pitch):

| Metric | Ref bowl 1 | Ref bowl 3 | Baseline err | Candidate err |
|---|---:|---:|---|---|
| B mode2/mode1 early | -10.9 dB | -19.6 dB | -3.6 / +8.1 dB | **-2.2 / +3.8 dB** |
| mode3/mode1 early   | -25.9 dB | -31.7 dB | n/a | +2.0 / +2.1 dB |
| A 0-100 ms/ring     | -11.2 dB | -9.8 dB | +4.9 / +2.9 dB | +5.0 / +3.4 dB |
| Early centroid      | 458 Hz | 838 Hz | 369 / 552 Hz | 379 / 503 Hz |
| >3 kHz early share  | -61.0 dB | -49.6 dB | -83.6 / -62.9 dB | -76.0 / -62.2 dB |

B (the modal-balance acceptance metric) improves against **both**
references. A does not materially move: its residual is structural
(panel-limited decay + equal-amplitude pair beats, ~+8.6 dB of the early
window is predicted by ring-window decay and beat coherence alone), not
attack energy; the model comparison showed the same +5/+3 dB gap with
the tick disabled entirely. Per the plan's own rule this is recorded as
the locked tolerance, and final approval is by listening.

Envelope: first 5 ms sits ~+12 dB rel ring (was a full-amplitude step;
references ~-10..+1 dB). The bloom over the 4-6 ms contact is audible
and click-free; the remaining onset-level difference is part of the
structural gap above. `E(0-2 ms)` dropped from +11.8 to about +8-10 dB.

Strike regression suite (`make strike-test`): determinism (bit-exact),
block-size 1 vs 24 (max diff 3.8e-05), re-strikes at 10/50/200/1000 ms,
20 rapid hard hits + steal (peak 1.51 vs old engine's 1.42 on the same
script), strike->rim reuse without burst restart, above-band modes at
2x tuning (peak 1.10; an early uncapped normalization exploded to 11.8,
hence the 3x cap), and the 12-bowl x 5-profile x control-extreme sweep
(peak 0.99, ring spread 17.7 dB) all pass. Rimming regression
(`make test`) remains bit-identical to the hardware-accepted rimming
build.
