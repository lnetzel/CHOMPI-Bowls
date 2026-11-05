/** @file BowlsPreset.h
 *  @brief SD-card state model for CHOMPI-BOWLS: one versioned JSON file
 *  (BOWLS/state.json), host-testable (no Daisy dependencies).
 *
 *  All panel parameters live in a single document that is autosaved
 *  shortly after the last change and loaded at boot, so the instrument
 *  remembers its state across power cycles (spec section 1).
 *
 *  Schema v7: the active play mode, four global controls, and six
 *  per-mode/per-key banks of 75 entries each (3 modes x 25 keys),
 *  flattened with index = mode * 25 + key. Latch and performance state
 *  are transient and never saved. v1-v6 files are deliberately NOT
 *  imported: a version mismatch (or any corruption) boots factory
 *  defaults in Strikes, and the first successful v7 save replaces the
 *  old file.
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
     * v5: split keybed - per-KEY slots 15 -> 24 (rim 0-11, strike 12-23);
     * v6: play modes - mode field + per-mode/per-key banks of 25 slots
     * (3 modes x 25 keys = 75), profile becomes per-bowl per-mode;
     * v7: brightness and decay move from globals to per-mode/per-key
     * banks (six banks of 75). */
    static constexpr int kVersion = 7;

    /* field ranges (mirror the tick counts in chompi_main.cpp) */
    static constexpr int kMaxTicks   = 32; /* 0..32 tick parameters */
    static constexpr int kMaxProfile = 4;  /* bowl profiles 0..4 */
    static constexpr int kKeySlots   = 25; /* keys per mode */
    static constexpr int kModeCount  = 3;  /* Strikes / Rimming / Split */
    static constexpr int kBankSlots  = kModeCount * kKeySlots; /* 75 */

    /* play modes (mirror bowls::PlayMode in bowl_scale.h) */
    static constexpr int kModeStrikes = 0;
    static constexpr int kModeRimming = 1;
    static constexpr int kModeSplit   = 2;

    /* boot defaults (mirror Params in chompi_main.cpp) */
    static constexpr int kDefaultMode       = kModeStrikes;
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

    uint8_t mode;       /**< 0..2 active play mode */
    uint8_t volume;     /**< 0..32 master volume ticks */
    uint8_t strike;     /**< 0..32 strike hardness ticks */
    uint8_t shimmer;    /**< 0..32 paired-mode detune ticks */
    uint8_t reverb_mix; /**< 0..32 reverb dry/wet mix ticks */
    /* per-mode/per-key banks, flattened [mode * 25 + key] */
    uint8_t pan[kBankSlots];     /**< per-key pan 0..32 (16 = centre) */
    uint8_t bvol[kBankSlots];    /**< per-key volume 0..32 (16 = unity) */
    uint8_t tune[kBankSlots];    /**< per-key tuning 0..32 (16 = unison) */
    uint8_t profile[kBankSlots]; /**< per-key bowl profile 0..4 */
    uint8_t bright[kBankSlots];  /**< per-key brightness 0..32 */
    uint8_t decay[kBankSlots];   /**< per-key decay 0..32 */

    void SetDefaults()
    {
        mode       = kDefaultMode;
        volume     = kDefaultVolume;
        strike     = kDefaultStrike;
        shimmer    = kDefaultShimmer;
        reverb_mix = kDefaultReverbMix;
        for(int w = 0; w < kBankSlots; ++w)
        {
            pan[w]     = kDefaultPan;
            bvol[w]    = kDefaultBowlVol;
            tune[w]    = kDefaultTune;
            profile[w] = kDefaultProfile;
            bright[w]  = kDefaultBrightness;
            decay[w]   = kDefaultDecay;
        }
    }
};

/** Append one `,"name":[v0,..,vN-1]` array at `off` (a scalar field
 *  always precedes the arrays, hence the leading comma). Returns the
 *  new offset, or 0 when the buffer is too small. */
inline size_t AppendArray(char *         buf,
                          size_t         size,
                          size_t         off,
                          const char *   name,
                          const uint8_t *values,
                          int            count)
{
    int n = snprintf(buf + off, size - off, ",\"%s\":[", name);
    if(n < 0 || static_cast<size_t>(n) >= size - off)
        return 0;
    off += static_cast<size_t>(n);
    for(int w = 0; w < count; ++w)
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
                     "{\"version\":%d,\"mode\":%u,"
                     "\"volume\":%u,\"strike\":%u,\"shimmer\":%u,"
                     "\"reverb_mix\":%u",
                     BowlsPreset::kVersion,
                     (unsigned)p.mode,
                     (unsigned)p.volume,
                     (unsigned)p.strike,
                     (unsigned)p.shimmer,
                     (unsigned)p.reverb_mix);
    if(n < 0 || static_cast<size_t>(n) >= size)
        return 0;
    size_t off = static_cast<size_t>(n);
    off = AppendArray(buf, size, off, "pan", p.pan, BowlsPreset::kBankSlots);
    if(off != 0)
        off = AppendArray(buf, size, off, "bvol", p.bvol,
                          BowlsPreset::kBankSlots);
    if(off != 0)
        off = AppendArray(buf, size, off, "tune", p.tune,
                          BowlsPreset::kBankSlots);
    if(off != 0)
        off = AppendArray(buf, size, off, "profile", p.profile,
                          BowlsPreset::kBankSlots);
    if(off != 0)
        off = AppendArray(buf, size, off, "bright", p.bright,
                          BowlsPreset::kBankSlots);
    if(off != 0)
        off = AppendArray(buf, size, off, "decay", p.decay,
                          BowlsPreset::kBankSlots);
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

/** Strictly read one `"name":[v0,..]` array of exactly `count` values,
 *  each clamped to 0..max_value, into dest. Returns false when the
 *  array is missing, malformed, or short - the caller then rejects the
 *  whole document rather than partially applying corrupt state. The
 *  buffer is non-const because coreJSON's JSON_Search takes a char*
 *  (it never writes). */
inline bool QueryArrayStrict(char *     buf,
                             size_t     len,
                             const char *name,
                             uint8_t *  dest,
                             int        count,
                             int        max_value)
{
    char * value     = nullptr;
    size_t value_len = 0;
    if(JSON_Search(buf, len, name, strlen(name), &value, &value_len)
           != JSONSuccess
       || value_len < 2 || value[0] != '[')
        return false;
    size_t i = 1;
    for(int w = 0; w < count; ++w)
    {
        if(i >= value_len)
            return false; /* short array */
        char *     end = nullptr;
        const long v   = strtol(value + i, &end, 10);
        if(end == value + i)
            return false; /* no number here: corrupt */
        dest[w] = static_cast<uint8_t>(
            v < 0 ? 0 : (v > max_value ? max_value : v));
        i = static_cast<size_t>(end - value);
        while(i < value_len && (value[i] == ',' || value[i] == ' '))
            ++i;
    }
    return true;
}

/** Parse a state document. Only the exact current schema version is
 *  accepted: v1-v6 files (and newer files) are deliberately NOT
 *  imported - the caller keeps factory defaults and the first
 *  successful new save replaces the old file. Corrupt, truncated or
 *  partially valid documents are rejected whole, never partially
 *  applied. */
inline bool Parse(char *buf, size_t len, BowlsPreset &p)
{
    if(JSON_Validate(buf, len) != JSONSuccess)
        return false;
    const int version = QueryInt(buf, len, "version", 0, 0, 100);
    if(version != BowlsPreset::kVersion)
        return false; /* old or newer firmware: do not misread it */

    /* parse into a scratch copy first: nothing is applied unless the
     * whole document validates */
    BowlsPreset tmp;
    tmp.SetDefaults();
    const int mode = QueryInt(buf, len, "mode", -1, -1, 100);
    if(mode < 0 || mode >= BowlsPreset::kModeCount)
        return false; /* invalid mode: reject, do not clamp */
    tmp.mode = static_cast<uint8_t>(mode);
    tmp.volume = QueryInt(buf, len, "volume", BowlsPreset::kDefaultVolume,
                          0, BowlsPreset::kMaxTicks);
    tmp.strike = QueryInt(buf, len, "strike", BowlsPreset::kDefaultStrike,
                          0, BowlsPreset::kMaxTicks);
    tmp.shimmer = QueryInt(buf, len, "shimmer",
                           BowlsPreset::kDefaultShimmer, 0,
                           BowlsPreset::kMaxTicks);
    tmp.reverb_mix = QueryInt(buf, len, "reverb_mix",
                              BowlsPreset::kDefaultReverbMix, 0,
                              BowlsPreset::kMaxTicks);

    /* per-mode/per-key banks: all six arrays must hold exactly
     * kBankSlots values, or the document is corrupt */
    if(!QueryArrayStrict(buf, len, "pan", tmp.pan,
                         BowlsPreset::kBankSlots, BowlsPreset::kMaxTicks)
       || !QueryArrayStrict(buf, len, "bvol", tmp.bvol,
                            BowlsPreset::kBankSlots, BowlsPreset::kMaxTicks)
       || !QueryArrayStrict(buf, len, "tune", tmp.tune,
                            BowlsPreset::kBankSlots, BowlsPreset::kMaxTicks)
       || !QueryArrayStrict(buf, len, "profile", tmp.profile,
                            BowlsPreset::kBankSlots,
                            BowlsPreset::kMaxProfile)
       || !QueryArrayStrict(buf, len, "bright", tmp.bright,
                            BowlsPreset::kBankSlots, BowlsPreset::kMaxTicks)
       || !QueryArrayStrict(buf, len, "decay", tmp.decay,
                            BowlsPreset::kBankSlots, BowlsPreset::kMaxTicks))
        return false;
    p = tmp;
    return true;
}

} // namespace chompi
