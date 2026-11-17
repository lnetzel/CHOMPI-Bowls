/** @file PresetStorage.h
 *  @brief SD-card storage for 15 CHOMPI-BOWLS presets.
 *
 *  Each slot is stored as BOWLS/P01.json .. P15.json. Writes/deletes are
 *  staged from the main loop, and the complete slot cache stays in RAM;
 *  performance parameter changes alone never dirty or write a slot.
 */
#pragma once
#include "daisy.h"
#include "fatfs.h"
#include "diskio.h"
#include "BowlsPreset.h"

namespace chompi
{

extern "C" const char default_state_json[];
extern "C" const char default_state_json_end[];

class PresetStorage
{
  public:
    static constexpr int      kNumSlots   = BowlsPresetBank::kNumSlots;
    static constexpr size_t   kJsonSize   = 4096;
    static constexpr int      kMaxRetries = 3;
    static constexpr uint32_t kBackoffMs  = 250;

    enum StatusEvent : uint8_t
    {
        kStatusNone = 0,
        kStatusFailed,
        kStatusRecovered,
        kStatusSaved
    };

    void Init()
    {
        no_sd_card_      = true;
        fs_leased_       = false;
        stage_           = kIdle;
        retry_stage_     = kIdle;
        slot_            = -1;
        retries_         = 0;
        last_disk_check_ = 0;
        retry_at_        = 0;
        retry_blocked_   = false;
        rescan_slot_     = kNumSlots;
        failed_          = false;
        event_           = kStatusNone;
        json_len_        = 0;
        valid_snapshot_  = false;
        factory_preset_.SetDefaults();
        for(int slot = 0; slot < kNumSlots; ++slot)
            incompatible_[slot] = false;

        daisy::SdmmcHandler::Config sd_cfg;
        sd_cfg.speed = daisy::SdmmcHandler::Speed::FAST;
        sd_cfg.width = daisy::SdmmcHandler::BusWidth::BITS_4;
        sdmmc_.Init(sd_cfg);
        fsi_.Init(daisy::FatFSInterface::Config::MEDIA_SD);
        f_mount(&fsi_.GetSDFileSystem(), fsi_.GetSDPath(), 1);
        f_mkdir("BOWLS");
        no_sd_card_ = disk_status(0) != RES_OK;
    }

    /** Parse the embedded factory preset on every boot. */
    bool LoadFactoryDefaults(BowlsPreset &preset)
    {
        const std::uintptr_t begin
            = reinterpret_cast<std::uintptr_t>(default_state_json);
        const std::uintptr_t end
            = reinterpret_cast<std::uintptr_t>(default_state_json_end);
        if(end < begin)
            return false;

        size_t len = 0;
        if(!CopyCompactJson(default_state_json,
                            static_cast<size_t>(end - begin), read_buf_,
                            sizeof(read_buf_), len))
            return false;
        if(!Parse(read_buf_, len, preset))
            return false;
        factory_preset_ = preset;
        return true;
    }

    /** Load each slot before audio starts. Missing or invalid files leave
     *  their slots empty; malformed files are marked for LED feedback. */
    void LoadAll(BowlsPresetBank &bank)
    {
        rescan_slot_ = kNumSlots;
        if(no_sd_card_)
            return;
        f_mkdir("BOWLS");
        for(int slot = 0; slot < kNumSlots; ++slot)
            LoadSlotFromCard(slot, bank, false);
    }

    /** One bounded slot-read or one staged write/delete step per pass. */
    void Service(BowlsPresetBank &bank)
    {
        const uint32_t now = daisy::System::GetNow();

        if(now - last_disk_check_ >= 1000)
        {
            last_disk_check_ = now;
            if(disk_status(0) != RES_OK)
            {
                Abort();
                no_sd_card_ = true;
                rescan_slot_ = kNumSlots;
            }
            else
            {
                if(no_sd_card_)
                {
                    if(fs_leased_)
                        return;
                    f_mount(&fsi_.GetSDFileSystem(), fsi_.GetSDPath(), 1);
                    f_mkdir("BOWLS");
                    rescan_slot_ = 0;
                }
                no_sd_card_ = false;
            }
        }

        if(no_sd_card_ || fs_leased_)
            return;

        if(rescan_slot_ < kNumSlots)
        {
            LoadSlotFromCard(rescan_slot_, bank, true);
            ++rescan_slot_;
            return;
        }

        if(retry_blocked_)
        {
            if(static_cast<int32_t>(now - retry_at_) < 0)
                return;
            retry_blocked_ = false;
        }

        if(stage_ == kIdle)
        {
            __disable_irq();
            slot_ = bank.NextDirtySlot();
            if(slot_ < 0)
            {
                __enable_irq();
                return;
            }
            valid_snapshot_    = bank.IsValid(slot_);
            preset_snapshot_   = bank.Get(slot_);
            revision_snapshot_ = bank.Revision(slot_);
            __enable_irq();

            retries_ = 0;
            if(valid_snapshot_)
            {
                json_len_ = Serialize(preset_snapshot_, json_, kJsonSize);
                if(json_len_ == 0)
                {
                    FailCycle(now);
                    return;
                }
                stage_ = kOpenTmp;
            }
            else
                stage_ = kDelete;
        }

        switch(stage_)
        {
            case kIdle:
                break;

            case kOpenTmp:
            {
                char name[24];
                SlotName(slot_, name, true);
                if(f_open(&fil_, name,
                          (FA_OPEN_ALWAYS | FA_WRITE | FA_READ))
                   == FR_OK)
                    stage_ = kWrite;
                else
                    Retry(now, kOpenTmp);
                break;
            }

            case kWrite:
            {
                UINT bw = 0;
                const FRESULT result = f_write(&fil_, json_, json_len_, &bw);
                if(result == FR_OK && bw == json_len_)
                    stage_ = kSyncClose;
                else
                    Retry(now, kOpenTmp);
                break;
            }

            case kSyncClose:
            {
                FRESULT result = f_truncate(&fil_);
                if(result == FR_OK)
                    result = f_sync(&fil_);
                f_close(&fil_);
                if(result == FR_OK)
                    stage_ = kUnlinkOld;
                else
                    Retry(now, kOpenTmp);
                break;
            }

            case kUnlinkOld:
            {
                char name[24];
                SlotName(slot_, name, false);
                const FRESULT result = f_unlink(name);
                if(result == FR_OK || result == FR_NO_FILE)
                    stage_ = kRename;
                else
                    Retry(now, kOpenTmp);
                break;
            }

            case kRename:
            {
                char from[24], to[24];
                SlotName(slot_, from, true);
                SlotName(slot_, to, false);
                const FRESULT result = f_rename(from, to);
                if(result == FR_OK)
                    CompleteCycle(bank);
                else
                    Retry(now, kOpenTmp);
                break;
            }

            case kDelete:
            {
                char name[24];
                SlotName(slot_, name, false);
                const FRESULT result = f_unlink(name);
                if(result == FR_OK || result == FR_NO_FILE)
                    CompleteCycle(bank);
                else
                    Retry(now, kDelete);
                break;
            }

            case kBackoff:
            default:
                if(static_cast<int32_t>(now - retry_at_) >= 0)
                    stage_ = retry_stage_;
                break;
        }
    }

    bool NoSdCard() const { return no_sd_card_; }
    bool Busy() const { return stage_ != kIdle || rescan_slot_ < kNumSlots; }

    bool IsIncompatibleSlot(int slot) const
    {
        return slot >= 0 && slot < kNumSlots && incompatible_[slot];
    }

    void ClearIncompatibleSlot(int slot)
    {
        if(slot >= 0 && slot < kNumSlots)
            incompatible_[slot] = false;
    }

    bool TryAcquireFilesystem()
    {
        if(no_sd_card_ || fs_leased_ || Busy())
            return false;
        fs_leased_ = true;
        return true;
    }

    void ReleaseFilesystem() { fs_leased_ = false; }

    StatusEvent ConsumeStatusEvent()
    {
        const StatusEvent result = event_;
        event_ = kStatusNone;
        return result;
    }

  private:
    enum Stage : uint8_t
    {
        kIdle = 0,
        kOpenTmp,
        kWrite,
        kSyncClose,
        kUnlinkOld,
        kRename,
        kDelete,
        kBackoff
    };

    static void SlotName(int slot, char *out, bool tmp)
    {
        snprintf(out, 24, "BOWLS/P%02d.%s", slot + 1,
                 tmp ? "tmp" : "json");
    }

    void LoadSlotFromCard(int slot, BowlsPresetBank &bank, bool gaps_only)
    {
        if(gaps_only)
        {
            __disable_irq();
            const bool occupied = bank.IsValid(slot) || bank.IsDirty(slot);
            __enable_irq();
            if(occupied)
                return;
        }

        char name[24];
        SlotName(slot, name, false);
        const FRESULT opened = f_open(&read_fil_, name, FA_READ);
        if(opened == FR_NO_FILE)
        {
            incompatible_[slot] = false;
            return;
        }
        if(opened != FR_OK)
        {
            incompatible_[slot] = true;
            return;
        }
        if(f_size(&read_fil_) >= kJsonSize)
        {
            f_close(&read_fil_);
            incompatible_[slot] = true;
            return;
        }
        UINT bytes_read = 0;
        const FRESULT result
            = f_read(&read_fil_, read_buf_, kJsonSize - 1, &bytes_read);
        f_close(&read_fil_);
        if(result != FR_OK)
        {
            incompatible_[slot] = true;
            return;
        }

        read_buf_[bytes_read] = '\0';
        BowlsPreset parsed;
        if(!ParseWithPitchFallback(read_buf_, bytes_read, sizeof(read_buf_),
                                   factory_preset_, parsed))
        {
            incompatible_[slot] = true;
            return;
        }

        __disable_irq();
        if(!gaps_only || (!bank.IsValid(slot) && !bank.IsDirty(slot)))
        {
            bank.LoadSlot(slot, parsed);
            incompatible_[slot] = false;
        }
        __enable_irq();
    }

    void CompleteCycle(BowlsPresetBank &bank)
    {
        __disable_irq();
        bank.ClearDirtyIfRevision(slot_, revision_snapshot_);
        __enable_irq();
        stage_ = kIdle;
        if(failed_)
        {
            failed_ = false;
            event_ = kStatusRecovered;
        }
        else
            event_ = kStatusSaved;
    }

    void FailCycle(uint32_t now)
    {
        stage_ = kIdle;
        retry_blocked_ = true;
        retry_at_ = now + 1000;
        if(!failed_)
        {
            failed_ = true;
            event_ = kStatusFailed;
        }
    }

    void Retry(uint32_t now, Stage retry_stage)
    {
        if(stage_ == kOpenTmp || stage_ == kWrite || stage_ == kSyncClose
           || stage_ == kUnlinkOld || stage_ == kRename)
            f_close(&fil_);
        if(++retries_ >= kMaxRetries)
        {
            FailCycle(now);
            return;
        }
        retry_stage_ = retry_stage;
        retry_at_ = now + kBackoffMs;
        stage_ = kBackoff;
    }

    void Abort()
    {
        if(stage_ == kOpenTmp || stage_ == kWrite || stage_ == kSyncClose
           || stage_ == kUnlinkOld || stage_ == kRename)
            f_close(&fil_);
        stage_ = kIdle;
        retry_blocked_ = false;
    }

    daisy::SdmmcHandler   sdmmc_;
    daisy::FatFSInterface fsi_;
    FIL                   fil_;
    FIL                   read_fil_;

    volatile bool no_sd_card_;
    volatile bool fs_leased_;
    Stage         stage_;
    Stage         retry_stage_;
    int           slot_;
    int           retries_;
    int           rescan_slot_;
    uint32_t      last_disk_check_;
    uint32_t      retry_at_;
    bool          retry_blocked_;

    BowlsPreset   factory_preset_;
    BowlsPreset   preset_snapshot_;
    uint32_t      revision_snapshot_;
    bool          valid_snapshot_;
    volatile bool incompatible_[kNumSlots];
    bool          failed_;
    StatusEvent   event_;
    size_t        json_len_;
    alignas(32) char json_[kJsonSize];
    alignas(32) char read_buf_[kJsonSize];
};

} // namespace chompi
