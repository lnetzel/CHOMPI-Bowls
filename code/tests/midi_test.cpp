// Host-side tests for the CHOMPI-BOWLS MIDI input feature
// (plans/midi-input-and-cc-control.md). Not part of the firmware
// build; run with `make -C code/tests midi-test` from the MSYS2
// UCRT64 shell.
//
//   T1  MidiCcMap: note map 48..72, tick conversion (endpoints,
//       monotonicity, all 33 reachable positions), profile/mode/edit
//       selector boundaries, supported-CC filter
//   T2  MidiEventQueue/MidiRouter: per-source FIFO order, bounded fair
//       dispatch, overflow cleanup protocol (no stale events after an
//       emergency cleanup), transport-loss cleanup
//   T3  PlayModes source-aware ownership: local/DIN/USB overlap,
//       source-isolated releases, repeated Note Ons, cross-mode
//       replay, velocity strengths, sustain, source-scoped CC 121/123,
//       global CC 120, Split rim sets/fallbacks, steals, MIDI mode
//       changes, CC 1 per-sender rub eligibility
//   T4  Rub arbitration: highest level wins (never the sum), texture
//       follows the winner, ties go to the latest control event

#define BOWLS_MIDI_HOST_TEST

#include "MidiCcMap.h"
#include "MidiHandler.h"
#include "play_modes.h"

#include <cstdio>
#include <vector>

namespace
{
int g_failures = 0;

void Check(bool ok, const char* name, const char* detail)
{
    std::printf("%-52s %s  %s\n", name, ok ? "PASS" : "FAIL", detail);
    if(!ok)
        ++g_failures;
}

using bowls::PlayMode;
using bowls::PlayModes;
using bowls::KeyAction;
using bowls::Source;
namespace ccmap = bowls::MidiCcMap;

// -- T1: mapping and conversions -----------------------------------------

void TestNoteMap()
{
    bool ok = true;
    for(int n = 0; n < 128; ++n)
    {
        const int key = ccmap::NoteToKey(static_cast<uint8_t>(n));
        if(n >= 48 && n <= 72)
        {
            if(key != n - 48)
                ok = false;
        }
        else if(key != -1)
            ok = false; // no wrap, transpose or clamp-to-edge
    }
    Check(ok, "T1a notes 48..72 -> keys 0..24, rest ignored", "");
    Check(ccmap::NoteToKey(48) == 0 && ccmap::NoteToKey(72) == 24
              && ccmap::NoteToKey(47) == -1 && ccmap::NoteToKey(73) == -1,
          "T1b note map endpoints", "");
}

void TestTickConversion()
{
    // Endpoints and documented anchors: 0/32/64/95/127 -> 0/8/16/24/32.
    bool ok = ccmap::CcToTicks(0) == 0 && ccmap::CcToTicks(127) == 32
              && ccmap::CcToTicks(32) == 8 && ccmap::CcToTicks(64) == 16
              && ccmap::CcToTicks(95) == 24;
    // Monotonic non-decreasing across all 128 values, and exactly 33
    // reachable positions (0..32 all hit).
    bool reachable[33] = {};
    int  prev = -1;
    for(int v = 0; v < 128; ++v)
    {
        const int t = ccmap::CcToTicks(static_cast<uint8_t>(v));
        if(t < prev || t < 0 || t > 32)
            ok = false;
        prev       = t;
        reachable[t] = true;
    }
    for(int t = 0; t <= 32; ++t)
        if(!reachable[t])
            ok = false;
    Check(ok, "T1c ticks: anchors, monotone, 33 reachable", "");
}

void TestSelectorConversions()
{
    // CC 70 profile boundaries: 0-25/26-51/52-76/77-102/103-127.
    bool ok = true;
    const int bounds[][2] = {{0, 0},  {25, 0},  {26, 1},  {51, 1},
                             {52, 2}, {76, 2},  {77, 3},  {102, 3},
                             {103, 4}, {127, 4}};
    for(const auto& b : bounds)
        if(ccmap::CcToProfile(static_cast<uint8_t>(b[0])) != b[1])
            ok = false;
    Check(ok, "T1d CC 70 profile ranges 25/26 51/52 76/77 102/103", "");

    ok = ccmap::CcToMode(0) == 0 && ccmap::CcToMode(42) == 0
         && ccmap::CcToMode(43) == 1 && ccmap::CcToMode(85) == 1
         && ccmap::CcToMode(86) == 2 && ccmap::CcToMode(127) == 2;
    Check(ok, "T1e CC 80 mode ranges 0-42/43-85/86-127", "");

    ok = ccmap::CcToEditBank(0) == 0 && ccmap::CcToEditBank(1) == 1
         && ccmap::CcToEditBank(2) == 2
         && ccmap::CcToEditBank(127) == ccmap::kEditFollow
         && ccmap::CcToEditBank(3) == ccmap::kEditIgnore
         && ccmap::CcToEditBank(64) == ccmap::kEditIgnore
         && ccmap::CcToEditBank(126) == ccmap::kEditIgnore;
    Check(ok, "T1f CC 81 exact 0/1/2 fixed, 127 follow, rest ignored",
          "");

    ok = ccmap::CcToEditKey(48) == 0 && ccmap::CcToEditKey(72) == 24
         && ccmap::CcToEditKey(47) == -1 && ccmap::CcToEditKey(73) == -1
         && ccmap::CcToEditKey(0) == -1;
    Check(ok, "T1g CC 82 exact note 48-72 selects key 0-24", "");

    // The supported-CC filter: every documented CC passes; reserved
    // and unused controllers are rejected (so floods never queue).
    const uint8_t supported[] = {1,  7,  10, 64, 70, 74, 75,
                                 76, 77, 78, 79, 80, 81, 82,
                                 83, 84, 85, 91, 120, 121, 123};
    ok = true;
    for(uint8_t cc : supported)
        if(!ccmap::IsSupportedCc(cc))
            ok = false;
    const uint8_t unsupported[] = {0,  2,  11, 32,  65,  98, 99,
                                   100, 101, 122, 124, 125, 126, 127};
    for(uint8_t cc : unsupported)
        if(ccmap::IsSupportedCc(cc))
            ok = false;
    Check(ok, "T1h supported-CC filter (incl. reserved 122/124-127)",
          "");

    ok = ccmap::CcToLevel(0) == 0.0f && ccmap::CcToLevel(127) == 1.0f
         && !ccmap::CcToSwitch(63) && ccmap::CcToSwitch(64);
    Check(ok, "T1i CC 1 level endpoints, 64-switch midpoint", "");
}

// -- T2: queues and dispatch ----------------------------------------------

bowls::BowlsMidiEvent Ev(uint8_t type, uint8_t d1, uint8_t d2)
{
    bowls::BowlsMidiEvent e;
    e.type  = type;
    e.data1 = d1;
    e.data2 = d2;
    return e;
}

void TestQueueFifo()
{
    bowls::MidiEventQueue<8> q; // ring holds 7
    q.Init();
    bool ok = true;
    for(int i = 0; i < 7; ++i)
        if(!q.Push(Ev(bowls::BowlsMidiEvent::kNoteOn, 48 + i, 100)))
            ok = false;
    Check(ok, "T2a queue accepts up to capacity", "");
    // FIFO order.
    ok = true;
    for(int i = 0; i < 7; ++i)
    {
        if(!q.HasEvent())
            ok = false;
        else if(q.Pop().data1 != 48 + i)
            ok = false;
    }
    Check(ok && !q.HasEvent(), "T2b per-source FIFO order", "");
}

void TestQueueOverflowCleanup()
{
    bowls::MidiEventQueue<8> q;
    q.Init();
    for(int i = 0; i < 7; ++i)
        q.Push(Ev(bowls::BowlsMidiEvent::kNoteOn, 60, 100));
    // The 8th push overflows: cleanup latched, backlog discarded by the
    // producer, no event lost silently (drops counted).
    bool ok = !q.Push(Ev(bowls::BowlsMidiEvent::kNoteOn, 61, 100))
              && q.CleanupRequested();
    // While the cleanup is latched the producer keeps discarding.
    ok = ok && !q.Push(Ev(bowls::BowlsMidiEvent::kNoteOff, 60, 0));
    // Consumer performs the cleanup: the stale backlog (Note Ons that
    // could be lost) is dropped, never replayed after the cleanup.
    q.FinishCleanup();
    ok = ok && !q.HasEvent() && !q.CleanupRequested();
    // A fresh batch flows again.
    ok = ok && q.Push(Ev(bowls::BowlsMidiEvent::kNoteOn, 62, 90))
         && q.HasEvent() && q.Pop().data1 == 62;
    ok = ok && q.Drops() >= 2;
    Check(ok, "T2c overflow: latched cleanup, no stale events, recovers",
          "");

    // Transport loss takes the same path without an overflow.
    q.Init();
    q.Push(Ev(bowls::BowlsMidiEvent::kNoteOn, 64, 100));
    q.RequestCleanup();
    ok = q.CleanupRequested();
    q.FinishCleanup();
    ok = ok && !q.HasEvent();
    Check(ok, "T2d transport-loss cleanup without overflow", "");
}

void TestFairDispatch()
{
    bowls::MidiRouter r;
    r.Init();
    // Load both sources; DIN has a longer backlog than the budget.
    for(int i = 0; i < 6; ++i)
        r.Queue(bowls::kDin)
            .Push(Ev(bowls::BowlsMidiEvent::kNoteOn, 48 + i, 100));
    for(int i = 0; i < 2; ++i)
        r.Queue(bowls::kUsb)
            .Push(Ev(bowls::BowlsMidiEvent::kCc, 74, 64));

    std::vector<int> order; // source sequence actually served
    int              served_total = 0;
    while(r.Queue(bowls::kDin).HasEvent()
          || r.Queue(bowls::kUsb).HasEvent())
    {
        served_total += r.Dispatch(
            4,
            [&](int src, bowls::BowlsMidiEvent) { order.push_back(src); },
            [&](int) {});
    }
    bool ok = served_total == 8 && order.size() == 8;
    // USB's two events must both appear within the first 4 served
    // (fair alternation: DIN can never starve USB), and within each
    // source the original FIFO order holds.
    int usb_seen = 0, din_first = -1;
    for(size_t i = 0; i < order.size() && ok; ++i)
    {
        if(order[i] == bowls::kUsb)
        {
            ++usb_seen;
            if(i > 3)
                ok = false; // starved past the first budget window
        }
        else if(din_first < 0)
            din_first = static_cast<int>(i);
    }
    ok = ok && usb_seen == 2;
    Check(ok, "T2e fair bounded dispatch: no starvation, budget honored",
          "");

    // A latched cleanup on one source is dispatched as an item and
    // does not block the other source.
    r.Init();
    r.Queue(bowls::kDin).Push(Ev(bowls::BowlsMidiEvent::kNoteOn, 48, 100));
    r.Queue(bowls::kUsb).RequestCleanup();
    int applied = 0, cleaned = 0;
    r.Dispatch(4,
               [&](int src, bowls::BowlsMidiEvent) {
                   if(src == bowls::kDin)
                       ++applied;
               },
               [&](int src) {
                   if(src == bowls::kUsb)
                       ++cleaned;
               });
    Check(applied == 1 && cleaned == 1
              && !r.Queue(bowls::kUsb).CleanupRequested(),
          "T2f cleanup dispatches alongside the other source", "");
}

// -- T3: source-aware ownership (PlayModes) -------------------------------

void TestMultiSourcePads()
{
    // DIN and USB (and local) overlap on one Rimming pad: the gate
    // stays open until ALL owners release.
    PlayModes sm;
    sm.Init(PlayMode::kRimming);
    KeyAction a = sm.MidiNoteOn(Source::kMidiDin, 9, 100);
    bool ok = a.kind == KeyAction::kPadOn && sm.PadOn(9);
    a = sm.MidiNoteOn(Source::kMidiUsb, 9, 64);
    ok = ok && a.kind == KeyAction::kPadOn; // joins (first attack)
    sm.KeyDown(9, false); // local joins too
    ok = ok && sm.PadOn(9);
    sm.MidiNoteOff(Source::kMidiDin, 9);
    ok = ok && sm.PadOn(9); // USB + local still own it
    sm.KeyUp(9);
    ok = ok && sm.PadOn(9); // USB remains
    sm.MidiNoteOff(Source::kMidiUsb, 9);
    ok = ok && !sm.PadOn(9); // last owner released
    Check(ok, "T3a pad gate outlives single-source releases", "");

    // A repeated Note On from one source never needs two Note Offs.
    sm.Init(PlayMode::kRimming);
    sm.MidiNoteOn(Source::kMidiDin, 4, 90);
    a = sm.MidiNoteOn(Source::kMidiDin, 4, 110); // re-on while held
    ok = a.kind == KeyAction::kNone && sm.PadOn(4); // no stacked gate
    sm.MidiNoteOff(Source::kMidiDin, 4);
    ok = ok && !sm.PadOn(4);
    Check(ok, "T3b repeated Note On: one Note Off releases", "");

    // The sender's velocity claims: maximum among owners, retained
    // through the final release.
    sm.Init(PlayMode::kRimming);
    sm.MidiNoteOn(Source::kMidiDin, 7, 60);
    sm.MidiNoteOn(Source::kMidiUsb, 7, 110);
    ok = sm.ClaimStrength(PlayMode::kRimming, 7) == 110;
    sm.MidiNoteOff(Source::kMidiUsb, 7);
    ok = ok && sm.ClaimStrength(PlayMode::kRimming, 7) == 60;
    sm.MidiNoteOff(Source::kMidiDin, 7);
    ok = ok && sm.ClaimStrength(PlayMode::kRimming, 7) == 0;
    Check(ok, "T3c strength = max owner velocity, 0 after release", "");
}

void TestCrossModeReplay()
{
    // A held note replayed by the same source in a different mode
    // releases its previous claim first, then starts the new one.
    PlayModes sm;
    sm.Init(PlayMode::kRimming);
    sm.MidiNoteOn(Source::kMidiDin, 12, 100);
    bool ok = sm.PadOn(12);
    sm.SetMode(PlayMode::kStrikes);
    // Held pad persists across the mode change (releases against its
    // ORIGINAL mode)...
    ok = ok && sm.PadOn(12)
         && sm.DownMode(Source::kMidiDin, 12)
                == static_cast<int>(PlayMode::kRimming);
    // ...until a fresh Note On in the new mode replaces the claim.
    KeyAction a = sm.MidiNoteOn(Source::kMidiDin, 12, 100);
    ok = ok && a.kind == KeyAction::kStrike
         && a.mode == PlayMode::kStrikes && !sm.PadOn(12);
    // And the new claim releases normally in the new mode.
    sm.MidiNoteOff(Source::kMidiDin, 12);
    ok = ok && sm.DownMode(Source::kMidiDin, 12) == -1;
    Check(ok, "T3d cross-mode replay releases the previous claim", "");

    // The same transition with a plain Note Off: the outgoing pad
    // releases against Rimming even though Strikes is now active.
    sm.Init(PlayMode::kRimming);
    sm.MidiNoteOn(Source::kMidiUsb, 3, 100);
    sm.SetMode(PlayMode::kSplit);
    ok = sm.PadOn(3);
    const int origin = sm.MidiNoteOff(Source::kMidiUsb, 3);
    ok = ok && !sm.PadOn(3)
         && origin == static_cast<int>(PlayMode::kRimming);
    Check(ok, "T3e Note Off after mode change releases original mode",
          "");
}

void TestSustain()
{
    // CC 64 defers the sender's Rimming releases; pedal-up clears only
    // that sender's deferrals. Strikes/Split claims ignore the pedal.
    PlayModes sm;
    sm.Init(PlayMode::kRimming);
    sm.MidiSetSustain(Source::kMidiDin, true);
    sm.MidiNoteOn(Source::kMidiDin, 5, 100);
    sm.MidiNoteOn(Source::kMidiUsb, 6, 100);
    sm.MidiNoteOff(Source::kMidiDin, 5);
    bool ok = sm.PadOn(5); // deferred by DIN's pedal
    sm.MidiNoteOff(Source::kMidiUsb, 6);
    ok = ok && !sm.PadOn(6); // USB has no pedal down
    Check(ok, "T3f sustain defers only the sender's releases", "");

    // A second source's pedal-up must not clear DIN's deferral.
    sm.MidiSetSustain(Source::kMidiUsb, true);
    sm.MidiSetSustain(Source::kMidiUsb, false);
    ok = ok && sm.PadOn(5);
    sm.MidiSetSustain(Source::kMidiDin, false);
    ok = ok && !sm.PadOn(5);
    Check(ok, "T3g pedal-up clears only its own sustain claims", "");

    // Re-pressing a sustained note reopens its own gate (the deferral
    // becomes a held claim again).
    sm.Init(PlayMode::kRimming);
    sm.MidiSetSustain(Source::kMidiDin, true);
    sm.MidiNoteOn(Source::kMidiDin, 8, 100);
    sm.MidiNoteOff(Source::kMidiDin, 8);
    ok = sm.PadOn(8);
    sm.MidiNoteOn(Source::kMidiDin, 8, 100);
    ok = ok && sm.PadOn(8);
    sm.MidiSetSustain(Source::kMidiDin, false);
    ok = ok && sm.PadOn(8); // actually held: survives pedal-up
    sm.MidiNoteOff(Source::kMidiDin, 8);
    ok = ok && !sm.PadOn(8);
    Check(ok, "T3h sustained note re-press and release", "");

    // Sustain is inert outside Rimming.
    sm.Init(PlayMode::kStrikes);
    sm.MidiSetSustain(Source::kMidiDin, true);
    sm.MidiNoteOn(Source::kMidiDin, 10, 100);
    sm.MidiNoteOff(Source::kMidiDin, 10);
    ok = sm.DownMode(Source::kMidiDin, 10) == -1; // strike claim ended
    Check(ok, "T3i pedal ignored for strikes", "");
}

void TestLatchSources()
{
    // CC 83 idempotent setter with per-source capture ownership.
    PlayModes sm;
    sm.Init(PlayMode::kRimming);
    sm.MidiNoteOn(Source::kMidiDin, 2, 100);
    sm.KeyDown(3, false);
    sm.SetLatchEnabled(true);
    sm.SetLatchEnabled(true); // idempotent: no toggle-off
    bool ok = sm.LatchEnabled() && sm.Latched(2) && sm.Latched(3);
    sm.MidiNoteOff(Source::kMidiDin, 2);
    sm.KeyUp(3);
    ok = ok && sm.PadOn(2) && sm.PadOn(3); // both latched
    Check(ok, "T3j CC 83 capture, idempotent, multi-source", "");

    // CC 123 from DIN clears DIN's latch but not the local one; the
    // global LOOP state stays.
    sm.MidiAllNotesOff(Source::kMidiDin);
    ok = ok && !sm.PadOn(2) && sm.PadOn(3) && sm.Latched(3)
         && sm.LatchEnabled();
    Check(ok, "T3k CC 123 cleans one sender's latch only", "");

    // CC 83 off drops the remaining latched-only notes.
    sm.SetLatchEnabled(false);
    ok = ok && !sm.LatchEnabled() && !sm.PadOn(3);
    Check(ok, "T3l CC 83 off drops latched-only claims", "");

    // CC 83 is inert outside Rimming.
    sm.Init(PlayMode::kStrikes);
    sm.SetLatchEnabled(true);
    Check(!sm.LatchEnabled(), "T3m CC 83 inert outside Rimming", "");
}

void TestResetAndPanic()
{
    // CC 121: sustain/rub reset but held notes keep sounding.
    PlayModes sm;
    sm.Init(PlayMode::kRimming);
    sm.MidiSetSustain(Source::kMidiDin, true);
    sm.MidiNoteOn(Source::kMidiDin, 4, 100);
    sm.MidiNoteOn(Source::kMidiDin, 5, 100);
    sm.MidiNoteOff(Source::kMidiDin, 5); // deferred
    bool ok = sm.PadOn(5);
    sm.MidiResetControllers(Source::kMidiDin);
    ok = ok && !sm.PadOn(5)     // deferred claim cleared
         && sm.PadOn(4);        // actually held note survives
    Check(ok, "T3n CC 121 clears deferrals, keeps held notes", "");

    // CC 123 ignores sustain (it cannot leave stuck pads).
    sm.Init(PlayMode::kRimming);
    sm.MidiSetSustain(Source::kMidiDin, true);
    sm.MidiNoteOn(Source::kMidiDin, 6, 100);
    sm.MidiNoteOff(Source::kMidiDin, 6);
    ok = sm.PadOn(6);
    sm.MidiAllNotesOff(Source::kMidiDin);
    ok = ok && !sm.PadOn(6);
    Check(ok, "T3o CC 123 ignores the pedal", "");

    // CC 120: everything clears (claims, latch, rim set, pedals), the
    // mode is preserved, and a still-held note cannot revive.
    sm.Init(PlayMode::kRimming);
    sm.SetLatchEnabled(true);
    sm.MidiNoteOn(Source::kMidiDin, 1, 100);
    sm.KeyDown(2, false);
    sm.MidiSetSustain(Source::kMidiUsb, true);
    sm.MidiPanic();
    ok = !sm.PadOn(1) && !sm.PadOn(2) && !sm.LatchEnabled()
         && sm.Mode() == PlayMode::kRimming
         && sm.DownMode(Source::kMidiDin, 1) == -1
         && sm.DownMode(Source::kLocal, 2) == -1;
    sm.KeyUp(2); // a release after the panic is a harmless no-op
    ok = ok && !sm.PadOn(2);
    Check(ok, "T3p CC 120 clears all claims, keeps the mode", "");
}

void TestSplitSources()
{
    // Split rim selection is silent selection (never a strike), with
    // the shared press-ordered max-2 set across sources.
    PlayModes sm;
    sm.Init(PlayMode::kSplit);
    KeyAction a = sm.MidiNoteOn(Source::kMidiDin, 0, 100);
    bool ok = a.kind == KeyAction::kRimSelect && a.key == 0;
    a = sm.KeyDown(1, false); // local joins
    ok = ok && a.kind == KeyAction::kRimSelect
         && sm.RimHeldCount() == 2;
    // A third held rim key (USB) evicts the oldest (key 0) - and
    // DIN's later Note Off for the evicted key is a harmless no-op.
    a = sm.MidiNoteOn(Source::kMidiUsb, 2, 100);
    ok = ok && sm.RimHeldCount() == 2 && sm.RimHeld(0) == 1
         && sm.RimHeld(1) == 2;
    sm.MidiNoteOff(Source::kMidiDin, 0);
    ok = ok && sm.RimHeldCount() == 2;
    Check(ok, "T3q Split: shared max-2 set, cross-source eviction", "");

    // Per-sender CC 1 eligibility: DIN holds nothing -> its own
    // fallback (its last selection); USB holds key 2.
    ok = sm.MidiRubTargets(Source::kMidiUsb, 2)
         && !sm.MidiRubTargets(Source::kMidiUsb, 1)
         && sm.MidiRubTargets(Source::kMidiDin, 0)  // DIN's fallback
         && !sm.MidiRubTargets(Source::kMidiDin, 2);
    // The local wheel drives the combined held set.
    ok = ok && sm.WheelTargets(1) && sm.WheelTargets(2)
         && !sm.WheelTargets(0);
    Check(ok, "T3r Split per-sender rub eligibility + wheel set", "");

    // Releasing all of a source's rim keys leaves its fallback.
    sm.MidiNoteOff(Source::kMidiUsb, 2);
    ok = sm.MidiRubTargets(Source::kMidiUsb, 2) // USB fallback
         && sm.WheelTargets(1);                 // wheel: key 1 held
    Check(ok, "T3s Split fallback survives Note Off", "");

    // CC 123 clears the sender's fallback (DIN selected key 0 last for
    // itself; USB's own fallback key 2 remains).
    sm.MidiAllNotesOff(Source::kMidiDin);
    ok = !sm.MidiRubTargets(Source::kMidiDin, 0)
         && sm.MidiRubTargets(Source::kMidiUsb, 2);
    Check(ok, "T3t CC 123 clears the sender's Split fallback", "");

    // Split strike zone strikes (keys 12..24).
    sm.Init(PlayMode::kSplit);
    a = sm.MidiNoteOn(Source::kMidiDin, 12, 100);
    ok = a.kind == KeyAction::kStrike && a.mode == PlayMode::kSplit;
    a = sm.MidiNoteOn(Source::kMidiDin, 24, 100);
    ok = ok && a.kind == KeyAction::kStrike;
    Check(ok, "T3u Split keys 60-72 strike", "");
}

void TestStealAndRubTargets()
{
    // A stolen pad clears all sources' latch/gate claims; the held
    // state is remembered (the claim marker stays) but the gate is
    // suppressed until a fresh press.
    PlayModes sm;
    sm.Init(PlayMode::kRimming);
    sm.SetLatchEnabled(true);
    sm.MidiNoteOn(Source::kMidiDin, 3, 100);
    sm.MidiNoteOff(Source::kMidiDin, 3); // latched
    bool ok = sm.PadOn(3) && sm.Latched(3);
    sm.OnVoiceStolen(PlayMode::kRimming, 3);
    ok = ok && !sm.PadOn(3) && !sm.Latched(3);
    // Merely holding CC 1 nonzero must not revive it: no rub target.
    ok = ok && !sm.MidiRubTargets(Source::kMidiDin, 3);
    // A fresh Note On revives it.
    KeyAction a = sm.MidiNoteOn(Source::kMidiDin, 3, 100);
    ok = ok && a.kind == KeyAction::kPadOn && sm.PadOn(3);
    Check(ok, "T3v steal: gate/latch cleared, fresh press revives", "");

    // CC 1 eligibility in Strikes: only the sender's OWN held strikes.
    sm.Init(PlayMode::kStrikes);
    sm.MidiNoteOn(Source::kMidiDin, 5, 100);
    sm.KeyDown(6, false);
    ok = sm.MidiRubTargets(Source::kMidiDin, 5)
         && !sm.MidiRubTargets(Source::kMidiDin, 6)
         && !sm.MidiRubTargets(Source::kMidiUsb, 5);
    // The local wheel set covers both held strikes.
    ok = ok && sm.WheelTargets(5) && sm.WheelTargets(6);
    // Note Off ends DIN's rubbing eligibility (the ring remains).
    sm.MidiNoteOff(Source::kMidiDin, 5);
    ok = ok && !sm.MidiRubTargets(Source::kMidiDin, 5);
    Check(ok, "T3w Strikes: per-sender CC 1, wheel covers all held", "");

    // Rimming CC 1: held, sustained and latched claims of the sender.
    sm.Init(PlayMode::kRimming);
    sm.MidiNoteOn(Source::kMidiDin, 7, 100);
    sm.KeyDown(8, false);
    ok = sm.MidiRubTargets(Source::kMidiDin, 7)
         && !sm.MidiRubTargets(Source::kMidiDin, 8); // local's pad
    sm.MidiSetSustain(Source::kMidiDin, true);
    sm.MidiNoteOff(Source::kMidiDin, 7);
    ok = ok && sm.MidiRubTargets(Source::kMidiDin, 7); // sustained
    Check(ok, "T3x Rimming CC 1: own held/sustained claims", "");
}

void TestMidiModeChange()
{
    // CC 80 SetMode: the same cleanup as the local shortcut - latched
    // notes release, held pads keep their gate to their own Note Off.
    PlayModes sm;
    sm.Init(PlayMode::kRimming);
    sm.SetLatchEnabled(true);
    sm.MidiNoteOn(Source::kMidiDin, 1, 100);
    sm.KeyDown(2, false);
    sm.KeyUp(2); // latched-only (local)
    bool ok = sm.PadOn(1) && sm.PadOn(2) && sm.LatchEnabled();
    ok = ok && !sm.SetMode(PlayMode::kRimming); // idempotent
    ok = ok && sm.SetMode(PlayMode::kSplit)
         && sm.Mode() == PlayMode::kSplit;
    ok = ok && !sm.LatchEnabled() && !sm.PadOn(2) // latched-only out
         && sm.PadOn(1);                          // held pad persists
    sm.MidiNoteOff(Source::kMidiDin, 1);
    ok = ok && !sm.PadOn(1);
    Check(ok, "T3y CC 80 mode change cleanup + idempotence", "");

    // MIDI notes never inherit local SHIFT semantics: the shortcut
    // keys play/select normally over MIDI, and a MIDI mode change
    // does not consume any key.
    sm.Init(PlayMode::kStrikes);
    KeyAction a = sm.MidiNoteOn(Source::kMidiUsb, 6, 100); // F#3
    ok = a.kind == KeyAction::kStrike && a.key == 6;
    a = sm.MidiNoteOn(Source::kMidiUsb, 8, 100); // G#3
    ok = ok && a.kind == KeyAction::kStrike;
    a = sm.MidiNoteOn(Source::kMidiUsb, 10, 100); // A#3
    ok = ok && a.kind == KeyAction::kStrike;
    Check(ok, "T3z MIDI notes never trigger mode shortcuts", "");
}

// -- T4: rub arbitration ----------------------------------------------------

void TestArbitration()
{
    using bowls::ArbitrateRub;
    using bowls::RubCandidate;
    // Highest level wins (never the sum) and its texture applies;
    // the outcome does not depend on the argument order.
    RubCandidate wheel{0.5f, false, 10};
    RubCandidate midi{0.8f, true, 5};
    RubCandidate r = ArbitrateRub(wheel, midi);
    bool ok = r.level == 0.8f && r.clean;
    r = ArbitrateRub(midi, wheel);
    ok = ok && r.level == 0.8f && r.clean;
    // Equal levels: the LATEST control event wins (sequence order),
    // again regardless of the argument order.
    RubCandidate a{0.6f, false, 7};
    RubCandidate b{0.6f, true, 9};
    r = ArbitrateRub(a, b);
    ok = ok && r.clean;
    r = ArbitrateRub(b, a);
    ok = ok && r.clean;
    Check(ok, "T4a max level (not sum), texture, tie -> latest", "");
}

} // namespace

int main()
{
    std::printf("CHOMPI-BOWLS MIDI host tests\n");
    TestNoteMap();
    TestTickConversion();
    TestSelectorConversions();
    TestQueueFifo();
    TestQueueOverflowCleanup();
    TestFairDispatch();
    TestMultiSourcePads();
    TestCrossModeReplay();
    TestSustain();
    TestLatchSources();
    TestResetAndPanic();
    TestSplitSources();
    TestStealAndRubTargets();
    TestMidiModeChange();
    TestArbitration();
    std::printf(g_failures == 0 ? "\nALL PASS\n" : "\n%d FAILURES\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
