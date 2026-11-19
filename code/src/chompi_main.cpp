// CHOMPI-BOWLS firmware entry point. Connects the hardware, voice
// engine, play modes, presets, MIDI, and SD recording/playback.

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
#include "PresetTransfer.h"
#include "SdRecorder.h"
#include "SdPlayer.h"
#include "MidiCcMap.h"
#include "MidiHandler.h"
#include "midi_clock.h"
#include "bowl_reverb.h"

#include "Utility/dsp.h"

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
 *  (the LED diagnostics stay). Diagnostic builds keep the serial log
 *  and skip USB MIDI instead (DIN MIDI still works there). */
#ifdef AUDIO_DEADLINE_DIAG
#define BOWLS_LOG(...) \
    daisy::Logger<daisy::LOGGER_EXTERNAL>::PrintLine(__VA_ARGS__)
#elif defined(CPU_METER)
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
chompi::PresetTransfer preset_transfer;
chompi::sd_audio::StereoRing<kSdRingFrames> DSY_SDRAM_BSS record_ring;
chompi::sd_audio::StereoRing<kSdRingFrames> DSY_SDRAM_BSS playback_ring;
chompi::SdRecorder<kSdRingFrames> sd_recorder(record_ring);
chompi::SdPlayer<kSdRingFrames>   sd_player(playback_ring);

constexpr int  kVoiceCount = 6; // shared pool, quietest-voice steal
bowls::BowlVoice voices[kVoiceCount];

// Stereo, modulated 1970s-style room reverb on the bowl bus.
bowls::BowlReverb DSY_SDRAM_BSS reverb;

#ifdef CPU_METER
// CPU meter with a ~5 s peak-hold: dropouts come from the WORST block,
// not the average, so the peak is what matters. The Shift LED shows it
// (green -> red) and it is printed over USB serial when logging is on.
CpuLoadMeter g_cpu_meter;
float        g_cpu_peak          = 0.0f;
uint32_t     g_steal_flash_until = 0; // encoder-4 LED steal witness
#endif

// ---------------------------------------------------------------------------
// Parameter state
// ---------------------------------------------------------------------------

constexpr int kTickCount = 32; // 0..32 -> 33 positions

using bowls::PlayMode;

int ModeIdx(PlayMode mode) { return static_cast<int>(mode); }

// The play-mode state machine: mode, shortcuts, per-key press
// provenance and the Rimming LOOP latch (play_modes.h). Boot default
// is Split; a valid preset restores the saved mode at boot.
bowls::PlayModes play_modes;
int strike_arpeggio_rate_tenths_hz
    = bowls::kStrikeArpeggioDefaultRateTenthsHz;
int strike_arpeggio_clock_divider_ticks
    = bowls::StrikeArpeggioClockDividerTicks(
        bowls::kStrikeArpeggioDefaultRateTenthsHz);
constexpr uint32_t kStrikeRetriggerFlashMs = 80;
uint32_t strike_retrigger_flash_until_[bowls::kKeyCount] = {};
bool midi_clock_active_ = false;
int midi_clock_source_ = -1;
float midi_clock_pulse_period_ms_ = 0.0f;

enum class Overlay : int
{
    None = 0,
    Volume,
    Strike,
    Brightness,
    Beat,
    Decay,
    Shimmer,
    Character,
    Pan,
    ReverbMix,
    ReverbDecay,
    BowlVolume,
    Tune,
};

// White-key bars disappear 500 ms after turning.
constexpr uint32_t kOverlayMs = 500;
constexpr int kDefaultMasterVolume = 8;

uint32_t PitchToMilliHz(float pitch_hz)
{
    return static_cast<uint32_t>(
        std::lround(static_cast<double>(pitch_hz) * 1000.0));
}

float MilliHzToPitch(uint32_t pitch_millihz)
{
    return static_cast<float>(pitch_millihz) * 0.001f;
}

struct Params
{
    Params()
    {
        const bowls::PitchTables& factory = bowls::FactoryPitchTables();
        for(int m = 0; m < bowls::kPlayModeCount; ++m)
            for(int w = 0; w < bowls::kKeyCount; ++w)
            {
                pitch_millihz[m][w].store(
                    PitchToMilliHz(bowls::FundamentalHz(
                        static_cast<PlayMode>(m), w, factory)),
                    std::memory_order_relaxed);
                pan_ticks[m][w].store(chompi::BowlsPreset::kDefaultPan,
                                      std::memory_order_relaxed);
                bowl_vol_ticks[m][w].store(
                    chompi::BowlsPreset::kDefaultBowlVol,
                    std::memory_order_relaxed);
                profile_ticks[m][w].store(
                    chompi::BowlsPreset::kDefaultProfile,
                    std::memory_order_relaxed);
                brightness_ticks[m][w].store(
                    chompi::BowlsPreset::kDefaultBrightness,
                    std::memory_order_relaxed);
                decay_ticks[m][w].store(chompi::BowlsPreset::kDefaultDecay,
                                        std::memory_order_relaxed);
                beat_ticks[m][w].store(chompi::BowlsPreset::kDefaultBeat,
                                       std::memory_order_relaxed);
            }
    }

    // Global controls (shared by all modes).
    std::atomic<int>      volume_ticks{kDefaultMasterVolume};
    std::atomic<int>      strike_ticks{chompi::BowlsPreset::kDefaultStrike};
    std::atomic<int>      shimmer_ticks{chompi::BowlsPreset::kDefaultShimmer};

    // Per-mode/per-key banks [mode][key]: pan (16 = centre), bowl
    // volume (16 = unity), direct fundamental pitch in millihertz,
    // resonance character (profile 0..4), brightness and decay are
    // independent for each physical key in each mode. Encoders 1-3 edit
    // the current mode's bank for the selected (last played) key.
    std::atomic<int>      pan_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<int>      bowl_vol_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<uint32_t> pitch_millihz[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<int>      profile_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<int>      brightness_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<int>      decay_ticks[bowls::kPlayModeCount][bowls::kKeyCount];
    std::atomic<int>      beat_ticks[bowls::kPlayModeCount][bowls::kKeyCount];

    // Volume knob page 2: room reverb mix and SHIFT-decay.
    std::atomic<int>      reverb_mix_ticks{
        chompi::BowlsPreset::kDefaultReverbMix};
    std::atomic<int>      reverb_decay_ticks{
        chompi::BowlsPreset::kDefaultReverbDecay};

    // UI overlay: which parameter bar is shown on the white keys.
    std::atomic<int>      overlay{static_cast<int>(Overlay::Volume)};
    std::atomic<uint32_t> overlay_until{0};
    std::atomic<bool>     tune_fine{false};
    std::atomic<uint32_t> tune_fine_reference_millihz{0};
    std::atomic<int>      tune_fine_reference_mode{-1};
    std::atomic<int>      tune_fine_reference_key{-1};
};

Params params;

// Volume-knob pages (press cycles): master volume -> reverb.
constexpr int kVolPageVolume = 0;
constexpr int kVolPageReverb = 1;
volatile int  vol_page_      = kVolPageVolume;

// Audio routing selection (audio_routing.h): Normal (boot default) or
// the external send-FX loop. Session-only - never saved to the SD
// preset, so a fresh boot always starts in Normal monitoring.
volatile int routing_ = bowls::kRoutingDefault;

// ---------------------------------------------------------------------------
// MIDI input.
// ---------------------------------------------------------------------------

// DIN (UART) + USB MIDI device, fixed channel 1. Input parsers and output
// transmitters are serviced in the main loop; the audio callback drains
// bounded input queues and publishes local key notes to output queues.
bowls::BowlsMidi midi;
bowls::MidiClockTracker midi_clock_trackers[bowls::kMidiTransportCount];

void UpdateMidiClock(uint32_t now)
{
    for(int src = 0; src < bowls::kMidiTransportCount; ++src)
        midi_clock_trackers[src].Update(midi.ClockPulses(src), now);

    int source = -1;
    if(midi_clock_trackers[bowls::kDin].HasTempo(now))
        source = bowls::kDin;
    else if(midi_clock_trackers[bowls::kUsb].HasTempo(now))
        source = bowls::kUsb;

    const bool active = source >= 0;
    const float pulse_period_ms
        = active ? midi_clock_trackers[source].PulsePeriodMs() : 0.0f;
    if(active != midi_clock_active_ || source != midi_clock_source_
       || (active && pulse_period_ms != midi_clock_pulse_period_ms_))
    {
        midi_clock_active_ = active;
        midi_clock_source_ = source;
        midi_clock_pulse_period_ms_ = pulse_period_ms;
    }
}

#if defined(CPU_METER) || defined(AUDIO_DEADLINE_DIAG)
constexpr bool kUsbMidiEnable = false; // diagnostic build: keep USB serial
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

// Encoder-1 pages (press cycles): bowl volume -> pan -> tuning.
constexpr int kEnc1PageVolume    = 0;
constexpr int kEnc1PagePan       = 1;
constexpr int kEnc1PageTune      = 2;
volatile int  enc1_page_         = kEnc1PageVolume;
uint32_t      fine_tune_key_play_until_ = 0;

// Encoder-2 pages: selected-bowl brightness -> selected-bowl beating.
constexpr int kEnc2PageBrightness = 0;
constexpr int kEnc2PageBeat       = 1;
volatile int  enc2_page_          = kEnc2PageBrightness;

// Encoder-3 pages: selected-bowl decay -> resonance character.
constexpr int kEnc3PageDecay     = 0;
constexpr int kEnc3PageCharacter = 1;
volatile int  enc3_page_         = kEnc3PageDecay;

// Encoder-4 pages (press cycles): global strike hardness -> global shimmer.
constexpr int kEnc4PageStrike  = 0;
constexpr int kEnc4PageShimmer = 1;
volatile int  enc4_page_       = kEnc4PageStrike;

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
volatile uint32_t preset_copy_error_until_ = 0;

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
bool PresetKeyEvent(int key, bool down, bool shift, bool fine_tune_key_play);

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
    }
}

void ShowOverlay(Overlay overlay, uint32_t now)
{
    params.overlay.store(static_cast<int>(overlay), std::memory_order_relaxed);
    params.overlay_until.store(now + kOverlayMs, std::memory_order_relaxed);
}

void ShowTuneOverlay(bool fine, uint32_t now)
{
    params.tune_fine.store(fine, std::memory_order_relaxed);
    fine_tune_key_play_until_ = fine ? now + kOverlayMs : now;
    if(!fine)
    {
        params.tune_fine_reference_mode.store(-1, std::memory_order_relaxed);
        params.tune_fine_reference_key.store(-1, std::memory_order_relaxed);
    }
    ShowOverlay(Overlay::Tune, now);
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

float BeatMultTicks(int ticks)
{
    return 0.5f * TicksToNorm(ticks); // additional 0..0.5x detune
}

float BowlDetuneMult(int mode, int key)
{
    const int shimmer_ticks
        = params.shimmer_ticks.load(std::memory_order_relaxed);
    const int beat_ticks
        = params.beat_ticks[mode][key].load(std::memory_order_relaxed);
    return ShimmerMultTicks(shimmer_ticks) + BeatMultTicks(beat_ticks);
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

void SetBeatTicks(int m, int key, int ticks, uint32_t now)
{
    if(!StoreTicks(params.beat_ticks[m][key], ticks, now))
        return;
    const float mult = BowlDetuneMult(m, key);
    ForVoiceIdentity(m, key, [&](int v) { voices[v].SetShimmerMult(mult); });
}

void SetPitchHz(int m, int key, int detents, bool fine)
{
    if(m < 0 || m >= bowls::kPlayModeCount || key < 0
       || key >= bowls::kKeyCount)
        return;
    const float current = MilliHzToPitch(
        params.pitch_millihz[m][key].load(std::memory_order_relaxed));
    const float updated = bowls::AdjustPitchHz(current, detents, fine);
    params.pitch_millihz[m][key].store(PitchToMilliHz(updated),
                                       std::memory_order_relaxed);
}

void BeginFineTune(int m, int key)
{
    if(params.tune_fine_reference_mode.load(std::memory_order_relaxed) == m
       && params.tune_fine_reference_key.load(std::memory_order_relaxed)
              == key)
        return;

    params.tune_fine.store(false, std::memory_order_relaxed);
    params.tune_fine_reference_millihz.store(
        params.pitch_millihz[m][key].load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    params.tune_fine_reference_mode.store(m, std::memory_order_relaxed);
    params.tune_fine_reference_key.store(key, std::memory_order_relaxed);
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
    for(int v = 0; v < kVoiceCount; ++v)
    {
        const int mode = voices[v].VoiceMode();
        const int key = voices[v].Key();
        const float mult
            = mode >= 0 && mode < bowls::kPlayModeCount
                      && key >= 0 && key < bowls::kKeyCount
                  ? BowlDetuneMult(mode, key)
                  : ShimmerMultTicks(
                        params.shimmer_ticks.load(std::memory_order_relaxed));
        voices[v].SetShimmerMult(mult);
    }
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
    voices[v].SetDecayMultForNextExcitation(DecayMultTicks(
        params.decay_ticks[m][key].load(std::memory_order_relaxed)));
    voices[v].SetPan(PanNormTicks(
        params.pan_ticks[m][key].load(std::memory_order_relaxed)));
    voices[v].SetBowlGain(BowlGainTicks(
        params.bowl_vol_ticks[m][key].load(std::memory_order_relaxed)));
}

/** Direct fundamental frequency for (mode, key). */
float KeyFundamental(PlayMode mode, int key)
{
    const int m = ModeIdx(mode);
    if(m < 0 || m >= bowls::kPlayModeCount || key < 0
       || key >= bowls::kKeyCount)
        return 0.0f;
    return MilliHzToPitch(
        params.pitch_millihz[m][key].load(std::memory_order_relaxed));
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
    voices[v].SetShimmerMultForNextExcitation(
        BowlDetuneMult(ModeIdx(mode), key));
    voices[v].SetKey(key);
    voices[v].SetVoiceMode(ModeIdx(mode));
    // A strike takes the voice over: any pad/rim performance gain left
    // over from the voice's previous life returns to unity (the strike
    // velocity already shaped the excitation itself).
    voices[v].SetPerfGain(1.0f);
    voices[v].StrikeWithAmplitude(KeyFundamental(mode, key), strike.hardness,
                                  strike.amplitude, 1.0f, 1.0f,
                                  stolen ? 0.02f : 0.25f);
    reverb.DampenImpact();
#ifdef CPU_METER
    if(stolen)
        g_steal_flash_until = System::GetNow() + 300;
#endif
    // MIDI notes never move the LOCAL bowl-editor target (the CC 81/82
    // edit context is kept per sender instead).
    if(edit_target)
        last_key_[ModeIdx(mode)] = key;
}

/** Retrigger latched Strikes using the selected cadence. The duration
 *  mode follows each bowl's decay; arpeggio modes use the wheel-selected
 *  interval. */
void RetriggerLatchedStrikes()
{
    static bool arpeggio_active = false;
    static bowls::StrikeArpeggioMode previous_mode
        = bowls::StrikeArpeggioMode::kDurationLoop;
    static uint32_t previous_mask = 0;
    static uint32_t next_strike_at = 0;
    static int sequence_position = 0;
    if(!play_modes.StrikeLatchEnabled())
    {
        arpeggio_active = false;
        return;
    }

    const bowls::StrikeArpeggioMode arpeggio_mode
        = play_modes.GetStrikeArpeggioMode();
    if(arpeggio_mode != bowls::StrikeArpeggioMode::kDurationLoop)
    {
        int keys[bowls::kStrikeArpeggioMaxSequenceLength];
        const int count = play_modes.GetStrikeArpeggioOrder(keys);
        uint32_t mask = 0;
        for(int i = 0; i < count; ++i)
            mask |= uint32_t(1) << keys[i];
        if(count == 0)
        {
            arpeggio_active = false;
            return;
        }

        const uint32_t now = System::GetNow();
        if(!arpeggio_active || previous_mode != arpeggio_mode
           || previous_mask != mask)
        {
            arpeggio_active = true;
            previous_mode = arpeggio_mode;
            previous_mask = mask;
            sequence_position = 0;
            next_strike_at = now;
        }
        if(static_cast<int32_t>(now - next_strike_at) < 0)
            return;

        const int key = keys[sequence_position];
        const int velocity = play_modes.StrikeLatchVelocity(key);
        StrikeKey(PlayMode::kStrikes, key, velocity > 0 ? velocity : 127,
                  false, play_modes.StrikeLatchUsesMidiVelocity(key));
        strike_retrigger_flash_until_[key] = now + kStrikeRetriggerFlashMs;
        sequence_position = (sequence_position + 1) % count;
        const uint32_t interval_ms
            = midi_clock_active_
                  ? bowls::StrikeArpeggioClockIntervalMs(
                        midi_clock_pulse_period_ms_,
                        strike_arpeggio_clock_divider_ticks)
                  : bowls::StrikeArpeggioIntervalMs(
                        strike_arpeggio_rate_tenths_hz);
        next_strike_at = now + interval_ms;
        return;
    }

    arpeggio_active = false;
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
        strike_retrigger_flash_until_[key]
            = System::GetNow() + kStrikeRetriggerFlashMs;
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
    voices[v].SetShimmerMultForNextExcitation(
        BowlDetuneMult(ModeIdx(mode), key));
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
    voices[v].SetShimmerMultForNextExcitation(
        BowlDetuneMult(ModeIdx(mode), key));
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
volatile bool  rub_display_clean_ = false;

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

        if(hw.PlayPressed() && !hw.LoopPressed() && !hw.ShiftHeld()
           && play_hold_valid_
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
        {
            if(hw.ShiftHeld())
                play_modes.CycleStrikeArpeggioMode();
            else
                play_modes.PlayPressed();
        }
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
    const uint32_t now = System::GetNow();
    UpdateMidiClock(now);
    // Pad gate reconciliation runs even while the playback transport UI
    // owns this function (HandleTransport early-returns then): MIDI
    // note-offs must never leave a stuck pad gate.
    SyncPadGates();
    const bool shift = hw.ShiftHeld();

    // Keep the master-volume knob available while the transport owns the UI.
    if(hw.VolumeClicked())
    {
        vol_page_ = (vol_page_ + 1) % 2;
        ShowOverlay(vol_page_ == kVolPageVolume
                        ? Overlay::Volume
                        : Overlay::ReverbMix,
                    now);
    }

    const int volume_inc = hw.EncoderIncrement(5);
    if(volume_inc != 0)
    {
        if(vol_page_ == kVolPageReverb)
        {
            if(shift)
                BumpTicks(params.reverb_decay_ticks, volume_inc,
                          Overlay::ReverbDecay, now);
            else
                BumpTicks(params.reverb_mix_ticks, volume_inc,
                          Overlay::ReverbMix, now);
        }
        else if(!shift)
            BumpTicks(params.volume_ticks, volume_inc, Overlay::Volume, now);
    }

    if(HandleTransport())
        return;
    RetriggerLatchedStrikes();
    static bool prev_shift_held = false;
    if(shift && !prev_shift_held)
        PresetShiftConfirm();
    prev_shift_held = shift;

    // Physical encoder 1 press: cycle page 1 bowl volume -> page 2 pan
    // -> page 3 direct-Hz frequency. The newly active page's bar shows
    // as confirmation.
    if(hw.EncoderClicked(0))
    {
        enc1_page_ = (enc1_page_ + 1) % 3;
        if(enc1_page_ == kEnc1PageTune)
            ShowTuneOverlay(false, now);
        else
            ShowOverlay(enc1_page_ == kEnc1PageVolume ? Overlay::BowlVolume
                                                      : Overlay::Pan,
                        now);
    }

    if(hw.EncoderClicked(1))
    {
        enc2_page_ = enc2_page_ == kEnc2PageBrightness
                         ? kEnc2PageBeat
                         : kEnc2PageBrightness;
        ShowOverlay(enc2_page_ == kEnc2PageBrightness ? Overlay::Brightness
                                                       : Overlay::Beat,
                    now);
    }

    if(hw.EncoderClicked(2))
    {
        enc3_page_ = enc3_page_ == kEnc3PageDecay
                         ? kEnc3PageCharacter
                         : kEnc3PageDecay;
        ShowOverlay(enc3_page_ == kEnc3PageDecay ? Overlay::Decay
                                                  : Overlay::Character,
                    now);
    }

    // Physical encoder 1: page 1 edits the selected bowl's volume,
    // page 2 its pan, and page 3 its direct-Hz frequency.
    const int enc1_inc = hw.EncoderIncrement(0);
    if(enc1_inc != 0)
    {
        const int cur_mode = ModeIdx(play_modes.Mode());
        const int key      = last_key_[cur_mode];
        if(enc1_page_ == kEnc1PageVolume && !shift)
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
        else if(enc1_page_ == kEnc1PagePan)
        {
            // Pan the selected bowl one tick per detent; matching voices
            // are re-panned live while other modes keep their own pan.
            SetPanTicks(cur_mode, key,
                        params.pan_ticks[cur_mode][key].load(
                                std::memory_order_relaxed)
                                + enc1_inc,
                        now);
            ShowOverlay(Overlay::Pan, now); // also shows at the end stops
        }
        else if(enc1_page_ == kEnc1PageTune)
        {
            // Direct fundamental pitch applies at the next excitation.
            if(shift)
                BeginFineTune(cur_mode, key);
            SetPitchHz(cur_mode, key, enc1_inc, shift);
            ShowTuneOverlay(shift, now);
        }
    }

    // Physical encoder 2: selected-bowl brightness or beating amount.
    const int enc2_inc = hw.EncoderIncrement(1);
    if(enc2_page_ == kEnc2PageBrightness && enc2_inc != 0)
    {
        const int cur_mode = ModeIdx(play_modes.Mode());
        const int key      = last_key_[cur_mode];
        SetBrightnessTicks(cur_mode, key,
                           params.brightness_ticks[cur_mode][key].load(
                               std::memory_order_relaxed)
                               + enc2_inc,
                           now);
        ShowOverlay(Overlay::Brightness, now);
    }
    else if(enc2_page_ == kEnc2PageBeat && enc2_inc != 0)
    {
        const int cur_mode = ModeIdx(play_modes.Mode());
        const int key      = last_key_[cur_mode];
        SetBeatTicks(cur_mode, key,
                     params.beat_ticks[cur_mode][key].load(
                         std::memory_order_relaxed)
                         + enc2_inc,
                     now);
        ShowOverlay(Overlay::Beat, now);
    }

    // Physical encoder 4: press to select page 1 strike hardness or
    // page 2 shimmer; both controls are global.
    if(hw.EncoderClicked(3))
    {
        enc4_page_ = enc4_page_ == kEnc4PageStrike
                         ? kEnc4PageShimmer
                         : kEnc4PageStrike;
        ShowOverlay(enc4_page_ == kEnc4PageStrike ? Overlay::Strike
                                                  : Overlay::Shimmer,
                    now);
    }

    const int enc4_inc = hw.EncoderIncrement(3);
    if(enc4_page_ == kEnc4PageStrike)
    {
        BumpTicks(params.strike_ticks, enc4_inc, Overlay::Strike, now);
    }

    // Physical encoder 3: page 1 adjusts selected-bowl decay; page 2
    // selects its resonance character.
    const int enc3_inc = hw.EncoderIncrement(2);
    if(enc3_inc != 0)
    {
        const int cur_mode = ModeIdx(play_modes.Mode());
        const int key      = last_key_[cur_mode];
        if(enc3_page_ == kEnc3PageDecay)
        {
            SetDecayTicks(cur_mode, key,
                          params.decay_ticks[cur_mode][key].load(
                              std::memory_order_relaxed)
                              + enc3_inc,
                          now);
            ShowOverlay(Overlay::Decay, now); // also at the end stops
        }
        else if(!shift)
        {
            // Two detent ticks per profile step; applies at the next
            // strike/selection, like tuning.
            const int step = SectionStep(profile_sel_acc, enc3_inc);
            SetProfileTicks(cur_mode, key,
                            params.profile_ticks[cur_mode][key].load(
                                std::memory_order_relaxed)
                                + step,
                            now);
            ShowOverlay(Overlay::Character, now);
        }
    }

    if(enc4_page_ == kEnc4PageShimmer && enc4_inc != 0)
    {
        BumpTicks(params.shimmer_ticks, enc4_inc, Overlay::Shimmer, now);
        ApplyShimmerToVoices();
    }

    // Transport wheel: in a latched Strike arpeggio, adjust its rate
    // only while there are latched notes. The duration-loop latch stays
    // tail-driven; all other wheel turns retain their rimming behavior.
    const int wheel_inc = hw.EncoderIncrement(4);
    if(wheel_inc != 0)
    {
        const PlayMode mode = play_modes.Mode();
        const bowls::StrikeArpeggioMode arpeggio_mode
            = play_modes.GetStrikeArpeggioMode();
        bool has_latched_strike = false;
        if(mode == PlayMode::kStrikes && play_modes.StrikeLatchEnabled()
           && arpeggio_mode != bowls::StrikeArpeggioMode::kDurationLoop)
            for(int key = 0; key < bowls::kKeyCount; ++key)
                if(play_modes.StrikeLatched(key))
                {
                    has_latched_strike = true;
                    break;
                }

        if(has_latched_strike)
        {
            if(midi_clock_active_)
            {
                strike_arpeggio_clock_divider_ticks
                    = bowls::AdjustStrikeArpeggioClockDivider(
                        strike_arpeggio_clock_divider_ticks, wheel_inc);
                strike_arpeggio_rate_tenths_hz
                    = bowls::StrikeArpeggioRateFromClockDivider(
                        strike_arpeggio_clock_divider_ticks);
            }
            else
            {
                strike_arpeggio_rate_tenths_hz
                    = bowls::AdjustStrikeArpeggioRateTenthsHz(
                        strike_arpeggio_rate_tenths_hz, wheel_inc);
                strike_arpeggio_clock_divider_ticks
                    = bowls::StrikeArpeggioClockDividerTicks(
                        strike_arpeggio_rate_tenths_hz);
            }
        }
        else
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
            const bool clean    = wheel_inc < 0;
            const int  cur_mode = ModeIdx(mode);
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
        bool      clean    = false;
        for(int k = 0; k < bowls::kKeyCount; ++k)
        {
            const bowls::RubCandidate candidate = EffectiveRub(cur_mode, k);
            const float lvl = candidate.level;
            if(lvl > d)
            {
                d = lvl;
                clean = candidate.clean;
            }
        }
        rub_display_ = d;
        rub_display_clean_ = clean;
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
    const bool fine_tune_key_play
        = shift && enc1_page_ == kEnc1PageTune
          && static_cast<int32_t>(fine_tune_key_play_until_ - now) > 0;
    for(int key = 0; key < bowls::kKeyCount; ++key)
    {
        if(hw.KeyRising(key))
        {
            if(PresetKeyEvent(key, true, shift, fine_tune_key_play))
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
            if(PresetKeyEvent(key, false, shift, fine_tune_key_play))
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
    if(!rev_reset)
        reverb.SetDecayTicks(
            params.reverb_decay_ticks.load(std::memory_order_relaxed));
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
        // The filtered, pre-delayed room return is mixed before master gain.
        float wet_l = 0.0f, wet_r = 0.0f;
        if(!rev_reset)
            reverb.Process(mix_l[i], mix_r[i], &wet_l, &wet_r);
        const float dry   = 1.0f - mix_smooth;
        bus_l[i] = (mix_l[i] * dry + wet_l * mix_smooth) * panic_gain_;
        bus_r[i] = (mix_r[i] * dry + wet_r * mix_smooth) * panic_gain_;
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

/** Pan bar grows from white key 8; the centre detent is green, otherwise
 *  the bar is cyan and extends toward the selected pan direction. */
void DrawPanBar(int ticks, uint8_t red, uint8_t green, uint8_t blue)
{
    if(ticks == chompi::BowlsPreset::kDefaultPan)
    {
        chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[7], 0, 48, 0);
        return;
    }

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

/** Logarithmic 40-10,000 Hz frequency position across the white keys. */
void DrawPitchBar(float pitch_hz)
{
    const int lit = bowls::PitchLedIndex(pitch_hz, 15) + 1;
    for(int white = 0; white < lit; ++white)
        chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[white],
                               30, 0, 48);
}

/** Fine tuning shows Hz steps from the pitch captured at the first turn. */
void DrawFinePitchBar(uint32_t pitch_millihz, uint32_t reference_millihz)
{
    const int32_t delta_millihz
        = static_cast<int32_t>(pitch_millihz)
          - static_cast<int32_t>(reference_millihz);
    const int offset_hz
        = delta_millihz < 0 ? -((-delta_millihz + 500) / 1000)
                            : (delta_millihz + 500) / 1000;
    const int magnitude = offset_hz < 0 ? -offset_hz : offset_hz;
    const int span      = magnitude > 7 ? 7 : magnitude;

    for(int step = 1; step <= span; ++step)
    {
        const int white = offset_hz < 0 ? 7 - step : 7 + step;
        chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[white], 0, 40, 40);
    }
    chompi_leds::SetSmtLed(chompi_leds::kWhiteKeyLeds[7], 0, 48, 0);
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
    // Per-bowl overlays use the current mode's selected (last played)
    // key.
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
                // Yellow bar, matching encoder 4 page 1.
                DrawBar(params.strike_ticks.load(std::memory_order_relaxed),
                        48, 26, 0);
                break;
            case Overlay::Brightness:
                // Pink-white bar: brightness of the selected bowl.
                DrawBar(params.brightness_ticks[cur_mode][edit_key].load(
                            std::memory_order_relaxed),
                        48, 20, 36);
                break;
            case Overlay::Beat:
            {
                const int mode = ModeIdx(play_modes.Mode());
                const int key = last_key_[mode];
                DrawBar(params.beat_ticks[mode][key].load(
                            std::memory_order_relaxed),
                        48, 0, 0);
                break;
            }
            case Overlay::Decay:
                // Blue bar: decay of the selected bowl, matching the
                // encoder 3 LED.
                DrawBar(params.decay_ticks[cur_mode][edit_key].load(
                            std::memory_order_relaxed),
                        0, 20, 48);
                break;
            case Overlay::Shimmer:
                // Pink center-out bar, matching encoder 4 page 2.
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
                // Pale ice bar for the room reverb mix.
                DrawBar(params.reverb_mix_ticks.load(std::memory_order_relaxed),
                        24, 32, 48);
                break;
            case Overlay::ReverbDecay:
                DrawBar(
                    params.reverb_decay_ticks.load(std::memory_order_relaxed),
                    40, 16, 40);
                break;
            case Overlay::BowlVolume:
                // Green bar: per-bowl volume of the selected bowl.
                DrawBar(params.bowl_vol_ticks[cur_mode][edit_key].load(
                            std::memory_order_relaxed),
                        0, 48, 0);
                break;
            case Overlay::Tune:
            {
                const bool fine
                    = params.tune_fine.load(std::memory_order_relaxed);
                if(fine)
                {
                    const uint32_t pitch_millihz
                        = params.pitch_millihz[cur_mode][edit_key].load(
                            std::memory_order_relaxed);
                    uint32_t reference_millihz = pitch_millihz;
                    if(params.tune_fine_reference_mode.load(
                           std::memory_order_relaxed)
                           == cur_mode
                       && params.tune_fine_reference_key.load(
                              std::memory_order_relaxed)
                              == edit_key)
                        reference_millihz
                            = params.tune_fine_reference_millihz.load(
                                std::memory_order_relaxed);
                    DrawFinePitchBar(pitch_millihz, reference_millihz);
                }
                else
                    DrawPitchBar(KeyFundamental(
                        static_cast<PlayMode>(cur_mode), edit_key));
                break;
            }
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
                else if(key <= bowls::kRimKeyLast)
                    chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[key],
                                           40, 40, 0);
                else
                    chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[key],
                                           0, 0, 48);
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
        else if(mode == kPresetCopyDest && slot == preset_sel_slot_
                && static_cast<int32_t>(preset_copy_error_until_ - now) > 0)
        {
            if(pre_blink)
                red = 48;
        }
        else if(filled && slot == preset_current_
                && (mode == kPresetSaveSel || mode == kPresetCopySrc
                    || mode == kPresetCopyDest))
        {
            red = 0;
            green = 48;
            blue = 48;
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
    if(hw.PlayPressed() && !hw.LoopPressed() && !hw.ShiftHeld()
       && play_hold_valid_
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
                    {
                        if(slot == preset_current_)
                            chompi_leds::SetSmtLed(
                                chompi_leds::kWhiteKeyLeds[slot], 0, 48, 48);
                        else
                            chompi_leds::SetSmtLed(
                                chompi_leds::kWhiteKeyLeds[slot],
                                48, 48, 48);
                    }
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

    for(int key = 0; key < bowls::kKeyCount; ++key)
        if(static_cast<int32_t>(strike_retrigger_flash_until_[key] - now) > 0)
            chompi_leds::SetSmtLed(chompi_leds::kKeyLeds[key], 48, 48, 48);

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
    // Encoder-1 LED: green = bowl volume, cyan = pan,
    // violet = direct-Hz frequency.
    if(enc1_page_ == kEnc1PagePan)
    {
        const int cur_mode = ModeIdx(play_modes.Mode());
        const int edit_key = last_key_[cur_mode];
        const bool centered
            = params.pan_ticks[cur_mode][edit_key].load(
                  std::memory_order_relaxed)
              == chompi::BowlsPreset::kDefaultPan;
        chompi_leds::SetPthLed(chompi_leds::kPthEncoder1,
                               0, 20, centered ? 0 : 20);
    }
    else if(enc1_page_ == kEnc1PageTune)
        chompi_leds::SetPthLed(chompi_leds::kPthEncoder1, 14, 0, 20);
    else
        chompi_leds::SetPthLed(chompi_leds::kPthEncoder1, 0, 20, 0);
    chompi_leds::SetPthLed(
        chompi_leds::kPthEncoder2,
        enc2_page_ == kEnc2PageBeat ? 48 : 48,
        enc2_page_ == kEnc2PageBeat ? 0 : 20,
        enc2_page_ == kEnc2PageBeat ? 0 : 36);
    // Encoder-3 LED: blue = decay, orange = resonance character.
    if(enc3_page_ == kEnc3PageCharacter)
        chompi_leds::SetPthLed(chompi_leds::kPthEncoder3, 20, 9, 0);
    else
        chompi_leds::SetPthLed(chompi_leds::kPthEncoder3, 0, 8, 20);
    if(enc4_page_ == kEnc4PageStrike)
        chompi_leds::SetPthLed(chompi_leds::kPthEncoder4, 20, 11, 0); // strike
    else
        chompi_leds::SetPthLed(chompi_leds::kPthEncoder4, 34, 0, 48); // shimmer
#ifdef CPU_METER
    // Steal witness: encoder 4 LED goes white for ~300 ms when a strike
    // steals a still-ringing voice - correlate it with audible chokes.
    if(System::GetNow() < g_steal_flash_until)
        chompi_leds::SetPthLed(chompi_leds::kPthEncoder4, 40, 40, 40);
#endif
    const uint8_t rub = static_cast<uint8_t>(rub_display_ * 40.0f);
    if(rub_display_clean_)
    {
        const uint8_t pink_blue = rub / 2;
        chompi_leds::SetPthLed(chompi_leds::kPthWheelLeft,
                               rub, 0, pink_blue);
        chompi_leds::SetPthLed(chompi_leds::kPthWheelRight,
                               rub, 0, pink_blue);
    }
    else
    {
        const uint8_t purple_red
            = static_cast<uint8_t>(rub * (34.0f / 48.0f));
        chompi_leds::SetPthLed(chompi_leds::kPthWheelLeft,
                               purple_red, 0, rub);
        chompi_leds::SetPthLed(chompi_leds::kPthWheelRight,
                               purple_red, 0, rub);
    }
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
        const uint8_t level
            = play_modes.StrikeLatchEnabled() ? 40 : 8;
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;
        switch(play_modes.GetStrikeArpeggioMode())
        {
            case bowls::StrikeArpeggioMode::kDurationLoop:
                blue = level;
                break;
            case bowls::StrikeArpeggioMode::kBottomPair:
                green = level;
                break;
            case bowls::StrikeArpeggioMode::kTopPair:
                red = level;
                green = static_cast<uint8_t>(level / 3);
                break;
            case bowls::StrikeArpeggioMode::kMiddlePair:
                red = static_cast<uint8_t>(level / 2);
                blue = level;
                break;
        }
        chompi_leds::SetPthLed(chompi_leds::kPthPlay, red, green, blue);
        chompi_leds::SetPthLed(chompi_leds::kPthLoop,
                               play_modes.LatchEnabled() ? 40 : 0,
                               play_modes.LatchEnabled() ? 40 : 0,
                               0);
    }
    // The volume knob LED follows the page: master gradient or reverb.
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
            const bool copy_error
                = preset_mode_ == kPresetCopyDest
                  && static_cast<int32_t>(preset_copy_error_until_ - now) > 0;
            const uint8_t level = (now / (copy_error ? 100u : 250u)) % 2u == 0
                                      ? 48
                                      : 0;
            chompi_leds::SetPthLed(chompi_leds::kPthShift, level, 0, 0);
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
    p.SetDefaults();
    p.mode = static_cast<uint8_t>(ModeIdx(play_modes.Mode()));
    p.strike = static_cast<uint8_t>(
        params.strike_ticks.load(std::memory_order_relaxed));
    p.shimmer = static_cast<uint8_t>(
        params.shimmer_ticks.load(std::memory_order_relaxed));
    for(int m = 0; m < bowls::kPlayModeCount; ++m)
        for(int w = 0; w < bowls::kKeyCount; ++w)
        {
            const int slot = m * bowls::kKeyCount + w;
            const float pitch_hz = MilliHzToPitch(
                params.pitch_millihz[m][w].load(std::memory_order_relaxed));
            if(m == ModeIdx(PlayMode::kStrikes))
                p.pitch_hz.strikes[w] = pitch_hz;
            else if(m == ModeIdx(PlayMode::kRimming))
                p.pitch_hz.rimming[w] = pitch_hz;
            else
                p.pitch_hz.split[w] = pitch_hz;
            p.pan[slot] = static_cast<uint8_t>(
                params.pan_ticks[m][w].load(std::memory_order_relaxed));
            p.bvol[slot] = static_cast<uint8_t>(
                params.bowl_vol_ticks[m][w].load(
                    std::memory_order_relaxed));
            p.profile[slot] = static_cast<uint8_t>(
                params.profile_ticks[m][w].load(
                    std::memory_order_relaxed));
            p.bright[slot] = static_cast<uint8_t>(
                params.brightness_ticks[m][w].load(
                    std::memory_order_relaxed));
            p.decay[slot] = static_cast<uint8_t>(
                params.decay_ticks[m][w].load(std::memory_order_relaxed));
            p.beat[slot] = static_cast<uint8_t>(
                params.beat_ticks[m][w].load(std::memory_order_relaxed));
        }
    p.reverb_mix = static_cast<uint8_t>(
        params.reverb_mix_ticks.load(std::memory_order_relaxed));
    p.reverb_decay = static_cast<uint8_t>(
        params.reverb_decay_ticks.load(std::memory_order_relaxed));
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
    params.strike_ticks.store(p.strike, std::memory_order_relaxed);
    params.shimmer_ticks.store(p.shimmer, std::memory_order_relaxed);
    for(int m = 0; m < bowls::kPlayModeCount; ++m)
        for(int w = 0; w < bowls::kKeyCount; ++w)
        {
            const int slot = m * bowls::kKeyCount + w;
            params.pitch_millihz[m][w].store(
                PitchToMilliHz(bowls::FundamentalHz(
                    static_cast<PlayMode>(m), w, p.pitch_hz)),
                std::memory_order_relaxed);
            params.pan_ticks[m][w].store(p.pan[slot],
                                         std::memory_order_relaxed);
            params.bowl_vol_ticks[m][w].store(p.bvol[slot],
                                              std::memory_order_relaxed);
            params.profile_ticks[m][w].store(p.profile[slot],
                                             std::memory_order_relaxed);
            params.brightness_ticks[m][w].store(p.bright[slot],
                                                std::memory_order_relaxed);
            params.decay_ticks[m][w].store(p.decay[slot],
                                           std::memory_order_relaxed);
            params.beat_ticks[m][w].store(p.beat[slot],
                                          std::memory_order_relaxed);
        }
    params.reverb_mix_ticks.store(p.reverb_mix, std::memory_order_relaxed);
    params.reverb_decay_ticks.store(p.reverb_decay,
                                    std::memory_order_relaxed);
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
    preset_copy_error_until_ = 0;
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
                chompi::BowlsPreset preset = SnapshotParams();
                if(preset_bank.IsValid(preset_sel_slot_))
                    preset.CopyMetadataFrom(preset_bank.Get(preset_sel_slot_));
                preset_bank.Save(preset_sel_slot_, preset);
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
                preset_copy_error_until_ = 0;
                preset_storage.ClearIncompatibleSlot(preset_sel_slot_);
                preset_mode_ = kPresetCopying;
                preset_anim_t_ = System::GetNow();
            }
            else if(preset_copy_src_ >= 0
                    && preset_bank.IsValid(preset_copy_src_)
                    && !preset_bank.Get(preset_copy_src_).CanAppendCopySuffix())
            {
                preset_copy_error_until_ = System::GetNow() + 1500;
                BOWLS_LOG("Preset copy failed: name cannot fit -copy suffix");
            }
            break;
        default:
            break;
    }
}

/** Handle preset gestures while keeping note releases
 *  for notes that began before a selection view was entered. */
bool PresetKeyEvent(int key, bool down, bool shift, bool fine_tune_key_play)
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

    if(!down || !shift || fine_tune_key_play)
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
    // MIDI uses DIN (UART RX/TX D13/D14) and the panel USB-C port.
    // Diagnostic builds reserve the panel USB-C port for serial logging.
    midi.Init(kUsbMidiEnable);
    hw.ConfigureUsbDevicePath();
#ifdef AUDIO_DEADLINE_DIAG
    daisy::Logger<daisy::LOGGER_EXTERNAL>::StartLog(false);
#elif defined(CPU_METER)
    hw.seed.StartLog(false);
#endif
    chompi_leds::Init();
    chompi_leds::ClearAll();

    for(int v = 0; v < kVoiceCount; ++v)
        voices[v].Init(kSampleRate);
    ApplyShimmerToVoices();

    // 1970s-style room reverb with a 20 ms pre-delay and gentle modulation.
    reverb.Init(kSampleRate,
                params.reverb_decay_ticks.load(std::memory_order_relaxed));

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

#ifdef AUDIO_DEADLINE_DIAG
    if(!hw.seed.audio_handle.EnableDeadlineDiagnostics())
        BOWLS_LOG("AUDIO_DIAG: unable to enable cycle counter");
    uint32_t audio_diag_reported_at = System::GetNow();
#endif

    hw.StartAudio(AudioCallback);

    while(true)
    {
        /* MIDI transport service, thread level only: UART error
         * recovery, USB listening and the parser drain into the fixed
         * per-source event queues (the audio ISR just pops queues). */
        midi.Service();
        preset_transfer.Service(midi, preset_storage, preset_bank);

        /* CC 120 deferred reverb clear: the panic fade has muted the
         * bowl bus and the ISR skips reverb.Process while this runs,
         * so the reverb state clear never races the renderer;
         * the mute lifts only after the clear completes. */
        if(reverb_reset_pending_)
        {
            reverb.Init(
                kSampleRate,
                params.reverb_decay_ticks.load(std::memory_order_relaxed));
            reverb_reset_pending_ = false;
            panic_mute_           = false;
        }

        /* Staged, non-blocking SD-card work: one small FatFS step per
         * pass, at thread level only (never in the audio ISR). */
        preset_storage.Service(preset_bank);
        sd_recorder.Service(preset_storage, System::GetNow());
        const uint32_t now = System::GetNow();
#ifdef AUDIO_DEADLINE_DIAG
        if(now - audio_diag_reported_at >= 1000)
        {
            daisy::AudioHandle::DeadlineStats stats;
            if(hw.seed.audio_handle.ConsumeDeadlineStats(stats))
                BOWLS_LOG("AUDIO_DIAG callbacks=%lu misses=%lu max=%luus budget=%luus",
                          static_cast<unsigned long>(stats.callbacks),
                          static_cast<unsigned long>(stats.misses),
                          static_cast<unsigned long>(stats.max_us),
                          static_cast<unsigned long>(stats.budget_us));
            else
                BOWLS_LOG("AUDIO_DIAG: timing stats unavailable");
            audio_diag_reported_at = now;
        }
#endif
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
        int storage_event_slot = -1;
        const auto storage_event
            = preset_storage.ConsumeStatusEvent(&storage_event_slot);
        preset_transfer.OnStorageEvent(storage_event, storage_event_slot, midi);
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