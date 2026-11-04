// Generates .baseline/baseline.raw: the reference render of the fixed
// script below, compiled against the PRE-CHANGE snapshot of
// bowl_voice.h / bowl_profile.h in .baseline/ (copied before the
// direction-selected rim texture was implemented). Run `make baseline`
// once; render_test.cpp T0 replays the identical script against the
// current header and requires bit-identical output.
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
    v.Strike(220.0f, 0.7f, 1.0f, 1.0f);

    // 0.5 s struck, then 1.5 s struck + textured rub (level 0.8).
    std::vector<float> mono;
    float              l[kBlk], r[kBlk];
    for(int phase = 0; phase < 2; ++phase)
    {
        if(phase == 1)
            v.SetRub(0.8f);
        const int blocks = phase == 0 ? 1000 : 3000;
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
