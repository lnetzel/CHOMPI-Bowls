#pragma once

#include <cmath>
#include "bowl_scale.h"

namespace bowls
{
inline float MidiVelocityToGain(int velocity)
{
    if(velocity < 0)
        velocity = 0;
    if(velocity > 127)
        velocity = 127;
    return static_cast<float>(velocity) * (1.0f / 127.0f);
}

inline float MidiVelocityToStrikeHardness(int velocity)
{
    if(velocity < 1)
        velocity = 1;
    if(velocity > 127)
        velocity = 127;
    return static_cast<float>(velocity - 1) * (1.0f / 126.0f);
}

inline float BrightnessFromTicks(int ticks)
{
    if(ticks < 0)
        ticks = 0;
    if(ticks > 32)
        ticks = 32;
    return 1.5f * static_cast<float>(ticks) * (1.0f / 32.0f);
}

inline float DecayMultiplierFromTicks(int ticks)
{
    if(ticks < 0)
        ticks = 0;
    if(ticks > 32)
        ticks = 32;
    return 0.25f * powf(12.0f, static_cast<float>(ticks) * (1.0f / 32.0f));
}

inline float AdjustPitchHz(float pitch_hz, int detents, bool fine)
{
    if(!std::isfinite(pitch_hz))
        pitch_hz = kMinPitchHz;
    if(pitch_hz < kMinPitchHz)
        pitch_hz = kMinPitchHz;
    if(pitch_hz > kMaxPitchHz)
        pitch_hz = kMaxPitchHz;

    while(detents > 0)
    {
        const float step = fine
                               ? 1.0f
                               : fmaxf(1.0f, floorf(pitch_hz * 0.01f + 0.5f));
        pitch_hz = fminf(kMaxPitchHz, pitch_hz + step);
        --detents;
    }
    while(detents < 0)
    {
        const float step = fine
                               ? 1.0f
                               : fmaxf(1.0f, floorf(pitch_hz * 0.01f + 0.5f));
        pitch_hz = fmaxf(kMinPitchHz, pitch_hz - step);
        ++detents;
    }
    return pitch_hz;
}

inline int PitchLedIndex(float pitch_hz, int led_count)
{
    if(led_count <= 1)
        return 0;
    if(!std::isfinite(pitch_hz) || pitch_hz < kMinPitchHz)
        pitch_hz = kMinPitchHz;
    if(pitch_hz > kMaxPitchHz)
        pitch_hz = kMaxPitchHz;
    const float position
        = logf(pitch_hz / kMinPitchHz) / logf(kMaxPitchHz / kMinPitchHz);
    const int index = static_cast<int>(
        floorf(position * static_cast<float>(led_count - 1) + 0.5f));
    return index < 0 ? 0 : (index >= led_count ? led_count - 1 : index);
}

struct StrikeInputs
{
    float hardness;
    float amplitude;
};

inline StrikeInputs GetStrikeInputs(float local_hardness,
                                    int velocity,
                                    bool midi_velocity)
{
    if(midi_velocity)
        return {MidiVelocityToStrikeHardness(velocity),
                MidiVelocityToGain(velocity)};
    return {local_hardness, 0.30f + 0.70f * local_hardness};
}
} // namespace bowls