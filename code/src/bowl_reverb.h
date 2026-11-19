#pragma once

#include "Effects/reverbsc.h"

#include <cmath>
#include <cstring>

namespace bowls
{

class BowlReverb
{
  public:
    static constexpr int kMinDecayTicks = 0;
    static constexpr int kMaxDecayTicks = 32;
    static constexpr float kMinDecaySeconds = 1.0f;
    static constexpr float kMaxDecaySeconds = 20.0f;

    int Init(float sample_rate, int decay_ticks = 4)
    {
        if(sample_rate <= 16000.0f)
            return 1;

        const int predelay_samples
            = static_cast<int>(sample_rate * 0.020f + 0.5f);
        if(predelay_samples <= 0
           || predelay_samples > kMaxPredelaySamples)
            return 1;

        if(reverb_.Init(sample_rate) != 0)
            return 1;

        sample_rate_ = sample_rate;
        predelay_samples_ = predelay_samples;
        predelay_write_ = 0;
        mod_write_ = 0;
        mod_phase_ = 0.0f;
        decay_ticks_ = -1;
        mod_phase_increment_
            = kTwoPi * kModulationRateHz / sample_rate;
        memset(predelay_l_, 0, sizeof(predelay_l_));
        memset(predelay_r_, 0, sizeof(predelay_r_));
        memset(mod_l_, 0, sizeof(mod_l_));
        memset(mod_r_, 0, sizeof(mod_r_));

        highpass_l_.Init(sample_rate, 780.0f);
        highpass_r_.Init(sample_rate, 780.0f);
        const float high_cut_hz = 8000.0f;
        reverb_.SetLpFreq(high_cut_hz);
        SetDecayTicks(decay_ticks);
        return 0;
    }

    void SetDecayTicks(int ticks)
    {
        if(ticks < kMinDecayTicks)
            ticks = kMinDecayTicks;
        else if(ticks > kMaxDecayTicks)
            ticks = kMaxDecayTicks;
        if(ticks == decay_ticks_)
            return;
        decay_ticks_ = ticks;
        SetFeedbackForDecay();
    }

    static float DecaySecondsForTicks(int ticks)
    {
        if(ticks < kMinDecayTicks)
            ticks = kMinDecayTicks;
        else if(ticks > kMaxDecayTicks)
            ticks = kMaxDecayTicks;
        return kMinDecaySeconds
               + (kMaxDecaySeconds - kMinDecaySeconds)
                     * static_cast<float>(ticks)
                     / static_cast<float>(kMaxDecayTicks);
    }

    int Process(const float& in_l, const float& in_r, float* out_l,
                float* out_r)
    {
        const float filtered_l = highpass_l_.Process(in_l);
        const float filtered_r = highpass_r_.Process(in_r);
        const float delayed_l = predelay_l_[predelay_write_];
        const float delayed_r = predelay_r_[predelay_write_];
        predelay_l_[predelay_write_] = filtered_l;
        predelay_r_[predelay_write_] = filtered_r;
        if(++predelay_write_ >= predelay_samples_)
            predelay_write_ = 0;

        float wet_l = 0.0f;
        float wet_r = 0.0f;
        const int status = reverb_.Process(delayed_l, delayed_r, &wet_l, &wet_r);

        mod_l_[mod_write_] = wet_l;
        mod_r_[mod_write_] = wet_r;
        const float phase_offset = 3.14159265358979323846f;
        const float left_delay
            = ModulationDelaySamples(mod_phase_);
        const float right_delay
            = ModulationDelaySamples(mod_phase_ + phase_offset);
        *out_l = ReadModulated(mod_l_, left_delay);
        *out_r = ReadModulated(mod_r_, right_delay);
        if(++mod_write_ >= kModBufferSamples)
            mod_write_ = 0;

        mod_phase_ += mod_phase_increment_;
        if(mod_phase_ >= kTwoPi)
            mod_phase_ -= kTwoPi;
        return status;
    }

  private:
    static constexpr int kMaxPredelaySamples = 2048;
    static constexpr int kModBufferSamples = 256;
    static constexpr float kTwoPi = 6.28318530717958647692f;
    static constexpr float kModulationRateHz = 2.6f;
    static constexpr float kModulationBaseDelayMs = 1.5f;
    static constexpr float kModulationDepthMs = 0.5f;
    static constexpr float kMeanDelaySamples = 3015.5f;

    class BiquadHighpass
    {
      public:
        void Init(float sample_rate, float cutoff_hz)
        {
            const float omega = kTwoPi * cutoff_hz / sample_rate;
            const float cosine = cosf(omega);
            const float alpha = sinf(omega) * 0.70710678118f;
            const float a0 = 1.0f + alpha;
            b0_ = (1.0f + cosine) * 0.5f / a0;
            b1_ = -(1.0f + cosine) / a0;
            b2_ = b0_;
            a1_ = -2.0f * cosine / a0;
            a2_ = (1.0f - alpha) / a0;
            x1_ = x2_ = y1_ = y2_ = 0.0f;
        }

        float Process(float input)
        {
            const float output
                = b0_ * input + b1_ * x1_ + b2_ * x2_ - a1_ * y1_
                  - a2_ * y2_;
            x2_ = x1_;
            x1_ = input;
            y2_ = y1_;
            y1_ = output;
            return output;
        }

      private:
        float b0_;
        float b1_;
        float b2_;
        float a1_;
        float a2_;
        float x1_;
        float x2_;
        float y1_;
        float y2_;
    };

    float ModulationDelaySamples(float phase) const
    {
        const float base
            = sample_rate_ * (kModulationBaseDelayMs * 0.001f);
        const float depth = sample_rate_ * (kModulationDepthMs * 0.001f);
        return base + depth * sinf(phase);
    }

    float ReadModulated(const float* buffer, float delay_samples) const
    {
        float position = static_cast<float>(mod_write_) - delay_samples;
        if(position < 0.0f)
            position += static_cast<float>(kModBufferSamples);
        const int index0 = static_cast<int>(position);
        const int index1 = index0 + 1 == kModBufferSamples ? 0 : index0 + 1;
        const float fraction = position - static_cast<float>(index0);
        return buffer[index0]
               + fraction * (buffer[index1] - buffer[index0]);
    }

    void SetFeedbackForDecay()
    {
        const float decay_seconds = DecaySecondsForTicks(decay_ticks_);
        const float mean_delay_seconds = kMeanDelaySamples / sample_rate_;
        const float feedback
            = powf(0.001f, mean_delay_seconds / decay_seconds);
        reverb_.SetFeedback(feedback);
    }

    daisysp::ReverbSc reverb_;
    BiquadHighpass highpass_l_;
    BiquadHighpass highpass_r_;
    float predelay_l_[kMaxPredelaySamples];
    float predelay_r_[kMaxPredelaySamples];
    float mod_l_[kModBufferSamples];
    float mod_r_[kModBufferSamples];
    float sample_rate_;
    float mod_phase_;
    float mod_phase_increment_;
    int predelay_samples_;
    int predelay_write_;
    int mod_write_;
    int decay_ticks_;
};

} // namespace bowls
