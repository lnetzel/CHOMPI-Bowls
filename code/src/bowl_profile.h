#pragma once

// Bowl profile data for CHOMPI-BOWLS.
//
// A BowlProfile is pure data - modal frequency ratios, paired-mode detune
// offsets, per-mode T60 decay times and excitation weights - so new bowl
// models never touch the DSP in bowl_voice.h.
//
// Profile 0 follows the measured 180 mm bowl ratios from the 2006
// acoustic study cited in the spec (1.0 / 2.8 / 5.2 / 8.1 / 11.6 / 15.6 /
// 19.9, plus one extrapolated mode at 24.5). The other profiles are
// starting design variations, meant to be tuned by ear.
//
// detune_hz is specified at a 220 Hz reference fundamental and scales
// linearly with the played fundamental (keeps the beat rate proportional
// to pitch). t60_s is the -60 dB decay time per mode.

#include <cstddef>

namespace bowls
{
constexpr int kNumModes    = 8; // mode pairs per voice
constexpr int kNumProfiles = 5;

struct BowlProfile
{
    const char* name;
    float       ratio[kNumModes];     // modal frequency ratios
    float       detune_hz[kNumModes]; // pair offset at 220 Hz: B = f + detune
    float       t60_s[kNumModes];     // per-mode decay time (seconds)
    float       excite[kNumModes];    // per-mode excitation weight 0..1
};

inline const BowlProfile& GetBowlProfile(int index)
{
    static const BowlProfile kProfiles[kNumProfiles] = {
        {
            // 0: Temple - the measured 180 mm reference bowl
            "Temple",
            {1.00f, 2.80f, 5.20f, 8.10f, 11.60f, 15.60f, 19.90f, 24.50f},
            {0.8f, 1.4f, 2.4f, 3.2f, 4.0f, 5.0f, 6.0f, 7.0f},
            {12.0f, 9.0f, 7.0f, 5.5f, 4.0f, 3.0f, 2.5f, 2.0f},
            {1.00f, 0.55f, 0.50f, 0.40f, 0.30f, 0.22f, 0.16f, 0.12f},
        },
        {
            // 1: Deep - large bowl: tighter ratios, slow beating, long ring
            "Deep",
            {1.00f, 2.72f, 5.05f, 7.90f, 11.30f, 15.10f, 19.30f, 23.80f},
            {0.5f, 0.8f, 1.4f, 1.9f, 2.4f, 3.0f, 3.6f, 4.2f},
            {15.0f, 11.5f, 9.0f, 7.0f, 5.0f, 3.8f, 3.1f, 2.5f},
            {1.00f, 0.50f, 0.42f, 0.30f, 0.20f, 0.14f, 0.10f, 0.07f},
        },
        {
            // 2: Bright - small bowl: wider ratios, quick decay, strong highs
            "Bright",
            {1.00f, 2.95f, 5.45f, 8.40f, 12.00f, 16.10f, 20.50f, 25.20f},
            {1.0f, 1.7f, 2.9f, 3.8f, 4.8f, 6.0f, 7.2f, 8.4f},
            {7.0f, 5.5f, 4.2f, 3.3f, 2.4f, 1.8f, 1.5f, 1.2f},
            {1.00f, 0.65f, 0.60f, 0.55f, 0.45f, 0.35f, 0.28f, 0.22f},
        },
        {
            // 3: Wide - pronounced paired-mode beating at every mode
            "Wide",
            {1.00f, 2.80f, 5.20f, 8.10f, 11.60f, 15.60f, 19.90f, 24.50f},
            {2.0f, 3.5f, 6.0f, 8.0f, 10.0f, 12.5f, 15.0f, 17.5f},
            {12.0f, 9.0f, 7.0f, 5.5f, 4.0f, 3.0f, 2.5f, 2.0f},
            {1.00f, 0.55f, 0.50f, 0.40f, 0.30f, 0.22f, 0.16f, 0.12f},
        },
        {
            // 4: Thin - small high bowl, short and brilliant
            "Thin",
            {1.00f, 2.88f, 5.32f, 8.25f, 11.80f, 15.85f, 20.20f, 24.90f},
            {1.2f, 2.1f, 3.6f, 4.8f, 6.0f, 7.5f, 9.0f, 10.5f},
            {5.5f, 4.2f, 3.2f, 2.5f, 1.8f, 1.4f, 1.1f, 0.9f},
            {0.90f, 0.70f, 0.65f, 0.60f, 0.50f, 0.40f, 0.32f, 0.25f},
        },
    };
    if(index < 0)
        index = 0;
    if(index >= kNumProfiles)
        index = kNumProfiles - 1;
    return kProfiles[index];
}

} // namespace bowls
