#include "sd_audio.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

int main()
{
    using namespace chompi::sd_audio;

    StereoRing<8> ring;
    const int16_t input[] = {1, -1, 2, -2, 3, -3, 4, -4, 5, -5};
    assert(ring.Push(input, 5));
    assert(ring.Available() == 5);
    int16_t output[12] = {};
    assert(ring.Pop(output, 3) == 3);
    assert(ring.Push(input + 6, 3));
    assert(!ring.Push(input, 6));
    assert(ring.Pop(output + 6, 5) == 5);
    for(size_t i = 0; i < 10; ++i)
        assert(output[i] == input[i]);

    assert(FloatToPcm16(-2.0f) == -32768);
    assert(FloatToPcm16(2.0f) == 32767);
    assert(Pcm16ToFloat(-32768) == -1.0f);
    assert(Pcm16ToFloat(32767) == 1.0f);

    uint8_t header[kWavHeaderSize];
    uint32_t data_bytes = 0;
    MakeWavHeader(header, 1920);
    assert(ValidateWavHeader(header, sizeof(header), data_bytes));
    assert(data_bytes == 1920);
    assert(ReadLe32(header + 24) == 48000);
    assert(ReadLe16(header + 22) == 2);
    header[34] = 8;
    assert(!ValidateWavHeader(header, sizeof(header), data_bytes));

    char path[48];
    MakeTakePath(path, sizeof(path), 7);
    assert(std::string(path) == "BOWLS/RECORDINGS/Recording 07.wav");
    MakeTakePath(path, sizeof(path), 999);
    assert(std::string(path) == "BOWLS/RECORDINGS/Recording 999.wav");
    MakeTakePath(path, sizeof(path), 0);
    assert(path[0] == '\0');

    const uint16_t found[] = {4, 90, 8, 999, 13, 512};
    uint16_t slots[kTakeCount] = {};
    const size_t count = NewestTakes(found, 6, slots, kTakeCount);
    const uint16_t expected[] = {999, 512, 90, 13, 8, 4};
    assert(count == 6);
    for(size_t i = 0; i < count; ++i)
        assert(slots[i] == expected[i]);

    std::puts("sd_audio_test: PASS");
    return 0;
}