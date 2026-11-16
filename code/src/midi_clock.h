#pragma once

#include <cstdint>

namespace bowls
{
constexpr uint32_t kMidiClockPulsesPerQuarter = 24;
constexpr uint32_t kMidiClockTimeoutMs = 500;

/** Audio-side tempo estimate from a main-loop MIDI clock pulse counter. */
class MidiClockTracker
{
  public:
    bool Update(uint32_t pulse_count, uint32_t now_ms)
    {
        if(!initialized_)
        {
            initialized_ = true;
            pulse_count_ = pulse_count;
            last_pulse_at_ = now_ms;
            return false;
        }
        if(pulse_count == pulse_count_)
            return false;

        const uint32_t elapsed = now_ms - last_pulse_at_;
        const uint32_t pulses = pulse_count - pulse_count_;
        if(elapsed > kMidiClockTimeoutMs)
            has_period_ = false;
        else if(elapsed > 0 && pulses > 0)
        {
            const float sample_ms
                = static_cast<float>(elapsed) / static_cast<float>(pulses);
            if(has_period_)
                pulse_period_ms_ += (sample_ms - pulse_period_ms_) * 0.25f;
            else
            {
                pulse_period_ms_ = sample_ms;
                has_period_ = true;
            }
        }
        pulse_count_ = pulse_count;
        last_pulse_at_ = now_ms;
        return true;
    }

    bool HasTempo(uint32_t now_ms) const
    {
        return has_period_
               && now_ms - last_pulse_at_ <= kMidiClockTimeoutMs;
    }

    float PulsePeriodMs() const { return pulse_period_ms_; }

  private:
    uint32_t pulse_count_ = 0;
    uint32_t last_pulse_at_ = 0;
    float pulse_period_ms_ = 0.0f;
    bool initialized_ = false;
    bool has_period_ = false;
};

constexpr int kMinArpeggioClockDivider = 2;
constexpr int kMaxArpeggioClockDivider = 480;

/** Convert the free-running rate to MIDI clock pulses at a 120 BPM
 *  reference tempo (one pulse is 1/24 of a quarter note). */
inline int StrikeArpeggioClockDividerTicks(int rate_tenths_hz)
{
    if(rate_tenths_hz < 1)
        rate_tenths_hz = 1;
    if(rate_tenths_hz > 200)
        rate_tenths_hz = 200;
    int ticks = (480 + rate_tenths_hz / 2) / rate_tenths_hz;
    if(ticks < kMinArpeggioClockDivider)
        ticks = kMinArpeggioClockDivider;
    if(ticks > kMaxArpeggioClockDivider)
        ticks = kMaxArpeggioClockDivider;
    return ticks;
}

/** Positive wheel detents shorten the divider and therefore increase
 *  the arpeggio rate, matching the free-running wheel direction. */
inline int AdjustStrikeArpeggioClockDivider(int ticks, int detents)
{
    int64_t adjusted = static_cast<int64_t>(ticks) - detents;
    if(adjusted < kMinArpeggioClockDivider)
        adjusted = kMinArpeggioClockDivider;
    if(adjusted > kMaxArpeggioClockDivider)
        adjusted = kMaxArpeggioClockDivider;
    return static_cast<int>(adjusted);
}

inline int StrikeArpeggioRateFromClockDivider(int ticks)
{
    if(ticks < kMinArpeggioClockDivider)
        ticks = kMinArpeggioClockDivider;
    if(ticks > kMaxArpeggioClockDivider)
        ticks = kMaxArpeggioClockDivider;
    int rate = (480 + ticks / 2) / ticks;
    if(rate < 1)
        rate = 1;
    if(rate > 200)
        rate = 200;
    return rate;
}

inline uint32_t StrikeArpeggioClockIntervalMs(float pulse_period_ms,
                                               int divider_ticks)
{
    if(divider_ticks < kMinArpeggioClockDivider)
        divider_ticks = kMinArpeggioClockDivider;
    if(divider_ticks > kMaxArpeggioClockDivider)
        divider_ticks = kMaxArpeggioClockDivider;
    const float interval_ms
        = pulse_period_ms * static_cast<float>(divider_ticks);
    return interval_ms < 1.0f ? 1u : static_cast<uint32_t>(interval_ms + 0.5f);
}

} // namespace bowls
