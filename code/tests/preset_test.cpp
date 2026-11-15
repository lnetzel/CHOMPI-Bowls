// Host-side tests for the v10 preset schema, slot bank, and direct pitches.
// Run with `make -C code/tests modes-test` from the MSYS2 UCRT64 shell.

#include "BowlsPreset.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
int g_failures = 0;
constexpr size_t kStorageJsonSize = 4096;

void Check(bool ok, const char* name, const char* detail = "")
{
    std::printf("%-48s %s  %s\n", name, ok ? "PASS" : "FAIL", detail);
    if(!ok)
        ++g_failures;
}

bool Close(float a, float b, float tolerance = 0.00051f)
{
    return std::fabs(a - b) <= tolerance;
}

chompi::BowlsPreset PatternPreset(int mode)
{
    chompi::BowlsPreset p;
    p.SetDefaults();
    p.mode = static_cast<uint8_t>(mode);
    p.volume = 3;
    p.strike = 31;
    p.shimmer = 29;
    p.reverb_mix = 21;
    p.delay_mix = 27;
    p.delay_time = 5;
    for(int i = 0; i < chompi::BowlsPreset::kBankSlots; ++i)
    {
        p.pan[i] = static_cast<uint8_t>(i % 33);
        p.bvol[i] = static_cast<uint8_t>((i * 7) % 33);
        p.profile[i] = static_cast<uint8_t>(i % 5);
        p.bright[i] = static_cast<uint8_t>((i * 3) % 33);
        p.decay[i] = static_cast<uint8_t>((i * 11) % 33);
    }
    for(int i = 0; i < bowls::kKeyCount; ++i)
    {
        p.pitch_hz.strikes[i] = 100.12345f + i * 17.03125f;
        p.pitch_hz.rimming[i] = 200.54321f + i * 13.015625f;
    }
    for(int i = 0; i < bowls::kKeyCount; ++i)
        p.pitch_hz.split[i] = 300.23456f + i * 21.078125f;
    return p;
}

bool SameTicks(const chompi::BowlsPreset& a, const chompi::BowlsPreset& b)
{
    return a.mode == b.mode && a.volume == b.volume && a.strike == b.strike
           && a.shimmer == b.shimmer && a.reverb_mix == b.reverb_mix
           && a.delay_mix == b.delay_mix && a.delay_time == b.delay_time
           && std::memcmp(a.pan, b.pan, sizeof(a.pan)) == 0
           && std::memcmp(a.bvol, b.bvol, sizeof(a.bvol)) == 0
           && std::memcmp(a.profile, b.profile, sizeof(a.profile)) == 0
           && std::memcmp(a.bright, b.bright, sizeof(a.bright)) == 0
           && std::memcmp(a.decay, b.decay, sizeof(a.decay)) == 0;
}

bool SamePitches(const bowls::PitchTables& a, const bowls::PitchTables& b,
                 float tolerance = 0.00051f)
{
    for(int i = 0; i < bowls::kKeyCount; ++i)
        if(!Close(a.strikes[i], b.strikes[i], tolerance)
           || !Close(a.rimming[i], b.rimming[i], tolerance))
            return false;
    for(int i = 0; i < bowls::kKeyCount; ++i)
        if(!Close(a.split[i], b.split[i], tolerance))
            return false;
    return true;
}

bool Same(const chompi::BowlsPreset& a, const chompi::BowlsPreset& b)
{
    return SameTicks(a, b) && SamePitches(a.pitch_hz, b.pitch_hz);
}

bool Replace(char* text, const char* old_text, const char* new_text)
{
    char* at = std::strstr(text, old_text);
    if(at == nullptr)
        return false;
    const size_t old_len = std::strlen(old_text);
    const size_t new_len = std::strlen(new_text);
    const size_t tail_len = std::strlen(at + old_len);
    std::memmove(at + new_len, at + old_len, tail_len + 1);
    std::memcpy(at, new_text, new_len);
    return true;
}

bool ReplacePitchArray(char* text, const char* name, const char* array_text)
{
    char marker[32];
    std::snprintf(marker, sizeof(marker), "\"%s\":", name);
    char* at = std::strstr(text, marker);
    if(at == nullptr)
        return false;
    at += std::strlen(marker);
    if(*at != '[')
        return false;
    char* end = std::strchr(at, ']');
    if(end == nullptr)
        return false;
    const size_t old_len = static_cast<size_t>(end - at) + 1;
    const size_t new_len = std::strlen(array_text);
    std::memmove(at + new_len, end + 1, std::strlen(end + 1) + 1);
    std::memcpy(at, array_text, new_len);
    return old_len != 0;
}

bool ReplacePitchArrayWithCount(char* text, const char* name, int count)
{
    char array[512];
    size_t off = 0;
    array[off++] = '[';
    for(int i = 0; i < count; ++i)
    {
        const int n = std::snprintf(array + off, sizeof(array) - off,
                                    "%s100", i == 0 ? "" : ",");
        if(n < 0 || static_cast<size_t>(n) >= sizeof(array) - off)
            return false;
        off += static_cast<size_t>(n);
    }
    array[off++] = ']';
    array[off] = '\0';
    return ReplacePitchArray(text, name, array);
}

bool BuildArrayWithFirst(char* array, size_t size, const char* first,
                         int count)
{
    size_t off = 0;
    int n = std::snprintf(array, size, "[%s", first);
    if(n < 0 || static_cast<size_t>(n) >= size)
        return false;
    off = static_cast<size_t>(n);
    for(int i = 1; i < count; ++i)
    {
        n = std::snprintf(array + off, size - off, ",100");
        if(n < 0 || static_cast<size_t>(n) >= size - off)
            return false;
        off += static_cast<size_t>(n);
    }
    n = std::snprintf(array + off, size - off, "]");
    return n >= 0 && static_cast<size_t>(n) < size - off;
}

bool ParseCopy(const char* source, chompi::BowlsPreset& destination)
{
    char copy[kStorageJsonSize];
    const size_t len = std::strlen(source);
    if(len >= sizeof(copy))
        return false;
    std::memcpy(copy, source, len + 1);
    return chompi::Parse(copy, len, destination);
}

} // namespace

int main()
{
    char buf[kStorageJsonSize];

    // Factory tables remain exactly the compiled factory frequencies.
    {
        const bowls::PitchTables& defaults = bowls::FactoryPitchTables();
        bool ok = true;
        chompi::BowlsPreset preset;
        preset.SetDefaults();
        ok = SamePitches(preset.pitch_hz, defaults, 0.0f);
        for(int i = 0; i < bowls::kKeyCount; ++i)
            if(preset.pitch_hz.strikes[i] != bowls::kFullScaleHz[i]
               || preset.pitch_hz.rimming[i] != bowls::kFullScaleHz[i])
                ok = false;
        for(int i = 0; i < 12; ++i)
            if(preset.pitch_hz.split[i] != bowls::kBowlScaleHz[i]
               || preset.pitch_hz.split[12 + i] != bowls::kBowlScaleHz[i])
                ok = false;
        if(preset.pitch_hz.split[24] != 1240.8f)
            ok = false;
        Check(ok, "factory pitches equal existing tables", "");
    }

    // The first-boot template is embedded directly from the state example.
    {
        std::ifstream input("../state examples/default.json",
                            std::ios::binary);
        const bool opened = input.is_open();
        const std::string source((std::istreambuf_iterator<char>(input)),
                                 std::istreambuf_iterator<char>());
        char doc[kStorageJsonSize];
        size_t len = 0;
        bool ok = opened
                  && chompi::CopyCompactJson(source.data(), source.size(), doc,
                                             sizeof(doc), len);
        chompi::BowlsPreset preset;
        preset.SetDefaults();
        if(!ok || !chompi::Parse(doc, len, preset)
           || preset.mode != 0
           || preset.delay_time != chompi::BowlsPreset::kDefaultDelayTime
           || !SamePitches(preset.pitch_hz, bowls::FactoryPitchTables()))
            ok = false;
        Check(ok, "embedded first-boot template parses", "");
    }

    // Distinct fractional pitches, all six banks, all globals and modes.
    {
        bool ok = true;
        for(int mode = 0; mode < chompi::BowlsPreset::kModeCount; ++mode)
        {
            const chompi::BowlsPreset src = PatternPreset(mode);
            const size_t len = chompi::Serialize(src, buf, sizeof(buf));
            chompi::BowlsPreset dst;
            dst.SetDefaults();
            if(len == 0 || !chompi::Parse(buf, len, dst) || !Same(src, dst))
                ok = false;
        }
        Check(ok, "round-trip 75 pitches, five banks, all modes", "");
    }

    // Slot cache operations are explicit; editing a separate live snapshot
    // cannot dirty or silently replace a saved preset.
    {
        chompi::BowlsPresetBank bank;
        bank.Init();
        chompi::BowlsPreset live_state = PatternPreset(0);
        const chompi::BowlsPreset saved_state = live_state;
        bank.LoadSlot(0, saved_state);
        live_state.volume = 12;
        const bool ok = bank.IsValid(0) && !bank.IsDirty(0)
                        && bank.NextDirtySlot() == -1
                        && Same(bank.Get(0), saved_state)
                        && !Same(live_state, saved_state);
        Check(ok, "performance edits do not auto-dirty cached slots", "");
    }

    // Saving, copying and erasing dirty only the explicitly selected slots.
    {
        chompi::BowlsPresetBank bank;
        bank.Init();
        const chompi::BowlsPreset first = PatternPreset(0);
        bank.Save(0, first);
        const uint32_t first_revision = bank.Revision(0);
        bool ok = bank.IsValid(0) && bank.IsDirty(0)
                  && bank.NextDirtySlot() == 0
                  && bank.Copy(0, 14) && bank.IsValid(14)
                  && bank.IsDirty(14) && Same(bank.Get(14), first)
                  && !bank.Copy(14, 14) && !bank.Copy(1, 2);

        bank.ClearDirtyIfRevision(0, first_revision - 1);
        if(!bank.IsDirty(0))
            ok = false;
        bank.ClearDirtyIfRevision(0, first_revision);
        if(bank.IsDirty(0) || !bank.IsDirty(14))
            ok = false;

        const uint32_t copy_revision = bank.Revision(14);
        if(!bank.Erase(14) || bank.IsValid(14)
           || bank.Revision(14) == copy_revision || !bank.IsDirty(14)
           || bank.Erase(14) || bank.Copy(14, 3))
            ok = false;
        bank.ClearDirtyIfRevision(14, copy_revision);
        if(!bank.IsDirty(14))
            ok = false;
        bank.ClearDirtyIfRevision(14, bank.Revision(14));
        bank.Save(15, first);
        if(bank.IsDirty(14) || bank.IsValid(15))
            ok = false;
        Check(ok, "explicit save/copy/erase slot bookkeeping", "");
    }

    // Measure maximum numeric size, terminator and undersized-buffer failure.
    {
        chompi::BowlsPreset p;
        p.SetDefaults();
        for(int i = 0; i < chompi::BowlsPreset::kBankSlots; ++i)
        {
            p.pan[i] = p.bvol[i] = p.bright[i] = p.decay[i] = 255;
            p.profile[i] = 255;
        }
        for(int i = 0; i < bowls::kKeyCount; ++i)
            p.pitch_hz.strikes[i] = p.pitch_hz.rimming[i] = 10000.0f;
        for(int i = 0; i < bowls::kKeyCount; ++i)
            p.pitch_hz.split[i] = 10000.0f;
        p.volume = p.strike = p.shimmer = p.reverb_mix = 255;
        p.delay_mix = p.delay_time = 255;
        const size_t len = chompi::Serialize(p, buf, sizeof(buf));
        char small[512];
        Check(len > 512 && len + 1 <= kStorageJsonSize,
              "worst numeric preset fits 4096-byte budget",
              "includes terminator");
        char formatted[kStorageJsonSize];
        size_t formatted_len = 0;
        bool format_ok = len > 0;
        for(size_t i = 0; i < len && format_ok; ++i)
        {
            if(formatted_len + (buf[i] == ',' ? 2u : 1u) >= sizeof(formatted))
            {
                format_ok = false;
                break;
            }
            formatted[formatted_len++] = buf[i];
            if(buf[i] == ',')
                formatted[formatted_len++] = '\n';
        }
        formatted[formatted_len] = '\0';
        chompi::BowlsPreset parsed;
        parsed.SetDefaults();
        char size_detail[64];
        std::snprintf(size_detail, sizeof(size_detail), "compact=%lu formatted=%lu",
                  static_cast<unsigned long>(len),
                  static_cast<unsigned long>(formatted_len));
        Check(format_ok && formatted_len + 1 <= kStorageJsonSize
                  && chompi::Parse(formatted, formatted_len, parsed),
              "formatted offline input fits and parses", size_detail);
        Check(chompi::Serialize(p, small, sizeof(small)) == 0,
              "undersized serialization buffer rejected", "");
    }

    // Float rounding is deterministic and stays within the stated bound.
    {
        chompi::BowlsPreset src = PatternPreset(0);
        bool ok = true;
        for(int cycle = 0; cycle < 4; ++cycle)
        {
            const size_t len = chompi::Serialize(src, buf, sizeof(buf));
            if(cycle == 0
               && (std::strstr(buf, "100.123") == nullptr
                   || std::memchr(buf, '\0', len) != nullptr))
                ok = false;
            chompi::BowlsPreset next;
            next.SetDefaults();
            if(len == 0 || !chompi::Parse(buf, len, next))
                ok = false;
            src = next;
        }
        Check(ok && SamePitches(src.pitch_hz, PatternPreset(0).pitch_hz),
              "three-decimal save cycles remain stable", "");
    }

    // Older firmware's nano printf wrote NULs instead of pitch decimals.
    {
        const chompi::BowlsPreset src = PatternPreset(1);
        chompi::BowlsPreset fallback;
        fallback.SetDefaults();
        size_t len = chompi::Serialize(src, buf, sizeof(buf));
        char damaged[kStorageJsonSize];
        bool ok = len > 0;
        if(ok)
            std::memcpy(damaged, buf, len + 1);
        char* pitch_object
            = ok ? std::strstr(damaged, ",\"pitch_hz\":{") : nullptr;
        char* first_pitch
            = pitch_object == nullptr
                  ? nullptr
                  : std::strstr(pitch_object, "\"strikes\":[");
        if(first_pitch == nullptr)
            ok = false;
        else
        {
            first_pitch += std::strlen("\"strikes\":[");
            *first_pitch = '\0';
            chompi::BowlsPreset recovered;
            recovered.SetDefaults();
            if(chompi::Parse(damaged, len, recovered)
               || !chompi::ParseWithPitchFallback(
                   damaged, len, sizeof(damaged), fallback, recovered)
               || !SameTicks(recovered, src)
               || !SamePitches(recovered.pitch_hz, fallback.pitch_hz))
                ok = false;
        }
        Check(ok, "damaged pitch JSON recovers with factory pitch fallback",
              "");
    }

    // Strict versions and pitch arrays; failed parses preserve the target.
    {
        const chompi::BowlsPreset src = PatternPreset(1);
        const size_t len = chompi::Serialize(src, buf, sizeof(buf));
        bool ok = len > 0;
        char doc[kStorageJsonSize];
        chompi::BowlsPreset sentinel = PatternPreset(2);
        const chompi::BowlsPreset before = sentinel;
        const char* versions[] = {"7", "8", "9", "11"};
        for(const char* version : versions)
        {
            std::memcpy(doc, buf, len + 1);
            char replacement[24];
            std::snprintf(replacement, sizeof(replacement), "\"version\":%s",
                          version);
            if(!Replace(doc, "\"version\":10", replacement)
               || chompi::Parse(doc, std::strlen(doc), sentinel)
               || !Same(sentinel, before))
                ok = false;
        }
        std::memcpy(doc, buf, len + 1);
        chompi::BowlsPreset legacy_destination = before;
        if(!Replace(doc, "\"version\":10", "\"version\":9")
           || chompi::ParseWithPitchFallback(
               doc, std::strlen(doc), sizeof(doc), src, legacy_destination)
           || !Same(legacy_destination, before)
           || std::strstr(doc, "\"version\":9") == nullptr)
            ok = false;
        std::memcpy(doc, buf, len + 1);
        char* pitch_object = std::strstr(doc, ",\"pitch_hz\":{");
        char* object_end = pitch_object == nullptr
                               ? nullptr
                               : std::strstr(pitch_object, "}}");
        if(pitch_object == nullptr || object_end == nullptr)
            ok = false;
        else
        {
            std::memmove(pitch_object, object_end + 1,
                         std::strlen(object_end + 1) + 1);
            if(chompi::Parse(doc, std::strlen(doc), sentinel)
               || !Same(sentinel, before))
                ok = false;
        }
        Check(ok, "legacy versions and absent pitch object rejected", "");
    }

    // All table lengths are exact, and invalid token types/ranges reject whole.
    {
        const chompi::BowlsPreset src = PatternPreset(0);
        const size_t len = chompi::Serialize(src, buf, sizeof(buf));
        bool ok = len > 0;
        const int full_bad_lengths[] = {0, 12, 14, 24, 26};
        const int split_bad_lengths[] = {0, 12, 24, 26};
        char doc[kStorageJsonSize];
        for(int count : full_bad_lengths)
        {
            std::memcpy(doc, buf, len + 1);
            chompi::BowlsPreset dst = src;
            if(!ReplacePitchArrayWithCount(doc, "strikes", count)
               || ParseCopy(doc, dst) || !Same(dst, src))
                ok = false;
        }
        for(int count : split_bad_lengths)
        {
            std::memcpy(doc, buf, len + 1);
            chompi::BowlsPreset dst = src;
            if(!ReplacePitchArrayWithCount(doc, "split", count)
                    || ParseCopy(doc, dst) || !Same(dst, src))
                ok = false;
        }
        const char* invalid_first[] = {"\"100\"", "null", "true", "1e9999",
                                       "39.999", "10000.001", "1e"};
        for(const char* first : invalid_first)
        {
            std::memcpy(doc, buf, len + 1);
            chompi::BowlsPreset dst = src;
            char array[512];
            if(!BuildArrayWithFirst(array, sizeof(array), first,
                                    bowls::kKeyCount)
               || !ReplacePitchArray(doc, "strikes", array)
               || ParseCopy(doc, dst) || !Same(dst, src))
                ok = false;
        }
        const int tick_bad_lengths[] = {74, 76};
        for(int count : tick_bad_lengths)
        {
            std::memcpy(doc, buf, len + 1);
            chompi::BowlsPreset dst = src;
            if(!ReplacePitchArrayWithCount(doc, "pan", count)
               || ParseCopy(doc, dst) || !Same(dst, src))
                ok = false;
        }
        Check(ok, "wrong pitch types, lengths, overflow and range rejected", "");
    }

    // Endpoints, scientific notation, duplicates, and arbitrary ordering are valid.
    {
        chompi::BowlsPreset src;
        src.SetDefaults();
        size_t len = chompi::Serialize(src, buf, sizeof(buf));
        bool ok = len > 0;
        char doc[kStorageJsonSize];
        std::memcpy(doc, buf, len + 1);
        // Change the first values while retaining the exact 25-entry length.
        char values[512];
        size_t off = static_cast<size_t>(std::snprintf(values, sizeof(values),
                                                       "[4e1,1e4,100,100,40"));
        for(int i = 5; i < bowls::kKeyCount; ++i)
            off += static_cast<size_t>(std::snprintf(values + off,
                        sizeof(values) - off, ",%d", 100 + i));
        std::snprintf(values + off, sizeof(values) - off, "]");
        std::memcpy(doc, buf, len + 1);
        if(!ReplacePitchArray(doc, "strikes", values))
            ok = false;
        chompi::BowlsPreset dst;
        dst.SetDefaults();
        if(!ParseCopy(doc, dst) || dst.pitch_hz.strikes[0] != 40.0f
           || dst.pitch_hz.strikes[1] != 10000.0f
           || dst.pitch_hz.strikes[2] != 100.0f)
            ok = false;
        Check(ok, "pitch boundaries, exponent, duplicates and ordering accepted", "");
    }

    // Factory state and default fields remain unchanged when no preset exists.
    {
        chompi::BowlsPreset p;
        p.SetDefaults();
        const size_t len = chompi::Serialize(p, buf, sizeof(buf));
        Check(p.mode == chompi::BowlsPreset::kModeStrikes
                  && p.delay_mix == 0
                  && p.delay_time == chompi::BowlsPreset::kDefaultDelayTime
                  && len > 0 && std::strstr(buf, "\"pitch_hz\"") != nullptr
                  && std::strstr(buf, "latch") == nullptr,
              "factory defaults include pitches without runtime state", "");
    }

    std::printf("%s (%d failures)\n",
                g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}