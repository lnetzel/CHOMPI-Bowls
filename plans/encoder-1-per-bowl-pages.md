# Encoder 1 — Per-Bowl Pages: Volume / Character / Tuning (plan)

> Superseded for tuning behavior by [per-bowl-direct-hz-tuning.md](./per-bowl-direct-hz-tuning.md): Page 3 now uses direct Hz, with SHIFT+turn for 1 Hz fine adjustment.

Status: awaiting approval. Builds on [plans/bowls-v1-first-firmware.md](plans/bowls-v1-first-firmware.md).

## Problem and goal

Physical encoder 1 (SW4 / ENC_4) currently selects the bowl profile, with SHIFT+turn panning the last
struck bowl. Repurpose it as a three-page per-bowl editor (press to cycle 1→2→3→1, same pattern as the
volume knob):

| Page | Turn | SHIFT+turn |
| ---- | ---- | ---------- |
| 1 — Bowl volume | Volume of the last struck bowl (0..32) | Pan of the last struck bowl (moved here, unchanged) |
| 2 — Resonance character | Bowl profile 1–5 (the existing page-1 behavior) | — |
| 3 — Bowl tuning | Tuning ratio of the last struck bowl (0..32) | — |

"Selected bowl" is the last struck white key (the existing `last_white_key_` selection, same as pan
today). All new parameters persist in BOWLS/state.json across power cycles.

## Decisions (from the clarifying questions — all confirmed)

| # | Question | Decision |
| - | -------- | -------- |
| 1 | Page cycling | **Press encoder 1 cycles 1→2→3→1**; the newly active page's bar flashes as confirmation (volume-knob pattern). SHIFT stays free for pan. |
| 2 | Tuning range | **Free ratio ½×–2×**: ticks 0..32 map logarithmically, `ratio = 2^((t−16)/16)` (~75 cents/step); 16 = unison. |
| 3 | Volume curve | **16 = unity (0 dB), 32 = +6 dB, 0 = silence**: ticks 0..16 → squared 0..1, ticks 16..32 → linear 1..2×. |
| 4 | Apply timing | **Volume live** (per-sample smoothed gain on ringing voices, click-free like pan); **tuning at the next strike** (physical character of the bowl). |

## Recommended approach

### A. Parameter state + persistence (schema v4)

- [code/src/chompi_main.cpp](code/src/chompi_main.cpp) `Params` gains two per-bowl arrays:
  `std::atomic<int> bowl_vol_ticks[15]` and `tune_ticks[15]`, both defaulting to 16 (unity / unison).
- [code/src/BowlsPreset.h](code/src/BowlsPreset.h): `kVersion` 3→4, add `uint8_t bvol[15]` and
  `uint8_t tune[15]` with `kDefaultBowlVol = 16` / `kDefaultTune = 16`; serialize as `"bvol":[...]` and
  `"tune":[...]`; parse with the same strtol array-walk as `pan`. Older v1–v3 files load with defaults
  for the new arrays (existing tolerance rule).
- [code/src/PresetStorage.h](code/src/PresetStorage.h): `kJsonSize` 384 → **512** (current ~230 B +
  ~106 B for the two arrays ≈ 336 B was too tight). SRAM cost +128 B, trivial.
- Autosave, boot load and the 1 s debounce need no changes — `MarkDirty` already covers the new turns.

### B. BowlVoice per-bowl gain stage

[code/src/bowl_voice.h](code/src/bowl_voice.h): add `SetBowlGain(float)` with `gain_` / `gain_target_`
smoothed per sample with the same 0.002f slew as the pan gains, multiplied into the output alongside
`pan_l_`/`pan_r_`. +2 floats per voice. Tuning needs no voice change: `StrikeKey` computes
`kBowlScaleHz[key] * TuneRatioTicks(tune_ticks[key])` at strike time (one `powf` per tick change /
strike, never per sample).

### C. Encoder-1 page state machine

In `ApplyControls()` ([code/src/chompi_main.cpp](code/src/chompi_main.cpp)):

- `hw.EncoderClicked(0)` cycles `enc1_page_` 1→2→3→1 and calls `ShowOverlay()` for the new page.
- Rework the encoder-1 turn block by page:
  - **Page 1, turn**: bump `bowl_vol_ticks[last_white_key_]`, then live-apply
    `SetBowlGain(BowlGainTicks(...))` to every voice holding that key (pan-style loop, already proven).
    **Page 1, SHIFT+turn**: the existing pan block, moved verbatim.
  - **Page 2, turn**: the existing `SectionStep` profile-select block, moved verbatim (two detent ticks
    per step kept).
  - **Page 3, turn**: bump `tune_ticks[last_white_key_]`; audible at the next strike.
- Overlay shows on every turn including end stops (consistent with pan today).
- `StrikeKey` applies both stored per-bowl values alongside `SetPan`.

### D. LED feedback

- New `Overlay::BowlVolume` and `Overlay::Tune` entries. Bowl volume: green left→right `DrawBar`
  (distinct from the master-volume gradient bar on the volume knob). Tuning: directional center bar on
  white key 8 like `DrawPanBar`, in violet (centre = unison, bar grows toward the flattened/sharpened
  side).
- LED2 / PTH 1 (encoder-1 LED) follows the page: green = volume, orange = character (unchanged),
  violet = tuning. Page-confirmation flash comes free via `ShowOverlay`.

## Affected files / components

- [code/src/chompi_main.cpp](code/src/chompi_main.cpp) — page state machine, encoder-1 rework, two new
  overlays + bars, PTH LED page colors, `SnapshotParams`/`ApplyPreset`, top-of-file control-map comment.
- [code/src/bowl_voice.h](code/src/bowl_voice.h) — smoothed per-voice bowl gain.
- [code/src/BowlsPreset.h](code/src/BowlsPreset.h) — schema v4 (`bvol`, `tune`).
- [code/src/PresetStorage.h](code/src/PresetStorage.h) — `kJsonSize` bump.
- [code/src/bowl_scale.h](code/src/bowl_scale.h), [code/src/bowl_profile.h](code/src/bowl_profile.h),
  [code/src/Makefile](code/src/Makefile), [code/src/chompi_sram.lds](code/src/chompi_sram.lds) —
  unchanged.

## Step-by-step tasks (in order)

1. **Persistence first**: schema v4 in BowlsPreset.h (`bvol`/`tune` arrays, serialize, parse, defaults),
   `kJsonSize` bump in PresetStorage.h, `Params` arrays + `SnapshotParams`/`ApplyPreset` wiring. Build
   and verify a power cycle restores defaults.
2. **Voice gain stage**: `SetBowlGain` + smoothing in bowl_voice.h; apply in `ProcessBlock`; build.
3. **Page state machine**: `enc1_page_`, press-to-cycle with confirmation overlays, LED2 page colors.
   Build, flash, verify page cycling and LED feedback.
4. **Page 1**: per-bowl volume turn handling (live apply to ringing voices + stored apply in
   `StrikeKey`) and the pan block moved to SHIFT+turn on page 1; green bar overlay. Verify live volume
   rides a ringing bowl click-free.
5. **Page 2**: move the character/profile block (behavior unchanged). Verify sections + SectionStep.
6. **Page 3**: tuning ticks → `TuneRatioTicks`, strike-time application, violet directional bar. Verify
   a detuned bowl sounds shifted only on re-strike.
7. **Docs**: top-of-file control-map comment in chompi_main.cpp.
8. **Build + listen-through** per the build skill (MSYS2, GCC 10.3.1, `make -j4`), report the
   SRAM/SRAM_EXEC/SDRAM table.

## Risks, open questions, memory/CPU/timing

- **JSON headroom**: resolved by the 384→512 B buffer bump (task 1).
- **Tuning is strike-time only** (decision 4): turning it while a bowl rings shows the bar but changes
  nothing audible until the next strike — by design.
- **75-cent tuning steps** are coarse for fine pitch matching; accepted with the "free ratio" decision.
  Revisit as a cents-based range only if ear tests demand it.
- **CPU**: one extra fonepole per voice per sample (gain slew) — negligible; `powf` only on tick
  changes/strikes. No new SRAM_EXEC or SDRAM pressure; +~160 B SRAM for state. Confirm with the build
  memory table.
- **Open question (deferred)**: whether per-bowl volume should also scale the rub level of the affected
  voice. Proposal: yes, it falls out naturally since the gain is applied at the voice output.

## Testing and validation

No host-test harness exists yet (`code/tests/` is uncreated); BowlsPreset.h stays host-testable
(no Daisy deps), so a native serialize/parse round-trip test for the v4 schema can be added there later.
For this feature:

1. Build per the build skill; confirm the SRAM/SRAM_EXEC/SDRAM table shows no region growth issues.
2. On hardware: page-cycle through 1→2→3→1 and confirm bar + LED2 color on each press.
3. Page 1: strike a bowl, turn — ringing voice gets louder/softer smoothly; SHIFT+turn still pans.
4. Page 2: profile select behaves exactly as before.
5. Page 3: detune a bowl, re-strike — pitch shifted; other bowls unaffected.
6. Power-cycle: all per-bowl volume, pan and tuning values restored.
