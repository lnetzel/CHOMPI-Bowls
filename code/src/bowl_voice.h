#pragma once

// Modal singing-bowl voice for CHOMPI-BOWLS (spec: plans/CHOMPI-BOWLS-plan.md).
//
// One voice = one independently struck bowl: eight vibration modes, each
// realised as a PAIR of second-order resonators (16 total). The pair
// members are detuned by a few Hz, which produces the characteristic
// beating/shimmer of real bowls without any LFO.
//
//   y[n] = g * x[n] + c1 * y[n-1] - c2 * y[n-2]
//   c1 = 2 r cos(w), c2 = r^2, w = 2 pi f / fs
//   r   = exp(-6.9078 / (T60 * fs))   (-60 dB at T60 seconds)
//   g   = sin(w)                      (equal peak response across modes: a
//                                      strike puts the same energy into a
//                                      mode no matter how long it rings;
//                                      the level comes from the excitation
//                                      weight, the sustain from T60)
//
// Every mode decays independently (no global amplitude envelope), so the
// tone evolves naturally after the strike. Coefficients are computed only
// at strike time or when a live parameter changes - no trig in the audio
// path. A re-strike adds energy on top of the still-ringing state; the
// resonator state is never reset.
//
// Excitation = a finite-contact mallet model, not a raw impulse. A
// single-sample impulse is flat to Nyquist - a digital click that the
// resonators pass straight through during the burst (ya = x + ...), so
// both the dry strike and the reverb send splatter. Instead: a smooth
// contact-force bump (zero value AND zero slope at both ends) plus a
// short contact-noise burst, lowpassed to the mallet's brightness with
// a fast attack ramp. Both live only for the contact time (~2 ms soft
// felt, ~1.3 ms hardest) and are distributed across the modes with
// per-mode weights. The Strike parameter tilts the burst spectrum
// (soft = low modes; the hardest contact is capped at a medium-bright
// mallet). Brightness adds a strong high-mode shelf (applied at the
// next strike) and also opens up the rubbed modes live.
// Rim rubbing feeds a low-level continuous noise into
// the voice for as long as the wheel is turned: lowpassed (~2.5 kHz)
// friction noise driving mainly the lowest modes - a real rubbed rim
// sings the fundamental, full-band noise would sound like hiss.
//
// Stereo: pair member A sits slightly left, member B slightly right, the
// spread widening for higher modes; the fundamental stays ~centred. A
// per-bowl pan (-1..+1, encoder 1 page 1 SHIFT+turn) tilts the whole
// voice, and a per-bowl volume trim (0..2, encoder 1 page 1 turn) scales
// its output; pan and gain are smoothed per sample, so live edits never
// zipper-noise.

#include "bowl_profile.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace bowls
{
class BowlVoice
{
  public:
    static constexpr int kPairs      = kNumModes;  // 8
    static constexpr int kResonators = 2 * kPairs; // 16

    void Init(float sample_rate)
    {
        fs_            = sample_rate;
        profile_       = 0;
        decay_mult_    = 1.0f;
        shimmer_mult_  = 1.0f;
        brightness_    = 0.5f;
        fundamental_   = 220.0f;
        intensity_     = 0.0f;
        detune_jitter_ = 1.0f;
        envelope_      = 0.0f;
        burst_pos_     = 0;
        burst_len_     = 1;
        pulse_gain_    = 0.0f;
        exc_lp_coef_   = 0.2f;
        exc_lp1_       = 0.0f;
        exc_lp2_       = 0.0f;
        rub_level_     = 0.0f;
        rub_smooth_    = 0.0f;
        rub_lp_        = 0.0f;
        rub_lp_coef_   = 0.08f;
        pan_l_         = 1.0f;
        pan_r_         = 1.0f;
        pan_l_target_  = 1.0f;
        pan_r_target_  = 1.0f;
        gain_          = 1.0f;
        gain_target_   = 1.0f;
        key_           = -1;
        rng_           = 0x12345678u;
        for(int k = 0; k < kResonators; ++k)
            res_[k] = Resonator{};
        float rw = 1.0f;
        for(int m = 0; m < kPairs; ++m)
        {
            pair_in_[m] = 0.0f;
            ga_l_[m] = ga_r_[m] = gb_l_[m] = gb_r_[m] = 0.0f;
            rub_weight_[m] = rw;
            rw *= 0.35f;
        }
    }

    // -- voice-manager bookkeeping --------------------------------------

    void SetKey(int key) { key_ = key; }
    int  Key() const { return key_; }

    /** Rough loudness estimate for quietest-voice stealing. */
    float Envelope() const { return envelope_; }

    /** Still sounding (or about to sound)? */
    bool IsActive() const { return envelope_ > 1.0e-4f || burst_pos_ > 0; }

    // -- parameters ------------------------------------------------------

    /** Bowl profile; applied at the NEXT strike (a ringing voice keeps
     *  the physical character it was struck with). */
    void SetProfile(int profile) { profile_ = profile; }

    /** Excitation-time parameter; applied at the next strike. */
    void SetBrightness(float brightness) { brightness_ = brightness; }

    /** Live decay multiplier (0.25..2); updates the ringing voice. */
    void SetDecayMult(float mult)
    {
        if(mult == decay_mult_)
            return;
        decay_mult_ = mult;
        UpdateCoeffs();
    }

    /** Live paired-mode detune multiplier (0..2); updates the voice. */
    void SetShimmerMult(float mult)
    {
        if(mult == shimmer_mult_)
            return;
        shimmer_mult_ = mult;
        UpdateCoeffs();
    }

    /** Rim-rubbing level 0..1 (continuous noise excitation). */
    void SetRub(float level) { rub_level_ = level; }

    /** Per-bowl stereo pan -1 (left) .. +1 (right); smoothed per sample. */
    void SetPan(float pan)
    {
        if(pan < -1.0f)
            pan = -1.0f;
        if(pan > 1.0f)
            pan = 1.0f;
        pan_l_target_ = pan < 0.0f ? 1.0f : 1.0f - pan;
        pan_r_target_ = pan > 0.0f ? 1.0f : 1.0f + pan;
    }

    /** Per-bowl volume trim 0..2 (1 = unity); smoothed per sample, so
     *  live volume edits on a ringing bowl are click-free. */
    void SetBowlGain(float gain)
    {
        if(gain < 0.0f)
            gain = 0.0f;
        if(gain > 2.0f)
            gain = 2.0f;
        gain_target_ = gain;
    }

    /** Strike the bowl. strike_norm 0..1 = soft..hard; the jitters are
     *  small per-strike humanization factors (e.g. 0.9..1.1). */
    void Strike(float fundamental_hz,
                float strike_norm,
                float amp_jitter,
                float detune_jitter)
    {
        fundamental_     = fundamental_hz;
        detune_jitter_   = detune_jitter;
        intensity_       = (0.30f + 0.70f * strike_norm) * amp_jitter;

        const BowlProfile& p = GetBowlProfile(profile_);
        // The strike spectral tilt is capped at half range: the hardest
        // strike sounds like the former 50% position - harder contact
        // than that stops sounding like a bowl. (Loudness is unaffected:
        // intensity_ still uses the full strike_norm range.)
        const float softness = 1.0f - 0.5f * strike_norm;

        // Per-mode excitation weight: profile weight x strike spectral
        // tilt (soft strikes favour the low modes) x brightness shelf
        // (up to 8x on the top mode - clearly audible on the next hit).
        for(int m = 0; m < kPairs; ++m)
        {
            const float tilt
                = expf(-2.2f * softness * static_cast<float>(m));
            const float bright
                = powf(8.0f,
                       brightness_
                           * (static_cast<float>(m)
                              / static_cast<float>(kPairs - 1)));
            in_weight_[m] = p.excite[m] * tilt * bright;
        }

        UpdateCoeffs();

        // Contact time scales with mallet hardness (soft felt ~2 ms,
        // the hardest mallet ~1.3 ms); the force bump and the contact
        // noise both live only for the contact time.
        burst_len_ = static_cast<int>(fs_ * (0.0006f + 0.0014f * softness));
        if(burst_len_ < 1)
            burst_len_ = 1;
        burst_pos_ = burst_len_;

        // Bump gain: the bump's low-frequency mode energy matches the
        // old 0.6 single-sample impulse (the bump shape sums to
        // ~0.53 x its length), so the ring's body stays as loud.
        pulse_gain_ = 0.6f * 1.9f / static_cast<float>(burst_len_);

        // Contact-noise lowpass (two one-poles = 12 dB/oct): 1.2 kHz
        // for soft felt, 4 kHz at the hardest. The contact never
        // carries Nyquist-flat energy, so neither the dry strike nor
        // the reverb can splatter.
        const float exc_cut = 1200.0f + 5600.0f * (1.0f - softness);
        exc_lp_coef_ = 1.0f - expf(-2.0f * kPi * exc_cut / fs_);
        exc_lp1_ = exc_lp2_ = 0.0f;
    }

    /** Select the bowl for RIMMING without striking it: sets the
     *  fundamental and recomputes the resonator coefficients (which
     *  also retunes the register-relative rub lowpass) but starts no
     *  contact burst. A still-ringing voice keeps its energy and just
     *  adopts the new tuning. */
    void SelectForRub(float fundamental_hz, float detune_jitter)
    {
        fundamental_   = fundamental_hz;
        detune_jitter_ = detune_jitter;
        UpdateCoeffs();
    }

    /** Accumulate the voice into the stereo block. */
    void ProcessBlock(float* out_l, float* out_r, size_t n)
    {
        if(!IsActive() && rub_level_ < 0.001f && rub_smooth_ < 0.001f)
            return;

        float env = envelope_;
        float rub = rub_smooth_;
        for(size_t i = 0; i < n; ++i)
        {
            // Per-sample rub slew (~10 ms): wheel-detent steps and
            // rub-target voice switches are inaudible.
            rub += 0.002f * (rub_level_ - rub);

            float exc = 0.0f;
            if(burst_pos_ > 0)
            {
                const float t
                    = static_cast<float>(burst_pos_)
                      / static_cast<float>(burst_len_);
                const float p = 1.0f - t; // contact phase 0 -> 1
                // Finite-contact force bump (replaces the old
                // single-sample impulse): (4p(1-p))^2 is zero with
                // zero slope at both ends, so there is no click and
                // no trig in the audio path.
                const float u = 4.0f * p * (1.0f - p);
                exc = intensity_ * pulse_gain_ * u * u;
                // Contact noise: two-pole lowpassed to the mallet's
                // brightness, ~0.25 ms attack ramp (no step onset),
                // t^2 release.
                const int   n = burst_len_ - burst_pos_;
                const float attack
                    = n >= 12 ? 1.0f : static_cast<float>(n) / 12.0f;
                exc_lp1_ += exc_lp_coef_ * (Noise() - exc_lp1_);
                exc_lp2_ += exc_lp_coef_ * (exc_lp1_ - exc_lp2_);
                exc += intensity_ * 1.2f * t * t * attack * exc_lp2_;
                --burst_pos_;
            }
            float rub_exc = 0.0f;
            if(rub > 0.0f)
            {
                // Soft friction excitation: the noise is lowpassed near
                // the voice's own register, and the level is square-law
                // shaped - slow turns stay gentle, nothing aggressive.
                rub_lp_ += rub_lp_coef_ * (Noise() - rub_lp_);
                rub_exc = rub * rub * 0.04f * rub_lp_;
            }

            float sum_l = 0.0f;
            float sum_r = 0.0f;
            for(int m = 0; m < kPairs; ++m)
            {
                // Strike burst uses the per-strike weights; rubbing uses
                // its own fundamental-dominant weights, and Brightness
                // opens up the rubbed higher modes live.
                const float rub_bright
                    = 1.0f + 1.2f * brightness_ * static_cast<float>(m);
                const float x = exc * pair_in_[m] * in_weight_[m]
                                + rub_exc * pair_in_[m] * rub_weight_[m]
                                      * rub_bright;
                Resonator&   a = res_[2 * m];
                Resonator&   b = res_[2 * m + 1];
                const float ya = x + a.c1 * a.y1 - a.c2 * a.y2;
                a.y2           = a.y1;
                a.y1           = ya;
                const float yb = x + b.c1 * b.y1 - b.c2 * b.y2;
                b.y2           = b.y1;
                b.y1           = yb;
                sum_l += ya * ga_l_[m] + yb * gb_l_[m];
                sum_r += ya * ga_r_[m] + yb * gb_r_[m];
            }
            // Voice pan + per-bowl volume (~10 ms slews: live edits are
            // click-free).
            pan_l_ += 0.002f * (pan_l_target_ - pan_l_);
            pan_r_ += 0.002f * (pan_r_target_ - pan_r_);
            gain_ += 0.002f * (gain_target_ - gain_);
            out_l[i] += sum_l * pan_l_ * gain_;
            out_r[i] += sum_r * pan_r_ * gain_;

            // Leaky peak-ish envelope for voice stealing / idle detection.
            const float mag = sum_l >= 0.0f ? sum_l : -sum_l;
            env += 0.001f * (mag - env);
            if(env < 0.0f)
                env = 0.0f;
        }
        envelope_   = env;
        rub_smooth_ = rub;

        // Anti-denormal snap: states below -240 dBFS go straight to
        // zero, so a long ring never decays into slow denormal math.
        for(int k = 0; k < kResonators; ++k)
        {
            if(res_[k].y1 > -1.0e-12f && res_[k].y1 < 1.0e-12f)
                res_[k].y1 = 0.0f;
            if(res_[k].y2 > -1.0e-12f && res_[k].y2 < 1.0e-12f)
                res_[k].y2 = 0.0f;
        }
    }

  private:
    struct Resonator
    {
        float c1 = 0.0f; // 2 r cos(w)
        float c2 = 0.0f; // r^2
        float y1 = 0.0f;
        float y2 = 0.0f;
    };

    static constexpr float kPi = 3.14159265358979f;

    /** Recompute every resonator coefficient + pan gain from the current
     *  fundamental, profile and live parameters. Called at strike time
     *  and on live decay/shimmer changes only - never per sample. */
    void UpdateCoeffs()
    {
        const BowlProfile& p = GetBowlProfile(profile_);
        // Beat rate scales with pitch (detune_hz is a 220 Hz reference).
        const float det_scale = fundamental_ / 220.0f;
        // The rub-noise lowpass tracks the voice's register (~3x the
        // fundamental, capped at 6 kHz): a smooth friction texture for
        // every bowl size.
        float rub_cut = 3.0f * fundamental_;
        if(rub_cut > 6000.0f)
            rub_cut = 6000.0f;
        rub_lp_coef_ = 1.0f - expf(-2.0f * kPi * rub_cut / fs_);
        for(int m = 0; m < kPairs; ++m)
        {
            const float f0  = fundamental_ * p.ratio[m];
            const float det = p.detune_hz[m] * det_scale * shimmer_mult_
                              * detune_jitter_;
            const float t60 = p.t60_s[m] * decay_mult_;
            SetResonator(res_[2 * m], f0, t60);
            SetResonator(res_[2 * m + 1], f0 + det, t60);

            // Pair input gain: sin(w) equalizes the impulse-response
            // peak across modes (a two-pole resonator's response peaks
            // at ~g / sin(w)); 0 for a muted (above-Nyquist) resonator.
            pair_in_[m] = PairInputGain(f0);

            // Stereo: A slightly left, B slightly right; higher modes
            // wider; the fundamental pair stays near-centre. kVoiceGain
            // leaves headroom for four voices.
            constexpr float kVoiceGain = 0.6f;
            const float     w          = 0.06f + 0.035f * static_cast<float>(m);
            ga_l_[m] = (0.5f + w) * kVoiceGain;
            ga_r_[m] = (0.5f - w) * kVoiceGain;
            gb_l_[m] = (0.5f - w) * kVoiceGain;
            gb_r_[m] = (0.5f + w) * kVoiceGain;
        }
    }

    void SetResonator(Resonator& r, float freq, float t60)
    {
        if(freq < 20.0f || freq > 0.45f * fs_ || t60 <= 0.0f)
        {
            // Above-Nyquist (high modes of the highest bowls) or
            // degenerate: keep the resonator silent.
            r.c1 = r.c2 = 0.0f;
            return;
        }
        const float rad   = 2.0f * kPi * freq / fs_;
        const float decay = expf(-6.907755f / (t60 * fs_));
        r.c1              = 2.0f * decay * cosf(rad);
        r.c2              = decay * decay;
    }

    /** sin(w) frequency equalization; 0 for a muted resonator. */
    float PairInputGain(float freq) const
    {
        if(freq < 20.0f || freq > 0.45f * fs_)
            return 0.0f;
        return sinf(2.0f * kPi * freq / fs_);
    }

    float Noise()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>(static_cast<int32_t>(rng_))
               * (1.0f / 2147483648.0f);
    }

    float     fs_            = 48000.0f;
    int       profile_       = 0;
    float     decay_mult_    = 1.0f;
    float     shimmer_mult_  = 1.0f;
    float     brightness_    = 0.5f;
    float     fundamental_   = 220.0f;
    float     intensity_     = 0.0f;
    float     detune_jitter_ = 1.0f;
    float     envelope_      = 0.0f;
    int       burst_pos_     = 0;
    int       burst_len_     = 1;   // contact time in samples
    float     pulse_gain_    = 0.0f; // contact-bump gain (per strike)
    float     exc_lp_coef_   = 0.2f; // contact-noise lowpass (per strike)
    float     exc_lp1_       = 0.0f; // contact-noise filter states
    float     exc_lp2_       = 0.0f;
    float     rub_level_     = 0.0f;
    float     rub_smooth_    = 0.0f;
    float     rub_lp_        = 0.0f;  // lowpassed rub-noise state
    float     rub_lp_coef_   = 0.08f; // register-relative rub LP
    float     pan_l_         = 1.0f;  // smoothed pan gains
    float     pan_r_         = 1.0f;
    float     pan_l_target_  = 1.0f;
    float     pan_r_target_  = 1.0f;
    float     gain_          = 1.0f;  // smoothed per-bowl volume trim
    float     gain_target_   = 1.0f;
    int       key_           = -1;
    uint32_t  rng_           = 0x12345678u;

    Resonator res_[kResonators];
    float     in_weight_[kPairs];  // per-mode excitation weight (strike)
    float     rub_weight_[kPairs]; // per-mode rub weight (low modes)
    float     pair_in_[kPairs];   // per-pair input gain (normalization)
    float     ga_l_[kPairs];
    float     ga_r_[kPairs];
    float     gb_l_[kPairs];
    float     gb_r_[kPairs];
};

} // namespace bowls
