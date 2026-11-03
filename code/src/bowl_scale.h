#pragma once

// Bowl scale and keyboard split for CHOMPI-BOWLS (plan:
// plans/split-strike-and-rimming-plan.md). The 25-key bed is split in
// two zones over the SAME 12 bowls:
//
//   keys 0-11  (C3-B3): rim zone - pressing a key SELECTS the bowl for
//                       transport-wheel rimming; it never strikes
//   keys 12-23 (C4-B4): strike zone - the same 12 bowls, struck
//   key  24    (C5):    unassigned
//
// Black keys are playable in both zones. The three highest bowls of
// the former 15-bowl scale are dropped for now (kept in the comment
// below).
//
// Anchored on measured fundamentals from the studies cited in the spec:
// 99 / 219.6 / 310.2 / 465 / 513 Hz (and 927 Hz). The remaining entries
// are octave siblings and design-choice intermediates - starting points,
// to be tuned by ear. Ratios are deliberately unequal.

namespace bowls
{
constexpr int kBowlCount = 12; // one bowl per key in each zone

// Bowls 1-12, lowest to highest. Strictly ascending.
// Dropped for now (the former scale's top three):
// 1240.8 (310.2 x 4), 1395.0 (465 x 3), 1560.0 (390 x 4).
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

// Zone boundaries (keyboard pitch-order index 0 = C3 .. 24 = C5, the
// order of kKeySwitches / kKeyLeds).
constexpr int kRimKeyLast     = 11; // rim zone: keys 0..11
constexpr int kStrikeKeyFirst = 12; // strike zone: keys 12..23
constexpr int kStrikeKeyLast  = 23; // key 24 (C5) is unassigned

// Keyboard pitch-order index -> bowl index (0..11), -1 = unassigned.
constexpr int kBowlForKey[25] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, // rim zone (C3-B3)
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, // strike zone (C4-B4)
    -1,                                   // C5: unassigned
};

} // namespace bowls
