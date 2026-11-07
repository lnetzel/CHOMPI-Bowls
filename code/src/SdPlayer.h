#pragma once

#include "PresetStorage.h"
#include "sd_audio.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace chompi
{

template <size_t RingFrames>
class SdPlayer
{
  public:
    using Ring = sd_audio::StereoRing<RingFrames>;
    static constexpr size_t kIoFrames = 512;
    static constexpr size_t kIoBytes = kIoFrames * 2 * sizeof(int16_t);

    explicit SdPlayer(Ring& ring) : ring_(ring) {}

    void RequestEnter()
    {
        exit_requested_.store(false, std::memory_order_relaxed);
        enter_requested_.store(true, std::memory_order_release);
        mode_active_.store(true, std::memory_order_release);
    }

    void RequestExit()
    {
        exit_requested_.store(true, std::memory_order_release);
        playing_.store(false, std::memory_order_release);
    }

    void TogglePause()
    {
        playing_.store(!playing_.load(std::memory_order_relaxed),
                       std::memory_order_release);
    }

    void ToggleLoop()
    {
        looping_.store(!looping_.load(std::memory_order_relaxed),
                       std::memory_order_release);
    }

    void SelectSlot(int slot)
    {
        if(slot >= 0 && slot < static_cast<int>(sd_audio::kTakeCount))
            requested_slot_.store(slot, std::memory_order_release);
    }

    bool ModeActive() const
    {
        return mode_active_.load(std::memory_order_acquire);
    }
    bool Playing() const { return playing_.load(std::memory_order_acquire); }
    bool Looping() const { return looping_.load(std::memory_order_acquire); }
    bool Ready() const { return ready_.load(std::memory_order_acquire); }
    int SelectedSlot() const
    {
        return selected_slot_public_.load(std::memory_order_acquire);
    }
    uint16_t TakeAt(int slot) const
    {
        return slot >= 0 && slot < static_cast<int>(take_count_)
                   ? takes_[slot]
                   : 0;
    }
    size_t TakeCount() const { return take_count_; }

    void Render(float* left, float* right, size_t frames)
    {
        const uint32_t flush = flush_request_.load(std::memory_order_acquire);
        if(flush != flush_ack_.load(std::memory_order_relaxed))
        {
            ring_.Clear();
            flush_ack_.store(flush, std::memory_order_release);
            return;
        }
        if(!Playing())
            return;
        if(frames > kMaxAudioFrames)
            frames = kMaxAudioFrames;
        const size_t count = ring_.Pop(audio_samples_, frames);
        for(size_t frame = 0; frame < count; ++frame)
        {
            left[frame] += sd_audio::Pcm16ToFloat(audio_samples_[frame * 2]);
            right[frame]
                += sd_audio::Pcm16ToFloat(audio_samples_[frame * 2 + 1]);
        }
    }

    /** One scan/read/write-sized filesystem operation per main-loop pass. */
    void Service(PresetStorage& storage)
    {
        if(enter_requested_.exchange(false, std::memory_order_acq_rel))
        {
            if(storage.NoSdCard())
            {
                mode_active_.store(false, std::memory_order_release);
                event_.store(kEventEmpty, std::memory_order_release);
                return;
            }
            if(!storage.TryAcquireFilesystem())
            {
                enter_requested_.store(true, std::memory_order_release);
                return;
            }
            storage_ = &storage;
            take_count_ = 0;
            stage_ = kOpenDir;
        }

        if(stage_ == kIdle)
            return;
        if(stage_ == kWaitFlush)
        {
            if(flush_ack_.load(std::memory_order_acquire)
               == flush_request_.load(std::memory_order_acquire))
            {
                if(exit_requested_.load(std::memory_order_acquire))
                    FinishExit();
                else
                {
                    if(file_open_)
                    {
                        f_close(&file_);
                        file_open_ = false;
                    }
                    stage_ = kOpenTake;
                }
            }
            return;
        }
        if(storage.NoSdCard())
        {
            Fail();
            return;
        }
        if(exit_requested_.load(std::memory_order_acquire))
        {
            BeginFlush();
            return;
        }
        const int requested_slot
            = requested_slot_.exchange(-1, std::memory_order_acq_rel);
        if(requested_slot >= 0 && requested_slot < static_cast<int>(take_count_)
           && requested_slot != selected_slot_)
        {
            selected_slot_ = requested_slot;
            playing_.store(false, std::memory_order_release);
            BeginFlush();
            return;
        }

        switch(stage_)
        {
            case kOpenDir:
            {
                const FRESULT result = f_opendir(&dir_, "BOWLS/RECORDINGS");
                if(result == FR_NO_PATH || result == FR_NO_FILE)
                {
                    event_.store(kEventEmpty, std::memory_order_release);
                    FinishExit();
                }
                else if(result == FR_OK)
                    stage_ = kScanDir;
                else
                    Fail();
                break;
            }

            case kScanDir:
                /* Bounded: at most 16 directory entries per pass. */
                for(int entry = 0; entry < 16; ++entry)
                {
                    const FRESULT result = f_readdir(&dir_, &info_);
                    if(result != FR_OK)
                    {
                        f_closedir(&dir_);
                        Fail();
                        break;
                    }
                    if(info_.fname[0] == '\0')
                    {
                        f_closedir(&dir_);
                        if(take_count_ == 0)
                        {
                            event_.store(kEventEmpty, std::memory_order_release);
                            FinishExit();
                        }
                        else
                        {
                            selected_slot_ = 0;
                            selected_slot_public_.store(0,
                                                        std::memory_order_release);
                            ready_.store(true, std::memory_order_release);
                            playing_.store(true, std::memory_order_release);
                            looping_.store(false, std::memory_order_release);
                            /* The SDRAM ring is NOLOAD: flush it (acknowledged
                             * by the audio callback) before the first fill. */
                            BeginFlush();
                        }
                        break;
                    }
                    if((info_.fattrib & AM_DIR) == 0)
                        InsertTake(ParseTakeNumber(info_.fname));
                }
                break;

            case kOpenTake:
                OpenTake(selected_slot_);
                break;

            case kPlaying:
                ServicePlayback();
                break;

            default: Fail(); break;
        }
    }

    enum Event : uint8_t
    {
        kEventNone = 0,
        kEventEmpty,
        kEventInvalidTake,
        kEventFatal,
        kEventExited
    };

    Event ConsumeEvent(int& slot)
    {
        slot = event_slot_.exchange(-1, std::memory_order_acq_rel);
        return event_.exchange(kEventNone, std::memory_order_acq_rel);
    }

  private:
    static constexpr size_t kMaxAudioFrames = 24;
    enum Stage : uint8_t
    {
        kIdle = 0,
        kOpenDir,
        kScanDir,
        kOpenTake,
        kPlaying,
        kWaitFlush
    };

    /** "Recording N.wav" -> N (1..999), else 0. */
    static uint16_t ParseTakeNumber(const char* name)
    {
        const char prefix[] = "Recording ";
        for(int i = 0; prefix[i] != '\0'; ++i)
            if(name[i] != prefix[i])
                return 0;
        name += 10;
        uint32_t value = 0;
        int      digits = 0;
        while(*name >= '0' && *name <= '9' && digits < 4)
        {
            value = value * 10 + static_cast<uint32_t>(*name - '0');
            ++digits;
            ++name;
        }
        if(digits == 0 || digits > 3 || value < 1 || value > 999)
            return 0;
        if(name[0] != '.' || (name[1] != 'W' && name[1] != 'w')
           || (name[2] != 'A' && name[2] != 'a')
           || (name[3] != 'V' && name[3] != 'v') || name[4] != '\0')
            return 0;
        return static_cast<uint16_t>(value);
    }

    /** Keep the newest 15 take numbers, sorted descending (slot order). */
    void InsertTake(uint16_t number)
    {
        if(number == 0)
            return;
        if(take_count_ >= sd_audio::kTakeCount
           && number <= takes_[sd_audio::kTakeCount - 1])
            return;
        if(take_count_ < sd_audio::kTakeCount)
            ++take_count_;
        int i = static_cast<int>(take_count_) - 1;
        while(i > 0 && takes_[i - 1] < number)
        {
            takes_[i] = takes_[i - 1];
            --i;
        }
        takes_[i] = number;
    }

    void OpenTake(int slot)
    {
        if(file_open_)
        {
            f_close(&file_);
            file_open_ = false;
        }
        sd_audio::MakeTakePath(path_, sizeof(path_), takes_[slot]);
        UINT read = 0;
        if(f_open(&file_, path_, FA_READ) == FR_OK)
        {
            file_open_ = true;
            if(f_read(&file_, header_, sizeof(header_), &read) == FR_OK
               && read == sizeof(header_)
               && sd_audio::ValidateWavHeader(header_, sizeof(header_),
                                              data_remaining_)
               && data_remaining_ > 0
               && f_size(&file_) >= sd_audio::kWavHeaderSize + data_remaining_
               && f_lseek(&file_, sd_audio::kWavHeaderSize) == FR_OK)
            {
                stage_ = kPlaying;
                selected_slot_ = slot;
                selected_slot_public_.store(slot, std::memory_order_release);
                invalid_attempts_ = 0;
                return;
            }
        }
        if(file_open_)
        {
            f_close(&file_);
            file_open_ = false;
        }
        event_slot_.store(slot, std::memory_order_release);
        event_.store(kEventInvalidTake, std::memory_order_release);
        if(++invalid_attempts_ >= take_count_ || !AdvanceSlot(slot))
            Fail();
    }

    bool AdvanceSlot(int from)
    {
        if(take_count_ == 0)
            return false;
        const int next = (from + 1) % static_cast<int>(take_count_);
        selected_slot_ = next;
        stage_ = kOpenTake;
        return true;
    }

    void ServicePlayback()
    {
        const size_t available = ring_.Available();
        if(data_remaining_ > 0 && available < RingFrames - kIoFrames)
        {
            UINT bytes_read = 0;
            const size_t free_frames = RingFrames - available;
            size_t frames = free_frames < kIoFrames ? free_frames : kIoFrames;
            size_t bytes = frames * 2 * sizeof(int16_t);
            if(bytes > data_remaining_)
                bytes = data_remaining_;
            bytes -= bytes % (2 * sizeof(int16_t));
            if(bytes > 0)
            {
                if(f_read(&file_, io_buffer_, static_cast<UINT>(bytes),
                          &bytes_read) != FR_OK
                   || bytes_read != bytes
                   || !ring_.Push(reinterpret_cast<int16_t*>(io_buffer_),
                                  bytes_read / (2 * sizeof(int16_t))))
                {
                    Fail();
                    return;
                }
                data_remaining_ -= bytes_read;
                return;
            }
        }

        if(data_remaining_ == 0 && available == 0 && Playing())
        {
            if(Looping())
            {
                if(f_lseek(&file_, sd_audio::kWavHeaderSize) != FR_OK)
                    Fail();
                else
                    data_remaining_ = sd_audio::ReadLe32(header_ + 40);
            }
            else if(!AdvanceSlot(selected_slot_))
                Fail();
        }
    }

    void FinishExit()
    {
        playing_.store(false, std::memory_order_release);
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
        mode_active_.store(false, std::memory_order_release);
        ready_.store(false, std::memory_order_release);
        exit_requested_.store(false, std::memory_order_relaxed);
        if(event_.load(std::memory_order_acquire) == kEventNone)
            event_.store(kEventExited, std::memory_order_release);
    }

    void Fail()
    {
        event_.store(kEventFatal, std::memory_order_release);
        exit_requested_.store(true, std::memory_order_release);
        playing_.store(false, std::memory_order_release);
        if(stage_ != kWaitFlush)
            BeginFlush();
        else
            FinishExit();
    }

    void BeginFlush()
    {
        if(stage_ != kWaitFlush)
        {
            flush_request_.fetch_add(1, std::memory_order_acq_rel);
            stage_ = kWaitFlush;
        }
        playing_.store(false, std::memory_order_release);
    }

    Ring& ring_;
    PresetStorage* storage_ = nullptr;
    FIL file_{};
    DIR dir_{};
    FILINFO info_{};
    alignas(32) uint8_t header_[sd_audio::kWavHeaderSize] = {};
    alignas(32) uint8_t io_buffer_[kIoBytes] = {};
    int16_t audio_samples_[kMaxAudioFrames * 2] = {};
    char path_[48] = {};
    uint16_t takes_[sd_audio::kTakeCount] = {};
    size_t take_count_ = 0;
    size_t invalid_attempts_ = 0;
    uint32_t data_remaining_ = 0;
    int selected_slot_ = 0;
    Stage stage_ = kIdle;
    bool file_open_ = false;
    std::atomic<bool> mode_active_{false};
    std::atomic<bool> ready_{false};
    std::atomic<uint32_t> flush_request_{0};
    std::atomic<uint32_t> flush_ack_{0};
    std::atomic<bool> playing_{false};
    std::atomic<bool> looping_{false};
    std::atomic<bool> enter_requested_{false};
    std::atomic<bool> exit_requested_{false};
    std::atomic<int> requested_slot_{-1};
    std::atomic<int> selected_slot_public_{-1};
    std::atomic<int> event_slot_{-1};
    std::atomic<Event> event_{kEventNone};
};

} // namespace chompi