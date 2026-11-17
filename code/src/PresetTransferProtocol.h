#pragma once

#include <cstddef>
#include <cstdint>

namespace chompi
{
namespace preset_transfer
{

constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kMaxFramePayload = 119;
constexpr size_t kMaxFrameBytes = kMaxFramePayload + 11;

enum Command : uint8_t
{
    kHello = 1,
    kListSlots,
    kReadBegin,
    kReadNext,
    kWriteBegin,
    kWriteData,
    kWriteCommit,
    kCancel
};

enum Status : uint8_t
{
    kOk = 0,
    kPending,
    kBadRequest,
    kUnsupported,
    kNoCard,
    kBusy,
    kInvalidSlot,
    kEmptySlot,
    kIncompatibleSlot,
    kInvalidPreset,
    kStorageError,
    kSequenceError,
    kTooLarge,
    kTransferExpired
};

struct Frame
{
    uint8_t command;
    uint8_t sequence;
    uint8_t payload[kMaxFramePayload];
    size_t payload_length;
};

inline uint16_t Crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffff;
    for(size_t i = 0; i < length; ++i)
    {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for(int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) != 0
                      ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                      : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

inline bool Encode(uint8_t command, uint8_t sequence,
                   const uint8_t *payload, size_t payload_length,
                   uint8_t *output, size_t output_capacity,
                   size_t &output_length)
{
    output_length = 0;
    if(command > 0x7f || sequence > 0x7f
       || payload_length > kMaxFramePayload
       || (payload_length != 0 && payload == nullptr) || output == nullptr
       || output_capacity < payload_length + 11)
        return false;
    for(size_t i = 0; i < payload_length; ++i)
        if(payload[i] > 0x7f)
            return false;

    output[0] = 0xf0;
    output[1] = 0x7d;
    output[2] = 0x43;
    output[3] = 0x42;
    output[4] = kProtocolVersion;
    output[5] = command;
    output[6] = sequence;
    for(size_t i = 0; i < payload_length; ++i)
        output[7 + i] = payload[i];

    const size_t body_length = payload_length + 6;
    const uint16_t crc = Crc16(output + 1, body_length);
    output[7 + payload_length] = static_cast<uint8_t>(crc & 0x7f);
    output[8 + payload_length] = static_cast<uint8_t>((crc >> 7) & 0x7f);
    output[9 + payload_length] = static_cast<uint8_t>((crc >> 14) & 0x03);
    output[10 + payload_length] = 0xf7;
    output_length = payload_length + 11;
    return true;
}

inline bool Decode(const uint8_t *input, size_t input_length, Frame &frame)
{
    if(input == nullptr || input_length < 9
       || input_length > kMaxFramePayload + 9)
        return false;
    for(size_t i = 0; i < input_length; ++i)
        if(input[i] > 0x7f)
            return false;
    if(input[0] != 0x7d || input[1] != 0x43 || input[2] != 0x42
       || input[3] != kProtocolVersion || input[4] > 0x7f
       || input[5] > 0x7f || input[input_length - 1] > 0x03)
        return false;

    const size_t payload_length = input_length - 9;
    const size_t body_length = input_length - 3;
    const uint16_t expected_crc = Crc16(input, body_length);
    const uint16_t received_crc
        = static_cast<uint16_t>(input[body_length])
          | static_cast<uint16_t>(input[body_length + 1] << 7)
          | static_cast<uint16_t>(input[body_length + 2] << 14);
    if(expected_crc != received_crc)
        return false;

    frame.command = input[4];
    frame.sequence = input[5];
    frame.payload_length = payload_length;
    for(size_t i = 0; i < payload_length; ++i)
        frame.payload[i] = input[6 + i];
    return true;
}

inline void Encode14(uint16_t value, uint8_t output[2])
{
    output[0] = static_cast<uint8_t>(value & 0x7f);
    output[1] = static_cast<uint8_t>((value >> 7) & 0x7f);
}

inline uint16_t Decode14(const uint8_t input[2])
{
    return static_cast<uint16_t>(input[0])
           | static_cast<uint16_t>(input[1] << 7);
}

inline void Encode21(uint32_t value, uint8_t output[3])
{
    output[0] = static_cast<uint8_t>(value & 0x7f);
    output[1] = static_cast<uint8_t>((value >> 7) & 0x7f);
    output[2] = static_cast<uint8_t>((value >> 14) & 0x7f);
}

inline uint32_t Decode21(const uint8_t input[3])
{
    return static_cast<uint32_t>(input[0])
           | (static_cast<uint32_t>(input[1]) << 7)
           | (static_cast<uint32_t>(input[2]) << 14);
}

} // namespace preset_transfer
} // namespace chompi
