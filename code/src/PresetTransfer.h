#pragma once

#include "BowlsPreset.h"
#include "MidiHandler.h"
#include "PresetStorage.h"
#include "PresetTransferProtocol.h"

#include <cstring>

namespace chompi
{

class PresetTransfer
{
  public:
    static constexpr size_t kChunkBytes = 96;
    static constexpr uint32_t kTimeoutMs = 60000;

    void Service(bowls::BowlsMidi &midi, PresetStorage &storage,
                 BowlsPresetBank &bank)
    {
        bowls::MidiSysExMessage message;
        while(midi.PopSysEx(message))
        {
            preset_transfer::Frame request;
            if(!preset_transfer::Decode(message.data, message.length, request))
                continue;
            HandleRequest(request, midi, storage, bank);
        }

        if(state_ != kIdle
           && static_cast<int32_t>(daisy::System::GetNow() - last_activity_)
                  >= static_cast<int32_t>(kTimeoutMs))
            state_ = kIdle;

        if(state_ == kAwaitSave)
        {
            if(storage.NoSdCard())
            {
                CompleteWrite(midi, preset_transfer::kNoCard);
                return;
            }
            if(!IsSlotDirty(bank, slot_) && !storage.Busy())
                CompleteWrite(midi, preset_transfer::kOk);
        }
    }

    void OnStorageEvent(PresetStorage::StatusEvent event, int slot,
                        bowls::BowlsMidi &midi)
    {
        if(state_ != kAwaitSave || slot != slot_)
            return;
        if(event == PresetStorage::kStatusFailed)
            CompleteWrite(midi, preset_transfer::kStorageError);
        else if((event == PresetStorage::kStatusSaved
                 || event == PresetStorage::kStatusRecovered)
                && !IsSlotDirtyForPending())
            CompleteWrite(midi, preset_transfer::kOk);
    }

  private:
    enum State : uint8_t
    {
        kIdle = 0,
        kReading,
        kWriting,
        kAwaitSave
    };

    static bool IsSlotDirty(BowlsPresetBank &bank, int slot)
    {
        __disable_irq();
        const bool dirty = bank.IsDirty(slot);
        __enable_irq();
        return dirty;
    }

    bool IsSlotDirtyForPending() const
    {
        __disable_irq();
        const bool dirty = bank_for_pending_->IsDirty(slot_);
        __enable_irq();
        return dirty;
    }

    static bool HasDirtySlots(BowlsPresetBank &bank)
    {
        __disable_irq();
        const bool dirty = bank.NextDirtySlot() >= 0;
        __enable_irq();
        return dirty;
    }

    static uint16_t SlotBitmap(BowlsPresetBank &bank, bool dirty)
    {
        uint16_t bitmap = 0;
        __disable_irq();
        for(int slot = 0; slot < BowlsPresetBank::kNumSlots; ++slot)
        {
            if(dirty ? bank.IsDirty(slot) : bank.IsValid(slot))
                bitmap |= static_cast<uint16_t>(1u << slot);
        }
        __enable_irq();
        return bitmap;
    }

    static uint16_t IncompatibleBitmap(PresetStorage &storage)
    {
        uint16_t bitmap = 0;
        for(int slot = 0; slot < BowlsPresetBank::kNumSlots; ++slot)
            if(storage.IsIncompatibleSlot(slot))
                bitmap |= static_cast<uint16_t>(1u << slot);
        return bitmap;
    }

    bool Reply(bowls::BowlsMidi &midi, uint8_t command, uint8_t sequence,
               preset_transfer::Status status, const uint8_t *extra = nullptr,
               size_t extra_length = 0)
    {
        if(extra_length + 1 > preset_transfer::kMaxFramePayload)
            return false;
        uint8_t payload[preset_transfer::kMaxFramePayload];
        payload[0] = static_cast<uint8_t>(status);
        for(size_t i = 0; i < extra_length; ++i)
            payload[i + 1] = extra[i];

        uint8_t output[preset_transfer::kMaxFrameBytes];
        size_t output_length = 0;
        if(!preset_transfer::Encode(
               static_cast<uint8_t>(command | 0x40), sequence, payload,
               extra_length + 1, output, sizeof(output), output_length))
            return false;
        return midi.SendUsbSysEx(output, output_length);
    }

    void HandleRequest(const preset_transfer::Frame &request,
                       bowls::BowlsMidi &midi, PresetStorage &storage,
                       BowlsPresetBank &bank)
    {
        using namespace preset_transfer;
        if(request.command < kHello || request.command > kCancel)
        {
            Reply(midi, request.command, request.sequence, kUnsupported);
            return;
        }
        if(request.command == kHello)
        {
            if(request.payload_length != 0)
            {
                Reply(midi, request.command, request.sequence, kBadRequest);
                return;
            }
            const uint8_t info[] = {
                kProtocolVersion,
                static_cast<uint8_t>(BowlsPreset::kVersion),
                static_cast<uint8_t>(BowlsPresetBank::kNumSlots),
                static_cast<uint8_t>(!storage.NoSdCard()),
                static_cast<uint8_t>(storage.Busy())};
            Reply(midi, request.command, request.sequence, kOk, info,
                  sizeof(info));
            return;
        }
        if(request.command == kListSlots)
        {
            if(request.payload_length != 0)
            {
                Reply(midi, request.command, request.sequence, kBadRequest);
                return;
            }
            uint8_t status[kMaxFramePayload - 1];
            uint8_t bits[3];
            Encode21(SlotBitmap(bank, false), bits);
            for(int i = 0; i < 3; ++i)
                status[i] = bits[i];
            Encode21(IncompatibleBitmap(storage), bits);
            for(int i = 0; i < 3; ++i)
                status[i + 3] = bits[i];
            Encode21(SlotBitmap(bank, true), bits);
            for(int i = 0; i < 3; ++i)
                status[i + 6] = bits[i];
            status[9] = static_cast<uint8_t>(storage.Busy());
            Reply(midi, request.command, request.sequence, kOk, status, 10);
            return;
        }
        if(request.command == kReadBegin)
        {
            ReadBegin(request, midi, storage, bank);
            return;
        }
        if(request.command == kReadNext)
        {
            ReadNext(request, midi);
            return;
        }
        if(request.command == kWriteBegin)
        {
            WriteBegin(request, midi, storage, bank);
            return;
        }
        if(request.command == kWriteData)
        {
            WriteData(request, midi);
            return;
        }
        if(request.command == kWriteCommit)
        {
            WriteCommit(request, midi, storage, bank);
            return;
        }
        Cancel(request, midi);
    }

    void ReadBegin(const preset_transfer::Frame &request,
                   bowls::BowlsMidi &midi, PresetStorage &storage,
                   BowlsPresetBank &bank)
    {
        using namespace preset_transfer;
        if(request.payload_length != 1)
        {
            Reply(midi, request.command, request.sequence, kBadRequest);
            return;
        }
        const int requested_slot = request.payload[0];
        if(requested_slot >= BowlsPresetBank::kNumSlots)
        {
            Reply(midi, request.command, request.sequence, kInvalidSlot);
            return;
        }
        if(storage.NoSdCard())
        {
            Reply(midi, request.command, request.sequence, kNoCard);
            return;
        }
        if(storage.Busy())
        {
            Reply(midi, request.command, request.sequence, kBusy);
            return;
        }
        if(state_ != kIdle
           && !(state_ == kReading && slot_ == requested_slot
                && read_begin_sequence_ == request.sequence))
        {
            Reply(midi, request.command, request.sequence, kBusy);
            return;
        }
        if(storage.IsIncompatibleSlot(requested_slot))
        {
            Reply(midi, request.command, request.sequence, kIncompatibleSlot);
            return;
        }

        BowlsPreset snapshot;
        __disable_irq();
        const bool valid = bank.IsValid(requested_slot);
        if(valid)
            snapshot = bank.Get(requested_slot);
        __enable_irq();
        if(!valid)
        {
            Reply(midi, request.command, request.sequence, kEmptySlot);
            return;
        }

        const size_t length
            = Serialize(snapshot, json_, sizeof(json_));
        if(length == 0 || length >= sizeof(json_))
        {
            Reply(midi, request.command, request.sequence, kStorageError);
            return;
        }
        state_ = kReading;
        slot_ = requested_slot;
        read_length_ = length;
        read_begin_sequence_ = request.sequence;
        last_activity_ = daisy::System::GetNow();
        uint8_t size_bytes[2];
        Encode14(static_cast<uint16_t>(length), size_bytes);
        Reply(midi, request.command, request.sequence, kOk, size_bytes, 2);
    }

    void ReadNext(const preset_transfer::Frame &request,
                  bowls::BowlsMidi &midi)
    {
        using namespace preset_transfer;
        if(request.payload_length != 2)
        {
            Reply(midi, request.command, request.sequence, kBadRequest);
            return;
        }
        if(state_ != kReading)
        {
            Reply(midi, request.command, request.sequence, kSequenceError);
            return;
        }
        const size_t offset = Decode14(request.payload);
        if(offset >= read_length_)
        {
            Reply(midi, request.command, request.sequence, kSequenceError);
            return;
        }
        const size_t length
            = (read_length_ - offset) < kChunkBytes
                  ? read_length_ - offset
                  : kChunkBytes;
        uint8_t extra[kChunkBytes + 2];
        Encode14(static_cast<uint16_t>(offset), extra);
        for(size_t i = 0; i < length; ++i)
            extra[i + 2] = static_cast<uint8_t>(json_[offset + i]);
        last_activity_ = daisy::System::GetNow();
        Reply(midi, request.command, request.sequence, kOk, extra,
              length + 2);
    }

    void WriteBegin(const preset_transfer::Frame &request,
                    bowls::BowlsMidi &midi, PresetStorage &storage,
                    BowlsPresetBank &bank)
    {
        using namespace preset_transfer;
        if(request.payload_length != 3)
        {
            Reply(midi, request.command, request.sequence, kBadRequest);
            return;
        }
        const int requested_slot = request.payload[0];
        const size_t length = Decode14(request.payload + 1);
        if(requested_slot >= BowlsPresetBank::kNumSlots)
        {
            Reply(midi, request.command, request.sequence, kInvalidSlot);
            return;
        }
        if(length == 0 || length >= PresetStorage::kJsonSize)
        {
            Reply(midi, request.command, request.sequence, kTooLarge);
            return;
        }
        if(storage.NoSdCard())
        {
            Reply(midi, request.command, request.sequence, kNoCard);
            return;
        }
        if(storage.Busy() || HasDirtySlots(bank))
        {
            Reply(midi, request.command, request.sequence, kBusy);
            return;
        }
        if(state_ != kIdle)
        {
            if(state_ == kWriting && slot_ == requested_slot
               && write_length_ == length
               && write_begin_sequence_ == request.sequence)
            {
                Reply(midi, request.command, request.sequence, kOk);
                return;
            }
            Reply(midi, request.command, request.sequence, kBusy);
            return;
        }

        state_ = kWriting;
        slot_ = requested_slot;
        write_length_ = length;
        write_received_ = 0;
        write_begin_sequence_ = request.sequence;
        last_commit_valid_ = false;
        last_activity_ = daisy::System::GetNow();
        Reply(midi, request.command, request.sequence, kOk);
    }

    void WriteData(const preset_transfer::Frame &request,
                   bowls::BowlsMidi &midi)
    {
        using namespace preset_transfer;
        if(request.payload_length < 3)
        {
            Reply(midi, request.command, request.sequence, kBadRequest);
            return;
        }
        if(state_ != kWriting)
        {
            Reply(midi, request.command, request.sequence, kSequenceError);
            return;
        }
        const size_t offset = Decode14(request.payload);
        const size_t length = request.payload_length - 2;
        if(length > kChunkBytes || offset > write_length_
           || length > write_length_ - offset)
        {
            Reply(midi, request.command, request.sequence, kTooLarge);
            return;
        }
        if(offset < write_received_)
        {
            if(offset + length <= write_received_
               && std::memcmp(json_ + offset, request.payload + 2, length) == 0)
            {
                uint8_t next[2];
                Encode14(static_cast<uint16_t>(write_received_), next);
                Reply(midi, request.command, request.sequence, kOk, next, 2);
            }
            else
                Reply(midi, request.command, request.sequence, kSequenceError);
            return;
        }
        if(offset != write_received_)
        {
            Reply(midi, request.command, request.sequence, kSequenceError);
            return;
        }
        for(size_t i = 0; i < length; ++i)
            json_[offset + i] = static_cast<char>(request.payload[i + 2]);
        write_received_ += length;
        last_activity_ = daisy::System::GetNow();
        uint8_t next[2];
        Encode14(static_cast<uint16_t>(write_received_), next);
        Reply(midi, request.command, request.sequence, kOk, next, 2);
    }

    void WriteCommit(const preset_transfer::Frame &request,
                     bowls::BowlsMidi &midi, PresetStorage &storage,
                     BowlsPresetBank &bank)
    {
        using namespace preset_transfer;
        if(request.payload_length != 0)
        {
            Reply(midi, request.command, request.sequence, kBadRequest);
            return;
        }
        if(state_ == kAwaitSave && request.sequence == pending_commit_sequence_)
        {
            Reply(midi, request.command, request.sequence, kPending);
            return;
        }
        if(state_ == kIdle && last_commit_valid_
           && request.sequence == last_commit_sequence_)
        {
            Reply(midi, request.command, request.sequence, last_commit_status_);
            return;
        }
        if(state_ != kWriting || write_received_ != write_length_)
        {
            Reply(midi, request.command, request.sequence, kSequenceError);
            return;
        }
        if(storage.NoSdCard())
        {
            FinishWrite(midi, request.sequence, kNoCard);
            return;
        }
        if(storage.Busy() || HasDirtySlots(bank))
        {
            FinishWrite(midi, request.sequence, kBusy);
            return;
        }

        BowlsPreset preset;
        json_[write_length_] = '\0';
        if(!Parse(json_, write_length_, preset))
        {
            FinishWrite(midi, request.sequence, kInvalidPreset);
            return;
        }

        __disable_irq();
        bank.Save(slot_, preset);
        __enable_irq();
        storage.ClearIncompatibleSlot(slot_);
        bank_for_pending_ = &bank;
        state_ = kAwaitSave;
        pending_commit_sequence_ = request.sequence;
        last_activity_ = daisy::System::GetNow();
        Reply(midi, request.command, request.sequence, kPending);
    }

    void Cancel(const preset_transfer::Frame &request,
                bowls::BowlsMidi &midi)
    {
        if(request.payload_length != 0)
        {
            Reply(midi, request.command, request.sequence,
                  preset_transfer::kBadRequest);
            return;
        }
        if(state_ == kAwaitSave)
        {
            Reply(midi, request.command, request.sequence,
                  preset_transfer::kBusy);
            return;
        }
        state_ = kIdle;
        Reply(midi, request.command, request.sequence, preset_transfer::kOk);
    }

    void FinishWrite(bowls::BowlsMidi &midi, uint8_t sequence,
                     preset_transfer::Status status)
    {
        state_ = kIdle;
        last_commit_valid_ = true;
        last_commit_sequence_ = sequence;
        last_commit_status_ = status;
        Reply(midi, preset_transfer::kWriteCommit, sequence, status);
    }

    void CompleteWrite(bowls::BowlsMidi &midi,
                       preset_transfer::Status status)
    {
        const uint8_t sequence = pending_commit_sequence_;
        FinishWrite(midi, sequence, status);
    }

    State state_ = kIdle;
    char json_[PresetStorage::kJsonSize]{};
    size_t read_length_ = 0;
    size_t write_length_ = 0;
    size_t write_received_ = 0;
    int slot_ = -1;
    uint8_t read_begin_sequence_ = 0;
    uint8_t write_begin_sequence_ = 0;
    uint8_t pending_commit_sequence_ = 0;
    uint8_t last_commit_sequence_ = 0;
    preset_transfer::Status last_commit_status_ = preset_transfer::kOk;
    bool last_commit_valid_ = false;
    uint32_t last_activity_ = 0;
    BowlsPresetBank *bank_for_pending_ = nullptr;
};

} // namespace chompi
