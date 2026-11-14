// CHOMPI-BOWLS.
//
// A five-voice modal singing-bowl instrument with three play modes
// (plans: plans/bowls-v1-first-firmware.md,
// plans/split-strike-and-rimming-plan.md, plans/improve-play-modes.md).
// Each strike excites eight paired resonant modes (16 resonators per
// voice) with per-mode decay, paired-mode beating (shimmer) and a
// finite-contact mallet excitation. The five voices are shared by all
// modes; a sixth simultaneous note steals the quietest unprotected voice
// (preserved Rimming and Strike latches are protected across modes).
// never interrupts ringing voices or reverb tails: outgoing voices keep
// their sound, and the wheel only drives notes started in the current
// mode. Persistent parameters are stored only in explicitly saved presets;
// the LOOP/PLAY latches and all other performance state remain transient.
//
//   Play modes            : SHIFT + lower F# (key 6) = STRIKES - all 25
//                         keys strike distinct ascending bowls; the
//                         wheel rims currently sounding, physically
//                         held strikes. SHIFT + lower G# (key 8) =
//                         RIMMING - all 25 keys hold clean sustained
//                         bowl pads (10 s attack to 90%, 10 s release
//                         to 10%); the wheel adds the direction-
//                         dependent rimming; LOOP toggles a note
//                         latch: enabling captures the held sounding
//                         pads, key-down toggles a note on/off, key-up
//                         never releases a latched note. In STRIKES, a
//                         separate PLAY latch captures held strikes;
//                         later key-downs toggle blue notes that retrigger
//                         after their resonator tails end. SHIFT + lower
//                         A# (key 10) = SPLIT - the v1 layout: keys
//                         0-11 (C3-B3) SELECT one of the 12 bowls for
//                         wheel rimming (never strike), keys 12-23
//                         strike the same 12 bowls, key 24 (C5)
//                         strikes an added higher bowl. A shortcut
//                         consumes its key through release - it never
//                         sounds a note or moves the edit target.
//   Physical encoder 1 (SW4/ENC_4): per-bowl editor for the selected
//                         bowl (the last played/selected key IN THE
//                         CURRENT MODE; pan, bowl volume, tuning,
//                         resonance character, brightness and decay
//                         are independent per key per mode); press
//                         cycles three pages. Page 1:
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
//                         bar, GLOBAL); SHIFT+turn = brightness of the
//                         selected bowl (yellow-white bar; rides the
//                         ringing voice live)
//   Physical encoder 3 (SW2/ENC_2): decay of the selected bowl (LED4
//                         blue, blue bar; rides the ringing voice live)
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
//   Volume encoder      : press cycles page 1 master volume / page 2
//                         stereo delay (yellow bar/LED; turn = combined
//                         mix/feedback, SHIFT+turn = 40..500 ms time) /
//                         page 3 temple-room reverb mix (pale ice)
//   SHIFT (KEY_26)      : modifier + mode shortcuts; while held, the
//                         active mode's shortcut key glows blue
//                         (Strikes), yellow (Rimming) or pink (Split);
//                         the active audio routing's selection key
//                         glows pink. SHIFT + Black 6 (C#4) = Normal
//                         audio routing, SHIFT + Black 7 (D#4) =
//                         Send FX routing (audio_routing.h); the
//                         combo is consumed - it never sounds a note
//                         or moves the bowl editor target.
//                         SHIFT + White Key 1-15 loads that preset;
//                         SHIFT + Black F#4/G#4/A#4 enters erase/copy/save
//                         selection, confirmed with a fresh SHIFT press.
//                         LOOP (KEY_28)       : tap toggles the Rimming note latch (yellow);
//                         retained in Strikes, cleared on entry to Split;
//                         hold with PLAY for recording arm/stop; in
//                         playback, tap toggles take looping (blue).
//   PLAY (KEY_27)       : in Strikes, tap toggles the Strike note latch
//                         (blue); hold alone 2 s to enter/exit playback;
//                         in playback, tap pauses/resumes. SW_TOG is reserved.
//
// Every encoder turn shows its bar on the 15 white keys for ~500 ms (the
// CHOMPI-PhaseOff overlay pattern); with no bar up, held Strike notes
// light blue, held Rimming notes yellow and ringing voices glow dim amber.
//
// Audio: 48 kHz, block 24, dual codec - headphones are outputs 0/1 and
// the master line out is outputs 2/3 (per the TAPE firmware channel
// map); AUX IN is inputs 2/3. Two selectable routings
// (audio_routing.h): Normal (boot default) sends the same bowl bus x
// master gain to both output pairs; Send FX sends the dry bowl bus at
// fixed unity (pre-master-gain) to the line out - an external-effects
// send - and the AUX IN return x master gain to the headphones (the
// volume knob is the return level). The return never leaves through
// the line out, so a feedback loop is impossible. The SD recorder
// taps the performed signal pre-master-gain: the bowl bus in Normal,
// the AUX IN return in Send FX.

#include "chompi_hardware.h"
#include "leds.h"
#include "bowl_profile.h"
#include "bowl_scale.h"
#include "play_modes.h"
#include "audio_routing.h"
#include "bowl_voice.h"
#include "control_mappings.h"
#include "voice_allocator.h"
#include "BowlsPreset.h"
#include "PresetStorage.h"
#include "SdRecorder.h"
#include "SdPlayer.h"
#include "StereoBbdDelay.h"
#include "delay_parameters.h"
#include "MidiCcMap.h"
#include "MidiHandler.h"

#include "Utility/dsp.h"
#include "Effects/reverbsc.h"

#include <atomic>
#include <cmath>
#include <cstdint>

#ifdef CPU_METER
#include "util/CpuLoadMeter.h"
#endif

using namespace daisy;

/* USB MIDI uses the panel USB-C port (EXTERNAL). libDaisy's usbd_mode
 * is a single GLOBAL read at descriptor time, so one USB device stack
 * cannot present CDC (the USB-serial log) and MIDI at once - MIDI
 * takes priority, so normal builds compile the serial diagnostics out
 * (the LED diagnostics stay). CPU_METER diagnostic builds keep the
 * serial log and skip USB MIDI instead (DIN MIDI still works there). */
#ifdef CPU_METER
#define BOWLS_LOG(...) hw.seed.PrintLine(__VA_ARGS__)
#else
#define BOWLS_LOG(...) ((void)0)
#endif

namespace
{
constexpr float kSampleRate = 48000.0f;
constexpr int   kBlockSize  = 24;
constexpr size_t kSdRingFrames = 1u << 18;

chompi::ChompiHardware hw;
chompi::BowlsPresetBank preset_bank;
chompi::PresetStorage  preset_storage;
chompi::sd_audio::StereoRing<kSdRingFrames> DSY_SDRAM_BSS record_ring;
chompi::sd_audio::StereoRing<kSdRingFrames> DSY_SDRAM_BSS playback_ring;
chompi::SdRecorder<kSdRingFrames> sd_recorder(record_ring);
chompi::SdPlayer<kSdRingFrames>   sd_player(playback_ring);

constexpr int  kVoiceCount = 5; // shared pool, quietest-voice steal
bowls::BowlVoice voices[kVoiceCount];

// Stereo temple-room reverb (DaisySP Freeverb derivative) on the bowl
// bus: a small wooden room - short tail, highs damped (wood absorbs
// them). Character is fixed; only the dry/wet mix is on the panel.
// The delay buffers (~387 KB) live in SDRAM - SRAM is for state that
// must be fast, not for bulk audio memory.
daisysp::ReverbSc DSY_SDRAM_BSS reverb;
float DSY_SDRAM_BSS delay_buffer_l[chompi::StereoBbdDelay::kBufferSize];
float DSY_SDRAM_BSS delay_buffer_r[chompi::StereoBbdDelay::kBufferSize];
chompi::StereoBbdDelay bbd_delay;

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
    DelayMix,
    DelayTime,
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
        pitch_hz = bowls::FactoryPitchTables();
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
                brightness_ticks[m][w].store(
                    chompi::BowlsPreset::kDefaultBrightness,
                    std::memory_order_relaxed);
                decay_ticks[m][w].store(chompi::BowlsPreset::kDefaultDecay,
                                        std::memory_order_relaxed);
            }
    }

    // Global controls (shared by all modes).
    std::atomic<int>      volume_ticks{chompi::BowlsPreset::kDefaultVolume};
    std::atomic<int>      strike_ticks{chompi::BowlsPreset::kDefaultStrike};
    std::atomic<int>      shimmer_ticks{chompi::BowlsPreset::kDefaultShimmer};

    // Per-mode/per-key banks [mode][key]: pan (16 = centre), bowl
    // volume (16 = unity), tuning (16 = unison, 1/2x..2x), resonance
    // character (profile 0..4), brightness and decay are independent
    // for each physical key in each mode. Encoders 1-3 edit the
    // current mode's bank for the selected (last played) key.
    std::atomic<int>      pan_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<int>      bowl_vol_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<int>      tune_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<int>      profile_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<int>      brightness_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<int>      decay_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    bowls::PitchTables    pitch_hz;

    // Volume knob page 2: temple-room reverb dry/wet mix.
    std::atomic<int>      reverb_mix_ticks{
        chompi::BowlsPreset::kDefaultReverbMix};
    std::atomic<int>      delay_mix_ticks{
        chompi::BowlsPreset::kDefaultDelayMix};
    std::atomic<int>      delay_time_ticks{
        chompi::BowlsPreset::kDefaultDelayTime};

    // UI overlay: which parameter bar is shown on the white keys.
    std::atomic<int>      overlay{static_cast<int>(Overlay::Volume)};
    std::atomic<uint32_t> overlay_until{0};
};

Params params;

// Volume-knob pages (press cycles): master volume -> delay -> reverb.
constexpr int kVolPageVolume = 0;
constexpr int kVolPageDelay  = 1;
constexpr int kVolPageReverb = 2;
volatile int  vol_page_      = kVolPageVolume;

// Audio routing selection (audio_routing.h): Normal (boot default) or
// the external send-FX loop. Session-only - never saved to the SD
// preset, so a fresh boot always starts in Normal monitoring.
volatile int routing_ = bowls::kRoutingDefault;

// ---------------------------------------------------------------------------
// MIDI input (plans/midi-input-and-cc-control.md; user docs: midi.md)
// ---------------------------------------------------------------------------

// DIN (UART) + USB MIDI device, fixed channel 1. Input parsers and output
// transmitters are serviced in the main loop; the audio callback drains
// bounded input queues and publishes local key notes to output queues.
bowls::BowlsMidi midi;

#ifdef CPU_METER
constexpr bool kUsbMidiEnable = false; // meter build: keep USB serial
#else
constexpr bool kUsbMidiEnable = true;
#endif

// Per-source transient MIDI performance state (never saved), indexed
// by bowls::kDin / bowls::kUsb: the CC 1 held rimming level (0 until
// first set), the CC 84 rim texture (false = friction, like the
// wheel's clockwise), and the CC 81/82 edit-bank/key context (-1 =
// follow the active mode).
float    midi_rub_level_[bowls::kMidiTransportCount] = {0.0f, 0.0f};
bool     midi_rub_clean_[bowls::kMidiTransportCount] = {false, false};
uint32_t midi_rub_seq_[bowls::kMidiTransportCount]   = {0, 0};
int      midi_edit_bank_[bowls::kMidiTransportCount] = {-1, -1};
int      midi_edit_key_[bowls::kMidiTransportCount]  = {0, 0};

// Wheel/MIDI rub arbitration recency (plan section 5): the wheel's
// per-(mode,key) pumps and each MIDI source's CC 1/84 changes share
// one monotonically increasing sequence; at equal levels the latest
// control event's candidate (and its texture) wins.
uint32_t rub_seq_   = 0;
uint32_t wheel_seq_[bowls::kPlayModeCount][bowls::kKeyCount] = {};

// CC 120 panic: a <= 5 ms bus fade masks the voice kill; the ~387 KB
// reverb state clear is deferred to the main loop (reverb_reset_pending_)
// and the mute lifts only after it completes - never a heavy reset in
// the audio ISR, and never a stale tail after unmute.
volatile bool panic_mute_           = false;
volatile bool reverb_reset_pending_ = false;
float         panic_gain_           = 1.0f;

/** Queue/transport index -> claim source. */
bowls::Source MidiSrcFromTransport(int src)
{
    return src == bowls::kDin ? bowls::Source::kMidiDin
                              : bowls::Source::kMidiUsb;
}

// Encoder-1 pages (press cycles): bowl volume -> character -> tuning.
constexpr int kEnc1PageVolume    = 0;
constexpr int kEnc1PageCharacter = 1;
constexpr int kEnc1PageTune      = 2;
volatile int  enc1_page_         = kEnc1PageVolume;

enum PresetUiMode : int
{
    kPresetNone = 0,
    kPresetSaveSel,
    kPresetSaving,
    kPresetEraseSel,
    kPresetErasing,
    kPresetCopySrc,
    kPresetCopyDest,
    kPresetCopying
};
volatile int      preset_mode_ = kPresetNone;
volatile int      preset_sel_slot_ = -1;
volatile int      preset_copy_src_ = -1;
volatile int      preset_current_ = -1;
volatile uint32_t preset_anim_t_ = 0;

// SD save-status flash on the SHIFT LED (persistence diagnostic): a
// short green pulse = a save completed; 1 s red = a save failed.
volatile uint32_t sd_flash_until_                  = 0;
volatile uint8_t  sd_flash_r_                      = 0;
volatile uint8_t  sd_flash_g_                      = 0;
volatile uint8_t  sd_flash_b_                      = 0;

enum class RecordUiState : uint8_t
{
    kIdle = 0,
    kArmHold,
    kCountdown,
    kStopHold
};

enum class PlaybackUiState : uint8_t
{
    kNormal = 0,
    kEntering,
    kPlaying,
    kExiting
};

enum TransportFeedback : int
{
    kFeedbackNone = 0,
    kFeedbackRecordRefused,
    kFeedbackNoTakes,
    kFeedbackPlayerFatal,
    kFeedbackRecorderError,
    kFeedbackInvalidTake
};

std::atomic<RecordUiState> record_ui_state{RecordUiState::kIdle};
std::atomic<uint32_t>      record_ui_started{0};
std::atomic<PlaybackUiState> playback_ui_state{PlaybackUiState::kNormal};
std::atomic<bool>          playback_entry_pending{false};
std::atomic<int>           transport_feedback{kFeedbackNone};
std::atomic<uint32_t>      transport_feedback_until{0};
std::atomic<int>           transport_feedback_slot{-1};
std::atomic<uint32_t>      playback_hold_started{0};

uint32_t play_hold_started_ = 0;
uint32_t loop_hold_started_ = 0;
uint32_t chord_hold_started_ = 0;
uint32_t countdown_started_ = 0;
bool play_hold_valid_ = false;
bool loop_hold_valid_ = false;
bool chord_hold_valid_ = false;
bool play_gesture_consumed_ = false;
bool loop_gesture_consumed_ = false;
bool play_solo_cancelled_ = false;
bool chord_action_done_ = false;
bool playback_route_saved_ = false;
int  playback_saved_routing_ = bowls::kRoutingDefault;
bool transport_wait_for_release_ = true;

constexpr int kWhiteKeyIndices[15] = {
    0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24};

chompi::BowlsPreset SnapshotParams();
void ApplyPreset(const chompi::BowlsPreset &p, bool boot);
void PresetShiftConfirm();
bool PresetKeyEvent(int key, bool down, bool shift);

float TicksToNorm(int ticks)
{
    return static_cast<float>(ticks) / static_cast<float>(kTickCount);
}

float PanNormTicks(int ticks)
{
    return static_cast<float>(ticks - 16) / 16.0f; // -1 (left) .. +1 (right)
}

void ApplyDelayTargets()
{
    const int mix_position
        = params.delay_mix_ticks.load(std::memory_order_relaxed);
    bbd_delay.SetMix(chompi::delay_parameters::DelayMix(mix_position));
    bbd_delay.SetFeedback(
        chompi::delay_parameters::DelayFeedback(mix_position));
    bbd_delay.SetDelayMs(static_cast<float>(
        chompi::delay_parameters::DelayTimeMs(
            params.delay_time_ticks.load(std::memory_order_relaxed))));
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
    return bowls::DecayMultiplierFromTicks(ticks); // 0.25x..3x
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

// ---------------------------------------------------------------------------
// Shared parameter setters (local encoders AND MIDI CCs): absolute and
// change-gated. Persistence occurs only through explicit preset operations.
// ---------------------------------------------------------------------------

/** Clamp-store a 0..32 tick cell. Returns true when the value changed. */
bool StoreTicks(std::atomic<int>& cell, int value, uint32_t)
{
    value = value < 0 ? 0 : (value > kTickCount ? kTickCount : value);
    if(value == cell.load(std::memory_order_relaxed))
        return false;
    cell.store(value, std::memory_order_relaxed);
    return true;
}

/** Clamp-store a 0..4 profile cell. */
bool StoreProfile(std::atomic<int>& cell, int value, uint32_t)
{
    value = value < 0 ? 0
                      : (value >= bowls::kNumProfiles
                             ? bowls::kNumProfiles - 1
                             : value);
    if(value == cell.load(std::memory_order_relaxed))
        return false;
    cell.store(value, std::memory_order_relaxed);
    return true;
}

/** Live-update a parameter on the voice holding (mode, key), if any. */
template <typename F>
void ForVoiceIdentity(int m, int key, F apply)
{
    for(int v = 0; v < kVoiceCount; ++v)
        if(voices[v].Key() == key && voices[v].VoiceMode() == m)
            apply(v);
}

// Per-(mode,key) bank setters: pan/bowl-volume/brightness/decay ride
// the ringing voice live; tuning and profile apply at the next
// excitation (store only).
void SetPanTicks(int m, int key, int ticks, uint32_t now)
{
    if(!StoreTicks(params.pan_ticks[m][key], ticks, now))
        return;
    const float pan_norm = PanNormTicks(
        params.pan_ticks[m][key].load(std::memory_order_relaxed));
    ForVoiceIdentity(m, key,
                     [&](int v) { voices[v].SetPan(pan_norm); });
}

void SetBowlVolTicks(int m, int key, int ticks, uint32_t now)
{
    if(!StoreTicks(params.bowl_vol_ticks[m][key], ticks, now))
        return;
    const float gain = BowlGainTicks(
        params.bowl_vol_ticks[m][key].load(std::memory_order_relaxed));
    ForVoiceIdentity(m, key,
                     [&](int v) { voices[v].SetBowlGain(gain); });
}

void SetBrightnessTicks(int m, int key, int ticks, uint32_t now)
{
    if(!StoreTicks(params.brightness_ticks[m][key], ticks, now))
        return;
    const float bnorm = bowls::BrightnessFromTicks(
        params.brightness_ticks[m][key].load(std::memory_order_relaxed));
    ForVoiceIdentity(m, key,
                     [&](int v) { voices[v].SetBrightness(bnorm); });
}

void SetDecayTicks(int m, int key, int ticks, uint32_t now)
{
    if(!StoreTicks(params.decay_ticks[m][key], ticks, now))
        return;
    const float mult = DecayMultTicks(
        params.decay_ticks[m][key].load(std::memory_order_relaxed));
    ForVoiceIdentity(m, key,
                     [&](int v) { voices[v].SetDecayMult(mult); });
}

void SetTuneTicks(int m, int key, int ticks, uint32_t now)
{
    StoreTicks(params.tune_ticks[m][key], ticks, now);
}

void SetProfileTicks(int m, int key, int profile, uint32_t now)
{
    StoreProfile(params.profile_ticks[m][key], profile, now);
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

// Shimmer stays GLOBAL: one paired-mode detune for the whole
// instrument. Changes are queued per voice until its ring and drive
// are silent, avoiding mid-tail coefficient jumps.
void ApplyShimmerToVoices()
{
    const float shimmer_mult
        = ShimmerMultTicks(params.shimmer_ticks.load(std::memory_order_relaxed));
    for(int v = 0; v < kVoiceCount; ++v)
        voices[v].SetShimmerMult(shimmer_mult);
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
bool IsProtectedRimmingVoice(int voice)
{
    const int key = voices[voice].Key();
    return play_modes.Mode() == PlayMode::kStrikes
           && voices[voice].VoiceMode() == ModeIdx(PlayMode::kRimming)
           && key >= 0 && play_modes.Latched(key);
}

bool IsProtectedStrikeVoice(int voice)
{
    const int key = voices[voice].Key();
    return voices[voice].VoiceMode() == ModeIdx(PlayMode::kStrikes)
           && key >= 0 && play_modes.StrikeLatched(key);
}

int AllocVoice(PlayMode mode, int key)
{
    const int found = FindVoice(mode, key);
    if(found >= 0)
        return found;
    for(int i = 0; i < kVoiceCount; ++i)
        if(!IsProtectedRimmingVoice(i) && !IsProtectedStrikeVoice(i)
           && !voices[i].IsActive())
            return i;
    const int v = bowls::QuietestUnprotectedVoice(
        kVoiceCount,
        [](int i) { return voices[i].IsActive(); },
        [](int i) {
            return IsProtectedRimmingVoice(i) || IsProtectedStrikeVoice(i);
        },
        [](int i) { return voices[i].Envelope(); });
    if(v < 0)
        return -1;
    if(voices[v].IsActive() && voices[v].Key() >= 0)
        play_modes.OnVoiceStolen(
            static_cast<PlayMode>(voices[v].VoiceMode()),
            voices[v].Key());
    return v;
}

/** Per-(mode,key) excitation settings, shared by strikes, rim
 *  selection and pads. Pan, bowl volume, brightness and decay also
 *  ride live via the encoder handlers; profile and tuning take effect
 *  here (excitation time) only. */
void ApplyVoiceSettings(int v, PlayMode mode, int key)
{
    const int m = ModeIdx(mode);
    voices[v].SetProfile(
        params.profile_ticks[m][key].load(std::memory_order_relaxed));
    voices[v].SetBrightness(bowls::BrightnessFromTicks(
        params.brightness_ticks[m][key].load(std::memory_order_relaxed)));
    voices[v].SetDecayMult(DecayMultTicks(
        params.decay_ticks[m][key].load(std::memory_order_relaxed)));
    voices[v].SetPan(PanNormTicks(
        params.pan_ticks[m][key].load(std::memory_order_relaxed)));
    voices[v].SetBowlGain(BowlGainTicks(
        params.bowl_vol_ticks[m][key].load(std::memory_order_relaxed)));
}

/** Bowl fundamental for (mode, key): the mode map's base pitch times
 *  the key's tuning (which scales the complete modal structure). */
float KeyFundamental(PlayMode mode, int key)
{
    return bowls::BaseFundamentalHz(mode, key, params.pitch_hz)
           * bowls::TuneRatioFromTicks(
               params.tune_ticks[ModeIdx(mode)][key].load(
                   std::memory_order_relaxed));
}

void StrikeKey(PlayMode mode, int key, int velocity = 127,
               bool edit_target = true, bool midi_velocity = false)
{
    const int   v      = AllocVoice(mode, key);
    if(v < 0)
        return;
    const float snorm
        = TicksToNorm(params.strike_ticks.load(std::memory_order_relaxed));
    const bowls::StrikeInputs strike
        = bowls::GetStrikeInputs(snorm, velocity, midi_velocity);
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
    // A strike takes the voice over: any pad/rim performance gain left
    // over from the voice's previous life returns to unity (the strike
    // velocity already shaped the excitation itself).
    voices[v].SetPerfGain(1.0f);
    voices[v].StrikeWithAmplitude(KeyFundamental(mode, key), strike.hardness,
                                  strike.amplitude, 1.0f, 1.0f,
                                  stolen ? 0.02f : 0.25f);
#ifdef CPU_METER
    if(stolen)
        g_steal_flash_until = System::GetNow() + 300;
#endif
    // The mallet contact burst barely excites a real room: darken the
    // reverb send (it re-opens into the pure ring over ~180 ms).
    send_dark_ = 1.0f;
    // MIDI notes never move the LOCAL bowl-editor target (the CC 81/82
    // edit context is kept per sender instead).
    if(edit_target)
        last_key_[ModeIdx(mode)] = key;
}

/** A latched Strike reuses its assigned voice after the resonators go
 *  silent; this makes its cadence follow the selected bowl's decay. */
void RetriggerLatchedStrikes()
{
    if(!play_modes.StrikeLatchEnabled())
        return;
    for(int key = 0; key < bowls::kKeyCount; ++key)
    {
        if(!play_modes.StrikeLatched(key))
            continue;
        const int voice = FindVoice(PlayMode::kStrikes, key);
        if(voice >= 0
           && (voices[voice].IsActive() || voices[voice].HasTail()))
            continue;
        const int velocity = play_modes.StrikeLatchVelocity(key);
        StrikeKey(PlayMode::kStrikes, key, velocity > 0 ? velocity : 127,
                  false, play_modes.StrikeLatchUsesMidiVelocity(key));
    }
}

/** Rimming-mode pad start: allocate/select the voice, tune it without
 *  striking (SelectForRub reuses the phase-continuous clean modal
 *  excitation - no second resonator bank) and open the slow pad gate.
 *  A STOLEN voice (different (mode,key) identity, still ringing) gets
 *  the ghost fade: its leftover ring is NOT gated by the pad envelope
 *  and would otherwise sound instantly at the new pitch, skipping the
 *  slow attack. The identity check runs before the voice is retagged. */
void StartPadKey(int key, int velocity = 127)
{
    const PlayMode mode = PlayMode::kRimming;
    const int      v    = AllocVoice(mode, key);
    if(v < 0)
        return;
    const bool stolen
        = (voices[v].Key() != key || voices[v].VoiceMode() != ModeIdx(mode))
          && voices[v].IsActive();
    ApplyVoiceSettings(v, mode, key);
    voices[v].SetKey(key);
    voices[v].SetVoiceMode(ModeIdx(mode));
    voices[v].SelectForRub(KeyFundamental(mode, key), 1.0f,
                           stolen ? 0.001f : 1.0f);
    // Pad velocity: a smoothed performance gain, separate from the
    // saved per-bowl volume (velocity 127 = 1.0 = local reference).
    voices[v].SetPerfGain(static_cast<float>(velocity) * (1.0f / 127.0f));
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
void SelectRimKey(int key, bool make_edit_target, int velocity = 127)
{
    const PlayMode mode = PlayMode::kSplit;
    const int      v    = AllocVoice(mode, key);
    if(v < 0)
        return;
    ApplyVoiceSettings(v, mode, key);
    voices[v].SetKey(key);
    voices[v].SetVoiceMode(ModeIdx(mode));
    // Deterministic detune, like strikes: the same bowl always sings
    // with the same voice when selected.
    voices[v].SelectForRub(KeyFundamental(mode, key), 1.0f);
    // Rim-selection velocity: the same smoothed performance gain as
    // pads (never the saved bowl volume).
    voices[v].SetPerfGain(static_cast<float>(velocity) * (1.0f / 127.0f));
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

/** Re-apply the performance gain of the voice holding (mode, key)
 *  from the current claim ownership: the maximum note-on velocity
 *  among the owners (shared pad/rim identities), retained unchanged
 *  through the release when no owners remain. */
void UpdatePerfGain(PlayMode mode, int key)
{
    const int best = play_modes.ClaimStrength(mode, key);
    if(best <= 0)
        return; // no owners: keep the release strength
    const int v = FindVoice(mode, key);
    if(v >= 0)
        voices[v].SetPerfGain(static_cast<float>(best) * (1.0f / 127.0f));
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

/** The effective rub candidate for (mode, key) in the CURRENT mode:
 *  the local wheel's decaying pumped level and each MIDI source's held
 *  CC 1 level compete per note; the HIGHEST level wins (never the
 *  sum, so mixed controllers cannot double the drive) and its texture
 *  applies; ties go to the latest control event (plan section 5). */
bowls::RubCandidate EffectiveRub(int vm, int key)
{
    bowls::RubCandidate best;
    if(play_modes.WheelTargets(key))
        best = bowls::ArbitrateRub(
            best,
            {wheel_level_[vm][key], wheel_clean_[vm][key],
             wheel_seq_[vm][key]});
    for(int s = 0; s < bowls::kMidiTransportCount; ++s)
        if(midi_rub_level_[s] > 0.0f
           && play_modes.MidiRubTargets(MidiSrcFromTransport(s), key))
            best = bowls::ArbitrateRub(
                best,
                {midi_rub_level_[s], midi_rub_clean_[s],
                 midi_rub_seq_[s]});
    return best;
}

/** Push each voice's rub level + character for this block. Only voices
 *  whose (mode, key) is in the CURRENT mode's wheel/MIDI rub sets are
 *  driven; outgoing-mode voices get target 0 and their last excitation
 *  falls away with the existing smooth slew (their resonator state is
 *  never erased). A MIDI level is HELD (no idle decay) until its
 *  source changes it or a cleanup clears it; the wheel level keeps its
 *  ~0.35 s idle decay. Split rim voices with a nonzero candidate but
 *  no voice (a strike chord stole it) silently reacquire, as today. */
void ApplyWheel()
{
    const int cur = ModeIdx(play_modes.Mode());
    if(play_modes.Mode() == PlayMode::kSplit)
        for(int key = 0; key <= bowls::kRimKeyLast; ++key)
            if(EffectiveRub(cur, key).level > 0.001f
               && FindVoice(PlayMode::kSplit, key) < 0)
                EnsureRimVoice(key); // silent reacquisition
    for(int i = 0; i < kVoiceCount; ++i)
    {
        const int key = voices[i].Key();
        const int vm  = voices[i].VoiceMode();
        float     target = 0.0f;
        float     clean  = 0.0f;
        if(key >= 0 && vm == cur)
        {
            const bowls::RubCandidate best = EffectiveRub(vm, key);
            target = best.level;
            clean  = best.clean ? 1.0f : 0.0f;
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

void SetTransportFeedback(TransportFeedback feedback, uint32_t duration_ms,
                          int slot = -1)
{
    transport_feedback_slot.store(slot, std::memory_order_relaxed);
    transport_feedback_until.store(System::GetNow() + duration_ms,
                                   std::memory_order_relaxed);
    transport_feedback.store(feedback, std::memory_order_release);
}

void BeginPlaybackEntry()
{
    if(playback_ui_state.load(std::memory_order_relaxed)
       != PlaybackUiState::kNormal)
        return;
    playback_saved_routing_ = routing_;
    playback_route_saved_   = true;
    routing_                = bowls::kRoutingNormal;
    playback_ui_state.store(PlaybackUiState::kEntering,
                            std::memory_order_release);
    playback_entry_pending.store(true, std::memory_order_release);
    record_ui_state.store(RecordUiState::kIdle, std::memory_order_release);
    chord_hold_valid_ = false;
    if(sd_recorder.IsBusy())
        sd_recorder.RequestStop();
    play_modes.Init(play_modes.Mode()); // releases ALL claims (incl. MIDI)
    midi_rub_level_[bowls::kDin] = 0.0f;
    midi_rub_level_[bowls::kUsb] = 0.0f;
    SyncPadGates();
    ApplyWheel();
}

void FinishPlaybackTransition()
{
    if(playback_route_saved_)
    {
        routing_ = playback_saved_routing_;
        playback_route_saved_ = false;
    }
    playback_entry_pending.store(false, std::memory_order_release);
    playback_ui_state.store(PlaybackUiState::kNormal,
                            std::memory_order_release);
}

/** Transport gestures run beside ordinary performance controls. */
bool HandleTransport()
{
    const uint32_t now = System::GetNow();
    const bool play_clicked = hw.PlayClicked();
    const bool loop_clicked = hw.LoopClicked();
    const bool play_released = hw.PlayReleased();
    const bool loop_released = hw.LoopReleased();
    PlaybackUiState playback_state
        = playback_ui_state.load(std::memory_order_acquire);
    const bool play_loop_chord
        = (hw.PlayPressed() && hw.LoopPressed()) || chord_hold_valid_
          || loop_hold_valid_;

    if(transport_wait_for_release_)
    {
        if(!hw.PlayPressed() && !hw.LoopPressed())
            transport_wait_for_release_ = false;
        return playback_state != PlaybackUiState::kNormal;
    }

    if(playback_state == PlaybackUiState::kEntering)
    {
        if(sd_player.Ready())
        {
            /* The hold that entered playback must not count toward the
             * exit hold: require a fresh PLAY press. */
            play_hold_valid_ = false;
            play_gesture_consumed_ = true;
            playback_ui_state.store(PlaybackUiState::kPlaying,
                                    std::memory_order_release);
        }
        else if(!sd_player.ModeActive()
                && !playback_entry_pending.load(std::memory_order_acquire))
            FinishPlaybackTransition();
        return true;
    }
    if(playback_state == PlaybackUiState::kExiting)
    {
        if(!sd_player.ModeActive())
            FinishPlaybackTransition();
        return true;
    }
    if(playback_state == PlaybackUiState::kPlaying
       && !sd_player.ModeActive())
    {
        FinishPlaybackTransition();
        return true;
    }
    if(playback_state == PlaybackUiState::kPlaying && sd_player.ModeActive())
    {
        if(play_clicked)
        {
            play_hold_started_ = now;
            play_hold_valid_ = true;
            play_gesture_consumed_ = false;
            play_solo_cancelled_ = hw.LoopPressed();
        }
        if(loop_clicked)
        {
            loop_hold_started_ = now;
            loop_hold_valid_ = true;
            loop_gesture_consumed_ = false;
            if(hw.PlayPressed())
                play_solo_cancelled_ = true;
        }
        if(hw.PlayPressed() && !hw.LoopPressed() && play_hold_valid_
           && !play_gesture_consumed_ && !play_solo_cancelled_
           && now - play_hold_started_ >= 2000)
        {
            playback_hold_started.store(play_hold_started_,
                                        std::memory_order_relaxed);
            play_gesture_consumed_ = true;
            sd_player.RequestExit();
            playback_ui_state.store(PlaybackUiState::kExiting,
                                    std::memory_order_release);
            return true;
        }
        if(play_released)
        {
            if(!play_gesture_consumed_)
                sd_player.TogglePause();
            play_hold_valid_ = false;
            play_gesture_consumed_ = false;
            play_solo_cancelled_ = false;
        }
        if(loop_released)
        {
            if(!loop_gesture_consumed_)
                sd_player.ToggleLoop();
            loop_hold_valid_ = false;
            loop_gesture_consumed_ = false;
        }
        for(int slot = 0; slot < 15; ++slot)
            if(sd_player.TakeAt(slot) != 0
               && hw.KeyRising(kWhiteKeyIndices[slot]))
                sd_player.SelectSlot(slot);
        return true;
    }

    if(playback_state != PlaybackUiState::kNormal)
        return true;

    if(play_clicked)
    {
        play_hold_started_ = now;
        play_hold_valid_ = true;
        play_gesture_consumed_ = false;
        play_solo_cancelled_ = hw.LoopPressed();
    }
    if(loop_clicked)
    {
        loop_hold_started_ = now;
        loop_hold_valid_ = true;
        loop_gesture_consumed_ = false;
        if(hw.PlayPressed())
            play_solo_cancelled_ = true;
    }

    const RecordUiState record_state
        = record_ui_state.load(std::memory_order_acquire);
    if(record_state == RecordUiState::kCountdown)
    {
        if(play_clicked || loop_clicked)
        {
            record_ui_state.store(RecordUiState::kIdle,
                                  std::memory_order_release);
            if(play_clicked)
                play_gesture_consumed_ = true;
            if(loop_clicked)
                loop_gesture_consumed_ = true;
        }
        else if(now - countdown_started_ >= 4000)
        {
            record_ui_state.store(RecordUiState::kIdle,
                                  std::memory_order_release);
            sd_recorder.RequestStart();
        }
    }
    else
    {
        /* One action per fresh chord hold: the SAME hold that stops a
         * recording must never fall through to re-arming, and an arm
         * hold must never retrigger before full release. */
        const bool chord = hw.PlayPressed() && hw.LoopPressed();
        const bool recorder_active
            = sd_recorder.IsRecording() || sd_recorder.IsBusy();
        if(chord)
        {
            if(!chord_hold_valid_ && !play_gesture_consumed_
               && !loop_gesture_consumed_)
            {
                chord_hold_started_ = now;
                chord_hold_valid_ = true;
                chord_action_done_ = false;
                record_ui_started.store(now, std::memory_order_relaxed);
                record_ui_state.store(recorder_active
                                          ? RecordUiState::kStopHold
                                          : RecordUiState::kArmHold,
                                      std::memory_order_release);
            }
            if(chord_hold_valid_ && !chord_action_done_)
            {
                if(recorder_active && now - chord_hold_started_ >= 1000)
                {
                    sd_recorder.RequestStop();
                    chord_action_done_ = true;
                    play_gesture_consumed_ = true;
                    loop_gesture_consumed_ = true;
                    record_ui_state.store(RecordUiState::kIdle,
                                          std::memory_order_release);
                }
                else if(!recorder_active && now - chord_hold_started_ >= 2000)
                {
                    chord_action_done_ = true;
                    play_gesture_consumed_ = true;
                    loop_gesture_consumed_ = true;
                    record_ui_state.store(RecordUiState::kIdle,
                                          std::memory_order_release);
                    if(preset_storage.NoSdCard())
                        SetTransportFeedback(kFeedbackRecordRefused, 1000);
                    else
                    {
                        countdown_started_ = now;
                        record_ui_started.store(now,
                                                std::memory_order_relaxed);
                        record_ui_state.store(RecordUiState::kCountdown,
                                              std::memory_order_release);
                    }
                }
            }
        }
        else if(chord_hold_valid_)
        {
            /* Released before the action: swallow both buttons past
             * 400 ms so a near-chord can never leak a LOOP latch. */
            if(!chord_action_done_ && now - chord_hold_started_ >= 400)
            {
                play_gesture_consumed_ = true;
                loop_gesture_consumed_ = true;
            }
            chord_hold_valid_ = false;
            if(record_state == RecordUiState::kArmHold
               || record_state == RecordUiState::kStopHold)
                record_ui_state.store(RecordUiState::kIdle,
                                      std::memory_order_release);
        }

        if(hw.PlayPressed() && !hw.LoopPressed() && play_hold_valid_
           && !play_solo_cancelled_ && !play_gesture_consumed_
           && preset_mode_ == kPresetNone
           && now - play_hold_started_ >= 2000)
        {
            play_gesture_consumed_ = true;
            playback_hold_started.store(play_hold_started_,
                                        std::memory_order_relaxed);
            BeginPlaybackEntry();
            return true;
        }
    }

    if(loop_released)
    {
        if(!loop_gesture_consumed_ && !chord_hold_valid_
           && record_ui_state.load(std::memory_order_relaxed)
                  == RecordUiState::kIdle)
            play_modes.LoopPressed();
        loop_hold_valid_ = false;
        loop_gesture_consumed_ = false;
    }
    if(play_released)
    {
        if(!play_loop_chord && !play_gesture_consumed_
           && !play_solo_cancelled_ && preset_mode_ == kPresetNone
           && record_ui_state.load(std::memory_order_relaxed)
                  == RecordUiState::kIdle)
            play_modes.PlayPressed();
        play_hold_valid_ = false;
        play_gesture_consumed_ = false;
        play_solo_cancelled_ = false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// MIDI event application (audio-ISR safe: voices/params/claims only -
// never HAL, USB, file access or blocking calls)
// ---------------------------------------------------------------------------

/** CC 120 All Sound Off: the global emergency panic. Clears every
 *  performance claim, the LOOP latch, sustain, wheel/MIDI excitation
 *  and the Split fallbacks; silences the bowl voices immediately
 *  (cheap per-voice state clear, no full Init) and mutes the bowl bus
 *  with a <= 5 ms fade. The reverb's ~387 KB tail is cleared in the
 *  main loop (reverb_reset_pending_) before the mute lifts. Saved
 *  parameters, the active mode and the routing are untouched; the AUX
 *  return in Send FX is intentionally NOT silenced (it may be another
 *  instrument). Fresh presses are required - nothing revives from a
 *  still-held key. */
void MidiPanic()
{
    play_modes.MidiPanic();
    for(int m = 0; m < bowls::kPlayModeCount; ++m)
        for(int k = 0; k < bowls::kKeyCount; ++k)
            wheel_level_[m][k] = 0.0f;
    midi_rub_level_[bowls::kDin] = 0.0f;
    midi_rub_level_[bowls::kUsb] = 0.0f;
    for(int v = 0; v < kVoiceCount; ++v)
    {
        voices[v].SetRub(0.0f, 0.0f);
        voices[v].PanicReset();
    }
    rub_display_            = 0.0f;
    panic_mute_             = true;
    reverb_reset_pending_   = true;
}

/** The sender's per-bowl edit target (CC 81 bank / CC 82 key):
 *  fixed bank or the active mode; never the local last_key_. */
void MidiEditTarget(int src, int& mode_idx, int& key)
{
    mode_idx = midi_edit_bank_[src] < 0 ? ModeIdx(play_modes.Mode())
                                        : midi_edit_bank_[src];
    key      = midi_edit_key_[src];
}

void ApplyMidiNoteOn(int src, uint8_t note, uint8_t vel, uint32_t now)
{
    const int key = bowls::MidiCcMap::NoteToKey(note);
    if(key < 0 || vel == 0)
        return; // out-of-range / vel-0 are filtered or normalized earlier
    // A valid positive-velocity Note On also selects this sender's
    // edit key for subsequent per-bowl CCs (even with a fixed bank).
    midi_edit_key_[src] = key;
    const bowls::Source    s   = MidiSrcFromTransport(src);
    const bowls::KeyAction act = play_modes.MidiNoteOn(s, key, vel);
    switch(act.kind)
    {
        case bowls::KeyAction::kStrike:
            StrikeKey(act.mode, act.key, vel, false, true);
            break;
        case bowls::KeyAction::kPadOn:
            StartPadKey(act.key, vel);
            UpdatePerfGain(PlayMode::kRimming, act.key);
            break;
        case bowls::KeyAction::kRimSelect:
            SelectRimKey(act.key, false, vel);
            UpdatePerfGain(PlayMode::kSplit, act.key);
            break;
        default:
            // Repeated Note On (no stacked gate) / latch toggle-off:
            // the velocity claim changed - refresh the shared strength.
            UpdatePerfGain(act.mode, act.key);
            break;
    }
    (void)now;
}

void ApplyMidiNoteOff(int src, uint8_t note)
{
    const int key = bowls::MidiCcMap::NoteToKey(note);
    if(key < 0)
        return;
    const int origin
        = play_modes.MidiNoteOff(MidiSrcFromTransport(src), key);
    if(origin >= 0)
        UpdatePerfGain(static_cast<PlayMode>(origin), key);
}

void ApplyMidiCc(int src, uint8_t cc, uint8_t val, uint32_t now)
{
    namespace ccmap           = bowls::MidiCcMap;
    const bowls::Source s     = MidiSrcFromTransport(src);
    int                 m, key;
    switch(cc)
    {
        case ccmap::kCcRimLevel: // CC 1: held absolute rub level
            midi_rub_level_[src] = ccmap::CcToLevel(val);
            midi_rub_seq_[src]   = ++rub_seq_;
            break;
        case ccmap::kCcMasterVolume: // CC 7
            StoreTicks(params.volume_ticks, ccmap::CcToTicks(val), now);
            break;
        case ccmap::kCcPan: // CC 10
            MidiEditTarget(src, m, key);
            SetPanTicks(m, key, ccmap::CcToTicks(val), now);
            break;
        case ccmap::kCcSustain: // CC 64: per-source Rimming hold pedal
            play_modes.MidiSetSustain(s, ccmap::CcToSwitch(val));
            break;
        case ccmap::kCcProfile: // CC 70
            MidiEditTarget(src, m, key);
            SetProfileTicks(m, key, ccmap::CcToProfile(val), now);
            break;
        case ccmap::kCcBrightness: // CC 74
            MidiEditTarget(src, m, key);
            SetBrightnessTicks(m, key, ccmap::CcToTicks(val), now);
            break;
        case ccmap::kCcDecay: // CC 75
            MidiEditTarget(src, m, key);
            SetDecayTicks(m, key, ccmap::CcToTicks(val), now);
            break;
        case ccmap::kCcTune: // CC 76
            MidiEditTarget(src, m, key);
            SetTuneTicks(m, key, ccmap::CcToTicks(val), now);
            break;
        case ccmap::kCcBowlVolume: // CC 77
            MidiEditTarget(src, m, key);
            SetBowlVolTicks(m, key, ccmap::CcToTicks(val), now);
            break;
        case ccmap::kCcStrike: // CC 78
            StoreTicks(params.strike_ticks, ccmap::CcToTicks(val), now);
            break;
        case ccmap::kCcShimmer: // CC 79
            if(StoreTicks(params.shimmer_ticks, ccmap::CcToTicks(val),
                          now))
                ApplyShimmerToVoices();
            break;
        case ccmap::kCcMode: // CC 80: global play-mode selection
            if(play_modes.SetMode(static_cast<PlayMode>(
                   ccmap::CcToMode(val))))
            {
                // A mode change silences MIDI rubbing: a held CC 1
                // level must not unexpectedly drive the new mode.
                midi_rub_level_[bowls::kDin] = 0.0f;
                midi_rub_level_[bowls::kUsb] = 0.0f;
            }
            break;
        case ccmap::kCcEditBank: // CC 81: 0/1/2 fixed, 127 follow
        {
            const int bank = ccmap::CcToEditBank(val);
            if(bank == ccmap::kEditFollow)
                midi_edit_bank_[src] = -1;
            else if(bank != ccmap::kEditIgnore)
                midi_edit_bank_[src] = bank;
            break;
        }
        case ccmap::kCcEditKey: // CC 82: exact note 48-72 selects key
        {
            const int edit_key = ccmap::CcToEditKey(val);
            if(edit_key >= 0)
                midi_edit_key_[src] = edit_key;
            break;
        }
        case ccmap::kCcLoop: // CC 83: idempotent LOOP latch setter
            play_modes.SetLatchEnabled(ccmap::CcToSwitch(val));
            break;
        case ccmap::kCcRimTexture: // CC 84: friction / clean
            midi_rub_clean_[src] = ccmap::CcToSwitch(val);
            midi_rub_seq_[src]   = ++rub_seq_;
            break;
        case ccmap::kCcRouting: // CC 85: session-only routing
            routing_ = ccmap::CcToSwitch(val) ? bowls::kRoutingSendFx
                                              : bowls::kRoutingNormal;
            break;
        case ccmap::kCcReverbMix: // CC 91
            StoreTicks(params.reverb_mix_ticks, ccmap::CcToTicks(val),
                       now);
            break;
        case ccmap::kCcAllSoundOff: // CC 120: global panic
            MidiPanic();
            break;
        case ccmap::kCcResetControllers: // CC 121: sender reset
            play_modes.MidiResetControllers(s);
            midi_rub_level_[src] = 0.0f;
            midi_rub_clean_[src] = false;
            midi_rub_seq_[src]   = ++rub_seq_;
            midi_edit_bank_[src] = -1; // follow the active mode
            midi_edit_key_[src]  = 0;
            break;
        case ccmap::kCcAllNotesOff: // CC 123: sender notes off
            play_modes.MidiAllNotesOff(s);
            midi_rub_level_[src] = 0.0f;
            midi_rub_seq_[src]   = ++rub_seq_;
            break;
        default: break; // unreachable: the transport filters CCs
    }
}

void ApplyMidiEvent(int src, bowls::BowlsMidiEvent e)
{
    const uint32_t now = System::GetNow();
    if(e.type == bowls::BowlsMidiEvent::kNoteOn)
        ApplyMidiNoteOn(src, e.data1, e.data2, now);
    else if(e.type == bowls::BowlsMidiEvent::kNoteOff)
        ApplyMidiNoteOff(src, e.data1);
    else
        ApplyMidiCc(src, e.data1, e.data2, now);
}

/** Bounded MIDI dispatch, once per audio block: at most four queued
 *  items (events or source cleanups), fairly alternating DIN/USB so a
 *  flooded transport cannot starve the other; order within each source
 *  is preserved. A source cleanup (queue overflow or UART error,
 *  latched by the transport layer) releases that source's claims and
 *  rub level before any fresh batch is accepted - no stale queued
 *  Note On survives it. */
void DispatchMidi()
{
    midi.Router().Dispatch(
        4,
        [](int src, bowls::BowlsMidiEvent e) { ApplyMidiEvent(src, e); },
        [](int src) {
            play_modes.MidiAllNotesOff(MidiSrcFromTransport(src));
            midi_rub_level_[src] = 0.0f;
        });
}

/** Read encoders/keys, update parameter state and the voices.
 *  Runs once per audio block (~0.5 ms), after hw.ProcessControls(). */
void ApplyControls()
{
    DispatchMidi();
    // Pad gate reconciliation runs even while the playback transport UI
    // owns this function (HandleTransport early-returns then): MIDI
    // note-offs must never leave a stuck pad gate.
    SyncPadGates();
    if(HandleTransport())
        return;
    RetriggerLatchedStrikes();
    const uint32_t now   = System::GetNow();
    const bool     shift = hw.ShiftHeld();
    static bool prev_shift_held = false;
    if(shift && !prev_shift_held)
        PresetShiftConfirm();
    prev_shift_held = shift;

    // Volume knob press: page 1 = master, page 2 = delay, page 3 = reverb.
    // The newly active page's bar shows as confirmation.
    if(hw.VolumeClicked())
    {
        vol_page_ = (vol_page_ + 1) % 3;
        ShowOverlay(vol_page_ == kVolPageVolume
                        ? Overlay::Volume
                        : (vol_page_ == kVolPageDelay ? Overlay::DelayMix
                                                      : Overlay::ReverbMix),
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
            SetPanTicks(cur_mode, key,
                        params.pan_ticks[cur_mode][key].load(
                            std::memory_order_relaxed)
                            + enc1_inc,
                        now);
            ShowOverlay(Overlay::Pan, now); // also shows at the end stops
        }
        else if(enc1_page_ == kEnc1PageVolume)
        {
            // Per-bowl volume: rides the ringing voice live (smoothed
            // inside the voice, like the pan gains).
            SetBowlVolTicks(cur_mode, key,
                            params.bowl_vol_ticks[cur_mode][key].load(
                                std::memory_order_relaxed)
                                + enc1_inc,
                            now);
            ShowOverlay(Overlay::BowlVolume, now); // also at the end stops
        }
        else if(enc1_page_ == kEnc1PageCharacter && !shift)
        {
            // Resonance character (profile) of the selected bowl: two
            // detent ticks per step; the bar shows on any turn so the
            // section is visible while arming. Applies at the next
            // strike/selection, like tuning.
            const int step = SectionStep(profile_sel_acc, enc1_inc);
            SetProfileTicks(cur_mode, key,
                            params.profile_ticks[cur_mode][key].load(
                                std::memory_order_relaxed)
                                + step,
                            now);
            ShowOverlay(Overlay::Character, now);
        }
        else if(enc1_page_ == kEnc1PageTune && !shift)
        {
            // Per-bowl tuning: applied at the next strike/selection
            // (physical character); only the bar shows while a bowl
            // rings.
            SetTuneTicks(cur_mode, key,
                         params.tune_ticks[cur_mode][key].load(
                             std::memory_order_relaxed)
                             + enc1_inc,
                         now);
            ShowOverlay(Overlay::Tune, now); // also at the end stops
        }
    }

    // Physical encoder 2: strike hardness (GLOBAL); SHIFT+turn =
    // brightness of the SELECTED bowl (per-(mode,key)). Brightness is
    // pushed to the voice live: it brightens rubbing immediately and
    // the next strike.
    const int strike_inc = hw.EncoderIncrement(1);
    if(shift)
    {
        if(strike_inc != 0)
        {
            const int cur_mode = ModeIdx(play_modes.Mode());
            const int key      = last_key_[cur_mode];
            SetBrightnessTicks(cur_mode, key,
                               params.brightness_ticks[cur_mode][key].load(
                                   std::memory_order_relaxed)
                                   + strike_inc,
                               now);
            ShowOverlay(Overlay::Brightness, now); // also at the end stops
        }
    }
    else
    {
        BumpTicks(params.strike_ticks, strike_inc, Overlay::Strike, now);
    }

    // Physical encoder 3: decay of the SELECTED bowl (per-(mode,key));
    // rides the ringing voice holding it live.
    const int decay_inc = hw.EncoderIncrement(2);
    if(decay_inc != 0)
    {
        const int cur_mode = ModeIdx(play_modes.Mode());
        const int key      = last_key_[cur_mode];
        SetDecayTicks(cur_mode, key,
                      params.decay_ticks[cur_mode][key].load(
                          std::memory_order_relaxed)
                          + decay_inc,
                      now);
        ShowOverlay(Overlay::Decay, now); // also at the end stops
    }

    // Physical encoder 4: shimmer / paired-mode detune (GLOBAL, live).
    const int shimmer_inc = hw.EncoderIncrement(3);
    if(shimmer_inc != 0)
    {
        BumpTicks(params.shimmer_ticks, shimmer_inc, Overlay::Shimmer, now);
        ApplyShimmerToVoices();
    }

    // Volume encoder pages: master; combined delay mix/feedback with
    // SHIFT+turn for time; then reverb mix.
    const int volume_inc = hw.EncoderIncrement(5);
    if(volume_inc != 0)
    {
        if(vol_page_ == kVolPageDelay)
        {
            if(shift)
            {
                BumpTicks(params.delay_time_ticks, volume_inc,
                          Overlay::DelayTime, now);
            }
            else
            {
                BumpTicks(params.delay_mix_ticks, volume_inc,
                          Overlay::DelayMix, now);
            }
            ApplyDelayTargets();
        }
        else if(vol_page_ == kVolPageReverb)
        {
            if(!shift)
                BumpTicks(params.reverb_mix_ticks, volume_inc,
                          Overlay::ReverbMix, now);
        }
        else if(!shift)
            BumpTicks(params.volume_ticks, volume_inc, Overlay::Volume, now);
    }

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
                    wheel_seq_[cur_mode][key]   = ++rub_seq_;
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
                wheel_seq_[cur_mode][key]   = ++rub_seq_;
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
                wheel_seq_[cur_mode][key]   = ++rub_seq_;
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
    // With MIDI playing, the EFFECTIVE (arbitrated) rub level shows.
    {
        const int cur_mode = ModeIdx(play_modes.Mode());
        float     d        = 0.0f;
        for(int k = 0; k < bowls::kKeyCount; ++k)
        {
            const float lvl = EffectiveRub(cur_mode, k).level;
            if(lvl > d)
                d = lvl;
        }
        rub_display_ = d;
    }
    ApplyWheel();

    // Preset gestures are intercepted before routing and play-mode
    // handling, so white keys select slots only in the matching context.
    // Route every remaining key through the play-mode state machine: SHIFT +
    // Black 6/Black 7 are consumed as the audio-routing selection
    // first; SHIFT + lower F#/G#/A# are consumed as mode shortcuts
    // through release; every other key press/release is interpreted
    // in the mode it started in (so a key-up after a mode switch
    // still releases the right note).
    for(int key = 0; key < bowls::kKeyCount; ++key)
    {
        if(hw.KeyRising(key))
        {
            if(PresetKeyEvent(key, true, shift))
                continue;
            // SHIFT + Black 6 (C#4, key 13) / Black 7 (D#4, key 15):
            // audio routing selection (audio_routing.h). Consumed
            // before the play-mode state machine, so the combo never
            // sounds a note or moves the bowl editor target; the
            // release is a no-op (the key was never marked down).
            if(shift && bowls::IsRoutingSelectKey(key))
            {
                routing_ = bowls::RoutingAfterKey(routing_, key);
                continue;
            }
            const bowls::KeyAction act = play_modes.KeyDown(key, shift);
            if(act.kind != bowls::KeyAction::kModeChanged
               && play_modes.DownMode(bowls::Source::kLocal, key) >= 0)
                midi.OutRouter().PushLocalNoteOn(key);
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
                    // MIDI rubbing silences too:
                    // a held CC 1 level must not unexpectedly drive the
                    // newly selected mode.
                    midi_rub_level_[bowls::kDin] = 0.0f;
                    midi_rub_level_[bowls::kUsb] = 0.0f;
                    break;
                default: break;
            }
        }
        else if(hw.KeyFalling(key))
        {
            if(PresetKeyEvent(key, false, shift))
                continue;
            // Releases reconcile through the state machine (PadOn) and
            // SyncPadGates below - no explicit action needed here.
            const bool was_note =
                play_modes.DownMode(bowls::Source::kLocal, key) >= 0;
            play_modes.KeyUp(key);
            if(was_note)
                midi.OutRouter().PushLocalNoteOff(key);
        }
    }

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
    // The routing is read once per block; the per-sample loop below
    // branches on this cached value (audio_routing.h).
    const int routing = sd_player.ModeActive() ? bowls::kRoutingNormal
                                              : routing_;
    // CC 120 panic handshake: while the main loop clears the reverb's
    // SDRAM state, the reverb is not processed (no concurrent access)
    // and the bowl bus stays muted by the fast panic fade.
    const bool rev_reset = reverb_reset_pending_;
    float bus_l[kBlockSize];
    float bus_r[kBlockSize];
    for(size_t i = 0; i < size; ++i)
    {
        daisysp::fonepole(volume_smooth, volume_target, 0.001f);
        daisysp::fonepole(mix_smooth, mix_target, 0.001f);
        // Panic mute: ~5 ms click-controlled fade of the internal bowl
        // bus (voices + their reverb tail); the Send-FX AUX return is
        // intentionally not silenced (audio_routing.h applies it after
        // this gain).
        panic_gain_
            += 0.03f * ((panic_mute_ ? 0.0f : 1.0f) - panic_gain_);
        // Temple-room reverb on the stereo bowl bus, before the master
        // gain so the volume knob also controls the tail. The send is
        // lowpassed dark right after a strike (the mallet contact burst
        // barely excites a real room) and re-opens into the pure ring;
        // the dry path keeps the full spectrum.
        float delay_l, delay_r;
        bbd_delay.Process(mix_l[i], mix_r[i], delay_l, delay_r);
        send_dark_ -= kSendDarkRelease * send_dark_;
        const float send_coef
            = kSendLpOpen + (kSendLpDark - kSendLpOpen) * send_dark_;
        daisysp::fonepole(send_lp_l_, delay_l, send_coef);
        daisysp::fonepole(send_lp_r_, delay_r, send_coef);
        daisysp::fonepole(send_lp2_l_, send_lp_l_, send_coef);
        daisysp::fonepole(send_lp2_r_, send_lp_r_, send_coef);
        float wet_l = 0.0f, wet_r = 0.0f;
        if(!rev_reset)
            reverb.Process(send_lp2_l_, send_lp2_r_, &wet_l, &wet_r);
        const float dry   = 1.0f - mix_smooth;
        bus_l[i] = (delay_l * dry + wet_l * mix_smooth) * panic_gain_;
        bus_r[i] = (delay_r * dry + wet_r * mix_smooth) * panic_gain_;
    }

    // SD record tap (SdRecorder applies the -6 dB record level):
    // pre-master-gain in both routings, so the take level never
    // depends on the volume knob. Normal records the stereo bowl bus
    // (including reverb); Send FX records the AUX IN return - the
    // processed signal the performer actually hears in the
    // headphones. Playback is injected after this tap, so a take
    // never re-records playback.
    if(routing == bowls::kRoutingSendFx)
        sd_recorder.Capture(in[2], in[3], size);
    else
        sd_recorder.Capture(bus_l, bus_r, size);
    sd_player.Render(bus_l, bus_r, size);

    const float gain = volume_smooth * volume_smooth;
    for(size_t i = 0; i < size; ++i)
    {
        // Playback is injected after reverb but before master gain.
        float hp_l, hp_r, line_l, line_r;
        bowls::RouteOutputs(routing,
                            bus_l[i],
                            bus_r[i],
                            in[2][i],
                            in[3][i],
                            gain,
                            hp_l,
                            hp_r,
                            line_l,
                            line_r);
        out[0][i] = daisysp::SoftClip(hp_l);   // headphone L
        out[1][i] = daisysp::SoftClip(hp_r);   // headphone R
        out[2][i] = daisysp::SoftClip(line_l); // master L
        out[3][i] = daisysp::SoftClip(line_r); // master R
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
                // Yellow-white bar: brightness of the selected bowl.
                DrawBar(params.brightness_ticks[cur_mode][edit_key].load(
                            std::memory_order_relaxed),
                        44, 44, 8);
                break;
            case Overlay::Decay:
                // Blue bar: decay of the selected bowl, matching the
                // encoder 3 LED.
                DrawBar(params.decay_ticks[cur_mode][edit_key].load(
                            std::memory_order_relaxed),
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
                case Overlay::DelayMix:
                DrawBar(params.delay_mix_ticks.load(std::memory_order_relaxed),
                    44, 38, 0);
                break;
                case Overlay::DelayTime:
                DrawBar(params.delay_time_ticks.load(std::memory_order_relaxed),
                    44, 38, 0);
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

    // Held Strike notes glow blue, held Rimming notes glow yellow,
    // ringing voices glow dim amber, and latched notes use their mode
    // color - except while a temporary overlay bar is up.
    if(!overlay_active)
    {
        // Physical keys AND MIDI-held notes (any source's claim) alike.
        for(unsigned key = 0; key < 25; ++key)
            if(hw.KeyHeld(key) || play_modes.AnyHeld(key))
            {
                if(play_modes.Mode() == PlayMode::kStrikes)
                    chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[key],
                                           0, 0, 48);
                else if(play_modes.Mode() == PlayMode::kRimming)
                    chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[key],
                                           40, 40, 0);
                else
                    chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[key],
                                           48, 48, 48);
            }
        for(int v = 0; v < kVoiceCount; ++v)
        {
            const int key = voices[v].Key();
            if(key >= 0 && voices[v].IsActive() && !hw.KeyHeld(key)
               && !play_modes.AnyHeld(key))
                chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[key],
                                       14, 8, 2);
        }
        for(int key = 0; key < bowls::kKeyCount; ++key)
            if(!hw.KeyHeld(key) && !play_modes.AnyHeld(key))
            {
                const bool rimming_latched = play_modes.Latched(key);
                const bool strike_latched = play_modes.StrikeLatched(key);
                const bool show_strike
                    = strike_latched
                      && (play_modes.Mode() == PlayMode::kStrikes
                          || !rimming_latched);
                if(show_strike)
                    chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[key],
                                           0, 0, 48);
                else if(rimming_latched)
                    chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[key],
                                           40, 40, 0);
            }
    }
}

bool DrawPresetKeybed(uint32_t now)
{
    const int mode = preset_mode_;
    if(mode == kPresetNone)
        return false;

    for(unsigned led = 0; led < chompi_leds::kSmtLedCount; ++led)
        chompi_leds::SetSmtLed(led, 0, 0, 0);

    const bool pre_blink = (now / 250u) % 2u == 0;
    const bool animation = mode == kPresetSaving || mode == kPresetCopying
                           || mode == kPresetErasing;
    const bool save_like = mode == kPresetSaveSel || mode == kPresetCopyDest;
    for(int slot = 0; slot < 15; ++slot)
    {
        uint8_t red = 0, green = 0, blue = 0;
        const bool filled = preset_bank.IsValid(slot);
        if(animation)
        {
            if(filled)
            {
                red = 26;
                blue = 48;
            }
        }
        else if(mode == kPresetCopyDest && slot == preset_copy_src_)
            green = 48;
        else if(slot == preset_sel_slot_)
        {
            if(mode == kPresetEraseSel)
                red = 48;
            else
                blue = 48;
        }
        else if(filled && slot == preset_current_)
        {
            if(save_like || pre_blink)
                red = green = blue = 48;
        }
        else if(preset_storage.IsIncompatibleSlot(slot) && pre_blink)
            red = 48;
        else if(filled)
        {
            if(save_like)
            {
                red = 26;
                blue = 48;
            }
            else if(pre_blink)
            {
                red = 10;
                blue = 19;
            }
        }
        else if(save_like && pre_blink)
            red = green = blue = 19;

        chompi_leds::SetSmtLed(
            chompi_leds::kWhiteKeyLeds[slot], red, green, blue);
    }
    return true;
}

bool DrawTransportKeybed(uint32_t now)
{
    const uint32_t feedback_until
        = transport_feedback_until.load(std::memory_order_relaxed);
    const int feedback = transport_feedback.load(std::memory_order_acquire);
    if(static_cast<int32_t>(feedback_until - now) > 0)
    {
        if(feedback == kFeedbackRecordRefused)
            for(int i = 0; i < 4; ++i)
                chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[i], 48, 0, 0);
        else if(feedback == kFeedbackNoTakes
                || feedback == kFeedbackPlayerFatal)
            for(int i = 0; i < 15; ++i)
                chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[i], 48, 0, 0);
        else if(feedback == kFeedbackInvalidTake)
        {
            const int slot = transport_feedback_slot.load(std::memory_order_relaxed);
            if(slot >= 0 && slot < 15)
                chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[slot], 48, 0, 0);
        }
        return true;
    }

    const RecordUiState rec_state
        = record_ui_state.load(std::memory_order_acquire);
    if(rec_state == RecordUiState::kArmHold
       || rec_state == RecordUiState::kStopHold)
    {
        /* Red fill: 2 s to arm, 1 s to stop. */
        const uint32_t hold_ms
            = rec_state == RecordUiState::kArmHold ? 2000u : 1000u;
        const uint32_t started = record_ui_started.load(std::memory_order_relaxed);
        const unsigned lit = static_cast<unsigned>(
            (static_cast<uint64_t>(now - started) * 15u + hold_ms - 1u)
            / hold_ms);
        for(unsigned i = 0; i < lit && i < 15; ++i)
            chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[i], 40, 0, 0);
        return true;
    }
    if(rec_state == RecordUiState::kCountdown)
    {
        const uint32_t elapsed = now - record_ui_started.load(std::memory_order_relaxed);
        const unsigned red = elapsed / 1000u > 4 ? 4 : elapsed / 1000u;
        for(unsigned i = 0; i < 4; ++i)
            chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[i],
                                   i < red ? 40 : 30,
                                   i < red ? 0 : 30,
                                   i < red ? 0 : 30);
        return true;
    }

    const PlaybackUiState playback_state
        = playback_ui_state.load(std::memory_order_acquire);
    if(playback_state == PlaybackUiState::kEntering
       || playback_state == PlaybackUiState::kExiting)
    {
        const uint32_t started = playback_hold_started.load(std::memory_order_relaxed);
        const unsigned lit = playback_state == PlaybackUiState::kEntering
                                 ? (now / 120u) % 16u
                                 : static_cast<unsigned>(
                                       (static_cast<uint64_t>(now - started) * 15u
                                        + 1999u) / 2000u);
        for(unsigned i = 0; i < lit && i < 15; ++i)
            chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[i], 0, 0, 40);
        return true;
    }
    /* PLAY-alone hold progress toward entering or exiting playback: a
     * blue left-to-right fill over the 2 s hold, drawn in BOTH normal
     * and playback modes. A chord (LOOP joins), a cancelled solo hold,
     * or an already-completed hold never fills. */
    if(hw.PlayPressed() && !hw.LoopPressed() && play_hold_valid_
       && !play_gesture_consumed_ && !play_solo_cancelled_
       && preset_mode_ == kPresetNone)
    {
        const unsigned lit = static_cast<unsigned>(
            (static_cast<uint64_t>(now - play_hold_started_) * 15u + 1999u)
            / 2000u);
        for(unsigned i = 0; i < lit && i < 15; ++i)
            chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[i], 0, 0, 40);
        return true;
    }
    if(playback_state == PlaybackUiState::kPlaying && sd_player.Ready())
    {
        const int selected = sd_player.SelectedSlot();
        for(int slot = 0; slot < 15; ++slot)
        {
            if(sd_player.TakeAt(slot) == 0)
                continue;
            if(slot == selected)
            {
                const bool on = !sd_player.Playing() || (now / 250u) % 2u == 0;
                if(on)
                    chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[slot],
                                           0, 0, 48);
            }
            else
                chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[slot],
                                       10, 10, 10);
        }
        return true;
    }
    return false;
}

void RenderLeds()
{
    // Every frame starts dark: overlays are fully redrawn each pass so
    // timed-out bars revert to the base view (held keys + ringing glow).
    for(unsigned led = 0; led < chompi_leds::kSmtLedCount; ++led)
        chompi_leds::SetSmtLed(led, 0, 0, 0);

    const uint32_t now = System::GetNow();
    const bool transport_keybed = DrawTransportKeybed(now);
    const bool keybar_overlay_active
        = static_cast<int32_t>(
              params.overlay_until.load(std::memory_order_relaxed) - now)
          > 0;
    if(!transport_keybed && !DrawPresetKeybed(now))
        DrawOverlayBar(now);

    // While SHIFT is held, highlight the active mode's lower black
    // shortcut key in its mode color and the active audio routing's
    // selection key in pink. Black keys never collide with the white-key
    // parameter bars, so this reads over either view.
    if(hw.ShiftHeld() && !sd_player.ModeActive())
    {
        const int sk
            = bowls::kModeShortcutKey[ModeIdx(play_modes.Mode())];
        switch(play_modes.Mode())
        {
            case PlayMode::kStrikes:
                chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[sk],
                                       0, 26, 48);
                break;
            case PlayMode::kRimming:
                chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[sk],
                                       40, 40, 0);
                break;
            case PlayMode::kSplit:
                chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[sk],
                                       48, 0, 24);
                break;
        }
        chompi_leds::SetSmtLed(
            chompi_leds::kKeyLeds[bowls::RoutingSelectKey(routing_)],
            static_cast<uint8_t>(48.0f * bowls::kRoutingColorR),
            static_cast<uint8_t>(48.0f * bowls::kRoutingColorG),
            static_cast<uint8_t>(48.0f * bowls::kRoutingColorB));

        if(preset_mode_ == kPresetNone
           && playback_ui_state.load(std::memory_order_acquire)
                  == PlaybackUiState::kNormal
           && (transport_keybed || !keybar_overlay_active))
        {
            // SHIFT advertises loadable slots above other keybed overlays.
            for(int slot = 0; slot < 15; ++slot)
                chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[slot],
                                       0, 0, 0);
            if(preset_storage.NoSdCard())
            {
                for(int slot = 0; slot < 15; ++slot)
                    chompi_leds::SetSmtLed(
                        chompi_leds::kWhiteKeyLeds[slot], 48, 0, 0);
            }
            else
            {
                for(int slot = 0; slot < 15; ++slot)
                    if(preset_bank.IsValid(slot))
                        chompi_leds::SetSmtLed(
                            chompi_leds::kWhiteKeyLeds[slot],
                            48, 48, 48);
            }
        }
    }

    const PlaybackUiState preset_playback_state
        = playback_ui_state.load(std::memory_order_acquire);
    if(preset_playback_state == PlaybackUiState::kNormal
       && (hw.ShiftHeld() || preset_mode_ != kPresetNone))
    {
        uint8_t erase_led = 0, copy_led = 0, save_led = 0;
        if(!preset_storage.NoSdCard())
        {
            switch(preset_mode_)
            {
                case kPresetNone:
                    erase_led = copy_led = save_led = 48;
                    break;
                case kPresetEraseSel:
                case kPresetErasing:
                    erase_led = 48;
                    break;
                case kPresetCopySrc:
                case kPresetCopyDest:
                case kPresetCopying:
                    copy_led = 48;
                    break;
                default:
                    save_led = 48;
                    break;
            }
        }
        chompi_leds::SetSmtLed(7, erase_led, 0, 0);
        chompi_leds::SetSmtLed(8, 0, copy_led, 0);
        chompi_leds::SetSmtLed(9, 0, 0, save_led);
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
    chompi_leds::SetPthLed(chompi_leds::kPthEncoder2, 20, 11, 0); // strike / brightness (shift turn)
    chompi_leds::SetPthLed(chompi_leds::kPthEncoder3, 0, 8, 20);  // decay
    chompi_leds::SetPthLed(chompi_leds::kPthEncoder4, 34, 0, 48); // shimmer
#ifdef CPU_METER
    // Steal witness: encoder 4 LED goes white for ~300 ms when a strike
    // steals a still-ringing voice - correlate it with audible chokes.
    if(System::GetNow() < g_steal_flash_until)
        chompi_leds::SetPthLed(chompi_leds::kPthEncoder4, 40, 40, 40);
#endif
    const uint8_t rub = static_cast<uint8_t>(rub_display_ * 40.0f);
    chompi_leds::SetPthLed(chompi_leds::kPthWheelLeft, rub, rub, rub);
    chompi_leds::SetPthLed(chompi_leds::kPthWheelRight, rub, rub, rub);
    const PlaybackUiState playback_state
        = playback_ui_state.load(std::memory_order_acquire);
    if(playback_state != PlaybackUiState::kNormal)
    {
        const bool play_led_on
            = (playback_state == PlaybackUiState::kPlaying
               && !sd_player.Playing())
              || (now / 250u) % 2u == 0;
        chompi_leds::SetPthLed(chompi_leds::kPthPlay,
                               0, 0, play_led_on ? 40 : 0);
        chompi_leds::SetPthLed(chompi_leds::kPthLoop,
                               0, 0,
                               playback_state == PlaybackUiState::kPlaying
                                       && sd_player.Looping()
                                   ? 40
                                   : 0);
    }
    else
    {
        const uint8_t strike_latch
            = play_modes.StrikeLatchEnabled() ? 40 : 0;
        chompi_leds::SetPthLed(chompi_leds::kPthPlay,
                               0, 0, strike_latch);
        chompi_leds::SetPthLed(chompi_leds::kPthLoop,
                               play_modes.LatchEnabled() ? 40 : 0,
                               play_modes.LatchEnabled() ? 40 : 0,
                               0);
    }
    // The volume knob LED follows the page: master gradient, yellow
    // delay, or pale-ice reverb.
    if(vol_page_ == kVolPageDelay)
        chompi_leds::SetPthLed(chompi_leds::kPthVolume, 24, 20, 0);
    else if(vol_page_ == kVolPageReverb)
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

    // SD save feedback sits below live recorder status and SHIFT.
    const int feedback = transport_feedback.load(std::memory_order_acquire);
    if(!shift && now < sd_flash_until_)
        chompi_leds::SetPthLed(chompi_leds::kPthShift,
                               sd_flash_r_, sd_flash_g_, sd_flash_b_);
    /* Recording (and its close-out) blinks the SHIFT LED red at 2 Hz
     * until the take is fully written and the WAV header patched. */
    if(!shift && (sd_recorder.IsRecording() || sd_recorder.IsBusy()))
    {
        const bool on = (now / 250u) % 2u == 0;
        chompi_leds::SetPthLed(chompi_leds::kPthShift,
                               on ? 40 : 0, 0, 0);
    }
    if(!shift && feedback == kFeedbackRecorderError
       && static_cast<int32_t>(transport_feedback_until.load(
                                   std::memory_order_relaxed) - now) > 0)
    {
        const bool on = (now / 100u) % 2u == 0;
        chompi_leds::SetPthLed(chompi_leds::kPthShift,
                               on ? 48 : 0, 0, 0);
    }
    if(!shift && preset_mode_ != kPresetNone)
    {
        const bool animation = preset_mode_ == kPresetSaving
                               || preset_mode_ == kPresetCopying
                               || preset_mode_ == kPresetErasing;
        if(animation)
        {
            const uint8_t level = (now / 250u) % 2u == 0 ? 48 : 0;
            chompi_leds::SetPthLed(chompi_leds::kPthShift,
                                   level, level, level);
        }
        else if(preset_sel_slot_ >= 0
                && (preset_mode_ == kPresetSaveSel
                    || preset_mode_ == kPresetEraseSel
                    || preset_mode_ == kPresetCopyDest))
        {
            const uint8_t level = (now / 250u) % 2u == 0 ? 48 : 0;
            chompi_leds::SetPthLed(chompi_leds::kPthShift,
                                   level, 0, 0);
        }
    }
    if(shift)
        chompi_leds::SetPthLed(chompi_leds::kPthShift, 40, 40, 40);

    chompi_leds::Flush();
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

chompi::BowlsPreset SnapshotParams()
{
    chompi::BowlsPreset p;
    p.pitch_hz = params.pitch_hz;
    p.mode = static_cast<uint8_t>(ModeIdx(play_modes.Mode()));
    p.volume = static_cast<uint8_t>(
        params.volume_ticks.load(std::memory_order_relaxed));
    p.strike = static_cast<uint8_t>(
        params.strike_ticks.load(std::memory_order_relaxed));
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
            p.bright[slot] = static_cast<uint8_t>(
                params.brightness_ticks[m][w].load(
                    std::memory_order_relaxed));
            p.decay[slot] = static_cast<uint8_t>(
                params.decay_ticks[m][w].load(std::memory_order_relaxed));
        }
    p.reverb_mix = static_cast<uint8_t>(
        params.reverb_mix_ticks.load(std::memory_order_relaxed));
    p.delay_mix = static_cast<uint8_t>(
        params.delay_mix_ticks.load(std::memory_order_relaxed));
    p.delay_time = static_cast<uint8_t>(
        params.delay_time_ticks.load(std::memory_order_relaxed));
    return p;
}

void ApplyPreset(const chompi::BowlsPreset& p, bool boot)
{
    const PlayMode target_mode = static_cast<PlayMode>(p.mode);
    if(boot)
        play_modes.Init(target_mode);
    else if(play_modes.SetMode(target_mode))
    {
        midi_rub_level_[bowls::kDin] = 0.0f;
        midi_rub_level_[bowls::kUsb] = 0.0f;
    }
    params.pitch_hz = p.pitch_hz;
    params.volume_ticks.store(p.volume, std::memory_order_relaxed);
    params.strike_ticks.store(p.strike, std::memory_order_relaxed);
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
            params.brightness_ticks[m][w].store(p.bright[slot],
                                                std::memory_order_relaxed);
            params.decay_ticks[m][w].store(p.decay[slot],
                                           std::memory_order_relaxed);
        }
    params.reverb_mix_ticks.store(p.reverb_mix, std::memory_order_relaxed);
    params.delay_mix_ticks.store(p.delay_mix, std::memory_order_relaxed);
    params.delay_time_ticks.store(p.delay_time, std::memory_order_relaxed);
    for(int v = 0; v < kVoiceCount; ++v)
    {
        const int key = voices[v].Key();
        const int mode = voices[v].VoiceMode();
        if(!voices[v].IsActive() || key < 0 || key >= bowls::kKeyCount
           || mode < 0 || mode >= bowls::kPlayModeCount)
            continue;
        voices[v].SetBrightness(bowls::BrightnessFromTicks(
            params.brightness_ticks[mode][key].load(
                std::memory_order_relaxed)));
        voices[v].SetDecayMult(DecayMultTicks(
            params.decay_ticks[mode][key].load(std::memory_order_relaxed)));
        voices[v].SetPan(PanNormTicks(
            params.pan_ticks[mode][key].load(std::memory_order_relaxed)));
        voices[v].SetBowlGain(BowlGainTicks(
            params.bowl_vol_ticks[mode][key].load(
                std::memory_order_relaxed)));
    }
    ApplyDelayTargets();
    ApplyShimmerToVoices();
    params.overlay_until.store(0, std::memory_order_relaxed);
    SyncPadGates();
}

int PresetSlotForKey(int key)
{
    for(int slot = 0; slot < 15; ++slot)
        if(kWhiteKeyIndices[slot] == key)
            return slot;
    return -1;
}

void PresetEnterMode(int mode)
{
    preset_mode_ = mode;
    preset_sel_slot_ = -1;
    preset_copy_src_ = -1;
    params.overlay_until.store(0, std::memory_order_relaxed);
}

void PresetLoadSlot(int slot)
{
    if(!preset_bank.IsValid(slot))
        return;
    ApplyPreset(preset_bank.Get(slot), false);
    preset_current_ = slot;
}

void PresetShiftConfirm()
{
    if(preset_mode_ == kPresetNone || preset_storage.NoSdCard())
        return;

    switch(preset_mode_)
    {
        case kPresetSaveSel:
            if(preset_sel_slot_ >= 0)
            {
                preset_bank.Save(preset_sel_slot_, SnapshotParams());
                preset_storage.ClearIncompatibleSlot(preset_sel_slot_);
                preset_current_ = preset_sel_slot_;
                preset_mode_ = kPresetSaving;
                preset_anim_t_ = System::GetNow();
            }
            break;
        case kPresetEraseSel:
            if(preset_sel_slot_ >= 0
               && preset_bank.Erase(preset_sel_slot_))
            {
                preset_storage.ClearIncompatibleSlot(preset_sel_slot_);
                if(preset_current_ == preset_sel_slot_)
                    preset_current_ = -1;
                preset_mode_ = kPresetErasing;
                preset_anim_t_ = System::GetNow();
            }
            break;
        case kPresetCopyDest:
            if(preset_sel_slot_ >= 0
               && preset_bank.Copy(preset_copy_src_, preset_sel_slot_))
            {
                preset_storage.ClearIncompatibleSlot(preset_sel_slot_);
                preset_mode_ = kPresetCopying;
                preset_anim_t_ = System::GetNow();
            }
            break;
        default:
            break;
    }
}

/** Match the CHOMPI-PIPES preset gestures while keeping note releases
 *  for notes that began before a selection view was entered. */
bool PresetKeyEvent(int key, bool down, bool shift)
{
    const PlaybackUiState playback_state
        = playback_ui_state.load(std::memory_order_acquire);
    if(playback_state != PlaybackUiState::kNormal)
        return false;

    if(preset_mode_ != kPresetNone)
    {
        if(preset_storage.NoSdCard())
        {
            preset_mode_ = kPresetNone;
            preset_sel_slot_ = -1;
            preset_copy_src_ = -1;
            return true;
        }
        if(!down)
            return play_modes.DownMode(bowls::Source::kLocal, key) < 0;

        const int slot = PresetSlotForKey(key);
        if(slot >= 0)
        {
            switch(preset_mode_)
            {
                case kPresetSaveSel:
                    preset_sel_slot_ = slot;
                    break;
                case kPresetEraseSel:
                    if(preset_bank.IsValid(slot))
                        preset_sel_slot_ = slot;
                    break;
                case kPresetCopySrc:
                    if(preset_bank.IsValid(slot))
                    {
                        preset_copy_src_ = slot;
                        preset_sel_slot_ = -1;
                        preset_mode_ = kPresetCopyDest;
                    }
                    break;
                case kPresetCopyDest:
                    if(slot != preset_copy_src_)
                        preset_sel_slot_ = slot;
                    break;
                default:
                    break;
            }
            return true;
        }

        if((key == 18 && preset_mode_ == kPresetEraseSel)
           || (key == 20
               && (preset_mode_ == kPresetCopySrc
                   || preset_mode_ == kPresetCopyDest))
           || (key == 22 && preset_mode_ == kPresetSaveSel))
        {
            preset_mode_ = kPresetNone;
            preset_sel_slot_ = -1;
            preset_copy_src_ = -1;
        }
        return true;
    }

    if(!down || !shift)
        return false;

    const int slot = PresetSlotForKey(key);
    if(preset_storage.NoSdCard())
        return slot >= 0 || key == 18 || key == 20 || key == 22;

    if(key == 18)
        PresetEnterMode(kPresetEraseSel);
    else if(key == 20)
        PresetEnterMode(kPresetCopySrc);
    else if(key == 22)
        PresetEnterMode(kPresetSaveSel);
    else if(slot >= 0)
        PresetLoadSlot(slot);
    else
        return false;
    return true;
}

/** Boot-only SD status signal on the SHIFT LED (storage has no panel
 *  LED by request - this runs once, before audio starts): `count`
 *  short pulses of the given colour. Blocking; polled-DMA Flush. */
void BootBlink(int count, uint8_t r, uint8_t g, uint8_t b)
{
    for(int i = 0; i < count; ++i)
    {
        chompi_leds::SetPthLed(chompi_leds::kPthShift, r, g, b);
        chompi_leds::Flush();
        System::Delay(180);
        chompi_leds::ClearAll();
        chompi_leds::Flush();
        System::Delay(180);
    }
}

} // namespace

int main()
{
    hw.Init();
    // MIDI in/out: DIN (UART RX/TX D13/D14) + the panel USB-C device
    // port, channel 1 (plans/midi-output-to-daw.md).
    // Initialized first, right after the hardware - the same boot order
    // as the known-good CHOMPI-PIPES firmware. USB MIDI owns the shared
    // USB device-mode descriptor, so the USB serial diagnostics are
    // compiled out of MIDI builds (BOWLS_LOG); CPU_METER builds keep
    // the serial log and skip USB MIDI instead.
    midi.Init(kUsbMidiEnable);
    hw.ConfigureUsbDevicePath();
    bbd_delay.Init(kSampleRate, delay_buffer_l, delay_buffer_r);
    ApplyDelayTargets();
    chompi_leds::Init();
    chompi_leds::ClearAll();

    for(int v = 0; v < kVoiceCount; ++v)
        voices[v].Init(kSampleRate);
    ApplyShimmerToVoices();

    // Temple-room reverb: a small wooden room, not a cathedral.
    reverb.Init(kSampleRate);
    reverb.SetFeedback(0.82f);   // short-to-medium tail
    reverb.SetLpFreq(4500.0f);   // wood damps the highs

    // Presets: load the embedded factory state and cache the 15 saved
    // slots. A populated slot is loaded only by an explicit user action.
    preset_bank.Init();
    preset_storage.Init();
    chompi::BowlsPreset preset;
    const bool factory_loaded = preset_storage.LoadFactoryDefaults(preset);
    if(!factory_loaded)
    {
        BOWLS_LOG("SD: embedded factory preset is invalid");
        preset.SetDefaults();
    }
    ApplyPreset(preset, true);
    preset_storage.LoadAll(preset_bank);

    if(preset_storage.NoSdCard())
    {
        BOWLS_LOG("SD: no card at boot - presets unavailable");
        BootBlink(3, 48, 0, 0);
    }
    else if(!factory_loaded)
    {
        BOWLS_LOG("SD: factory preset load failed");
        BootBlink(3, 40, 24, 0);
    }
    else
    {
        BOWLS_LOG("SD: factory preset active; saved slots ready");
        BootBlink(1, 0, 40, 0);
    }

    // Show the volume bar briefly at boot.
    params.overlay.store(static_cast<int>(Overlay::Volume),
                         std::memory_order_relaxed);
    params.overlay_until.store(System::GetNow() + kOverlayMs,
                               std::memory_order_relaxed);

    // Prime the debouncers before audio starts so a transport key held
    // through boot cannot appear as a fresh gesture.
    for(int i = 0; i < 10; ++i)
    {
        hw.ProcessControls();
        System::Delay(1);
    }
    transport_wait_for_release_ = hw.PlayPressed() || hw.LoopPressed();

    hw.StartAudio(AudioCallback);

    while(true)
    {
        /* MIDI transport service, thread level only: UART error
         * recovery, USB listening and the parser drain into the fixed
         * per-source event queues (the audio ISR just pops queues). */
        midi.Service();

        /* CC 120 deferred reverb clear: the panic fade has muted the
         * bowl bus and the ISR skips reverb.Process while this runs,
         * so the ~387 KB SDRAM state clear never races the renderer;
         * the mute lifts only after the clear completes. */
        if(reverb_reset_pending_)
        {
            reverb.Init(kSampleRate);
            reverb.SetFeedback(0.82f);
            reverb.SetLpFreq(4500.0f);
            reverb_reset_pending_ = false;
            panic_mute_           = false;
        }

        /* Staged, non-blocking SD-card work: one small FatFS step per
         * pass, at thread level only (never in the audio ISR). */
        preset_storage.Service(preset_bank);
        sd_recorder.Service(preset_storage, System::GetNow());
        const uint32_t now = System::GetNow();
        if((preset_mode_ == kPresetSaving || preset_mode_ == kPresetCopying
            || preset_mode_ == kPresetErasing)
           && now - preset_anim_t_ >= 1000)
        {
            preset_mode_ = kPresetNone;
            preset_sel_slot_ = -1;
            preset_copy_src_ = -1;
        }
        else if(preset_mode_ != kPresetNone && preset_storage.NoSdCard())
        {
            preset_mode_ = kPresetNone;
            preset_sel_slot_ = -1;
            preset_copy_src_ = -1;
        }
        if(playback_entry_pending.load(std::memory_order_acquire)
           && !sd_recorder.IsBusy())
        {
            playback_entry_pending.store(false, std::memory_order_release);
            sd_player.RequestEnter();
        }
        sd_player.Service(preset_storage);

        if(sd_recorder.ConsumeErrorEvent())
        {
            BOWLS_LOG("SD: recording failed");
            SetTransportFeedback(kFeedbackRecorderError, 2000);
        }
        int player_event_slot = -1;
        const auto player_event = sd_player.ConsumeEvent(player_event_slot);
        if(player_event == chompi::SdPlayer<kSdRingFrames>::kEventEmpty)
        {
            BOWLS_LOG("SD: no recordings found");
            SetTransportFeedback(kFeedbackNoTakes, 1000);
        }
        else if(player_event
                == chompi::SdPlayer<kSdRingFrames>::kEventInvalidTake)
        {
            SetTransportFeedback(kFeedbackInvalidTake, 500,
                                 player_event_slot);
        }
        else if(player_event
                == chompi::SdPlayer<kSdRingFrames>::kEventFatal)
        {
            BOWLS_LOG("SD: playback failed");
            SetTransportFeedback(kFeedbackPlayerFatal, 2000);
        }

        /* One diagnostic and SHIFT-LED flash per preset write
         * failure/recovery/completion. */
        const auto storage_event = preset_storage.ConsumeStatusEvent();
        if(storage_event == chompi::PresetStorage::kStatusFailed)
        {
            BOWLS_LOG("SD: preset write failed - slot kept in RAM, retrying");
            sd_flash_r_     = 48;
            sd_flash_g_     = 0;
            sd_flash_b_     = 0;
            sd_flash_until_ = System::GetNow() + 1000;
        }
        else if(storage_event == chompi::PresetStorage::kStatusRecovered)
        {
            BOWLS_LOG("SD: save recovered");
            sd_flash_r_     = 0;
            sd_flash_g_     = 40;
            sd_flash_b_     = 0;
            sd_flash_until_ = System::GetNow() + 300;
        }
        else if(storage_event == chompi::PresetStorage::kStatusSaved)
        {
            BOWLS_LOG("SD: preset saved");
            sd_flash_r_     = 0;
            sd_flash_g_     = 40;
            sd_flash_b_     = 0;
            sd_flash_until_ = System::GetNow() + 300;
        }

        /* Card-presence edge: a card detected at boot that later reads
         * as gone (or comes back) flashes the SHIFT LED and logs. */
        static bool sd_was_present = !preset_storage.NoSdCard();
        const bool  sd_present     = !preset_storage.NoSdCard();
        if(sd_was_present && !sd_present)
        {
            BOWLS_LOG("SD: card LOST at runtime");
            sd_flash_r_     = 32;
            sd_flash_g_     = 0;
            sd_flash_b_     = 40;
            sd_flash_until_ = System::GetNow() + 1000;
        }
        else if(!sd_was_present && sd_present)
        {
            BOWLS_LOG("SD: card detected");
        }
        sd_was_present = sd_present;

        RenderLeds();
    }
}