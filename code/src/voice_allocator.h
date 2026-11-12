#pragma once

namespace bowls
{
template <typename IsActive, typename IsProtected, typename Envelope>
int QuietestUnprotectedVoice(int count,
                             IsActive is_active,
                             IsProtected is_protected,
                             Envelope envelope)
{
    int   quietest_voice = -1;
    float quietest_level = 1.0e30f;
    for(int voice = 0; voice < count; ++voice)
        if(is_active(voice) && !is_protected(voice)
           && envelope(voice) < quietest_level)
        {
            quietest_level = envelope(voice);
            quietest_voice = voice;
        }
    return quietest_voice;
}
} // namespace bowls