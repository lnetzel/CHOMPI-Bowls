#include "bowl_reverb.h"

#include <cmath>
#include <cstdio>

namespace
{
constexpr float kSampleRate = 48000.0f;
constexpr int kRenderSamples = 60000;
constexpr int kMeasureStart = 36000;
constexpr int kMeasureEnd = kRenderSamples;

bowls::BowlReverb room;
daisysp::ReverbSc reference;
int failures = 0;

void Check(bool ok, const char* name)
{
    std::printf("%-42s %s\n", name, ok ? "PASS" : "FAIL");
    if(!ok)
        ++failures;
}

float ReferenceFeedback()
{
    const float mean_delay_seconds = 3015.5f / kSampleRate;
    return powf(0.001f,
                mean_delay_seconds
                    / bowls::BowlReverb::DecaySecondsForTicks(4));
}

float RoomToneRms(float frequency)
{
    if(room.Init(kSampleRate) != 0)
        return 0.0f;
    room.SetDecayTicks(4);
    double energy = 0.0;
    int count = 0;
    for(int i = 0; i < kRenderSamples; ++i)
    {
        const float input = sinf(6.28318530718f * frequency
                                 * static_cast<float>(i) / kSampleRate);
        float left = 0.0f;
        float right = 0.0f;
        if(room.Process(input, input, &left, &right) != 0)
            return 0.0f;
        if(i >= kMeasureStart && i < kMeasureEnd)
        {
            energy += static_cast<double>(left) * left
                      + static_cast<double>(right) * right;
            ++count;
        }
    }
    return count == 0 ? 0.0f
                      : sqrtf(static_cast<float>(energy / (2 * count)));
}

float ReferenceToneRms(float frequency)
{
    if(reference.Init(kSampleRate) != 0)
        return 0.0f;
    const float high_cut_hz = 20000.0f;
    reference.SetLpFreq(high_cut_hz);
    const float feedback = ReferenceFeedback();
    reference.SetFeedback(feedback);
    double energy = 0.0;
    int count = 0;
    for(int i = 0; i < kRenderSamples; ++i)
    {
        const float input = sinf(6.28318530718f * frequency
                                 * static_cast<float>(i) / kSampleRate);
        float left = 0.0f;
        float right = 0.0f;
        if(reference.Process(input, input, &left, &right) != 0)
            return 0.0f;
        if(i >= kMeasureStart && i < kMeasureEnd)
        {
            energy += static_cast<double>(left) * left
                      + static_cast<double>(right) * right;
            ++count;
        }
    }
    return count == 0 ? 0.0f
                      : sqrtf(static_cast<float>(energy / (2 * count)));
}

int FirstRoomOutput()
{
    if(room.Init(kSampleRate) != 0)
        return -1;
    for(int i = 0; i < 6000; ++i)
    {
        const float input = i == 0 ? 1.0f : 0.0f;
        float left = 0.0f;
        float right = 0.0f;
        if(room.Process(input, input, &left, &right) != 0)
            return -1;
        if(std::fabs(left) > 1.0e-9f || std::fabs(right) > 1.0e-9f)
            return i;
    }
    return -1;
}

int FirstReferenceOutput()
{
    if(reference.Init(kSampleRate) != 0)
        return -1;
    for(int i = 0; i < 6000; ++i)
    {
        const float input = i == 0 ? 1.0f : 0.0f;
        float left = 0.0f;
        float right = 0.0f;
        if(reference.Process(input, input, &left, &right) != 0)
            return -1;
        if(std::fabs(left) > 1.0e-9f || std::fabs(right) > 1.0e-9f)
            return i;
    }
    return -1;
}

float TailRms(int ticks)
{
    if(room.Init(kSampleRate) != 0)
        return 0.0f;
    room.SetDecayTicks(ticks);
    double energy = 0.0;
    int count = 0;
    for(int i = 0; i < 120000; ++i)
    {
        const float input = i == 0 ? 1.0f : 0.0f;
        float left = 0.0f;
        float right = 0.0f;
        if(room.Process(input, input, &left, &right) != 0)
            return 0.0f;
        if(i >= 96000)
        {
            energy += static_cast<double>(left) * left
                      + static_cast<double>(right) * right;
            ++count;
        }
    }
    return count == 0 ? 0.0f
                      : sqrtf(static_cast<float>(energy / (2 * count)));
}
} // namespace

int main()
{
    Check(bowls::BowlReverb::DecaySecondsForTicks(0) == 1.0f
              && bowls::BowlReverb::DecaySecondsForTicks(32) == 20.0f
              && std::fabs(
                     bowls::BowlReverb::DecaySecondsForTicks(4) - 3.375f)
                     < 0.001f,
          "decay spans 1-20 seconds");

    const int room_start = FirstRoomOutput();
    const int reference_start = FirstReferenceOutput();
    const int added_delay = room_start - reference_start;
    Check(room_start >= 0 && reference_start >= 0 && added_delay >= 950
              && added_delay <= 1100,
          "room input adds a 20 ms pre-delay");

    const float low_room = RoomToneRms(220.0f);
    const float low_reference = ReferenceToneRms(220.0f);
    const float mid_room = RoomToneRms(1500.0f);
    const float mid_reference = ReferenceToneRms(1500.0f);
    const float low_cut_ratio = low_room / low_reference;
    const float mid_cut_ratio = mid_room / mid_reference;
    Check(low_reference > 0.0f && mid_reference > 0.0f
              && low_cut_ratio < mid_cut_ratio * 0.25f,
          "780 Hz low cut attenuates low bowl tones");

    const float mid_high_room = RoomToneRms(4000.0f);
    const float mid_high_reference = ReferenceToneRms(4000.0f);
    const float high_room = RoomToneRms(12000.0f);
    const float high_reference = ReferenceToneRms(12000.0f);
    const float mid_high_ratio = mid_high_room / mid_high_reference;
    const float high_ratio = high_room / high_reference;
    Check(mid_high_reference > 0.0f && high_reference > 0.0f
              && high_ratio < mid_high_ratio * 0.85f,
          "8 kHz damping attenuates high frequencies");

    const float short_tail = TailRms(0);
    const float long_tail = TailRms(32);
    Check(long_tail > short_tail * 8.0f,
          "20 second decay sustains longer than 1 second");

    return failures == 0 ? 0 : 1;
}
