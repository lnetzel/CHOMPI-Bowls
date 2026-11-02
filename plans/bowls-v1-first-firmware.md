# CHOMPI-BOWLS v1 — First Playable Firmware (plan)

Status: awaiting approval. Source spec: [plans/CHOMPI-BOWLS-plan.md](plans/CHOMPI-BOWLS-plan.md).

## Problem and goal

Turn the bare audio-passthrough skeleton in [code/src/chompi_main.cpp](code/src/chompi_main.cpp) into the
first playable CHOMPI-BOWLS firmware: a physical-modelling singing-bowl instrument with four-voice modal
synthesis (paired resonators), a measured bowl-scale keyboard on the 15 white keys, and the CHOMPI-PhaseOff
white-key overlay-bar UI for every encoder turn. Rim rubbing on the transport wheel and a single autosaved
JSON state file on the SD card are in scope; reverb and the Bowl Collection / Free Tuning modes are not.

## Decisions (from the clarifying questions — all confirmed "A")

| # | Question | Decision |
| - | -------- | -------- |
| 1 | Voice count | **4 voices from the start**, quietest-voice stealing (acceptance criteria). The modal engine is a per-voice class anyway. |
| 2 | Keyboard / tuning | **One Bowl Scale lookup table on the 15 white keys**, anchored on the measured fundamentals 99 / 219.6 / 310.2 / 465 / 513 Hz, design-choice intermediates, spanning ~2 octaves. Each key = one bowl, no equal temperament. Black keys dark/unused in v1. |
| 3 | Control map | Spec table confirmed: Enc 1 = bowl character (profile select), Enc 2 = strike, SHIFT+Enc 2 = brightness, Enc 3 = decay, Enc 4 = shimmer, Volume = master. PLAY / LOOP / mode toggle unused in v1 (reserved). |
| 4 | Rim rubbing | **In v1**: transport-wheel turn rate drives a sustained excitation of the held / most-recent voice (slow = soft, fast = loud). |
| 5 | Persistence | **One JSON state file** on SD: all parameters autosaved on change, loaded at boot (FatFS + coreJSON, PhaseOff PresetStorage pattern). |
| 6 | Humanization | **Small random per-strike variation** of excitation strength and detune (natural variation between hits). |

Overlay behavior (per request): every encoder turn shows a temporary (~500 ms) bar on the 15 white keys,
exactly like the PhaseOff volume encoder — parameters stored as 0..32 ticks in atomics, bars drawn through
the white-key LED map, held keys light white when no overlay is up.

## Recommended approach

### A. Port the PhaseOff infrastructure (hardware-identical)

From C:\Dev\CHOMPI-PhaseOff\code\src\ into code/src/:

- chompi_hardware.h — ChompiHardware: six encoders (physical order, quadrature decode), 40-input button
  chain with debounce, KeyRising/KeyFalling/KeyHeld in pitch order, ShiftHeld, ModeSwitchUp, PLAY/LOOP,
  wheel click, plus the dual-codec SAI1+SAI2 audio init already proven on this hardware.
- leds.h — WS2812 driver for the 25 SMT key LEDs and 10 PTH panel LEDs, including kWhiteKeyLeds[15]
  (white keys left to right) and kKeyLeds[25] (pitch order); Set*/Flush lifecycle.

Both files are hardware-level and firmware-agnostic; port with comment tweaks only. Naming follows
[hardware_ui_map.md](hardware_ui_map.md) (physical encoder 1 = SW4 / ENC_4, etc.).

### B. Port the PhaseOff overlay/UI pattern

From C:\Dev\CHOMPI-PhaseOff\code\src\chompi_main.cpp:

- Params struct: std::atomic<int> ticks per parameter, kTickCount = 32 (0..32, 33 positions).
- BumpTicks() (turn → clamp → overlay) and ShowOverlay() (press → confirm), kOverlayMs = 500.
- RenderLeds() in the main loop: clear all SMT LEDs every frame, draw the active view by priority;
  overlay bars win; with no overlay, held keys light white.
- DrawVolumeBar(): lit = (ticks * 15 + 16) / 32, green → amber (key ≥ 7) → red (key ≥ 11), and the
  volume knob's PTH LED (LED10 / PTH 9) follows the same gradient. Volume bar shows briefly at boot.
- SectionStep(): two detent ticks per step for the discrete profile select (stray-brush protection).

### C. New DSP: modal bowl engine

New header-only modules in code/src/ (data kept separate from DSP, per spec §7):

- bowl_profile.h — BowlProfile: per-mode frequency ratio, pair-detune offset (Hz), T60 decay (s),
  excitation weight, stereo family. Five built-in profiles; profile 0 uses the measured 180 mm bowl
  ratios from the spec (1.0, 2.8, 5.2, 8.1, 11.6, 15.6, 19.9 + one extrapolated ≈ 24) with spec decay
  starting points (fundamental ~12 s down to ~2–5 s for high modes). The other four profiles are
  variations (different ratios/detune/damping) — starting templates, tuned by ear later.
- bowl_voice.h — BowlVoice: 8 mode pairs = 16 second-order resonators per voice. Coefficients
  (biquad band-pass/resonator form) are computed at strike time or on parameter change only — never
  trig in the audio callback. Per-mode independent exponential decay via the feedback coefficient
  (no global amplitude envelope). Strike excitation = impulse + short spectrally shaped noise burst,
  distributed across modes with per-mode weights; Strike ticks set amplitude + spectral tilt
  (soft → low modes, hard → all modes); Brightness scales the high-mode weights; Shimmer scales the
  per-pair detune (0..2× profile offsets, per-mode independent — not a global chorus); Decay scales
  all T60s (≈0.25×..2×, log-ish). Re-strike adds energy on top of the still-ringing state — the
  resonator state is never reset.
- bowl_scale.h — BowlKey table: 15 white keys → {fundamental_hz, reserved profile slot}. Anchored on
  99.0 / 219.6 / 310.2 / 465.0 / 513.0 Hz with design-choice intermediates (e.g. 99, 146, 219.6, 310.2,
  390, 465, 513 for the lower octave, octave-derived entries above); unequal ratios by design. Changing
  key scales the whole modal structure proportionally (character preserved).

Voice manager (in chompi_main.cpp): 4 BowlVoice instances; on key press allocate a free voice, else steal
the **quietest** (tracked from resonator energy), never the oldest by default. The keyboard has no velocity
(switch chain), so strike intensity = Strike ticks + small random humanization per hit.

Stereo (fixed in v1, no width knob): mode family A slightly left, family B slightly right, higher modes
wider, fundamental centered — spatial movement from the paired modes themselves, no chorus.

Rim rubbing: wheel detent rate → sustained low-level noise excitation fed to the voices of held keys
(most-recent voice when idle); speed → intensity. Wheel LEDs (LED6/LED7 / PTH 5/6) indicate rubbing.

### D. Control map (v1)

| Control | Parameter | White-key overlay | Panel LED |
| ------- | --------- | ----------------- | --------- |
| Physical encoder 1 (SW4 / ENC_4) | Bowl character: profile 1–5 | Five 3-key sections lit in profile color (DrawStereoModeBar pattern); two detent ticks per step | LED2 / PTH 1: orange |
| Physical encoder 2 (SW1 / ENC_1) | Strike hardness 0–32 | Amber left→right bar | LED3 / PTH 2: amber |
| SHIFT + encoder 2 | Brightness 0–32 | Yellow-white left→right bar | — |
| Physical encoder 3 (SW2 / ENC_2) | Decay 0–32 | Blue left→right bar | LED4 / PTH 3: blue |
| Physical encoder 4 (SW3 / ENC_3) | Shimmer 0–32 | Center-out purple bar (DrawWidthBar pattern) | LED5 / PTH 4: purple |
| Transport wheel (SW5 / ENC_5) | Rim rubbing (momentary) | Center-out white bar while rubbing | LED6/LED7 / PTH 5/6 |
| Volume encoder (SW6 / ENC_6) | Master volume 0–32 | PhaseOff gradient bar (green→amber→red); shown briefly at boot | LED10 / PTH 9 follows the gradient |
| SHIFT (KEY_26), PLAY (KEY_27), LOOP (KEY_28), mode toggle (SW_TOG) | — reserved in v1 — | — | SHIFT LED on while held |

Master volume mapping identical to PhaseOff: TicksToNorm → fonepole smoothing → squared gain in the
audio callback. Audio path: 48 kHz, block 24, dual codec (headphones out 0/1, master line out 2/3).

### E. Persistence

- [code/src/Makefile](code/src/Makefile) gains USE_FATFS = 1, the coreJSON C source + include path, and
  the CPU_METER ifdef (same additions as PhaseOff's Makefile).
- Port + simplify C:\Dev\CHOMPI-PhaseOff\code\src\PresetStorage.h → PresetStorage.h and a small
  BowlsPreset.h: one file BOWLS/state.json holding all ticks + selected profile, with a schema version.
  Load at boot (defaults + no-card fallback), autosave debounced (~1 s after the last parameter change).
- coreJSON is already vendored at [code/libs/coreJSON](code/libs/coreJSON); FatFS ships with
  [code/libs/libDaisy](code/libs/libDaisy).

## Affected files / components

Existing:

- [code/src/chompi_main.cpp](code/src/chompi_main.cpp) — rewritten: hardware/UI/voice-manager glue.
- [code/src/Makefile](code/src/Makefile) — FatFS, coreJSON, CPU_METER.
- [code/src/chompi_sram.lds](code/src/chompi_sram.lds) — unchanged (verify regions still fit).
- [code/libs/DaisySP](code/libs/DaisySP) — used for dsp helpers (fonepole, etc.); unmodified.

New (to be created in code/src/): chompi_hardware.h, leds.h (ported), bowl_profile.h, bowl_voice.h,
bowl_scale.h, BowlsPreset.h, PresetStorage.h. Optional native test under code/tests/ (mirroring
C:\Dev\CHOMPI-PhaseOff\code\tests\).

## Step-by-step tasks (in order)

1. **UI skeleton**: port chompi_hardware.h + leds.h; rewire chompi_main.cpp (hw.Init, ProcessControls per
   block, RenderLeds + Flush in the main loop); Params with volume_ticks only; BumpTicks/ShowOverlay,
   DrawVolumeBar, boot volume bar, held-keys-light-white. Build, flash, verify the volume overlay bar.
2. **Bowl DSP core (mono, 1 voice)**: bowl_profile.h + bowl_voice.h; a white-key press strikes the voice
   at a fixed scale-table fundamental. Verify by ear: metallic ring, paired-mode beating, per-mode decay.
3. **Scale table**: bowl_scale.h; all 15 white keys mapped; verify non-equal intervals across the keyboard.
4. **Parameter plumbing**: Strike / Decay / Shimmer / Brightness / Character encoders with their overlay
   bars, PTH LED colors, and SectionStep profile select. Parameters live-update ringing voices.
5. **Polyphony**: 4 voices, free-else-quietest allocation, re-strike on ringing state; verify 4 overlapping
   long decays without glitches.
6. **Stereo placement**: fixed family A/B pan (fundamental centered, higher modes wider).
7. **Rim rubbing**: wheel-rate sustained excitation + wheel LEDs + rubbing overlay bar.
8. **Humanization**: small per-strike random variation of excitation amplitude and detune.
9. **Persistence**: Makefile additions, PresetStorage + BowlsPreset, boot load + debounced autosave;
   verify power-cycle restore.
10. **CPU measurement**: CPU_METER=1 build with 4 ringing voices + rubbing; if load is high, reduce to
    6–7 mode pairs (keep 4 voices) and re-measure.
11. **Polish pass**: optional dim key glow while a voice rings; full acceptance-criteria listen-through.

Each task ends with a build per the build skill (MSYS2 UCRT64, GCC 10.3.1, make -j4 DEBUG=0 OPT=-O3) and
its memory table; tasks 1–8 stay SRAM-only.

## Risks, open questions, memory/CPU/timing

- **Sound-design constants are templates.** Profile ratios/decays and the scale's intermediate
  frequencies are starting points anchored in the two cited studies, not final measurements; expect an
  ear-tuning pass. Data/DSP separation (spec §7) keeps this to table edits.
- **CPU**: 64 resonators ≈ a few FLOPs each per sample — expected modest at 48 kHz, but measure
  (task 10) rather than assume; contingencies: fewer mode pairs, cheaper noise burst.
- **SRAM is the bottleneck** (PhaseOff experience: "runs out of SRAM, not flash"). LED symbol buffers
  ≈ 5.6 KB; FatFS + JSON buffers add tens of KB — watch the SRAM column in the build report, keep < 90%.
  No SDRAM needed for the synthesis engine (spec §1).
- **Timing**: LED Flush blocks ~2 ms in the main loop (off the audio ISR) — fine; coefficient math happens
  only at strike / parameter change — no trig in the audio callback.
- **Rim rubbing with no key held** rubs the most recently struck voice (documented edge case).
- **Black keys are dark in v1**; a future Bowl Collection mode can assign them.
- **SD failure handling**: no card → boot defaults, autosave silently skipped in v1. Open question:
  should a save failure flash a warning (e.g. SHIFT LED)? Deferred to implementation.
- **Preset schema**: version the JSON from day one (PhaseOff's incompatible-slot detection is the
  precedent) so future firmware doesn't misread v1 files.
- **Reverb**: explicitly out of scope (spec §1 — future iteration).

## Testing and validation

- **Build**: per the build skill; always report the SRAM_EXEC / SRAM / SDRAM table; fail loudly on
  toolchain mismatch (make check-toolchain → GCC 10.3.1).
- **CPU meter**: CPU_METER=1 build; target comfortable headroom with 4 voices ringing + rubbing
  (PhaseOff prints CPU load over USB serial; reuse that pattern).
- **Native DSP test (optional but recommended)**: create code/tests/ mirroring
  C:\Dev\CHOMPI-PhaseOff\code\tests\ (dsp_test.cpp + Makefile): assert a resonator's measured peak
  frequency and ring time for a known mode, assert the scale table is strictly ascending, assert profile
  ratios match the spec anchors. Run only on explicit request (make -C code/tests test).
- **On-hardware validation** against the spec's acceptance criteria: single strike = clearly metallic bowl;
  long decay evolves naturally; beating audible but not exaggerated; four simultaneous notes without
  stealing glitches; profiles sound distinct; keyboard intervals unequal by design; soft vs hard strike
  audibly different; CPU headroom remains.
- **Install**: copy CHOMPI_BOWLS_v1.0.bin to the SD card root (delete other .bin files); do not flash the
  bootloader.
