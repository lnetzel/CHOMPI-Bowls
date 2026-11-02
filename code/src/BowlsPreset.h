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
    static constexpr int kVersion = 2; /* v2 adds the per-bowl pan array */

    /* field ranges (mirror the tick counts in chompi_main.cpp) */
    static constexpr int kMaxTicks   = 32; /* 0..32 tick parameters */
    static constexpr int kMaxProfile = 4;  /* bowl profiles 0..4 */
    static constexpr int kWhiteKeys  = 15; /* one pan value per bowl key */

    /* boot defaults (mirror Params in chompi_main.cpp) */
    static constexpr int kDefaultVolume     = 16;
    static constexpr int kDefaultStrike     = 16;
    static constexpr int kDefaultBrightness = 16;
    static constexpr int kDefaultDecay      = 16;
    static constexpr int kDefaultShimmer    = 8;
    static constexpr int kDefaultProfile    = 0;
    static constexpr int kDefaultPan        = 16; /* centre */

    uint8_t volume;     /**< 0..32 master volume ticks */
    uint8_t strike;     /**< 0..32 strike hardness ticks */
    uint8_t brightness; /**< 0..32 high-mode excitation ticks */
    uint8_t decay;      /**< 0..32 decay multiplier ticks */
    uint8_t shimmer;    /**< 0..32 paired-mode detune ticks */
    uint8_t profile;    /**< 0..4 selected bowl profile */
    uint8_t pan[kWhiteKeys]; /**< per-bowl pan 0..32 (16 = centre) */

    void SetDefaults()
    {
        volume     = kDefaultVolume;
        strike     = kDefaultStrike;
        brightness = kDefaultBrightness;
        decay      = kDefaultDecay;
        shimmer    = kDefaultShimmer;
        profile    = kDefaultProfile;
        for(int w = 0; w < kWhiteKeys; ++w)
            pan[w] = kDefaultPan;
    }
};

/** Serialize as compact versioned JSON. Returns the number of bytes
 *  written (excluding the terminator), or 0 when the buffer is too
 *  small. */
inline size_t Serialize(const BowlsPreset &p, char *buf, size_t size)
{
    int n = snprintf(buf,
                     size,
                     "{\"version\":%d,"
                     "\"volume\":%u,\"strike\":%u,\"brightness\":%u,"
                     "\"decay\":%u,\"shimmer\":%u,\"profile\":%u,\"pan\":[",
                     BowlsPreset::kVersion,
                     (unsigned)p.volume,
                     (unsigned)p.strike,
                     (unsigned)p.brightness,
                     (unsigned)p.decay,
                     (unsigned)p.shimmer,
                     (unsigned)p.profile);
    if(n < 0 || static_cast<size_t>(n) >= size)
        return 0;
    size_t off = static_cast<size_t>(n);
    for(int w = 0; w < BowlsPreset::kWhiteKeys; ++w)
    {
        n = snprintf(buf + off, size - off, "%s%u", w == 0 ? "" : ",",
                     (unsigned)p.pan[w]);
        if(n < 0 || static_cast<size_t>(n) >= size - off)
            return 0;
        off += static_cast<size_t>(n);
    }
    n = snprintf(buf + off, size - off, "]}");
    if(n < 0 || static_cast<size_t>(n) >= size - off)
        return 0;
    off += static_cast<size_t>(n);
    return off;
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

/** Parse a state document. Returns false for invalid JSON or an
 *  unsupported schema version (caller keeps its defaults). */
inline bool Parse(char *buf, size_t len, BowlsPreset &p)
{
    if(JSON_Validate(buf, len) != JSONSuccess)
        return false;
    const int version
        = QueryInt(buf, len, "version", 0, 0, BowlsPreset::kVersion);
    if(version != BowlsPreset::kVersion)
        return false; /* unknown schema: do not misread it */
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

    /* pan array: "[v0,..,v14]" - walk the numbers with strtol; a missing
     * or short array keeps the defaults for the remaining keys */
    char * pan_value = nullptr;
    size_t pan_len   = 0;
    if(JSON_Search(buf, len, "pan", 3, &pan_value, &pan_len) == JSONSuccess
       && pan_len >= 2 && pan_value[0] == '[')
    {
        size_t i = 1;
        for(int w = 0; w < BowlsPreset::kWhiteKeys && i < pan_len; ++w)
        {
            char *     end = nullptr;
            const long v   = strtol(pan_value + i, &end, 10);
            if(end == pan_value + i)
                break; /* no number here: keep the remaining defaults */
            p.pan[w] = static_cast<uint8_t>(
                v < 0 ? 0
                      : (v > BowlsPreset::kMaxTicks ? BowlsPreset::kMaxTicks
                                                    : v));
            i = static_cast<size_t>(end - pan_value);
            while(i < pan_len && (pan_value[i] == ',' || pan_value[i] == ' '))
                ++i;
        }
    }
    return true;
}

} // namespace chompi
