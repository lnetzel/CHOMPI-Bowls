/** @file StereoBbdDelay.h
 *  @brief Warm, stereo BBD-inspired delay: two independent caller-owned
 *  float lines with fractional reads, stereo spread/modulation, dark
 *  bounded feedback, click-free mix ramps and delay-time glide.
 *
 *  The delay lines are caller-owned so firmware can place them in SDRAM.
 *  Init() clears all buffers and state; call it only after SDRAM is ready.
 *  The class has no Daisy dependencies and runs continuously at dry mix.
 */
#pragma once
#include <stdint.h>
#include <math.h>

namespace chompi
{

class StereoBbdDelay
{
  public:
    static constexpr int kBufferSize = 65536;
    static constexpr float kMinDelayMs = 40.f;
    static constexpr float kMaxDelayMs = 1000.f;
    static constexpr float kDefaultDelayMs = 500.f;
    static constexpr float kMixRampSeconds = .010f;
    static constexpr float kDefaultFeedback = .35f;
    static constexpr float kMaxFeedback = 1.2f;
    static constexpr float kDarkenLpHz = 2800.f;
    static constexpr float kLowCutHz = 40.f;
    static constexpr float kInputHighPassHz = 100.f;
    static constexpr float kStereoSpread = .01f;
    static constexpr float kModDepthLMs = .5f;
    static constexpr float kModDepthRMs = .7f;
    static constexpr float kModLfoLHz = .25f;
    static constexpr float kModLfoRHz = .37f;
    static constexpr float kTimeSlewSeconds = .080f;

    void Init(float sample_rate, float *left, float *right)
    {
        sample_rate_ = sample_rate;
        buf_l_ = left;
        buf_r_ = right;
        for(int i = 0; i < kBufferSize; i++)
        {
            buf_l_[i] = 0.f;
            buf_r_[i] = 0.f;
        }
        write_ = 0;
        mix_ = mix_target_ = 0.f;
        mix_inc_ = 1.f / (kMixRampSeconds * sample_rate);
        feedback_ = feedback_target_ = kDefaultFeedback;
        feedback_inc_ = mix_inc_;
        lp_coeff_ = 1.f - expf(-6.283185307179586f * kDarkenLpHz
                                / sample_rate);
        hp_coeff_ = expf(-6.283185307179586f * kLowCutHz / sample_rate);
        input_hp_coeff_ = expf(-6.283185307179586f * kInputHighPassHz
                               / sample_rate);
        lp_l_ = lp_r_ = 0.f;
        hp_x_l_ = hp_y_l_ = 0.f;
        hp_x_r_ = hp_y_r_ = 0.f;
        input_hp_x_l_ = input_hp_y_l_ = 0.f;
        input_hp_x_r_ = input_hp_y_r_ = 0.f;
        slew_coeff_ = 1.f - expf(-1.f / (kTimeSlewSeconds * sample_rate));
        min_samples_ = kMinDelayMs * .001f * sample_rate;
        max_samples_ = kMaxDelayMs * .001f * sample_rate;
        if(max_samples_ > static_cast<float>(kBufferSize - 2))
            max_samples_ = static_cast<float>(kBufferSize - 2);
        target_ms_ = kDefaultDelayMs;
        target_samples_ = target_ms_ * .001f * sample_rate;
        delay_samples_ = target_samples_;
        for(int i = 0; i < kLfoTableSize; i++)
            lfo_table_[i] = sinf(6.283185307179586f * static_cast<float>(i)
                                 / kLfoTableSize);
        lfo_inc_l_ = kModLfoLHz * kLfoTableSize / sample_rate;
        lfo_inc_r_ = kModLfoRHz * kLfoTableSize / sample_rate;
        lfo_pos_l_ = 0.f;
        lfo_pos_r_ = kLfoTableSize * .5f;
        mod_l_samples_ = kModDepthLMs * .001f * sample_rate;
        mod_r_samples_ = kModDepthRMs * .001f * sample_rate;
    }

    void SetMix(float mix)
    {
        mix_target_ = mix < 0.f ? 0.f : (mix > 1.f ? 1.f : mix);
    }
    float GetMix() const { return mix_target_; }

    void SetFeedback(float feedback)
    {
        feedback_target_ = feedback < 0.f ? 0.f
                           : (feedback > kMaxFeedback ? kMaxFeedback
                                                      : feedback);
    }
    float GetFeedback() const { return feedback_target_; }

    void SetDelayMs(float ms)
    {
        ms = ms < kMinDelayMs ? kMinDelayMs
                              : (ms > kMaxDelayMs ? kMaxDelayMs : ms);
        target_ms_ = ms;
        target_samples_ = ms * .001f * sample_rate_;
    }
    float GetDelayMs() const { return target_ms_; }

    inline void Process(float in_l, float in_r, float &out_l, float &out_r)
    {
        const float dmix = mix_target_ - mix_;
        if(dmix > mix_inc_)
            mix_ += mix_inc_;
        else if(dmix < -mix_inc_)
            mix_ -= mix_inc_;
        else
            mix_ = mix_target_;

        const float dfb = feedback_target_ - feedback_;
        if(dfb > feedback_inc_)
            feedback_ += feedback_inc_;
        else if(dfb < -feedback_inc_)
            feedback_ -= feedback_inc_;
        else
            feedback_ = feedback_target_;

        float d = delay_samples_
                  + (target_samples_ - delay_samples_) * slew_coeff_;
        if(fabsf(target_samples_ - d) < .05f)
            d = target_samples_;
        delay_samples_ = d;

        const float d_l = ClampRead(d * (1.f - .5f * kStereoSpread)
                                    + mod_l_samples_ * Lfo(lfo_pos_l_));
        const float d_r = ClampRead(d * (1.f + .5f * kStereoSpread)
                                    + mod_r_samples_ * Lfo(lfo_pos_r_));
        lfo_pos_l_ += lfo_inc_l_;
        if(lfo_pos_l_ >= kLfoTableSize)
            lfo_pos_l_ -= kLfoTableSize;
        lfo_pos_r_ += lfo_inc_r_;
        if(lfo_pos_r_ >= kLfoTableSize)
            lfo_pos_r_ -= kLfoTableSize;

        const float rd_l = Read(buf_l_, d_l);
        const float rd_r = Read(buf_r_, d_r);
        lp_l_ += lp_coeff_ * (rd_l - lp_l_);
        lp_r_ += lp_coeff_ * (rd_r - lp_r_);
        const float hp_l = lp_l_ - hp_x_l_ + hp_coeff_ * hp_y_l_;
        hp_x_l_ = lp_l_;
        hp_y_l_ = hp_l;
        const float hp_r = lp_r_ - hp_x_r_ + hp_coeff_ * hp_y_r_;
        hp_x_r_ = lp_r_;
        hp_y_r_ = hp_r;
        const float input_hp_l
            = input_hp_coeff_ * (input_hp_y_l_ + in_l - input_hp_x_l_);
        const float input_hp_r
            = input_hp_coeff_ * (input_hp_y_r_ + in_r - input_hp_x_r_);
        input_hp_x_l_ = in_l;
        input_hp_y_l_ = input_hp_l;
        input_hp_x_r_ = in_r;
        input_hp_y_r_ = input_hp_r;
        buf_l_[write_] = SoftClip(input_hp_l + feedback_ * hp_l);
        buf_r_[write_] = SoftClip(input_hp_r + feedback_ * hp_r);
        write_ = (write_ + 1) & (kBufferSize - 1);

        if(mix_ == 0.f)
        {
            out_l = in_l;
            out_r = in_r;
        }
        else if(mix_ == 1.f)
        {
            out_l = lp_l_;
            out_r = lp_r_;
        }
        else
        {
            out_l = in_l * (1.f - mix_) + lp_l_ * mix_;
            out_r = in_r * (1.f - mix_) + lp_r_ * mix_;
        }
    }

  private:
    static constexpr int kLfoTableSize = 256;

    static float SoftClip(float x)
    {
        if(x > 1.f)
            x = 1.f;
        else if(x < -1.f)
            x = -1.f;
        return x - x * x * x * (1.f / 3.f);
    }

    float ClampRead(float samples) const
    {
        return samples < min_samples_
                   ? min_samples_
                   : (samples > max_samples_ ? max_samples_ : samples);
    }

    float Read(const float *buf, float delay) const
    {
        const int di = static_cast<int>(delay);
        const float frac = delay - static_cast<float>(di);
        const int r0 = (write_ - di) & (kBufferSize - 1);
        const int r1 = (r0 - 1) & (kBufferSize - 1);
        return buf[r0] + frac * (buf[r1] - buf[r0]);
    }

    float Lfo(float pos) const
    {
        const int i0 = static_cast<int>(pos);
        const float frac = pos - static_cast<float>(i0);
        const int i1 = (i0 + 1) & (kLfoTableSize - 1);
        return lfo_table_[i0] + frac * (lfo_table_[i1] - lfo_table_[i0]);
    }

    float *buf_l_;
    float *buf_r_;
    int write_;
    float sample_rate_;
    float mix_;
    float mix_target_;
    float mix_inc_;
    float feedback_;
    float feedback_target_;
    float feedback_inc_;
    float lp_coeff_;
    float hp_coeff_;
    float input_hp_coeff_;
    float lp_l_, lp_r_;
    float hp_x_l_, hp_y_l_;
    float hp_x_r_, hp_y_r_;
    float input_hp_x_l_, input_hp_y_l_;
    float input_hp_x_r_, input_hp_y_r_;
    float slew_coeff_;
    float min_samples_;
    float max_samples_;
    float target_ms_;
    float target_samples_;
    float delay_samples_;
    float lfo_table_[kLfoTableSize];
    float lfo_inc_l_, lfo_inc_r_;
    float lfo_pos_l_, lfo_pos_r_;
    float mod_l_samples_, mod_r_samples_;
};

} // namespace chompi