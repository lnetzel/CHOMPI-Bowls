/** @file PresetStorage.h
 *  @brief SD-card state storage for CHOMPI-BOWLS: mount + boot load of
 *  BOWLS/state.json, and a staged, non-blocking writer that runs ONLY
 *  from the main loop.
 *
 *  Single-file variant of the CHOMPI-PhaseOff PresetStorage: the state is
 *  written as state.tmp, synced, then renamed over state.json, so a power
 *  loss can corrupt at most the write in flight and the previous file
 *  survives. Each Service() call performs at most one small FatFS stage,
 *  so the watchdog keeps being fed and the audio ISR is never stalled.
 *
 *  Card presence is re-checked once per second via disk_status(0);
 *  without a card the firmware simply runs on its RAM defaults and saves
 *  are dropped.
 */
#pragma once
#include "daisy.h"
#include "fatfs.h"
#include "diskio.h"
#include "BowlsPreset.h"

namespace chompi
{

class PresetStorage
{
  public:
    /* v6 state (mode + four 75-entry banks) serializes to ~1 KB worst
     * case - well past one 512 B sector, so reads/writes go
     * multi-sector; 1536 B leaves headroom (measured in the host
     * preset test). Buffers stay members in SRAM (.bss): the SDMMC IDMA
     * cannot reach DTCMRAM stack. */
    static constexpr size_t   kJsonSize    = 1536;
    static constexpr int      kMaxRetries  = 3;
    static constexpr uint32_t kBackoffMs   = 250;

    /** One-shot storage status change, for a single serial diagnostic
     *  per failure/recovery (no panel LED by request). */
    enum StatusEvent : uint8_t
    {
        kStatusNone = 0,
        kStatusFailed,   // a write cycle exhausted its retries
        kStatusRecovered // a write completed after a failure
    };

    void Init()
    {
        no_sd_card_      = true;
        stage_           = kIdle;
        retries_         = 0;
        last_disk_check_ = 0;
        retry_at_        = 0;
        pending_         = false;
        save_seq_        = 0;
        written_seq_     = 0;
        failed_          = false;
        event_           = kStatusNone;

        /* same SDMMC/FatFS init as chompi-tape/PhaseOff; with no card
         * inserted this configures the peripheral without touching a
         * card and the disk_status check below leaves storage disabled
         * instead of hanging the boot */
        daisy::SdmmcHandler::Config sd_cfg;
        sd_cfg.speed = daisy::SdmmcHandler::Speed::FAST;
        sd_cfg.width = daisy::SdmmcHandler::BusWidth::BITS_4;
        sdmmc_.Init(sd_cfg);
        fsi_.Init(daisy::FatFSInterface::Config::MEDIA_SD);
        f_mount(&fsi_.GetSDFileSystem(), fsi_.GetSDPath(), 1);
        f_mkdir("BOWLS"); /* FR_EXIST is fine */
        no_sd_card_ = disk_status(0) != RES_OK;
    }

    /** Boot load. Returns false (caller keeps defaults) when there is no
     *  card, no file, or the file fails validation. */
    bool Load(BowlsPreset &p)
    {
        if(no_sd_card_)
            return false;
        /* read_fil_ and read_buf_ MUST be members (SRAM, .bss), never
         * stack locals: with _FS_TINY=0 every FIL carries its own
         * 512 B sector buffer and f_read DMAs the data sector into it.
         * The main stack is in DTCMRAM, which the SDMMC IDMA cannot
         * reach, so a stack FIL stalls the read forever. */
        if(f_open(&read_fil_, "BOWLS/state.json", FA_READ) != FR_OK)
            return false;
        UINT    br = 0;
        FRESULT r  = f_read(&read_fil_, read_buf_, kJsonSize - 1, &br);
        f_close(&read_fil_);
        if(r != FR_OK)
            return false;
        read_buf_[br] = '\0';
        return Parse(read_buf_, br, p);
    }

    /** Snapshot the state and request a save. If a write is already in
     *  flight, the newest snapshot is written when the next cycle starts
     *  (save_seq_ tracks that a newer snapshot exists). */
    void RequestSave(const BowlsPreset &p)
    {
        snapshot_ = p;
        ++save_seq_;
        pending_ = true;
    }

    /** One storage stage per main-loop pass. */
    void Service()
    {
        const uint32_t now = daisy::System::GetNow();

        /* 1 s card-presence check */
        if(now - last_disk_check_ >= 1000)
        {
            last_disk_check_ = now;
            const bool gone  = disk_status(0) != RES_OK;
            if(gone)
            {
                Abort(); /* drop any in-flight write */
                no_sd_card_ = true;
            }
            else
            {
                if(no_sd_card_)
                {
                    /* card (re-)inserted: re-mount; the next RequestSave
                     * writes the current RAM state */
                    f_mount(&fsi_.GetSDFileSystem(), fsi_.GetSDPath(), 1);
                    f_mkdir("BOWLS");
                }
                no_sd_card_ = false;
            }
        }

        if(no_sd_card_ || !pending_)
            return;

        switch(stage_)
        {
            case kIdle:
                json_len_    = Serialize(snapshot_, json_, kJsonSize);
                written_seq_ = save_seq_;
                if(json_len_ > 0)
                {
                    retries_ = 0;
                    stage_   = kOpenTmp;
                }
                else
                {
                    pending_ = false;
                }
                break;

            case kOpenTmp:
                if(f_open(&fil_, "BOWLS/state.tmp",
                          (FA_OPEN_ALWAYS | FA_WRITE | FA_READ))
                   == FR_OK)
                {
                    stage_ = kWrite;
                }
                else
                {
                    Retry(now, kOpenTmp);
                }
                break;

            case kWrite:
            {
                UINT bw = 0;
                const FRESULT r = f_write(&fil_, json_, json_len_, &bw);
                if(r == FR_OK && bw == json_len_)
                    stage_ = kSyncClose;
                else
                    Retry(now, kOpenTmp);
                break;
            }

            case kSyncClose:
            {
                /* truncate at the write pointer: the tmp file may hold a
                 * longer stale body from a previous attempt, and trailing
                 * garbage would fail JSON_Validate on load */
                FRESULT r = f_truncate(&fil_);
                if(r == FR_OK)
                    r = f_sync(&fil_);
                f_close(&fil_);
                if(r == FR_OK)
                    stage_ = kUnlinkOld;
                else
                    Retry(now, kOpenTmp);
                break;
            }

            case kUnlinkOld:
            {
                const FRESULT r = f_unlink("BOWLS/state.json");
                if(r == FR_OK || r == FR_NO_FILE)
                    stage_ = kRename;
                else
                    Retry(now, kOpenTmp);
                break;
            }

            case kRename:
                /* done either way: a failed rename is not retried (the
                 * .tmp stays behind and is ignored by Load) */
                f_rename("BOWLS/state.tmp", "BOWLS/state.json");
                stage_ = kIdle;
                if(written_seq_ == save_seq_)
                    pending_ = false;
                /* else: a newer snapshot arrived mid-write - loop again */
                if(failed_)
                {
                    /* a write cycle completed after a failure: report
                     * the recovery once */
                    failed_ = false;
                    event_  = kStatusRecovered;
                }
                break;

            case kBackoff:
            default:
                if(static_cast<int32_t>(now - retry_at_) >= 0)
                    stage_ = retry_stage_;
                break;
        }
    }

    /** Card state, polled by the UI. */
    bool NoSdCard() const { return no_sd_card_; }

    /** A write cycle is in flight. */
    bool Busy() const { return stage_ != kIdle; }

    /** A write cycle exhausted its retries and no successful write has
     *  completed since. The RAM state is kept and saving retries on the
     *  next change / card insertion. */
    bool Degraded() const { return failed_; }

    /** Consume the one-shot failure/recovery event (kStatusNone when
     *  nothing changed): the caller emits ONE serial diagnostic per
     *  transition, never per retry. */
    StatusEvent ConsumeStatusEvent()
    {
        const StatusEvent e = event_;
        event_              = kStatusNone;
        return e;
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
        kBackoff
    };

    /** Failed stage: retry a few times with a short backoff, then drop
     *  the write (the RAM state remains; the next change re-requests).
     *  The failure is reported once and stays latched until a write
     *  completes. */
    void Retry(uint32_t now, Stage retry_stage)
    {
        if(stage_ == kOpenTmp || stage_ == kWrite || stage_ == kSyncClose
           || stage_ == kUnlinkOld)
            f_close(&fil_); /* harmless if not open */
        if(++retries_ >= kMaxRetries)
        {
            stage_ = kIdle;
            if(!failed_)
            {
                failed_ = true;
                event_  = kStatusFailed;
            }
            return;
        }
        retry_stage_ = retry_stage;
        retry_at_    = now + kBackoffMs;
        stage_       = kBackoff;
    }

    /** Card vanished mid-write: close and drop back to idle. */
    void Abort()
    {
        if(stage_ == kOpenTmp || stage_ == kWrite || stage_ == kSyncClose
           || stage_ == kUnlinkOld)
            f_close(&fil_);
        stage_ = kIdle;
    }

    daisy::SdmmcHandler   sdmmc_;
    daisy::FatFSInterface fsi_;
    FIL                   fil_;
    FIL                   read_fil_; /* own FIL for the reader: the write
                                      * path's fil_ may hold an open tmp
                                      * file; both must stay off the
                                      * DTCMRAM stack (IDMA, see above) */

    volatile bool no_sd_card_;
    Stage         stage_;
    Stage         retry_stage_;
    int           retries_;
    uint32_t      last_disk_check_;
    uint32_t      retry_at_;

    BowlsPreset   snapshot_;
    volatile bool pending_;
    uint32_t      save_seq_;
    uint32_t      written_seq_;
    size_t        json_len_ = 0;
    bool          failed_   = false; /* write failure latched until success */
    StatusEvent   event_    = kStatusNone;
    char          json_[kJsonSize];
    char          read_buf_[kJsonSize];
};

} // namespace chompi
