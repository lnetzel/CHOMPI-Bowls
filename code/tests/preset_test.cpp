// Host-side tests for the v6 preset schema (plans/improve-play-modes.md).
// Not part of the firmware build; run with `make -C code/tests
// modes-test` from the MSYS2 UCRT64 shell.
//
//   S1  round-trip: distinct values in all 75 mode/key slots of all
//       four banks, all globals, and each of the three modes survive
//       serialize -> parse
//   S2  size: the worst-case serialization crosses 512 bytes (the old
//       single-sector buffer - reads/writes must go multi-sector) and
//       fits the 1536-byte storage buffers INCLUDING the terminator,
//       even with impossible 3-digit values
//   S3  rejection: v5/v7 files, invalid JSON, truncation, an
//       out-of-range mode, a short array and non-integer array entries
//       are all rejected WHOLE (never partially applied)
//   S4  clamping: out-of-range array values clamp to the field ranges
//   S5  factory defaults: fresh state is Strikes; no latch or
//       performance state appears in the serialized document

#include "BowlsPreset.h"

#include <cstdio>
#include <cstring>

namespace
{
int g_failures = 0;

void Check(bool ok, const char* name, const char* detail)
{
    std::printf("%-46s %s  %s\n", name, ok ? "PASS" : "FAIL", detail);
    if(!ok)
        ++g_failures;
}

void Check(bool ok, const char* name, long value)
{
    char detail[32];
    std::snprintf(detail, sizeof(detail), "%ld", value);
    Check(ok, name, detail);
}

// Mirror of PresetStorage::kJsonSize (PresetStorage.h itself pulls in
// daisy.h and is not host-compilable).
constexpr size_t kStorageJsonSize = 1536;

chompi::BowlsPreset PatternPreset(int mode)
{
    chompi::BowlsPreset p;
    p.SetDefaults();
    p.mode       = static_cast<uint8_t>(mode);
    p.volume     = 3;
    p.strike     = 31;
    p.brightness = 17;
    p.decay      = 5;
    p.shimmer    = 29;
    p.reverb_mix = 21;
    for(int i = 0; i < chompi::BowlsPreset::kBankSlots; ++i)
    {
        p.pan[i]     = static_cast<uint8_t>(i % 33);
        p.bvol[i]    = static_cast<uint8_t>((i * 7) % 33);
        p.tune[i]    = static_cast<uint8_t>((i * 13) % 33);
        p.profile[i] = static_cast<uint8_t>(i % 5);
    }
    return p;
}

bool Same(const chompi::BowlsPreset& a, const chompi::BowlsPreset& b)
{
    return a.mode == b.mode && a.volume == b.volume && a.strike == b.strike
           && a.brightness == b.brightness && a.decay == b.decay
           && a.shimmer == b.shimmer && a.reverb_mix == b.reverb_mix
           && std::memcmp(a.pan, b.pan, sizeof(a.pan)) == 0
           && std::memcmp(a.bvol, b.bvol, sizeof(a.bvol)) == 0
           && std::memcmp(a.tune, b.tune, sizeof(a.tune)) == 0
           && std::memcmp(a.profile, b.profile, sizeof(a.profile)) == 0;
}

} // namespace

int main()
{
    char buf[kStorageJsonSize];

    // S1: round-trip all 75 slots x 4 banks, all globals, each mode.
    {
        bool ok = true;
        for(int mode = 0; mode < chompi::BowlsPreset::kModeCount; ++mode)
        {
            chompi::BowlsPreset src = PatternPreset(mode);
            const size_t        len = chompi::Serialize(src, buf, sizeof(buf));
            if(len == 0)
            {
                ok = false;
                break;
            }
            chompi::BowlsPreset dst;
            dst.SetDefaults();
            if(!chompi::Parse(buf, len, dst) || !Same(src, dst))
                ok = false;
        }
        Check(ok, "S1 round-trip 75 slots x 4 banks + mode", "");
    }

    // S2: worst-case size crosses 512 and fits 1536 incl. terminator.
    {
        chompi::BowlsPreset p;
        p.SetDefaults();
        for(int i = 0; i < chompi::BowlsPreset::kBankSlots; ++i)
        {
            p.pan[i] = p.bvol[i] = p.tune[i] = 32;
            p.profile[i]                     = 4;
        }
        const size_t len = chompi::Serialize(p, buf, sizeof(buf));
        Check(len > 512, "S2 worst case crosses 512 B (multi-sector)",
              static_cast<long>(len));
        Check(len + 1 <= kStorageJsonSize,
              "S2b worst case fits 1536 B incl. terminator",
              static_cast<long>(len + 1));
        // Even impossible 3-digit values (corrupt-but-uint8) must fit.
        for(int i = 0; i < chompi::BowlsPreset::kBankSlots; ++i)
            p.pan[i] = p.bvol[i] = p.tune[i] = p.profile[i] = 255;
        const size_t len3 = chompi::Serialize(p, buf, sizeof(buf));
        Check(len3 > 0 && len3 + 1 <= kStorageJsonSize,
              "S2c 3-digit values still fit", static_cast<long>(len3 + 1));
    }

    // S3: rejections (never partially applied).
    {
        chompi::BowlsPreset src = PatternPreset(1);
        const size_t        len = chompi::Serialize(src, buf, sizeof(buf));

        // version 5 / version 7
        char* v6 = std::strstr(buf, "\"version\":6");
        Check(v6 != nullptr, "S3 pre: version field present", "");
        chompi::BowlsPreset dst;
        dst.SetDefaults();
        v6[9] = '5';
        Check(!chompi::Parse(buf, len, dst), "S3a v5 file rejected", "");
        v6[9] = '7';
        Check(!chompi::Parse(buf, len, dst), "S3b v7 file rejected", "");
        v6[9] = '6';

        // invalid JSON
        char junk[64];
        std::strcpy(junk, "{\"version\":6,\"mode\":0,not json");
        Check(!chompi::Parse(junk, std::strlen(junk), dst),
              "S3c invalid JSON rejected", "");

        // truncated mid-array
        Check(!chompi::Parse(buf, len * 3 / 5, dst),
              "S3d truncated document rejected", "");

        // out-of-range mode
        char* m1 = std::strstr(buf, "\"mode\":1");
        Check(m1 != nullptr, "S3 pre2: mode field present", "");
        m1[7] = '9';
        Check(!chompi::Parse(buf, len, dst), "S3e mode 9 rejected", "");
        m1[7] = '1';

        // short array: rebuild a doc with a 74-entry pan array
        {
            char small[kStorageJsonSize];
            int  n = std::snprintf(small, sizeof(small),
                                   "{\"version\":6,\"mode\":0,"
                                   "\"volume\":1,\"strike\":1,\"brightness\":1,"
                                   "\"decay\":1,\"shimmer\":1,\"reverb_mix\":1");
            size_t off = static_cast<size_t>(n);
            off = chompi::AppendArray(small, sizeof(small), off, "pan",
                                      src.pan, 74); // one short
            off = chompi::AppendArray(small, sizeof(small), off, "bvol",
                                      src.bvol, chompi::BowlsPreset::kBankSlots);
            off = chompi::AppendArray(small, sizeof(small), off, "tune",
                                      src.tune, chompi::BowlsPreset::kBankSlots);
            off = chompi::AppendArray(small, sizeof(small), off, "profile",
                                      src.profile,
                                      chompi::BowlsPreset::kBankSlots);
            n = std::snprintf(small + off, sizeof(small) - off, "}");
            off += static_cast<size_t>(n);
            Check(!chompi::Parse(small, off, dst),
                  "S3f 74-entry array rejected", "");
        }

        // non-integer array entry (valid JSON, invalid schema):
        // "pan":[0,1,... -> "pan":[0.1,... (a float where the schema
        // wants 75 integers)
        {
            char* pan_arr = std::strstr(buf, "\"pan\":[0,1,");
            Check(pan_arr != nullptr, "S3 pre3: pan array present", "");
            pan_arr[8] = '.';
            Check(!chompi::Parse(buf, len, dst),
                  "S3g float array entry rejected", "");
            pan_arr[8] = ',';
        }
    }

    // S4: out-of-range values clamp to the field ranges.
    {
        chompi::BowlsPreset p;
        p.SetDefaults();
        for(int i = 0; i < chompi::BowlsPreset::kBankSlots; ++i)
            p.pan[i] = p.bvol[i] = p.tune[i] = p.profile[i] = 99;
        const size_t len = chompi::Serialize(p, buf, sizeof(buf));
        chompi::BowlsPreset dst;
        dst.SetDefaults();
        const bool ok = chompi::Parse(buf, len, dst);
        bool       clamped = ok;
        for(int i = 0; i < chompi::BowlsPreset::kBankSlots; ++i)
            if(dst.pan[i] != 32 || dst.bvol[i] != 32 || dst.tune[i] != 32
               || dst.profile[i] != 4)
                clamped = false;
        Check(clamped, "S4 out-of-range values clamp", "");
    }

    // S5: factory defaults are Strikes; no latch/performance state is
    // ever serialized.
    {
        chompi::BowlsPreset p;
        p.SetDefaults();
        Check(p.mode == chompi::BowlsPreset::kModeStrikes,
              "S5a factory default mode is Strikes", "");
        const size_t len = chompi::Serialize(p, buf, sizeof(buf));
        Check(std::strstr(buf, "latch") == nullptr
                  && std::strstr(buf, "held") == nullptr,
              "S5b no latch/performance state in JSON", "");
        (void)len;
    }

    std::printf("%s (%d failures)\n",
                g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
