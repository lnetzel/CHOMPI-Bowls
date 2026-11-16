// Host-side tests for the play-mode state machine and the 25-key mode
// maps (plans/improve-play-modes.md). Not part of the firmware build;
// run with `make -C code/tests modes-test` from the MSYS2 UCRT64 shell.
//
//   M1  mappings: Split keeps the original 24 pitches + the added
//       1240.8 Hz top strike; Strikes/Rimming share 25 distinct,
//       ascending fundamentals; shortcuts are lower F#/G#/A# (6/8/10)
//   M2  shortcut consumption: press/release in both SHIFT orders,
//       idempotent same-mode selection, and no note/latch/edit side
//       effects from a consumed shortcut key
//   M3  Strikes: key-down strikes, key-up is inert
//   M4  Rimming momentary pads: down opens the gate, up releases
//   M5  LOOP latch: capture of held sounding pads, key-down toggles,
//       key-up never releases a latched note, LOOP-off releases
//       latched-only notes but keeps physically held ones
//   M6  leaving Rimming = latch cleanup + LOOP off; held pads persist
//       until their own key-up; key-up after a mode switch releases
//       the note started in the old mode
//   M7  voice steal of a latched/held pad clears only that note's
//       latch; LOOP stays enabled; the stolen note never restarts
//   M8  wheel sets: Split two-held/last-selected fallback, Strikes
//       held-only, Rimming held-or-latched, and current-mode-only
//       wheel addressing after a mode switch

#include "play_modes.h"
#include "voice_allocator.h"
#include "control_mappings.h"

#include <cstdio>

namespace
{
int g_failures = 0;

void Check(bool ok, const char* name, const char* detail)
{
    std::printf("%-46s %s  %s\n", name, ok ? "PASS" : "FAIL", detail);
    if(!ok)
        ++g_failures;
}

using bowls::PlayMode;
using bowls::PlayModes;
using bowls::KeyAction;

bool FloatEq(float a, float b)
{
    const float d = a - b;
    return d < 0.001f && d > -0.001f;
}

bool IntArrayEq(const int* actual, int count, const int* expected,
                int expected_count)
{
    if(count != expected_count)
        return false;
    for(int i = 0; i < count; ++i)
        if(actual[i] != expected[i])
            return false;
    return true;
}

// -- M1: mappings ----------------------------------------------------------

void TestMappings()
{
    bool ok = true;
    // Split: keys 0-11 and 12-23 share the original 12 bowls...
    for(int k = 0; k < 12; ++k)
    {
        if(!FloatEq(bowls::FundamentalHz(PlayMode::kSplit, k),
                    bowls::kBowlScaleHz[k]))
            ok = false;
        if(!FloatEq(bowls::FundamentalHz(PlayMode::kSplit, 12 + k),
                    bowls::kBowlScaleHz[k]))
            ok = false;
    }
    // ...and key 24 strikes the added 1240.8 Hz top bowl.
    if(!FloatEq(bowls::FundamentalHz(PlayMode::kSplit, 24), 1240.8f))
        ok = false;
    Check(ok, "M1a Split keeps 12 bowls + 1240.8 top", "");

    // Full-key modes: 25 distinct, strictly ascending fundamentals.
    ok = true;
    for(int k = 0; k < bowls::kKeyCount; ++k)
    {
        const float fs = bowls::FundamentalHz(PlayMode::kStrikes, k);
        const float fr = bowls::FundamentalHz(PlayMode::kRimming, k);
        if(!FloatEq(fs, fr))
            ok = false;
        if(k > 0 && !(fs > bowls::FundamentalHz(PlayMode::kStrikes,
                                                    k - 1)))
            ok = false;
        for(int j = 0; j < k; ++j)
            if(FloatEq(fs, bowls::FundamentalHz(PlayMode::kStrikes, j)))
                ok = false;
    }
    Check(ok, "M1b full modes: 25 distinct ascending", "");

    bowls::PitchTables custom = bowls::FactoryPitchTables();
    custom.strikes[3] = 321.25f;
    custom.rimming[3] = 654.5f;
    custom.split[4] = 777.75f;
    custom.split[16] = 888.25f;
    Check(FloatEq(bowls::FundamentalHz(PlayMode::kStrikes, 3, custom),
                  321.25f)
              && FloatEq(bowls::FundamentalHz(PlayMode::kRimming, 3,
                                                   custom),
                         654.5f)
              && FloatEq(bowls::FundamentalHz(PlayMode::kSplit, 4,
                                                   custom),
                         777.75f)
              && FloatEq(bowls::FundamentalHz(PlayMode::kSplit, 16,
                                                   custom),
                         888.25f),
          "M1c direct pitches preserve mode and Split-key independence",
          "");

    Check(bowls::AdjustPitchHz(40.0f, 1, false) == 41.0f
              && bowls::AdjustPitchHz(100.0f, 1, false) == 101.0f
              && bowls::AdjustPitchHz(1000.0f, 1, false) == 1010.0f
              && bowls::AdjustPitchHz(10000.0f, 1, false) == 10000.0f
              && bowls::AdjustPitchHz(40.0f, -1, false) == 40.0f
              && bowls::AdjustPitchHz(1000.0f, -1, false) == 990.0f,
          "M1d coarse tuning is ~1% with bounded endpoints", "");
    Check(bowls::AdjustPitchHz(40.0f, 1, true) == 41.0f
              && bowls::AdjustPitchHz(10000.0f, -1, true) == 9999.0f
              && bowls::AdjustPitchHz(1000.0f, 2, true) == 1002.0f,
          "M1e fine tuning is exactly 1 Hz per detent", "");

    bool led_ordered = bowls::PitchLedIndex(40.0f, 15) == 0
                       && bowls::PitchLedIndex(10000.0f, 15) == 14;
    int previous_led = -1;
    for(int i = 0; i <= 100; ++i)
    {
        const float pitch
            = 40.0f * powf(250.0f, static_cast<float>(i) / 100.0f);
        const int led = bowls::PitchLedIndex(pitch, 15);
        if(led < previous_led)
            led_ordered = false;
        previous_led = led;
    }
    Check(led_ordered, "M1f logarithmic LED position spans full Hz range",
          "");

    Check(bowls::kModeShortcutKey[0] == 8
              && bowls::kModeShortcutKey[1] == 10
              && bowls::kModeShortcutKey[2] == 6,
           "M1e shortcut keys are Split/Strikes/Rimming = 6/8/10", "");
    Check(PlayModes::IsModeShortcut(6) && PlayModes::IsModeShortcut(8)
              && PlayModes::IsModeShortcut(10)
              && !PlayModes::IsModeShortcut(7),
          "M1f IsModeShortcut", "");
    Check(PlayModes::ShortcutMode(6) == PlayMode::kSplit
              && PlayModes::ShortcutMode(8) == PlayMode::kStrikes
              && PlayModes::ShortcutMode(10) == PlayMode::kRimming,
          "M1g ShortcutMode", "");
}

// -- M2: shortcut consumption ----------------------------------------------

void TestShortcuts()
{
    {
        PlayModes sm;
        Check(sm.Mode() == PlayMode::kSplit,
              "M2 default mode is Split", "");
    }
    {
        PlayModes sm;
        sm.Init(PlayMode::kSplit);
        // SHIFT+G# (8): mode change to Strikes, nothing else.
        KeyAction a = sm.KeyDown(8, true);
        Check(a.kind == KeyAction::kModeChanged
                  && a.mode == PlayMode::kStrikes
                  && sm.Mode() == PlayMode::kStrikes,
              "M2a SHIFT+G# selects Strikes", "");
        // Release with SHIFT still held: consumed, no note-off action.
        a = sm.KeyUp(8);
        Check(a.kind == KeyAction::kNone && sm.DownMode(8) == -1,
              "M2b shortcut key-up consumed", "");
    }
    {
        PlayModes sm;
        sm.Init(PlayMode::kStrikes);
        // SHIFT down, shortcut down (consumed), SHIFT UP first, then
        // the key releases: still consumed, must not strike.
        KeyAction a = sm.KeyDown(10, true);
        Check(a.kind == KeyAction::kModeChanged
                  && sm.Mode() == PlayMode::kRimming,
              "M2c SHIFT+A# selects Rimming", "");
        a = sm.KeyUp(10); // shift released first - still swallowed
        Check(a.kind == KeyAction::kNone, "M2d release after SHIFT-up consumed",
              "");
        // The shortcut press never struck/selected anything.
        Check(sm.DownMode(10) == -1, "M2e shortcut never becomes a note", "");
    }
    {
        PlayModes sm;
        sm.Init(PlayMode::kSplit);
        // Re-selecting the active mode with SHIFT+F# is idempotent
        // (consumed, no mode-change action).
        KeyAction a = sm.KeyDown(6, true);
        Check(a.kind == KeyAction::kNone && sm.Mode() == PlayMode::kSplit,
              "M2f same-mode selection is idempotent", "");
        a = sm.KeyUp(6);
        Check(a.kind == KeyAction::kNone, "M2g idempotent release consumed", "");
    }
    {
        PlayModes sm;
        sm.Init(PlayMode::kStrikes);
        // A non-shortcut key WITH shift still plays normally.
        KeyAction a = sm.KeyDown(7, true);
        Check(a.kind == KeyAction::kStrike && a.key == 7
                  && a.mode == PlayMode::kStrikes,
              "M2h SHIFT+non-shortcut key still plays", "");
        sm.KeyUp(7);
        // A shortcut must not toggle a latched note either: in Rimming
        // with latch on, SHIFT+A# re-selection is consumed without
        // touching pad state.
        PlayModes sm2;
        sm2.Init(PlayMode::kRimming);
        sm2.KeyDown(3, false);
        sm2.LoopPressed(); // capture
        sm2.KeyUp(3);      // latched
        KeyAction b = sm2.KeyDown(10, true); // same-mode shortcut
        Check(b.kind == KeyAction::kNone && sm2.Latched(3)
                  && sm2.PadOn(3) && sm2.LatchEnabled(),
              "M2i shortcut never toggles latch", "");
    }
}

// -- M3/M4: Strikes and momentary Rimming pads ------------------------------

void TestBasicModes()
{
    {
        PlayModes sm;
        sm.Init(PlayMode::kStrikes);
        bool ok = true;
        for(int k = 0; k < bowls::kKeyCount; ++k)
        {
            KeyAction a = sm.KeyDown(k, false);
            if(a.kind != KeyAction::kStrike || a.key != (int)k
               || a.mode != PlayMode::kStrikes)
                ok = false;
            if(sm.KeyUp(k).kind != KeyAction::kNone)
                ok = false;
        }
        Check(ok, "M3 Strikes: all 25 keys strike, key-up inert", "");
    }
    {
        PlayModes sm;
        sm.Init(PlayMode::kRimming);
        KeyAction a = sm.KeyDown(9, false);
        Check(a.kind == KeyAction::kPadOn && a.key == 9
                  && a.mode == PlayMode::kRimming && sm.PadOn(9),
              "M4a Rimming key-down opens the pad", "");
        sm.KeyUp(9);
        Check(!sm.PadOn(9), "M4b key-up releases the momentary pad", "");
    }
}

// -- M5: LOOP latch ----------------------------------------------------------

void TestLatch()
{
    PlayModes sm;
    sm.Init(PlayMode::kRimming);
    // Hold two pads, then LOOP on: both captured (latched).
    sm.KeyDown(2, false);
    sm.KeyDown(5, false);
    sm.LoopPressed();
    Check(sm.LatchEnabled() && sm.Latched(2) && sm.Latched(5)
              && !sm.Latched(7),
          "M5a LOOP captures held sounding pads", "");
    // Key-up does not release a latched note.
    sm.KeyUp(2);
    Check(sm.PadOn(2) && sm.Latched(2), "M5b key-up keeps latched note", "");
    // A key-down on a fresh key latches it on.
    KeyAction a = sm.KeyDown(7, false);
    Check(a.kind == KeyAction::kPadOn && sm.Latched(7) && sm.PadOn(7),
          "M5c key-down latches a new note on", "");
    sm.KeyUp(7);
    Check(sm.PadOn(7), "M5d new latched note survives key-up", "");
    // A second key-down toggles the note OFF immediately, even while
    // the key is physically down.
    a = sm.KeyDown(5, false);
    Check(a.kind == KeyAction::kNone && !sm.PadOn(5) && !sm.Latched(5)
              && sm.DownMode(5) == (int)PlayMode::kRimming,
          "M5e second key-down releases immediately", "");
    sm.KeyUp(5); // inert: the note is already off
    Check(!sm.PadOn(5), "M5f key-up after toggle-off is inert", "");
    // LOOP off: latched-only notes (2, 7) release; a still-held note
    // keeps its gate until its own key-up.
    sm.KeyDown(11, false); // held while LOOP goes off
    sm.LoopPressed();
    Check(!sm.LatchEnabled() && !sm.Latched(2) && !sm.Latched(7)
              && !sm.PadOn(2) && !sm.PadOn(7) && sm.PadOn(11),
          "M5g LOOP-off releases latched-only, keeps held", "");
    sm.KeyUp(11);
    Check(!sm.PadOn(11), "M5h held note releases on its key-up", "");
}

// -- M9: Strike PLAY latch --------------------------------------------------

void TestStrikeLatch()
{
    PlayModes sm;
    sm.Init(PlayMode::kStrikes);
    Check(sm.KeyDown(4, false).kind == KeyAction::kStrike,
          "M9a ordinary Strike still triggers", "");
    sm.PlayPressed();
    Check(sm.StrikeLatchEnabled() && sm.StrikeLatched(4),
          "M9b PLAY captures held strikes", "");
    sm.KeyUp(4);
    Check(sm.StrikeLatched(4),
          "M9c captured Strike survives key-up", "");

    KeyAction a = sm.KeyDown(7, false);
    Check(a.kind == KeyAction::kStrike && sm.StrikeLatched(7),
          "M9d latched-mode key-down strikes and latches", "");
    sm.KeyUp(7);
    a = sm.KeyDown(7, false);
    Check(a.kind == KeyAction::kNone && !sm.StrikeLatched(7),
          "M9e second key-down toggles a Strike latch off", "");
    sm.KeyUp(7);

    sm.PlayPressed();
    Check(!sm.StrikeLatchEnabled() && !sm.StrikeLatched(4),
          "M9f PLAY off clears Strike latches", "");

    sm.PlayPressed();
    sm.KeyDown(12, false);
    sm.KeyUp(12);
    sm.OnVoiceStolen(PlayMode::kStrikes, 12);
    Check(!sm.StrikeLatched(12),
          "M9g a stolen Strike latch is cleared", "");
    sm.SetMode(PlayMode::kRimming);
    sm.PlayPressed();
    Check(sm.StrikeLatchEnabled(),
          "M9h PLAY latch is inert outside Strikes", "");
    sm.SetMode(PlayMode::kSplit);
    Check(!sm.StrikeLatchEnabled(),
          "M9i entering Split clears Strike latches", "");

    PlayModes midi;
    midi.Init(PlayMode::kStrikes);
    midi.PlayPressed();
    midi.MidiNoteOn(bowls::Source::kMidiDin, 9, 86);
    midi.MidiNoteOff(bowls::Source::kMidiDin, 9);
    Check(midi.StrikeLatchVelocity(9) == 86
              && midi.StrikeLatchUsesMidiVelocity(9),
          "M9j MIDI latch retains its retrigger velocity", "");
}

void TestStrikeArpeggioModes()
{
    PlayModes sm;
    sm.Init(PlayMode::kStrikes);
    Check(sm.GetStrikeArpeggioMode()
              == bowls::StrikeArpeggioMode::kDurationLoop,
          "M9k Strike latch defaults to duration loop", "");
    sm.PlayPressed();
    const int latched[] = {2, 5, 9, 14, 20};
    for(unsigned i = 0; i < sizeof(latched) / sizeof(latched[0]); ++i)
    {
        sm.KeyDown(latched[i], false);
        sm.KeyUp(latched[i]);
    }

    int order[bowls::kStrikeArpeggioMaxSequenceLength];
    const int bottom_expected[] = {2, 5, 9, 2, 5, 14, 2, 5, 20};
    sm.CycleStrikeArpeggioMode();
    int count = sm.GetStrikeArpeggioOrder(order);
    Check(sm.GetStrikeArpeggioMode()
                  == bowls::StrikeArpeggioMode::kBottomPair
              && IntArrayEq(order, count, bottom_expected, 9),
          "M9l bottom pair repeats with ascending third notes", "");

    const int top_expected[] = {20, 14, 9, 20, 14, 5, 20, 14, 2};
    sm.CycleStrikeArpeggioMode();
    count = sm.GetStrikeArpeggioOrder(order);
    Check(sm.GetStrikeArpeggioMode()
                  == bowls::StrikeArpeggioMode::kTopPair
              && IntArrayEq(order, count, top_expected, 9),
          "M9m top pair repeats with descending third notes", "");

    const int middle_expected[] = {9, 14, 2, 9, 14, 5, 9, 14, 20};
    sm.CycleStrikeArpeggioMode();
    count = sm.GetStrikeArpeggioOrder(order);
    Check(sm.GetStrikeArpeggioMode()
                  == bowls::StrikeArpeggioMode::kMiddlePair
              && IntArrayEq(order, count, middle_expected, 9),
          "M9n middle pair repeats with ascending third notes", "");

    sm.CycleStrikeArpeggioMode();
    Check(sm.GetStrikeArpeggioMode()
                  == bowls::StrikeArpeggioMode::kDurationLoop,
          "M9o Strike arpeggio modes cycle back to duration loop", "");

    PlayModes four;
    four.Init(PlayMode::kStrikes);
    four.PlayPressed();
    for(int key = 0; key < 4; ++key)
    {
        four.KeyDown(key, false);
        four.KeyUp(key);
    }
    for(int i = 0; i < 3; ++i)
        four.CycleStrikeArpeggioMode();
    const int four_middle_expected[] = {1, 2, 0, 1, 2, 3};
    count = four.GetStrikeArpeggioOrder(order);
    Check(IntArrayEq(order, count, four_middle_expected, 6),
          "M9p four-note middle pattern matches example", "");

    sm.SetMode(PlayMode::kRimming);
    sm.CycleStrikeArpeggioMode();
    Check(sm.GetStrikeArpeggioMode()
                  == bowls::StrikeArpeggioMode::kDurationLoop,
          "M9q SHIFT+PLAY mode cycle is inert outside Strikes", "");
}

void TestIndependentLatches()
{
    PlayModes sm;
    sm.Init(PlayMode::kStrikes);
    sm.KeyDown(3, false);
    sm.PlayPressed();
    sm.KeyUp(3);

    Check(sm.StrikeLatchEnabled() && sm.StrikeLatched(3),
          "M10a Strike latch enabled in Strikes", "");

    sm.SetMode(PlayMode::kRimming);
    Check(sm.StrikeLatchEnabled() && sm.StrikeLatched(3),
          "M10b Strike latch persists in Rimming", "");
    sm.LoopPressed();
    sm.KeyDown(5, false);
    sm.KeyUp(5);
    Check(sm.LatchEnabled() && sm.Latched(5) && sm.PadOn(5)
              && sm.StrikeLatchEnabled() && sm.StrikeLatched(3),
          "M10c both independent latches can be enabled", "");

    sm.SetMode(PlayMode::kStrikes);
    Check(sm.LatchEnabled() && sm.StrikeLatchEnabled()
              && sm.Latched(5) && sm.StrikeLatched(3),
          "M10d both latches persist between modes", "");

    sm.SetMode(PlayMode::kSplit);
    Check(!sm.LatchEnabled() && !sm.StrikeLatchEnabled()
              && !sm.Latched(5) && !sm.StrikeLatched(3)
              && !sm.PadOn(5),
          "M10e entering Split clears both latches", "");
}

// -- M6: leaving Rimming -----------------------------------------------------

void TestModeExit()
{
    PlayModes sm;
    sm.Init(PlayMode::kRimming);
    sm.KeyDown(1, false); // will be latched, then released physically
    sm.KeyDown(4, false); // will stay held across the mode switch
    sm.LoopPressed();
    sm.KeyUp(1); // latched-only now
    // Leave Rimming for Strikes: latched notes persist; held notes keep
    // their existing release-on-key-up behavior.
    KeyAction a = sm.KeyDown(8, true);
    Check(a.kind == KeyAction::kModeChanged
              && sm.Mode() == PlayMode::kStrikes && sm.LatchEnabled()
              && sm.Latched(1) && sm.PadOn(1),
          "M6a Rimming-to-Strikes preserves latched-only pads", "");
    Check(sm.PadOn(4), "M6b held pad persists across the mode switch", "");
    // Its key-up (in the NEW mode) still releases the Rimming note.
        a = sm.KeyUp(4);
        Check(a.kind == KeyAction::kNone && sm.PadOn(4) && sm.Latched(4),
            "M6c key-up preserves a captured latch across modes", "");
        sm.KeyDown(10, true); // -> Rimming
        Check(sm.Mode() == PlayMode::kRimming && sm.LatchEnabled()
              && sm.Latched(1) && sm.PadOn(1),
            "M6d returning to Rimming restores the preserved latch", "");
        sm.LoopPressed();
        Check(!sm.LatchEnabled() && !sm.Latched(1) && !sm.PadOn(1)
                  && !sm.Latched(4) && !sm.PadOn(4),
            "M6e preserved latch toggles off normally", "");
        sm.LoopPressed();
        sm.KeyDown(2, false);
        sm.KeyUp(2);
        sm.KeyDown(6, true); // -> Split: existing cleanup remains
        Check(sm.Mode() == PlayMode::kSplit && !sm.LatchEnabled()
              && !sm.Latched(2) && !sm.PadOn(2),
            "M6f Rimming-to-Split clears latch and releases pad", "");
    // And a Strikes key held across a switch never becomes a pad.
        sm.KeyDown(8, true); // -> Strikes
    sm.KeyDown(12, false); // strike in Strikes mode
    sm.KeyDown(10, true);  // -> Rimming
    Check(sm.Mode() == PlayMode::kRimming && !sm.PadOn(12)
              && !sm.WheelTargets(12),
            "M6g held strike is not reinterpreted as a pad", "");
    Check(sm.KeyUp(12).kind == KeyAction::kNone,
            "M6h old-mode strike key-up stays inert", "");
}

// -- M7: steals ----------------------------------------------------------------

void TestSteals()
{
    PlayModes sm;
    sm.Init(PlayMode::kRimming);
    sm.KeyDown(3, false);
    sm.LoopPressed();
    sm.KeyUp(3); // latched-only
    // The pad's voice is stolen by a sixth note.
    sm.OnVoiceStolen(PlayMode::kRimming, 3);
    Check(!sm.Latched(3) && !sm.PadOn(3) && sm.LatchEnabled(),
          "M7a steal clears only that note's latch", "");
    // The stolen note does not restart on the next key-down? It does
    // re-trigger cleanly as a NEW note (pad_on was cleared).
    KeyAction a = sm.KeyDown(3, false);
    Check(a.kind == KeyAction::kPadOn && sm.PadOn(3),
          "M7b stolen note can be replayed deliberately", "");
    // A stolen HELD note is not restarted and releases silently.
    PlayModes sm2;
    sm2.Init(PlayMode::kRimming);
    sm2.KeyDown(9, false);
    sm2.OnVoiceStolen(PlayMode::kRimming, 9);
    Check(!sm2.PadOn(9) && sm2.DownMode(9) == (int)PlayMode::kRimming,
          "M7c stolen held note: gate cleared, press tracked", "");
    sm2.KeyUp(9);
    Check(!sm2.PadOn(9) && sm2.DownMode(9) == -1,
          "M7d stolen held note releases without restart", "");
    // Steals in other modes do not touch latch state.
    PlayModes sm3;
    sm3.Init(PlayMode::kSplit);
    sm3.OnVoiceStolen(PlayMode::kSplit, 2);
    Check(true, "M7e non-Rimming steal is a no-op for latch", "");
}

void TestLatchedVoiceProtection()
{
    PlayModes sm;
    sm.Init(PlayMode::kRimming);
    sm.LoopPressed();
    bool  active[5] = {true, true, true, true, true};
    bool  protected_voice[5] = {};
    float envelope[5] = {0.01f, 0.02f, 0.03f, 0.04f, 0.05f};
    for(int key = 0; key < 5; ++key)
    {
        sm.KeyDown(key, false);
        sm.KeyUp(key);
        protected_voice[key] = sm.Latched(key);
    }
    sm.SetMode(PlayMode::kStrikes);
    const auto select = [&]() {
        return bowls::QuietestUnprotectedVoice(
            5, [&](int i) { return active[i]; },
            [&](int i) { return protected_voice[i]; },
            [&](int i) { return envelope[i]; });
    };
    Check(sm.LatchEnabled() && select() == -1,
          "M7f five preserved latches reject voice stealing", "");
    protected_voice[0] = false;
    protected_voice[1] = false;
    Check(select() == 0,
          "M7g allocator steals quietest unprotected voice only", "");

    sm.Init(PlayMode::kStrikes);
    sm.PlayPressed();
    for(int key = 0; key < 5; ++key)
    {
        sm.KeyDown(key, false);
        sm.KeyUp(key);
        protected_voice[key] = sm.StrikeLatched(key);
    }
    Check(bowls::QuietestUnprotectedVoice(
              5, [&](int i) { return active[i]; },
              [&](int i) { return protected_voice[i]; },
              [&](int i) { return envelope[i]; })
              == -1,
          "M7h five Strike latches are protected from stealing", "");
}

// -- M8: wheel sets ------------------------------------------------------------

void TestWheelSets()
{
    {
        PlayModes sm;
        sm.Init(PlayMode::kSplit);
        // Last-selected fallback: press/release 3, then 5 held, 7 held,
        // 9 held (drops 3?? no - drops oldest of the held pair).
        sm.KeyDown(3, false);
        sm.KeyUp(3);
        Check(sm.WheelTargets(3) && !sm.WheelTargets(5),
              "M8a Split: last-selected rim key is the fallback", "");
        sm.KeyDown(5, false);
        sm.KeyDown(7, false);
        Check(sm.WheelTargets(5) && sm.WheelTargets(7)
                  && !sm.WheelTargets(3),
              "M8b Split: two held rim keys win", "");
        sm.KeyDown(9, false); // third held: drops the oldest (5)
        Check(!sm.WheelTargets(5) && sm.WheelTargets(7)
                  && sm.WheelTargets(9),
              "M8c Split: third held key drops the oldest", "");
        Check(sm.RimLatch() == 9, "M8d Split: latch follows last press", "");
        sm.KeyUp(7);
        Check(sm.WheelTargets(9) && !sm.WheelTargets(7),
              "M8e Split: key-up leaves the rub set", "");
        // Strike-zone keys are never wheel targets.
        Check(!sm.WheelTargets(15) && !sm.WheelTargets(24),
              "M8f Split: strike keys never rim", "");
    }
    {
        PlayModes sm;
        sm.Init(PlayMode::kStrikes);
        sm.KeyDown(0, false);
        sm.KeyDown(20, false);
        Check(sm.WheelTargets(0) && sm.WheelTargets(20)
                  && !sm.WheelTargets(1),
              "M8g Strikes: wheel addresses physically held notes", "");
        sm.KeyUp(0);
        Check(!sm.WheelTargets(0),
              "M8h Strikes: released key leaves the wheel set", "");
    }
    {
        PlayModes sm;
        sm.Init(PlayMode::kRimming);
        sm.KeyDown(2, false);            // held pad
        sm.KeyDown(6, false);
        sm.LoopPressed();
        sm.KeyUp(6);                     // latched-only pad
        Check(sm.WheelTargets(2) && sm.WheelTargets(6)
                  && !sm.WheelTargets(3),
              "M8i Rimming: wheel addresses held or latched pads", "");
        // Switching to Strikes: old pads are no longer wheel targets;
        // only new-mode notes are addressed.
        sm.KeyDown(8, true);             // SHIFT+G#: -> Strikes
        // (key 8 was the shortcut; the latched pad on key 6 was
        // released by the mode exit)
        Check(sm.Mode() == PlayMode::kStrikes && !sm.WheelTargets(2),
              "M8j wheel addresses current mode only", "");
    }
}

} // namespace

int main()
{
    TestMappings();
    TestShortcuts();
    TestBasicModes();
    TestLatch();
    TestStrikeLatch();
    TestStrikeArpeggioModes();
    TestIndependentLatches();
    TestModeExit();
    TestSteals();
    TestLatchedVoiceProtection();
    TestWheelSets();
    std::printf("%s (%d failures)\n",
                g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
