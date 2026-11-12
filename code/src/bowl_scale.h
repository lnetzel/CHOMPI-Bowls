#pragma once

// Bowl scale, keyboard layouts and play modes for CHOMPI-BOWLS (plans:
// plans/split-strike-and-rimming-plan.md, plans/improve-play-modes.md).
//
// Three performance modes share the 25-key bed (keyboard pitch-order
// index 0 = C3 .. 24 = C5, the order of kKeySwitches / kKeyLeds):
//
//   Strikes - every key strikes a distinct bowl (25 ascending pitches)
//   Rimming - every key holds a sustained clean bowl pad; the
//             transport wheel adds the direction-dependent rimming
//   Split   - the v1 layout: keys 0-11 (C3-B3) SELECT one of the 12
//             bowls for wheel rimming (never strike), keys 12-23
//             (C4-B4) strike the same 12 bowls, and key 24 (C5)
//             strikes an added higher bowl
//
// Modes are selected with SHIFT + a lower black key: F#3 (index 6) =
// Strikes, G#3 (8) = Rimming, A#3 (10) = Split.
//
// Anchored on measured fundamentals from the studies cited in the spec:
// 99 / 219.6 / 310.2 / 465 / 513 Hz (and 927 Hz). The remaining entries
// are octave siblings and design-choice intermediates - starting points,
// to be tuned by ear. Ratios are deliberately unequal; the full-key
// table interpolates in frequency ratio and is NOT 25 measured bowls.

namespace bowls
{
/** Performance modes (stored in the preset; Strikes is the factory
 *  default for a fresh install or an incompatible old state file). */
enum class PlayMode : int
{
    kStrikes = 0, // all 25 keys strike
    kRimming = 1, // all 25 keys hold sustained clean pads; wheel rims
    kSplit   = 2, // rim 0-11, strike 12-24 (the v1 layout)
};
constexpr int kPlayModeCount = 3;

/** Mode shortcuts: SHIFT + lower black keys (keyboard pitch-order
 *  index), per mode. */
constexpr int kModeShortcutKey[kPlayModeCount] = {
    6,  // kStrikes - lower F# (F#3)
    8,  // kRimming - lower G# (G#3)
    10, // kSplit   - lower A# (A#3)
};

constexpr int kKeyCount = 25; // C3..C5

// The 12 measured/reference bowls, lowest to highest. Strictly
// ascending. (The former 15-bowl scale's top three - 1240.8 =
// 310.2 x 4, 1395.0 = 465 x 3, 1560.0 = 390 x 4 - now live in the
// full-key table; 1240.8 is also Split's added top strike.)
constexpr int kBowlCount = 12;
constexpr float kBowlScaleHz[kBowlCount] = {
    99.0f,   // B1  - measured (23 cm bowl)
    146.0f,  // B2  - design choice
    219.6f,  // B3  - measured
    310.2f,  // B4  - measured
    390.0f,  // B5  - design choice
    465.0f,  // B6  - measured (12 cm bowl)
    513.0f,  // B7  - measured
    620.4f,  // B8  - 310.2 x 2
    693.6f,  // B9  - 86.7 x 8
    780.0f,  // B10 - 390 x 2
    927.0f,  // B11 - measured (8 cm bowl)
    1026.0f, // B12 - 513 x 2
};

// Split zone boundaries. Rim zone: keys 0..11 select bowls for wheel
// rimming; strike zone: keys 12..23 strike the same 12 bowls and key 24
// (C5) strikes kSplitTopStrikeHz.
constexpr int kRimKeyLast     = 11;
constexpr int kStrikeKeyFirst = 12;
constexpr int kStrikeKeyLast  = 24;

/// Split's added highest strike: 310.2 x 4, retained from the former
/// 15-bowl scale.
constexpr float kSplitTopStrikeHz = 1240.8f;

/// The 25 distinct, ascending starting fundamentals of the full-key
/// modes (Strikes and Rimming share one layout), key 0 -> key 24.
/// Measured/reference anchors are preserved (99.0 / 146.0 / 219.6 /
/// 310.2 / 390.0 / 465.0 / 513.0 / 620.4 / 693.6 / 780.0 / 927.0 /
/// 1026.0 / 1240.8 / 1395.0 / 1560.0); the added intermediate pitches
/// are design choices, interpolated in frequency ratio and subject to
/// listening - not measured bowl data.
constexpr float kFullScaleHz[kKeyCount] = {
    99.0f,   // C3  - measured
    112.7f,  // C#3 - interpolated
    128.3f,  // D3  - interpolated
    146.0f,  // D#3 - design choice
    167.3f,  // E3  - interpolated
    191.7f,  // F3  - interpolated
    219.6f,  // F#3 - measured
    261.0f,  // G3  - interpolated
    310.2f,  // G#3 - measured
    347.8f,  // A3  - interpolated
    390.0f,  // A#3 - design choice
    425.9f,  // B3  - interpolated
    465.0f,  // C4  - measured
    513.0f,  // C#4 - measured
    564.1f,  // D4  - interpolated
    620.4f,  // D#4 - 310.2 x 2
    693.6f,  // E4  - 86.7 x 8
    780.0f,  // F4  - 390 x 2
    850.3f,  // F#4 - interpolated
    927.0f,  // G4  - measured
    1026.0f, // G#4 - 513 x 2
    1128.3f, // A4  - interpolated
    1240.8f, // A#4 - 310.2 x 4
    1395.0f, // B4  - 465 x 3
    1560.0f, // C5  - 390 x 4
};

struct PitchTables
{
    float strikes[kKeyCount];
    float rimming[kKeyCount];
    float split[13];

    PitchTables()
    {
        for(int i = 0; i < kKeyCount; ++i)
        {
            strikes[i] = kFullScaleHz[i];
            rimming[i] = kFullScaleHz[i];
        }
        for(int i = 0; i < kBowlCount; ++i)
            split[i] = kBowlScaleHz[i];
        split[12] = kSplitTopStrikeHz;
    }
};

inline const PitchTables& FactoryPitchTables()
{
    static const PitchTables tables;
    return tables;
}

inline float BaseFundamentalHz(PlayMode mode, int key,
                               const PitchTables& tables)
{
    if(key < 0 || key >= kKeyCount)
        return 0.0f;
    if(mode == PlayMode::kStrikes)
        return tables.strikes[key];
    if(mode == PlayMode::kRimming)
        return tables.rimming[key];
    if(mode == PlayMode::kSplit)
    {
        if(key <= kRimKeyLast)
            return tables.split[key];
        if(key < kStrikeKeyLast)
            return tables.split[key - kStrikeKeyFirst];
        return tables.split[12];
    }
    return 0.0f;
}

/// Base (untuned) fundamental for a key in a mode; 0 for out-of-range
/// keys. The per-key tuning control scales this complete modal
/// structure at excitation time.
inline float BaseFundamentalHz(PlayMode mode, int key)
{
    return BaseFundamentalHz(mode, key, FactoryPitchTables());
}

// -- compile-time checks --------------------------------------------------

constexpr bool FullScaleAscending()
{
    for(int i = 1; i < kKeyCount; ++i)
        if(!(kFullScaleHz[i] > kFullScaleHz[i - 1]))
            return false;
    return true;
}

constexpr bool BowlScaleAscending()
{
    for(int i = 1; i < kBowlCount; ++i)
        if(!(kBowlScaleHz[i] > kBowlScaleHz[i - 1]))
            return false;
    return true;
}

static_assert(FullScaleAscending(),
              "kFullScaleHz must be 25 strictly ascending pitches");
static_assert(BowlScaleAscending(),
              "kBowlScaleHz must be strictly ascending");
static_assert(kModeShortcutKey[0] == 6 && kModeShortcutKey[1] == 8
                  && kModeShortcutKey[2] == 10,
              "mode shortcuts: lower F# / G# / A#");

} // namespace bowls
