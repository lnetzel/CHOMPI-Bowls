#pragma once

// Bowl scale for CHOMPI-BOWLS v1: the 15 white keys play a fixed lookup
// table of bowl fundamentals - each key is a particular bowl, not a
// semitone (spec section 3, "Bowl Scale" mode). The black keys are
// unassigned in v1.
//
// Anchored on measured fundamentals from the studies cited in the spec:
// 99 / 219.6 / 310.2 / 465 / 513 Hz (and 927 Hz). The remaining entries
// are octave siblings and design-choice intermediates - starting points,
// to be tuned by ear. Ratios are deliberately unequal.

namespace bowls
{
constexpr int kWhiteKeyCount = 15;

// White keys 1-15, left to right. Strictly ascending.
constexpr float kBowlScaleHz[kWhiteKeyCount] = {
    99.0f,   // W1  - measured (23 cm bowl)
    146.0f,  // W2  - design choice
    219.6f,  // W3  - measured
    310.2f,  // W4  - measured
    390.0f,  // W5  - design choice
    465.0f,  // W6  - measured (12 cm bowl)
    513.0f,  // W7  - measured
    620.4f,  // W8  - 310.2 x 2
    693.6f,  // W9  - 86.7 x 8
    780.0f,  // W10 - 390 x 2
    927.0f,  // W11 - measured (8 cm bowl)
    1026.0f, // W12 - 513 x 2
    1240.8f, // W13 - 310.2 x 4
    1395.0f, // W14 - 465 x 3
    1560.0f, // W15 - 390 x 4
};

// Keyboard pitch-order index (0 = C3 .. 24 = C5, the order of
// kKeySwitches / kKeyLeds) -> white-key ordinal (0..14), -1 = black key.
constexpr int kWhiteOrdinal[25] = {
    0,  -1, 1,  -1, 2,  3,  -1, 4,  -1, 5,  -1, 6,  7,
    -1, 8,  -1, 9,  10, -1, 11, -1, 12, -1, 13, 14,
};

// White-key ordinal -> keyboard pitch-order index (inverse of the above).
constexpr int kWhiteToPitchIndex[kWhiteKeyCount] = {
    0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24,
};

} // namespace bowls
