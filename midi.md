# CHOMPI-BOWLS - MIDI Implementation

All messages use a fixed MIDI channel: **1**.
Transports: the hardware MIDI input (5-pin DIN, UART) and the USB MIDI
device port (the panel USB-C), simultaneously. A USB keyboard cannot be
plugged into CHOMPI directly - USB requires a host (a computer, or a USB
host adapter); use the keyboard's MIDI output or route it through the
host.
The USB device identifies itself to the host as **CHOMPI-BOWLS**.
USB MIDI is unavailable in CPU_METER diagnostic builds.
MIDI IN receives notes, supported CCs and MIDI Timing Clock. When clock
pulses arrive, the latched Strike arpeggio follows that tempo; without
clock it retains its normal free-running rate. MIDI OUT sends local keyboard
notes on both enabled transports; incoming messages are never echoed or
soft-thru'd, and no CCs are transmitted.
Other channels and unsupported MIDI messages are ignored.

## Web preset editor

USB MIDI SysEx protocol v1 supports reading and writing the 15 SD-card
preset slots. It validates schema-v11 data and saves through the staged
storage path; it does not expose or alter the embedded factory preset.
See the [web editor guide](web/README.md) for browser setup and deployment.

MIDI Timing Clock is accepted regardless of channel (24 pulses per
quarter note). The Strike arpeggio wheel selects the number of clock
pulses between strikes. Clock sync expires after 500 ms without a pulse;
DIN takes priority if both DIN and USB clocks are active.

Saved rotary parameters use the same 33 positions as the panel encoders:
`position = (value * 32 + 63) / 127`. CC values 0, 64 and 127 select
positions 0, 16 and 32. Profile ranges (CC 70), the exact-value edit
selectors (CC 81/82), the play-mode ranges (CC 80) and the on/off
switches (64-127 = on) use their own conversions, documented per row.

## MIDI In

| Function | Type / CC | Value span |
|---|---|---|
| Note On | Note On (0x90) | Note 48-72 (key = note - 48); velocity 1-127 (scaled to 0.0-1.0). Velocity 0 = Note Off. Other notes are ignored |
| Note Off | Note Off (0x80) | Note 48-72; release velocity ignored |
| MIDI rimming level | CC 1 | 0-127 = held rub level 0.0-1.0 for this transport's eligible notes (see Notes) |
| Master volume | CC 7 | 0-32 positions; gain is position-squared (0 = silence, 32 = unity). Saved |
| Bowl pan | CC 10 | 0-32 positions, left - centre (16) - right, for the edit-selected bowl. Saved |
| Sustain pedal | CC 64 | 0-63 off, 64-127 on; per transport; defers this sender's Rimming pad releases only |
| Resonance character | CC 70 | 0-25 = Temple, 26-51 = Deep, 52-76 = Bright, 77-102 = Wide, 103-127 = Thin; for the edit-selected bowl, applies at the next strike/pad/selection. Saved |
| Bowl brightness | CC 74 | 0-32 positions, darkest - brightest, for the edit-selected bowl (live). Saved |
| Bowl decay | CC 75 | 0-32 positions = 0.25x - 3x the profile decay, for the edit-selected bowl (live; tick 16 = about 0.87x). Deep profile maximum is about 45 s T60. Saved |
| Bowl volume | CC 77 | 0-32 positions (16 = unity, 32 = +6 dB), for the edit-selected bowl (live). Saved |
| Strike hardness | CC 78 | 0-32 positions, soft - hard mallet (global; next strike). Saved |
| Shimmer | CC 79 | 0-32 positions = 0x - 2x the profile paired-mode detune (global, live). Saved |
| Active play mode | CC 80 | 0-42 = Strikes, 43-85 = Rimming, 86-127 = Split; Rimming-to-Strikes preserves LOOP, Rimming-to-Split clears it. Saved |
| Parameter edit bank | CC 81 | Exact 0 = Strikes bank, 1 = Rimming, 2 = Split, 127 = follow the active mode; values 3-126 ignored. Selector only - never changes the mode or sounds a note |
| Parameter edit key | CC 82 | Exact value = MIDI note number 48-72 selects key 0-24; all other values ignored. Selector only - never sounds a note |
| Rimming LOOP latch | CC 83 | 0-63 = disabled, 64-127 = enabled (idempotent; inert outside Rimming) |
| MIDI rim texture | CC 84 | 0-63 = friction (wheel-clockwise equivalent), 64-127 = clean sustained tone; per transport |
| Audio routing | CC 85 | 0-63 = Normal, 64-127 = Send FX; session-only (boots to Normal, never saved) |
| Temple-room reverb mix | CC 91 | 0-32 positions, dry - wet (live). Saved |
| All Sound Off (panic) | CC 120 | Any value: stops all bowl voices and their reverb tail immediately (short fade), including local notes; saved settings unchanged |
| Reset All Controllers | CC 121 | Any value: resets this transport's sustain pedal, CC 1 level (0), CC 84 texture (friction) and edit context (follow mode, key 0); held notes keep sounding |
| All Notes Off | CC 123 | Any value: releases this transport's held/sustained/latched notes (natural releases; strike and reverb tails remain) |

### Note map (channel 1, all modes)

The active play mode decides what a note does; in Split it also decides
the bowl frequency. Note names assume C4 = MIDI 60 (some DAWs label
octaves differently). Frequencies below are the factory untuned
fundamentals; v11 `pitch_hz` values store each mode/key's final frequency
without changing its MIDI note. Encoder 1 Page 3 changes that frequency
directly; MIDI has no tuning CC. Resonance character (CC 70) changes the
modal structure. See the pitch-editing procedure and complete key/index
mapping in [README.md](README.md#direct-hz-tuning-and-preset-editing).

| MIDI note | Name | Key | Factory Strikes / Rimming Hz | Split action | Factory Split Hz |
|---|---|---|---|---|---|
| 48 | C3 | 0 | 99.0 Hz | Select rim bowl 1 | 99.0 |
| 49 | C#3 | 1 | 112.7 Hz | Select rim bowl 2 | 146.0 |
| 50 | D3 | 2 | 128.3 Hz | Select rim bowl 3 | 219.6 |
| 51 | D#3 | 3 | 146.0 Hz | Select rim bowl 4 | 310.2 |
| 52 | E3 | 4 | 167.3 Hz | Select rim bowl 5 | 390.0 |
| 53 | F3 | 5 | 191.7 Hz | Select rim bowl 6 | 465.0 |
| 54 | F#3 | 6 | 219.6 Hz | Select rim bowl 7 | 513.0 |
| 55 | G3 | 7 | 261.0 Hz | Select rim bowl 8 | 620.4 |
| 56 | G#3 | 8 | 310.2 Hz | Select rim bowl 9 | 693.6 |
| 57 | A3 | 9 | 347.8 Hz | Select rim bowl 10 | 780.0 |
| 58 | A#3 | 10 | 390.0 Hz | Select rim bowl 11 | 927.0 |
| 59 | B3 | 11 | 425.9 Hz | Select rim bowl 12 | 1026.0 |
| 60 | C4 | 12 | 465.0 Hz | Strike bowl 1 | 99.0 |
| 61 | C#4 | 13 | 513.0 Hz | Strike bowl 2 | 146.0 |
| 62 | D4 | 14 | 564.1 Hz | Strike bowl 3 | 219.6 |
| 63 | D#4 | 15 | 620.4 Hz | Strike bowl 4 | 310.2 |
| 64 | E4 | 16 | 693.6 Hz | Strike bowl 5 | 390.0 |
| 65 | F4 | 17 | 780.0 Hz | Strike bowl 6 | 465.0 |
| 66 | F#4 | 18 | 850.3 Hz | Strike bowl 7 | 513.0 |
| 67 | G4 | 19 | 927.0 Hz | Strike bowl 8 | 620.4 |
| 68 | G#4 | 20 | 1026.0 Hz | Strike bowl 9 | 693.6 |
| 69 | A4 | 21 | 1128.3 Hz | Strike bowl 10 | 780.0 |
| 70 | A#4 | 22 | 1240.8 Hz | Strike bowl 11 | 927.0 |
| 71 | B4 | 23 | 1395.0 Hz | Strike bowl 12 | 1026.0 |
| 72 | C5 | 24 | 1560.0 Hz | Strike added top bowl 13 | 1240.8 |

Notes:

- Incoming messages on channels other than 1 are discarded.
- Note behavior by mode: **Strikes** = a Note On strikes the bowl
  (velocity 1-127 maps linearly from softest to hardest timbre and keeps
  the existing 1/127-1.0 loudness scaling; MIDI velocity overrides CC 78
  for that strike, while CC 78 continues to control local strikes) and the
  Note Off ends its MIDI rubbing eligibility but never chokes the ring.
  **Rimming** = Note On opens a sustained pad (10 s attack/release);
  velocity sets a smoothed performance gain that never overwrites the
  saved bowl volume. **Split** = notes 48-59 silently select a rim bowl
  (never strike - sound starts via CC 1 or the physical wheel), notes
  60-72 strike.
- Every saved sound parameter except direct-Hz pitch has a CC: the five
  per-bowl parameters (pan 10, character 70, brightness 74, decay 75,
  bowl volume 77) edit the CC 81/82-selected bank/key, and the global
  controls are CC 7/78/79/91 plus the mode on CC 80. Saved values
  autosave to BOWLS/state.json after a 1 s quiet period, exactly as
  local edits. Never saved: velocity, CC 1/84 rubbing, sustain, the
  LOOP latch state, selectors, routing, and all cleanup messages.
- Editing a bowl without playing it: send CC 81 (bank), CC 82 (key),
  then the parameter CCs, in that order. A valid Note On also selects
  that key for the sending transport's subsequent per-bowl CCs (even
  with a fixed bank); Note Off and invalid notes do not move it. A
  fixed bank edits another mode's bowls while the current mode keeps
  sounding; CC 81 = 127 restores follow-mode. DIN and USB keep
  independent edit contexts; there is no cross-transport ordering
  guarantee and no echo acknowledging a selector.
- The instrument has five shared voices across local, DIN and USB
  playing; a sixth distinct note steals the quietest eligible voice.
  Latched Rimming voices are protected while Strikes is active, and a
  strike is dropped if all five voices are protected. Other stolen pads
  or strikes never restart by themselves; Split rim bowls reacquire
  silently, as with local play.
- CC 1 is a held absolute level per transport (initially 0), not a
  simulated wheel detent: it drives that sender's eligible notes in the
  current mode - its held Strikes notes, its held/sustained/latched
  Rimming pads, or its Split rim selection (its held rim keys, max two
  shared with other sources, else its last-selected fallback). It never
  drives the CC 82 edit key and never creates a strike. The physical
  wheel keeps working alongside: per bowl the higher of the wheel and
  MIDI levels wins (never summed), and the winner's texture applies; on
  equal levels the latest control event wins. A mode change clears held
  CC 1 levels.
- CC 64 is a conventional hold-to-sustain pedal for Rimming pads,
  distinct from the LOOP latch (CC 83): pedal-down defers the sender's
  pad releases; pedal-up releases only its own deferrals. The pedal is
  ignored for Strikes and Split. LOOP enable captures the currently
  held sounding pads (any source); LOOP disable smoothly releases
  latched-only notes. Rimming-to-Strikes preserves the latch and its
  voices; returning to Rimming resumes it. Rimming-to-Split clears it.
- CC 121 resets a sender's controllers but keeps its held notes;
  CC 123 releases a sender's notes (ignoring its pedal) without
  touching other sources, saved settings or the global LOOP state;
  CC 120 is the global emergency stop - it also silences local playing
  and the reverb tail. In Send FX routing the AUX return is
  intentionally NOT silenced by CC 120 (it may be another instrument).
- Incoming CCs set values silently: they never trigger the white-key
  parameter bars (the bars follow local encoders only). Held MIDI notes
  light their keys like physical keys, ringing voices glow dim amber,
  and the wheel LEDs follow the effective (wheel vs CC 1) rub level.
## MIDI Out

The physical keyboard sends Note On (0x90) and Note Off (0x80) on fixed
channel 1 over the DIN/UART output and USB MIDI device port. Key indices
0-24 map to MIDI notes 48-72, respectively, in every play mode. Note On
velocity is fixed at 127 because the keybed has no velocity sensor; a
physical key release sends Note Off, including when the BOWLS Rimming
LOOP latch keeps its internal pad sounding. SHIFT mode and routing
shortcuts are not sent as notes.

Only local physical key gestures are transmitted. MIDI received on DIN
or USB is not forwarded, and panel parameter changes, CCs, mode changes,
clock, and program changes are not transmitted. Firmware uses UART1 TX
on D14 for DIN output.

## Examples (all channel 1)

- **Strike the lowest bowl:** CC 80 = 0 (Strikes); Note On 48,
  velocity 100. Note Off 48 ends its held status; the bowl keeps
  ringing.
- **Edit that bowl without striking it:** CC 81 = 0, CC 82 = 48,
  CC 10 = 64 (pan centre), CC 77 = 64 (unity volume), CC 74 = 127
  (brightest). Set its frequency with Encoder 1 Page 3.
- **Change another bowl's character:** CC 81 = 0, CC 82 = 60,
  CC 70 = 26 selects Deep for Strikes key 12 (465 Hz) - not Split's
  99 Hz strike slot.
- **Sustain a clean pad:** CC 80 = 64 (Rimming), CC 83 = 0 (LOOP off),
  Note On 60 velocity 127. CC 64 = 127 then Note Off keeps it held;
  CC 64 = 0 releases it.
- **Rim Split bowl 1:** CC 80 = 127 (Split), CC 84 = 0 (friction),
  Note On 48 velocity 127, then CC 1 = 80. Note On 60 strikes the 99 Hz
  strike bowl alongside it. CC 1 = 0 stops MIDI rim excitation; the
  physical wheel remains usable.
- **Prepare Split strike bowl 1 while another mode plays:** CC 81 = 2,
  CC 82 = 60, CC 75 = 127 (longest decay). Restore follow-mode with
  CC 81 = 127 when finished.
