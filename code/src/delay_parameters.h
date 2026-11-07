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
    return 40 + ScaledInteger(position, 460);
}

} // namespace delay_parameters
} // namespace chompi