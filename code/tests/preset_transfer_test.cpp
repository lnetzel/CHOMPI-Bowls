#include "../src/PresetTransferProtocol.h"

#include <cassert>
#include <cstdint>

int main()
{
    using namespace chompi::preset_transfer;

    const uint8_t payload[] = {0, 1, 42, 127};
    uint8_t encoded[kMaxFrameBytes] = {};
    size_t encoded_length = 0;
    assert(Encode(kWriteData, 127, payload, sizeof(payload), encoded,
                  sizeof(encoded), encoded_length));
    assert(encoded[0] == 0xf0);
    assert(encoded[encoded_length - 1] == 0xf7);

    Frame decoded{};
    assert(Decode(encoded + 1, encoded_length - 2, decoded));
    assert(decoded.command == kWriteData);
    assert(decoded.sequence == 127);
    assert(decoded.payload_length == sizeof(payload));
    for(size_t i = 0; i < sizeof(payload); ++i)
        assert(decoded.payload[i] == payload[i]);

    encoded[8] ^= 1;
    assert(!Decode(encoded + 1, encoded_length - 2, decoded));
    const uint8_t high_bit_payload[] = {0x80};
    assert(!Encode(kWriteData, 0, high_bit_payload, 1, encoded,
                   sizeof(encoded), encoded_length));
    assert(!Encode(kWriteData, 128, payload, sizeof(payload), encoded,
                   sizeof(encoded), encoded_length));
    assert(!Encode(kWriteData, 0, payload, kMaxFramePayload + 1, encoded,
                   sizeof(encoded), encoded_length));

    const uint16_t values14[] = {0, 127, 128, 4095, 16383};
    for(uint16_t value : values14)
    {
        uint8_t bytes[2];
        Encode14(value, bytes);
        assert(bytes[0] <= 0x7f && bytes[1] <= 0x7f);
        assert(Decode14(bytes) == value);
    }

    const uint32_t values21[] = {0, 127, 128, 16383, 16384, 32767, 0x1fffff};
    for(uint32_t value : values21)
    {
        uint8_t bytes[3];
        Encode21(value, bytes);
        assert(bytes[0] <= 0x7f && bytes[1] <= 0x7f && bytes[2] <= 0x7f);
        assert(Decode21(bytes) == value);
    }

    return 0;
}
