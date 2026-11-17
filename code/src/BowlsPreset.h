/** @file BowlsPreset.h
 *  @brief SD-card preset model for CHOMPI-BOWLS: 15 versioned JSON slots,
 *  host-testable (no Daisy dependencies).
 *
 *  Schema v11 stores persistent panel parameters in per-mode objects,
 *  including each mode's per-key values and direct fundamental frequencies.
 *  Only the current schema is accepted.
 */
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cmath>
#include <cstring>
#include "bowl_scale.h"
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
    * banks (six banks of 75); v8: delay mix and time; v9: base pitches;
    * v10: direct per-mode/per-key pitches replace relative tune ticks;
     * v11: per-mode values become separate JSON objects. */
    static constexpr int kVersion = 11;

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
    static constexpr int kDefaultMode       = kModeSplit;
    static constexpr int kDefaultVolume     = 8;
    static constexpr int kDefaultStrike     = 32;
    static constexpr int kDefaultBrightness = 24;
    static constexpr int kDefaultDecay      = 0;
    static constexpr int kDefaultShimmer    = 0;
    static constexpr int kDefaultProfile    = 0;
    static constexpr int kDefaultPan        = 16; /* centre */
    static constexpr int kDefaultReverbMix  = 0;  /* subtle temple room */
    static constexpr int kDefaultDelayMix   = 0;  /* dry, 35% feedback */
    static constexpr int kDefaultDelayTime  = 15; /* 500 ms */
    static constexpr int kDefaultBowlVol    = 8; /* unity gain (0 dB) */
    static constexpr float kMinPitchHz = bowls::kMinPitchHz;
    static constexpr float kMaxPitchHz = bowls::kMaxPitchHz;

    uint8_t mode;       /**< 0..2 active play mode */
    uint8_t volume;     /**< 0..32 master volume ticks */
    uint8_t strike;     /**< 0..32 strike hardness ticks */
    uint8_t shimmer;    /**< 0..32 paired-mode detune ticks */
    uint8_t reverb_mix; /**< 0..32 reverb dry/wet mix ticks */
    uint8_t delay_mix;  /**< 0..32 combined wet/feedback control */
    uint8_t delay_time; /**< 0..32 delay time (40..1000 ms) */
    /* per-mode/per-key banks, flattened [mode * 25 + key] */
    uint8_t pan[kBankSlots];     /**< per-key pan 0..32 (16 = centre) */
    uint8_t bvol[kBankSlots];    /**< per-key volume 0..32 (16 = unity) */
    uint8_t profile[kBankSlots]; /**< per-key bowl profile 0..4 */
    uint8_t bright[kBankSlots];  /**< per-key brightness 0..32 */
    uint8_t decay[kBankSlots];   /**< per-key decay 0..32 */
    bowls::PitchTables pitch_hz; /**< final fundamental for each mode/key */

    void SetDefaults()
    {
        pitch_hz = bowls::FactoryPitchTables();
        mode       = kDefaultMode;
        volume     = kDefaultVolume;
        strike     = kDefaultStrike;
        shimmer    = kDefaultShimmer;
        reverb_mix = kDefaultReverbMix;
        delay_mix  = kDefaultDelayMix;
        delay_time = kDefaultDelayTime;
        for(int w = 0; w < kBankSlots; ++w)
        {
            pan[w]     = kDefaultPan;
            bvol[w]    = kDefaultBowlVol;
            profile[w] = kDefaultProfile;
            bright[w]  = kDefaultBrightness;
            decay[w]   = kDefaultDecay;
        }
    }
};

/** RAM cache for the 15 white-key preset slots. Performance edits do not
 *  mutate this bank; only explicit save/copy/erase operations dirty slots. */
class BowlsPresetBank
{
  public:
    static constexpr int kNumSlots = 15;

    void Init()
    {
        dirty_mask_ = 0;
        for(int slot = 0; slot < kNumSlots; ++slot)
        {
            slots_[slot].SetDefaults();
            valid_[slot] = false;
            revision_[slot] = 0;
        }
    }

    bool IsValid(int slot) const
    {
        return slot >= 0 && slot < kNumSlots && valid_[slot];
    }

    const BowlsPreset &Get(int slot) const { return slots_[slot]; }

    void Save(int slot, const BowlsPreset &preset)
    {
        if(slot < 0 || slot >= kNumSlots)
            return;
        slots_[slot] = preset;
        valid_[slot] = true;
        MarkDirty(slot);
    }

    bool Copy(int source, int destination)
    {
        if(source < 0 || source >= kNumSlots || destination < 0
           || destination >= kNumSlots || source == destination
           || !valid_[source])
            return false;
        slots_[destination] = slots_[source];
        valid_[destination] = true;
        MarkDirty(destination);
        return true;
    }

    bool Erase(int slot)
    {
        if(slot < 0 || slot >= kNumSlots || !valid_[slot])
            return false;
        valid_[slot] = false;
        MarkDirty(slot);
        return true;
    }

    void LoadSlot(int slot, const BowlsPreset &preset)
    {
        if(slot < 0 || slot >= kNumSlots)
            return;
        slots_[slot] = preset;
        valid_[slot] = true;
    }

    int NextDirtySlot() const
    {
        for(int slot = 0; slot < kNumSlots; ++slot)
            if(IsDirty(slot))
                return slot;
        return -1;
    }

    bool IsDirty(int slot) const
    {
        return slot >= 0 && slot < kNumSlots
               && (dirty_mask_ & Bit(slot)) != 0;
    }

    uint32_t Revision(int slot) const
    {
        return slot >= 0 && slot < kNumSlots ? revision_[slot] : 0;
    }

    void ClearDirtyIfRevision(int slot, uint32_t revision)
    {
        if(slot >= 0 && slot < kNumSlots && revision_[slot] == revision)
            dirty_mask_ &= ~Bit(slot);
    }

  private:
    static constexpr uint32_t Bit(int slot) { return 1u << slot; }

    void MarkDirty(int slot)
    {
        dirty_mask_ |= Bit(slot);
        ++revision_[slot];
    }

    BowlsPreset slots_[kNumSlots];
    bool        valid_[kNumSlots];
    uint32_t    dirty_mask_;
    uint32_t    revision_[kNumSlots];
};

static const char *const kPresetModeKeys[BowlsPreset::kModeCount] = {
    "strikes", "rimming", "split"};

/** Append a JSON array at `off`. Returns the new offset, or 0 when the
 *  buffer is too small. */
inline size_t AppendArrayValues(char *buf, size_t size, size_t off,
                                const uint8_t *values, int count)
{
    int n = snprintf(buf + off, size - off, "[");
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

/** Append one `,"name":{"strikes":[..],"rimming":[..],"split":[..]}`
 *  bank, slicing the internal mode-major 75-value array into key banks. */
inline size_t AppendModeArrays(char *buf, size_t size, size_t off,
                               const char *name, const uint8_t *values)
{
    int n = snprintf(buf + off, size - off, ",\"%s\":{", name);
    if(n < 0 || static_cast<size_t>(n) >= size - off)
        return 0;
    off += static_cast<size_t>(n);
    for(int mode = 0; mode < BowlsPreset::kModeCount; ++mode)
    {
        n = snprintf(buf + off, size - off, "%s\"%s\":",
                     mode == 0 ? "" : ",", kPresetModeKeys[mode]);
        if(n < 0 || static_cast<size_t>(n) >= size - off)
            return 0;
        off += static_cast<size_t>(n);
        off = AppendArrayValues(buf, size, off,
                                values + mode * bowls::kKeyCount,
                                bowls::kKeyCount);
        if(off == 0)
            return 0;
    }
    n = snprintf(buf + off, size - off, "}");
    if(n < 0 || static_cast<size_t>(n) >= size - off)
        return 0;
    return off + static_cast<size_t>(n);
}

inline size_t AppendFloatValues(char *buf, size_t size, size_t off,
                                const float *values, int count)
{
    int n = snprintf(buf + off, size - off, "[");
    if(n < 0 || static_cast<size_t>(n) >= size - off)
        return 0;
    off += static_cast<size_t>(n);
    for(int i = 0; i < count; ++i)
    {
        if(!std::isfinite(values[i]) || values[i] < BowlsPreset::kMinPitchHz
           || values[i] > BowlsPreset::kMaxPitchHz)
            return 0;
        const uint32_t milli_hz = static_cast<uint32_t>(
            std::lround(static_cast<double>(values[i]) * 1000.0));
        n = snprintf(buf + off, size - off, "%s%u.%03u",
                     i == 0 ? "" : ",",
                     static_cast<unsigned>(milli_hz / 1000u),
                     static_cast<unsigned>(milli_hz % 1000u));
        if(n < 0 || static_cast<size_t>(n) >= size - off)
            return 0;
        off += static_cast<size_t>(n);
    }
    n = snprintf(buf + off, size - off, "]");
    if(n < 0 || static_cast<size_t>(n) >= size - off)
        return 0;
    return off + static_cast<size_t>(n);
}

inline size_t AppendPitchObject(char *buf, size_t size, size_t off,
                                const BowlsPreset &preset)
{
    int n = snprintf(buf + off, size - off,
                     ",\"pitch_hz\":{\"strikes\":");
    if(n < 0 || static_cast<size_t>(n) >= size - off)
        return 0;
    off += static_cast<size_t>(n);
    off = AppendFloatValues(buf, size, off, preset.pitch_hz.strikes,
                            bowls::kKeyCount);
    if(off == 0)
        return 0;
    n = snprintf(buf + off, size - off, ",\"rimming\":");
    if(n < 0 || static_cast<size_t>(n) >= size - off)
        return 0;
    off += static_cast<size_t>(n);
    off = AppendFloatValues(buf, size, off, preset.pitch_hz.rimming,
                            bowls::kKeyCount);
    if(off == 0)
        return 0;
    n = snprintf(buf + off, size - off, ",\"split\":");
    if(n < 0 || static_cast<size_t>(n) >= size - off)
        return 0;
    off += static_cast<size_t>(n);
    off = AppendFloatValues(buf, size, off, preset.pitch_hz.split,
                            bowls::kKeyCount);
    if(off == 0)
        return 0;
    n = snprintf(buf + off, size - off, "}}");
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
                     "\"reverb_mix\":%u,\"delay_mix\":%u,"
                     "\"delay_time\":%u",
                     BowlsPreset::kVersion,
                     (unsigned)p.mode,
                     (unsigned)p.volume,
                     (unsigned)p.strike,
                     (unsigned)p.shimmer,
                     (unsigned)p.reverb_mix,
                     (unsigned)p.delay_mix,
                     (unsigned)p.delay_time);
    if(n < 0 || static_cast<size_t>(n) >= size)
        return 0;
    size_t off = static_cast<size_t>(n);
    off = AppendModeArrays(buf, size, off, "pan", p.pan);
    if(off != 0)
        off = AppendModeArrays(buf, size, off, "bvol", p.bvol);
    if(off != 0)
        off = AppendModeArrays(buf, size, off, "profile", p.profile);
    if(off != 0)
        off = AppendModeArrays(buf, size, off, "bright", p.bright);
    if(off != 0)
        off = AppendModeArrays(buf, size, off, "decay", p.decay);
    if(off == 0)
        return 0;
    return AppendPitchObject(buf, size, off, p);
}

inline void SkipWhitespace(const char *buf, size_t len, size_t &index)
{
    while(index < len && (buf[index] == ' ' || buf[index] == '\t'
                          || buf[index] == '\n' || buf[index] == '\r'))
        ++index;
}

inline bool CopyCompactJson(const char *source, size_t source_len,
                            char *dest, size_t capacity, size_t &dest_len)
{
    bool in_string = false;
    bool escaped = false;
    dest_len = 0;
    for(size_t i = 0; i < source_len; ++i)
    {
        const char c = source[i];
        if(!in_string && (c == ' ' || c == '\t' || c == '\n' || c == '\r'))
            continue;
        if(dest_len + 1 >= capacity)
            return false;
        dest[dest_len++] = c;
        if(in_string)
        {
            if(escaped)
                escaped = false;
            else if(c == '\\')
                escaped = true;
            else if(c == '"')
                in_string = false;
        }
        else if(c == '"')
            in_string = true;
    }
    if(in_string || escaped || dest_len >= capacity)
        return false;
    dest[dest_len] = '\0';
    return true;
}

inline bool NumberTokenEnd(const char *buf, size_t len, size_t start,
                           size_t &end)
{
    size_t i = start;
    if(i < len && buf[i] == '-')
        ++i;
    if(i >= len)
        return false;
    if(buf[i] == '0')
        ++i;
    else if(buf[i] >= '1' && buf[i] <= '9')
    {
        do { ++i; } while(i < len && buf[i] >= '0' && buf[i] <= '9');
    }
    else
        return false;
    if(i < len && buf[i] == '.')
    {
        ++i;
        const size_t fraction_start = i;
        while(i < len && buf[i] >= '0' && buf[i] <= '9')
            ++i;
        if(i == fraction_start)
            return false;
    }
    if(i < len && (buf[i] == 'e' || buf[i] == 'E'))
    {
        ++i;
        if(i < len && (buf[i] == '+' || buf[i] == '-'))
            ++i;
        const size_t exponent_start = i;
        while(i < len && buf[i] >= '0' && buf[i] <= '9')
            ++i;
        if(i == exponent_start)
            return false;
    }
    end = i;
    return true;
}

/** Read a present integer scalar, rejecting missing or non-integer JSON
 *  values while clamping numeric positions to the supported range. */
inline bool QueryIntPresent(char *buf, size_t len, const char *query,
                            int lo, int hi, int &result)
{
    char *value = nullptr;
    size_t value_len = 0;
    JSONTypes_t type = JSONInvalid;
    if(JSON_SearchT(buf, len, query, strlen(query), &value, &value_len, &type)
           != JSONSuccess
       || type != JSONNumber || value_len == 0 || value_len >= 24)
        return false;
    size_t token_end = 0;
    if(!NumberTokenEnd(value, value_len, 0, token_end) || token_end != value_len
       || memchr(value, '.', value_len) != nullptr
       || memchr(value, 'e', value_len) != nullptr
       || memchr(value, 'E', value_len) != nullptr)
        return false;
    char tmp[24];
    memcpy(tmp, value, value_len);
    tmp[value_len] = '\0';
    char *end = nullptr;
    errno = 0;
    const long parsed = strtol(tmp, &end, 10);
    if(errno == ERANGE || end == tmp || *end != '\0')
        return false;
    if(parsed < lo)
        result = lo;
    else if(parsed > hi)
        result = hi;
    else
        result = static_cast<int>(parsed);
    return true;
}

/** Strictly read one `"name":[v0,..]` array of exactly `count` values,
 *  each clamped to 0..max_value, into dest. Returns false when the
 *  array is missing, malformed, or short - the caller then rejects the
 *  whole document rather than partially applying corrupt state. The
 *  buffer is non-const because coreJSON's JSON_Search takes a char*
 *  (it never writes). */
inline bool QueryArrayStrict(char *buf, size_t len, const char *name,
                             uint8_t *dest, int count, int max_value)
{
    char *value = nullptr;
    size_t value_len = 0;
    JSONTypes_t type = JSONInvalid;
    if(JSON_SearchT(buf, len, name, strlen(name), &value, &value_len, &type)
           != JSONSuccess
       || type != JSONArray || value_len < 2 || value[0] != '[')
        return false;
    size_t i = 1;
    for(int w = 0; w < count; ++w)
    {
        SkipWhitespace(value, value_len, i);
        size_t end = 0;
        if(!NumberTokenEnd(value, value_len, i, end)
           || memchr(value + i, '.', end - i) != nullptr
           || memchr(value + i, 'e', end - i) != nullptr
           || memchr(value + i, 'E', end - i) != nullptr
           || end - i >= 24)
            return false;
        char tmp[24];
        const size_t token_len = end - i;
        memcpy(tmp, value + i, token_len);
        tmp[token_len] = '\0';
        errno = 0;
        char *parsed_end = nullptr;
        const long parsed = strtol(tmp, &parsed_end, 10);
        if(errno == ERANGE || parsed_end == tmp || *parsed_end != '\0')
            return false;
        dest[w] = static_cast<uint8_t>(
            parsed < 0 ? 0 : (parsed > max_value ? max_value : parsed));
        i = end;
        SkipWhitespace(value, value_len, i);
        if(w + 1 < count)
        {
            if(i >= value_len || value[i] != ',')
                return false;
            ++i;
        }
    }
    SkipWhitespace(value, value_len, i);
    return i + 1 == value_len && value[i] == ']';
}

inline bool QueryModeArraysStrict(char *buf, size_t len, const char *name,
                                  uint8_t *dest, int max_value)
{
    char query[48];
    for(int mode = 0; mode < BowlsPreset::kModeCount; ++mode)
    {
        const int n = snprintf(query, sizeof(query), "%s.%s", name,
                               kPresetModeKeys[mode]);
        if(n < 0 || static_cast<size_t>(n) >= sizeof(query)
           || !QueryArrayStrict(buf, len, query,
                                dest + mode * bowls::kKeyCount,
                                bowls::kKeyCount, max_value))
            return false;
    }
    return true;
}

inline bool QueryPitchArrayStrict(char *buf, size_t len, const char *query,
                                  float *dest, int count)
{
    char *value = nullptr;
    size_t value_len = 0;
    JSONTypes_t type = JSONInvalid;
    if(JSON_SearchT(buf, len, query, strlen(query), &value, &value_len, &type)
           != JSONSuccess
       || type != JSONArray || value_len < 2 || value[0] != '[')
        return false;
    size_t i = 1;
    for(int w = 0; w < count; ++w)
    {
        SkipWhitespace(value, value_len, i);
        const size_t start = i;
        size_t end = 0;
        if(!NumberTokenEnd(value, value_len, start, end) || end - start >= 32)
            return false;
        char tmp[32];
        const size_t token_len = end - start;
        memcpy(tmp, value + start, token_len);
        tmp[token_len] = '\0';
        errno = 0;
        char *parsed_end = nullptr;
        const double parsed = strtod(tmp, &parsed_end);
        if(errno == ERANGE || parsed_end == tmp || *parsed_end != '\0'
           || !std::isfinite(parsed) || parsed < BowlsPreset::kMinPitchHz
           || parsed > BowlsPreset::kMaxPitchHz)
            return false;
        dest[w] = static_cast<float>(parsed);
        if(!std::isfinite(dest[w]))
            return false;
        i = end;
        SkipWhitespace(value, value_len, i);
        if(w + 1 < count)
        {
            if(i >= value_len || value[i] != ',')
                return false;
            ++i;
        }
    }
    SkipWhitespace(value, value_len, i);
    return i + 1 == value_len && value[i] == ']';
}

/** Parse a complete current-schema preset document. Unsupported,
 *  malformed, truncated and partially valid documents are rejected without
 *  changing the destination. */
inline bool Parse(char *buf, size_t len, BowlsPreset &p)
{
    if(JSON_Validate(buf, len) != JSONSuccess)
        return false;
    int version = 0;
    if(!QueryIntPresent(buf, len, "version", 0, 100, version)
       || version != BowlsPreset::kVersion)
        return false;

    BowlsPreset tmp;
    tmp.SetDefaults();
    int mode = -1;
    int volume = 0;
    int strike = 0;
    int shimmer = 0;
    int reverb_mix = 0;
    int delay_mix = 0;
    int delay_time = 0;
    if(!QueryIntPresent(buf, len, "mode", -100, 100, mode)
       || mode < 0 || mode >= BowlsPreset::kModeCount
       || !QueryIntPresent(buf, len, "volume", 0, BowlsPreset::kMaxTicks,
                           volume)
       || !QueryIntPresent(buf, len, "strike", 0, BowlsPreset::kMaxTicks,
                           strike)
       || !QueryIntPresent(buf, len, "shimmer", 0, BowlsPreset::kMaxTicks,
                           shimmer)
       || !QueryIntPresent(buf, len, "reverb_mix", 0,
                           BowlsPreset::kMaxTicks, reverb_mix)
       || !QueryIntPresent(buf, len, "delay_mix", 0,
                           BowlsPreset::kMaxTicks, delay_mix)
       || !QueryIntPresent(buf, len, "delay_time", 0,
                           BowlsPreset::kMaxTicks, delay_time))
        return false;
    tmp.mode = static_cast<uint8_t>(mode);
    tmp.volume = static_cast<uint8_t>(volume);
    tmp.strike = static_cast<uint8_t>(strike);
    tmp.shimmer = static_cast<uint8_t>(shimmer);
    tmp.reverb_mix = static_cast<uint8_t>(reverb_mix);
    tmp.delay_mix = static_cast<uint8_t>(delay_mix);
    tmp.delay_time = static_cast<uint8_t>(delay_time);

    if(!QueryModeArraysStrict(buf, len, "pan", tmp.pan,
                              BowlsPreset::kMaxTicks)
       || !QueryModeArraysStrict(buf, len, "bvol", tmp.bvol,
                                 BowlsPreset::kMaxTicks)
       || !QueryModeArraysStrict(buf, len, "profile", tmp.profile,
                                 BowlsPreset::kMaxProfile)
       || !QueryModeArraysStrict(buf, len, "bright", tmp.bright,
                                 BowlsPreset::kMaxTicks)
       || !QueryModeArraysStrict(buf, len, "decay", tmp.decay,
                                 BowlsPreset::kMaxTicks)
       || !QueryPitchArrayStrict(buf, len, "pitch_hz.strikes",
                                 tmp.pitch_hz.strikes, bowls::kKeyCount)
       || !QueryPitchArrayStrict(buf, len, "pitch_hz.rimming",
                                 tmp.pitch_hz.rimming, bowls::kKeyCount)
       || !QueryPitchArrayStrict(buf, len, "pitch_hz.split",
                                 tmp.pitch_hz.split, bowls::kKeyCount))
        return false;
    p = tmp;
    return true;
}

/** Restore factory pitch values for a malformed stored pitch table while
 *  preserving the preset's other parameters. */
inline bool ParseWithPitchFallback(char *buf, size_t len, size_t capacity,
                                   const BowlsPreset &fallback,
                                   BowlsPreset &p)
{
    if(Parse(buf, len, p))
        return true;
    if(buf == nullptr || len >= capacity)
        return false;

    static const char kPitchMarker[] = ",\"pitch_hz\":{";
    char *pitch_object = std::strstr(buf, kPitchMarker);
    if(pitch_object == nullptr)
        return false;
    const size_t pitch_offset = static_cast<size_t>(pitch_object - buf);
    bool has_nul_in_pitch = false;
    for(size_t i = 0; i < pitch_offset; ++i)
        if(buf[i] == '\0')
            return false;
    for(size_t i = pitch_offset; i < len; ++i)
        has_nul_in_pitch |= buf[i] == '\0';
    if(!has_nul_in_pitch)
        return false;

    const size_t repaired_len
        = AppendPitchObject(buf, capacity, pitch_offset, fallback);
    if(repaired_len == 0 || repaired_len >= capacity)
        return false;
    buf[repaired_len] = '\0';
    return Parse(buf, repaired_len, p);
}

} // namespace chompi
