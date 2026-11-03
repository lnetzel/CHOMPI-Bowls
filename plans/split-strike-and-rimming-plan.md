# Split Keyboard: Strike and Rimming Zones

Status: **implemented 2026-11-03** — build clean, on-device validation
pending (checklist below). Idea doc:
[split_strike_and_rimming.md](split_strike_and_rimming.md) (Q&A decisions
recorded below, 2026-11-03).

## Problem and goal

Today the 15 white keys all strike bowls and the transport wheel rubs the held
keys (or the last-struck voice). Realistic playing needs rimming of **two bowls
at once**, and while rimming there is less need for strike keys. So the keybed
splits into two zones:

- **Rim zone — keys 0-11 (C3-B3)**: the 12 lowest-tuned bowls
  (99 Hz .. 1026 Hz). Pressing a key never strikes; it *selects* the bowl.
  The wheel makes the selected/held bowls sing.
- **Strike zone — keys 12-23 (C4-B4)**: the same 12 bowls, played as strikes.
- **Key 24 (C5): dead.**
- Black keys become playable. Polyphony grows 4 -> 6 voices.
- The 3 highest bowls (1240.8 / 1395 / 1560 Hz) are dropped for now.

## Decisions (Q&A 2026-11-03)

| Question | Decision |
|---|---|
| Split point | Rim 0-11, strike 12-23, key 24 dead |
| Voice allocation | Shared pool of 6, quietest-voice steal |
| Rim selection | Single latch: last-pressed rim key stays selected; two-bowl rimming = hold two keys |
| Per-bowl settings | Independent per half (24 slots; rim and strike instances of a bowl can differ) |
| Rim selection LEDs | No persistent indication; keep the existing rub overlay bar while turning |

## Recommended approach

### Key map (bowl_scale.h)

- Trim `kBowlScaleHz` to `kBowlCount = 12` (drop the top three entries, keep
  them in a comment for later).
- Replace `kWhiteOrdinal` / `kWhiteToPitchIndex` with
  `kBowlForKey[25] = {0..11, 0..11, -1}` plus zone constants
  (`kRimKeyLast = 11`, `kStrikeKeyFirst = 12`, `kStrikeKeyLast = 23`).

### Voice model (bowl_voice.h)

- New method `SelectForRub(float fundamental_hz, float detune_jitter)`:
  sets the fundamental, applies the humanized detune jitter and calls
  `UpdateCoeffs()` (which also recomputes the register-relative rub
  lowpass), **without** starting a burst. Rub weights, pan/gain setters
  and the per-sample rub slew are unchanged.

### Key handling and rim selection (chompi_main.cpp)

- `kVoiceCount` 4 -> 6. Voices are keyed by **key index** (0..23) instead of
  white ordinal, so a rim instance and a strike instance of the same bowl are
  separate voices (matching the independent-settings decision).
- Rim state: `rim_held_[2]` (press-ordered, maintained via `KeyRising` /
  `KeyFalling` on keys 0-11) and `rim_latch_` (last-pressed rim key).
- Rim-key press: allocate a voice (same allocator as strikes: key match ->
  free voice -> quietest steal), `SelectForRub`, apply that key's
  profile/brightness/pan/gain, set `rim_latch_` and `last_key_` (encoder 1
  edit target, replaces `last_white_key_`). No strike, no reverb-send
  darkening.
- `ApplyRub()` rework: the active rub set is the held rim keys (up to 2,
  newest first) or, when none are held, the latched key. Voices get
  `SetRub(rub_level)` only when their key is in the active set.
- On wheel activity, *ensure* voices exist for the active set: if a latched
  bowl's voice was stolen, re-allocate and `SelectForRub` silently so the
  wheel always has something to sing into.
- Key 24: ignored (no strike, no selection, no LED).

### Parameters and preset v5 (chompi_main.cpp, BowlsPreset.h)

- `pan_ticks` / `bowl_vol_ticks` / `tune_ticks` widen 15 -> 24
  (`kPlayableKeys`), indexed by key index 0..23; ApplyPreset/SavePreset
  loops follow.
- `BowlsPreset`: `kVersion` 4 -> 5, `kWhiteKeys` -> `kKeySlots = 24`,
  arrays sized 24. `Parse` accepts version < 5 files: read the old 15-entry
  arrays, then remap `arr[12+k] = arr[k]` for k in 0..11 (both halves inherit
  the bowl's old settings; the three dropped bowls are discarded).
- Serialized JSON grows by ~80 B (~340 B -> ~420 B) — still fits the 512 B
  single-sector buffer in PresetStorage.h (`kJsonSize`), but headroom shrinks
  to ~90 B; verify at build time.

### UI

- Overlay bars (volume, strike, brightness, ...) stay on the 15 white keys —
  they are meters, not bowl indicators; unchanged.
- No persistent rim-selection LEDs (decided); the rub overlay bar while
  turning the wheel is the only rub feedback.

## Affected files/components

- [bowl_scale.h](../code/src/bowl_scale.h) — 12-bowl table, `kBowlForKey`, zone constants
- [bowl_voice.h](../code/src/bowl_voice.h) — `SelectForRub()`
- [chompi_main.cpp](../code/src/chompi_main.cpp) — 6 voices, key-indexed voice identity, rim selection state machine, `ApplyRub()`, key loop, params arrays, preset apply/save
- [BowlsPreset.h](../code/src/BowlsPreset.h) — v5, 24-slot arrays, v4 migration
- [PresetStorage.h](../code/src/PresetStorage.h) — verify `kJsonSize` headroom (expected: no change)
- [leds.h](../code/src/leds.h) — no change (`kWhiteKeyLeds` bars stay as-is)

## Step-by-step tasks, in order

1. bowl_scale.h: 12-bowl table, `kBowlForKey[25]`, zone constants; update the
   header comment (black keys now playable, key 24 dead).
2. bowl_voice.h: `SelectForRub(fundamental_hz, detune_jitter)`.
3. chompi_main.cpp: `kVoiceCount` 6; voice identity by key index; `StrikeKey`
   takes a key index and reads bowl/settings through `kBowlForKey` and the
   per-key arrays.
4. chompi_main.cpp: rim selection state (`rim_held_[2]`, `rim_latch_`,
   `last_key_`); key loop split by zone; rim-press voice allocation.
5. chompi_main.cpp: `ApplyRub()` active-set logic + ensure-voices-on-wheel.
6. Params + preset v5: widen arrays to 24, bump version, v4 migration remap,
   update ApplyPreset/SavePreset, check serialized size against `kJsonSize`.
7. Build (build skill) + a clean `CPU_METER=1` build to measure the 6-voice
   load (remember: `make clean` first — make does not track `C_DEFS`).
8. On-device validation (checklist below).

## Risks, open questions, memory/CPU/timing

- **Rim voice stolen mid-sing**: with a shared pool, a loud struck chord can
  steal a bowl that is actively being rubbed (audible choke). Accepted
  (decision), mitigated by the ensure-on-wheel rule in task 5.
- **Re-press retunes a ringing rim voice**: `SelectForRub` runs
  `UpdateCoeffs()` on a possibly ringing voice — a subtle live beat-rate
  change. Acceptable; can be made "only when inactive" later if it bothers.
- **CPU**: 6 voices = +50% resonator math over the current build. Must be
  measured with a `CPU_METER=1` build (task 7); fallback: 5 voices or fewer
  mode pairs for the two highest bowls.
- **Memory**: +2 voices ≈ +1.3 KB SRAM, preset struct +27 B, JSON ~+80 B
  (~90 B headroom left in the 512 B sector buffer). All modest; confirm in
  the build report.
- Open question 1: should turning the wheel while holding *strike-zone* keys
  rub them? Proposed: no — the rub set is lower-half only.
- Open question 2: brief LED flash when a rim key is selected (no sound
  confirms the press)? Currently: no indication (decision) — easy to add later.

## Testing and validation

No native test harness exists yet (`code/tests/` is absent). BowlsPreset.h is
host-testable by design (no Daisy dependencies), so the preset work gets a
small optional host check; everything else is validated on-device.

- Optional host test: v5 round-trip (serialize -> parse) and v4 -> v5
  migration (15-entry arrays land on keys 0-11 and are copied to 12-23;
  clamps hold; newer-version files are still rejected).
- On-device checklist:
  1. Press a rim key (no sound), turn the wheel -> that bowl sings.
  2. Hold two rim keys, turn -> both sing; release both -> ring decays, wheel
     rubs the last-pressed one again (latch).
  3. Strike zone: keys 12-23 strike the same 12 bowls; key 24 is silent.
  4. Encoder 1 pages edit rim and strike instances independently (press key
     in that zone first, then turn).
  5. Loud struck chord while rimming -> at most a choke, wheel re-selects
     (ensure rule).
  6. Save/reboot: settings persist; a v4 `state.json` loads with both halves
     inheriting the old per-bowl settings.
  7. `CPU_METER=1` build: strike 4 bowls while rubbing 2 -> CPU load stays
     comfortable (target < ~80%).
