# MIDI Input and CC Control (feature plan)

> Superseded for bowl tuning by [per-bowl-direct-hz-tuning.md](./per-bowl-direct-hz-tuning.md): MIDI CC 76 is unsupported; tuning is direct Hz from Encoder 1 Page 3.

Status: implemented (2026-11-09). Firmware, host tests and midi.md are
complete; the "Firmware and hardware acceptance" matrix below is NOT yet
run on device - DIN/USB wiring, DAW interop and flood/overflow behavior
still need physical validation.

## Problem and goal

Play CHOMPI-BOWLS from an external keyboard, sequencer, DAW or MIDI controller,
and control every existing sound parameter remotely. Support both the CHOMPI
MIDI input and USB MIDI, with the same musical behavior as the local keyboard.

The completeness reference is CHOMPI-Pipes' `MidiHandler.h`, `MidiCcMap.h` and
`ParameterSteps.h` in the sibling repository: DIN and USB, fixed channel 1,
velocity-zero Note On treated as Note Off, and CC access to every saved sound
parameter. Reuse those conventions, not the organ-specific CC assignments or
its unbounded event drain inside the audio callback.

### Portable sibling repository reference

Resolve CHOMPI-Pipes as `../CHOMPI-Pipes` **relative to the CHOMPI-BOWLS
repository root**, not relative to this plan file or an assumed terminal working
directory. Both repositories live in the same parent directory; that parent's
absolute path and drive can differ between computers. Never depend on the path
used by the computer that created this plan.

Reference files, with all paths relative to the BOWLS repository root:

- `../CHOMPI-Pipes/code/src/MidiHandler.h`: working DIN/USB transport reference.
- `../CHOMPI-Pipes/code/src/MidiCcMap.h`: CC mapping conventions.
- `../CHOMPI-Pipes/code/src/ParameterSteps.h`: shared value conversions.
- `../CHOMPI-Pipes/midi.md`: required user-documentation format reference.

The implementing agent must locate the BOWLS root, resolve this sibling, and
check these files exist before relying on them. If the sibling is absent or has
a different directory name, ask for its location rather than assuming this
computer's absolute path. Treat PIPES as a read-only reference; changes belong
in BOWLS only.

BOWLS currently has five shared voices, three modes, four saved global controls,
and six saved per-key parameters across 3 x 25 independent slots. MIDI must
respect that model, including the unequal bowl scale and Split's separate rim
and strike settings. This feature does not introduce an equal-tempered synth,
more voices, new effects, or a preset-slot bank.

## Recommended approach

### 1. Transport and channel

- Receive MIDI 1.0 notes and CCs over the hardware MIDI input and USB MIDI device
  connection. Channel **1** only (libDaisy's zero-based channel index 0).
- USB is a device connected to a computer or another USB host. A USB keyboard
  cannot be plugged directly into CHOMPI without a suitable host; use its MIDI
  output or a USB host adapter instead.
- No MIDI output, soft thru, or parameter echo in this first version. This keeps
  the requested input workflow independent of Pipes' transmit/DMA machinery.
- Ignore other channels, unsupported CCs, Program Change, pitch bend, pressure,
  SysEx and realtime clock/transport. No clock synchronization is required.
- Use libDaisy MIDI parsers rather than a custom byte parser. Confirm board UART
  pins and the USB peripheral against Pipes and the installed libDaisy version
  before initializing them; do not assume a default UART configuration is safe.
- Service transport parsing/recovery in the main loop; move ordered events
  through fixed-capacity queues to the audio callback. Never run HAL recovery,
  USB initialization, file access, or blocking calls inside the audio ISR.

### 2. Complete MIDI note map

**MIDI note numbers are authoritative.** Names below use C4 = MIDI 60; some DAWs
display a different octave label. Notes **48 through 72 inclusive** map to local
key indices **0 through 24**, using `key = note - 48`. All other notes are ignored;
there is no wrap, transpose, clamp-to-edge, or hidden command-note range.

These names identify trigger keys, NOT the sounding pitch. Frequencies below
are the current untuned bowl fundamentals in
[bowl_scale.h](../code/src/bowl_scale.h). Each slot's saved tuning multiplies its
frequency by 0.5x through 2x. Profile changes the modal structure, not this map.

| MIDI note | Key name | Key index | STRIKES: strike / RIMMING: pad, Hz | SPLIT action | SPLIT base Hz |
| --------- | -------- | --------- | ------------------------------------------------ | ------------ | ------------- |
| 48 | C3 | 0 | 99.0 | Select rim bowl 1 | 99.0 |
| 49 | C#3 | 1 | 112.7 | Select rim bowl 2 | 146.0 |
| 50 | D3 | 2 | 128.3 | Select rim bowl 3 | 219.6 |
| 51 | D#3 | 3 | 146.0 | Select rim bowl 4 | 310.2 |
| 52 | E3 | 4 | 167.3 | Select rim bowl 5 | 390.0 |
| 53 | F3 | 5 | 191.7 | Select rim bowl 6 | 465.0 |
| 54 | F#3 | 6 | 219.6 | Select rim bowl 7 | 513.0 |
| 55 | G3 | 7 | 261.0 | Select rim bowl 8 | 620.4 |
| 56 | G#3 | 8 | 310.2 | Select rim bowl 9 | 693.6 |
| 57 | A3 | 9 | 347.8 | Select rim bowl 10 | 780.0 |
| 58 | A#3 | 10 | 390.0 | Select rim bowl 11 | 927.0 |
| 59 | B3 | 11 | 425.9 | Select rim bowl 12 | 1026.0 |
| 60 | C4 | 12 | 465.0 | Strike bowl 1 | 99.0 |
| 61 | C#4 | 13 | 513.0 | Strike bowl 2 | 146.0 |
| 62 | D4 | 14 | 564.1 | Strike bowl 3 | 219.6 |
| 63 | D#4 | 15 | 620.4 | Strike bowl 4 | 310.2 |
| 64 | E4 | 16 | 693.6 | Strike bowl 5 | 390.0 |
| 65 | F4 | 17 | 780.0 | Strike bowl 6 | 465.0 |
| 66 | F#4 | 18 | 850.3 | Strike bowl 7 | 513.0 |
| 67 | G4 | 19 | 927.0 | Strike bowl 8 | 620.4 |
| 68 | G#4 | 20 | 1026.0 | Strike bowl 9 | 693.6 |
| 69 | A4 | 21 | 1128.3 | Strike bowl 10 | 780.0 |
| 70 | A#4 | 22 | 1240.8 | Strike bowl 11 | 927.0 |
| 71 | B4 | 23 | 1395.0 | Strike bowl 12 | 1026.0 |
| 72 | C5 | 24 | 1560.0 | Strike added top bowl 13 | 1240.8 |

#### Note and performance semantics

- **STRIKES:** positive-velocity Note On strikes the mapped bowl. Note Off ends
  its held status and MIDI rubbing eligibility, but does not choke its ring.
- **RIMMING:** Note On opens a clean sustained pad; Note Off starts the existing
  release unless sustain or LOOP latch owns the gate. Retain the current pad
  attack/release: approximately 10 seconds to 90% / 10% respectively.
- **SPLIT, notes 48-59:** silently select a rim bowl, never strike. Sound starts
  through CC 1 or the physical wheel. Keep the existing maximum two held rim
  targets and last-selected fallback after release. The fallback is not CC 64
  sustain or the Rimming LOOP latch.
- **SPLIT, notes 60-72:** strike the corresponding bowl; Note Off leaves its tail.
  Rim key 0 and strike key 12 share 99 Hz but have independent settings/voices.
- Incoming notes never inherit local SHIFT. MIDI 54/56/58 always play/select,
  even while SHIFT is held; mode selection is CC 80. Likewise MIDI 61/63 never
  invoke local audio-routing shortcuts.
- Velocity 1-127 gives normalized performance gain `velocity / 127`.
  For strikes, map velocity linearly to hardness `(velocity - 1) / 126` and
  use the normalized velocity as independent amplitude; MIDI hardness
  overrides CC 78 for that strike. For pads/rim selection, keep the separate
  smoothed performance-gain factor; never overwrite saved bowl volume.
- Note On velocity 0 is Note Off; release velocity is ignored. Strikes may be
  retriggered without an intervening Note Off. Repeated pad Note Ons do not stack
  gates; with LOOP enabled they follow the existing on/off toggle semantics.
- Five voices total across local, DIN and USB inputs; a sixth distinct active
  identity steals the quietest voice. No automatic revival of stolen held pads
  or strikes. Split may silently reacquire selected rim voices, as today.
- A MIDI mode change is global and preserves ringing voices/reverb. Rimming to
  Strikes preserves LOOP, latched claims and pad gates; Rimming to Split clears
  them. Held notes continue to release against their original mode.

### 3. Complete CC map

All CCs are absolute setters, not encoder increments. Saved controls use the
existing panel resolution and DSP curves. For a CC value `v` in 0-127:

`t = (v * 32 + 63) / 127` using integer division, giving ticks 0-32.

This is Pipes' conversion. Values 0 / 32 / 64 / 95 / 127 select ticks
0 / 8 / 16 / 24 / 32. Unless a row says otherwise, ticks mean this conversion.

| CC | Parameter | Scope and conversion | Apply timing | Saved? |
| -- | --------- | -------------------- | ------------ | ------ |
| **1** | MIDI rimming level | Per input source; `v / 127`, 0 = no MIDI rub, 127 = full rub | Live, voice-side smoothing; held until changed | No |
| **7** | Master volume | Global; gain `(t / 32)^2`, 0 = silence, 32 = unity | Live, existing master smoothing | Yes |
| **10** | Bowl pan | Selected bank/key; `(t - 16) / 16`, left through center through right | Live on that identity, existing pan smoothing | Yes |
| **64** | Sustain pedal | Per input source; 0-63 off, 64-127 on | Live; Rimming gates only | No |
| **70** | Resonance character/profile | Selected bank/key; profile ranges listed below | Next strike, pad start or rim selection | Yes |
| **74** | Bowl brightness | Selected bank/key; `t / 32`, darkest through brightest | Live on that identity | Yes |
| **75** | Bowl decay | Selected bank/key; `0.25 * 12^(t / 32)`, 0.25x through 3x profile decay; tick 16 = 0.87x | Live on that identity | Yes |
| **76** | Bowl tuning | Selected bank/key; `2^((t - 16) / 16)`, half through unison through double | Next strike, pad start or rim selection; no live retune | Yes |
| **77** | Bowl volume | Selected bank/key; ticks 0-16: `(t / 16)^2`; 17-32: `1 + (t - 16) / 16` | Live, existing bowl-gain smoothing | Yes |
| **78** | Strike hardness | Global; `t / 32`, soft through hard, same mallet control as encoder 2 | Next strike | Yes |
| **79** | Shimmer | Global; `2 * t / 32`, 0x through 2x profile paired-mode detune | Live on all voices, as today | Yes |
| **80** | Active play mode | Global; 0-42 STRIKES, 43-85 RIMMING, 86-127 SPLIT | At event boundary; same cleanup as local mode selection | Yes |
| **81** | Parameter edit bank | Per input source; exact 0 = STRIKES, 1 = RIMMING, 2 = SPLIT, 127 = follow active mode; 3-126 ignored | Selector only; never changes play mode or starts a voice | No |
| **82** | Parameter edit key | Per input source; exact MIDI note number 48-72 selects key 0-24; all other values ignored | Selector only; never starts a voice | No |
| **83** | Rimming LOOP latch | Global; 0-63 disabled, 64-127 enabled; inert outside RIMMING | Idempotent setter matching LOOP cleanup/capture, not repeated toggling | No |
| **84** | MIDI rim texture | Per input source; 0-63 friction (clockwise equivalent), 64-127 clean (counter-clockwise equivalent) | Live, existing character crossfade | No |
| **85** | Audio routing | Global; 0-63 Normal, 64-127 Send FX | Next block, same routing as local selection | No; boot remains Normal |
| **91** | Temple-room reverb mix | Global; dry fraction `1 - t / 32`, wet fraction `t / 32` | Live, existing mix smoothing | Yes |
| **120** | All Sound Off / panic | Any value on channel 1; stop all internal bowl voices and their internal reverb tail, including local notes | Immediate output suppression, click-controlled short fade | No |
| **121** | Reset All Controllers | Any value; reset sender's transient MIDI controllers as specified below | Live; do not restore factory sound settings | No |
| **123** | All Notes Off | Any value; release sender's note claims, sustain-held notes and owned latched notes | Natural pad release/strike tails; no new excitation | No |

All other CCs, including CC 0/32 bank select, CC 11 expression, CC 98-101
NRPN/RPN, and channel-mode CC 122/124-127, are unsupported and ignored. In
particular, CC 75 is BOWLS' modal decay, not an ADSR decay, and CC 76 is stored
bowl tuning, not vibrato. Do not accidentally interpret reserved CCs as custom
parameters. Audio routing is included for remote completeness even though it is
not currently part of the saved preset.

#### CC 70 profile ranges

Use `profile = min(4, (v * 5) / 128)`; this bypasses the encoder's two-detent
accumulator, which is a physical-control affordance, not a MIDI requirement.

| CC value | Stored profile | Panel profile | Name |
| -------- | -------------- | ------------- | ---- |
| 0-25 | 0 | 1 | Temple |
| 26-51 | 1 | 2 | Deep |
| 52-76 | 2 | 3 | Bright |
| 77-102 | 3 | 4 | Wide |
| 103-127 | 4 | 5 | Thin |

### 4. Addressing every per-bowl parameter

Maintain separate edit context for DIN and USB. At boot each follows the active
mode and selects key 0. CC 81 selects a fixed bank or restores follow-mode;
CC 82 selects the key using the same note numbers as the note table. A valid
positive-velocity Note On also selects that key for the sender's subsequent
per-bowl CCs, even if the bank is fixed. Note Off and invalid notes do not move
the target. Global CCs ignore the selectors.

Thus every combination `(mode 0-2, key 0-24)` is addressable without playing a
note. Fixed-bank editing permits editing a different mode while the current
mode continues sounding. Keep MIDI selectors separate from local `last_key_`:
local playing/encoder edits must not redirect queued external automation.

Examples, all on MIDI channel 1:

- **Strike the lowest bowl:** CC 80 = 0; Note On 48, velocity 100. Note Off 48
  ends its held status; the bowl continues ringing.
- **Edit that bowl without striking:** CC 81 = 0, CC 82 = 48, CC 10 = 64
  (center), CC 77 = 64 (unity), CC 76 = 64 (unison), CC 74 = 127 (brightest).
- **Change another bowl's profile:** CC 81 = 0, CC 82 = 60, CC 70 = 26 selects
  Deep for STRIKES key 12 (465 Hz), not Split's 99 Hz strike slot.
- **Sustain a clean pad:** CC 80 = 64 (RIMMING), CC 83 = 0 (LOOP off), Note On
  60, velocity 127. CC 64 = 127 then Note Off keeps it held; CC 64 = 0 releases.
- **Rim Split bowl 1:** CC 80 = 127, CC 84 = 0, Note On 48, velocity 127,
  CC 1 = 80. Note On 60 strikes the 99 Hz strike bowl alongside it. CC 1 = 0
  stops MIDI rim excitation; the physical wheel remains usable.
- **Prepare Split strike bowl 1 while another mode plays:** CC 81 = 2,
  CC 82 = 60, CC 75 = 127. Restore follow-mode with CC 81 = 127 when finished.

Send bank, key, then parameter messages in that order from one transport. When
automating several bowls, send the selectors before each group of writes;
subsequent notes can change the sender's edit key. There is no cross-transport
ordering guarantee or echo acknowledging a selector transaction.

### 5. MIDI rubbing and local wheel coexistence

- CC 1 is a held absolute level, not a simulated wheel detent and not an
  autosaved parameter. Initial level 0, initial CC 84 texture friction.
- It drives the sender's eligible notes in the current active mode: held
  STRIKES, held/sustained/latched RIMMING, or its Split rim selection/fallback.
  It does not drive whichever key CC 82 selects and cannot create a strike.
- Keep Split selection history per source; combine eligible rim keys with the
  existing overall maximum of two held targets, ordered by accepted presses.
  Without held rim targets use the most recently selected source fallback.
- Compute wheel and MIDI candidates separately. For each eligible `(mode,key)`,
  use the highest available level, not the sum, avoiding extra gain from mixed
  controllers. Its candidate's texture wins; on equal levels, latest relevant
  control event wins. Wheel contribution retains its current idle decay.
- MIDI level stays held until CC 1 changes or a cleanup event occurs. A new
  eligible note receives the current level. Releasing a strike stops its MIDI
  excitation even though its resonator ring remains. Split fallback can keep
  rubbing after Note Off, matching local Split selection behavior.
- On a mode change clear MIDI rub levels to zero, preventing a nonzero level
  from unexpectedly driving the newly selected mode. The next CC 1 sets it.
- Preserve the existing exception: Split rim targets can reacquire silently;
  other modes never revive stolen notes merely because CC 1 is nonzero.

### 6. Ownership, sustain, reset and panic

The current `PlayModes` stores one `down_mode_` per physical key. Feeding MIDI
directly into `KeyDown`/`KeyUp` would lose ownership when local, DIN and USB play
the same key, or when a held note spans a mode change. Extend this abstraction
with source-aware claims; do not add a second competing voice pool/state machine.

- Track local/DIN/USB held claims and their originating modes; keep latched and
  sustain-held claims source-aware too. Pad gates stay on while any valid claim
  owns `(mode,key)`. One sender's Note Off cannot release another sender's pad.
- A repeated Note On from one MIDI source does not require multiple Note Offs.
  If the same source/note is replayed in a different mode while held, release its
  previous claim first, then create the new one. Preserve outgoing audio tails.
- Same-identity simultaneous pad/rim claims share one voice. Use the maximum
  normalized strength among owners; when no owners remain, retain the release
  strength. Strike intensity applies to each excitation, not to a gain that
  would rescale previous strikes. Stealing clears all gates/latches for that
  identity, while remembering physical held state until a fresh press.
- CC 64 is standard hold-to-sustain, distinct from LOOP toggling. In RIMMING it
  defers the sender's released gates; pedal-up clears only its sustain claims.
  Ignore pedal effects on Strikes and Split. Leaving Rimming clears deferred
  sustain claims; actually held outgoing pads wait for their Note Off.
- CC 83 changes the existing global LOOP-enabled state. Enable captures current
  held sounding Rimming claims; disable drops latched-only claims. Track which
  source owns each latch so CC 123 can clean one sender without erasing local
  latches. Repeated CC 83 values are idempotent.
- CC 121 resets the sender's sustain, rub level (0), texture (friction), and edit
  context (follow active mode, key 0). Clear its deferred sustain claims but keep
  actually held notes. Do not change saved parameters, routing, or global LOOP
  state/latched claims; use CC 83 or CC 123 for those explicitly.
- CC 123 clears all sender-owned held/deferred/latched claims, rub level and
  Split fallback. Ignore sustain for this cleanup so it cannot leave stuck pads.
  Other owners keep their gates; otherwise pads release naturally. Strike and
  reverb tails remain. Global LOOP-enabled state stays unchanged.
- CC 120 is deliberately a global emergency panic, unlike sender-scoped CC 123.
  Clear all performance claims, LOOP, sustain, wheel/MIDI excitation and Split
  fallbacks. Suppress the internal bowl/reverb bus with a short <=5 ms fade;
  reset voice/reverb state outside the realtime critical path before unmuting.
  Do not run full `BowlVoice::Init` or `ReverbSc::Init` in the audio ISR. Require
  fresh presses; no revival from a still-held physical key. Leave saved controls
  unchanged. In Send FX, AUX return is intentionally NOT silenced by MIDI panic
  (it may be another instrument); master volume remains its return control.
- USB disconnect or UART receive error clears that transport's claims and
  rubbing, using natural releases. Main-loop UART recovery follows Pipes'
  stop/start approach. DIN cable removal is generally undetectable without
  Active Sensing; no blanket inactivity timeout, which would break long held
  notes. Do not claim complete DIN unplug detection in this release.

### 7. Persistence and feedback

- Route local and MIDI setters through shared parameter application helpers;
  keep pan/volume/brightness/decay live updates scoped to matching `(mode,key)`.
  Profiles and tuning apply only at the existing excitation boundaries.
- Mark saved state dirty only when its quantized value actually changes. Use
  the existing one-second-after-last-change autosave and staged SD service.
  Never save selectors, velocity, sustain, rubbing, latch, panic, or routing.
- Keep schema v7: all saved MIDI targets already have fields. No schema bump,
  migration, or discard of existing state is needed for a receive-only feature.
- MIDI CC streams must not continually refresh keyboard parameter bars; those
  belong to local encoder interaction, following Pipes' incoming-CC convention.
  Reflect sounding/held MIDI notes and LOOP in existing note/latch LEDs. Keep
  local encoder page indicators intact; use wheel LEDs for effective rub level.
- Boot restores the saved active mode and sound parameters, but initializes
  transient MIDI context/claims to the defaults defined above.

### 8. User-facing MIDI documentation (PIPES format)

Create a root-level `midi.md` during implementation, matching the location,
structure, table style and level of detail of the sibling repository's
`../CHOMPI-Pipes/midi.md` (resolved from the BOWLS repository root). Adapt the
content to BOWLS; do not copy
PIPES-specific controls or claim its bidirectional support for this input-only
feature. This is a required deliverable, not just a link to this planning file.

Required document structure:

1. **Title:** `CHOMPI-BOWLS - MIDI Implementation`.
2. **Opening summary:** fixed channel 1; hardware MIDI input (DIN/UART) and USB
  MIDI; receive-only; ignored channels and unsupported message types. Include
  the USB host requirement and simultaneous DIN/USB support.
3. **Value conversion paragraph:** the same 33-position formula as PIPES, with
  CC 0/64/127 selecting ticks 0/16/32. Explain that profile ranges, exact-value
  selectors and switches use their separately documented conversions. Do not
  include PIPES' outgoing conversion as though BOWLS sends parameter CCs.
4. **`MIDI In`:** use PIPES' exact table columns `Function | Type / CC | Value
  span`. Include Note On (0x90), Note Off (0x80), and a separate row for every
  supported CC in this plan, including selectors, sustain, routing and cleanup.
  Document value spans and units, not just controller numbers. Follow with the
  complete 25-row note table from section 2: note numbers/names, key indices,
  mode-specific actions and untuned fundamentals. Make explicit that active
  play mode changes note behavior and, in Split, can change bowl frequency.
5. **`MIDI Out`:** retain the heading for consistency with PIPES, but state
  explicitly that no messages are transmitted: no local note/CC output, echo
  or soft thru. Do not invent an outgoing implementation table.
6. **`Notes:`:** follow PIPES' concise bullet style. Cover CC 81/82 select-before-
  edit behavior and defaults, note-driven target changes, independent DIN/USB
  selectors, fixed-bank versus follow-mode editing, and the six per-bowl versus
  global parameters. Include the exact profile ranges/names, apply timing,
  autosave versus transient state, velocity/Note Off behavior, five-voice limit,
  sustain versus LOOP, rimming/wheel coexistence, reset/panic behavior and DIN
  unplug limitations. State that incoming CCs do not trigger local value bars.
7. **Examples:** include ordered channel-1 messages for playing a bowl, editing
  one without sounding it, preparing an inactive mode's bowl, and Split rimming
  via CC 1/84. These must agree with the final implemented selector semantics.

Add a MIDI documentation link to the existing root user guide during
implementation. Before release, cross-check every documented note, CC number,
range, default and unsupported-message claim against the implementation and
host tests; exercise the examples over both DIN and USB. Mark any unfinished
hardware verification honestly rather than presenting planned behavior as
already verified.

## Affected files/components

- [code/src/chompi_main.cpp](../code/src/chompi_main.cpp): shared absolute setters,
  event application, velocity-aware excitation, wheel arbitration, MIDI startup,
  bounded callback dispatch, main-loop transport service and cleanup.
- [code/src/play_modes.h](../code/src/play_modes.h): source-aware performance
  ownership, explicit mode/latch setters, sustain and cleanup; preserve local
  shortcut consumption and original-mode release behavior.
- [code/src/bowl_voice.h](../code/src/bowl_voice.h): separate smoothed pad/rim
  performance gain and a narrowly scoped panic reset path if needed. Existing
  strike intensity already supplies the strike velocity entry point.
- [code/src/chompi_hardware.h](../code/src/chompi_hardware.h): verify MIDI pin
  ownership and USB initialization compatibility; change only if necessary.
- [code/src/BowlsPreset.h](../code/src/BowlsPreset.h) and
  [code/src/PresetStorage.h](../code/src/PresetStorage.h): existing v7 fields and
  autosave integration; no format change expected.
- [code/src/bowl_scale.h](../code/src/bowl_scale.h) and
  [code/src/bowl_profile.h](../code/src/bowl_profile.h): authoritative unchanged
  note frequencies and profile names.
- Proposed new `code/src/MidiCcMap.h`: pure constants, selectors and conversions.
- Proposed new `code/src/MidiHandler.h`: BOWLS transport adapter and fixed queues,
  no organ engine dependency or transmit implementation.
- [code/tests/play_modes_test.cpp](../code/tests/play_modes_test.cpp),
  [code/tests/pad_test.cpp](../code/tests/pad_test.cpp),
  [code/tests/strike_test.cpp](../code/tests/strike_test.cpp),
  [code/tests/preset_test.cpp](../code/tests/preset_test.cpp) and
  [code/tests/Makefile](../code/tests/Makefile): extend existing behavior coverage.
- Proposed new `code/tests/midi_test.cpp`: mapping, ordered dispatch, queues and
  MIDI ownership integration without Daisy hardware. Add a `midi-test` target.
- Proposed new root `midi.md`: user-facing implementation reference matching
  PIPES' root MIDI document, as specified in section 8.
- Existing root user guide: add a link to the new MIDI reference at implementation
  time, alongside connection notes as appropriate.

## Step-by-step tasks, in order

1. Resolve the sibling reference files using the portable repository-root rule
  above. Approve this wire protocol and default channel. Confirm it against a
  DAW and a typical controller before freezing the CC numbers.
2. Create the pure mapping/conversion header and focused host tests first.
   Exhaust all 128 CC values, all 128 notes and all 75 bank/key addresses.
3. Extend `PlayModes` with source-aware claims and explicit mode/latch setters;
   add sustain, source cleanup and steal tests while preserving all local tests.
4. Extract minimal shared setters from encoder handlers, preserving their local
   overlay behavior. Test changed-value dirty marking and per-identity updates.
5. Add strike velocity using the existing intensity argument; add independent
   smoothed pad/rim strength. Verify velocity 127 preserves local reference sound.
6. Implement MIDI rub candidates, deterministic wheel arbitration and Split
   selection handling; test silence, held absolute level, release and steals.
7. Implement reset/all-notes-off/panic and transport-loss handling; validate the
   deferred reset handshake cannot race audio rendering or unmute stale tails.
8. Add DIN/USB receive adapters, source-specific ordered fixed queues and bounded
   audio dispatch. Verify UART recovery and USB/serial peripheral compatibility.
9. Integrate startup, main-loop service, callback event application, LED status
   and existing autosave. No MIDI parser/HAL or file work in audio context.
10. Run host regressions, build firmware using the build skill, report SRAM,
    SRAM_EXEC and SDRAM usage, then execute the hardware matrix below.
11. Create root `midi.md` in the PIPES format specified in section 8, including
  the verified note/CC tables, selector examples, receive-only MIDI Out section
  and unsupported-message list. Link it from the root user guide and validate
  documentation against the implementation and both transports. Keep this
  plan awaiting approval until implementation is explicitly authorized.

## Risks, open questions, memory/CPU/timing considerations

- **Protocol decisions for approval:** channel 1 only; current unequal-pitch
  key mapping; exact-value edit selectors; CC 1 absolute rimming; sender-scoped
  sustain/All Notes Off; global LOOP and global panic; receive-only scope.
- **USB logging conflict:** current code uses USB serial diagnostics. Verify
  whether the selected USB peripheral permits MIDI and logs concurrently.
  If not, prioritize USB MIDI and disable/reroute USB serial diagnostics in MIDI
  builds; do not repeatedly initialize the same peripheral for both classes.
- **Callback deadline:** 24 samples at 48 kHz gives 0.5 ms per audio block. Start
  with at most four MIDI events total per block, fairly alternating nonempty
  DIN/USB queues while preserving order within each source. Measure worst-case
  note allocation/coefficient updates; reduce the budget if necessary.
- **Queue sizing:** initial target 128 fixed four-byte normalized events per
  transport, about 1 KB total plus indices. Additional source claims/selectors
  should stay within a few KB SRAM; measure actual `sizeof` and linker totals.
  No dynamic allocation, extra voice bank or new SDRAM buffers for normal MIDI.
- **Overflow must not lose releases:** on overflow latch a source cleanup request
  separate from the event queue, discard that source's backlog, and release its
  claims before accepting a fresh batch. Count drops/recoveries outside the ISR.
  Never process stale queued Note Ons after emergency cleanup.
- **Timing/order:** under light load apply at the next audio block after parsing.
  Fair dispatch prevents DIN from starving USB. Floods add measurable latency;
  verify throughput/latency rather than claiming sample-accurate USB scheduling.
- **Velocity/headroom:** strong hits and +6 dB bowl gain can hit existing soft
  clipping. Keep normalized velocity <=1 and rub arbitration at max, not sum;
  inspect low-velocity monotonicity and mixed-controller behavior by listening.
- **SD wear:** quantization and debounce eliminate duplicate writes, but spaced
  automation can still cause repeated saves. Retain current persistence semantics
  initially; a session-only automation mode would be a separate approved feature.
- **Panic reset:** reverb contains substantial SDRAM state. Audio suppression and
  safe staged clearing need explicit synchronization; a heavy reset in a callback
  is not acceptable. Measure fade and reset/recovery time on hardware.
- **DIN disconnect limitation:** without Active Sensing there is no reliable
  unplug event. Document CC 123/120 recovery; optional Active Sensing is deferred.
- **Not included:** MIDI out/thru, configurable channels, MPE, chromatic mapping,
  pitch bend, per-note pressure, NRPN, Program Change presets, MIDI clock and
  future delay/recording controls. Existing saved parameters are all covered.

## Testing and validation

### Host tests

1. Verify every note table row against `BaseFundamentalHz` in all three modes;
   notes 47/73 and all other out-of-range inputs do nothing. Verify channel 1
   filtering and positive/zero-velocity Note On behavior.
2. Exhaust tick conversion endpoints/monotonicity/33 reachable positions and
   profile boundaries 25/26, 51/52, 76/77, 102/103. Test exact selector values,
   invalid-value no-ops, all 75 slots and independent DIN/USB contexts.
3. Assert all four saved global controls, active mode and all six saved banks
   are reachable. Verify live versus next-excitation timing, no cross-bank edits,
   no local-target redirection, unchanged values not marking dirty, and v7
   serialization retaining edited values without transient state.
4. Test local/DIN/USB overlap, source-isolated releases, repeated Note Ons,
   mode changes before Note Off, local SHIFT held during MIDI notes, shared
   velocity claims, five-voice steals and no stolen-pad revival.
5. Test pedal down/up, LOOP capture/toggle/off, idempotent CC 83, outgoing-mode
   cleanup, source-specific CC 121/123 and global CC 120 with held local keys.
6. Test rub level zero/full/held, texture switching, max arbitration/tie order,
   Split two-key limit/fallback/reacquisition, mode-change reset and CC 82 not
   redirecting performance rubbing.
7. Test per-source FIFO order, fair bounded dispatch, simultaneous transports,
   queue overflow/cleanup and connection recovery with no stale note revival.
8. Run `make -C code/tests midi-test`, `modes-test`, `strike-test`, `routing-test`
   and `test` in the existing MSYS2 host-test environment. Extend current test
   files rather than duplicating their DSP fixtures.

### Firmware and hardware acceptance

1. Build with the repository build skill; record SRAM, SRAM_EXEC and SDRAM usage
   and compare with a pre-feature build. Use CPU_METER only with a logging path
   compatible with USB MIDI. Confirm no dropouts at the chosen dispatch budget.
2. Test DIN and USB separately, then together, with a keyboard and DAW. Confirm
   all 25 notes/actions in each mode and the actual custom fundamentals.
3. Sweep every listed CC and verify its range, target, apply timing and sound.
   Edit a silent/inactive bank, switch into it, and confirm stored settings.
4. Run sequences with held notes across modes, mixed local/external same-key
   notes, velocity extremes, pedal/latch interaction and more than five notes.
5. Combine wheel and CC 1, verify clean/friction transitions, no doubled drive,
   and that Note Off differs correctly among Strikes, Rimming and Split.
6. Flood both transports with notes/CCs while staged SD autosave runs. Measure
   worst block time, event latency and drop counters; verify overflow recovery.
7. Unplug/reconnect USB, inject DIN framing errors and use CC 123/120. Confirm
   recovery, no stuck MIDI pads, no stale reverb after panic, and unchanged AUX
   return behavior in Send FX. Explicitly document undetectable DIN unplug.
8. Power-cycle after CC edits: all saved sound controls/mode return; routing
   returns to Normal and all transient MIDI state returns to its boot defaults.
9. Confirm MIDI automation never takes over local parameter bars and no input
   MIDI is echoed back to a DAW. No implementation is considered complete until
   this physical DIN/USB validation passes; host tests cannot prove wiring.
10. Verify root `midi.md` follows PIPES' structure and table columns, documents
  all 25 notes and all supported CCs, and clearly states that MIDI Out is not
  implemented. Cross-check values/defaults with the mapping tests, follow its
  selector examples over DIN and USB, and confirm the user-guide link resolves.