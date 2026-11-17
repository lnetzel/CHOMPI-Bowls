/** @file MidiCcMap.h
 *  @brief Note/CC number map and value conversions for the CHOMPI-BOWLS
 *  MIDI input (see midi.md for the full protocol reference).
 *
 *  Header-only with no Daisy dependencies, so the host tests can include
 *  it (same convention as BowlsPreset.h / play_modes.h). MIDI uses
 *  fixed channel 1, velocity-0 Note On as Note Off, and a 33-position
 *  tick conversion.
 *  `t = (v * 32 + 63) / 127` for every saved rotary parameter.
 *
 *  Every preset-saved rotary parameter has a CC. All CCs
 *  are absolute setters (not encoder increments) and set their values
 *  silently - incoming CCs never trigger the white-key parameter bars.
 */
#pragma once
#include <cstdint>

namespace bowls
{
namespace MidiCcMap
{
    /* Fixed MIDI channel 1 (libDaisy's zero-based channel index 0). */
    constexpr uint8_t kChannel = 0;

    /* --- note map ------------------------------------------------------------
     * MIDI notes 48..72 (C3..C5, C4 = 60) map to local key indices 0..24.
     * All other notes are ignored: no wrap, transpose or clamp-to-edge. */
    constexpr int kNoteBase = 48; /* C3 -> key 0 */
    constexpr int kNoteLast = 72; /* C5 -> key 24 */

    /** Key index 0..24 for a MIDI note, or -1 when out of range. */
    inline int NoteToKey(uint8_t note)
    {
        return note >= kNoteBase && note <= kNoteLast ? note - kNoteBase
                                                      : -1;
    }

    /* --- CC numbers ---------------------------------------------------------- */
    constexpr uint8_t kCcRimLevel     = 1;  /* per-source MIDI rub level v/127 */
    constexpr uint8_t kCcMasterVolume = 7;  /* global; ticks; gain (t/32)^2 */
    constexpr uint8_t kCcPan          = 10; /* selected bank/key; ticks 16 = centre */
    constexpr uint8_t kCcSustain      = 64; /* per source; 0-63 off, 64-127 on */
    constexpr uint8_t kCcProfile      = 70; /* selected bank/key; ranges below */
    constexpr uint8_t kCcBrightness   = 74; /* selected bank/key; ticks */
    constexpr uint8_t kCcDecay        = 75; /* selected bank/key; ticks */
    constexpr uint8_t kCcBowlVolume   = 77; /* selected bank/key; ticks 16 = unity */
    constexpr uint8_t kCcStrike       = 78; /* global strike hardness; ticks */
    constexpr uint8_t kCcShimmer      = 79; /* global shimmer; ticks */
    constexpr uint8_t kCcMode         = 80; /* 0-42 Strikes, 43-85 Rimming, 86-127 Split */
    constexpr uint8_t kCcEditBank     = 81; /* exact 0/1/2 = bank, 127 = follow mode */
    constexpr uint8_t kCcEditKey      = 82; /* exact note number 48-72 = key 0-24 */
    constexpr uint8_t kCcLoop         = 83; /* Rimming LOOP latch; 0-63 off, 64-127 on */
    constexpr uint8_t kCcRimTexture   = 84; /* per source; 0-63 friction, 64-127 clean */
    constexpr uint8_t kCcRouting      = 85; /* 0-63 Normal, 64-127 Send FX */
    constexpr uint8_t kCcReverbMix    = 91; /* global temple-room mix; ticks */
    constexpr uint8_t kCcAllSoundOff  = 120; /* any value: global panic */
    constexpr uint8_t kCcResetControllers = 121; /* any value: sender reset */
    constexpr uint8_t kCcAllNotesOff  = 123; /* any value: sender notes off */

    /** Is `cc` one of the supported controllers? Unsupported CCs are
     *  ignored (and never queued), so an NRPN/bank-select flood cannot
     *  fill the event queues. */
    inline bool IsSupportedCc(uint8_t cc)
    {
        switch(cc)
        {
            case kCcRimLevel:
            case kCcMasterVolume:
            case kCcPan:
            case kCcSustain:
            case kCcProfile:
            case kCcBrightness:
            case kCcDecay:
            case kCcBowlVolume:
            case kCcStrike:
            case kCcShimmer:
            case kCcMode:
            case kCcEditBank:
            case kCcEditKey:
            case kCcLoop:
            case kCcRimTexture:
            case kCcRouting:
            case kCcReverbMix:
            case kCcAllSoundOff:
            case kCcResetControllers:
            case kCcAllNotesOff: return true;
            default: return false;
        }
    }

    /* --- conversions ----------------------------------------------------------

     * CC value -> panel ticks 0..32 (the shared 33-position grid):
     * values 0 / 32 / 64 / 95 / 127 select ticks 0 / 8 / 16 / 24 / 32.
     */
    inline int CcToTicks(uint8_t v)
    {
        return (static_cast<int>(v) * 32 + 63) / 127;
    }

    /** CC 1 rimming level: held absolute level 0..1 (no tick grid). */
    inline float CcToLevel(uint8_t v)
    {
        return static_cast<float>(v) * (1.0f / 127.0f);
    }

    /** Standard switch convention: 0-63 off, 64-127 on. */
    inline bool CcToSwitch(uint8_t v) { return v >= 64; }

    /** CC 70 resonance character -> stored profile 0..4 (panel profile
     *  1..5). Bypasses the encoder's two-detent accumulator. */
    inline int CcToProfile(uint8_t v)
    {
        const int p = (static_cast<int>(v) * 5) / 128;
        return p > 4 ? 4 : p;
    }

    /** CC 80 active play mode -> PlayMode index 0..2. */
    inline int CcToMode(uint8_t v)
    {
        return v < 43 ? 0 : (v < 86 ? 1 : 2);
    }

    /* CC 81 edit-bank selector results. */
    constexpr int kEditIgnore = -1; /* values 3..126: no-op */
    constexpr int kEditFollow = -2; /* value 127: follow the active mode */

    /** CC 81 parameter edit bank: exact 0/1/2 selects a fixed bank
     *  (Strikes/Rimming/Split), exact 127 restores follow-active-mode,
     *  all other values are ignored. Selector only: never changes the
     *  play mode and never starts a voice. */
    inline int CcToEditBank(uint8_t v)
    {
        if(v <= 2)
            return static_cast<int>(v);
        if(v == 127)
            return kEditFollow;
        return kEditIgnore;
    }

    /** CC 82 parameter edit key: the exact MIDI note numbers 48-72
     *  select keys 0-24; all other values are ignored (-1). Selector
     *  only: never starts a voice. */
    inline int CcToEditKey(uint8_t v) { return NoteToKey(v); }

} /* namespace MidiCcMap */
} /* namespace bowls */
