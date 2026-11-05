#pragma once

// Hardware-independent play-mode state machine for CHOMPI-BOWLS
// (plans/improve-play-modes.md). Owns the performance mode, the
// SHIFT + lower-black-key mode shortcuts, per-key press provenance
// (which mode a physically held key was pressed in, so a key-up after
// a mode switch still releases the right note) and the Rimming LOOP
// latch. No audio, no hardware: chompi_main.cpp feeds debounced
// key/LOOP events in and executes the returned KeyAction on the voice
// pool; pad gate-offs are reconciled from PadOn() every block, so a
// single event can never lose a release.
//
// Latch rules (Rimming only):
//   - latch off: key-down opens the pad gate, key-up releases it
//   - LOOP on captures the currently held, sounding pad notes; a
//     later key-down toggles that key's note on (latched) or off
//     (released immediately, even while the key is physically down);
//     key-up alone never releases a latched note
//   - LOOP off clears all latch flags; latched-only notes enter the
//     10 s release, physically held notes keep their gate to key-up
//   - leaving Rimming performs the same cleanup; a stolen latched or
//     held note is unlatched and never restarts by itself
// Latch, held-key and pad state are transient - never persisted.

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
        for(int k = 0; k < kKeyCount; ++k)
        {
            down_mode_[k] = -1;
            latched_[k]   = false;
            pad_on_[k]    = false;
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

    /** Key's pad note is latched on (drives the latched-key LED). */
    bool Latched(int key) const
    {
        return key >= 0 && key < kKeyCount ? latched_[key] : false;
    }

    /** Desired pad gate for a Rimming key; chompi_main reconciles the
     *  voice gates against this after every event, so latch-off, mode
     *  exit and voice steals all close gates reliably. */
    bool PadOn(int key) const
    {
        return key >= 0 && key < kKeyCount ? pad_on_[key] : false;
    }

    /** The mode a physically held key was pressed in, -1 when not held. */
    int DownMode(int key) const
    {
        return key >= 0 && key < kKeyCount ? down_mode_[key] : -1;
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
     *  never creates a strike or revives a stolen full-mode note). */
    bool WheelTargets(int key) const
    {
        if(key < 0 || key >= kKeyCount)
            return false;
        switch(mode_)
        {
            case PlayMode::kSplit: return InRubSet(key);
            case PlayMode::kStrikes:
                return down_mode_[key] == StrikeIndex;
            case PlayMode::kRimming:
                return pad_on_[key]
                       && (down_mode_[key] == RimmingIndex
                           || latched_[key]);
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
        down_mode_[key] = static_cast<int8_t>(mode_);
        a.mode          = mode_;
        a.key           = key;
        switch(mode_)
        {
            case PlayMode::kStrikes: a.kind = KeyAction::kStrike; break;
            case PlayMode::kRimming:
                if(latch_enabled_)
                {
                    // Key-down TOGGLES the note: a second press on a
                    // sounding note starts its release immediately,
                    // even though the key is physically down.
                    if(pad_on_[key])
                    {
                        pad_on_[key]  = false;
                        latched_[key] = false;
                        a.kind = KeyAction::kNone; // gate-off via PadOn()
                    }
                    else
                    {
                        pad_on_[key]  = true;
                        latched_[key] = true;
                        a.kind        = KeyAction::kPadOn;
                    }
                }
                else
                {
                    pad_on_[key] = true;
                    a.kind       = KeyAction::kPadOn;
                }
                break;
            case PlayMode::kSplit:
                if(key <= kRimKeyLast)
                {
                    // Press-ordered held set, max 2 (two-bowl rimming):
                    // a third held rim key drops the oldest from the
                    // rub set - its ring decays naturally.
                    if(rim_held_n_ < 2)
                        rim_held_[rim_held_n_++] = key;
                    else
                    {
                        rim_held_[0] = rim_held_[1];
                        rim_held_[1] = key;
                    }
                    rim_latch_ = key;
                    a.kind     = KeyAction::kRimSelect;
                }
                else
                {
                    a.kind = KeyAction::kStrike; // keys 12..24
                }
                break;
        }
        return a;
    }

    /** Key released. */
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
        const int dm   = down_mode_[key];
        down_mode_[key] = -1;
        if(dm < 0)
            return a;
        if(dm == RimmingIndex)
        {
            // Key-up alone never releases a latched note.
            if(latched_[key])
                return a;
            pad_on_[key] = false; // gate-off via PadOn() reconciliation
        }
        else if(dm == SplitIndex && key <= kRimKeyLast)
        {
            for(int i = 0; i < rim_held_n_; ++i)
                if(rim_held_[i] == key)
                {
                    rim_held_[i] = rim_held_[rim_held_n_ - 1];
                    --rim_held_n_;
                    break;
                }
        }
        return a; // strikes are one-shot: nothing to release
    }

    /** LOOP toggles the Rimming note latch. Reserved (inert) in the
     *  other modes. */
    void LoopPressed()
    {
        if(mode_ != PlayMode::kRimming)
            return;
        if(!latch_enabled_)
        {
            // Capture the currently held, sounding pad notes (a pad
            // whose voice was stolen has pad_on_ cleared and is not
            // captured).
            latch_enabled_ = true;
            for(int k = 0; k < kKeyCount; ++k)
                if(pad_on_[k] && down_mode_[k] == RimmingIndex)
                    latched_[k] = true;
        }
        else
        {
            latch_enabled_ = false;
            for(int k = 0; k < kKeyCount; ++k)
            {
                latched_[k] = false;
                // Latched-only notes enter the 10 s release; notes
                // still physically held keep their gate until key-up.
                if(pad_on_[k] && down_mode_[k] != RimmingIndex)
                    pad_on_[k] = false;
            }
        }
    }

    /** The voice holding (mode, key) was stolen by a newer note: clear
     *  that note's latch/gate. LOOP itself stays enabled; a stolen held
     *  note is not restarted - it must be released and played again. */
    void OnVoiceStolen(PlayMode mode, int key)
    {
        if(mode == PlayMode::kRimming && key >= 0 && key < kKeyCount)
        {
            latched_[key] = false;
            pad_on_[key]  = false;
        }
    }

  private:
    static constexpr int8_t StrikeIndex
        = static_cast<int8_t>(PlayMode::kStrikes);
    static constexpr int8_t RimmingIndex
        = static_cast<int8_t>(PlayMode::kRimming);
    static constexpr int8_t SplitIndex
        = static_cast<int8_t>(PlayMode::kSplit);

    /** Leaving a mode: Rimming performs the latch cleanup (LOOP off,
     *  latched-only notes release; physically held pads keep their
     *  gate until their own key-up). */
    void LeaveMode()
    {
        if(mode_ != PlayMode::kRimming)
            return;
        latch_enabled_ = false;
        for(int k = 0; k < kKeyCount; ++k)
        {
            latched_[k] = false;
            if(pad_on_[k] && down_mode_[k] != RimmingIndex)
                pad_on_[k] = false;
        }
    }

    PlayMode mode_         = PlayMode::kStrikes;
    int      shortcut_key_ = -1;
    int8_t   down_mode_[kKeyCount]; // -1, or the mode the press began in
    bool     latch_enabled_ = false;
    bool     latched_[kKeyCount];
    bool     pad_on_[kKeyCount];
    int      rim_held_[2] = {-1, -1};
    int      rim_held_n_  = 0;
    int      rim_latch_   = -1;
};

} // namespace bowls
