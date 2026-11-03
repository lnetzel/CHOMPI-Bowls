// CHOMPI-BOWLS.
//
// A four-voice modal singing-bowl instrument (plan:
// plans/bowls-v1-first-firmware.md). Each white-key strike excites eight
// paired resonant modes (16 resonators per voice) with per-mode decay,
// paired-mode beating (shimmer) and a spectrally shaped strike burst.
// The 15 white keys play a measured bowl scale (bowl_scale.h) - each key
// is a particular bowl, not a semitone; the black keys are unassigned in
// v1. All parameters autosave to BOWLS/state.json on the SD card and are
// restored at boot.
//
//   Physical encoder 1 (SW4/ENC_4): per-bowl editor for the selected
//                         bowl (the last struck white key); press
//                         cycles three pages. Page 1: bowl volume
//                         (LED2 green, green bar; 16 = unity, 32 =
//                         +6 dB; rides ringing voices live);
//                         SHIFT+turn = pan the bowl left/right (cyan
//                         bar from white key 8 toward the panned
//                         side). Page 2: resonance character -
//                         profile select 1-5 (LED2 orange; five 3-key
//                         orange sections; two detent ticks per
//                         step). Page 3: tuning (LED2 violet, violet
//                         bar from white key 8; 16 = unison, 0 =
//                         half, 32 = double; next strike)
//   Physical encoder 2 (SW1/ENC_1): strike hardness (LED3 amber, amber
//                         bar); SHIFT+turn = brightness (yellow-white bar)
//   Physical encoder 3 (SW2/ENC_2): decay (LED4 blue, blue bar)
//   Physical encoder 4 (SW3/ENC_3): shimmer - paired-mode detune (LED5
//                         purple, centered purple bar)
//   Transport wheel     : rim rubbing - the turn rate drives a sustained
//                         excitation of the held / most-recent voice
//                         (slow = soft, fast = loud; centered white bar;
//                         the wheel LEDs follow the rub level)
//   Volume encoder      : press cycles page 1 master volume (green->
//                         amber->red bar, LED10 follows the gradient;
//                         the bar shows briefly at boot) / page 2 temple-
//                         room reverb dry/wet mix (pale ice bar + LED)
//   SHIFT (KEY_26)      : modifier only; PLAY (KEY_27), LOOP (KEY_28) and
//                         the mode toggle (SW_TOG) are reserved in v1
//
// Every encoder turn shows its bar on the 15 white keys for ~500 ms (the
// CHOMPI-PhaseOff overlay pattern); with no bar up, held keys light white
// and ringing voices glow dim amber on their key.
//
// Audio: 48 kHz, block 24, dual codec - headphones are outputs 0/1 and
// the master line out is outputs 2/3 (per the TAPE firmware channel map).

#include "chompi_hardware.h"
#include "leds.h"
#include "bowl_profile.h"
#include "bowl_scale.h"
#include "bowl_voice.h"
#include "BowlsPreset.h"
#include "PresetStorage.h"

#include "Utility/dsp.h"
#include "Effects/reverbsc.h"

#include <atomic>
#include <cmath>
#include <cstdint>

#ifdef CPU_METER
#include "util/CpuLoadMeter.h"
#endif

using namespace daisy;

namespace
{
constexpr float kSampleRate = 48000.0f;
constexpr int   kBlockSize  = 24;

chompi::ChompiHardware hw;
chompi::PresetStorage  preset_storage;

constexpr int  kVoiceCount = 4;
bowls::BowlVoice voices[kVoiceCount];

// Stereo temple-room reverb (DaisySP Freeverb derivative) on the bowl
// bus: a small wooden room - short tail, highs damped (wood absorbs
// them). Character is fixed; only the dry/wet mix is on the panel.
// The delay buffers (~387 KB) live in SDRAM - SRAM is for state that
// must be fast, not for bulk audio memory.
daisysp::ReverbSc DSY_SDRAM_BSS reverb;

// Strike-transient darkening of the reverb SEND: a real mallet thud
// barely excites a room - the room blooms on the sustained ring. So at
// the moment of a strike the send's two-pole lowpass closes to a dark
// ~1.2 kHz and re-opens toward ~9 kHz over ~180 ms; the dry path is
// untouched, so the strike stays present while the room's answer to
// the contact burst stays soft. (The send is filtered, not the voice:
// the bowl itself keeps its full spectrum.)
float       send_dark_       = 0.0f; // 1 at the strike instant, -> 0
float       send_lp_l_       = 0.0f; // send-filter states (2-pole)
float       send_lp_r_       = 0.0f;
float       send_lp2_l_      = 0.0f;
float       send_lp2_r_      = 0.0f;
constexpr float kSendDarkRelease = 0.00011574f; // ~180 ms at 48 kHz
constexpr float kSendLpDark      = 0.1454f;     // ~1.2 kHz one-pole
constexpr float kSendLpOpen      = 0.6922f;     // ~9 kHz (~open)

// ---------------------------------------------------------------------------
// Parameter state
// ---------------------------------------------------------------------------

constexpr int kTickCount = 32; // 0..32 -> 33 positions
constexpr int kWhiteKeys = 15; // one bowl per white key

enum class Overlay : int
{
    None = 0,
    Volume,
    Strike,
    Brightness,
    Decay,
    Shimmer,
    Character,
    Rub,
    Pan,
    ReverbMix,
    BowlVolume,
    Tune,
};

// White-key bars are temporary: refreshed while turning, gone 500 ms
// later (the CHOMPI-PhaseOff overlay behavior).
constexpr uint32_t kOverlayMs = 500;

struct Params
{
    Params()
    {
        for(int w = 0; w < kWhiteKeys; ++w)
        {
            pan_ticks[w].store(chompi::BowlsPreset::kDefaultPan,
                               std::memory_order_relaxed);
            bowl_vol_ticks[w].store(chompi::BowlsPreset::kDefaultBowlVol,
                                    std::memory_order_relaxed);
            tune_ticks[w].store(chompi::BowlsPreset::kDefaultTune,
                                std::memory_order_relaxed);
        }
    }

    std::atomic<int>      volume_ticks{chompi::BowlsPreset::kDefaultVolume};
    std::atomic<int>      strike_ticks{chompi::BowlsPreset::kDefaultStrike};
    std::atomic<int>      brightness_ticks{
        chompi::BowlsPreset::kDefaultBrightness};
    std::atomic<int>      decay_ticks{chompi::BowlsPreset::kDefaultDecay};
    std::atomic<int>      shimmer_ticks{chompi::BowlsPreset::kDefaultShimmer};
    std::atomic<int>      profile_index{chompi::BowlsPreset::kDefaultProfile};

    // Per-bowl pan positions: one 0..32 tick value per white key
    // (16 = centre). Encoder 1 page 1, SHIFT+turn pans the selected
    // bowl.
    std::atomic<int>      pan_ticks[kWhiteKeys];

    // Per-bowl volume trims: 0..32 ticks per white key (16 = unity).
    // Encoder 1 page 1 turn; rides ringing voices live.
    std::atomic<int>      bowl_vol_ticks[kWhiteKeys];

    // Per-bowl tuning: 0..32 ticks per white key (16 = unison, ratio
    // 1/2x..2x; applied at the next strike). Encoder 1 page 3 turn.
    std::atomic<int>      tune_ticks[kWhiteKeys];

    // Volume knob page 2: temple-room reverb dry/wet mix.
    std::atomic<int>      reverb_mix_ticks{
        chompi::BowlsPreset::kDefaultReverbMix};

    // UI overlay: which parameter bar is shown on the white keys.
    std::atomic<int>      overlay{static_cast<int>(Overlay::Volume)};
    std::atomic<uint32_t> overlay_until{0};
};

Params params;

// Volume-knob pages (press cycles): master volume -> reverb mix.
constexpr int kVolPageVolume = 0;
constexpr int kVolPageReverb = 1;
volatile int  vol_page_      = kVolPageVolume;

// Encoder-1 pages (press cycles): bowl volume -> character -> tuning.
constexpr int kEnc1PageVolume    = 0;
constexpr int kEnc1PageCharacter = 1;
constexpr int kEnc1PageTune      = 2;
volatile int  enc1_page_         = kEnc1PageVolume;

// Autosave: a parameter change re-arms a 1 s debounce; the main loop
// writes BOWLS/state.json once the panel has been quiet for 1 s.
volatile bool     params_dirty      = false;
volatile uint32_t params_changed_at = 0;

void MarkDirty(uint32_t now)
{
    params_dirty      = true;
    params_changed_at = now;
}

float TicksToNorm(int ticks)
{
    return static_cast<float>(ticks) / static_cast<float>(kTickCount);
}

float PanNormTicks(int ticks)
{
    return static_cast<float>(ticks - 16) / 16.0f; // -1 (left) .. +1 (right)
}

void BumpTicks(std::atomic<int>& ticks, int inc, Overlay overlay, uint32_t now)
{
    if(inc == 0)
        return;
    int value = ticks.load(std::memory_order_relaxed) + inc;
    value     = value < 0 ? 0 : (value > kTickCount ? kTickCount : value);
    if(value != ticks.load(std::memory_order_relaxed))
    {
        ticks.store(value, std::memory_order_relaxed);
        params.overlay.store(static_cast<int>(overlay),
                             std::memory_order_relaxed);
        params.overlay_until.store(now + kOverlayMs,
                                   std::memory_order_relaxed);
        MarkDirty(now);
    }
}

void ShowOverlay(Overlay overlay, uint32_t now)
{
    params.overlay.store(static_cast<int>(overlay), std::memory_order_relaxed);
    params.overlay_until.store(now + kOverlayMs, std::memory_order_relaxed);
}

// ticks -> DSP mappings
float DecayMultTicks(int ticks)
{
    return 0.25f * powf(8.0f, TicksToNorm(ticks)); // 0.25x..2x
}

float ShimmerMultTicks(int ticks)
{
    return 2.0f * TicksToNorm(ticks); // 0..2x the profile detune
}

// Per-bowl volume: 0..16 -> squared 0..1 (16 = unity, 0 = silence),
// 16..32 -> linear 1..2x (+6 dB at 32).
float BowlGainTicks(int ticks)
{
    if(ticks <= 16)
    {
        const float n = static_cast<float>(ticks) * (1.0f / 16.0f);
        return n * n;
    }
    return 1.0f + static_cast<float>(ticks - 16) * (1.0f / 16.0f);
}

// Per-bowl tuning ratio: 2^((t-16)/16) - 16 = unison, 0 = 1/2x, 32 = 2x.
float TuneRatioTicks(int ticks)
{
    return powf(2.0f, static_cast<float>(ticks - 16) * (1.0f / 16.0f));
}

// xorshift32 for the per-strike humanization jitter
uint32_t rng_state = 0x9E3779B9u;

float RandFloat() // 0..1
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return static_cast<float>(rng_state >> 8) * (1.0f / 16777216.0f);
}

// The profile select needs TWO detent ticks per step: a single tick only
// arms the accumulator, so a stray detent or an accidental brush can't
// hop the sections (PhaseOff SectionStep pattern).
int profile_sel_acc = 0;

int SectionStep(int& acc, int inc)
{
    acc += inc;
    if(acc >= 2)
    {
        acc -= 2;
        return 1;
    }
    if(acc <= -2)
    {
        acc += 2;
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------

int          last_voice      = 0; // most recently struck voice (rub fallback)
volatile int last_white_key_ = 0; // selected bowl for SHIFT+encoder 1 panning

void ApplyDecayShimmerToVoices()
{
    const float decay_mult
        = DecayMultTicks(params.decay_ticks.load(std::memory_order_relaxed));
    const float shimmer_mult
        = ShimmerMultTicks(params.shimmer_ticks.load(std::memory_order_relaxed));
    for(int v = 0; v < kVoiceCount; ++v)
    {
        voices[v].SetDecayMult(decay_mult);
        voices[v].SetShimmerMult(shimmer_mult);
    }
}

// Brightness is an excitation-time parameter for strikes, but it also
// brightens the RUBBED modes live - so push it to the voices the moment
// it changes (turning SHIFT+encoder 2 while rubbing is heard at once).
void ApplyBrightnessToVoices()
{
    const float brightness
        = TicksToNorm(params.brightness_ticks.load(std::memory_order_relaxed));
    for(int v = 0; v < kVoiceCount; ++v)
        voices[v].SetBrightness(brightness);
}

void StrikeKey(int white_ordinal)
{
    // 1) a voice already ringing on this key is re-struck (its ring is
    //    kept and the new strike adds energy on top)
    int v = -1;
    for(int i = 0; i < kVoiceCount; ++i)
        if(voices[i].Key() == white_ordinal)
        {
            v = i;
            break;
        }
    // 2) a free voice
    if(v < 0)
        for(int i = 0; i < kVoiceCount; ++i)
            if(!voices[i].IsActive())
            {
                v = i;
                break;
            }
    // 3) steal the QUIETEST voice, not the oldest
    if(v < 0)
    {
        float quietest = 1.0e30f;
        for(int i = 0; i < kVoiceCount; ++i)
            if(voices[i].Envelope() < quietest)
            {
                quietest = voices[i].Envelope();
                v        = i;
            }
    }

    const int   profile = params.profile_index.load(std::memory_order_relaxed);
    const float snorm
        = TicksToNorm(params.strike_ticks.load(std::memory_order_relaxed));
    // Humanization: small natural variation between hits.
    const float amp_jitter = 0.90f + 0.20f * RandFloat();
    const float det_jitter = 0.90f + 0.20f * RandFloat();
    voices[v].SetProfile(profile);
    voices[v].SetBrightness(
        TicksToNorm(params.brightness_ticks.load(std::memory_order_relaxed)));
    voices[v].SetPan(PanNormTicks(
        params.pan_ticks[white_ordinal].load(std::memory_order_relaxed)));
    voices[v].SetBowlGain(BowlGainTicks(
        params.bowl_vol_ticks[white_ordinal].load(std::memory_order_relaxed)));
    voices[v].SetKey(white_ordinal);
    voices[v].Strike(bowls::kBowlScaleHz[white_ordinal]
                         * TuneRatioTicks(params.tune_ticks[white_ordinal].load(
                             std::memory_order_relaxed)),
                     snorm,
                     amp_jitter,
                     det_jitter);
    // The mallet contact burst barely excites a real room: darken the
    // reverb send (it re-opens into the pure ring over ~180 ms).
    send_dark_      = 1.0f;
    last_voice      = v;
    last_white_key_ = white_ordinal;
}

// Rim rubbing: wheel detents pump rub_target up (fast turns = louder);
// rub_level swells toward it over ~250 ms so the attack blooms naturally
// and individual detent steps are never audible; the target decays back
// to silence over ~0.35 s of no turning. The rub feeds the voices of the
// held keys, or the most recent voice when none are held.
float rub_target = 0.0f;
float rub_level  = 0.0f;

void ApplyRub()
{
    bool held[kVoiceCount] = {};
    bool any_held          = false;
    for(int i = 0; i < kVoiceCount; ++i)
    {
        const int key = voices[i].Key();
        if(key >= 0 && hw.KeyHeld(bowls::kWhiteToPitchIndex[key]))
        {
            held[i]  = true;
            any_held = true;
        }
    }
    for(int i = 0; i < kVoiceCount; ++i)
        voices[i].SetRub((held[i] || (!any_held && i == last_voice))
                             ? rub_level
                             : 0.0f);
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

/** Read encoders/keys, update parameter state and the voices.
 *  Runs once per audio block (~0.5 ms), after hw.ProcessControls(). */
void ApplyControls()
{
    const uint32_t now   = System::GetNow();
    const bool     shift = hw.ShiftHeld();

    // Volume knob press: page 1 = master volume, page 2 = reverb mix.
    // The newly active page's bar shows as confirmation.
    if(hw.VolumeClicked())
    {
        vol_page_ = (vol_page_ + 1) % 2;
        ShowOverlay(vol_page_ == kVolPageVolume ? Overlay::Volume
                                                : Overlay::ReverbMix,
                    now);
    }

    // Physical encoder 1 press: cycle page 1 bowl volume -> page 2
    // resonance character -> page 3 tuning. The newly active page's bar
    // shows as confirmation.
    if(hw.EncoderClicked(0))
    {
        enc1_page_ = (enc1_page_ + 1) % 3;
        ShowOverlay(enc1_page_ == kEnc1PageVolume
                        ? Overlay::BowlVolume
                        : (enc1_page_ == kEnc1PageCharacter
                               ? Overlay::Character
                               : Overlay::Tune),
                    now);
    }

    // Physical encoder 1: three pages editing the selected bowl (the
    // last struck white key). Page 1: turn = per-bowl volume (rides
    // ringing voices live), SHIFT+turn = pan. Page 2: resonance
    // character (profile 1-5). Page 3: per-bowl tuning (next strike).
    // SHIFT+turn on pages 2/3 is reserved.
    const int enc1_inc = hw.EncoderIncrement(0);
    if(enc1_inc != 0)
    {
        const int key = last_white_key_;
        if(enc1_page_ == kEnc1PageVolume && shift)
        {
            // Pan the selected bowl one tick per detent; a voice holding
            // that key is re-panned live (smoothed inside the voice).
            int pan
                = params.pan_ticks[key].load(std::memory_order_relaxed)
                  + enc1_inc;
            pan = pan < 0 ? 0 : (pan > kTickCount ? kTickCount : pan);
            if(pan != params.pan_ticks[key].load(std::memory_order_relaxed))
            {
                params.pan_ticks[key].store(pan, std::memory_order_relaxed);
                const float pan_norm = PanNormTicks(pan);
                for(int v = 0; v < kVoiceCount; ++v)
                    if(voices[v].Key() == key)
                        voices[v].SetPan(pan_norm);
                MarkDirty(now);
            }
            ShowOverlay(Overlay::Pan, now); // also shows at the end stops
        }
        else if(enc1_page_ == kEnc1PageVolume)
        {
            // Per-bowl volume: rides the ringing voice live (smoothed
            // inside the voice, like the pan gains).
            int vol
                = params.bowl_vol_ticks[key].load(std::memory_order_relaxed)
                  + enc1_inc;
            vol = vol < 0 ? 0 : (vol > kTickCount ? kTickCount : vol);
            if(vol
               != params.bowl_vol_ticks[key].load(std::memory_order_relaxed))
            {
                params.bowl_vol_ticks[key].store(vol,
                                                 std::memory_order_relaxed);
                const float gain = BowlGainTicks(vol);
                for(int v = 0; v < kVoiceCount; ++v)
                    if(voices[v].Key() == key)
                        voices[v].SetBowlGain(gain);
                MarkDirty(now);
            }
            ShowOverlay(Overlay::BowlVolume, now); // also at the end stops
        }
        else if(enc1_page_ == kEnc1PageCharacter && !shift)
        {
            // Profile select: two detent ticks per step; the bar shows
            // on any turn so the section is visible while arming.
            const int step = SectionStep(profile_sel_acc, enc1_inc);
            int       profile
                = params.profile_index.load(std::memory_order_relaxed) + step;
            profile = profile < 0 ? 0
                                  : (profile >= bowls::kNumProfiles
                                         ? bowls::kNumProfiles - 1
                                         : profile);
            if(profile
               != params.profile_index.load(std::memory_order_relaxed))
            {
                params.profile_index.store(profile,
                                           std::memory_order_relaxed);
                MarkDirty(now);
            }
            ShowOverlay(Overlay::Character, now);
        }
        else if(enc1_page_ == kEnc1PageTune && !shift)
        {
            // Per-bowl tuning: applied at the next strike (physical
            // character); only the bar shows while a bowl rings.
            int tune
                = params.tune_ticks[key].load(std::memory_order_relaxed)
                  + enc1_inc;
            tune = tune < 0 ? 0 : (tune > kTickCount ? kTickCount : tune);
            if(tune
               != params.tune_ticks[key].load(std::memory_order_relaxed))
            {
                params.tune_ticks[key].store(tune,
                                             std::memory_order_relaxed);
                MarkDirty(now);
            }
            ShowOverlay(Overlay::Tune, now); // also at the end stops
        }
    }

    // Physical encoder 2: strike hardness; SHIFT+turn = brightness
    // (brightness is pushed to the voices live: it brightens rubbing
    // immediately and the next strike).
    const int strike_inc = hw.EncoderIncrement(1);
    if(shift)
    {
        BumpTicks(params.brightness_ticks, strike_inc, Overlay::Brightness, now);
        ApplyBrightnessToVoices();
    }
    else
    {
        BumpTicks(params.strike_ticks, strike_inc, Overlay::Strike, now);
    }

    // Physical encoder 3: decay (live-updates ringing voices).
    const int decay_inc = hw.EncoderIncrement(2);
    if(decay_inc != 0)
    {
        BumpTicks(params.decay_ticks, decay_inc, Overlay::Decay, now);
        ApplyDecayShimmerToVoices();
    }

    // Physical encoder 4: shimmer / paired-mode detune (also live).
    const int shimmer_inc = hw.EncoderIncrement(3);
    if(shimmer_inc != 0)
    {
        BumpTicks(params.shimmer_ticks, shimmer_inc, Overlay::Shimmer, now);
        ApplyDecayShimmerToVoices();
    }

    // Volume encoder: page 1 = master volume, page 2 = reverb dry/wet.
    const int volume_inc = hw.EncoderIncrement(5);
    if(vol_page_ == kVolPageReverb)
        BumpTicks(params.reverb_mix_ticks, volume_inc, Overlay::ReverbMix, now);
    else
        BumpTicks(params.volume_ticks, volume_inc, Overlay::Volume, now);

    // Transport wheel: rim rubbing. Detents pump the rub TARGET (the
    // turn rate sets the level); the applied level swells toward it over
    // ~250 ms, so the attack blooms naturally instead of snapping on at
    // the first tick.
    const int wheel_inc = hw.EncoderIncrement(4);
    if(wheel_inc != 0)
    {
        rub_target += 0.06f * static_cast<float>(wheel_inc < 0 ? -wheel_inc
                                                               : wheel_inc);
        if(rub_target > 1.0f)
            rub_target = 1.0f;
        ShowOverlay(Overlay::Rub, now);
    }
    rub_target *= 0.9990f; // ~0.35 s half-life once the wheel stops
    rub_level += 0.002f * (rub_target - rub_level); // ~250 ms swell
    ApplyRub();

    // Keys: the 15 white keys strike bowls; the black keys are
    // unassigned in v1 (kWhiteOrdinal < 0).
    for(int key = 0; key < 25; ++key)
    {
        const int white = bowls::kWhiteOrdinal[key];
        if(white >= 0 && hw.KeyRising(key))
            StrikeKey(white);
    }
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
#ifdef CPU_METER
    static CpuLoadMeter meter;
    static bool         meter_init = false;
    if(!meter_init)
    {
        meter.Init(kSampleRate, size);
        meter_init = true;
    }
    meter.OnBlockStart();
#endif

    (void)in; // no input monitoring in v1

    hw.ProcessControls();
    ApplyControls();

    float mix_l[kBlockSize];
    float mix_r[kBlockSize];
    for(size_t i = 0; i < size; ++i)
    {
        mix_l[i] = 0.0f;
        mix_r[i] = 0.0f;
    }
    for(int v = 0; v < kVoiceCount; ++v)
        voices[v].ProcessBlock(mix_l, mix_r, size);

    static float volume_smooth = 0.0f;
    static float mix_smooth    = 0.0f;
    const float  volume_target
        = TicksToNorm(params.volume_ticks.load(std::memory_order_relaxed));
    const float mix_target
        = TicksToNorm(params.reverb_mix_ticks.load(std::memory_order_relaxed));
    for(size_t i = 0; i < size; ++i)
    {
        daisysp::fonepole(volume_smooth, volume_target, 0.001f);
        daisysp::fonepole(mix_smooth, mix_target, 0.001f);
        // Temple-room reverb on the stereo bowl bus, before the master
        // gain so the volume knob also controls the tail. The send is
        // lowpassed dark right after a strike (the mallet contact burst
        // barely excites a real room) and re-opens into the pure ring;
        // the dry path keeps the full spectrum.
        send_dark_ -= kSendDarkRelease * send_dark_;
        const float send_coef
            = kSendLpOpen + (kSendLpDark - kSendLpOpen) * send_dark_;
        daisysp::fonepole(send_lp_l_, mix_l[i], send_coef);
        daisysp::fonepole(send_lp_r_, mix_r[i], send_coef);
        daisysp::fonepole(send_lp2_l_, send_lp_l_, send_coef);
        daisysp::fonepole(send_lp2_r_, send_lp_r_, send_coef);
        float wet_l, wet_r;
        reverb.Process(send_lp2_l_, send_lp2_r_, &wet_l, &wet_r);
        const float dry   = 1.0f - mix_smooth;
        const float gain  = volume_smooth * volume_smooth;
        const float left  = daisysp::SoftClip(
            (mix_l[i] * dry + wet_l * mix_smooth) * gain);
        const float right = daisysp::SoftClip(
            (mix_r[i] * dry + wet_r * mix_smooth) * gain);
        out[0][i] = left;  // headphone L
        out[1][i] = right; // headphone R
        out[2][i] = left;  // master L
        out[3][i] = right; // master R
    }

#ifdef CPU_METER
    meter.OnBlockEnd();
    if(System::GetNow() % 500 < 1)
        hw.seed.PrintLine("CPU load: %d%%",
                          static_cast<int>(meter.GetAvgCpuLoad() * 100.0f));
#endif
}

// ---------------------------------------------------------------------------
// LED UI
// ---------------------------------------------------------------------------

/** Linear left-to-right bar on the 15 white keys. */
void DrawBar(int ticks, uint8_t red, uint8_t green, uint8_t blue)
{
    const unsigned lit = (ticks * 15 + 16) / 32;
    for(unsigned white = 0; white < lit && white < 15; ++white)
        chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[white],
                               red,
                               green,
                               blue);
}

/** Master-volume bar: green up to 50%, amber to ~73%, red above. */
void DrawVolumeBar(int ticks)
{
    const unsigned lit = (ticks * 15 + 16) / 32;
    for(unsigned white = 0; white < lit && white < 15; ++white)
    {
        const uint8_t red   = white >= 11 ? 48 : (white >= 7 ? 48 : 0);
        const uint8_t green = white >= 11 ? 0 : (white >= 7 ? 29 : 48);
        chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[white], red, green, 0);
    }
}

/** Center-out bar on white key 8 (index 7), spreading symmetrically. */
void DrawCenterBar(int ticks, uint8_t red, uint8_t green, uint8_t blue)
{
    const int spread = (ticks * 7 + 16) / 32;
    for(int white = 7 - spread; white <= 7 + spread; ++white)
        if(white >= 0 && white < 15)
            chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[white],
                                   red,
                                   green,
                                   blue);
}

/** Directional centre-detent bar, centered on white key 8 (index 7):
 *  values left of centre fill the bar toward key 1, right of centre
 *  toward key 15; dead centre lights key 8 alone. Used for pan (cyan)
 *  and tuning (violet). */
void DrawPanBar(int ticks, uint8_t red, uint8_t green, uint8_t blue)
{
    const int offset = ticks - 16; // -16 .. +16
    const int span   = ((offset < 0 ? -offset : offset) * 7 + 8) / 16; // 0..7
    const int lo     = offset < 0 ? 7 - span : 7;
    const int hi     = offset < 0 ? 7 : 7 + span;
    for(int white = lo; white <= hi; ++white)
        chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[white],
                               red,
                               green,
                               blue);
}

/** The five bowl profiles as five 3-key sections; the active one lights. */
void DrawCharacterSections(int profile)
{
    const int base = profile * 3;
    for(int white = base; white < base + 3 && white < 15; ++white)
        chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[white], 48, 22, 0);
}

void DrawOverlayBar(uint32_t now)
{
    const bool overlay_active
        = static_cast<int32_t>(
              params.overlay_until.load(std::memory_order_relaxed) - now)
          > 0;
    if(overlay_active)
    {
        switch(static_cast<Overlay>(params.overlay.load(std::memory_order_relaxed)))
        {
            case Overlay::Volume:
                DrawVolumeBar(params.volume_ticks.load(std::memory_order_relaxed));
                break;
            case Overlay::Strike:
                // Amber bar, matching the encoder 2 LED.
                DrawBar(params.strike_ticks.load(std::memory_order_relaxed),
                        48, 26, 0);
                break;
            case Overlay::Brightness:
                DrawBar(params.brightness_ticks.load(std::memory_order_relaxed),
                        44, 44, 8); // yellow-white
                break;
            case Overlay::Decay:
                // Blue bar, matching the encoder 3 LED.
                DrawBar(params.decay_ticks.load(std::memory_order_relaxed),
                        0, 20, 48);
                break;
            case Overlay::Shimmer:
                // Purple center-out bar, matching the encoder 4 LED.
                DrawCenterBar(params.shimmer_ticks.load(std::memory_order_relaxed),
                              34, 0, 48);
                break;
            case Overlay::Character:
                DrawCharacterSections(
                    params.profile_index.load(std::memory_order_relaxed));
                break;
            case Overlay::Rub:
                DrawCenterBar(static_cast<int>(rub_level * kTickCount),
                              40, 40, 40);
                break;
            case Overlay::Pan:
                DrawPanBar(params.pan_ticks[last_white_key_].load(
                               std::memory_order_relaxed),
                           0, 40, 40); // cyan
                break;
            case Overlay::ReverbMix:
                // Pale ice bar for the temple-room reverb mix.
                DrawBar(params.reverb_mix_ticks.load(std::memory_order_relaxed),
                        24, 32, 48);
                break;
            case Overlay::BowlVolume:
                // Green bar: per-bowl volume of the selected bowl.
                DrawBar(params.bowl_vol_ticks[last_white_key_].load(
                            std::memory_order_relaxed),
                        0, 48, 0);
                break;
            case Overlay::Tune:
                // Violet centre-detent bar: flat to the left, sharp to
                // the right, dead centre = unison.
                DrawPanBar(params.tune_ticks[last_white_key_].load(
                               std::memory_order_relaxed),
                           30, 0, 48);
                break;
            default: break;
        }
    }

    // Pressed keys light white, and ringing voices glow dim amber on
    // their key - but NOT while a temporary overlay bar is up, so the
    // bars stay readable while a setting is edited.
    if(!overlay_active)
    {
        for(unsigned key = 0; key < 25; ++key)
            if(hw.KeyHeld(key))
                chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[key], 48, 48, 48);
        for(int v = 0; v < kVoiceCount; ++v)
        {
            const int white = voices[v].Key();
            if(white >= 0 && voices[v].IsActive()
               && !hw.KeyHeld(bowls::kWhiteToPitchIndex[white]))
                chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[white],
                                       14, 8, 2);
        }
    }
}

void RenderLeds()
{
    // Every frame starts dark: overlays are fully redrawn each pass so
    // timed-out bars revert to the base view (held keys + ringing glow).
    for(unsigned led = 0; led < chompi_leds::kSmtLedCount; ++led)
        chompi_leds::SetSmtLed(led, 0, 0, 0);

    DrawOverlayBar(System::GetNow());

    // Panel LEDs (PTH chain).
    const bool shift = hw.ShiftHeld();
    chompi_leds::SetPthLed(chompi_leds::kPthShift,
                           shift ? 40 : 0, shift ? 40 : 0, shift ? 40 : 0);
    // The encoder-1 LED follows the page: green = bowl volume,
    // orange = resonance character, violet = tuning.
    if(enc1_page_ == kEnc1PageCharacter)
        chompi_leds::SetPthLed(chompi_leds::kPthEncoder1, 20, 9, 0);
    else if(enc1_page_ == kEnc1PageTune)
        chompi_leds::SetPthLed(chompi_leds::kPthEncoder1, 14, 0, 20);
    else
        chompi_leds::SetPthLed(chompi_leds::kPthEncoder1, 0, 20, 0);
    chompi_leds::SetPthLed(chompi_leds::kPthEncoder2, 20, 11, 0); // strike
    chompi_leds::SetPthLed(chompi_leds::kPthEncoder3, 0, 8, 20);  // decay
    chompi_leds::SetPthLed(chompi_leds::kPthEncoder4, 14, 0, 20); // shimmer
    const uint8_t rub = static_cast<uint8_t>(rub_level * 40.0f);
    chompi_leds::SetPthLed(chompi_leds::kPthWheelLeft, rub, rub, rub);
    chompi_leds::SetPthLed(chompi_leds::kPthWheelRight, rub, rub, rub);
    // The volume knob LED follows the page: master volume uses the
    // bar's green->amber->red gradient; the reverb page is pale ice.
    if(vol_page_ == kVolPageReverb)
    {
        const float mnorm = TicksToNorm(
            params.reverb_mix_ticks.load(std::memory_order_relaxed));
        const auto mlevel = static_cast<uint8_t>(8 + mnorm * 40.0f);
        chompi_leds::SetPthLed(chompi_leds::kPthVolume,
                               mlevel * 2 / 3, mlevel * 4 / 5, mlevel);
    }
    else
    {
        const int   vt     = params.volume_ticks.load(std::memory_order_relaxed);
        const float vnorm  = TicksToNorm(vt);
        const auto  level  = static_cast<uint8_t>(8 + vnorm * 40.0f);
        if(vt >= 23)
            chompi_leds::SetPthLed(chompi_leds::kPthVolume, level, 0, 0); // red
        else if(vt >= 14)
            chompi_leds::SetPthLed(chompi_leds::kPthVolume,
                                   level, level * 5 / 6, 0); // amber
        else
            chompi_leds::SetPthLed(chompi_leds::kPthVolume, 0, level, 0); // green
    }

    chompi_leds::Flush();
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

chompi::BowlsPreset SnapshotParams()
{
    chompi::BowlsPreset p;
    p.volume = static_cast<uint8_t>(
        params.volume_ticks.load(std::memory_order_relaxed));
    p.strike = static_cast<uint8_t>(
        params.strike_ticks.load(std::memory_order_relaxed));
    p.brightness = static_cast<uint8_t>(
        params.brightness_ticks.load(std::memory_order_relaxed));
    p.decay = static_cast<uint8_t>(
        params.decay_ticks.load(std::memory_order_relaxed));
    p.shimmer = static_cast<uint8_t>(
        params.shimmer_ticks.load(std::memory_order_relaxed));
    p.profile = static_cast<uint8_t>(
        params.profile_index.load(std::memory_order_relaxed));
    for(int w = 0; w < kWhiteKeys; ++w)
    {
        p.pan[w] = static_cast<uint8_t>(
            params.pan_ticks[w].load(std::memory_order_relaxed));
        p.bvol[w] = static_cast<uint8_t>(
            params.bowl_vol_ticks[w].load(std::memory_order_relaxed));
        p.tune[w] = static_cast<uint8_t>(
            params.tune_ticks[w].load(std::memory_order_relaxed));
    }
    p.reverb_mix = static_cast<uint8_t>(
        params.reverb_mix_ticks.load(std::memory_order_relaxed));
    return p;
}

void ApplyPreset(const chompi::BowlsPreset& p)
{
    params.volume_ticks.store(p.volume, std::memory_order_relaxed);
    params.strike_ticks.store(p.strike, std::memory_order_relaxed);
    params.brightness_ticks.store(p.brightness, std::memory_order_relaxed);
    params.decay_ticks.store(p.decay, std::memory_order_relaxed);
    params.shimmer_ticks.store(p.shimmer, std::memory_order_relaxed);
    params.profile_index.store(p.profile, std::memory_order_relaxed);
    for(int w = 0; w < kWhiteKeys; ++w)
    {
        params.pan_ticks[w].store(p.pan[w], std::memory_order_relaxed);
        params.bowl_vol_ticks[w].store(p.bvol[w],
                                       std::memory_order_relaxed);
        params.tune_ticks[w].store(p.tune[w], std::memory_order_relaxed);
    }
    params.reverb_mix_ticks.store(p.reverb_mix, std::memory_order_relaxed);
    ApplyDecayShimmerToVoices();
}

} // namespace

int main()
{
    hw.Init();
    chompi_leds::Init();
    chompi_leds::ClearAll();

    for(int v = 0; v < kVoiceCount; ++v)
        voices[v].Init(kSampleRate);
    ApplyDecayShimmerToVoices();

    // Temple-room reverb: a small wooden room, not a cathedral.
    reverb.Init(kSampleRate);
    reverb.SetFeedback(0.82f);   // short-to-medium tail
    reverb.SetLpFreq(4500.0f);   // wood damps the highs

    // SD card: mount, BOWLS/ folder, boot-load the saved state
    // (no card / no file: factory defaults, saves become inert).
    preset_storage.Init();
    chompi::BowlsPreset preset;
    if(preset_storage.Load(preset))
        ApplyPreset(preset);

    // Show the volume bar briefly at boot.
    params.overlay.store(static_cast<int>(Overlay::Volume),
                         std::memory_order_relaxed);
    params.overlay_until.store(System::GetNow() + kOverlayMs,
                               std::memory_order_relaxed);

    hw.StartAudio(AudioCallback);

    while(true)
    {
        /* Staged, non-blocking SD-card work: one small FatFS step per
         * pass, at thread level only (never in the audio ISR). */
        preset_storage.Service();

        /* autosave 1 s after the last parameter change */
        const uint32_t now = System::GetNow();
        if(params_dirty
           && static_cast<int32_t>(now - params_changed_at) >= 1000)
        {
            params_dirty = false;
            preset_storage.RequestSave(SnapshotParams());
        }

        RenderLeds();
    }
}