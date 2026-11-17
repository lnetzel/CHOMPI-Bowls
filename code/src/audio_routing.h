#pragma once

// Audio routing selection for normal monitoring or an external stereo
// send-FX loop.
//
//   Normal  (boot default): the bowl bus
//           x master gain goes to BOTH the headphones (out 0/1) and
//           the master line out (out 2/3). AUX IN is not monitored.
//   Send FX: the line out carries only the bowl bus, pre-master-gain,
//           at fixed unity - a send to an external effects unit; the
//           headphones carry only the AUX IN return x master gain
//           (the volume knob is the return level). The AUX return is
//           NEVER sent back out the line out, so a feedback loop
//           through the FX unit is impossible. With nothing patched
//           into AUX IN the headphones are silent - by design: the
//           line out keeps sending the dry bowls.
//
// Selection: SHIFT + Black 6 (C#4, key 13) = Normal; SHIFT + Black 7
// (D#4, key 15) = Send FX. Routing is session-only and resets to Normal
// at boot.
//
// Hardware-independent; the host tests use this same implementation.

#include "bowl_scale.h" // kModeShortcutKey / kKeyCount for the guards

namespace bowls
{
/** Output routings (plain int storage in the firmware). */
enum AudioRouting : int
{
    kRoutingNormal = 0, // bus x master gain to headphones AND line out
    kRoutingSendFx,     // line = dry bus send, headphones = AUX return
    kNumRoutings
};

constexpr int kRoutingDefault = kRoutingNormal;

/** Selection keys in keyboard pitch order (0 = C3 .. 24 = C5): the
 *  6th black key from the left (C#4, key 13) selects Normal, the 7th
 *  (D#4, key 15) selects Send FX. */
constexpr int kRoutingSelectKey[kNumRoutings] = {13, 15};

// The routing keys must be playable keys that no play-mode shortcut
// uses (guard against a future keyboard-layout change).
static_assert(kRoutingSelectKey[0] >= 0 && kRoutingSelectKey[0] < kKeyCount,
              "routing key out of range");
static_assert(kRoutingSelectKey[1] >= 0 && kRoutingSelectKey[1] < kKeyCount,
              "routing key out of range");
static_assert(kRoutingSelectKey[0] != kModeShortcutKey[0]
                  && kRoutingSelectKey[0] != kModeShortcutKey[1]
                  && kRoutingSelectKey[0] != kModeShortcutKey[2],
              "routing key collides with a play-mode shortcut");
static_assert(kRoutingSelectKey[1] != kModeShortcutKey[0]
                  && kRoutingSelectKey[1] != kModeShortcutKey[1]
                  && kRoutingSelectKey[1] != kModeShortcutKey[2],
              "routing key collides with a play-mode shortcut");

/** Is `key` one of the two routing selection keys? */
constexpr bool IsRoutingSelectKey(int key)
{
    return key == kRoutingSelectKey[kRoutingNormal]
           || key == kRoutingSelectKey[kRoutingSendFx];
}

/** New routing after a SHIFT + key press; any other key keeps `cur`. */
constexpr int RoutingAfterKey(int cur, int key)
{
    return key == kRoutingSelectKey[kRoutingNormal]
               ? kRoutingNormal
               : key == kRoutingSelectKey[kRoutingSendFx]
                     ? kRoutingSendFx
                     : cur;
}

/** The selection key of `routing` (the pink indicator LED target). */
constexpr int RoutingSelectKey(int routing)
{
    return routing == kRoutingSendFx ? kRoutingSelectKey[kRoutingSendFx]
                                     : kRoutingSelectKey[kRoutingNormal];
}

/** Pink indicator color (normalized; the caller scales it). */
constexpr float kRoutingColorR = 1.0f;
constexpr float kRoutingColorG = 0.0f;
constexpr float kRoutingColorB = 0.5f;

/** Per-sample output distribution. `bus_*` is the bowl bus after the
 *  reverb dry/wet mix, BEFORE the master gain; `aux_*` is the AUX IN
 *  return sample; `master_gain` is the smoothed volume-knob gain. The
 *  four results are pre-softclip levels - the caller applies the
 *  codec-boundary SoftClip. */
inline void RouteOutputs(int   routing,
                         float bus_l,
                         float bus_r,
                         float aux_l,
                         float aux_r,
                         float master_gain,
                         float& hp_l,
                         float& hp_r,
                         float& line_l,
                         float& line_r)
{
    if(routing == kRoutingSendFx)
    {
        // Headphones: the AUX return only - the volume knob is the
        // return level. Line out: the bowl bus at fixed unity
        // (pre-master-gain); the return never leaves through the
        // line out (no feedback loop).
        hp_l   = aux_l * master_gain;
        hp_r   = aux_r * master_gain;
        line_l = bus_l;
        line_r = bus_r;
    }
    else
    {
        // Normal: the same signal on both output pairs (v1 behavior).
        hp_l = line_l = bus_l * master_gain;
        hp_r = line_r = bus_r * master_gain;
    }
}

} // namespace bowls
