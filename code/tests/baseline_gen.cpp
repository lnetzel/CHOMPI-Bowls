// Generates .baseline/baseline.raw: the reference render of the fixed
// script below, compiled against the ACCEPTED post-rimming snapshot of
// bowl_voice.h / bowl_profile.h in .baseline/. Run `make baseline` once
// after a feature is hardware-accepted; render_test.cpp T0 replays the
// identical script against the current header and requires bit-identical
// output. The script is RUB-ONLY on purpose: it guards both rimming
// characters while the strike model is intentionally changed.
//
// The script must mirror RunBaselineScript() in render_test.cpp EXACTLY
// (same calls, same order, same values - the voice's RNG sequence and
// every float must line up).

#include ".baseline/bowl_voice.h"

#include <cstdio>
#include <vector>

int main()
{
    constexpr float kFs  = 48000.0f;
    constexpr int   kBlk = 24;

    bowls::BowlVoice v;
    v.Init(kFs);
    v.SetProfile(0);
    v.SetBrightness(0.5f);
    v.SelectForRub(220.0f, 1.0f);

    // 1.0 s textured rub, 1.0 s crossfade into clean, 0.5 s stopped
    // (fade + natural ring-down).
    std::vector<float> mono;
    float              l[kBlk], r[kBlk];
    for(int phase = 0; phase < 3; ++phase)
    {
        if(phase == 1)
            v.SetRub(0.8f, 1.0f);
        else if(phase == 2)
            v.SetRub(0.0f, 1.0f);
        else
            v.SetRub(0.8f, 0.0f);
        const int blocks = phase == 0 ? 2000 : (phase == 1 ? 2000 : 1000);
        for(int b = 0; b < blocks; ++b)
        {
            for(int i = 0; i < kBlk; ++i)
            {
                l[i] = 0.0f;
                r[i] = 0.0f;
            }
            v.ProcessBlock(l, r, kBlk);
            for(int i = 0; i < kBlk; ++i)
                mono.push_back(0.5f * (l[i] + r[i]));
        }
    }

    FILE* f = std::fopen(".baseline/baseline.raw", "wb");
    if(!f)
    {
        std::printf("cannot write .baseline/baseline.raw\n");
        return 1;
    }
    std::fwrite(mono.data(), sizeof(float), mono.size(), f);
    std::fclose(f);
    std::printf("wrote .baseline/baseline.raw (%u samples)\n",
                static_cast<unsigned>(mono.size()));
    return 0;
}
