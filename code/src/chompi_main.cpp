// CHOMPI-BOWLS.
//
// A five-voice modal singing-bowl instrument with three play modes
// (plans: plans/bowls-v1-first-firmware.md,
// plans/split-strike-and-rimming-plan.md, plans/improve-play-modes.md).
// Each strike excites eight paired resonant modes (16 resonators per
// voice) with per-mode decay, paired-mode beating (shimmer) and a
// finite-contact mallet excitation. The five voices are shared by all
// modes; a sixth simultaneous note steals the quietest voice (a stolen
// latched note is unlatched, never silently restarted). Switching modes
// never interrupts ringing voices or reverb tails: outgoing voices keep
// their sound, and the wheel only drives notes started in the current
// mode. All parameters autosave to BOWLS/state.json on the SD card and
// are restored at boot (the mode included; the LOOP latch and all
// performance state are never saved).
//
//   Play modes            : SHIFT + lower F# (key 6) = STRIKES - all 25
//                         keys strike distinct ascending bowls; the
//                         wheel rims currently sounding, physically
//                         held strikes. SHIFT + lower G# (key 8) =
//                         RIMMING - all 25 keys hold clean sustained
//                         bowl pads (5 s attack to 90%, 10 s release
//                         to 10%); the wheel adds the direction-
//                         dependent rimming; LOOP toggles a note
//                         latch: enabling captures the held sounding
//                         pads, key-down toggles a note on/off, key-up
//                         never releases a latched note. SHIFT + lower
//                         A# (key 10) = SPLIT - the v1 layout: keys
//                         0-11 (C3-B3) SELECT one of the 12 bowls for
//                         wheel rimming (never strike), keys 12-23
//                         strike the same 12 bowls, key 24 (C5)
//                         strikes an added higher bowl. A shortcut
//                         consumes its key through release - it never
//                         sounds a note or moves the edit target.
//   Physical encoder 1 (SW4/ENC_4): per-bowl editor for the selected
//                         bowl (the last played/selected key IN THE
//                         CURRENT MODE; pan, bowl volume, tuning and
//                         resonance character are independent per key
//                         per mode); press cycles three pages. Page 1:
//                         bowl volume (LED2 green, green bar; 16 =
//                         unity, 32 = +6 dB; rides ringing voices
//                         live); SHIFT+turn = pan the bowl left/right
//                         (cyan bar from white key 8 toward the panned
//                         side). Page 2: resonance character -
//                         profile select 1-5 (LED2 orange; five 3-key
//                         orange sections; two detent ticks per
//                         step). Page 3: tuning (LED2 violet, violet
//                         bar from white key 8; 16 = unison, 0 =
//                         half, 32 = double; next strike/selection)
//   Physical encoder 2 (SW1/ENC_1): strike hardness (LED3 amber, amber
//                         bar); SHIFT+turn = brightness (yellow-white bar)
//   Physical encoder 3 (SW2/ENC_2): decay (LED4 blue, blue bar)
//   Physical encoder 4 (SW3/ENC_3): shimmer - paired-mode detune (LED5
//                         purple, centered purple bar)
//   Transport wheel     : rimming - detents pump the PER-(MODE,KEY)
//                         rub level of the wheel set: SPLIT = the held
//                         rim-zone keys (max two) or the last-pressed
//                         rim key (a bowl stays SILENT until the wheel
//                         turns while it is selected); STRIKES = the
//                         currently sounding held strikes; RIMMING =
//                         the currently sounding held or latched pads
//                         (slow = soft, fast = loud; only the two
//                         wheel LEDs follow the level - no keyboard
//                         bar). The wheel never creates a strike or
//                         revives a stolen note. The DIRECTION picks
//                         the sound: clockwise = the classic friction
//                         texture, counter-clockwise = a clean
//                         sustained bowl tone; the last direction
//                         sticks per (mode, key) while the wheel is
//                         still.
//   Volume encoder      : press cycles page 1 master volume (green->
//                         amber->red bar, LED10 follows the gradient;
//                         the bar shows briefly at boot) / page 2
//                         temple-room reverb dry/wet mix (pale ice
//                         bar + LED)
//   SHIFT (KEY_26)      : modifier + mode shortcuts; while held, the
//                         active mode's shortcut key glows blue
//   LOOP (KEY_28)       : toggles the Rimming note latch (LOOP LED lit
//                         while enabled; latched keys glow violet);
//                         reserved in the other modes. PLAY (KEY_27)
//                         and the mode toggle (SW_TOG) are reserved.
//
// Every encoder turn shows its bar on the 15 white keys for ~500 ms (the
// CHOMPI-PhaseOff overlay pattern); with no bar up, held keys light
// white and ringing voices glow dim amber on their key.
//
// Audio: 48 kHz, block 24, dual codec - headphones are outputs 0/1 and
// the master line out is outputs 2/3 (per the TAPE firmware channel map).

#include "chompi_hardware.h"
#include "leds.h"
#include "bowl_profile.h"
#include "bowl_scale.h"
#include "play_modes.h"
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

constexpr int  kVoiceCount = 5; // shared pool, quietest-voice steal
bowls::BowlVoice voices[kVoiceCount];

// Stereo temple-room reverb (DaisySP Freeverb derivative) on the bowl
// bus: a small wooden room - short tail, highs damped (wood absorbs
// them). Character is fixed; only the dry/wet mix is on the panel.
// The delay buffers (~387 KB) live in SDRAM - SRAM is for state that
// must be fast, not for bulk audio memory.
daisysp::ReverbSc DSY_SDRAM_BSS reverb;

#ifdef CPU_METER
// CPU meter with a ~5 s peak-hold: dropouts come from the WORST block,
// not the average, so the peak is what matters. The Shift LED shows it
// (green -> red) and it is printed over USB serial when logging is on.
CpuLoadMeter g_cpu_meter;
float        g_cpu_peak          = 0.0f;
uint32_t     g_steal_flash_until = 0; // encoder-4 LED steal witness
#endif

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

using bowls::PlayMode;

int ModeIdx(PlayMode mode) { return static_cast<int>(mode); }

// The play-mode state machine: mode, shortcuts, per-key press
// provenance and the Rimming LOOP latch (play_modes.h). Boot default
// is Strikes; a valid preset restores the saved mode at boot.
bowls::PlayModes play_modes;

enum class Overlay : int
{
    None = 0,
    Volume,
    Strike,
    Brightness,
    Decay,
    Shimmer,
    Character,
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
        for(int m = 0; m < bowls::kPlayModeCount; ++m)
            for(int w = 0; w < bowls::kKeyCount; ++w)
            {
                pan_ticks[m][w].store(chompi::BowlsPreset::kDefaultPan,
                                      std::memory_order_relaxed);
                bowl_vol_ticks[m][w].store(
                    chompi::BowlsPreset::kDefaultBowlVol,
                    std::memory_order_relaxed);
                tune_ticks[m][w].store(chompi::BowlsPreset::kDefaultTune,
                                       std::memory_order_relaxed);
                profile_ticks[m][w].store(
                    chompi::BowlsPreset::kDefaultProfile,
                    std::memory_order_relaxed);
            }
    }

    // Global controls (shared by all modes).
    std::atomic<int>      volume_ticks{chompi::BowlsPreset::kDefaultVolume};
    std::atomic<int>      strike_ticks{chompi::BowlsPreset::kDefaultStrike};
    std::atomic<int>      brightness_ticks{
        chompi::BowlsPreset::kDefaultBrightness};
    std::atomic<int>      decay_ticks{chompi::BowlsPreset::kDefaultDecay};
    std::atomic<int>      shimmer_ticks{chompi::BowlsPreset::kDefaultShimmer};

    // Per-mode/per-key banks [mode][key]: pan (16 = centre), bowl
    // volume (16 = unity), tuning (16 = unison, 1/2x..2x) and resonance
    // character (profile 0..4) are independent for each physical key
    // in each mode. Encoder 1 edits the current mode's bank.
    std::atomic<int>      pan_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<int>      bowl_vol_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<int>      tune_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<int>      profile_ticks[bowls::kPlayModeCount][bowls::kKeyCount];

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

// Encoder-1 edit target: the last musically played/selected key in each
// mode (never a shortcut key). Transient, per mode, initialized to 0.
int last_key_[bowls::kPlayModeCount] = {0, 0, 0};

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

/** Find the voice holding the identity (mode, key), or -1. Identity is
 *  mode AND key: the same physical key in another mode is a different
 *  note and must not reuse this voice. */
int FindVoice(PlayMode mode, int key)
{
    const int m = ModeIdx(mode);
    for(int i = 0; i < kVoiceCount; ++i)
        if(voices[i].Key() == key && voices[i].VoiceMode() == m)
            return i;
    return -1;
}

/** Allocate a voice for (mode, key): the voice already holding that
 *  identity (a re-strike/re-selection adds energy on top of its ring),
 *  else a free voice, else steal the QUIETEST voice, not the oldest. A
 *  just-started pad voice counts as active while its slow attack is
 *  still inaudible (BowlVoice::IsActive includes the pad gate). A steal
 *  is reported to the play-mode state machine, which clears the stolen
 *  note's latch (and only that note's). */
int AllocVoice(PlayMode mode, int key)
{
    const int found = FindVoice(mode, key);
    if(found >= 0)
        return found;
    for(int i = 0; i < kVoiceCount; ++i)
        if(!voices[i].IsActive())
            return i;
    int   v        = 0;
    float quietest = 1.0e30f;
    for(int i = 0; i < kVoiceCount; ++i)
        if(voices[i].Envelope() < quietest)
        {
            quietest = voices[i].Envelope();
            v        = i;
        }
    if(voices[v].IsActive() && voices[v].Key() >= 0)
        play_modes.OnVoiceStolen(
            static_cast<PlayMode>(voices[v].VoiceMode()),
            voices[v].Key());
    return v;
}

/** Global + per-(mode,key) excitation settings, shared by strikes, rim
 *  selection and pads. Pan and bowl volume ride live; profile and
 *  tuning take effect here (excitation time) only. */
void ApplyVoiceSettings(int v, PlayMode mode, int key)
{
    const int m = ModeIdx(mode);
    voices[v].SetProfile(
        params.profile_ticks[m][key].load(std::memory_order_relaxed));
    voices[v].SetBrightness(
        TicksToNorm(params.brightness_ticks.load(std::memory_order_relaxed)));
    voices[v].SetPan(PanNormTicks(
        params.pan_ticks[m][key].load(std::memory_order_relaxed)));
    voices[v].SetBowlGain(BowlGainTicks(
        params.bowl_vol_ticks[m][key].load(std::memory_order_relaxed)));
}

/** Bowl fundamental for (mode, key): the mode map's base pitch times
 *  the key's tuning (which scales the complete modal structure). */
float KeyFundamental(PlayMode mode, int key)
{
    return bowls::BaseFundamentalHz(mode, key)
           * TuneRatioTicks(
               params.tune_ticks[ModeIdx(mode)][key].load(
                   std::memory_order_relaxed));
}

void StrikeKey(PlayMode mode, int key)
{
    const int   v      = AllocVoice(mode, key);
    const float snorm
        = TicksToNorm(params.strike_ticks.load(std::memory_order_relaxed));
    // No per-strike randomization: a well-played bowl is consistent.
    // Hit-to-hit variation still comes for free from the still-ringing
    // state each new strike lands on (and the slow detune wander).
    // A STOLEN voice (different (mode,key) identity, still ringing):
    // damp its ghost deep (-34 dB) so it can never interfere with the
    // new strike; a same-note re-hit keeps the gentler -12 dB
    // continuity. The identity check runs before the voice is retagged.
    const bool stolen
        = (voices[v].Key() != key || voices[v].VoiceMode() != ModeIdx(mode))
          && voices[v].IsActive();
    ApplyVoiceSettings(v, mode, key);
    voices[v].SetKey(key);
    voices[v].SetVoiceMode(ModeIdx(mode));
    voices[v].Strike(KeyFundamental(mode, key), snorm, 1.0f, 1.0f,
                     stolen ? 0.02f : 0.25f);
#ifdef CPU_METER
    if(stolen)
        g_steal_flash_until = System::GetNow() + 300;
#endif
    // The mallet contact burst barely excites a real room: darken the
    // reverb send (it re-opens into the pure ring over ~180 ms).
    send_dark_                = 1.0f;
    last_key_[ModeIdx(mode)] = key;
}

/** Rimming-mode pad start: allocate/select the voice, tune it without
 *  striking (SelectForRub reuses the phase-continuous clean modal
 *  excitation - no second resonator bank) and open the slow pad gate. */
void StartPadKey(int key)
{
    const PlayMode mode = PlayMode::kRimming;
    const int      v    = AllocVoice(mode, key);
    ApplyVoiceSettings(v, mode, key);
    voices[v].SetKey(key);
    voices[v].SetVoiceMode(ModeIdx(mode));
    voices[v].SelectForRub(KeyFundamental(mode, key), 1.0f);
    voices[v].SetPadGate(true);
    last_key_[ModeIdx(mode)] = key;
}

/** Reconcile every Rimming voice's pad gate with the state machine's
 *  desired gate. Runs every block: key-up, latch toggles, LOOP-off,
 *  mode exits and voice steals all close their gates through here, so
 *  no note-off is ever lost. */
void SyncPadGates()
{
    for(int v = 0; v < kVoiceCount; ++v)
    {
        if(voices[v].VoiceMode() != ModeIdx(PlayMode::kRimming))
            continue;
        const int key = voices[v].Key();
        if(key < 0)
            continue;
        const bool want = play_modes.PadOn(key);
        if(voices[v].PadGate() != want)
            voices[v].SetPadGate(want);
    }
}

/** Split rim-zone key press: SELECT the bowl for wheel rimming - never
 *  strikes. The voice is (re)allocated and tuned now, so the wheel has
 *  something to sing into immediately. */
void SelectRimKey(int key, bool make_edit_target)
{
    const PlayMode mode = PlayMode::kSplit;
    const int      v    = AllocVoice(mode, key);
    ApplyVoiceSettings(v, mode, key);
    voices[v].SetKey(key);
    voices[v].SetVoiceMode(ModeIdx(mode));
    // Deterministic detune, like strikes: the same bowl always sings
    // with the same voice when selected.
    voices[v].SelectForRub(KeyFundamental(mode, key), 1.0f);
    if(make_edit_target)
        last_key_[ModeIdx(mode)] = key;
}

/** Split only: if no voice currently holds rim key `key` (e.g. a strike
 *  chord stole it), re-select it silently - the wheel should always
 *  have a bowl to sing into. The full-key modes never reacquire: the
 *  wheel must not create a strike or revive a stolen note. */
void EnsureRimVoice(int key)
{
    if(FindVoice(PlayMode::kSplit, key) >= 0)
        return;
    SelectRimKey(key, false);
}

// Wheel rimming: detents pump the PER-(MODE,KEY) rub level of the
// current mode's wheel set (fast turns = louder). A note that has not
// been rimmed stays silent - even while other notes' levels are still
// decaying. Every level decays back to silence over ~0.35 s of no
// turning; the ~200 ms voice-side slew blooms each note from its own
// silence. Level and character are tracked per (mode, key), so old and
// new mode instances of the same physical key never affect one another.
float wheel_level_[bowls::kPlayModeCount][bowls::kKeyCount] = {};

// Per-(mode,key) rim character, chosen by the wheel DIRECTION of the
// last turn while the note was in the wheel set: false = clockwise
// friction texture, true = counter-clockwise clean sustained tone.
// Transient performance state - never saved; a note keeps its last
// character while its level fades.
bool wheel_clean_[bowls::kPlayModeCount][bowls::kKeyCount] = {};

// What the wheel LEDs show (set in ApplyControls, read in RenderLeds).
volatile float rub_display_ = 0.0f;

/** Push each voice's rub level + character for this block. Only voices
 *  whose (mode, key) is in the CURRENT mode's wheel set are driven;
 *  outgoing-mode voices get target 0 and their last wheel excitation
 *  falls away with the existing smooth slew (their resonator state is
 *  never erased). */
void ApplyWheel()
{
    const int cur = ModeIdx(play_modes.Mode());
    for(int i = 0; i < kVoiceCount; ++i)
    {
        const int key = voices[i].Key();
        const int vm  = voices[i].VoiceMode();
        float     target = 0.0f;
        float     clean  = 0.0f;
        if(key >= 0 && vm == cur && play_modes.WheelTargets(key))
        {
            target = wheel_level_[vm][key];
            clean  = wheel_clean_[vm][key] ? 1.0f : 0.0f;
        }
        // Level AND character travel together: a Split rim voice
        // reacquired after a strike-steal picks up the note's last
        // direction choice along with its level.
        voices[i].SetRub(target, clean);
    }
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
    // last played/selected key in the CURRENT mode). Page 1: turn =
    // per-bowl volume (rides ringing voices live), SHIFT+turn = pan.
    // Page 2: resonance character (profile 1-5). Page 3: per-bowl
    // tuning (next strike/selection). SHIFT+turn on pages 2/3 is
    // reserved.
    const int enc1_inc = hw.EncoderIncrement(0);
    if(enc1_inc != 0)
    {
        const int cur_mode = ModeIdx(play_modes.Mode());
        const int key      = last_key_[cur_mode];
        if(enc1_page_ == kEnc1PageVolume && shift)
        {
            // Pan the selected bowl one tick per detent; a voice holding
            // that (mode, key) is re-panned live (smoothed inside the
            // voice) - a same-key voice from ANOTHER mode keeps its own
            // pan.
            int pan
                = params.pan_ticks[cur_mode][key].load(
                      std::memory_order_relaxed)
                  + enc1_inc;
            pan = pan < 0 ? 0 : (pan > kTickCount ? kTickCount : pan);
            if(pan
               != params.pan_ticks[cur_mode][key].load(
                   std::memory_order_relaxed))
            {
                params.pan_ticks[cur_mode][key].store(
                    pan, std::memory_order_relaxed);
                const float pan_norm = PanNormTicks(pan);
                for(int v = 0; v < kVoiceCount; ++v)
                    if(voices[v].Key() == key
                       && voices[v].VoiceMode() == cur_mode)
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
                = params.bowl_vol_ticks[cur_mode][key].load(
                      std::memory_order_relaxed)
                  + enc1_inc;
            vol = vol < 0 ? 0 : (vol > kTickCount ? kTickCount : vol);
            if(vol
               != params.bowl_vol_ticks[cur_mode][key].load(
                   std::memory_order_relaxed))
            {
                params.bowl_vol_ticks[cur_mode][key].store(
                    vol, std::memory_order_relaxed);
                const float gain = BowlGainTicks(vol);
                for(int v = 0; v < kVoiceCount; ++v)
                    if(voices[v].Key() == key
                       && voices[v].VoiceMode() == cur_mode)
                        voices[v].SetBowlGain(gain);
                MarkDirty(now);
            }
            ShowOverlay(Overlay::BowlVolume, now); // also at the end stops
        }
        else if(enc1_page_ == kEnc1PageCharacter && !shift)
        {
            // Resonance character (profile) of the selected bowl: two
            // detent ticks per step; the bar shows on any turn so the
            // section is visible while arming. Applies at the next
            // strike/selection, like tuning.
            const int step = SectionStep(profile_sel_acc, enc1_inc);
            int       profile
                = params.profile_ticks[cur_mode][key].load(
                      std::memory_order_relaxed)
                  + step;
            profile = profile < 0 ? 0
                                  : (profile >= bowls::kNumProfiles
                                         ? bowls::kNumProfiles - 1
                                         : profile);
            if(profile
               != params.profile_ticks[cur_mode][key].load(
                   std::memory_order_relaxed))
            {
                params.profile_ticks[cur_mode][key].store(
                    profile, std::memory_order_relaxed);
                MarkDirty(now);
            }
            ShowOverlay(Overlay::Character, now);
        }
        else if(enc1_page_ == kEnc1PageTune && !shift)
        {
            // Per-bowl tuning: applied at the next strike/selection
            // (physical character); only the bar shows while a bowl
            // rings.
            int tune
                = params.tune_ticks[cur_mode][key].load(
                      std::memory_order_relaxed)
                  + enc1_inc;
            tune = tune < 0 ? 0 : (tune > kTickCount ? kTickCount : tune);
            if(tune
               != params.tune_ticks[cur_mode][key].load(
                   std::memory_order_relaxed))
            {
                params.tune_ticks[cur_mode][key].store(
                    tune, std::memory_order_relaxed);
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

    // Transport wheel: rimming. Detents pump the PER-(MODE,KEY) rub
    // levels of the current mode's wheel set only (the turn rate sets
    // the level), so a note that has not been rimmed stays silent even
    // while other notes' levels are still decaying.
    const int wheel_inc = hw.EncoderIncrement(4);
    if(wheel_inc != 0)
    {
        const float pump = 0.06f * static_cast<float>(wheel_inc < 0
                                                          ? -wheel_inc
                                                          : wheel_inc);
        // The wheel DIRECTION selects the rim character of the wheel
        // set (the magnitude keeps pumping the level as before):
        // clockwise = friction texture, counter-clockwise = clean
        // sustained tone. QuadratureEncoder documents +1 detent =
        // clockwise; if the physical wheel reads backwards, flip this
        // one comparison. A stopped wheel changes nothing - zero
        // movement is not a direction.
        const bool     clean    = wheel_inc < 0;
        const PlayMode mode     = play_modes.Mode();
        const int      cur_mode = ModeIdx(mode);
        if(mode == PlayMode::kSplit)
        {
            // The v1 wheel rules: at most two held rim-zone keys,
            // otherwise the last-selected rim key, including
            // reacquisition on wheel movement after a strike-steal.
            if(play_modes.RimHeldCount() > 0)
            {
                for(int k = 0; k < play_modes.RimHeldCount(); ++k)
                {
                    const int key = play_modes.RimHeld(k);
                    wheel_level_[cur_mode][key] += pump;
                    if(wheel_level_[cur_mode][key] > 1.0f)
                        wheel_level_[cur_mode][key] = 1.0f;
                    wheel_clean_[cur_mode][key] = clean;
                    EnsureRimVoice(key);
                }
            }
            else if(play_modes.RimLatch() >= 0)
            {
                const int key = play_modes.RimLatch();
                wheel_level_[cur_mode][key] += pump;
                if(wheel_level_[cur_mode][key] > 1.0f)
                    wheel_level_[cur_mode][key] = 1.0f;
                wheel_clean_[cur_mode][key] = clean;
                EnsureRimVoice(key);
            }
        }
        else
        {
            // Full-key modes: the wheel addresses currently sounding
            // held notes (Rimming: held or latched pads). It never
            // creates a strike and never revives a stolen note.
            for(int key = 0; key < bowls::kKeyCount; ++key)
            {
                if(!play_modes.WheelTargets(key))
                    continue;
                const int v = FindVoice(mode, key);
                if(v < 0 || !voices[v].IsActive())
                    continue;
                wheel_level_[cur_mode][key] += pump;
                if(wheel_level_[cur_mode][key] > 1.0f)
                    wheel_level_[cur_mode][key] = 1.0f;
                wheel_clean_[cur_mode][key] = clean;
            }
        }
        // No keyboard overlay here: the two wheel LEDs follow the rub
        // level, and the keys keep showing held keys + ringing glow.
    }
    // All rub levels decay back to silence over ~0.35 s of no turning.
    for(int m = 0; m < bowls::kPlayModeCount; ++m)
        for(int k = 0; k < bowls::kKeyCount; ++k)
            wheel_level_[m][k] *= 0.9990f;
    // The wheel LEDs follow the wheel set's loudest note: a freshly
    // selected, never-rimmed bowl reads dark until the wheel turns.
    {
        const int cur_mode = ModeIdx(play_modes.Mode());
        float     d        = 0.0f;
        for(int k = 0; k < bowls::kKeyCount; ++k)
            if(play_modes.WheelTargets(k)
               && wheel_level_[cur_mode][k] > d)
                d = wheel_level_[cur_mode][k];
        rub_display_ = d;
    }
    ApplyWheel();

    // Route every key through the play-mode state machine: SHIFT +
    // lower F#/G#/A# are consumed as mode shortcuts through release;
    // every other key press/release is interpreted in the mode it
    // started in (so a key-up after a mode switch still releases the
    // right note).
    for(int key = 0; key < bowls::kKeyCount; ++key)
    {
        if(hw.KeyRising(key))
        {
            const bowls::KeyAction act = play_modes.KeyDown(key, shift);
            switch(act.kind)
            {
                case bowls::KeyAction::kStrike:
                    StrikeKey(act.mode, act.key);
                    break;
                case bowls::KeyAction::kPadOn:
                    StartPadKey(act.key);
                    break;
                case bowls::KeyAction::kRimSelect:
                    SelectRimKey(act.key, true);
                    break;
                case bowls::KeyAction::kModeChanged:
                    // The mode change itself allocates no voice:
                    // outgoing voices keep their sound and reverb; only
                    // wheel control stops (ApplyWheel above) and
                    // latched-only pads release (SyncPadGates below).
                    // The mode is persisted.
                    MarkDirty(now);
                    break;
                default: break;
            }
        }
        else if(hw.KeyFalling(key))
        {
            // Releases reconcile through the state machine (PadOn) and
            // SyncPadGates below - no explicit action needed here.
            play_modes.KeyUp(key);
        }
    }

    // LOOP toggles the Rimming note latch (transient; never saved and
    // inert in the other modes).
    if(hw.LoopClicked())
        play_modes.LoopPressed();

    // Pad gate reconciliation: key-ups, latch toggles, LOOP-off, mode
    // exits and voice steals all close their gates through here.
    SyncPadGates();
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
#ifdef CPU_METER
    static bool meter_init = false;
    if(!meter_init)
    {
        g_cpu_meter.Init(kSampleRate, size);
        meter_init = true;
    }
    g_cpu_meter.OnBlockStart();
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
    g_cpu_meter.OnBlockEnd();
    // Per-block load (GetMaxCpuLoad resets each call), peak-held so a
    // brief overload stays visible on the LED / in the log.
    const float load = g_cpu_meter.GetMaxCpuLoad();
    if(load > g_cpu_peak)
        g_cpu_peak = load;
    else
        g_cpu_peak *= 0.9995f; // ~5 s fall
    if(System::GetNow() % 500 < 1)
        hw.seed.PrintLine("CPU peak: %d%%",
                          static_cast<int>(g_cpu_peak * 100.0f));
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
    // Encoder-1 overlays edit the current mode's bank for the selected
    // (last played) key of that mode.
    const int cur_mode = ModeIdx(play_modes.Mode());
    const int edit_key = last_key_[cur_mode];
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
                    params.profile_ticks[cur_mode][edit_key].load(
                        std::memory_order_relaxed));
                break;
            case Overlay::Pan:
                DrawPanBar(params.pan_ticks[cur_mode][edit_key].load(
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
                DrawBar(params.bowl_vol_ticks[cur_mode][edit_key].load(
                            std::memory_order_relaxed),
                        0, 48, 0);
                break;
            case Overlay::Tune:
                // Violet centre-detent bar: flat to the left, sharp to
                // the right, dead centre = unison.
                DrawPanBar(params.tune_ticks[cur_mode][edit_key].load(
                               std::memory_order_relaxed),
                           30, 0, 48);
                break;
            default: break;
        }
    }

    // Pressed keys light white, ringing voices glow dim amber on their
    // key, and LOOP-latched Rimming notes glow violet - but NOT while a
    // temporary overlay bar is up, so the bars stay readable while a
    // setting is edited. (A stolen note's latch is cleared by the state
    // machine, so its violet indication disappears at the steal.)
    if(!overlay_active)
    {
        for(unsigned key = 0; key < 25; ++key)
            if(hw.KeyHeld(key))
                chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[key], 48, 48, 48);
        for(int v = 0; v < kVoiceCount; ++v)
        {
            const int key = voices[v].Key();
            if(key >= 0 && voices[v].IsActive() && !hw.KeyHeld(key))
                chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[key],
                                       14, 8, 2);
        }
        if(play_modes.Mode() == PlayMode::kRimming)
            for(int key = 0; key < bowls::kKeyCount; ++key)
                if(play_modes.Latched(key) && !hw.KeyHeld(key))
                    chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[key],
                                           26, 0, 48);
    }
}

void RenderLeds()
{
    // Every frame starts dark: overlays are fully redrawn each pass so
    // timed-out bars revert to the base view (held keys + ringing glow).
    for(unsigned led = 0; led < chompi_leds::kSmtLedCount; ++led)
        chompi_leds::SetSmtLed(led, 0, 0, 0);

    DrawOverlayBar(System::GetNow());

    // While SHIFT is held, highlight the active mode's lower black
    // shortcut key (blue). Black keys never collide with the white-key
    // parameter bars, so this reads over either view.
    if(hw.ShiftHeld())
    {
        const int sk
            = bowls::kModeShortcutKey[ModeIdx(play_modes.Mode())];
        chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[sk], 0, 26, 48);
    }

    // Panel LEDs (PTH chain).
    const bool shift = hw.ShiftHeld();
    chompi_leds::SetPthLed(chompi_leds::kPthShift,
                           shift ? 40 : 0, shift ? 40 : 0, shift ? 40 : 0);
#ifdef CPU_METER
    // Meter build: the Shift LED is the CPU load meter instead
    // (green -> red, ~5 s peak-hold).
    chompi_leds::SetPthLed(chompi_leds::kPthShift,
                           static_cast<uint8_t>(48.0f * g_cpu_peak),
                           static_cast<uint8_t>(48.0f * (1.0f - g_cpu_peak)),
                           0);
#endif
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
#ifdef CPU_METER
    // Steal witness: encoder 4 LED goes white for ~300 ms when a strike
    // steals a still-ringing voice - correlate it with audible chokes.
    if(System::GetNow() < g_steal_flash_until)
        chompi_leds::SetPthLed(chompi_leds::kPthEncoder4, 40, 40, 40);
#endif
    const uint8_t rub = static_cast<uint8_t>(rub_display_ * 40.0f);
    chompi_leds::SetPthLed(chompi_leds::kPthWheelLeft, rub, rub, rub);
    chompi_leds::SetPthLed(chompi_leds::kPthWheelRight, rub, rub, rub);
    // The LOOP LED is lit only while the Rimming note latch is enabled.
    const bool    latch_on = play_modes.LatchEnabled();
    chompi_leds::SetPthLed(chompi_leds::kPthLoop,
                           latch_on ? 40 : 0,
                           latch_on ? 40 : 0,
                           latch_on ? 40 : 0);
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
    p.mode = static_cast<uint8_t>(ModeIdx(play_modes.Mode()));
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
    for(int m = 0; m < bowls::kPlayModeCount; ++m)
        for(int w = 0; w < bowls::kKeyCount; ++w)
        {
            const int slot = m * bowls::kKeyCount + w;
            p.pan[slot] = static_cast<uint8_t>(
                params.pan_ticks[m][w].load(std::memory_order_relaxed));
            p.bvol[slot] = static_cast<uint8_t>(
                params.bowl_vol_ticks[m][w].load(
                    std::memory_order_relaxed));
            p.tune[slot] = static_cast<uint8_t>(
                params.tune_ticks[m][w].load(std::memory_order_relaxed));
            p.profile[slot] = static_cast<uint8_t>(
                params.profile_ticks[m][w].load(
                    std::memory_order_relaxed));
        }
    p.reverb_mix = static_cast<uint8_t>(
        params.reverb_mix_ticks.load(std::memory_order_relaxed));
    return p;
}

void ApplyPreset(const chompi::BowlsPreset& p)
{
    // Restore the saved mode (boot-time only, before audio starts:
    // no voices exist yet, so nothing can be interrupted; latch and
    // performance state stay at their transient defaults).
    play_modes.Init(static_cast<PlayMode>(p.mode));
    params.volume_ticks.store(p.volume, std::memory_order_relaxed);
    params.strike_ticks.store(p.strike, std::memory_order_relaxed);
    params.brightness_ticks.store(p.brightness, std::memory_order_relaxed);
    params.decay_ticks.store(p.decay, std::memory_order_relaxed);
    params.shimmer_ticks.store(p.shimmer, std::memory_order_relaxed);
    for(int m = 0; m < bowls::kPlayModeCount; ++m)
        for(int w = 0; w < bowls::kKeyCount; ++w)
        {
            const int slot = m * bowls::kKeyCount + w;
            params.pan_ticks[m][w].store(p.pan[slot],
                                         std::memory_order_relaxed);
            params.bowl_vol_ticks[m][w].store(p.bvol[slot],
                                              std::memory_order_relaxed);
            params.tune_ticks[m][w].store(p.tune[slot],
                                          std::memory_order_relaxed);
            params.profile_ticks[m][w].store(p.profile[slot],
                                             std::memory_order_relaxed);
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

        /* One serial diagnostic per storage failure/recovery (no panel
         * LED by request): a failed save is never treated as
         * successful - the state lives in RAM until it can be saved. */
        const auto storage_event = preset_storage.ConsumeStatusEvent();
        if(storage_event == chompi::PresetStorage::kStatusFailed)
            hw.seed.PrintLine("SD: save failed - state kept in RAM, "
                              "retrying");
        else if(storage_event == chompi::PresetStorage::kStatusRecovered)
            hw.seed.PrintLine("SD: save recovered");

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