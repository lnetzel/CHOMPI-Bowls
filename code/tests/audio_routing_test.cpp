// Host-side tests for the audio routing selection (audio_routing.h).
// Not part of the firmware build; run with
// `make -C code/tests routing-test` from the MSYS2 UCRT64 shell.
//
//   R1  key mapping: Black 6 (C#4, key 13) -> Normal, Black 7 (D#4,
//       key 15) -> Send FX, from any current routing
//   R2  inert keys: every other key (white keys, the other black keys,
//       the mode-shortcut keys 6/8/10) and out-of-range values keep
//       the current routing; the selection persists across them
//   R3  Normal distribution: the bus x master gain reaches all four
//       outputs; AUX IN goes nowhere
//   R4  Send FX distribution: line out = the dry bus at fixed unity
//       (master-gain-independent), headphones = AUX x master gain;
//       the bus never reaches the headphones and the AUX return never
//       reaches the line out (no feedback loop)
//   R5  the Send FX line send equals the Normal line out at maximum
//       master gain (the plan's level convention)

#include "audio_routing.h"

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

bool FloatEq(float a, float b)
{
    const float d = a - b;
    return d < 0.0001f && d > -0.0001f;
}

// -- R1/R2: key mapping -------------------------------------------------

void TestKeyMapping()
{
    // Both selection keys, from both possible current routings.
    bool ok = bowls::RoutingAfterKey(bowls::kRoutingNormal, 13)
                  == bowls::kRoutingNormal
              && bowls::RoutingAfterKey(bowls::kRoutingSendFx, 13)
                     == bowls::kRoutingNormal
              && bowls::RoutingAfterKey(bowls::kRoutingNormal, 15)
                     == bowls::kRoutingSendFx
              && bowls::RoutingAfterKey(bowls::kRoutingSendFx, 15)
                     == bowls::kRoutingSendFx;
    Check(ok, "R1 Black 6/7 select Normal/Send FX", "");

    ok = bowls::IsRoutingSelectKey(13) && bowls::IsRoutingSelectKey(15);
    Check(ok, "R1b IsRoutingSelectKey on the selection keys", "");

    // Every in-range key except 13/15 is inert (this covers the white
    // keys, the unused black keys 1/3/18/20/22, and the mode-shortcut
    // keys 6/8/10), from both current routings; out-of-range too.
    ok = true;
    for(int key = -1; key <= bowls::kKeyCount; ++key)
    {
        if(key == 13 || key == 15)
            continue;
        if(bowls::IsRoutingSelectKey(key))
            ok = false;
        if(bowls::RoutingAfterKey(bowls::kRoutingNormal, key)
           != bowls::kRoutingNormal)
            ok = false;
        if(bowls::RoutingAfterKey(bowls::kRoutingSendFx, key)
           != bowls::kRoutingSendFx)
            ok = false;
    }
    Check(ok, "R2 all other keys keep the current routing", "");

    // The selection persists across unrelated keys pressed afterwards.
    int cur = bowls::RoutingAfterKey(bowls::kRoutingDefault, 15);
    cur     = bowls::RoutingAfterKey(cur, 0);  // C3
    cur     = bowls::RoutingAfterKey(cur, 6);  // Strikes shortcut key
    cur     = bowls::RoutingAfterKey(cur, 24); // C5
    Check(cur == bowls::kRoutingSendFx,
          "R2b selection persists across unrelated keys",
          "");

    // The indicator key is the selection key of the active routing.
    ok = bowls::RoutingSelectKey(bowls::kRoutingNormal) == 13
         && bowls::RoutingSelectKey(bowls::kRoutingSendFx) == 15
         && bowls::RoutingSelectKey(
                bowls::RoutingAfterKey(bowls::kRoutingNormal, 15))
                == 15;
    Check(ok, "R2c RoutingSelectKey is the inverse mapping", "");

    // Boot default is Normal.
    Check(bowls::kRoutingDefault == bowls::kRoutingNormal,
          "R2d boot default is Normal",
          "");
}

// -- R3/R4/R5: output distribution ---------------------------------------

struct Outs
{
    float hp_l, hp_r, line_l, line_r;
};

Outs Route(int routing, float bus_l, float bus_r, float aux_l,
           float aux_r, float gain)
{
    Outs o;
    bowls::RouteOutputs(routing,
                        bus_l,
                        bus_r,
                        aux_l,
                        aux_r,
                        gain,
                        o.hp_l,
                        o.hp_r,
                        o.line_l,
                        o.line_r);
    return o;
}

void TestDistribution()
{
    // Normal: bus x gain to all four outputs; AUX goes nowhere.
    const Outs n = Route(bowls::kRoutingNormal, 0.4f, -0.3f, 0.9f, -0.9f, 0.5f);
    bool       ok = FloatEq(n.hp_l, 0.2f) && FloatEq(n.hp_r, -0.15f)
              && FloatEq(n.line_l, 0.2f) && FloatEq(n.line_r, -0.15f);
    Check(ok, "R3 Normal: bus x gain on all four outs", "");

    // Send FX: line = the dry bus at unity, hp = AUX x gain; neither
    // side leaks into the other.
    const Outs s = Route(bowls::kRoutingSendFx, 0.4f, -0.3f, 0.9f, -0.9f, 0.5f);
    ok            = FloatEq(s.line_l, 0.4f) && FloatEq(s.line_r, -0.3f)
              && FloatEq(s.hp_l, 0.45f) && FloatEq(s.hp_r, -0.45f);
    Check(ok, "R4 Send FX: line = bus, hp = AUX x gain", "");

    // Silence checks: with the AUX unplugged (aux = 0) the headphones
    // are silent while the line keeps sending; with no notes playing
    // (bus = 0) the line sends silence and no AUX leaks into it.
    const Outs unpatched = Route(bowls::kRoutingSendFx, 0.4f, -0.3f, 0.0f, 0.0f, 1.0f);
    ok = FloatEq(unpatched.hp_l, 0.0f) && FloatEq(unpatched.hp_r, 0.0f)
         && FloatEq(unpatched.line_l, 0.4f)
         && FloatEq(unpatched.line_r, -0.3f);
    Check(ok, "R4b AUX unplugged: silent hp, dry send stays", "");

    const Outs no_notes = Route(bowls::kRoutingSendFx, 0.0f, 0.0f, 0.9f, -0.9f, 1.0f);
    ok = FloatEq(no_notes.line_l, 0.0f) && FloatEq(no_notes.line_r, 0.0f)
         && FloatEq(no_notes.hp_l, 0.9f)
         && FloatEq(no_notes.hp_r, -0.9f);
    Check(ok, "R4c no notes: silent send, return unaffected", "");

    // The send level must not move with the volume knob.
    ok = true;
    float ref_l = 0.0f, ref_r = 0.0f;
    for(int g = 0; g <= 8; ++g)
    {
        const float gain = static_cast<float>(g) * 0.125f; // 0..1
        const Outs  o = Route(bowls::kRoutingSendFx, 0.4f, -0.3f, 0.9f, -0.9f, gain);
        if(g == 0)
        {
            ref_l = o.line_l;
            ref_r = o.line_r;
        }
        else if(!FloatEq(o.line_l, ref_l) || !FloatEq(o.line_r, ref_r))
            ok = false;
        // ...while the headphone return tracks the knob.
        if(!FloatEq(o.hp_l, 0.9f * gain))
            ok = false;
    }
    Check(ok, "R5 send gain-independent, return tracks knob", "");

    // Level convention: the Send FX line out equals the Normal line
    // out with the volume knob at maximum (gain = 1).
    const Outs fx_max = Route(bowls::kRoutingSendFx, 0.4f, -0.3f, 0.9f, -0.9f, 1.0f);
    const Outs nm_max = Route(bowls::kRoutingNormal, 0.4f, -0.3f, 0.9f, -0.9f, 1.0f);
    ok = FloatEq(fx_max.line_l, nm_max.line_l)
         && FloatEq(fx_max.line_r, nm_max.line_r);
    Check(ok, "R5b send = Normal line out at max volume", "");
}

} // namespace

int main()
{
    TestKeyMapping();
    TestDistribution();
    std::printf("%s (%d failures)\n",
                g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
