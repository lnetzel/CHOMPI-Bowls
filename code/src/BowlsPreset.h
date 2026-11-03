/** @file BowlsPreset.h
 *  @brief SD-card state model for CHOMPI-BOWLS: one versioned JSON file
 *  (BOWLS/state.json), host-testable (no Daisy dependencies).
 *
 *  All panel parameters live in a single document that is autosaved
 *  shortly after the last change and loaded at boot, so the instrument
 *  remembers its state across power cycles (spec section 1). Parsing
 *  clamps to the field ranges and fills missing keys with the boot
 *  defaults, so slightly older/newer files are tolerated; a version
 *  mismatch rejects the file.
 */
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "core_json.h"

namespace chompi
{

struct BowlsPreset
{
    /* v2: pan array; v3: reverb mix; v4: per-bowl volume + tuning;
     * v5: split keybed - per-KEY slots 15 -> 24 (rim 0-11, strike 12-23) */
    static constexpr int kVersion = 5;

    /* field ranges (mirror the tick counts in chompi_main.cpp) */
    static constexpr int kMaxTicks   = 32; /* 0..32 tick parameters */
    static constexpr int kMaxProfile = 4;  /* bowl profiles 0..4 */
    static constexpr int kKeySlots   = 24; /* rim keys 0-11 + strike 12-23 */

    /* boot defaults (mirror Params in chompi_main.cpp) */
    static constexpr int kDefaultVolume     = 16;
    static constexpr int kDefaultStrike     = 16;
    static constexpr int kDefaultBrightness = 16;
    static constexpr int kDefaultDecay      = 16;
    static constexpr int kDefaultShimmer    = 8;
    static constexpr int kDefaultProfile    = 0;
    static constexpr int kDefaultPan        = 16; /* centre */
    static constexpr int kDefaultReverbMix  = 8;  /* subtle temple room */
    static constexpr int kDefaultBowlVol    = 16; /* unity gain (0 dB) */
    static constexpr int kDefaultTune       = 16; /* unison (ratio 1) */

    uint8_t volume;     /**< 0..32 master volume ticks */
    uint8_t strike;     /**< 0..32 strike hardness ticks */
    uint8_t brightness; /**< 0..32 high-mode excitation ticks */
    uint8_t decay;      /**< 0..32 decay multiplier ticks */
    uint8_t shimmer;    /**< 0..32 paired-mode detune ticks */
    uint8_t profile;    /**< 0..4 selected bowl profile */
    uint8_t pan[kKeySlots];  /**< per-key pan 0..32 (16 = centre) */
    uint8_t reverb_mix; /**< 0..32 reverb dry/wet mix ticks */
    uint8_t bvol[kKeySlots]; /**< per-key volume 0..32 (16 = unity) */
    uint8_t tune[kKeySlots]; /**< per-key tuning 0..32 (16 = unison) */

    void SetDefaults()
    {
        volume     = kDefaultVolume;
        strike     = kDefaultStrike;
        brightness = kDefaultBrightness;
        decay      = kDefaultDecay;
        shimmer    = kDefaultShimmer;
        profile    = kDefaultProfile;
        reverb_mix = kDefaultReverbMix;
        for(int w = 0; w < kKeySlots; ++w)
        {
            pan[w]  = kDefaultPan;
            bvol[w] = kDefaultBowlVol;
            tune[w] = kDefaultTune;
        }
    }
};

/** Append one `,"name":[v0,..,v23]` array at `off` (a scalar field
 *  always precedes the arrays, hence the leading comma). Returns the
 *  new offset, or 0 when the buffer is too small. */
inline size_t AppendArray(char *         buf,
                          size_t         size,
                          size_t         off,
                          const char *   name,
                          const uint8_t *values)
{
    int n = snprintf(buf + off, size - off, ",\"%s\":[", name);
    if(n < 0 || static_cast<size_t>(n) >= size - off)
        return 0;
    off += static_cast<size_t>(n);
    for(int w = 0; w < BowlsPreset::kKeySlots; ++w)
    {
        n = snprintf(buf + off, size - off, "%s%u", w == 0 ? "" : ",",
                     (unsigned)values[w]);
        if(n < 0 || static_cast<size_t>(n) >= size - off)
            return 0;
        off += static_cast<size_t>(n);
    }
    n = snprintf(buf + off, size - off, "]");
    if(n < 0 || static_cast<size_t>(n) >= size - off)
        return 0;
    return off + static_cast<size_t>(n);
}

/** Serialize as compact versioned JSON. Returns the number of bytes
 *  written (excluding the terminator), or 0 when the buffer is too
 *  small. */
inline size_t Serialize(const BowlsPreset &p, char *buf, size_t size)
{
    int n = snprintf(buf,
                     size,
                     "{\"version\":%d,"
                     "\"volume\":%u,\"strike\":%u,\"brightness\":%u,"
                     "\"decay\":%u,\"shimmer\":%u,\"profile\":%u,"
                     "\"reverb_mix\":%u",
                     BowlsPreset::kVersion,
                     (unsigned)p.volume,
                     (unsigned)p.strike,
                     (unsigned)p.brightness,
                     (unsigned)p.decay,
                     (unsigned)p.shimmer,
                     (unsigned)p.profile,
                     (unsigned)p.reverb_mix);
    if(n < 0 || static_cast<size_t>(n) >= size)
        return 0;
    size_t off = static_cast<size_t>(n);
    off        = AppendArray(buf, size, off, "pan", p.pan);
    if(off != 0)
        off = AppendArray(buf, size, off, "bvol", p.bvol);
    if(off != 0)
        off = AppendArray(buf, size, off, "tune", p.tune);
    if(off == 0)
        return 0;
    n = snprintf(buf + off, size - off, "}");
    if(n < 0 || static_cast<size_t>(n) >= size - off)
        return 0;
    return off + static_cast<size_t>(n);
}

/** Read one integer value for `query`, clamped to [lo, hi]. Missing
 *  keys and non-numeric values yield `fallback`. The buffer is non-const
 *  because coreJSON's JSON_Search takes a char* (it never writes). */
inline int QueryInt(char *      buf,
                    size_t      len,
                    const char *query,
                    int         fallback,
                    int         lo,
                    int         hi)
{
    char * value     = nullptr;
    size_t value_len = 0;
    if(JSON_Search(buf, len, query, strlen(query), &value, &value_len)
       != JSONSuccess)
        return fallback;
    /* the value is not null-terminated: copy it out for strtol */
    char tmp[12];
    if(value_len >= sizeof(tmp))
        return fallback;
    memcpy(tmp, value, value_len);
    tmp[value_len] = '\0';
    const long v = strtol(tmp, nullptr, 10);
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return static_cast<int>(v);
}

/** Read one `"name":[v0,..]` array into dest, each value clamped to
 *  0..kMaxTicks. A missing or short array keeps the existing (default)
 *  values for the remaining keys. The buffer is non-const because
 *  coreJSON's JSON_Search takes a char* (it never writes). */
inline void QueryArray(char *     buf,
                       size_t     len,
                       const char *name,
                       uint8_t *  dest,
                       int        count)
{
    char * value     = nullptr;
    size_t value_len = 0;
    if(JSON_Search(buf, len, name, strlen(name), &value, &value_len)
           != JSONSuccess
       || value_len < 2 || value[0] != '[')
        return;
    size_t i = 1;
    for(int w = 0; w < count && i < value_len; ++w)
    {
        char *     end = nullptr;
        const long v   = strtol(value + i, &end, 10);
        if(end == value + i)
            break; /* no number here: keep the remaining defaults */
        dest[w] = static_cast<uint8_t>(
            v < 0 ? 0
                  : (v > BowlsPreset::kMaxTicks ? BowlsPreset::kMaxTicks
                                                : v));
        i = static_cast<size_t>(end - value);
        while(i < value_len && (value[i] == ',' || value[i] == ' '))
            ++i;
    }
}

/** Parse a state document. Returns false for invalid JSON or a NEWER
 *  schema version; older files load with defaults for missing keys. */
inline bool Parse(char *buf, size_t len, BowlsPreset &p)
{
    if(JSON_Validate(buf, len) != JSONSuccess)
        return false;
    const int version = QueryInt(buf, len, "version", 0, 0, 100);
    if(version < 1 || version > BowlsPreset::kVersion)
        return false; /* from a newer firmware: do not misread it */
    p.SetDefaults();
    p.volume = QueryInt(buf, len, "volume", BowlsPreset::kDefaultVolume,
                        0, BowlsPreset::kMaxTicks);
    p.strike = QueryInt(buf, len, "strike", BowlsPreset::kDefaultStrike,
                        0, BowlsPreset::kMaxTicks);
    p.brightness = QueryInt(buf, len, "brightness",
                            BowlsPreset::kDefaultBrightness, 0,
                            BowlsPreset::kMaxTicks);
    p.decay = QueryInt(buf, len, "decay", BowlsPreset::kDefaultDecay, 0,
                       BowlsPreset::kMaxTicks);
    p.shimmer = QueryInt(buf, len, "shimmer", BowlsPreset::kDefaultShimmer,
                         0, BowlsPreset::kMaxTicks);
    p.profile = QueryInt(buf, len, "profile", BowlsPreset::kDefaultProfile,
                         0, BowlsPreset::kMaxProfile);
    p.reverb_mix = QueryInt(buf, len, "reverb_mix",
                            BowlsPreset::kDefaultReverbMix, 0,
                            BowlsPreset::kMaxTicks);

    /* per-key arrays: "[v0,..,v23]" - a missing or short array keeps
     * the defaults for the remaining keys. v4-and-older files hold 15
     * white-key entries (one bowl per white key): load those into the
     * rim keys, then copy bowls 0-11 onto the strike keys (12-23) so
     * both halves inherit the bowl's old settings; the three dropped
     * high bowls (old entries 12-14) are discarded. */
    const int slots = version >= 5 ? BowlsPreset::kKeySlots : 15;
    QueryArray(buf, len, "pan", p.pan, slots);
    QueryArray(buf, len, "bvol", p.bvol, slots);
    QueryArray(buf, len, "tune", p.tune, slots);
    if(version < 5)
        for(int k = 0; k < 12; ++k)
        {
            p.pan[12 + k]  = p.pan[k];
            p.bvol[12 + k] = p.bvol[k];
            p.tune[12 + k] = p.tune[k];
        }
    return true;
}

} // namespace chompi
