// Strike-vs-reference analysis for the striking-character work
// (plans/improve_striking_character_plan.md). Host tool, not part of
// the firmware build; run via `make -C code/tests analyze` (compare
// mode) from the MSYS2 UCRT64 shell.
//
// Usage:
//   ./strike_analyze.exe compare
//       Both reference recordings vs current-engine renders at the
//       matched reference pitches and panel defaults (Strike 16,
//       Brightness 16, Decay 16, Shimmer 8, profile Temple), plus the
//       acceptance metrics (A, B below).
//   ./strike_analyze.exe ref <wav> <f1_hz> <f2_hz>
//       Analyze one reference recording with its measured mode freqs.
//   ./strike_analyze.exe render <fund_hz> <strike 0..1> <bright 0..1>
//          <profile 0..4> [decay_mult] [shimmer_mult] [force_s] [noise_s]
//       Render one strike with the current BowlVoice (force_s/noise_s
//       are the SetStrikeDiagnostics scalers: 0/1 = force-only,
//       1/0 = noise-only).
//
// Method (identical windows on references and renders):
//   - Renders strike at t=0 with NO pre-roll; references are
//     onset-aligned (first 5 ms RMS window at -20 dB of the file max).
//   - Windowed energies (sum of squares, mono): 0-20, 20-100,
//     100-500 ms and the ring window 0.5-2.0 s.
//   - A = 10*log10(E(0-100 ms) / E(ring 0.5-2 s))   - acceptance 1
//   - B = 10*log10(P(mode 2) / P(mode 1)) in the 0-93 ms early window
//     (Goertzel at the mode frequencies; pair splitting unresolved,
//     like the reference measurements)                      - acceptance 2
//   - Early spectral centroid (4096-sample Hann at the native rate -
//     93 ms at 44.1 kHz, 85 ms at 48 kHz) and the >3 kHz energy share.
//   - Modal ratio is repeated in the ring window as a decay sanity
//     check. References keep their native 44.1 kHz; no resampling.

#include "bowl_profile.h"
#include "bowl_voice.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
constexpr double kPiD = 3.14159265358979;

// ---------------------------------------------------------------------------
// WAV reader (PCM, 16/24-bit, any channel count -> mono average)
// ---------------------------------------------------------------------------

uint32_t Rd32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
           | (static_cast<uint32_t>(p[2]) << 16)
           | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t Rd16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

bool ReadWav(const char* path, std::vector<float>& out, double& fs_out)
{
    FILE* f = std::fopen(path, "rb");
    if(!f)
    {
        std::printf("cannot open %s\n", path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if(std::fread(buf.data(), 1, static_cast<size_t>(size), f)
       != static_cast<size_t>(size))
    {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    if(size < 44 || std::memcmp(buf.data(), "RIFF", 4) != 0
       || std::memcmp(buf.data() + 8, "WAVE", 4) != 0)
    {
        std::printf("%s: not a RIFF/WAVE file\n", path);
        return false;
    }
    uint16_t channels = 0, bits = 0, format = 0;
    uint32_t rate = 0;
    size_t   data_off = 0;
    uint32_t data_len = 0;
    size_t   pos      = 12;
    while(pos + 8 <= buf.size())
    {
        const uint32_t csize = Rd32(&buf[pos + 4]);
        if(std::memcmp(&buf[pos], "fmt ", 4) == 0)
        {
            format   = Rd16(&buf[pos + 8]);
            channels = Rd16(&buf[pos + 10]);
            rate     = Rd32(&buf[pos + 12]);
            bits     = Rd16(&buf[pos + 22]);
        }
        else if(std::memcmp(&buf[pos], "data", 4) == 0)
        {
            data_off = pos + 8;
            data_len = csize;
        }
        pos += 8 + csize + (csize & 1); // chunks are word-aligned
    }
    if(format != 1 || channels == 0 || rate == 0 || data_len == 0
       || (bits != 16 && bits != 24))
    {
        std::printf("%s: unsupported WAV (fmt %u, %u ch, %u Hz, %u bit)\n",
                    path, format, channels, rate, bits);
        return false;
    }
    const size_t frame_bytes = channels * (bits / 8);
    const size_t frames      = data_len / frame_bytes;
    out.assign(frames, 0.0f);
    for(size_t i = 0; i < frames; ++i)
    {
        double m = 0.0;
        for(uint16_t c = 0; c < channels; ++c)
        {
            const uint8_t* s
                = &buf[data_off + i * frame_bytes + c * (bits / 8)];
            if(bits == 16)
            {
                const int16_t v = static_cast<int16_t>(Rd16(s));
                m += static_cast<double>(v) * (1.0 / 32768.0);
            }
            else
            {
                int32_t v = s[0] | (s[1] << 8) | (s[2] << 16);
                if(v & 0x800000)
                    v -= 0x1000000;
                m += static_cast<double>(v) * (1.0 / 8388608.0);
            }
        }
        out[i] = static_cast<float>(m / channels);
    }
    fs_out = static_cast<double>(rate);
    return true;
}

// ---------------------------------------------------------------------------
// Small FFT (iterative radix-2, magnitude/power spectra for the early window)
// ---------------------------------------------------------------------------

void Fft(std::vector<double>& re, std::vector<double>& im)
{
    const size_t n = re.size();
    for(size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for(; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if(i < j)
        {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for(size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPiD / static_cast<double>(len);
        const double wr = std::cos(ang), wi = std::sin(ang);
        for(size_t i = 0; i < n; i += len)
        {
            double cwr = 1.0, cwi = 0.0;
            for(size_t j = 0; j < len / 2; ++j)
            {
                const size_t a = i + j, b = i + j + len / 2;
                const double tr = re[b] * cwr - im[b] * cwi;
                const double ti = re[b] * cwi + im[b] * cwr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
                const double nwr = cwr * wr - cwi * wi;
                cwi              = cwr * wi + cwi * wr;
                cwr              = nwr;
            }
        }
    }
}

double GoertzelPower(const float* x, size_t n, double fs, double f)
{
    const double w  = 2.0 * kPiD * f / fs;
    const double cw = 2.0 * std::cos(w);
    double       s1 = 0.0, s2 = 0.0;
    for(size_t i = 0; i < n; ++i)
    {
        const double s0 = static_cast<double>(x[i]) + cw * s1 - s2;
        s2              = s1;
        s1              = s0;
    }
    return s1 * s1 + s2 * s2 - cw * s1 * s2;
}

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------

struct Metrics
{
    double onset_s = 0.0;
    double a_db    = 0.0; // E(0-100 ms) / E(ring 0.5-4 s), dB
    double b_db    = 0.0; // mode2/mode1 power, early window, dB
    double b3_db   = 0.0; // mode3/mode1 power, early window, dB
    double b_ring_db = 0.0; // same in the ring window
    double t60_f1    = 0.0; // mode-1 decay estimate from the ring, s
    double centroid_hz = 0.0;
    double hi_db       = 0.0; // >3 kHz share of early spectrum, dB
    double e0_20 = 0.0, e20_100 = 0.0, e100_500 = 0.0, ering = 0.0;
    double ring_rms = 0.0;
    double peak     = 0.0;
    bool   ok       = false;
};

double Energy(const float* x, size_t n)
{
    double e = 0.0;
    for(size_t i = 0; i < n; ++i)
        e += static_cast<double>(x[i]) * x[i];
    return e;
}

double Db(double ratio)
{
    return 10.0 * std::log10(ratio + 1.0e-30);
}

Metrics Analyze(const float* xs, size_t ntotal, double fs, double f1,
                double f2, double f3, bool onset_known)
{
    Metrics m;
    size_t onset = 0;
    if(!onset_known)
    {
        // First 5 ms RMS window reaching -20 dB of the file's max.
        const size_t w = static_cast<size_t>(0.005 * fs);
        double       maxe = 0.0;
        std::vector<double> we;
        for(size_t base = 0; base + w <= ntotal; base += w)
        {
            const double e = Energy(xs + base, w);
            we.push_back(e);
            if(e > maxe)
                maxe = e;
        }
        const double thr = maxe * 0.01; // -20 dB in energy == -20 dB RMS
        for(size_t k = 0; k < we.size(); ++k)
            if(we[k] >= thr)
            {
                onset = k * w;
                break;
            }
    }
    m.onset_s = static_cast<double>(onset) / fs;
    const float* x  = xs + onset;
    const size_t n  = ntotal - onset;
    const size_t w20  = static_cast<size_t>(0.020 * fs);
    const size_t w100 = static_cast<size_t>(0.100 * fs);
    const size_t w500 = static_cast<size_t>(0.500 * fs);
    const size_t w4s  = static_cast<size_t>(4.0 * fs);
    if(n < w4s)
        return m;

    // Ring level window 0.5-4.0 s: long enough to average the mode-pair
    // beat (a deep null inside a short window would corrupt the
    // normalization) and the reference's slow amplitude wander.
    m.e0_20   = Energy(x, w20);
    m.e20_100 = Energy(x + w20, w100 - w20);
    m.e100_500 = Energy(x + w100, w500 - w100);
    m.ering   = Energy(x + w500, w4s - w500);
    m.a_db    = 10.0 * std::log10((m.e0_20 + m.e20_100) / m.ering);
    m.ring_rms = std::sqrt(m.ering / static_cast<double>(w4s - w500));
    for(size_t i = 0; i < w4s; ++i)
    {
        const double a = std::fabs(static_cast<double>(x[i]));
        if(a > m.peak)
            m.peak = a;
    }

    // Modal bands (Goertzel): early 0-93 ms and the ring window.
    const size_t we93 = static_cast<size_t>(0.093 * fs);
    const double pe1  = GoertzelPower(x, we93, fs, f1);
    const double pe2  = GoertzelPower(x, we93, fs, f2);
    const double pe3  = GoertzelPower(x, we93, fs, f3);
    const double pr1  = GoertzelPower(x + w500, w4s - w500, fs, f1);
    const double pr2  = GoertzelPower(x + w500, w4s - w500, fs, f2);
    m.b_db      = 10.0 * std::log10(pe2 / pe1);
    m.b3_db     = 10.0 * std::log10(pe3 / pe1);
    m.b_ring_db = 10.0 * std::log10(pr2 / pr1);

    // Mode-1 T60 from Goertzel power in two ring sub-windows centered
    // 1 s apart (power decays at twice the amplitude rate). Used to
    // decay-match the comparison renders to a reference.
    const double pg1 = GoertzelPower(x + w500, w500, fs, f1);
    const double pg2
        = GoertzelPower(x + w500 + static_cast<size_t>(fs), w500, fs, f1);
    if(pg2 > 0.0 && pg1 > pg2)
        m.t60_f1 = 2.0 * 6.9078 * 1.0 / std::log(pg1 / pg2);

    // Early spectrum: 4096-sample Hann at the native rate.
    const size_t nf = 4096;
    std::vector<double> re(nf), im(nf, 0.0);
    for(size_t i = 0; i < nf; ++i)
    {
        const double h
            = 0.5 - 0.5 * std::cos(2.0 * kPiD * i / (nf - 1));
        re[i] = static_cast<double>(x[i]) * h;
    }
    Fft(re, im);
    double sum_mag = 0.0, sum_fmag = 0.0, hi_e = 0.0, tot_e = 0.0;
    for(size_t k = 1; k < nf / 2; ++k)
    {
        const double f   = static_cast<double>(k) * fs / nf;
        const double mag = std::sqrt(re[k] * re[k] + im[k] * im[k]);
        sum_mag += mag;
        sum_fmag += f * mag;
        const double p = mag * mag;
        tot_e += p;
        if(f > 3000.0)
            hi_e += p;
    }
    m.centroid_hz = sum_mag > 0.0 ? sum_fmag / sum_mag : 0.0;
    m.hi_db       = 10.0 * std::log10(hi_e / tot_e);
    m.ok          = true;
    return m;
}

void Print(const char* tag, const Metrics& m)
{
    std::printf("  %-26s onset=%.3fs peak=%.3f ringRMS=%.5f T60(f1)=%.1fs\n",
                tag, m.onset_s, m.peak, m.ring_rms, m.t60_f1);
    std::printf("  %-26s A(0-100ms/ring)=%+.1f dB  B(mode2/mode1 "
                "early)=%+.1f dB (ring %+.1f dB)  mode3/mode1=%+.1f dB\n",
                "", m.a_db, m.b_db, m.b_ring_db, m.b3_db);
    std::printf("  %-26s centroid(early)=%.0f Hz  >3kHz share=%.1f dB\n",
                "", m.centroid_hz, m.hi_db);
}

/** Time-localized early energy: 0-2 / 2-10 / 10-100 ms and the ring,
 *  each in dB relative to 1 s of ring-window energy rate. */
void PrintWindows(const char* tag, const float* xs, size_t ntotal,
                  double fs, double onset_s)
{
    const size_t onset = static_cast<size_t>(onset_s * fs);
    const float* x     = xs + onset;
    const size_t n     = ntotal - onset;
    const size_t w2    = static_cast<size_t>(0.002 * fs);
    const size_t w10   = static_cast<size_t>(0.010 * fs);
    const size_t w100  = static_cast<size_t>(0.100 * fs);
    const size_t w500  = static_cast<size_t>(0.500 * fs);
    const size_t w4s   = static_cast<size_t>(4.0 * fs);
    if(n < w4s)
        return;
    const double ring_rate = Energy(x + w500, w4s - w500)
                             / static_cast<double>(w4s - w500);
    std::printf("  %-26s E(0-2ms)=%+.1f E(2-10)=%+.1f E(10-100)=%+.1f "
                "dB rel ring rate\n",
                tag,
                Db(Energy(x, w2) / static_cast<double>(w2) / ring_rate),
                Db(Energy(x + w2, w10 - w2) / static_cast<double>(w10 - w2)
                   / ring_rate),
                Db(Energy(x + w10, w100 - w10)
                   / static_cast<double>(w100 - w10) / ring_rate));
}

// Render one strike at 48 kHz / 24-sample blocks, exactly like the
// firmware's StrikeKey path (unity jitters, 0.25 contact damp target).
void RenderStrike(float fund, float snorm, float bright, int profile,
                  float decay, float shimmer, float force_s, float noise_s,
                  std::vector<float>& out)
{
    constexpr float kFs = 48000.0f;
    constexpr int   kBlk = 24;
    bowls::BowlVoice v;
    v.Init(kFs);
    v.SetProfile(profile);
    v.SetBrightness(bright);
    v.SetDecayMult(decay);
    v.SetShimmerMult(shimmer);
    v.SetStrikeDiagnostics(force_s, noise_s);
    v.Strike(fund, snorm, 1.0f, 1.0f);
    const int blocks = static_cast<int>(4.5 * kFs) / kBlk;
    float      l[kBlk], r[kBlk];
    out.clear();
    for(int b = 0; b < blocks; ++b)
    {
        for(int i = 0; i < kBlk; ++i)
        {
            l[i] = 0.0f;
            r[i] = 0.0f;
        }
        v.ProcessBlock(l, r, kBlk);
        for(int i = 0; i < kBlk; ++i)
            out.push_back(0.5f * (l[i] + r[i]));
    }
}

/** 5 ms RMS windows for the onset, in dB relative to the ring-window
 *  rate - the actual attack shape (the plan's 5 ms RMS method). */
void PrintEnvelope(const char* tag, const float* xs, size_t ntotal,
                   double fs, double onset_s)
{
    const size_t onset = static_cast<size_t>(onset_s * fs);
    const float* x     = xs + onset;
    const size_t n     = ntotal - onset;
    const size_t w     = static_cast<size_t>(0.005 * fs);
    const size_t w500  = static_cast<size_t>(0.500 * fs);
    const size_t w4s   = static_cast<size_t>(4.0 * fs);
    if(n < w4s)
        return;
    const double ring_rate = Energy(x + w500, w4s - w500)
                             / static_cast<double>(w4s - w500);
    std::printf("  %-14s 5 ms RMS rel ring:", tag);
    for(int k = 0; k < 12; ++k)
        std::printf(" %6.1f",
                    Db(Energy(x + k * w, w) / static_cast<double>(w)
                       / ring_rate));
    std::printf("\n  %-14s at 0.25/0.5/1/2/3/4 s:", "");
    const double checkpoints[6] = {0.25, 0.5, 1.0, 2.0, 3.0, 4.0};
    for(int k = 0; k < 6; ++k)
    {
        const size_t o = static_cast<size_t>(checkpoints[k] * fs);
        std::printf(" %6.1f", Db(Energy(x + o, w) / static_cast<double>(w)
                                 / ring_rate));
    }
    std::printf("\n");
}

struct Ref
{
    const char* path;
    const char* name;
    double      f1, f2, f3; // measured reference resonances
    double      fund;       // matched render pitch
};

const Ref kRefs[2] = {
    {"../../Audiofiles/Kasper - Singing Bowls - 04 Bowl 1 Articulation 1 "
     "Microphone 1.wav",
     "Bowl 1 ref", 243.6, 653.4, 1194.8, 243.6},
    {"../../Audiofiles/Kasper - Singing Bowls - 20 Bowl 3 Articulation 1 "
     "Microphone 1.wav",
     "Bowl 3 ref", 369.4, 1031.9, 1926.9, 369.4},
};

int Compare()
{
    for(int k = 0; k < 2; ++k)
    {
        const Ref& r = kRefs[k];
        std::printf("%s (%s, f1=%.1f f2=%.1f f3=%.1f)\n", r.name,
                    "reference", r.f1, r.f2, r.f3);
        std::vector<float> wav;
        double             fs;
        if(!ReadWav(r.path, wav, fs))
            return 1;
        const Metrics rm = Analyze(wav.data(), wav.size(), fs, r.f1,
                                   r.f2, r.f3, false);
        Print("reference", rm);
        PrintEnvelope("reference", wav.data(), wav.size(), fs, rm.onset_s);
        PrintWindows("reference", wav.data(), wav.size(), fs, rm.onset_s);

        // Matched render: panel defaults (Strike/Brightness 16,
        // Shimmer 8, profile Temple) at the reference pitch, with the
        // decay set to the reference's measured mode-1 T60 so the
        // attack-to-ring comparison is not polluted by a decay
        // mismatch (the panel's default decay is much shorter than
        // these recordings' rings).
        const float decay = static_cast<float>(
            rm.t60_f1 > 0.0 ? rm.t60_f1 / 12.0 : 1.0); // Temple mode 1
        std::vector<float> ren;
        RenderStrike(static_cast<float>(r.fund), 0.5f, 0.5f, 0,
                     decay < 0.25f ? 0.25f : (decay > 2.0f ? 2.0f : decay),
                     0.5f, 1.0f, 1.0f, ren);
        const Metrics mm = Analyze(ren.data(), ren.size(), 48000.0, r.fund,
                                   r.fund * 2.8, r.fund * 5.2, true);
        Print("render (decay-matched)", mm);
        PrintEnvelope("render", ren.data(), ren.size(), 48000.0, 0.0);
        PrintWindows("render (defaults)", ren.data(), ren.size(), 48000.0,
                     0.0);
        std::printf("  %-26s A error %+.1f dB, B error %+.1f dB "
                    "(render - reference)\n\n",
                    "delta", mm.a_db - rm.a_db, mm.b_db - rm.b_db);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if(argc >= 2 && std::strcmp(argv[1], "compare") == 0)
        return Compare();
    if(argc >= 5 && std::strcmp(argv[1], "ref") == 0)
    {
        std::vector<float> wav;
        double             fs;
        if(!ReadWav(argv[2], wav, fs))
            return 1;
        const double f3 = argc > 5 ? std::atof(argv[5])
                                   : 1.83 * std::atof(argv[4]);
        Print("reference",
              Analyze(wav.data(), wav.size(), fs, std::atof(argv[3]),
                      std::atof(argv[4]), f3, false));
        return 0;
    }
    if(argc >= 6 && std::strcmp(argv[1], "render") == 0)
    {
        const float fund   = static_cast<float>(std::atof(argv[2]));
        const float snorm  = static_cast<float>(std::atof(argv[3]));
        const float bright = static_cast<float>(std::atof(argv[4]));
        const int   prof   = std::atoi(argv[5]);
        const float decay  = argc > 6 ? static_cast<float>(std::atof(argv[6])) : 0.7071f;
        const float shim   = argc > 7 ? static_cast<float>(std::atof(argv[7])) : 0.5f;
        const float force  = argc > 8 ? static_cast<float>(std::atof(argv[8])) : 1.0f;
        const float noise  = argc > 9 ? static_cast<float>(std::atof(argv[9])) : 1.0f;
        std::vector<float> ren;
        RenderStrike(fund, snorm, bright, prof, decay, shim, force, noise,
                     ren);
        const bowls::BowlProfile& p = bowls::GetBowlProfile(prof);
        const Metrics mm = Analyze(ren.data(), ren.size(), 48000.0,
                                   fund * p.ratio[0], fund * p.ratio[1],
                                   fund * p.ratio[2], true);
        Print("render", mm);
        PrintEnvelope("render", ren.data(), ren.size(), 48000.0, 0.0);
        PrintWindows("render", ren.data(), ren.size(), 48000.0, 0.0);
        return 0;
    }
    std::printf("usage: compare | ref <wav> <f1> <f2> | render <fund> "
                "<strike> <bright> <profile> [decay] [shimmer] [force] "
                "[noise]\n");
    return 1;
}
