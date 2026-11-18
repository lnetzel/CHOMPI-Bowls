// Host-side strike tests. Run with `make -C code/tests strike-test`.
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
//   S10 the subtle, pitch-relative contact tick is present by default
//   S11 brightness ticks retain zero and extend to 1.5 at the maximum
//   S12 maximum brightness substantially raises the strike's upper-mode balance
//   S13 an idle lower-register voice clears its stale activity envelope
//   S14 higher and user-tuned pitches build into the resonant body, not a transient
//   S16 staging strike/steal parameters before excitation preserves the render

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
    const float brights[3] = {0.0f, 0.75f, 1.5f};
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
    Check(finite && worst_peak <= 1.5 && spread_db <= 50.0,
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

double GoertzelPower(const std::vector<float>& samples, float frequency)
{
    const double w = 2.0 * 3.14159265358979323846 * frequency / kFs;
    const double cw = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for(float sample : samples)
    {
        const double s0 = sample + cw * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - cw * s1 * s2;
}

double ModalBrightness(const std::vector<float>& samples, float fundamental)
{
    const bowls::BowlProfile& profile = bowls::GetBowlProfile(0);
    double low = 0.0, high = 0.0;
    for(int m = 0; m < bowls::kNumModes; ++m)
    {
        const float frequency = fundamental * profile.ratio[m];
        if(frequency > 0.45f * kFs)
            continue;
        const float detune = profile.detune_hz[m] * (fundamental / 220.0f)
                             * 0.5f;
        const double power = GoertzelPower(samples, frequency)
                             + GoertzelPower(samples, frequency + detune);
        if(m == 0)
            low += power;
        else
            high += power;
    }
    return std::sqrt(high / low);
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

void S10_DefaultContactTickPresent()
{
    const float pitches[] = {465.0f, 1100.0f, 1560.0f};
    double      worst_tick_ratio = 0.0;
    bool        valid = true;
    for(float pitch : pitches)
    {
        std::vector<float> normal, force_only, tick_enabled;
        for(int render = 0; render < 3; ++render)
        {
            bowls::BowlVoice voice;
            FreshVoice(voice);
            if(render == 1)
                voice.SetStrikeDiagnostics(1.0f, 0.0f);
            else if(render == 2)
                voice.SetStrikeDiagnostics(1.0f, 1.0f);
            voice.Strike(pitch, 0.7f, 1.0f, 1.0f);
            Render(voice, 0.15,
                   render == 0 ? &normal
                   : render == 1 ? &force_only
                                 : &tick_enabled);
        }

        double tick_diff = 0.0;
        double explicit_diff = 0.0;
        double peak = 0.0;
        for(size_t i = 0; i < normal.size(); ++i)
        {
            const double level = std::fabs(static_cast<double>(normal[i]));
            if(level > peak)
                peak = level;
            const double force_diff
                = std::fabs(static_cast<double>(normal[i] - force_only[i]));
            const double enabled_diff
                = std::fabs(static_cast<double>(normal[i] - tick_enabled[i]));
            if(force_diff > tick_diff)
                tick_diff = force_diff;
            if(enabled_diff > explicit_diff)
                explicit_diff = enabled_diff;
        }
        if(peak <= 0.0 || tick_diff <= 1.0e-8
           || tick_diff > 0.25 * peak || explicit_diff != 0.0)
        {
            valid = false;
            continue;
        }
        const double ratio = tick_diff / peak;
        if(ratio > worst_tick_ratio)
            worst_tick_ratio = ratio;
    }

    char detail[96];
    std::snprintf(detail, sizeof(detail),
                  "worst tick/peak=%g across high and tuned pitches",
                  worst_tick_ratio);
    Check(valid && worst_tick_ratio < 0.25,
          "S10 subtle pitch-relative contact tick is present by default",
          detail);
}

void S11_BrightnessRange()
{
    const float dullest = bowls::BrightnessFromTicks(0);
    const float middle = bowls::BrightnessFromTicks(16);
    const float brightest = bowls::BrightnessFromTicks(32);
    char detail[96];
    std::snprintf(detail, sizeof(detail),
                  "ticks 0/16/32 -> %.2f/%.2f/%.2f",
                  dullest, middle, brightest);
    Check(dullest == 0.0f && middle == 0.75f && brightest == 1.5f,
          "S11 brightness keeps zero and extends max by 50%", detail);
}

void S12_MaxBrightnessStrengthensStrikeHighModes()
{
    std::vector<float> middle, brightest;
    for(int setting = 0; setting < 2; ++setting)
    {
        bowls::BowlVoice voice;
        FreshVoice(voice, setting == 0 ? 0.75f : 1.5f);
        voice.Strike(310.2f, 0.5f, 1.0f, 1.0f);
        Render(voice, 0.1, setting == 0 ? &middle : &brightest);
    }
    const double middle_brightness = ModalBrightness(middle, 310.2f);
    const double max_brightness = ModalBrightness(brightest, 310.2f);
    char detail[96];
    std::snprintf(detail, sizeof(detail), "mid=%.3f max=%.3f ratio=%.2f",
                  middle_brightness, max_brightness,
                  max_brightness / middle_brightness);
    Check(max_brightness >= 1.5 * middle_brightness,
          "S12 maximum brightness strengthens strike high modes", detail);
}

void S13_IdleLowerVoiceClearsActivityEnvelope()
{
    bowls::BowlVoice voice;
    voice.Init(kFs);
    voice.SetProfile(0);
    voice.SetBrightness(1.5f);
    voice.SetDecayMult(0.25f);
    voice.SetShimmerMult(0.0f);
    voice.Strike(219.6f, 1.0f, 1.0f, 1.0f);

    float left[kBlk] = {};
    float right[kBlk] = {};
    int samples = 0;
    while((voice.IsActive() || voice.HasTail())
          && samples < static_cast<int>(10.0f * kFs))
    {
        for(int i = 0; i < kBlk; ++i)
            left[i] = right[i] = 0.0f;
        voice.ProcessBlock(left, right, kBlk);
        samples += kBlk;
    }

    Check(!voice.HasTail() && !voice.IsActive(),
          "S13 idle lower voice clears its activity envelope", "");
}

void S14_HighRegisterAttackBuildsResonantBody()
{
    const float pitches[] = {927.0f, 1100.0f, 1560.0f};
    double      worst_attack_ratio = 0.0;
    bool        valid = true;
    for(float pitch : pitches)
    {
        bowls::BowlVoice voice;
        FreshVoice(voice);
        voice.Strike(pitch, 0.5f, 1.0f, 1.0f);
        std::vector<float> audio;
        const Stats stats = Render(voice, 0.1, &audio);
        const double attack_rms = WindowRms(audio, 0.0, 0.005);
        const double body_rms = WindowRms(audio, 0.005, 0.020);
        if(!stats.finite || body_rms <= 0.0)
        {
            valid = false;
            continue;
        }
        const double ratio = attack_rms / body_rms;
        if(ratio > worst_attack_ratio)
            worst_attack_ratio = ratio;
    }

    char detail[80];
    std::snprintf(detail, sizeof(detail), "worst 0-5ms / 5-20ms RMS = %.2f",
                  worst_attack_ratio);
    Check(valid && worst_attack_ratio < 2.0,
          "S14 high-register attack builds into resonant body", detail);
}

void S15_MutedModeRetune()
{
    bowls::BowlVoice voice;
    FreshVoice(voice);
    voice.Strike(850.0f, 0.75f, 1.0f, 1.0f);
    const Stats before = Render(voice, 0.05, nullptr);
    const bool tail_before = voice.HasTail();

    // At 900 Hz the Temple profile's highest pair is above the 0.45 fs
    // limit; returning to 850 Hz makes it audible again.
    voice.SelectForRub(900.0f, 1.0f);
    const bool tail_muted = voice.HasTail();
    const Stats muted = Render(voice, 0.05, nullptr);
    voice.SelectForRub(850.0f, 1.0f);
    const bool tail_enabled = voice.HasTail();
    const Stats enabled = Render(voice, 0.05, nullptr);

    const double peak = std::max(before.peak,
                                 std::max(muted.peak, enabled.peak));
    char         detail[64];
    std::snprintf(detail, sizeof(detail), "peak=%.3f", peak);
    Check(before.finite && muted.finite && enabled.finite && peak <= 1.5
              && tail_before && tail_muted && tail_enabled,
          "S15 muted mode retune stays finite and bounded", detail);
}

void S16_StagedExcitationSetup()
{
    const float old_pitches[] = {0.0f, 130.81f, 1100.0f};
    const float new_pitches[] = {130.81f, 880.0f, 100.0f};
    double      max_diff = 0.0;
    bool        finite = true;

    for(size_t scenario = 0;
        scenario < sizeof(old_pitches) / sizeof(old_pitches[0]);
        ++scenario)
    {
        std::vector<float> immediate, staged;
        for(int path = 0; path < 2; ++path)
        {
            bowls::BowlVoice voice;
            FreshVoice(voice);
            if(old_pitches[scenario] > 0.0f)
            {
                voice.Strike(old_pitches[scenario], 0.75f, 1.0f, 1.0f);
                Render(voice, 0.08, nullptr);
            }

            voice.SetProfile(4);
            voice.SetBrightness(1.25f);
            if(path == 0)
            {
                voice.SetDecayMult(2.4f);
                voice.SetShimmerMult(1.4f);
            }
            else
            {
                voice.SetDecayMultForNextExcitation(2.4f);
                voice.SetShimmerMultForNextExcitation(1.4f);
            }

            voice.StrikeWithAmplitude(new_pitches[scenario],
                                      0.8f,
                                      0.6f,
                                      1.0f,
                                      1.0f,
                                      0.02f);
            const Stats stats = Render(
                voice, 0.3, path == 0 ? &immediate : &staged);
            finite = finite && stats.finite;
        }

        if(immediate.size() != staged.size())
        {
            finite = false;
            continue;
        }
        for(size_t i = 0; i < immediate.size(); ++i)
        {
            const double diff
                = std::fabs(static_cast<double>(immediate[i] - staged[i]));
            if(diff > max_diff)
                max_diff = diff;
        }
    }

    char detail[64];
    std::snprintf(detail, sizeof(detail), "max|diff|=%g", max_diff);
    Check(finite && max_diff == 0.0,
          "S16 staged strike/steal setup is sample-identical", detail);
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
    S10_DefaultContactTickPresent();
    S11_BrightnessRange();
    S12_MaxBrightnessStrengthensStrikeHighModes();
    S13_IdleLowerVoiceClearsActivityEnvelope();
    S14_HighRegisterAttackBuildsResonantBody();
    S15_MutedModeRetune();
    S16_StagedExcitationSetup();
    std::printf(g_failures == 0 ? "\nALL STRIKE TESTS PASSED\n"
                                : "\n%d STRIKE TEST(S) FAILED\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
