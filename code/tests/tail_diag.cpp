// Tail diagnostic: where does the ring actually stop sounding?
// Renders a strike tail and a clean-rub stop tail with the production
// BowlVoice, reporting per-100 ms level (dBFS), the voice envelope,
// and the exact instant the output goes permanently silent.
// Host-only tool - not part of the firmware build.

#include "bowl_voice.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr float kFs  = 48000.0f;
constexpr int   kBlk = 24;

float Db(double x)
{
    return x > 1.0e-12 ? static_cast<float>(20.0 * std::log10(x)) : -240.0f;
}

/** Render `seconds`, printing a line every 100 ms: output RMS (dBFS),
 *  voice envelope, IsActive. Returns nothing; the interesting bit is
 *  the transition to permanent digital silence. */
void RunTail(bowls::BowlVoice& v, double seconds, const char* tag)
{
    const long total   = static_cast<long>(seconds * kFs);
    float      l[kBlk], r[kBlk];
    double     win_sum = 0.0;
    long       win_n   = 0;
    long       last_nz = -1; // last sample with any non-zero output
    bool       was_active = true;
    for(long base = 0; base < total; base += kBlk)
    {
        for(int i = 0; i < kBlk; ++i)
        {
            l[i] = 0.0f;
            r[i] = 0.0f;
        }
        v.ProcessBlock(l, r, kBlk);
        for(int i = 0; i < kBlk; ++i)
        {
            const double m = 0.5 * (std::fabs(l[i]) + std::fabs(r[i]));
            win_sum += m * m;
            ++win_n;
            if(l[i] != 0.0f || r[i] != 0.0f)
                last_nz = base + i;
        }
        const bool active = v.IsActive();
        if(win_n >= static_cast<long>(0.1 * kFs) || active != was_active)
        {
            const double rms = win_n > 0 ? std::sqrt(win_sum / win_n) : 0.0;
            std::printf("[%s] t=%6.2f s  out=%7.1f dBFS  env=%9.6f  %s\n",
                        tag,
                        static_cast<double>(base + kBlk) / kFs,
                        Db(rms),
                        v.Envelope(),
                        active ? "active" : "INACTIVE");
            win_sum = 0.0;
            win_n   = 0;
            was_active = active;
        }
    }
    std::printf("[%s] last non-zero sample at t=%.3f s (of %.1f s)\n\n",
                tag,
                static_cast<double>(last_nz) / kFs,
                seconds);
}

} // namespace

int main()
{
    // A: plain strike, profile 0 (Temple), default live params, medium
    // strike on a 220 Hz bowl. The fundamental's T60 is 12 s.
    {
        bowls::BowlVoice v;
        v.Init(kFs);
        v.SetProfile(0);
        v.SetBrightness(0.5f);
        v.SetDecayMult(1.0f);
        v.SetShimmerMult(1.0f);
        v.Strike(220.0f, 0.7f, 1.0f, 1.0f);
        RunTail(v, 40.0, "strike");
    }

    // B: clean rim tone at full level for 4 s, then rub released -
    // the tail should ring out on the resonators' T60 alone.
    {
        bowls::BowlVoice v;
        v.Init(kFs);
        v.SetProfile(0);
        v.SetBrightness(0.5f);
        v.SetDecayMult(1.0f);
        v.SetShimmerMult(1.0f);
        v.SelectForRub(220.0f, 1.0f);
        v.SetRub(1.0f, 1.0f);
        // Sustain 4 s without printing.
        const long total = static_cast<long>(4.0 * kFs);
        float      l[kBlk], r[kBlk];
        for(long base = 0; base < total; base += kBlk)
        {
            for(int i = 0; i < kBlk; ++i)
            {
                l[i] = 0.0f;
                r[i] = 0.0f;
            }
            v.ProcessBlock(l, r, kBlk);
        }
        v.SetRub(0.0f, 1.0f); // wheel released
        RunTail(v, 30.0, "rubstop");
    }
    return 0;
}
