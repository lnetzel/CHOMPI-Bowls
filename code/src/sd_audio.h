#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chompi
{
namespace sd_audio
{

constexpr uint32_t kSampleRate = 48000;
constexpr uint16_t kChannels = 2;
constexpr uint16_t kBitsPerSample = 16;
constexpr size_t kWavHeaderSize = 44;
constexpr size_t kTakeCount = 15;
constexpr uint16_t kMaxTakeNumber = 999;

inline int16_t FloatToPcm16(float sample)
{
    if(sample > 1.0f)
        sample = 1.0f;
    else if(sample < -1.0f)
        sample = -1.0f;
    const float scaled = sample * (sample < 0.0f ? 32768.0f : 32767.0f);
    return static_cast<int16_t>(scaled);
}

inline float Pcm16ToFloat(int16_t sample)
{
    return sample < 0 ? static_cast<float>(sample) / 32768.0f
                      : static_cast<float>(sample) / 32767.0f;
}

inline void WriteLe16(uint8_t* out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
}

inline void WriteLe32(uint8_t* out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
    out[2] = static_cast<uint8_t>(value >> 16);
    out[3] = static_cast<uint8_t>(value >> 24);
}

inline uint16_t ReadLe16(const uint8_t* in)
{
    return static_cast<uint16_t>(in[0])
           | static_cast<uint16_t>(static_cast<uint16_t>(in[1]) << 8);
}

inline uint32_t ReadLe32(const uint8_t* in)
{
    return static_cast<uint32_t>(in[0])
           | (static_cast<uint32_t>(in[1]) << 8)
           | (static_cast<uint32_t>(in[2]) << 16)
           | (static_cast<uint32_t>(in[3]) << 24);
}

inline void MakeWavHeader(uint8_t* header, uint32_t data_bytes)
{
    std::memset(header, 0, kWavHeaderSize);
    std::memcpy(header, "RIFF", 4);
    WriteLe32(header + 4, data_bytes + 36);
    std::memcpy(header + 8, "WAVEfmt ", 8);
    WriteLe32(header + 16, 16);
    WriteLe16(header + 20, 1);
    WriteLe16(header + 22, kChannels);
    WriteLe32(header + 24, kSampleRate);
    WriteLe32(header + 28, kSampleRate * kChannels * (kBitsPerSample / 8));
    WriteLe16(header + 32, kChannels * (kBitsPerSample / 8));
    WriteLe16(header + 34, kBitsPerSample);
    std::memcpy(header + 36, "data", 4);
    WriteLe32(header + 40, data_bytes);
}

inline bool ValidateWavHeader(const uint8_t* header, size_t size,
                              uint32_t& data_bytes)
{
    if(size < kWavHeaderSize || std::memcmp(header, "RIFF", 4) != 0
       || std::memcmp(header + 8, "WAVEfmt ", 8) != 0
       || std::memcmp(header + 36, "data", 4) != 0)
        return false;
    if(ReadLe32(header + 16) != 16 || ReadLe16(header + 20) != 1
       || ReadLe16(header + 22) != kChannels
       || ReadLe32(header + 24) != kSampleRate
       || ReadLe32(header + 28)
              != kSampleRate * kChannels * (kBitsPerSample / 8)
       || ReadLe16(header + 32) != kChannels * (kBitsPerSample / 8)
       || ReadLe16(header + 34) != kBitsPerSample)
        return false;
    data_bytes = ReadLe32(header + 40);
    return (data_bytes & 3u) == 0
           && ReadLe32(header + 4) == data_bytes + 36;
}

inline void MakeTakePath(char* path, size_t capacity, uint16_t number)
{
    if(capacity == 0)
        return;
    if(number == 0 || number > kMaxTakeNumber)
    {
        path[0] = '\0';
        return;
    }
    const char prefix[] = "BOWLS/RECORDINGS/Recording ";
    size_t pos = 0;
    while(prefix[pos] != '\0' && pos + 1 < capacity)
    {
        path[pos] = prefix[pos];
        ++pos;
    }
    char digits[3];
    size_t digit_count = 0;
    uint16_t value = number;
    do
    {
        digits[digit_count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while(value != 0 && digit_count < sizeof(digits));
    if(digit_count < 2)
        digits[digit_count++] = '0';
    while(digit_count > 0 && pos + 1 < capacity)
        path[pos++] = digits[--digit_count];
    const char suffix[] = ".wav";
    for(size_t i = 0; suffix[i] != '\0' && pos + 1 < capacity; ++i)
        path[pos++] = suffix[i];
    path[pos] = '\0';
}

inline size_t NewestTakes(const uint16_t* available, size_t count,
                          uint16_t* slots, size_t capacity)
{
    size_t written = 0;
    for(uint16_t number = kMaxTakeNumber;
        number > 0 && written < capacity && written < kTakeCount;
        --number)
    {
        for(size_t i = 0; i < count; ++i)
            if(available[i] == number)
            {
                slots[written++] = number;
                break;
            }
    }
    return written;
}

template <size_t CapacityFrames>
class StereoRing
{
    static_assert(CapacityFrames > 1, "ring must hold multiple frames");
    static_assert((CapacityFrames & (CapacityFrames - 1)) == 0,
                  "ring capacity must be a power of two");

  public:
    bool Push(const int16_t* interleaved, size_t frames)
    {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        if(frames > CapacityFrames - (head - tail))
            return false;
        for(size_t i = 0; i < frames; ++i)
        {
            const size_t slot = (head + i) & (CapacityFrames - 1);
            samples_[slot * 2] = interleaved[i * 2];
            samples_[slot * 2 + 1] = interleaved[i * 2 + 1];
        }
        head_.store(head + static_cast<uint32_t>(frames),
                    std::memory_order_release);
        return true;
    }

    size_t Pop(int16_t* interleaved, size_t max_frames)
    {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        const uint32_t head = head_.load(std::memory_order_acquire);
        const size_t frames
            = (head - tail) < max_frames ? head - tail : max_frames;
        for(size_t i = 0; i < frames; ++i)
        {
            const size_t slot = (tail + i) & (CapacityFrames - 1);
            interleaved[i * 2] = samples_[slot * 2];
            interleaved[i * 2 + 1] = samples_[slot * 2 + 1];
        }
        tail_.store(tail + static_cast<uint32_t>(frames),
                    std::memory_order_release);
        return frames;
    }

    size_t Available() const
    {
        const uint32_t head = head_.load(std::memory_order_acquire);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }

    void Clear()
    {
        const uint32_t head = head_.load(std::memory_order_acquire);
        tail_.store(head, std::memory_order_release);
    }

  private:
    int16_t samples_[CapacityFrames * 2] = {};
    alignas(4) std::atomic<uint32_t> head_{0};
    alignas(4) std::atomic<uint32_t> tail_{0};
};

} // namespace sd_audio
} // namespace chompi