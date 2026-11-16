#pragma once

namespace chompi
{
namespace delay_parameters
{

constexpr int kMaxPosition = 32;

inline int ClampPosition(int position)
{
    return position < 0 ? 0 : (position > kMaxPosition ? kMaxPosition : position);
}

inline int ScaledInteger(int position, int maximum)
{
    return (ClampPosition(position) * maximum + kMaxPosition / 2)
           / kMaxPosition;
}

inline int DelayCombinedPercent(int position)
{
    return ScaledInteger(position, 100);
}

inline float DelayMix(int position)
{
    const int percent = DelayCombinedPercent(position);
    return percent <= 50 ? static_cast<float>(percent) * .01f : .5f;
}

inline float DelayFeedback(int position)
{
    const int percent = DelayCombinedPercent(position);
    return percent <= 50
               ? .35f
               : .35f + static_cast<float>(percent - 50) * (.85f / 50.f);
}

inline int DelayTimeMs(int position)
{
    const int clamped = ClampPosition(position);
    if(clamped <= 15)
        return 40 + (clamped * 460 + 7) / 15;
    return 500 + ((clamped - 15) * 500 + 8) / 17;
}

/** Clock-pulse divider matching the time control at a 120 BPM reference. */
inline int DelayClockDividerTicks(int position)
{
    return (DelayTimeMs(position) * 24 + 250) / 500;
}

} // namespace delay_parameters
} // namespace chompi