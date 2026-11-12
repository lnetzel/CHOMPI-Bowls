// Host-side strike regressions for the improved striking character
// (plans/improve_striking_character_plan.md). Not part of the firmware
// build; run with `make -C code/tests strike-test`.
//
//   S1  determinism: identical fresh-voice renders are bit-identical
//   S2  block-size equivalence: ProcessBlock(24) vs ProcessBlock(1)
//   S3  re-strikes at 10/50/200/1000 ms stay finite and bounded
//   S4  rapid repeated hard strikes + voice steal stay finite/bounded
//   S5  strike -> silent rim selection on a reused voice: no leftover
//       burst restarts (level decays, no re-trigger)
//   S6  above-band modes (highest bowl at 2x tuning) stay finite/bounded
//   S7  register/control sweep: 12 bowls x 5 profiles, Strike/Brightness
//       extremes, tuning endpoints - finite, bounded, no loudness blowups

#include "bowl_scale.h"
#include "bowl_voice.h"
#include "control_mappings.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr float kFs  = 48000.0f;
constexpr int   kBlk = 24;
int             g_failures = 0;

void Check(bool ok, const char* name, const char* detail)
{
    std::printf("%-52s %s  %s\n", name, ok ? "PASS" : "FAIL", detail);
    if(!ok)
        ++g_failures;
}

struct Stats
{
    double rms    = 0.0;
    double peak   = 0.0;
    bool   finite = true;
};

Stats Render(bowls::BowlVoice& v, double seconds, std::vector<float>* cap,
             int block = kBlk)
{
    const long total = static_cast<long>(seconds * kFs);
    float      l[64], r[64];
    Stats      s;
    double     sum = 0.0;
    for(long base = 0; base < total; base += block)
    {
        for(int i = 0; i < block; ++i)
        {
            l[i] = 0.0f;
            r[i] = 0.0f;
        }
        v.ProcessBlock(l, r, static_cast<size_t>(block));
        for(int i = 0; i < block; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]);
            if(cap)
                cap->push_back(m);
            if(!std::isfinite(m))
                s.finite = false;
            const double a = std::fabs(static_cast<double>(m));
            if(a > s.peak)
                s.peak = a;
            sum += static_cast<double>(m) * m;
        }
    }
    s.rms = std::sqrt(sum / static_cast<double>(total));
    return s;
}

void FreshVoice(bowls::BowlVoice& v, float bright = 0.5f)
{
    v.Init(kFs);
    v.SetProfile(0);
    v.SetBrightness(bright);
    v.SetDecayMult(0.7071f); // panel default (Decay 16)
    v.SetShimmerMult(0.5f);  // panel default (Shimmer 8)
}

double WindowRms(const std::vector<float>& x, double t0, double t1)
{
    const size_t a = static_cast<size_t>(t0 * kFs);
    const size_t b = static_cast<size_t>(t1 * kFs);
    double       sum = 0.0;
    for(size_t i = a; i < b && i < x.size(); ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(b - a));
}

void S1_Determinism()
{
    std::vector<float> a, b;
    for(int k = 0; k < 2; ++k)
    {
        bowls::BowlVoice v;
        FreshVoice(v);
        v.Strike(310.2f, 0.5f, 1.0f, 1.0f);
        Render(v, 1.0, k == 0 ? &a : &b);
    }
    double max_diff = 0.0;
    for(size_t i = 0; i < a.size(); ++i)
    {
        const double d = std::fabs(static_cast<double>(a[i]) - b[i]);
        if(d > max_diff)
            max_diff = d;
    }
    char det[64];
    std::snprintf(det, sizeof(det), "max|diff|=%g over %u samples",
                  max_diff, static_cast<unsigned>(a.size()));
    Check(a.size() == b.size() && max_diff == 0.0, "S1 deterministic strike",
          det);
}

void S2_BlockEquivalence()
{
    std::vector<float> a, b;
    {
        bowls::BowlVoice v;
        FreshVoice(v);
        v.Strike(310.2f, 0.7f, 1.0f, 1.0f);
        Render(v, 1.0, &a, 24);
    }
    {
        bowls::BowlVoice v;
        FreshVoice(v);
        v.Strike(310.2f, 0.7f, 1.0f, 1.0f);
        Render(v, 1.0, &b, 1);
    }
    double max_diff = 0.0;
    for(size_t i = 0; i < a.size(); ++i)
    {
        const double d = std::fabs(static_cast<double>(a[i]) - b[i]);
        if(d > max_diff)
            max_diff = d;
    }
    // Block-rate modulation (swirl/wander/renorm) advances per block
    // from n, so 1-sample blocks track with sub-ms timing granularity;
    // the contact envelope is sample-count based and does not change
    // duration. Tolerance documents the block-rate modulation slack.
    char det[64];
    std::snprintf(det, sizeof(det), "max|diff|=%g", max_diff);
    Check(max_diff < 1.0e-3, "S2 block size 1 vs 24 equivalent", det);
}

void S3_Restrikes()
{
    const double gaps_ms[4] = {10, 50, 200, 1000};
    bool         finite = true;
    double       peak = 0.0;
    for(int g = 0; g < 4; ++g)
        for(int phase = 0; phase < 3; ++phase)
        {
            bowls::BowlVoice v;
            FreshVoice(v);
            v.Strike(219.6f, 0.6f, 1.0f, 1.0f);
            Render(v, gaps_ms[g] / 1000.0 * phase, nullptr);
            v.Strike(219.6f, 0.9f, 1.0f, 1.0f); // same-bowl re-hit
            const Stats s = Render(v, 1.0, nullptr);
            if(!s.finite)
                finite = false;
            if(s.peak > peak)
                peak = s.peak;
        }
    char det[64];
    std::snprintf(det, sizeof(det), "peak=%.3f", peak);
    Check(finite && peak <= 1.5, "S3 re-strikes 10/50/200/1000 ms bounded",
          det);
}

void S4_RapidAndSteal()
{
    // 20 fast hard hits on the same bowl, then a steal-style deep-damped
    // strike of a DIFFERENT bowl on the same voice while it rings.
    bowls::BowlVoice v;
    FreshVoice(v);
    bool   finite = true;
    double peak = 0.0;
    for(int k = 0; k < 20; ++k)
    {
        v.Strike(310.2f, 1.0f, 1.0f, 1.0f);
        const Stats s = Render(v, 0.08, nullptr);
        if(!s.finite)
            finite = false;
        if(s.peak > peak)
            peak = s.peak;
    }
    v.Strike(693.6f, 0.8f, 1.0f, 1.0f, 0.02f); // stolen-voice damping
    const Stats s = Render(v, 1.5, nullptr);
    if(!s.finite)
        finite = false;
    if(s.peak > peak)
        peak = s.peak;
    char det[64];
    std::snprintf(det, sizeof(det), "peak=%.3f", peak);
    // 20 fortissimo hits at 80 ms genuinely accumulate ring energy; the
    // pre-change engine peaks at 1.42 on this exact script, so the bar
    // is 2.0 (no regression), not silence. The master SoftClip bounds
    // the downstream mix as before.
    Check(finite && peak <= 2.0, "S4 rapid hits + steal bounded", det);
}

void S5_StrikeToRimReuse()
{
    // Strike, then re-select the SAME voice for rimming (no wheel
    // activity): the leftover ring must decay, no burst may restart.
    bowls::BowlVoice v;
    FreshVoice(v);
    v.Strike(465.0f, 0.8f, 1.0f, 1.0f);
    std::vector<float> cap;
    Render(v, 0.2, &cap);
    v.SelectForRub(465.0f, 1.0f); // rim selection never strikes
    v.SetRub(0.0f, 0.0f);
    Render(v, 1.5, &cap);
    const double before = WindowRms(cap, 0.05, 0.15);
    const double after  = WindowRms(cap, 1.4, 1.7);
    char               det[96];
    std::snprintf(det, sizeof(det), "rms early=%.5f late=%.6f", before,
                  after);
    Check(after < before * 0.5, "S5 strike->rim reuse: no burst restart",
          det);
}

void S6_AboveBandModes()
{
    // Highest bowl at 2x tuning: modes above Nyquist must stay muted.
    bowls::BowlVoice v;
    FreshVoice(v, 1.0f);
    v.Strike(1026.0f * 2.0f, 1.0f, 1.0f, 1.0f);
    const Stats s = Render(v, 2.0, nullptr);
    char        det[64];
    std::snprintf(det, sizeof(det), "peak=%.3f rms=%.5f", s.peak, s.rms);
    Check(s.finite && s.peak <= 1.5, "S6 above-band modes bounded", det);
}

void S7_RegisterControlSweep()
{
    bool   finite = true;
    double worst_peak = 0.0, min_ring = 1.0e30, max_ring = 0.0;
    char   worst_cfg[96] = "";

    // 12 bowls x 5 profiles at panel defaults.
    for(int b = 0; b < bowls::kBowlCount; ++b)
        for(int pr = 0; pr < bowls::kNumProfiles; ++pr)
        {
            bowls::BowlVoice v;
            FreshVoice(v);
            v.SetProfile(pr);
            v.Strike(bowls::kBowlScaleHz[b], 0.5f, 1.0f, 1.0f);
            std::vector<float> cap;
            Render(v, 2.0, &cap);
            const Stats      s = Render(v, 0.0, nullptr); // no-op guard
            (void)s;
            double peak = 0.0;
            bool   fin  = true;
            for(size_t i = 0; i < cap.size(); ++i)
            {
                if(!std::isfinite(cap[i]))
                    fin = false;
                const double a = std::fabs(static_cast<double>(cap[i]));
                if(a > peak)
                    peak = a;
            }
            const double ring = WindowRms(cap, 0.5, 1.5);
            if(!fin)
                finite = false;
            if(peak > worst_peak)
            {
                worst_peak = peak;
                std::snprintf(worst_cfg, sizeof(worst_cfg), "bowl %d (%0.0f Hz) profile %d",
                              b + 1, bowls::kBowlScaleHz[b], pr);
            }
            if(ring < min_ring)
                min_ring = ring;
            if(ring > max_ring)
                max_ring = ring;
        }

    // Strike/Brightness extremes x tuning endpoints on a middle bowl.
    const float strikes[3] = {0.0f, 0.5f, 1.0f};
    const float brights[3] = {0.0f, 0.5f, 1.0f};
    const float tunes[3]   = {0.5f, 1.0f, 2.0f};
    for(int si = 0; si < 3; ++si)
        for(int bi = 0; bi < 3; ++bi)
            for(int ti = 0; ti < 3; ++ti)
            {
                bowls::BowlVoice v;
                FreshVoice(v, brights[bi]);
                v.Strike(465.0f * tunes[ti], strikes[si], 1.0f, 1.0f);
                const Stats s = Render(v, 1.0, nullptr);
                if(!s.finite)
                    finite = false;
                if(s.peak > worst_peak)
                {
                    worst_peak = s.peak;
                    std::snprintf(worst_cfg, sizeof(worst_cfg),
                                  "strike %.1f bright %.1f tune %.1f",
                                  strikes[si], brights[bi], tunes[ti]);
                }
            }

    const double spread_db = 20.0 * std::log10(max_ring / min_ring);
    char         det[192];
    std::snprintf(det, sizeof(det),
                  "peak=%.3f (%s); ring spread over 12 bowls x 5 "
                  "profiles=%.1f dB",
                  worst_peak, worst_cfg, spread_db);
    Check(finite && worst_peak <= 1.5 && spread_db <= 30.0,
          "S7 register/control sweep bounded", det);
}

void S8_CustomPitchRange()
{
    bool finite = true;
    double worst_peak = 0.0;
    const float bases[] = {40.0f, 10000.0f};
    const float tunes[] = {0.5f, 1.0f, 2.0f};
    for(int profile = 0; profile < bowls::kNumProfiles; ++profile)
        for(float base : bases)
            for(float tune : tunes)
            {
                bowls::BowlVoice voice;
                FreshVoice(voice);
                voice.SetProfile(profile);
                voice.Strike(base * tune, 0.5f, 1.0f, 1.0f);
                const Stats stats = Render(voice, 0.1, nullptr);
                finite = finite && stats.finite;
                if(stats.peak > worst_peak)
                    worst_peak = stats.peak;
            }
    char detail[64];
    std::snprintf(detail, sizeof(detail), "peak=%.3f", worst_peak);
    Check(finite && worst_peak <= 1.5,
          "S8 40-10000 Hz pitch range across profiles bounded", detail);
}

double HighBandRms(const std::vector<float>& samples)
{
    double sum = 0.0;
    for(size_t i = 1; i < samples.size(); ++i)
    {
        const double difference
            = static_cast<double>(samples[i]) - samples[i - 1];
        sum += difference * difference;
    }
    return std::sqrt(sum / static_cast<double>(samples.size() - 1));
}

void S8_IndependentHardnessAndAmplitude()
{
    std::vector<float> soft, hard, quiet, loud, legacy, explicit_local;
    {
        bowls::BowlVoice v;
        FreshVoice(v);
        v.StrikeWithAmplitude(465.0f, 0.0f, 0.6f, 1.0f, 1.0f);
        Render(v, 0.25, &soft);
    }
    {
        bowls::BowlVoice v;
        FreshVoice(v);
        v.StrikeWithAmplitude(465.0f, 1.0f, 0.6f, 1.0f, 1.0f);
        Render(v, 0.25, &hard);
    }
    {
        bowls::BowlVoice v;
        FreshVoice(v);
        v.StrikeWithAmplitude(465.0f, 0.5f, 0.3f, 1.0f, 1.0f);
        Render(v, 0.25, &quiet);
    }
    {
        bowls::BowlVoice v;
        FreshVoice(v);
        v.StrikeWithAmplitude(465.0f, 0.5f, 0.6f, 1.0f, 1.0f);
        Render(v, 0.25, &loud);
    }
    {
        bowls::BowlVoice v;
        FreshVoice(v);
        v.Strike(465.0f, 0.5f, 1.0f, 1.0f);
        Render(v, 0.25, &legacy);
    }
    {
        bowls::BowlVoice v;
        FreshVoice(v);
        v.StrikeWithAmplitude(465.0f, 0.5f, 0.65f, 1.0f, 1.0f);
        Render(v, 0.25, &explicit_local);
    }
    const double amplitude_ratio
        = WindowRms(loud, 0.02, 0.20) / WindowRms(quiet, 0.02, 0.20);
    double local_difference = 0.0;
    for(size_t i = 0; i < legacy.size(); ++i)
        local_difference = std::max(
            local_difference,
            std::fabs(static_cast<double>(legacy[i] - explicit_local[i])));
    const bool timbre_changed = HighBandRms(hard) > HighBandRms(soft);
    char detail[128];
    std::snprintf(detail, sizeof(detail),
                  "high-band soft=%.5g hard=%.5g; gain ratio=%.3f; local diff=%g",
                  HighBandRms(soft), HighBandRms(hard), amplitude_ratio,
                  local_difference);
    Check(timbre_changed && std::fabs(amplitude_ratio - 2.0) < 0.02
              && local_difference == 0.0,
          "S8 hardness timbre and amplitude independent; local curve unchanged",
          detail);
}

void S9_MaximumDecay()
{
    const float min_mult = bowls::DecayMultiplierFromTicks(0);
    const float mid_mult = bowls::DecayMultiplierFromTicks(16);
    const float max_mult = bowls::DecayMultiplierFromTicks(32);
    const float deep_t60 = bowls::GetBowlProfile(1).t60_s[0] * max_mult;
    bowls::BowlVoice voice;
    voice.Init(kFs);
    voice.SetProfile(1);
    voice.SetDecayMult(max_mult);
    voice.StrikeWithAmplitude(99.0f, 0.7f, 0.7f, 1.0f, 1.0f);
    const Stats stats = Render(voice, 1.0, nullptr);
    char detail[128];
    std::snprintf(detail, sizeof(detail), "decay %.3f/%.3f/%.3f; Deep T60=%.1f s; peak=%.3f",
                  min_mult, mid_mult, max_mult, deep_t60, stats.peak);
    Check(std::fabs(min_mult - 0.25f) < 1.0e-6f
              && std::fabs(mid_mult - std::sqrt(0.75f)) < 1.0e-5f
              && std::fabs(max_mult - 3.0f) < 1.0e-6f
              && std::fabs(deep_t60 - 45.0f) < 1.0e-5f
              && stats.finite && stats.peak <= 1.5,
          "S9 3x decay endpoint remains finite and bounded", detail);
}

} // namespace

int main()
{
    S1_Determinism();
    S2_BlockEquivalence();
    S3_Restrikes();
    S4_RapidAndSteal();
    S5_StrikeToRimReuse();
    S6_AboveBandModes();
    S7_RegisterControlSweep();
    S8_CustomPitchRange();
    S8_IndependentHardnessAndAmplitude();
    S9_MaximumDecay();
    std::printf(g_failures == 0 ? "\nALL STRIKE TESTS PASSED\n"
                                : "\n%d STRIKE TEST(S) FAILED\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
