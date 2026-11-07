#pragma once

#include "PresetStorage.h"
#include "sd_audio.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chompi
{

template <size_t RingFrames>
class SdRecorder
{
  public:
    using Ring = sd_audio::StereoRing<RingFrames>;
    static constexpr size_t kIoFrames = 512;
    static constexpr size_t kIoBytes = kIoFrames * 2 * sizeof(int16_t);

    explicit SdRecorder(Ring& ring) : ring_(ring) {}

    void RequestStart()
    {
        stop_requested_.store(false, std::memory_order_relaxed);
        overflow_.store(false, std::memory_order_relaxed);
        start_requested_.store(true, std::memory_order_release);
    }

    void RequestStop()
    {
        stop_requested_.store(true, std::memory_order_release);
        recording_.store(false, std::memory_order_release);
    }

    bool IsRecording() const
    {
        return recording_.load(std::memory_order_acquire);
    }

    bool IsBusy() const
    {
        return stage_ != kIdle
               || start_requested_.load(std::memory_order_acquire);
    }

    bool Capture(const float* left, const float* right, size_t frames)
    {
        if(!recording_.load(std::memory_order_acquire))
            return true;
        int16_t converted[48];
        if(frames > 24)
        {
            overflow_.store(true, std::memory_order_release);
            recording_.store(false, std::memory_order_release);
            return false;
        }
        constexpr float kRecordGain = 0.501187234f;
        for(size_t frame = 0; frame < frames; ++frame)
        {
            converted[frame * 2] = sd_audio::FloatToPcm16(left[frame] * kRecordGain);
            converted[frame * 2 + 1]
                = sd_audio::FloatToPcm16(right[frame] * kRecordGain);
        }
        if(!ring_.Push(converted, frames))
        {
            overflow_.store(true, std::memory_order_release);
            recording_.store(false, std::memory_order_release);
            return false;
        }
        return true;
    }

    /** Performs at most one bounded filesystem transfer per pass. */
    void Service(PresetStorage& storage, uint32_t now)
    {
        if(stage_ == kIdle
           && start_requested_.exchange(false, std::memory_order_acq_rel))
        {
            if(storage.NoSdCard())
            {
                error_event_.store(true, std::memory_order_release);
                return;
            }
            if(!storage.TryAcquireFilesystem())
            {
                start_requested_.store(true, std::memory_order_release);
                return;
            }
            storage_ = &storage;
            number_ = 1;
            stage_ = kMakeDirectory;
        }

        if(stage_ == kIdle)
            return;
        if(storage.NoSdCard())
        {
            Fail();
            return;
        }

        switch(stage_)
        {
            case kMakeDirectory:
            {
                const FRESULT result = f_mkdir("BOWLS/RECORDINGS");
                if(result == FR_OK || result == FR_EXIST)
                    stage_ = kFindTake;
                else
                    Fail();
                break;
            }

            case kFindTake:
                sd_audio::MakeTakePath(path_, sizeof(path_), number_);
                if(f_stat(path_, &info_) == FR_OK)
                {
                    if(number_ == sd_audio::kMaxTakeNumber)
                        Fail();
                    else
                        ++number_;
                }
                else if(f_open(&file_, path_, FA_CREATE_NEW | FA_WRITE) == FR_OK)
                {
                    file_open_ = true;
                    std::memset(header_, 0, sizeof(header_));
                    stage_ = kWriteHeader;
                }
                else
                    Fail();
                break;

            case kWriteHeader:
                if(WriteBytes(header_, sizeof(header_)))
                {
                    ring_.Clear();
                    data_bytes_ = 0;
                    last_sync_ = now;
                    overflow_.store(false, std::memory_order_relaxed);
                    recording_.store(true, std::memory_order_release);
                    stage_ = kRecording;
                }
                else
                    Fail();
                break;

            case kRecording:
                if(overflow_.load(std::memory_order_acquire))
                {
                    failed_recording_ = true;
                    recording_.store(false, std::memory_order_release);
                    stage_ = kDrain;
                }
                else if(stop_requested_.load(std::memory_order_acquire))
                {
                    recording_.store(false, std::memory_order_release);
                    stage_ = kDrain;
                }
                if(stage_ == kRecording)
                {
                    if(!WriteRingChunk())
                        Fail();
                    else if(now - last_sync_ >= 2000)
                    {
                        if(f_sync(&file_) != FR_OK)
                            Fail();
                        else
                            last_sync_ = now;
                    }
                }
                break;

            case kDrain:
                if(ring_.Available() > 0)
                {
                    if(!WriteRingChunk())
                        Fail();
                }
                else
                    stage_ = kPatchHeader;
                break;

            case kPatchHeader:
                sd_audio::MakeWavHeader(header_, data_bytes_);
                if(f_lseek(&file_, 0) != FR_OK
                   || !WriteBytes(header_, sizeof(header_))
                   || f_sync(&file_) != FR_OK)
                    failed_recording_ = true;
                f_close(&file_);
                file_open_ = false;
                storage.ReleaseFilesystem();
                storage_ = nullptr;
                stage_ = kIdle;
                if(failed_recording_)
                    error_event_.store(true, std::memory_order_release);
                failed_recording_ = false;
                break;

            default: Fail(); break;
        }
    }

    bool ConsumeErrorEvent()
    {
        return error_event_.exchange(false, std::memory_order_acq_rel);
    }

  private:
    enum Stage : uint8_t
    {
        kIdle = 0,
        kMakeDirectory,
        kFindTake,
        kWriteHeader,
        kRecording,
        kDrain,
        kPatchHeader
    };

    bool WriteBytes(const void* data, UINT bytes)
    {
        UINT written = 0;
        return f_write(&file_, data, bytes, &written) == FR_OK
               && written == bytes;
    }

    bool WriteRingChunk()
    {
        const size_t frames = ring_.Pop(
            reinterpret_cast<int16_t*>(io_buffer_), kIoFrames);
        if(frames == 0)
            return true;
        const size_t bytes = frames * 2 * sizeof(int16_t);
        if(data_bytes_ > 0xffffffffu - bytes
           || !WriteBytes(io_buffer_, static_cast<UINT>(bytes)))
            return false;
        data_bytes_ += static_cast<uint32_t>(bytes);
        return true;
    }

    void Fail()
    {
        recording_.store(false, std::memory_order_release);
        if(file_open_)
        {
            f_close(&file_);
            file_open_ = false;
        }
        if(storage_ != nullptr)
        {
            storage_->ReleaseFilesystem();
            storage_ = nullptr;
        }
        stage_ = kIdle;
        start_requested_.store(false, std::memory_order_relaxed);
        error_event_.store(true, std::memory_order_release);
    }

    Ring& ring_;
    PresetStorage* storage_ = nullptr;
    FIL file_{};
    FILINFO info_{};
    alignas(32) uint8_t header_[sd_audio::kWavHeaderSize] = {};
    alignas(32) uint8_t io_buffer_[kIoBytes] = {};
    char path_[48] = {};
    uint32_t data_bytes_ = 0;
    uint32_t last_sync_ = 0;
    uint16_t number_ = 1;
    Stage stage_ = kIdle;
    bool file_open_ = false;
    bool failed_recording_ = false;
    std::atomic<bool> recording_{false};
    std::atomic<bool> start_requested_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> overflow_{false};
    std::atomic<bool> error_event_{false};
};

} // namespace chompi