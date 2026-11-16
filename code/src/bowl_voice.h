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
// path. A re-strike's contact briefly damps the still-ringing state (the
// mallet rests on the bowl), so re-hits stay consistent instead of
// phase-cancelling the old ring; the state is never hard-reset.
//
// Excitation = a finite-contact mallet model, not a raw impulse. A
// single-sample impulse is flat to Nyquist - a digital click that the
// resonators pass straight through during the burst (ya = x + ...), so
// both the dry strike and the reverb send splatter. Instead: a smooth
// contact-force bump (zero value AND zero slope at both ends) over a
// short, hardness-scaled mallet contact (~1.1-1.6 ms at high pitches,
// extended to 1.5 cycles at low pitches), plus a subtle contact tick:
// high-passed noise above the second mode inside a ~1.2 ms initial-touch
// sub-burst. The cutoff follows the played pitch and bowl profile. The
// tick is enabled by default and can be isolated through the host
// diagnostic hook. Both are distributed
// across the modes with per-mode weights. The Strike parameter tilts the burst
// spectrum (soft = low modes; the hardest contact is capped at a
// medium-bright mallet). Brightness adds a strong high-mode shelf
// (applied at the next strike) and also opens up the rubbed modes live.
// Rim rubbing feeds a low-level continuous excitation into the voice
// for as long as the wheel is turned, in two characters selected by
// the wheel direction (chompi_main.cpp):
//   clockwise         = friction noise, two-pole lowpassed to a narrow
//                       band (~3x the fundamental, capped at 750 Hz),
//                       driving mainly the lowest modes - a real rubbed
//                       rim sings the fundamental, and wideband noise
//                       reads as hiss (worst on high bowls)
//   counter-clockwise = a CLEAN sustained tone: phase-continuous
//                       rotation oscillators at each pair's (wandering)
//                       modal frequencies drive the same resonators in
//                       place of the noise. The drive is normalized by
//                       sqrt(1-r): a sustained sine at resonance builds
//                       to A/(2(1-r)), so the sustained level scales
//                       with 1/sqrt(1-r) - the same Decay-knob law as
//                       broadband friction noise - and stays bounded at
//                       the longest decays.
// The characters crossfade over ~100 ms on a reversal (no re-strike,
// no phase reset, no doubled drive); in full clean mode the friction
// path is skipped outright, so its contribution is exactly zero.
// The Rimming play mode adds a clean-pad gate: while a key is held (or
// LOOP-latched), the same phase-continuous rotation oscillators are
// driven by a slow pad envelope (10 s attack to 90%, 10 s release to
// 10% of the note-off level), mixed into the wheel's clean drive with
// a bounded sum that can never exceed the wheel path's own maximum.
// With the pad gate off the wheel DSP path is bit-identical to the
// accepted regression baseline.
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
        shimmer_target_mult_ = 1.0f;
        shimmer_change_pending_ = false;
        brightness_    = 0.5f;
        fundamental_   = 220.0f;
        intensity_     = 0.0f;
        detune_jitter_ = 1.0f;
        envelope_      = 0.0f;
        burst_pos_     = 0;
        burst_len_     = 1;   // force-contact time in samples
        noise_len_     = 1;   // initial-touch tick sub-burst in samples
        pulse_gain_    = 0.0f;
        exc_lp_coef_   = 0.2f;
        exc_lp1_       = 0.0f;
        exc_lp2_       = 0.0f;
        diag_force_    = 1.0f;
        diag_noise_    = 1.0f;
        rub_level_     = 0.0f;
        rub_smooth_    = 0.0f;
        rub_lp_        = 0.0f;
        rub_lp2_       = 0.0f;
        rub_lp_coef_   = 0.08f;
        char_target_   = 0.0f; // power-on character: friction texture
        char_clean_    = 0.0f;
        char_step_     = 10.0f / sample_rate; // ~100 ms crossfade
        pad_gate_      = false;
        pad_env_       = 0.0f;
        ring_fade_coef_ = 1.0f;
        ring_fade_left_ = 0;
        // Pad envelope coefficients (one-pole, no trig per sample):
        // from silence the drive reaches 0.90 after exactly 10 s; after
        // note-off it falls to 0.10 of the note-off level after exactly
        // 10 s (env(N)/env(0) = (1-c)^N = 0.1 for N = T*fs).
        pad_attack_coef_  = 1.0f - expf(-2.302585093f / (10.0f * sample_rate));
        pad_release_coef_ = 1.0f - expf(-2.302585093f / (10.0f * sample_rate));
        pan_l_         = 1.0f;
        pan_r_         = 1.0f;
        pan_l_target_  = 1.0f;
        pan_r_target_  = 1.0f;
        gain_          = 1.0f;
        gain_target_   = 1.0f;
        perf_gain_        = 1.0f;
        perf_gain_target_ = 1.0f;
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

            // Clean-drive oscillators: unit amplitude, phase 0
            // (deterministic, like strikes); identity rotation and zero
            // drive until the first UpdateCoeffs.
            osc_sa_[m] = osc_sb_[m] = 0.0f;
            osc_ca_[m] = osc_cb_[m] = 1.0f;
            cos_a_[m] = cos_b_[m] = 1.0f;
            sin_a_[m] = sin_b_[m] = 0.0f;
            clean_amp_a_[m] = clean_amp_b_[m] = 0.0f;

            // Rotating shimmer: random start phase, 2..10 s orbit
            // period spread across pairs and slightly randomized so two
            // bowls never swirl in sync.
            swirl_phase_[m] = 6.2831853f * (Noise() * 0.5f + 0.5f);
            const float rate
                = 0.10f * (1.0f + 0.22f * static_cast<float>(m))
                  * (0.85f + 0.30f * (Noise() * 0.5f + 0.5f));
            swirl_inc_[m]   = 2.0f * kPi * rate / fs_;
            swirl_l_[m]     = swirl_l_t_[m] = 1.0f;
            swirl_r_[m]     = swirl_r_t_[m] = 1.0f;
            wander_[m]      = wander_target_[m] = wander_applied_[m] = 1.0f;
            wander_hold_[m] = 0;
        }
    }

    // -- voice-manager bookkeeping --------------------------------------

    void SetKey(int key) { key_ = key; }
    int  Key() const { return key_; }

    /** Originating play mode (voice-manager bookkeeping: 0 = Strikes,
     *  1 = Rimming, 2 = Split, -1 = none). A voice's identity is
     *  (mode, key): the same physical key pressed in another mode is a
     *  DIFFERENT note and must never reuse, retune or reset this
     *  still-ringing voice. */
    void SetVoiceMode(int mode) { voice_mode_ = mode; }
    int  VoiceMode() const { return voice_mode_; }

    /** Rough loudness estimate for quietest-voice stealing. */
    float Envelope() const { return envelope_; }

    /** Still sounding (or about to sound)? Drives voice allocation and
     *  the LED ring glow - an AUDIBILITY notion, not an audio gate. A
     *  gated pad counts from the instant its gate opens, so a
     *  just-started pad voice is reserved while its slow 10 s attack is
     *  still inaudible. */
    bool IsActive() const
    {
        return envelope_ > 1.0e-4f || burst_pos_ > 0 || pad_gate_
               || pad_env_ > 1.0e-4f;
    }

    /** Still holds computable ring energy? Unlike IsActive() this reads
     *  the resonator STATES: the output sum can null between two beating
     *  pair members while the ring is still alive, so gating the audio
     *  path on the output envelope killed the tail mid-swell - the beat
     *  simply stopped ~20 dB early instead of ringing out. The state
     *  check is null-immune, and its -100 dBFS threshold sits below the
     *  codec noise floor, so the ring decays until it is truly silent. */
    bool HasTail() const
    {
        for(int k = 0; k < kResonators; ++k)
            if(res_[k].y1 > 1.0e-5f || res_[k].y1 < -1.0e-5f
               || res_[k].y2 > 1.0e-5f || res_[k].y2 < -1.0e-5f)
                return true;
        return false;
    }

    // -- parameters ------------------------------------------------------

    /** Bowl profile; applied at the NEXT strike (a ringing voice keeps
     *  the physical character it was struck with). */
    void SetProfile(int profile) { profile_ = profile; }

    /** Excitation-time parameter; applied at the next strike. */
    void SetBrightness(float brightness) { brightness_ = brightness; }

    /** Live decay multiplier (0.25..3); updates the ringing voice. */
    void SetDecayMult(float mult)
    {
        if(mult < 0.25f)
            mult = 0.25f;
        if(mult > 3.0f)
            mult = 3.0f;
        if(mult == decay_mult_)
            return;
        decay_mult_ = mult;
        UpdateCoeffs();
    }

    /** Paired-mode detune multiplier (0..2). Defer changes until the voice
     *  is quiet or a new strike/rim selection recalculates its coefficients. */
    void SetShimmerMult(float mult)
    {
        if(mult == shimmer_target_mult_)
            return;
        shimmer_target_mult_ = mult;
        shimmer_change_pending_ = true;
        ApplyPendingShimmerIfQuiet();
    }

    /** Rim-rubbing level 0..1 and character (0 = clockwise friction
     *  texture, 1 = counter-clockwise clean sustained tone). The level
     *  slews with the usual bloom/fade; the character crossfades over
     *  ~100 ms and lands exactly on its target, so a reversal never
     *  re-strikes, resets a phase, or doubles the drive. */
    void SetRub(float level, float clean)
    {
        rub_level_   = level;
        char_target_ = clean > 0.0f ? 1.0f : 0.0f;
    }

    /** Clean-pad gate (Rimming mode): while open, the phase-continuous
     *  clean modal oscillators are driven at kPadDrive x the pad
     *  envelope - 10 s attack to 90%, 10 s release to 10% of the
     *  note-off level. The bowl's natural ring and the reverb can decay
     *  well beyond the excitation envelope. Retriggering starts from
     *  the current envelope value, never from a reset. */
    void SetPadGate(bool on) { pad_gate_ = on; }
    bool PadGate() const { return pad_gate_; }
    /** Current pad-drive envelope 0..1 (host-test hook). */
    float PadEnvelope() const { return pad_env_; }

    /** Test hook (code/tests strike analysis): scale the strike's
     *  force-bump and contact-noise components independently for
     *  force-only / noise-only diagnostic renders. Defaults 1/1, so
     *  the subtle contact tick is present during normal operation. */
    void SetStrikeDiagnostics(float force_scale, float noise_scale)
    {
        diag_force_ = force_scale;
        diag_noise_ = noise_scale;
    }

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

    /** MIDI performance gain 0..1 (note-on velocity for pads and rim
     *  selections; 1 = the local keyboard's reference). Smoothed per
     *  sample like the pan/bowl gains, so live CC-driven changes never
     *  zipper. Multiplies the voice output independently of the saved
     *  per-bowl volume - a transient control, never persisted. */
    void SetPerfGain(float gain)
    {
        if(gain < 0.0f)
            gain = 0.0f;
        if(gain > 1.0f)
            gain = 1.0f;
        perf_gain_target_ = gain;
    }

    /** CC 120 panic: silence the voice WITHOUT a full Init (no
     *  coefficient recomputation, no profile/tuning loss) - zero the
     *  resonator states, the strike burst, the rub/pad excitation and
     *  the envelopes, so the ring stops immediately. Cheap enough for
     *  the audio ISR (32 floats + state); the caller fades the bus to
     *  mask any residual click. A fresh Strike()/SelectForRub() fully
     *  re-arms the voice. */
    void PanicReset()
    {
        for(int k = 0; k < kResonators; ++k)
            res_[k] = Resonator{};
        envelope_       = 0.0f;
        burst_pos_      = 0;
        rub_level_      = 0.0f;
        rub_smooth_     = 0.0f;
        rub_lp_         = 0.0f;
        rub_lp2_        = 0.0f;
        pad_gate_       = false;
        pad_env_        = 0.0f;
        ring_fade_left_ = 0;
        ring_fade_coef_ = 1.0f;
        restrike_damp_  = 1.0f;
        exc_lp1_ = exc_lp2_ = 0.0f;
    }

    /** Strike the bowl. strike_norm 0..1 = soft..hard; the jitters are
     *  small per-strike humanization factors (e.g. 0.9..1.1). damp_target
     *  is how much of a previous ring survives the contact time (a
     *  stolen voice is damped much deeper than a same-bowl re-hit). */
    void Strike(float fundamental_hz,
                float strike_norm,
                float amp_jitter,
                float detune_jitter,
                float damp_target = 0.25f)
    {
        StrikeWithAmplitude(fundamental_hz, strike_norm,
                            0.30f + 0.70f * strike_norm, amp_jitter,
                            detune_jitter, damp_target);
    }

    /** Strike with independent timbral hardness and excitation amplitude.
     *  The legacy Strike() entry point retains the local keyboard level curve. */
    void StrikeWithAmplitude(float fundamental_hz,
                             float strike_norm,
                             float strike_amplitude,
                             float amp_jitter,
                             float detune_jitter,
                             float damp_target = 0.25f)
    {
        fundamental_     = fundamental_hz;
        detune_jitter_   = detune_jitter;
        intensity_       = strike_amplitude * amp_jitter;
        // A strike takes the voice over completely: a leftover pad
        // gate/envelope (a stolen Rimming voice) ends here. The old
        // ring itself is handled by the contact damping below.
        pad_gate_ = false;
        pad_env_  = 0.0f;

        const BowlProfile& p = GetBowlProfile(profile_);
        // The strike spectral tilt is capped at half range: the hardest
        // strike sounds like the former 50% position - harder contact
        // than that stops sounding like a bowl. (Loudness is unaffected:
        // intensity_ still uses the full strike_norm range.)
        const float softness = 1.0f - 0.5f * strike_norm;

        // Scale contact with hardness and the played pitch. Low notes
        // need at least 1.5 cycles of force; higher or user-tuned notes
        // get a proportionally shorter pulse so their fundamental rings
        // instead of being under-driven by a fixed long contact.
        float contact_s = 0.0006f + 0.0010f * softness;
        const float low_note_contact_s = 1.5f / fundamental_;
        if(contact_s < low_note_contact_s)
            contact_s = low_note_contact_s;
        burst_len_ = static_cast<int>(fs_ * contact_s);
        if(burst_len_ < 12)
            burst_len_ = 12;
        burst_pos_ = burst_len_;

        // Brightness blends the dark strike voicing into the same
        // per-mode shelf used by the rim path. At maximum, compensate
        // for the contact pulse's frequency rolloff; at zero, the
        // original strike voicing is unchanged.
        const float brightness_blend
            = brightness_ <= 0.0f ? 0.0f
              : brightness_ >= 1.5f
                  ? 1.0f
                  : brightness_ * (1.0f / 1.5f);
        for(int m = 0; m < kPairs; ++m)
        {
            const float tilt
                = expf(-2.4f * softness * static_cast<float>(m));
            const float strike_weight
                = p.excite[m] * tilt
                  * powf(8.0f,
                         brightness_
                             * (static_cast<float>(m)
                                / static_cast<float>(kPairs - 1)))
                  * (m > 0 ? kStrikeUpper : 1.0f);
            const float rim_bright
                = 1.0f + 1.2f * brightness_ * static_cast<float>(m);
            const float mode_freq = fundamental_ * p.ratio[m];
            float shelf_position = (mode_freq - 1500.0f) * (1.0f / 4500.0f);
            if(shelf_position < 0.0f)
                shelf_position = 0.0f;
            if(shelf_position > 1.0f)
                shelf_position = 1.0f;
            shelf_position
                = shelf_position * shelf_position * (3.0f - 2.0f * shelf_position);
            const float strike_brightness_shelf
                = 1.0f
                  + brightness_blend * (kStrikeBrightnessBoost - 1.0f)
                        * shelf_position;
            const float rim_weight
                = rub_weight_[m] * rim_bright * strike_brightness_shelf;
            in_weight_[m]
                = strike_weight
                  + brightness_blend * (rim_weight - strike_weight);
        }

        ApplyPendingShimmerAtNoteStart();
        UpdateCoeffs();

        // The brief initial touch: a subtle high-frequency mallet edge
        // inside the longer force contact.
        noise_len_ = static_cast<int>(fs_ * 0.0012f);
        if(noise_len_ > burst_len_)
            noise_len_ = burst_len_;
        if(noise_len_ < 12)
            noise_len_ = 12;

        // Contact damping: during the contact time the mallet rests on
        // the bowl and mutes the previous ring to ~25% (-12 dB), so a
        // re-hit answers with a fresh, consistent ring instead of
        // randomly phase-cancelling whatever was still sounding. On a
        // quiet voice the state is ~zero and this is a no-op. The damp
        // target is spread over the contact time, so the longer push
        // damps more gently per sample.
        restrike_damp_ = powf(damp_target, 1.0f / static_cast<float>(burst_len_));

        // Bump gain: normalize the mode-1 ring to the established level
        // (the old 0.6 impulse equivalent) at THIS contact length: the
        // pulse's response at the fundamental is measured per strike
        // with a short Goertzel recurrence over the bump shape, so a
        // low bowl's longer contact does not dull its body.
        {
            const float w  = 2.0f * kPi * fundamental_ / fs_;
            const float cw = 2.0f * cosf(w);
            float s1 = 0.0f, s2 = 0.0f, area = 0.0f;
            for(int n = 0; n < burst_len_; ++n)
            {
                const float b = ContactShape(
                    static_cast<float>(n) / static_cast<float>(burst_len_));
                area += b;
                const float s0 = b + cw * s1 - s2;
                s2             = s1;
                s1             = s0;
            }
            const float mag = sqrtf(s1 * s1 + s2 * s2 - cw * s1 * s2);
            // Anchor: the old 1.65 ms symmetric bump delivered ~0.6 of
            // mode-1 charge; normalize the new pulse to the same charge
            // at THIS fundamental, so ring loudness survives the
            // contact-time change and stays consistent across
            // registers. The lift is capped at 3x the flat-response
            // gain: a felt push physically cannot drive a mode far
            // above 1/contact_hz (small bowls with soft strikes ring
            // quieter - use a harder strike), and the cap keeps tuning
            // extremes bounded (an uncapped 1/mag exploded at the top
            // of the range in host sweeps).
            const float flat = 0.6f / area;
            float       pg
                = mag > 1.0e-6f * area ? 0.6f / mag : flat;
            if(pg > 3.0f * flat)
                pg = 3.0f * flat;
            pulse_gain_ = pg;
        }

        // A two-pole highpass (12 dB/oct) puts the short contact tick
        // above the bowl's second mode, so its edge follows both the
        // played pitch and the selected profile instead of a fixed Hz
        // band. Keep the corner below the active audio band.
        float exc_cut = fundamental_ * p.ratio[1] * 0.8f;
        if(exc_cut > 0.35f * fs_)
            exc_cut = 0.35f * fs_;
        exc_lp_coef_ = 1.0f - expf(-2.0f * kPi * exc_cut / fs_);
        exc_lp1_ = exc_lp2_ = 0.0f;
    }

    /** Select the bowl for RIMMING without striking it: sets the
     *  fundamental and recomputes the resonator coefficients (which
     *  also retunes the register-relative rub lowpass) but starts no
     *  contact burst. A still-ringing voice keeps its energy and just
     *  adopts the new tuning - unless ring_damp_target < 1, which fades
     *  the old ring to that fraction over ~100 ms (per-sample multiply,
     *  the restrike_damp_ idiom - never a hard reset, so no click). A
     *  pad stealing a still-ringing voice needs this fade: the pad
     *  envelope gates only the pad DRIVE, not the resonator output, so
     *  without it the old ring would sound instantly at the new pitch
     *  and the slow pad attack would be bypassed. */
    void SelectForRub(float fundamental_hz,
                      float detune_jitter,
                      float ring_damp_target = 1.0f)
    {
        fundamental_   = fundamental_hz;
        detune_jitter_ = detune_jitter;
        // A plain rim selection is not a pad: any leftover pad drive
        // from this voice's previous life ends here.
        pad_gate_ = false;
        pad_env_  = 0.0f;
        if(ring_damp_target < 1.0f)
        {
            if(ring_damp_target < 1.0e-5f)
                ring_damp_target = 1.0e-5f;
            ring_fade_left_ = static_cast<int>(kGhostFadeS * fs_);
            if(ring_fade_left_ < 1)
                ring_fade_left_ = 1;
            ring_fade_coef_
                = powf(ring_damp_target,
                       1.0f / static_cast<float>(ring_fade_left_));
            // A leftover wheel slew must not keep exciting the retuned
            // bowl through the fade; ApplyWheel re-establishes any real
            // target the next block (blooming from silence as designed).
            rub_smooth_ = 0.0f;
            rub_lp_     = 0.0f;
            rub_lp2_    = 0.0f;
        }
        ApplyPendingShimmerAtNoteStart();
        UpdateCoeffs();
    }

    /** Accumulate the voice into the stereo block. */
    void ProcessBlock(float* out_l, float* out_r, size_t n)
    {
        ApplyPendingShimmerIfQuiet();
        // Idle gate: skip only when the ring is TRULY over. Gating on
        // the output envelope (IsActive) cut the voice during a beat
        // null with the tail still faintly audible; the null-immune
        // state check lets every mode ring out to silence. The
        // anti-denormal snap below eventually drops the states to exact
        // zero, so a fully decayed voice still costs nothing. A pad
        // gate (or its still-releasing envelope) keeps the voice awake
        // without cutting off low-level resonator tails.
        if(burst_pos_ <= 0 && rub_level_ < 0.001f && rub_smooth_ < 0.001f
           && !pad_gate_ && pad_env_ < 0.001f && !HasTail())
        {
            // The output follower can outlast the resonator-state
            // threshold; don't leave an idle voice permanently active.
            envelope_ = 0.0f;
            return;
        }

        UpdateLife(n);

        float env = envelope_;
        float rub = rub_smooth_;
        for(size_t i = 0; i < n; ++i)
        {
            // Per-sample rub slew: a bowl entering the rub set blooms
            // from ITS OWN silence (~0.5 s - a real rim crescendo takes
            // time to catch) instead of jumping in at the shared level,
            // and a bowl leaving the set fades out faster (~0.2 s).
            // Detent steps stay inaudible.
            rub += (rub_level_ > rub ? 0.00004f : 0.0001f)
                   * (rub_level_ - rub);

            // Rim-character crossfade: a fixed-rate linear ramp that
            // lands EXACTLY on the target. In full clean mode the
            // friction path below is skipped outright, so its
            // contribution is exactly zero (no leftover noise floor),
            // and in full texture mode the clean oscillators are not
            // advanced (textured output is sample-identical).
            if(char_clean_ < char_target_)
            {
                char_clean_ += char_step_;
                if(char_clean_ > char_target_)
                    char_clean_ = char_target_;
            }
            else if(char_clean_ > char_target_)
            {
                char_clean_ -= char_step_;
                if(char_clean_ < char_target_)
                    char_clean_ = char_target_;
            }

            float exc = 0.0f;
            if(burst_pos_ > 0)
            {
                const float t
                    = static_cast<float>(burst_pos_)
                      / static_cast<float>(burst_len_);
                const float p = 1.0f - t; // contact phase 0 -> 1
                // Finite-contact force pulse (replaces the old
                // symmetric bump): a slow press and quicker release
                // (asymmetric smoothsteps, zero value AND zero slope at
                // both ends), so the ring blooms over the contact and
                // there is no click and no trig in the audio path.
                exc = intensity_ * pulse_gain_ * ContactShape(p)
                      * diag_force_;
                // Contact tick: two-pole high-band noise, only during
                // the ~1.2 ms initial-touch sub-burst (the force push
                // continues after it), with a ~0.25 ms attack ramp and
                // a (1-t)^2 release. Subordinate to the bowl's buildup
                // (kStrikeNoise, calibrated against the references).
                const int   n = burst_len_ - burst_pos_;
                if(n < noise_len_)
                {
                    const float attack
                        = n >= 12 ? 1.0f : static_cast<float>(n) / 12.0f;
                    const float nt
                        = static_cast<float>(n) / static_cast<float>(noise_len_);
                    const float nenv = (1.0f - nt) * (1.0f - nt);
                    const float noise = Noise();
                    exc_lp1_ += exc_lp_coef_ * (noise - exc_lp1_);
                    exc_lp2_ += exc_lp_coef_ * (exc_lp1_ - exc_lp2_);
                    const float tick_noise
                        = noise - 2.0f * exc_lp1_ + exc_lp2_;
                    exc += intensity_ * kStrikeNoise * nenv * attack
                           * tick_noise * diag_noise_;
                }
                --burst_pos_;
            }
            float rub_exc = 0.0f;
            if(rub > 0.0f && char_clean_ < 1.0f)
            {
                // Soft friction excitation: two-pole lowpassed noise in
                // a narrow friction band (12 dB/oct keeps high bowls
                // clean), and the level is square-law shaped - slow
                // turns stay gentle, nothing aggressive. Crossfaded
                // against the clean drive; at char_clean_ == 0 the
                // multiply is by exactly 1.0f (textured output
                // unchanged), at 1.0f the path never runs.
                rub_lp_  += rub_lp_coef_ * (Noise() - rub_lp_);
                rub_lp2_ += rub_lp_coef_ * (rub_lp_ - rub_lp2_);
                rub_exc = rub * rub * 0.04f * rub_lp2_
                          * (1.0f - char_clean_);
            }
            // Clean modal excitation level for this sample (square-law
            // shaped like the friction path); 0 in full-texture mode.
            float clean_lvl = char_clean_ > 0.0f && rub > 0.0f
                                  ? rub * rub * char_clean_
                                  : 0.0f;
            // Pad envelope: one-pole toward the gate, retriggering from
            // the current value (10 s to 90% attack, 10 s to 10%
            // release). Snaps to exact zero at -80 dB on release so an
            // idle pad costs nothing; the resonator tail is guarded by
            // HasTail(), not by this envelope.
            pad_env_ += (pad_gate_ ? pad_attack_coef_ : pad_release_coef_)
                        * ((pad_gate_ ? 1.0f : 0.0f) - pad_env_);
            if(!pad_gate_ && pad_env_ < 1.0e-4f)
                pad_env_ = 0.0f;
            if(pad_env_ > 0.0f)
            {
                // Bounded pad/wheel mix: the pad fills only the
                // headroom the wheel's clean drive leaves, so the sum
                // can never exceed the wheel path's own maximum drive
                // (no doubling, no click). With no pad this branch is
                // skipped and the wheel DSP path is bit-identical to
                // the accepted baseline.
                clean_lvl += kPadDrive * pad_env_ * (1.0f - clean_lvl);
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
                float clean_exc = 0.0f;
                if(clean_lvl > 0.0f)
                {
                    // Phase-continuous rotation oscillators locked to
                    // the pair's (wandering) resonant frequencies; the
                    // per-mode drive is decay-normalized at coefficient
                    // time, so a long ring cannot overbuild. Reversals
                    // keep the phase - only the drive level fades.
                    const float sa = cos_a_[m] * osc_sa_[m]
                                     - sin_a_[m] * osc_ca_[m];
                    osc_ca_[m]     = sin_a_[m] * osc_sa_[m]
                                     + cos_a_[m] * osc_ca_[m];
                    osc_sa_[m]     = sa;
                    const float sb = cos_b_[m] * osc_sb_[m]
                                     - sin_b_[m] * osc_cb_[m];
                    osc_cb_[m]     = sin_b_[m] * osc_sb_[m]
                                     + cos_b_[m] * osc_cb_[m];
                    osc_sb_[m]     = sb;
                    clean_exc = clean_lvl * rub_bright
                                * (clean_amp_a_[m] * sa
                                   + clean_amp_b_[m] * sb);
                }
                // Strike input is injected into the resonator STATE:
                // what radiates is the bowl's response, not the raw
                // mallet force - the references show no direct-force
                // transient at all (first 2 ms are 40-60 dB down).
                // The rub paths keep their direct term: the friction /
                // clean feedthrough is part of their established
                // texture (and their regression baseline).
                const float x_rub = rub_exc * pair_in_[m] * rub_weight_[m]
                                        * rub_bright
                                    + clean_exc;
                const float x_strike = exc * pair_in_[m] * in_weight_[m];
                Resonator&   a = res_[2 * m];
                Resonator&   b = res_[2 * m + 1];
                if(burst_pos_ > 0)
                {
                    // Mallet contact damping: the previous ring is
                    // smoothed toward silence over the contact time.
                    a.y1 *= restrike_damp_;
                    a.y2 *= restrike_damp_;
                    b.y1 *= restrike_damp_;
                    b.y2 *= restrike_damp_;
                }
                if(ring_fade_left_ > 0)
                {
                    // Ghost fade after a pad steal: the old ring decays
                    // to its damp target over ~100 ms instead of
                    // stepping to silence (no click) or ringing straight
                    // through the new pad's slow attack.
                    a.y1 *= ring_fade_coef_;
                    a.y2 *= ring_fade_coef_;
                    b.y1 *= ring_fade_coef_;
                    b.y2 *= ring_fade_coef_;
                }
                const float ya = x_rub + a.c1 * a.y1 - a.c2 * a.y2;
                a.y2           = a.y1;
                a.y1           = ya + x_strike;
                const float yb = x_rub + b.c1 * b.y1 - b.c2 * b.y2;
                b.y2           = b.y1;
                b.y1           = yb + x_strike;
                sum_l += (ya * ga_l_[m] + yb * gb_l_[m]) * swirl_l_[m];
                sum_r += (ya * ga_r_[m] + yb * gb_r_[m]) * swirl_r_[m];
                swirl_l_[m] += 0.004f * (swirl_l_t_[m] - swirl_l_[m]);
                swirl_r_[m] += 0.004f * (swirl_r_t_[m] - swirl_r_[m]);
            }
            // Voice pan + per-bowl volume + MIDI performance gain
            // (~10 ms slews: live edits are click-free).
            pan_l_ += 0.002f * (pan_l_target_ - pan_l_);
            pan_r_ += 0.002f * (pan_r_target_ - pan_r_);
            gain_ += 0.002f * (gain_target_ - gain_);
            perf_gain_ += 0.002f * (perf_gain_target_ - perf_gain_);
            out_l[i] += sum_l * pan_l_ * gain_ * perf_gain_;
            out_r[i] += sum_r * pan_r_ * gain_ * perf_gain_;

            // Leaky peak-ish envelope for voice stealing / idle detection.
            const float mag = sum_l >= 0.0f ? sum_l : -sum_l;
            env += 0.001f * (mag - env);
            if(env < 0.0f)
                env = 0.0f;
            if(ring_fade_left_ > 0)
                --ring_fade_left_;
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

    /** Clean rim-drive calibration constant: per-mode drive amplitude
     *  = kCleanDrive x sqrt(1-r) x pair input gain x rub weight. A
     *  sustained sine at resonance builds to A/(2(1-r)), so the
     *  sustained clean level at the resonator output scales with
     *  kCleanDrive/2 x 1/sqrt(1-r) - the same Decay-knob law as the
     *  friction path's broadband noise, and bounded at max decay.
     *  Calibrated against the friction path's sustained loudness with
     *  the host renderer (code/tests, `make cal`). */
    static constexpr float kCleanDrive = 0.0245f;

    /** Clean-pad drive level (Rimming mode pads): the pad envelope
     *  scales the same sqrt(1-r)-normalized clean oscillators the
     *  counter-clockwise wheel uses. A quarter of the firm-rub level
     *  (rub^2 ~ 0.5): pads are a background bed and must sit well
     *  below the strike loudness (listening calibration: 0.5 -> 0.25
     *  -> 0.125). */
    static constexpr float kPadDrive = 0.125f;

    /** Ghost-fade time when a pad steals a still-ringing voice: the
     *  old ring is damped to the steal target over this span - long
     *  enough to be click-free against silence, short enough to read
     *  as part of the note-on gesture (a stolen STRIKE damps over its
     *  pitch-scaled contact instead, masked there by the new burst). */
    static constexpr float kGhostFadeS = 0.1f;

    /** Subtle strike contact-tick gain. */
    static constexpr float kStrikeNoise = 0.02f;

    /** Strike upper-mode shelf (modes 2+): mild +5.1 dB over the
     *  plain profile x tilt x brightness weight - the felt voicing
     *  favours the low modes; the shelf only keeps the attack from
     *  going dull. The Brightness knob adds the metallic side live. */
    static constexpr float kStrikeUpper = 1.8f;

    /** Extra strike-only upper shelf at maximum brightness, compensating
     *  for the smooth contact pulse's high-frequency rolloff. */
    static constexpr float kStrikeBrightnessBoost = 40.0f;

    /** Contact force pulse shape: smoothstep press over the first ~65%
     *  of the contact, smoothstep release over the rest - zero value
     *  and zero slope at both ends, no spectral nulls (a symmetric bump
     *  puts nulls on the low modes at long contact times). The late
     *  press peak puts the bloom where the reference recordings swell.
     *  Polynomial only; evaluated per sample and per strike (Goertzel
     *  gain normalization). */
    static float ContactShape(float p)
    {
        if(p < 0.65f)
        {
            const float q = p * (1.0f / 0.65f);
            return q * q * (3.0f - 2.0f * q);
        }
        const float q = (1.0f - p) * (1.0f / 0.35f);
        return q * q * (3.0f - 2.0f * q);
    }

    void ApplyPendingShimmerIfQuiet()
    {
        if(!shimmer_change_pending_ || IsActive() || HasTail()
           || rub_level_ >= 0.001f || rub_smooth_ >= 0.001f)
            return;
        shimmer_mult_ = shimmer_target_mult_;
        shimmer_change_pending_ = false;
        UpdateCoeffs();
    }

    void ApplyPendingShimmerAtNoteStart()
    {
        if(!shimmer_change_pending_)
            return;
        shimmer_mult_ = shimmer_target_mult_;
        shimmer_change_pending_ = false;
    }

    /** Recompute every resonator coefficient + pan gain from the current
     *  fundamental, profile and live parameters. Called at strike time
     *  and on live decay/shimmer changes only - never per sample. */
    void UpdateCoeffs()
    {
        // Beat rate scales with pitch (detune_hz is a 220 Hz reference).
        const float det_scale = fundamental_ / 220.0f;
        // The rub-noise lowpass: ~3x the fundamental but capped at a
        // narrow 750 Hz friction band - low bowls keep their texture,
        // high bowls stay clean (wideband noise on a high bowl is hiss,
        // not friction).
        float rub_cut = 3.0f * fundamental_;
        if(rub_cut > 750.0f)
            rub_cut = 750.0f;
        rub_lp_coef_ = 1.0f - expf(-2.0f * kPi * rub_cut / fs_);
        for(int m = 0; m < kPairs; ++m)
            UpdatePairCoeffs(m, det_scale);
    }

    /** Recompute one pair's coefficients + pan gains. Called for every
     *  pair by UpdateCoeffs(), and on its own when a pair's wandering
     *  detune has drifted far enough to matter. */
    void UpdatePairCoeffs(int m, float det_scale)
    {
        const BowlProfile& p = GetBowlProfile(profile_);
        const float f0  = fundamental_ * p.ratio[m];
        const float det = p.detune_hz[m] * det_scale * shimmer_mult_
                          * detune_jitter_ * wander_[m];
        const float t60 = p.t60_s[m] * decay_mult_;
        const float ra = SetResonator(res_[2 * m], f0, t60);
        const float rb = SetResonator(res_[2 * m + 1], f0 + det, t60);
        wander_applied_[m] = wander_[m];

        // Pair input gain: sin(w) equalizes the impulse-response
        // peak across modes (a two-pole resonator's response peaks
        // at ~g / sin(w)); 0 for a muted (above-Nyquist) resonator.
        pair_in_[m] = PairInputGain(f0);

        // Clean rim drive: rotation-oscillator coefficients locked to
        // the pair's resonant frequencies (phase states are untouched,
        // so retunes and wander steps never click). A sustained sine at
        // resonance builds to A / (2(1-r)), so the drive is normalized
        // by sqrt(1-r): the sustained level scales with 1/sqrt(1-r) -
        // the same Decay-knob law as broadband friction noise - and
        // stays bounded at the longest decays. Muted (above-band)
        // members get zero drive and no rotation.
        if(ra > 0.0f)
        {
            const float w = 2.0f * kPi * f0 / fs_;
            cos_a_[m] = cosf(w);
            sin_a_[m] = sinf(w);
            clean_amp_a_[m] = kCleanDrive * sqrtf(1.0f - ra)
                              * pair_in_[m] * rub_weight_[m];
        }
        else
        {
            cos_a_[m]       = 1.0f;
            sin_a_[m]       = 0.0f;
            clean_amp_a_[m] = 0.0f;
        }
        if(rb > 0.0f)
        {
            const float w = 2.0f * kPi * (f0 + det) / fs_;
            cos_b_[m] = cosf(w);
            sin_b_[m] = sinf(w);
            clean_amp_b_[m] = kCleanDrive * sqrtf(1.0f - rb)
                              * pair_in_[m] * rub_weight_[m];
        }
        else
        {
            cos_b_[m]       = 1.0f;
            sin_b_[m]       = 0.0f;
            clean_amp_b_[m] = 0.0f;
        }

        // Stereo: A slightly left, B slightly right; higher modes
        // wider; the fundamental pair stays near-centre. kVoiceGain
        // leaves headroom for the voices.
        constexpr float kVoiceGain = 0.6f;
        const float     w = 0.06f + 0.035f * static_cast<float>(m);
        ga_l_[m] = (0.5f + w) * kVoiceGain;
        ga_r_[m] = (0.5f - w) * kVoiceGain;
        gb_l_[m] = (0.5f - w) * kVoiceGain;
        gb_r_[m] = (0.5f + w) * kVoiceGain;
    }

    /** Block-rate "life": advance the swirl LFOs and the wandering
     *  detune. All trig lives here - never per sample. */
    void UpdateLife(size_t n)
    {
        const float det_scale = fundamental_ / 220.0f;
        const float block_s   = static_cast<float>(n) / fs_;
        for(int m = 0; m < kPairs; ++m)
        {
            // Rotating shimmer: slow orbit, L/R in quadrature, so the
            // pair's energy circles the stereo field.
            swirl_phase_[m] += swirl_inc_[m] * static_cast<float>(n);
            if(swirl_phase_[m] > 6.2831853f)
                swirl_phase_[m] -= 6.2831853f;
            constexpr float kSwirlDepth = 0.18f;
            swirl_l_t_[m] = 1.0f + kSwirlDepth * cosf(swirl_phase_[m]);
            swirl_r_t_[m] = 1.0f + kSwirlDepth * sinf(swirl_phase_[m]);

            // Wandering beat: random-walk the pair's detune multiplier
            // within +/-8%, ~2.5 s drift constant, new target every
            // ~0.7-2.5 s. The beat never settles into a metronome.
            wander_hold_[m] -= static_cast<int>(n);
            if(wander_hold_[m] <= 0)
            {
                wander_target_[m]
                    = 0.92f + 0.16f * (Noise() * 0.5f + 0.5f);
                const float hold_s
                    = 0.7f + 1.8f * (Noise() * 0.5f + 0.5f);
                wander_hold_[m] = static_cast<int>(hold_s * fs_);
            }
            wander_[m]
                += (block_s / 2.5f) * (wander_target_[m] - wander_[m]);
            if(wander_[m] > wander_applied_[m] + 0.003f
               || wander_[m] < wander_applied_[m] - 0.003f)
            {
                // Steps are ~0.3% of the detune (<= ~0.15 Hz), so
                // retuning the ringing pair is inaudible.
                UpdatePairCoeffs(m, det_scale);
            }

            // Clean-drive oscillators: the rotation recurrence drifts
            // in magnitude over minutes of float math - pull back to
            // unit amplitude once per block (cheap Newton step, no
            // trig). Frozen states (full-texture mode, no pad) are
            // unaffected.
            if(char_clean_ > 0.0f || pad_env_ > 0.0f)
            {
                RenormOsc(osc_sa_[m], osc_ca_[m]);
                RenormOsc(osc_sb_[m], osc_cb_[m]);
            }
        }
    }

    /** Pull a rotation oscillator's state back to unit magnitude
     *  (Newton step for 1/sqrt, no divide). Block-rate only. */
    static void RenormOsc(float& s, float& c)
    {
        const float mag2 = s * s + c * c;
        if(mag2 > 1.0e-12f)
        {
            const float corr = 0.5f * (3.0f - mag2);
            s *= corr;
            c *= corr;
        }
    }

    /** Set a resonator's coefficients; returns the pole radius r (0
     *  when muted) for the clean-drive normalization. */
    float SetResonator(Resonator& r, float freq, float t60)
    {
        if(freq < 20.0f || freq > 0.45f * fs_ || t60 <= 0.0f)
        {
            // Above-Nyquist (high modes of the highest bowls) or
            // degenerate: keep the resonator silent.
            r.c1 = r.c2 = 0.0f;
            return 0.0f;
        }
        const float rad   = 2.0f * kPi * freq / fs_;
        const float decay = expf(-6.907755f / (t60 * fs_));
        r.c1 = 2.0f * decay * cosf(rad);
        r.c2 = decay * decay;
        return decay;
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
    float     shimmer_target_mult_ = 1.0f;
    bool      shimmer_change_pending_ = false;
    float     brightness_    = 0.5f;
    float     fundamental_   = 220.0f;
    float     intensity_     = 0.0f;
    float     detune_jitter_ = 1.0f;
    float     envelope_      = 0.0f;
    int       burst_pos_     = 0;
    int       burst_len_     = 1;   // force-contact time in samples
    int       noise_len_     = 1;   // contact-tick sub-burst in samples
    float     pulse_gain_    = 0.0f; // contact-bump gain (per strike)
    float     restrike_damp_ = 1.0f; // per-sample contact damping (per strike)
    float     ring_fade_coef_ = 1.0f; // per-sample ghost fade (pad steal)
    int       ring_fade_left_ = 0;    // ghost-fade samples remaining
    float     exc_lp_coef_   = 0.2f; // contact-noise lowpass (per strike)
    float     exc_lp1_       = 0.0f; // contact-noise filter states
    float     exc_lp2_       = 0.0f;
    float     diag_force_    = 1.0f; // test hook: force-bump scale
    float     diag_noise_    = 1.0f; // test hook: contact-noise scale
    float     rub_level_     = 0.0f;
    float     rub_smooth_    = 0.0f;
    float     rub_lp_        = 0.0f;  // lowpassed rub-noise states (2-pole)
    float     rub_lp2_       = 0.0f;
    float     rub_lp_coef_   = 0.08f; // friction-band lowpass coefficient
    float     char_target_   = 0.0f;  // rim character: 0 texture, 1 clean
    float     char_clean_    = 0.0f;  // smoothed (exact-landing ramp)
    float     char_step_     = 2.0e-4f; // per-sample step (~100 ms)
    bool      pad_gate_      = false; // clean-pad gate (Rimming mode)
    float     pad_env_       = 0.0f;  // pad-drive envelope 0..1
    float     pad_attack_coef_  = 5.0e-6f; // 10 s to 90% (set in Init)
    float     pad_release_coef_ = 5.0e-6f; // 10 s to 10% (set in Init)
    float     pan_l_         = 1.0f;  // smoothed pan gains
    float     pan_r_         = 1.0f;
    float     pan_l_target_  = 1.0f;
    float     pan_r_target_  = 1.0f;
    float     gain_          = 1.0f;  // smoothed per-bowl volume trim
    float     gain_target_   = 1.0f;
    float     perf_gain_     = 1.0f;  // smoothed MIDI performance gain
    float     perf_gain_target_ = 1.0f;
    int       key_           = -1;
    int       voice_mode_    = -1; // originating play mode (identity)
    uint32_t  rng_           = 0x12345678u;

    Resonator res_[kResonators];
    float     in_weight_[kPairs];  // per-mode excitation weight (strike)
    float     rub_weight_[kPairs]; // per-mode rub weight (low modes)
    float     pair_in_[kPairs];   // per-pair input gain (normalization)
    float     ga_l_[kPairs];
    float     ga_r_[kPairs];
    float     gb_l_[kPairs];
    float     gb_r_[kPairs];

    // Rotating shimmer: slow per-pair stereo orbit.
    float swirl_phase_[kPairs];   // LFO phase, radians
    float swirl_inc_[kPairs];     // per-sample phase increment
    float swirl_l_[kPairs];       // smoothed per-pair swirl gains
    float swirl_r_[kPairs];
    float swirl_l_t_[kPairs];     // block-rate targets
    float swirl_r_t_[kPairs];

    // Wandering beat: slow per-pair detune random walk.
    float wander_[kPairs];         // current detune multiplier
    float wander_target_[kPairs];  // where it is drifting to
    float wander_applied_[kPairs]; // multiplier in the current coeffs
    int   wander_hold_[kPairs];    // samples until a new target is picked

    // Clean rim drive: one phase-continuous rotation oscillator per
    // resonator (16 per voice), coefficient-locked to the resonator
    // frequencies (UpdatePairCoeffs) and advanced per sample only while
    // the clean character is active.
    float osc_sa_[kPairs];      // quadrature states, member A
    float osc_ca_[kPairs];
    float osc_sb_[kPairs];      // quadrature states, member B
    float osc_cb_[kPairs];
    float cos_a_[kPairs];       // rotation coefficients (block-rate)
    float sin_a_[kPairs];
    float cos_b_[kPairs];
    float sin_b_[kPairs];
    float clean_amp_a_[kPairs]; // decay-normalized per-mode drive
    float clean_amp_b_[kPairs];
};

} // namespace bowls
