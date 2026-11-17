// Host-side tests for the clean-pad gate/envelope in bowl_voice.h.
// Run with `make -C code/tests modes-test` from the MSYS2 UCRT64 shell.
//
//   P1  pad attack: from silence the pad-drive envelope reaches
//       0.90 +/- 0.005 after exactly 10 s
//   P2  pad release: the envelope reaches 0.10 +/- 0.005 of its
//       note-off level after exactly 10 s; the resonator ring and
//       tails are NOT cut by the envelope
//   P3  retrigger starts from the current envelope value (no reset)
//   P4  a sustained pad produces finite, bounded, audible output
//   P5  pad + full wheel drive (both characters) stays finite and
//       bounded - the mix can never double the maximum excitation
//   P6  pad gate never opened: PadEnvelope stays exactly 0 (the
//       wheel-only DSP path is untouched; bit-identity vs the accepted
//       baseline is guarded by render_test T0)
//   P7  Strike() takes a voice over completely: a leftover pad gate is
//       cleared (a stolen Rimming voice never keeps its pad)
//   P8  a gated pad voice is Active from the first block (voice
//       reservation while the slow attack is still inaudible)
//   P9  a STOLEN voice's leftover ring is ghost-faded away (~-60 dB
//       over 100 ms): no instant loud pad, the 10 s attack is intact
//       and the pad blooms afterwards (the pad envelope gates only the
//       pad DRIVE, not the resonator output - StartPadKey passes a
//       damp target for stolen voices)
//   P10 the steal transition is click-free (no sample-step)
//   P11 a plain rim re-selection (no damp target) keeps the ring

#include "bowl_voice.h"
#include "bowl_scale.h"

#include <cmath>
#include <cstdio>

namespace
{
constexpr float kFs  = 48000.0f;
constexpr int   kBlk = 24;

int g_failures = 0;

void Check(bool ok, const char* name, const char* detail)
{
    std::printf("%-46s %s  %s\n", name, ok ? "PASS" : "FAIL", detail);
    if(!ok)
        ++g_failures;
}

void Check(bool ok, const char* name, double value)
{
    char detail[64];
    std::snprintf(detail, sizeof(detail), "%.6f", value);
    Check(ok, name, detail);
}

struct Out
{
    double peak   = 0.0;
    double delta  = 0.0;
    double rms    = 0.0;
    bool   finite = true;
};

/** Render `samples` samples in 24-sample blocks; measure peak, max
 *  sample-to-sample jump and RMS over the whole run. */
Out Run(bowls::BowlVoice& v, long samples)
{
    float l[kBlk], r[kBlk];
    Out   o;
    float prev = 0.0f;
    bool  have = false;
    double sum = 0.0;
    long   cnt = 0;
    for(long base = 0; base < samples; base += kBlk)
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
            if(!std::isfinite(m))
                o.finite = false;
            const double am = std::fabs(static_cast<double>(m));
            if(am > o.peak)
                o.peak = am;
            if(have)
            {
                const double d = std::fabs(static_cast<double>(m) - prev);
                if(d > o.delta)
                    o.delta = d;
            }
            prev = m;
            have = true;
            sum += static_cast<double>(m) * m;
            ++cnt;
        }
    }
    o.rms = cnt > 0 ? std::sqrt(sum / static_cast<double>(cnt)) : 0.0;
    return o;
}

bowls::BowlVoice* FreshPad(bowls::BowlVoice& v, float fundamental)
{
    v.Init(kFs);
    v.SelectForRub(fundamental, 1.0f);
    v.SetPadGate(true);
    return &v;
}

} // namespace

int main()
{
    // P1: attack reaches 0.90 +/- 0.005 at exactly 10 s.
    {
        bowls::BowlVoice v;
        FreshPad(v, 220.0f);
        Run(v, static_cast<long>(10.0 * kFs));
        const double env = v.PadEnvelope();
        Check(std::fabs(env - 0.90) <= 0.005, "P1 pad attack 0.90 at 10 s", env);
    }

    // P2: release reaches 0.10 +/- 0.005 of the note-off level at 10 s,
    // and the resonator ring is not cut by the envelope.
    {
        bowls::BowlVoice v;
        FreshPad(v, 220.0f);
        Run(v, static_cast<long>(20.0 * kFs)); // attack converges ~1
        const double note_off = v.PadEnvelope();
        v.SetPadGate(false);
        Run(v, static_cast<long>(10.0 * kFs));
        const double env  = v.PadEnvelope();
        const double frac = env / note_off;
        Check(std::fabs(frac - 0.10) <= 0.005,
              "P2 pad release 0.10 at 10 s", frac);
        Check(v.HasTail(), "P2b resonator ring survives the release", "");
    }

    // P3: retrigger from the current value (no reset).
    {
        bowls::BowlVoice v;
        FreshPad(v, 220.0f);
        Run(v, static_cast<long>(5.0 * kFs));
        v.SetPadGate(false);
        Run(v, static_cast<long>(5.0 * kFs)); // partial release
        const double before = v.PadEnvelope();
        v.SetPadGate(true);
        Run(v, kBlk); // one block of re-attack
        const double after = v.PadEnvelope();
        Check(before > 0.05 && before < 0.9,
              "P3 partial release leaves a mid envelope", before);
        Check(after >= before && after < before + 0.01,
              "P3b retrigger resumes from current value", after);
    }

    // P4: sustained pad output is finite, bounded and audible.
    {
        bowls::BowlVoice v;
        FreshPad(v, 220.0f);
        Run(v, static_cast<long>(20.0 * kFs)); // past the attack
        const Out o = Run(v, static_cast<long>(4.0 * kFs));
        Check(o.finite && o.peak < 2.0, "P4 pad bounded", o.peak);
        Check(o.rms > 0.005, "P4b pad audible", o.rms);
        // Low and high pads too (register sweep).
        bool ok = true;
        for(int k = 0; k < bowls::kKeyCount; k += 6)
        {
            bowls::BowlVoice w;
            FreshPad(w, bowls::FundamentalHz(bowls::PlayMode::kRimming, k));
            Run(w, static_cast<long>(1.0 * kFs));
            const Out oo = Run(w, static_cast<long>(1.0 * kFs));
            if(!oo.finite || oo.peak > 2.0)
                ok = false;
        }
        Check(ok, "P4c pad register sweep bounded", "");

        bool range_ok = true;
        const float bases[] = {40.0f, 10000.0f};
        const float tunes[] = {0.5f, 1.0f, 2.0f};
        for(int profile = 0; profile < bowls::kNumProfiles; ++profile)
            for(float base : bases)
                for(float tune : tunes)
                {
                    bowls::BowlVoice endpoint;
                    endpoint.Init(kFs);
                    endpoint.SetProfile(profile);
                    endpoint.SelectForRub(base * tune, 1.0f);
                    endpoint.SetPadGate(true);
                    const Out output = Run(endpoint,
                                           static_cast<long>(0.1 * kFs));
                    if(!output.finite || output.peak > 2.0)
                        range_ok = false;
                }
        Check(range_ok,
              "P4d 40-10000 Hz pad range across profiles bounded", "");
    }

    // P5: pad + full wheel drive - the bounded mix can never exceed
    // the wheel path's own maximum excitation.
    {
        // At full CLEAN wheel level the wheel drive is already at its
        // maximum, so the pad's contribution is ~zero: pad + wheel must
        // not exceed the wheel-only render's level (within float-slew
        // residue - the resonator's sustained gain amplifies a ~1e-7
        // drive difference to ~-45 dB below peak).
        bowls::BowlVoice a, b;
        FreshPad(a, 310.2f);
        a.SetRub(1.0f, 1.0f);
        b.Init(kFs);
        b.SelectForRub(310.2f, 1.0f);
        b.SetRub(1.0f, 1.0f);
        Run(a, static_cast<long>(6.0 * kFs)); // converge rub + char
        Run(b, static_cast<long>(6.0 * kFs));
        const Out oa = Run(a, static_cast<long>(2.0 * kFs));
        const Out ob = Run(b, static_cast<long>(2.0 * kFs));
        Check(oa.finite, "P5 pad+clean wheel finite", "");
        Check(oa.peak <= ob.peak * 1.01 + 1.0e-3,
              "P5b pad cannot exceed the wheel maximum", oa.peak);

        // Pad + full FRICTION wheel: linear superposition bounds the
        // sum by the separate peaks (same resonator bank).
        bowls::BowlVoice c, d, e;
        FreshPad(c, 310.2f);
        c.SetRub(1.0f, 0.0f);
        d.Init(kFs);
        d.SelectForRub(310.2f, 1.0f);
        d.SetRub(1.0f, 0.0f);
        FreshPad(e, 310.2f);
        Run(c, static_cast<long>(6.0 * kFs));
        Run(d, static_cast<long>(6.0 * kFs));
        Run(e, static_cast<long>(6.0 * kFs));
        const Out oc = Run(c, static_cast<long>(2.0 * kFs));
        const Out od = Run(d, static_cast<long>(2.0 * kFs));
        const Out oe = Run(e, static_cast<long>(2.0 * kFs));
        Check(oc.finite, "P5c pad+friction finite", "");
        Check(oc.peak <= od.peak + oe.peak + 0.1,
              "P5d pad+friction bounded by the separate peaks", oc.peak);
    }

    // P6: pad gate never opened - envelope stays exactly zero.
    {
        bowls::BowlVoice v;
        v.Init(kFs);
        v.SelectForRub(220.0f, 1.0f);
        v.SetRub(1.0f, 1.0f);
        Run(v, static_cast<long>(1.0 * kFs));
        Check(v.PadEnvelope() == 0.0f && !v.PadGate(),
              "P6 wheel-only path keeps pad envelope at 0", "");
    }

    // P7: Strike() clears a leftover pad gate (voice takeover).
    {
        bowls::BowlVoice v;
        FreshPad(v, 220.0f);
        Run(v, static_cast<long>(1.0 * kFs));
        v.Strike(220.0f, 0.5f, 1.0f, 1.0f);
        Check(!v.PadGate() && v.PadEnvelope() == 0.0f,
              "P7 strike clears the pad gate", "");
    }

    // P8: a gated pad voice is Active from the first block.
    {
        bowls::BowlVoice v;
        FreshPad(v, 220.0f);
        Check(v.IsActive(), "P8 gated pad reserved before audibility", "");
        float l[kBlk], r[kBlk];
        for(int i = 0; i < kBlk; ++i)
        {
            l[i] = 0.0f;
            r[i] = 0.0f;
        }
        v.ProcessBlock(l, r, kBlk);
        Check(v.IsActive(), "P8b still reserved after first block", "");
    }

    // P9: a stolen voice's leftover ring must not bypass the pad
    // attack (the reported Rimming bug: 6th key steals the quietest
    // voice and the new pad sometimes started instantly - the stolen
    // ring is retuned but never gated by pad_env_).
    {
        bowls::BowlVoice v;
        v.Init(kFs);
        v.Strike(220.0f, 1.0f, 1.0f, 1.0f, 0.25f); // loud ring
        Run(v, static_cast<long>(0.5 * kFs));
        const Out before = Run(v, static_cast<long>(0.1 * kFs));
        Check(before.peak > 0.02, "P9 pre-steal ring is loud", before.peak);
        // The exact StartPadKey steal sequence: retune to a NEW pitch
        // with the ghost fade armed, then open the pad gate.
        v.SelectForRub(330.0f, 1.0f, 0.001f);
        v.SetPadGate(true);
        Run(v, static_cast<long>(0.5 * kFs)); // fade long finished
        const Out settled = Run(v, static_cast<long>(0.1 * kFs));

        // Control: the SAME steal WITHOUT the damp target (the old
        // buggy behavior) keeps ringing at the retuned pitch. The
        // engine is deterministic (fixed seed), so the two renders
        // diverge only through the fade.
        bowls::BowlVoice u;
        u.Init(kFs);
        u.Strike(220.0f, 1.0f, 1.0f, 1.0f, 0.25f);
        Run(u, static_cast<long>(0.6 * kFs));
        u.SelectForRub(330.0f, 1.0f); // no damp target
        u.SetPadGate(true);
        Run(u, static_cast<long>(0.5 * kFs));
        const Out unfaded = Run(u, static_cast<long>(0.1 * kFs));

        Check(settled.finite && settled.peak < 0.02
                  && settled.peak < 0.25 * unfaded.peak,
              "P9b ghost faded, no instant loud pad", settled.peak);
        // The slow attack itself is untouched: 10.0 s of gate so far.
        Run(v, static_cast<long>(9.4 * kFs));
        const double env = v.PadEnvelope();
        Check(std::fabs(env - 0.90) <= 0.005,
              "P9c attack still 0.90 at 10 s", env);
        const Out late = Run(v, static_cast<long>(0.5 * kFs));
        Check(late.rms > 4.0 * settled.rms,
              "P9d pad blooms after the fade", late.rms);
    }

    // P10: the steal transition itself is click-free - the ghost fade
    // never steps the resonator states (a hard zero would tick).
    {
        bowls::BowlVoice v;
        v.Init(kFs);
        v.Strike(220.0f, 1.0f, 1.0f, 1.0f, 0.25f);
        Run(v, static_cast<long>(0.5 * kFs));
        const Out before = Run(v, static_cast<long>(0.1 * kFs));
        v.SelectForRub(330.0f, 1.0f, 0.001f);
        v.SetPadGate(true);
        const Out during = Run(v, static_cast<long>(0.15 * kFs));
        Check(during.finite && during.delta <= 1.5 * before.delta + 1.0e-3,
              "P10 steal boundary click-free", during.delta);
    }

    // P11: default SelectForRub keeps the still-ringing state - the
    // Split-mode rim-selection continuity is intentional and unchanged.
    {
        bowls::BowlVoice v;
        v.Init(kFs);
        v.Strike(220.0f, 1.0f, 1.0f, 1.0f, 0.25f);
        Run(v, static_cast<long>(0.5 * kFs));
        const Out before = Run(v, static_cast<long>(0.1 * kFs));
        v.SelectForRub(330.0f, 1.0f); // no damp target: rim re-selection
        const Out after = Run(v, static_cast<long>(0.2 * kFs));
        Check(after.peak > 0.5 * before.peak,
              "P11 plain rim select keeps the ring", after.peak);
    }

    std::printf("%s (%d failures)\n",
                g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
