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
//   Physical encoder 1 (SW4/ENC_4): bowl character - profile select 1-5
//                         (LED2 orange; five 3-key orange sections on the
//                         white keys; two detent ticks per step);
//                         SHIFT+turn = pan the selected bowl (the last
//                         struck white key) left/right - a cyan bar from
//                         white key 8 toward the panned side
//   Physical encoder 2 (SW1/ENC_1): strike hardness (LED3 amber, amber
//                         bar); SHIFT+turn = brightness (yellow-white bar)
//   Physical encoder 3 (SW2/ENC_2): decay (LED4 blue, blue bar)
//   Physical encoder 4 (SW3/ENC_3): shimmer - paired-mode detune (LED5
//                         purple, centered purple bar)
//   Transport wheel     : rim rubbing - the turn rate drives a sustained
//                         excitation of the held / most-recent voice
//                         (slow = soft, fast = loud; centered white bar;
//                         the wheel LEDs follow the rub level)
//   Volume encoder      : master volume (green->amber->red bar, LED10
//                         follows the gradient; the bar shows briefly at
//                         boot)
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
};

// White-key bars are temporary: refreshed while turning, gone 500 ms
// later (the CHOMPI-PhaseOff overlay behavior).
constexpr uint32_t kOverlayMs = 500;

struct Params
{
    Params()
    {
        for(int w = 0; w < kWhiteKeys; ++w)
            pan_ticks[w].store(chompi::BowlsPreset::kDefaultPan,
                               std::memory_order_relaxed);
    }

    std::atomic<int>      volume_ticks{chompi::BowlsPreset::kDefaultVolume};
    std::atomic<int>      strike_ticks{chompi::BowlsPreset::kDefaultStrike};
    std::atomic<int>      brightness_ticks{
        chompi::BowlsPreset::kDefaultBrightness};
    std::atomic<int>      decay_ticks{chompi::BowlsPreset::kDefaultDecay};
    std::atomic<int>      shimmer_ticks{chompi::BowlsPreset::kDefaultShimmer};
    std::atomic<int>      profile_index{chompi::BowlsPreset::kDefaultProfile};

    // Per-bowl pan positions: one 0..32 tick value per white key
    // (16 = centre). SHIFT+encoder 1 pans the selected bowl.
    std::atomic<int>      pan_ticks[kWhiteKeys];

    // UI overlay: which parameter bar is shown on the white keys.
    std::atomic<int>      overlay{static_cast<int>(Overlay::Volume)};
    std::atomic<uint32_t> overlay_until{0};
};

Params params;

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
    voices[v].SetKey(white_ordinal);
    voices[v].Strike(bowls::kBowlScaleHz[white_ordinal],
                     snorm,
                     amp_jitter,
                     det_jitter);
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

    // Physical encoder 1: bowl character (profile 1-5); SHIFT+turn pans
    // the selected bowl (the last struck white key).
    const int char_inc = hw.EncoderIncrement(0);
    if(char_inc != 0)
    {
        if(shift)
        {
            // Pan the selected bowl one tick per detent; a voice holding
            // that key is re-panned live (smoothed inside the voice).
            const int key = last_white_key_;
            int       pan
                = params.pan_ticks[key].load(std::memory_order_relaxed)
                  + char_inc;
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
        else
        {
            // Profile select: two detent ticks per step; the bar shows
            // on any turn so the section is visible while arming.
            const int step = SectionStep(profile_sel_acc, char_inc);
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

    // Volume encoder: master volume.
    BumpTicks(params.volume_ticks, hw.EncoderIncrement(5), Overlay::Volume, now);

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
    const float  volume_target
        = TicksToNorm(params.volume_ticks.load(std::memory_order_relaxed));
    for(size_t i = 0; i < size; ++i)
    {
        daisysp::fonepole(volume_smooth, volume_target, 0.001f);
        const float gain  = volume_smooth * volume_smooth;
        const float left  = daisysp::SoftClip(mix_l[i] * gain);
        const float right = daisysp::SoftClip(mix_r[i] * gain);
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

/** Directional pan bar, centered on white key 8 (index 7): panning
 *  left fills the bar toward key 1, panning right toward key 15; dead
 *  centre lights key 8 alone. */
void DrawPanBar(int ticks)
{
    const int offset = ticks - 16; // -16 .. +16
    const int span   = ((offset < 0 ? -offset : offset) * 7 + 8) / 16; // 0..7
    const int lo     = offset < 0 ? 7 - span : 7;
    const int hi     = offset < 0 ? 7 : 7 + span;
    for(int white = lo; white <= hi; ++white)
        chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[white], 0, 40, 40);
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
                    std::memory_order_relaxed));
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
    chompi_leds::SetPthLed(chompi_leds::kPthEncoder1, 20, 9, 0);  // character
    chompi_leds::SetPthLed(chompi_leds::kPthEncoder2, 20, 11, 0); // strike
    chompi_leds::SetPthLed(chompi_leds::kPthEncoder3, 0, 8, 20);  // decay
    chompi_leds::SetPthLed(chompi_leds::kPthEncoder4, 14, 0, 20); // shimmer
    const uint8_t rub = static_cast<uint8_t>(rub_level * 40.0f);
    chompi_leds::SetPthLed(chompi_leds::kPthWheelLeft, rub, rub, rub);
    chompi_leds::SetPthLed(chompi_leds::kPthWheelRight, rub, rub, rub);
    // The volume knob LED follows the bar's green->amber->red gradient.
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
        p.pan[w] = static_cast<uint8_t>(
            params.pan_ticks[w].load(std::memory_order_relaxed));
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
        params.pan_ticks[w].store(p.pan[w], std::memory_order_relaxed);
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