#pragma once

// Hardware-independent play-mode state machine for CHOMPI-BOWLS
// (plans/improve-play-modes.md, plans/midi-input-and-cc-control.md).
// Owns the performance mode, the SHIFT + lower-black-key mode
// shortcuts, per-source per-key press provenance (which input source
// holds which key, and in which mode the press began, so a release
// after a mode switch still releases the right note) and the Rimming
// LOOP latch. No audio, no hardware: chompi_main.cpp feeds debounced
// key/LOOP/MIDI events in and executes the returned KeyAction on the
// voice pool; pad gate-offs are reconciled from PadOn() every block,
// so a single event can never lose a release.
//
// Ownership: every held note is a per-SOURCE claim (local keyboard,
// MIDI DIN, MIDI USB). A Rimming pad gate stays open while ANY source
// owns the (mode, key) identity - one sender's Note Off never
// releases another sender's pad. Latch and sustain (CC 64) deferrals
// are source-aware too, so CC 123 cleans one sender without erasing
// the others.
//
// Latch rules (Rimming only):
//   - latch off: note-on opens the pad gate, note-off releases it
//   - LOOP on captures the currently held, sounding pad notes (any
//     source); a later note-on toggles that SOURCE's claim on
//     (latched) or off (released immediately, even while the note
//     stays held); note-off alone never releases a latched note
//   - LOOP off clears all latch flags; latched-only notes enter the
//     10 s release, held notes keep their gate to note-off
//   - leaving Rimming performs the same cleanup and also clears
//     deferred sustain claims; a stolen latched or held note is
//     unlatched and never restarts by itself
// Latch, held-key, sustain and pad state are transient - never
// persisted.

#include "bowl_scale.h"

#include <cstdint>

namespace bowls
{
/** What the firmware must do in response to one input event. Pad
 *  gate-CLOSING is never reported here: the voice pool reconciles
 *  against PadOn() after every event (and every block), so bulk
 *  releases (LOOP off, mode exit, steal) cannot drop a note-off. */
struct KeyAction
{
    enum Kind : uint8_t
    {
        kNone = 0,   // consumed (or meaningless): no musical action
        kStrike,     // strike key `key` in mode `mode`
        kPadOn,      // open the clean pad gate for `key` (Rimming)
        kRimSelect,  // Split rim-zone selection of `key`
        kModeChanged // the active mode changed to `mode`
    };

    Kind     kind = kNone;
    PlayMode mode = PlayMode::kStrikes;
    int      key  = -1;
};

/** Performance input sources: the physical keyboard and the two MIDI
 *  transports. Claims, velocities, sustain pedals, latch ownership
 *  and Split selection history are tracked per source. */
enum class Source : uint8_t
{
    kLocal   = 0, // the physical 25-key bed
    kMidiDin = 1, // MIDI in on the DIN/UART input
    kMidiUsb = 2, // MIDI in on the USB device port
};
constexpr int kSourceCount = 3;

class PlayModes
{
  public:
    PlayModes() { Init(PlayMode::kStrikes); }

    /** Boot / preset-load state: mode `mode`, nothing held, latch off
     *  (latch and performance state are never persisted). */
    void Init(PlayMode mode)
    {
        mode_          = mode;
        shortcut_key_  = -1;
        latch_enabled_ = false;
        rim_held_[0]   = -1;
        rim_held_[1]   = -1;
        rim_held_n_    = 0;
        rim_latch_     = -1;
        rim_latch_src_ = -1;
        for(int s = 0; s < kSourceCount; ++s)
        {
            sustain_on_[s]    = false;
            src_rim_latch_[s] = -1;
            for(int k = 0; k < kKeyCount; ++k)
            {
                down_mode_[s][k]   = -1;
                latched_[s][k]     = false;
                sustained_[s][k]   = false;
                muted_held_[s][k]  = false;
                velocity_[s][k]    = 0;
            }
        }
        for(int k = 0; k < kKeyCount; ++k)
        {
            pad_on_[k]       = false;
            stolen_pad_[k]   = false;
            rim_held_src_[k] = 0;
        }
    }

    PlayMode Mode() const { return mode_; }

    // -- shortcuts ----------------------------------------------------

    static bool IsModeShortcut(int key)
    {
        return key == kModeShortcutKey[0] || key == kModeShortcutKey[1]
               || key == kModeShortcutKey[2];
    }

    static PlayMode ShortcutMode(int key)
    {
        for(int m = 0; m < kPlayModeCount; ++m)
            if(kModeShortcutKey[m] == key)
                return static_cast<PlayMode>(m);
        return PlayMode::kSplit; // unreachable for shortcut keys
    }

    // -- Rimming latch state ------------------------------------------

    /** LOOP latch enabled (Rimming only; cleared when Rimming is left). */
    bool LatchEnabled() const { return latch_enabled_; }

    /** Key's pad note is latched on for ANY source (drives the
     *  latched-key LED). */
    bool Latched(int key) const
    {
        if(key < 0 || key >= kKeyCount)
            return false;
        for(int s = 0; s < kSourceCount; ++s)
            if(latched_[s][key])
                return true;
        return false;
    }

    /** Desired pad gate for a Rimming key; chompi_main reconciles the
     *  voice gates against this after every event, so latch-off, mode
     *  exit and voice steals all close gates reliably. */
    bool PadOn(int key) const
    {
        return key >= 0 && key < kKeyCount ? pad_on_[key] : false;
    }

    /** The mode the LOCAL physically held key was pressed in, -1
     *  when not held. */
    int DownMode(int key) const { return DownMode(Source::kLocal, key); }

    /** The mode source `src`'s held claim on `key` began in, -1 when
     *  that source does not hold the key. */
    int DownMode(Source src, int key) const
    {
        return key >= 0 && key < kKeyCount
                   ? down_mode_[static_cast<int>(src)][key]
                   : -1;
    }

    /** Any source holds a claim on `key` (drives the held-key LED, so
     *  MIDI-held notes show like physical ones). */
    bool AnyHeld(int key) const
    {
        if(key < 0 || key >= kKeyCount)
            return false;
        for(int s = 0; s < kSourceCount; ++s)
            if(down_mode_[s][key] >= 0)
                return true;
        return false;
    }

    /** Maximum note-on velocity (1..127) among the current owners of
     *  the identity (mode, key); 0 when nothing owns it (the caller
     *  then retains the voice's last performance gain through the
     *  release). Rimming owners include latched and sustained claims. */
    int ClaimStrength(PlayMode mode, int key) const
    {
        if(key < 0 || key >= kKeyCount)
            return 0;
        const int m    = static_cast<int>(mode);
        int       best = 0;
        for(int s = 0; s < kSourceCount; ++s)
        {
            bool owns;
            if(m == RimmingIndex)
                owns = down_mode_[s][key] == RimmingIndex
                       || latched_[s][key] || sustained_[s][key];
            else
                owns = down_mode_[s][key] == m;
            if(owns && velocity_[s][key] > best)
                best = velocity_[s][key];
        }
        return best;
    }

    // -- Split rim set (press-ordered, max 2; latch = last selected) ---

    int RimHeldCount() const { return rim_held_n_; }
    int RimHeld(int i) const
    {
        return i >= 0 && i < rim_held_n_ ? rim_held_[i] : -1;
    }
    int RimLatch() const { return rim_latch_; }

    /** Split rub-set membership: held rim keys win; when none are held,
     *  the last-selected rim key. Distinct from the LOOP latch. */
    bool InRubSet(int key) const
    {
        if(rim_held_n_ > 0)
            return key == rim_held_[0]
                   || (rim_held_n_ > 1 && key == rim_held_[1]);
        return key >= 0 && key == rim_latch_;
    }

    /** Is `key` a wheel target in the CURRENT mode? The caller still
     *  checks the voice pool for an existing, sounding voice (the wheel
     *  never creates a strike or revives a stolen full-mode note). The
     *  wheel set is source-independent: any held strike, any sounding
     *  pad, or the combined Split rub set. */
    bool WheelTargets(int key) const
    {
        if(key < 0 || key >= kKeyCount)
            return false;
        switch(mode_)
        {
            case PlayMode::kSplit: return InRubSet(key);
            case PlayMode::kStrikes:
                for(int s = 0; s < kSourceCount; ++s)
                    if(down_mode_[s][key] == StrikeIndex)
                        return true;
                return false;
            case PlayMode::kRimming: return pad_on_[key];
        }
        return false;
    }

    /** Is `key` an eligible CC 1 rub target for MIDI source `src` in
     *  the CURRENT mode? Per-sender eligibility: the sender's own held
     *  strikes, its own held/sustained/latched pads, or its own Split
     *  rim selection (its held rim keys, else its own last-selected
     *  fallback). Never the CC 82 edit key, and never a strike. */
    bool MidiRubTargets(Source src, int key) const
    {
        if(key < 0 || key >= kKeyCount)
            return false;
        const int s = static_cast<int>(src);
        switch(mode_)
        {
            case PlayMode::kStrikes:
                return down_mode_[s][key] == StrikeIndex;
            case PlayMode::kRimming:
                return pad_on_[key]
                       && (down_mode_[s][key] == RimmingIndex
                           || latched_[s][key] || sustained_[s][key]);
            case PlayMode::kSplit:
                if(key > kRimKeyLast)
                    return false;
                if((rim_held_src_[key] & SrcBit(s)) != 0)
                    return true;
                // Without held rim keys of its own, the sender's
                // last-selected rim key is its fallback.
                return !SrcHoldsRimKey(s) && src_rim_latch_[s] == key;
        }
        return false;
    }

    // -- events ---------------------------------------------------------

    /** Key pressed. `shift` = SHIFT held at this instant. */
    KeyAction KeyDown(int key, bool shift)
    {
        KeyAction a;
        if(key < 0 || key >= kKeyCount)
            return a;
        // A consumed shortcut press swallows every event of that
        // physical key until its release, even if SHIFT came up first:
        // it must not strike, select, toggle a latched note, or change
        // the bowl editor target.
        if(shortcut_key_ == key)
            return a;
        if(shift && IsModeShortcut(key))
        {
            shortcut_key_          = key;
            const PlayMode target  = ShortcutMode(key);
            if(target == mode_)
                return a; // idempotent re-selection: consumed, no change
            LeaveMode();
            mode_  = target;
            a.kind = KeyAction::kModeChanged;
            a.mode = mode_;
            return a;
        }
        return NoteOnCore(Source::kLocal, key, 127);
    }

    /** Local key released. */
    KeyAction KeyUp(int key)
    {
        KeyAction a;
        if(key < 0 || key >= kKeyCount)
            return a;
        if(shortcut_key_ == key)
        {
            shortcut_key_ = -1; // fully consumed: no note-off either
            return a;
        }
        NoteOffCore(Source::kLocal, key);
        return a; // strikes are one-shot: nothing to report
    }

    /** LOOP toggles the Rimming note latch. Reserved (inert) in the
     *  other modes. */
    void LoopPressed() { SetLatchEnabled(!latch_enabled_); }

    // -- MIDI events (no shortcut/routing consumption, never SHIFT) -----

    /** MIDI Note On (velocity 1..127; velocity 0 arrives as
     *  MidiNoteOff). The mode in force at this instant owns the claim;
     *  a same-source replay while the note is still held in a
     *  DIFFERENT mode releases the previous claim first (its tail
     *  rings out), then starts the new one. */
    KeyAction MidiNoteOn(Source src, int key, int velocity)
    {
        KeyAction a;
        if(key < 0 || key >= kKeyCount)
            return a;
        const int s = static_cast<int>(src);
        if(down_mode_[s][key] >= 0
           && down_mode_[s][key] != static_cast<int>(mode_))
            NoteOffCore(src, key); // held over a mode change: release first
        return NoteOnCore(src, key, velocity);
    }

    /** MIDI Note Off: releases only this source's claim. Returns the
     *  mode the claim began in (also for an inert Note Off on a
     *  latched/sustained note), or -1 when this source held no claim.
     *  Strikes end their held/rub status but keep ringing. */
    int MidiNoteOff(Source src, int key)
    {
        if(key < 0 || key >= kKeyCount)
            return -1;
        return NoteOffCore(src, key);
    }

    /** CC 64 sustain pedal for one source (Rimming only): while on, the
     *  sender's released pad gates are deferred; pedal-up clears only
     *  this source's deferred claims. Inert for Strikes/Split claims. */
    void MidiSetSustain(Source src, bool on)
    {
        const int s    = static_cast<int>(src);
        sustain_on_[s] = on;
        if(!on)
        {
            for(int k = 0; k < kKeyCount; ++k)
                if(sustained_[s][k])
                {
                    sustained_[s][k] = false;
                    RecomputePadGate(k);
                }
        }
    }

    /** CC 123 All Notes Off for one source: clears its held, deferred
     *  and latched claims and its Split fallback (sustain is ignored
     *  for this cleanup, so it cannot leave stuck pads). Other owners
     *  keep their gates; otherwise pads release naturally via PadOn().
     *  Strike tails and the global LOOP-enabled state stay. */
    void MidiAllNotesOff(Source src)
    {
        const int s = static_cast<int>(src);
        for(int k = 0; k < kKeyCount; ++k)
        {
            down_mode_[s][k]   = -1;
            latched_[s][k]     = false;
            sustained_[s][k]   = false;
            muted_held_[s][k]  = false;
            velocity_[s][k]    = 0;
            RecomputePadGate(k);
            if(k <= kRimKeyLast && (rim_held_src_[k] & SrcBit(s)) != 0)
            {
                rim_held_src_[k] &= static_cast<uint8_t>(~SrcBit(s));
                if(rim_held_src_[k] == 0)
                    RemoveRimHeld(k);
            }
        }
        src_rim_latch_[s] = -1;
        if(rim_latch_src_ == s)
        {
            rim_latch_     = -1;
            rim_latch_src_ = -1;
        }
    }

    /** CC 121 Reset All Controllers for one source: sustain pedal off
     *  and its deferred sustain claims cleared (actually held notes and
     *  latched claims keep sounding; LOOP state is untouched). The
     *  caller resets the source's rub level/texture and edit context. */
    void MidiResetControllers(Source src)
    {
        MidiSetSustain(src, false);
    }

    /** CC 120 All Sound Off (global emergency panic): clears ALL
     *  performance claims of every source, the LOOP latch, sustain
     *  pedals, the Split rim set and both fallbacks. Voices keep their
     *  tails until the caller silences them; saved parameters and the
     *  active mode are untouched. A still-held key/note cannot revive
     *  anything - a fresh press is required. */
    void MidiPanic()
    {
        for(int s = 0; s < kSourceCount; ++s)
        {
            sustain_on_[s]    = false;
            src_rim_latch_[s] = -1;
            for(int k = 0; k < kKeyCount; ++k)
            {
                down_mode_[s][k]  = -1;
                latched_[s][k]    = false;
                sustained_[s][k]  = false;
                muted_held_[s][k] = false;
                velocity_[s][k]   = 0;
            }
        }
        latch_enabled_ = false;
        rim_held_[0]   = -1;
        rim_held_[1]   = -1;
        rim_held_n_    = 0;
        rim_latch_     = -1;
        rim_latch_src_ = -1;
        for(int k = 0; k < kKeyCount; ++k)
        {
            pad_on_[k]       = false;
            stolen_pad_[k]   = false;
            rim_held_src_[k] = 0;
        }
    }

    // -- explicit setters (MIDI CC 80 / CC 83) --------------------------

    /** CC 80 mode change: the same cleanup as the local mode shortcut
     *  (latched-only outgoing pads release; held pads keep their gate
     *  until their own Note Off). Returns true when the mode changed
     *  (idempotent re-selection is a no-op). */
    bool SetMode(PlayMode target)
    {
        if(target == mode_)
            return false;
        LeaveMode();
        mode_ = target;
        return true;
    }

    /** CC 83 LOOP setter (idempotent, inert outside Rimming): enabling
     *  captures the currently held sounding pads of every source;
     *  disabling drops latched-only claims (held/sustained notes keep
     *  their gates). */
    void SetLatchEnabled(bool on)
    {
        if(mode_ != PlayMode::kRimming || on == latch_enabled_)
            return;
        if(on)
        {
            // Capture the currently held, sounding pad notes (a pad
            // whose voice was stolen has pad_on_ cleared and is not
            // captured). Each held source keeps its own latch claim,
            // so a later CC 123 cleans one sender only.
            latch_enabled_ = true;
            for(int k = 0; k < kKeyCount; ++k)
                if(pad_on_[k])
                    for(int s = 0; s < kSourceCount; ++s)
                        if(down_mode_[s][k] == RimmingIndex)
                            latched_[s][k] = true;
        }
        else
        {
            latch_enabled_ = false;
            for(int k = 0; k < kKeyCount; ++k)
            {
                for(int s = 0; s < kSourceCount; ++s)
                    latched_[s][k] = false;
                // Latched-only notes enter the 10 s release; notes
                // still held (or pedal-deferred) keep their gate.
                RecomputePadGate(k);
            }
        }
    }

    /** The voice holding (mode, key) was stolen by a newer note: clear
     *  that note's latch/sustain claims (all sources) and its gate.
     *  LOOP itself stays enabled; the held state is remembered (the
     *  claim markers stay), but a stolen note is never restarted by
     *  itself - it must be released and played again. Split rim voices
     *  are not affected here (chompi_main silently reacquires them). */
    void OnVoiceStolen(PlayMode mode, int key)
    {
        if(mode == PlayMode::kRimming && key >= 0 && key < kKeyCount)
        {
            for(int s = 0; s < kSourceCount; ++s)
            {
                latched_[s][key]   = false;
                sustained_[s][key] = false;
            }
            stolen_pad_[key] = true; // suppress the gate until a fresh press
            pad_on_[key]     = false;
        }
    }

  private:
    static constexpr int8_t StrikeIndex
        = static_cast<int8_t>(PlayMode::kStrikes);
    static constexpr int8_t RimmingIndex
        = static_cast<int8_t>(PlayMode::kRimming);
    static constexpr int8_t SplitIndex
        = static_cast<int8_t>(PlayMode::kSplit);

    static constexpr uint8_t SrcBit(int s)
    {
        return static_cast<uint8_t>(1u << s);
    }

    bool SrcHoldsRimKey(int s) const
    {
        for(int i = 0; i < rim_held_n_; ++i)
            if((rim_held_src_[rim_held_[i]] & SrcBit(s)) != 0)
                return true;
        return false;
    }

    /** The pad gate follows the claims: open while any source owns the
     *  Rimming (mode, key) identity (held, latched or pedal-deferred),
     *  except after a steal, which suppresses the gate until a fresh
     *  press (a stolen held note never restarts by itself). A claim
     *  toggled OFF by the latch while still held (muted) keeps its
     *  held marker but no longer feeds the gate. */
    void RecomputePadGate(int key)
    {
        bool on = false;
        if(!stolen_pad_[key])
            for(int s = 0; s < kSourceCount && !on; ++s)
                on = (down_mode_[s][key] == RimmingIndex
                      && !muted_held_[s][key])
                     || latched_[s][key] || sustained_[s][key];
        pad_on_[key] = on;
    }

    /** Shared note-on for local keys (velocity 127) and MIDI notes:
     *  records the claim and returns the musical action. No shortcut
     *  consumption here - the local wrapper handles SHIFT before this. */
    KeyAction NoteOnCore(Source src, int key, int velocity)
    {
        KeyAction  a;
        const int  s = static_cast<int>(src);
        const bool had_claim = down_mode_[s][key] >= 0 || latched_[s][key]
                               || sustained_[s][key];
        down_mode_[s][key]   = static_cast<int8_t>(mode_);
        muted_held_[s][key]  = false; // a fresh press is never muted
        velocity_[s][key]    = static_cast<uint8_t>(
            velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity));
        a.mode = mode_;
        a.key  = key;
        switch(mode_)
        {
            case PlayMode::kStrikes: a.kind = KeyAction::kStrike; break;
            case PlayMode::kRimming:
                if(latch_enabled_)
                {
                    // Note-on TOGGLES this source's note: a second
                    // press on its sounding note starts its release
                    // immediately, even while still held. Another
                    // owner keeps the gate open.
                    if(had_claim && pad_on_[key])
                    {
                        // Toggle OFF: the note releases immediately,
                        // even while still held. The held marker STAYS
                        // (muted), so the later note-off is a
                        // consistent no-op and another press toggles
                        // the note back on; a muted claim feeds
                        // neither the gate nor the shared strength.
                        velocity_[s][key]   = 0;
                        latched_[s][key]    = false;
                        sustained_[s][key]  = false;
                        muted_held_[s][key] = true;
                        RecomputePadGate(key);
                        a.kind = KeyAction::kNone; // gate-off via PadOn()
                    }
                    else
                    {
                        latched_[s][key]  = true;
                        stolen_pad_[key]  = false;
                        const bool was_on = pad_on_[key];
                        pad_on_[key]      = true;
                        // A pad already sounding for another owner is
                        // joined silently - no fresh attack.
                        a.kind = was_on ? KeyAction::kNone
                                        : KeyAction::kPadOn;
                    }
                }
                else
                {
                    stolen_pad_[key]   = false;
                    const bool was_on  = pad_on_[key];
                    pad_on_[key]       = true;
                    // A repeated Note On does not stack gates (and does
                    // not restart the attack); it updates the velocity
                    // claim, which the caller re-applies as strength.
                    a.kind = (was_on && had_claim) ? KeyAction::kNone
                                                   : KeyAction::kPadOn;
                }
                break;
            case PlayMode::kSplit:
                if(key <= kRimKeyLast)
                {
                    // Press-ordered held set, max 2 (two-bowl rimming),
                    // shared across sources: a third held rim key drops
                    // the oldest from the rub set - its ring decays
                    // naturally.
                    if((rim_held_src_[key] & SrcBit(s)) == 0)
                    {
                        if(rim_held_src_[key] == 0)
                        {
                            // Not in the held set at all: append,
                            // evicting the oldest when full.
                            if(rim_held_n_ < 2)
                                rim_held_[rim_held_n_++] = key;
                            else
                            {
                                rim_held_src_[rim_held_[0]] = 0;
                                rim_held_[0]                = rim_held_[1];
                                rim_held_[1]                = key;
                            }
                        }
                        rim_held_src_[key] |= SrcBit(s);
                    }
                    rim_latch_        = key;
                    rim_latch_src_    = s;
                    src_rim_latch_[s] = key;
                    a.kind            = KeyAction::kRimSelect;
                }
                else
                {
                    a.kind = KeyAction::kStrike; // keys 12..24
                }
                break;
        }
        return a;
    }

    /** Shared note-off: releases only this source's held claim (a
     *  latched note survives its Note Off; a pedal-down Rimming Note
     *  Off defers into a sustained claim). Returns the mode the claim
     *  began in, -1 when the source held no claim at all. */
    int NoteOffCore(Source src, int key)
    {
        const int s  = static_cast<int>(src);
        const int dm = down_mode_[s][key];
        if(dm < 0)
            return latched_[s][key] || sustained_[s][key] ? RimmingIndex
                                                          : -1;
        const bool was_muted  = muted_held_[s][key];
        down_mode_[s][key]    = -1;
        muted_held_[s][key]   = false;
        if(dm == RimmingIndex)
        {
            // Note-off alone never releases a latched note; a held
            // pedal defers the release into a sustained claim (a
            // toggled-off muted claim has nothing left to defer).
            if(latched_[s][key])
                return dm;
            if(sustain_on_[s] && !was_muted)
                sustained_[s][key] = true;
            RecomputePadGate(key);
        }
        else if(dm == SplitIndex && key <= kRimKeyLast)
        {
            rim_held_src_[key] &= static_cast<uint8_t>(~SrcBit(s));
            if(rim_held_src_[key] == 0)
                RemoveRimHeld(key);
        }
        return dm; // strikes: the claim ends, the ring is not choked
    }

    void RemoveRimHeld(int key)
    {
        for(int i = 0; i < rim_held_n_; ++i)
            if(rim_held_[i] == key)
            {
                rim_held_[i] = rim_held_[rim_held_n_ - 1];
                --rim_held_n_;
                break;
            }
    }

    /** Leaving a mode: Rimming performs the latch cleanup (LOOP off,
     *  latched-only notes release, deferred sustain claims clear;
     *  actually held pads keep their gate until their own note-off -
     *  they release against their ORIGINAL mode). */
    void LeaveMode()
    {
        if(mode_ != PlayMode::kRimming)
            return;
        latch_enabled_ = false;
        for(int k = 0; k < kKeyCount; ++k)
        {
            for(int s = 0; s < kSourceCount; ++s)
            {
                latched_[s][k]   = false;
                sustained_[s][k] = false;
            }
            RecomputePadGate(k);
        }
    }

    PlayMode mode_         = PlayMode::kStrikes;
    int      shortcut_key_ = -1;
    // Per-source claims: the mode the press began in (-1 = not held),
    // latch/sustain deferrals, latch-toggle-muted held markers, and the
    // note-on velocity (0 = no claim).
    int8_t  down_mode_[kSourceCount][kKeyCount];
    bool    latched_[kSourceCount][kKeyCount];
    bool    sustained_[kSourceCount][kKeyCount];
    bool    muted_held_[kSourceCount][kKeyCount];
    uint8_t velocity_[kSourceCount][kKeyCount];
    bool    sustain_on_[kSourceCount];
    bool    latch_enabled_ = false;
    bool    pad_on_[kKeyCount];
    bool    stolen_pad_[kKeyCount]; // steal suppresses the gate
    // Split rim set: combined press-ordered held keys (max 2) with the
    // owning sources per key, the combined last-selected fallback and
    // each source's own last selection (the CC 1 fallback).
    int     rim_held_[2] = {-1, -1};
    int     rim_held_n_  = 0;
    uint8_t rim_held_src_[kKeyCount];
    int     rim_latch_     = -1;
    int     rim_latch_src_ = -1;
    int     src_rim_latch_[kSourceCount];
};

} // namespace bowls
