#include "StereoBbdDelay.h"
#include "delay_parameters.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using chompi::StereoBbdDelay;
constexpr float kSampleRate = 48000.f;

struct Rig
{
    StereoBbdDelay delay;
    std::vector<float> left;
    std::vector<float> right;

    Rig()
        : left(StereoBbdDelay::kBufferSize, 0.f),
          right(StereoBbdDelay::kBufferSize, 0.f)
    {
        delay.Init(kSampleRate, left.data(), right.data());
    }

    void Run(int frames, float in_l = 0.f, float in_r = 0.f)
    {
        for(int i = 0; i < frames; ++i)
        {
            float out_l, out_r;
            delay.Process(in_l, in_r, out_l, out_r);
        }
    }
};

int main()
{
    using namespace chompi::delay_parameters;
    assert(DelayCombinedPercent(0) == 0);
    assert(DelayCombinedPercent(16) == 50);
    assert(DelayCombinedPercent(32) == 100);
    assert(DelayMix(0) == 0.f && DelayFeedback(0) == .35f);
    assert(DelayMix(16) == .5f && DelayFeedback(16) == .35f);
    assert(DelayMix(32) == .5f && DelayFeedback(32) == 1.2f);
    assert(DelayTimeMs(0) == 40 && DelayTimeMs(15) == 500
           && DelayTimeMs(32) == 1000);
    float out_l, out_r;

    Rig dry;
    for(int i = 0; i < 5000; ++i)
    {
        const float in_l = std::sin(i * .17f) * .5f;
        const float in_r = std::cos(i * .13f) * .4f;
        dry.delay.Process(in_l, in_r, out_l, out_r);
        assert(out_l == in_l && out_r == in_r);
    }
    assert(dry.delay.GetMix() == 0.f);
    assert(dry.delay.GetFeedback() == .35f);
    assert(dry.delay.GetDelayMs() == 500.f);

    dry.delay.SetDelayMs(1.f);
    assert(dry.delay.GetDelayMs() == 40.f);
    dry.delay.SetDelayMs(900.f);
    assert(dry.delay.GetDelayMs() == 900.f);
    dry.delay.SetDelayMs(1500.f);
    assert(dry.delay.GetDelayMs() == 1000.f);
    dry.delay.SetFeedback(2.f);
    assert(dry.delay.GetFeedback() == 1.2f);

    Rig wet;
    wet.delay.SetMix(1.f);
    wet.Run(600);
    wet.delay.SetDelayMs(100.f);
    wet.Run(48000);
    wet.delay.Process(1.f, 1.f, out_l, out_r);
    assert(out_l == 0.f && out_r == 0.f);
    for(int i = 0; i < 4600; ++i)
    {
        wet.delay.Process(0.f, 0.f, out_l, out_r);
        assert(out_l == 0.f && out_r == 0.f);
    }
    float wet_peak = 0.f;
    for(int i = 0; i < 3000; ++i)
    {
        wet.delay.Process(0.f, 0.f, out_l, out_r);
        wet_peak = std::fmax(wet_peak, std::fabs(out_l));
    }
    assert(wet_peak > .1f);

    Rig ramp;
    ramp.delay.SetMix(1.f);
    for(int i = 0; i < 600; ++i)
        ramp.delay.Process(.5f, .5f, out_l, out_r);
    assert(out_l == 0.f && out_r == 0.f);

    Rig echo;
    echo.delay.SetMix(1.f);
    echo.Run(600);
    echo.delay.SetDelayMs(100.f);
    echo.Run(48000);
    echo.delay.Process(1.f, 0.f, out_l, out_r);
    float peak_l = 0.f;
    float peak_r = 0.f;
    int peak_sample = -1;
    for(int i = 0; i < 8000; ++i)
    {
        echo.delay.Process(0.f, 0.f, out_l, out_r);
        if(std::fabs(out_l) > peak_l)
        {
            peak_l = std::fabs(out_l);
            peak_sample = i;
        }
        if(std::fabs(out_r) > peak_r)
            peak_r = std::fabs(out_r);
    }
    assert(peak_l > .1f);
    assert(peak_sample > 4700 && peak_sample < 5100);
    assert(peak_r < peak_l * .01f);

    Rig history;
    history.delay.SetDelayMs(100.f);
    history.Run(48000);
    history.delay.Process(1.f, 1.f, out_l, out_r);
    assert(out_l == 1.f && out_r == 1.f);
    history.Run(1440);
    history.delay.SetMix(1.f);
    float history_peak = 0.f;
    for(int i = 0; i < 12000; ++i)
    {
        history.delay.Process(0.f, 0.f, out_l, out_r);
        history_peak = std::fmax(history_peak, std::fabs(out_l));
    }
    assert(history_peak > .1f);

    Rig bounded;
    bounded.delay.SetMix(1.f);
    bounded.delay.SetFeedback(1.2f);
    bounded.Run(48000);
    float max_output = 0.f;
    for(int i = 0; i < 240000; ++i)
    {
        const float in = i == 0 ? 1.f : 0.f;
        bounded.delay.Process(in, in, out_l, out_r);
        assert(std::isfinite(out_l) && std::isfinite(out_r));
        max_output = std::fmax(max_output, std::fabs(out_l));
    }
    assert(max_output < .7f);

    const auto MeasureDelayedRms = [](float frequency) {
        Rig rig;
        rig.delay.SetMix(1.f);
        rig.delay.SetFeedback(0.f);
        rig.delay.SetDelayMs(100.f);
        float sum_sq = 0.f;
        const int measure_start = static_cast<int>(2.f * kSampleRate);
        const int frames = measure_start + static_cast<int>(kSampleRate / 10.f);
        float out_l, out_r;
        for(int i = 0; i < frames; ++i)
        {
            const float in = .5f * std::sin(
                6.283185307179586f * frequency * i / kSampleRate);
            rig.delay.Process(in, in, out_l, out_r);
            if(i >= measure_start)
                sum_sq += out_l * out_l;
        }
        return std::sqrt(sum_sq / (frames - measure_start));
    };
    const float low_frequency_rms = MeasureDelayedRms(20.f);
    const float high_frequency_rms = MeasureDelayedRms(500.f);
    assert(low_frequency_rms < high_frequency_rms * .3f);

    std::puts("stereo_bbd_delay_test: PASS");
    return 0;
}