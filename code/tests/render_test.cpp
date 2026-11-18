// Host-side DSP tests for direction-selected rim texture.
// Run with `make -C code/tests test` from the MSYS2 UCRT64 shell.
//
// Settings for every render: 48 kHz, 24-sample blocks (the firmware's
// control/audio quantum), deterministic voices (same Init seed).
//
//   T0  rub path (both rimming characters + crossfade) matches the saved render
//       (.baseline/baseline.raw; skipped with a note when absent)
//   T1  textured sustained RMS matches the recorded anchor (<= 1 dB)
//   T2  clean from silence: blooms, sustains at the friction path's
//       matched loudness (<= 2 dB), rings down naturally on stop
//   T3  spectral cleanliness: off-modal energy >= 30 dB below modal
//       energy (Goertzel at every in-band paired mode; this is also the
//       "friction contribution is exactly zero" witness)
//   T4  rapid reversals, stop/restart and live parameter changes stay
//       finite, bounded and free of gross discontinuities
//   T5  bounded sweep: pitches x profiles x decay/brightness/shimmer
//       extremes, clean drive at full rub level
//   T6  45 s sustained clean drive at the longest decay: finite,
//       bounded, no growing oscillator error
//
// `./render_test.exe cal` only prints the numbers used to calibrate
// kCleanDrive (bowl_voice.h) and the T1 anchor.

#include "bowl_voice.h"
#include "bowl_scale.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
constexpr float  kFs  = 48000.0f;
constexpr int    kBlk = 24;
constexpr double kPiD = 3.14159265358979;

// Recorded from the pre-change firmware path at implementation time
// (see `make cal`); T1 guards the friction character against drift.
double kTexturedRefRms = 0.99932421;

int g_failures = 0;

void Check(bool ok, const char* name, const char* detail)
{
    std::printf("%-46s %s  %s\n", name, ok ? "PASS" : "FAIL", detail);
    if(!ok)
        ++g_failures;
}

struct Stats
{
    double rms    = 0.0;
    double peak   = 0.0;
    double delta  = 0.0; // max sample-to-sample jump (mono)
    bool   finite = true;
};

/** Render `seconds` of audio in 24-sample blocks. RMS is taken over
 *  [skip_s, seconds) of the mono sum; peak/delta over everything. */
Stats Render(bowls::BowlVoice& v,
             double            seconds,
             double            skip_s  = 0.0,
             std::vector<float>* cap   = nullptr)
{
    const long total = static_cast<long>(seconds * kFs);
    const long skip  = static_cast<long>(skip_s * kFs);
    float      l[kBlk], r[kBlk];
    Stats      s;
    double     sum = 0.0;
    long       cnt = 0;
    float      prev = 0.0f;
    bool       have_prev = false;
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
            const float m = 0.5f * (l[i] + r[i]);
            if(cap)
                cap->push_back(m);
            if(!std::isfinite(m) || !std::isfinite(l[i])
               || !std::isfinite(r[i]))
                s.finite = false;
            const double al = std::fabs(static_cast<double>(l[i]));
            const double ar = std::fabs(static_cast<double>(r[i]));
            if(al > s.peak)
                s.peak = al;
            if(ar > s.peak)
                s.peak = ar;
            if(have_prev)
            {
                const double d
                    = std::fabs(static_cast<double>(m) - prev);
                if(d > s.delta)
                    s.delta = d;
            }
            prev      = m;
            have_prev = true;
            if(base + i >= skip)
            {
                sum += static_cast<double>(m) * m;
                ++cnt;
            }
        }
    }
    s.rms = cnt > 0 ? std::sqrt(sum / static_cast<double>(cnt)) : 0.0;
    return s;
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

/** Power of `x[off..off+n)` at frequency f (Goertzel, no FFT needed). */
double GoertzelPower(const std::vector<float>& x, size_t off, size_t n,
                     double f)
{
    const double w  = 2.0 * kPiD * f / kFs;
    const double cw = 2.0 * std::cos(w);
    double       s1 = 0.0, s2 = 0.0;
    for(size_t i = 0; i < n; ++i)
    {
        const double s0 = x[off + i] + cw * s1 - s2;
        s2              = s1;
        s1              = s0;
    }
    return s1 * s1 + s2 * s2 - cw * s1 * s2;
}

/** The fixed T0 script - must mirror baseline_gen.cpp EXACTLY.
 *  Rub-only: guards both rimming characters (and their crossfade) while
 *  the strike model is intentionally changed. */
void RunBaselineScript(std::vector<float>& out)
{
    bowls::BowlVoice v;
    v.Init(kFs);
    v.SetProfile(0);
    v.SetBrightness(0.5f);
    v.SelectForRub(220.0f, 1.0f);
    v.SetRub(0.8f, 0.0f); // textured
    Render(v, 1.0, 0.0, &out);
    v.SetRub(0.8f, 1.0f); // crossfade to clean
    Render(v, 1.0, 0.0, &out);
    v.SetRub(0.0f, 1.0f); // wheel stops: fade + ring-down
    Render(v, 0.5, 0.0, &out);
}

void T0_BaselineIdentity()
{
    FILE* f = std::fopen(".baseline/baseline.raw", "rb");
    if(!f)
    {
        std::printf("%-46s SKIP  (.baseline/baseline.raw missing - run "
                    "'make baseline' from the accepted tree)\n",
                    "T0 rub sample-identity vs accepted build");
        return;
    }
    std::vector<float> ref;
    float              buf[4096];
    size_t             nr;
    while((nr = std::fread(buf, sizeof(float), 4096, f)) > 0)
        ref.insert(ref.end(), buf, buf + nr);
    std::fclose(f);

    std::vector<float> now;
    RunBaselineScript(now);

    double max_diff = 0.0;
    if(ref.size() == now.size())
        for(size_t i = 0; i < ref.size(); ++i)
        {
            const double d = std::fabs(static_cast<double>(ref[i])
                                       - static_cast<double>(now[i]));
            if(d > max_diff)
                max_diff = d;
        }
    char det[96];
    std::snprintf(det, sizeof(det), "samples=%u max|diff|=%g",
                  static_cast<unsigned>(now.size()), max_diff);
    // The block-step recurrence is mathematically equivalent to the saved
    // per-block trig render; keep its float-rounding error at 1e-5 or less.
    Check(ref.size() == now.size() && max_diff <= 1.0e-5,
          "T0 rub render matches accepted build within roundoff", det);
}

/** Sustained rubbed render at the reference setup (220 Hz, Temple,
 *  brightness 0.5, full rub level) at a given decay multiplier. */
double SustainedRms(bool clean, float decay_mult = 1.0f)
{
    bowls::BowlVoice v;
    v.Init(kFs);
    v.SetProfile(0);
    v.SetBrightness(0.5f);
    v.SetDecayMult(decay_mult);
    v.SelectForRub(220.0f, 1.0f);
    v.SetRub(1.0f, clean ? 1.0f : 0.0f);
    const Stats s = Render(v, 4.0, 2.0);
    return s.rms;
}

void T1_TexturedReference()
{
    const double rms = SustainedRms(false);
    const double db  = kTexturedRefRms > 0.0
                           ? 20.0 * std::log10(rms / kTexturedRefRms)
                           : 0.0;
    char         det[96];
    std::snprintf(det, sizeof(det), "rms=%.6f anchor=%.6f (%+.2f dB)",
                  rms, kTexturedRefRms, db);
    Check(kTexturedRefRms > 0.0 && std::fabs(db) <= 1.0,
          "T1 textured sustained reference", det);
}

void T2_CleanFromSilence()
{
    // Decay-matched reference: the clean drive is normalized by
    // sqrt(1-r), so it should track the friction path's Decay law.
    const double text_rms = SustainedRms(false, 0.25f);

    // Select WITHOUT striking, then drive counter-clockwise (clean).
    // Short decay so the natural ring-down is measurable in 2 s.
    bowls::BowlVoice v;
    v.Init(kFs);
    v.SetProfile(0);
    v.SetBrightness(0.5f);
    v.SetDecayMult(0.25f); // mode 1 T60 = 3 s
    v.SelectForRub(220.0f, 1.0f);
    v.SetRub(1.0f, 1.0f);
    std::vector<float> cap;
    Render(v, 3.5, 0.0, &cap);
    v.SetRub(0.0f, 1.0f); // wheel stops: excitation fades, bowl rings
    Render(v, 2.0, 0.0, &cap);

    const double bloom = WindowRms(cap, 0.05, 0.15);
    const double sust  = WindowRms(cap, 2.5, 3.4);
    const double tail  = WindowRms(cap, 4.5, 5.5);

    char det[128];
    std::snprintf(det, sizeof(det),
                  "bloom=%.6f sust=%.6f (textured %.6f, %+.2f dB)",
                  bloom, sust, text_rms,
                  20.0 * std::log10(sust / text_rms));
    Check(bloom > 0.0 && sust > 4.0 * bloom, // ~12 dB bloom
          "T2 clean blooms from silence", det);

    std::snprintf(det, sizeof(det), "clean=%.6f textured=%.6f %+.2f dB",
                  sust, text_rms, 20.0 * std::log10(sust / text_rms));
    Check(std::fabs(20.0 * std::log10(sust / text_rms)) <= 2.0,
          "T2 clean/textured matched sustained level", det);

    std::snprintf(det, sizeof(det), "tail=%.6f sust=%.6f (%.1f dB down)",
                  tail, sust, 20.0 * std::log10(sust / tail));
    Check(tail > 0.0 && tail <= sust * 0.1, // >= 20 dB natural ring-down
          "T2 clean rings down after stop", det);
}

void T3_SpectralCleanliness()
{
    constexpr double kFund = 220.0;
    bowls::BowlVoice v;
    v.Init(kFs);
    v.SetProfile(0);
    v.SetBrightness(0.5f);
    v.SelectForRub(kFund, 1.0f);
    v.SetRub(1.0f, 1.0f);
    std::vector<float> cap;
    Render(v, 3.5, 0.0, &cap);

    // Measurement window: after the bloom + 100 ms character crossfade
    // and any startup transient have settled. 8192 samples = 5.86 Hz
    // bins - wide enough for the detune wander and the swirl AM.
    const size_t off = static_cast<size_t>(2.5 * kFs);
    const size_t n   = 8192;
    double       tot = 0.0;
    for(size_t i = off; i < off + n; ++i)
        tot += static_cast<double>(cap[i]) * cap[i];
    tot /= static_cast<double>(n);

    const bowls::BowlProfile& p = bowls::GetBowlProfile(0);
    double                    modal = 0.0;
    for(int m = 0; m < bowls::kNumModes; ++m)
    {
        const double fa = kFund * p.ratio[m];
        const double fb = fa + p.detune_hz[m]; // shimmer=1, wander~1
        if(fa < 0.45 * kFs)
            modal += 2.0 / (static_cast<double>(n) * n)
                     * GoertzelPower(cap, off, n, fa);
        if(fb < 0.45 * kFs)
            modal += 2.0 / (static_cast<double>(n) * n)
                     * GoertzelPower(cap, off, n, fb);
    }
    double off_modal = tot - modal;
    if(off_modal < tot * 1.0e-12)
        off_modal = tot * 1.0e-12;
    const double db = 10.0 * std::log10(modal / off_modal);

    char det[96];
    std::snprintf(det, sizeof(det), "modal/off-modal = %.1f dB", db);
    Check(db >= 30.0, "T3 clean off-modal energy >= 30 dB down", det);
}

void T4_Reversals()
{
    // The same gesture script twice: always-textured vs alternating
    // direction. The reversal run must stay within the textured run's
    // envelope - no doubled drive, no clicks beyond the texture's own
    // roughness (relative thresholds, robust to calibration).
    const int rates_ms[2] = {60, 15}; // slow and fast reversal rates
    double   peak[2] = {0.0, 0.0}, delta[2] = {0.0, 0.0};
    bool     finite[2] = {true, true};
    for(int mode = 0; mode < 2; ++mode)
    {
        bowls::BowlVoice v;
        v.Init(kFs);
        v.SetProfile(0);
        v.SetBrightness(0.5f);
        v.Strike(310.2f, 0.6f, 1.0f, 1.0f);
        for(int r = 0; r < 2; ++r)
            for(int k = 0; k < 40; ++k)
            {
                const bool clean = mode == 1 && (k % 2) != 0;
                v.SetRub(1.0f, clean ? 1.0f : 0.0f);
                const Stats s = Render(v, rates_ms[r] / 1000.0);
                if(!s.finite)
                    finite[mode] = false;
                if(s.peak > peak[mode])
                    peak[mode] = s.peak;
                if(s.delta > delta[mode])
                    delta[mode] = s.delta;
            }
        // Stop, restart, and live parameter changes while rubbing.
        const float decays[]   = {2.0f, 0.5f};
        const float shimmers[] = {0.0f, 2.0f};
        v.SetRub(0.0f, 1.0f);
        Stats s = Render(v, 0.3);
        v.SetRub(1.0f, mode == 1 ? 1.0f : 0.0f);
        s = Render(v, 0.5);
        for(int d = 0; d < 2; ++d)
        {
            v.SetDecayMult(decays[d]);
            s = Render(v, 0.2);
            v.SetShimmerMult(shimmers[d]);
            s = Render(v, 0.2);
        }
        if(!s.finite)
            finite[mode] = false;
        if(s.peak > peak[mode])
            peak[mode] = s.peak;
        if(s.delta > delta[mode])
            delta[mode] = s.delta;
    }

    char det[160];
    std::snprintf(det, sizeof(det),
                  "peak tex=%.3f rev=%.3f  max-delta tex=%.3f rev=%.3f",
                  peak[0], peak[1], delta[0], delta[1]);
    Check(finite[0] && finite[1] && peak[1] <= 1.5 * peak[0] + 0.05
              && delta[1] <= 1.5 * delta[0] + 0.02,
          "T4 reversals/stop-restart/param changes bounded", det);
}

void T5_BoundedSweep()
{
    // Every configuration is rendered clean AND textured; the clean
    // peak must stay near the textured peak at the SAME settings (the
    // friction path deliberately goes dark on the highest bowls - the
    // rub lowpass caps at 750 Hz - so a few dB of clean-over-textured
    // is expected there, not a blowup).
    double worst_ratio = 0.0, worst_peak = 0.0;
    bool   finite = true;
    char   worst_cfg[128]  = "";
    const float funds_p[]  = {99.0f, 465.0f, 1026.0f};
    const float funds_sh[] = {99.0f, 1026.0f};
    const int   profs_sh[] = {0, 3};
    const float decays[]   = {0.25f, 1.0f, 2.0f};
    const float brights[]  = {0.0f, 1.0f};
    const float shimmers[] = {0.0f, 2.0f};

    struct Cfg
    {
        float fund, decay, bright, shimmer;
        int   prof;
    };
    std::vector<Cfg> cfgs;
    for(size_t fi = 0; fi < 3; ++fi)
        for(int pr = 0; pr < bowls::kNumProfiles; ++pr)
            for(size_t di = 0; di < 3; ++di)
                for(size_t bi = 0; bi < 2; ++bi)
                    cfgs.push_back(
                        Cfg{funds_p[fi], decays[di], brights[bi], 1.0f,
                            pr});
    // Shimmer extremes on a subset (det=0 locks the pair on one
    // frequency; det=2x maximizes the oscillator/resonator spread).
    for(size_t fi = 0; fi < 2; ++fi)
        for(size_t pi = 0; pi < 2; ++pi)
            for(size_t si = 0; si < 2; ++si)
                cfgs.push_back(Cfg{funds_sh[fi], 1.0f, 0.5f,
                                   shimmers[si], profs_sh[pi]});

    for(size_t c = 0; c < cfgs.size(); ++c)
    {
        double pk[2] = {0.0, 0.0};
        for(int mode = 0; mode < 2; ++mode)
        {
            bowls::BowlVoice v;
            v.Init(kFs);
            v.SetProfile(cfgs[c].prof);
            v.SetBrightness(cfgs[c].bright);
            v.SetDecayMult(cfgs[c].decay);
            v.SetShimmerMult(cfgs[c].shimmer);
            v.SelectForRub(cfgs[c].fund, 1.0f);
            v.SetRub(1.0f, mode == 1 ? 1.0f : 0.0f);
            const Stats s = Render(v, 0.8);
            if(!s.finite)
                finite = false;
            pk[mode] = s.peak;
        }
        const double ratio = pk[1] / (pk[0] + 1.0e-9);
        if(ratio > worst_ratio)
        {
            worst_ratio = ratio;
            std::snprintf(worst_cfg, sizeof(worst_cfg),
                          "f=%.0f p=%d d=%.2f b=%.1f sh=%.1f",
                          cfgs[c].fund, cfgs[c].prof, cfgs[c].decay,
                          cfgs[c].bright, cfgs[c].shimmer);
        }
        if(pk[1] > worst_peak)
            worst_peak = pk[1];
    }

    char det[192];
    std::snprintf(det, sizeof(det),
                  "worst clean/textured peak ratio=%.2f (%s), clean "
                  "peak=%.3f",
                  worst_ratio, worst_cfg, worst_peak);
    // Ratio allowance 4x: on the highest bowls the friction path is
    // intentionally near-silent (750 Hz band cap) while the clean tone
    // stays present - that is the feature. The absolute bound keeps
    // the clean drive inside the panel's existing envelope.
    Check(finite && worst_ratio <= 4.0 && worst_peak <= 4.0,
          "T5 bounded clean sweep", det);
}

void T6_LongSustained()
{
    // Worst-case corner: lowest bowl, longest profile, max decay (mode
    // 1 T60 = 45 s), max brightness. 45 s of sustained clean drive must
    // stay finite, stop growing (no oscillator/resonator runaway) and
    // remain near the friction path's own envelope at the same corner.
    double      pk[2] = {0.0, 0.0};
    bool        finite = true;
    double      mid = 0.0, late = 0.0;
    for(int mode = 0; mode < 2; ++mode)
    {
        bowls::BowlVoice v;
        v.Init(kFs);
        v.SetProfile(1); // Deep: mode 1 T60 = 15 s
        v.SetBrightness(1.0f);
        v.SetDecayMult(3.0f); // -> 45 s
        v.SelectForRub(99.0f, 1.0f);
        v.SetRub(1.0f, mode == 1 ? 1.0f : 0.0f);
        std::vector<float> cap;
        const Stats        s = Render(v, 45.0, 0.0, &cap);
        if(!s.finite)
            finite = false;
        pk[mode] = s.peak;
        if(mode == 1)
        {
            mid  = WindowRms(cap, 20.0, 25.0);
            late = WindowRms(cap, 40.0, 45.0);
        }
    }
    const double db = 20.0 * std::log10(late / mid);

    char det[192];
    std::snprintf(det, sizeof(det),
                  "peak tex=%.3f clean=%.3f  rms mid=%.4f late=%.4f "
                  "(%+.2f dB)",
                  pk[0], pk[1], mid, late, db);
    Check(finite && pk[1] <= 1.5 * pk[0] + 0.1 && std::fabs(db) <= 3.0,
          "T6 45 s clean drive at max decay: stable", det);
}

void T7_ShimmerRetune()
{
    bowls::BowlVoice changed, reference;
    for(bowls::BowlVoice* voice : {&changed, &reference})
    {
        voice->Init(kFs);
        voice->SetProfile(0);
        voice->SetBrightness(0.5f);
        voice->SelectForRub(310.2f, 1.0f);
        voice->SetRub(1.0f, 1.0f);
    }
    Render(changed, 1.0);
    Render(reference, 1.0);

    changed.SetShimmerMult(1.0625f);
    std::vector<float> changed_audio, reference_audio;
    const Stats changed_stats = Render(changed, 0.02, 0.0, &changed_audio);
    const Stats reference_stats = Render(reference, 0.02, 0.0, &reference_audio);

    double max_active_difference = 0.0;
    for(size_t i = 0; i < changed_audio.size(); ++i)
    {
        const double difference
            = std::fabs(static_cast<double>(changed_audio[i])
                        - reference_audio[i]);
        if(difference > max_active_difference)
            max_active_difference = difference;
    }

    char detail[96];
    std::snprintf(detail, sizeof(detail), "active output diff=%.8f",
                  max_active_difference);
    Check(changed_stats.finite && reference_stats.finite
              && max_active_difference <= 1.0e-7,
          "T7 active shimmer change is deferred", detail);

    changed.SelectForRub(310.2f, 1.0f);
    changed_audio.clear();
    reference_audio.clear();
    const Stats selected_stats = Render(changed, 0.02, 0.0, &changed_audio);
    const Stats current_stats = Render(reference, 0.02, 0.0, &reference_audio);
    double selected_difference = 0.0;
    for(size_t i = 0; i < changed_audio.size(); ++i)
    {
        const double difference
            = std::fabs(static_cast<double>(changed_audio[i])
                        - reference_audio[i]);
        if(difference > selected_difference)
            selected_difference = difference;
    }
    std::snprintf(detail, sizeof(detail), "after selection diff=%.6f",
                  selected_difference);
    Check(selected_stats.finite && current_stats.finite
              && selected_difference > 1.0e-4,
          "T7b new Rimming selection commits shimmer", detail);

    bowls::BowlVoice immediate, baseline;
    immediate.Init(kFs);
    baseline.Init(kFs);
    immediate.SetShimmerMult(0.0f);
    for(bowls::BowlVoice* voice : {&immediate, &baseline})
    {
        voice->SetProfile(0);
        voice->SetBrightness(0.5f);
        voice->SelectForRub(310.2f, 1.0f);
        voice->SetRub(1.0f, 1.0f);
    }
    changed_audio.clear();
    reference_audio.clear();
    const Stats immediate_stats = Render(immediate, 1.0, 0.0, &changed_audio);
    const Stats baseline_stats = Render(baseline, 1.0, 0.0, &reference_audio);
    double active_difference = 0.0;
    for(size_t i = 0; i < changed_audio.size(); ++i)
    {
        const double difference
            = std::fabs(static_cast<double>(changed_audio[i])
                        - reference_audio[i]);
        if(difference > active_difference)
            active_difference = difference;
    }
    std::snprintf(detail, sizeof(detail), "next-note output diff=%.6f",
                  active_difference);
    Check(immediate_stats.finite && baseline_stats.finite
              && active_difference > 1.0e-4,
          "T7b quiet voice uses new shimmer setting", detail);
}

void T8_SwirlRecurrence()
{
    bowls::BowlVoice a, b;
    for(bowls::BowlVoice* voice : {&a, &b})
    {
        voice->Init(kFs);
        voice->SetProfile(0);
        voice->SetBrightness(0.5f);
        voice->SelectForRub(220.0f, 1.0f);
        voice->SetRub(1.0f, 1.0f);
    }

    constexpr long kSamples = 10L * static_cast<long>(kFs);
    float          al[kBlk], ar[kBlk], bl[kBlk], br[kBlk];
    bool           finite = true;
    double         max_difference = 0.0;
    double         peak = 0.0;
    for(long base = 0; base < kSamples; base += kBlk)
    {
        for(int i = 0; i < kBlk; ++i)
            al[i] = ar[i] = bl[i] = br[i] = 0.0f;
        a.ProcessBlock(al, ar, kBlk);
        b.ProcessBlock(bl, br, kBlk);
        for(int i = 0; i < kBlk; ++i)
        {
            finite = finite && std::isfinite(al[i]) && std::isfinite(ar[i])
                     && std::isfinite(bl[i]) && std::isfinite(br[i]);
            if(std::fabs(static_cast<double>(al[i])) > peak)
                peak = std::fabs(static_cast<double>(al[i]));
            if(std::fabs(static_cast<double>(ar[i])) > peak)
                peak = std::fabs(static_cast<double>(ar[i]));
            const double left_difference
                = std::fabs(static_cast<double>(al[i]) - bl[i]);
            const double right_difference
                = std::fabs(static_cast<double>(ar[i]) - br[i]);
            if(left_difference > max_difference)
                max_difference = left_difference;
            if(right_difference > max_difference)
                max_difference = right_difference;
        }
    }

    char detail[96];
    std::snprintf(detail, sizeof(detail), "peak=%.3f max stereo diff=%.8f",
                  peak, max_difference);
    Check(finite && max_difference == 0.0,
          "T8 10 s swirl recurrence is deterministic and finite", detail);
}

void Cal()
{
    const double text_rms  = SustainedRms(false);
    const double clean_rms = SustainedRms(true);
    std::printf("calibration (220 Hz, Temple, defaults, rub=1, 2-4 s):\n");
    std::printf("  textured sustained rms = %.8f\n", text_rms);
    std::printf("  clean    sustained rms = %.8f\n", clean_rms);
    std::printf("  clean - textured       = %+.2f dB\n",
                20.0 * std::log10(clean_rms / text_rms));
    std::printf("  -> set kTexturedRefRms = %.8f (render_test.cpp)\n",
                text_rms);
    std::printf("  -> adjust kCleanDrive (bowl_voice.h) until the dB "
                "delta is ~0\n");
}

} // namespace

int main(int argc, char** argv)
{
    if(argc > 1 && std::strcmp(argv[1], "cal") == 0)
    {
        Cal();
        return 0;
    }
    T0_BaselineIdentity();
    T1_TexturedReference();
    T2_CleanFromSilence();
    T3_SpectralCleanliness();
    T4_Reversals();
    T5_BoundedSweep();
    T6_LongSustained();
    T7_ShimmerRetune();
    T8_SwirlRecurrence();
    std::printf(g_failures == 0 ? "\nALL TESTS PASSED\n"
                                : "\n%d TEST(S) FAILED\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
