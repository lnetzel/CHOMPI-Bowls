#pragma once

#include <cmath>

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

inline float DecayMultiplierFromTicks(int ticks)
{
    if(ticks < 0)
        ticks = 0;
    if(ticks > 32)
        ticks = 32;
    return 0.25f * powf(12.0f, static_cast<float>(ticks) * (1.0f / 32.0f));
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