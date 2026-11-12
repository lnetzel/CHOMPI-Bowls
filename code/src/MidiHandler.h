/** @file MidiHandler.h
 *  @brief MIDI transport for CHOMPI-BOWLS: DIN/UART and USB MIDI
 *  input plus local-key note output (plans/midi-output-to-daw.md).
 *
 *  Two-layer design, per the plan's realtime rules:
 *    - The PURE layer (BowlsMidiEvent, MidiEventQueue, MidiRouter,
 *      ArbitrateRub) has no Daisy dependencies and is exercised by the
 *      host tests (code/tests/midi_test.cpp, `make midi-test`).
 *    - The Daisy layer (class BowlsMidi) owns the libDaisy UART/USB
 *      handlers. Transport parsing, UART error recovery and USB
 *      servicing run in the MAIN LOOP (never in the audio ISR); parsed
 *      channel-1 events are normalized into fixed-capacity per-source
 *      SPSC queues that the audio callback drains with a bounded,
 *      fairly-alternating budget.
 *
 *  Queue overflow never loses releases silently: a full queue latches a
 *  per-source cleanup request (kept outside the event ring), the
 *  producer discards that source's backlog, and the consumer releases
 *  the source's claims before accepting a fresh batch - so no stale
 *  queued Note On can survive an emergency cleanup.
 */
#pragma once

#include "MidiCcMap.h"

#include <cstdint>

namespace bowls
{
/** Normalized MIDI input event (channel 1 only, supported messages
 *  only - the transport layer filters everything else). Note On with
 *  velocity 0 is already normalized to kNoteOff here. */
struct BowlsMidiEvent
{
    enum Type : uint8_t
    {
        kNoteOn = 0,
        kNoteOff,
        kCc,
    };
    uint8_t type;
    uint8_t data1; // note number, or CC number
    uint8_t data2; // velocity, or CC value
};

/** Fixed-capacity SPSC ring: the main loop produces, the audio ISR
 *  consumes. Plain volatile indices (single core, main-vs-ISR); head
 *  is written only by the producer, tail only by the consumer. */
template <int kCapacity>
class MidiEventQueue
{
  public:
    void Init()
    {
        head_             = 0;
        tail_             = 0;
        cleanup_requested_ = false;
        drops_            = 0;
    }

    /** Producer side (main loop). Returns false when the event was
     *  dropped: once (on overflow, latching a cleanup request) and for
     *  every later event until the consumer has performed the cleanup. */
    bool Push(BowlsMidiEvent e)
    {
        if(cleanup_requested_)
        {
            ++drops_;
            return false;
        }
        const int next = head_ + 1 == kCapacity ? 0 : head_ + 1;
        if(next == tail_)
        {
            // Overflow: request a source cleanup (separate from the
            // ring) and discard the backlog; the consumer answers with
            // FinishCleanup(). Never overwrite unreleased Note Offs.
            cleanup_requested_ = true;
            ++drops_;
            return false;
        }
        buf_[head_] = e;
        head_       = next;
        return true;
    }

    /** Consumer side (audio ISR). */
    bool HasEvent() const { return head_ != tail_; }
    BowlsMidiEvent Pop()
    {
        BowlsMidiEvent e = buf_[tail_];
        tail_            = tail_ + 1 == kCapacity ? 0 : tail_ + 1;
        return e;
    }

    /** Consumer side: a latched overflow (or a transport-loss) cleanup
     *  is pending for this source. */
    bool CleanupRequested() const { return cleanup_requested_; }
    /** Consumer side: discard the backlog and re-open the queue. The
     *  producer discards while the flag is latched, so the ring holds
     *  only pre-overflow events; dropping them guarantees no stale
     *  Note On outlives the cleanup. */
    void FinishCleanup()
    {
        tail_              = head_;
        cleanup_requested_ = false;
    }
    /** Producer side: transport loss (UART error / USB disconnect)
     *  requests the same source cleanup without an overflow. */
    void RequestCleanup() { cleanup_requested_ = true; }

    /** Diagnostics (main-loop reads): events dropped since boot. */
    uint32_t Drops() const { return drops_; }

  private:
    BowlsMidiEvent  buf_[kCapacity];
    volatile int      head_ = 0;
    volatile int      tail_ = 0;
    volatile bool     cleanup_requested_ = false;
    volatile uint32_t drops_ = 0;
};

/** The two MIDI transports as queue/source indices. */
enum MidiTransport : int
{
    kDin = 0,
    kUsb = 1,
    kMidiTransportCount = 2,
};

/** Local keyboard note event for MIDI output. */
struct MidiOutEvent
{
    enum Type : uint8_t
    {
        kNoteOn = 0,
        kNoteOff,
    };
    uint8_t type;
    uint8_t note;
    uint8_t velocity;
};

/** Fixed-capacity SPSC output queue: the audio callback produces and
 *  the main loop consumes. Overflow requests destination-local cleanup
 *  so active DAW notes can be released before fresh events. */
template <int kCapacity>
class MidiOutEventQueue
{
  public:
    void Init()
    {
        head_ = 0;
        tail_ = 0;
        cleanup_requested_ = false;
        generation_ = 0;
        drops_ = 0;
    }

    bool Push(MidiOutEvent event)
    {
        if(cleanup_requested_)
        {
            ++drops_;
            return false;
        }
        const int next = head_ + 1 == kCapacity ? 0 : head_ + 1;
        if(next == tail_)
        {
            RequestCleanup();
            ++drops_;
            return false;
        }
        events_[head_] = event;
        head_ = next;
        return true;
    }

    bool HasEvent() const { return head_ != tail_; }
    MidiOutEvent Peek() const { return events_[tail_]; }
    void Pop() { tail_ = tail_ + 1 == kCapacity ? 0 : tail_ + 1; }
    bool CleanupRequested() const { return cleanup_requested_; }
    uint32_t Generation() const { return generation_; }

    void RequestCleanup()
    {
        if(!cleanup_requested_)
        {
            cleanup_requested_ = true;
            ++generation_;
        }
    }

    void FinishCleanup()
    {
        tail_ = head_;
        cleanup_requested_ = false;
    }

    uint32_t Drops() const { return drops_; }

  private:
    MidiOutEvent events_[kCapacity];
    volatile int head_ = 0;
    volatile int tail_ = 0;
    volatile bool cleanup_requested_ = false;
    volatile uint32_t generation_ = 0;
    volatile uint32_t drops_ = 0;
};

/** Fans local keyboard events into independent DIN and USB queues. The
 *  held-key bookkeeping belongs to the audio-side producer and resets
 *  whenever a consumer requests destination cleanup. */
class MidiOutRouter
{
  public:
    static constexpr int kQueueCapacity = 64;

    void Init(bool usb_enabled)
    {
        enabled_[kDin] = true;
        enabled_[kUsb] = usb_enabled;
        for(int dst = 0; dst < kMidiTransportCount; ++dst)
        {
            queues_[dst].Init();
            generation_seen_[dst] = 0;
            for(int key = 0; key < kNoteCount; ++key)
                held_[dst][key] = false;
        }
    }

    bool PushLocalNoteOn(int key, uint8_t velocity = 127)
    {
        if(!ValidKey(key))
            return false;
        bool queued = false;
        for(int dst = 0; dst < kMidiTransportCount; ++dst)
        {
            SyncGeneration(dst);
            if(!enabled_[dst])
                continue;
            const MidiOutEvent event = {
                MidiOutEvent::kNoteOn,
                static_cast<uint8_t>(MidiCcMap::kNoteBase + key), velocity};
            if(queues_[dst].Push(event))
            {
                held_[dst][key] = true;
                queued = true;
            }
            else
                ClearHeld(dst);
        }
        return queued;
    }

    bool PushLocalNoteOff(int key)
    {
        if(!ValidKey(key))
            return false;
        bool queued = false;
        for(int dst = 0; dst < kMidiTransportCount; ++dst)
        {
            SyncGeneration(dst);
            if(!enabled_[dst] || !held_[dst][key])
                continue;
            const MidiOutEvent event = {
                MidiOutEvent::kNoteOff,
                static_cast<uint8_t>(MidiCcMap::kNoteBase + key), 0};
            if(queues_[dst].Push(event))
            {
                held_[dst][key] = false;
                queued = true;
            }
            else
                ClearHeld(dst);
        }
        return queued;
    }

    MidiOutEventQueue<kQueueCapacity>& Queue(int dst) { return queues_[dst]; }
    const MidiOutEventQueue<kQueueCapacity>& Queue(int dst) const
    {
        return queues_[dst];
    }

  private:
    static constexpr int kNoteCount
        = MidiCcMap::kNoteLast - MidiCcMap::kNoteBase + 1;

    static bool ValidKey(int key) { return key >= 0 && key < kNoteCount; }

    void ClearHeld(int dst)
    {
        for(int key = 0; key < kNoteCount; ++key)
            held_[dst][key] = false;
    }

    void SyncGeneration(int dst)
    {
        const uint32_t generation = queues_[dst].Generation();
        if(generation != generation_seen_[dst])
        {
            ClearHeld(dst);
            generation_seen_[dst] = generation;
        }
    }

    MidiOutEventQueue<kQueueCapacity> queues_[kMidiTransportCount];
    bool enabled_[kMidiTransportCount] = {false, false};
    bool held_[kMidiTransportCount][kNoteCount] = {};
    uint32_t generation_seen_[kMidiTransportCount] = {};
};

/** Owns the two per-source queues and the fair, bounded dispatch the
 *  audio callback uses. Pure C++ (host-testable). */
class MidiRouter
{
  public:
    static constexpr int kQueueCapacity = 128; // per transport

    void Init()
    {
        for(int s = 0; s < kMidiTransportCount; ++s)
            queues_[s].Init();
        dispatch_turn_ = 0;
    }

    MidiEventQueue<kQueueCapacity>& Queue(int src) { return queues_[src]; }
    const MidiEventQueue<kQueueCapacity>& Queue(int src) const
    {
        return queues_[src];
    }

    /** Drain up to `budget` items total, fairly alternating the two
     *  sources (order within each source is preserved; a nonempty
     *  source is never starved by the other). `apply(src, event)`
     *  handles one event; `cleanup(src)` performs a source's overflow
     *  /transport-loss claim cleanup (both run in the consumer's
     *  context). Cleanups count against the budget. Returns the number
     *  of dispatched items (events + cleanups). */
    template <typename ApplyFn, typename CleanupFn>
    int Dispatch(int budget, ApplyFn apply, CleanupFn cleanup)
    {
        int dispatched = 0;
        while(dispatched < budget)
        {
            const int first  = dispatch_turn_;
            const int second = 1 - dispatch_turn_;
            int       src    = -1;
            if(queues_[first].CleanupRequested()
               || queues_[first].HasEvent())
                src = first;
            else if(queues_[second].CleanupRequested()
                    || queues_[second].HasEvent())
                src = second;
            if(src < 0)
                break;
            if(queues_[src].CleanupRequested())
            {
                cleanup(src);
                queues_[src].FinishCleanup();
            }
            else
            {
                apply(src, queues_[src].Pop());
            }
            dispatch_turn_ = 1 - src;
            ++dispatched;
        }
        return dispatched;
    }

  private:
    MidiEventQueue<kQueueCapacity> queues_[kMidiTransportCount];
    int                            dispatch_turn_ = 0;
};

/** One side of the wheel/MIDI rimming arbitration (plan section 5):
 *  the local wheel and each MIDI source compute their candidate
 *  independently; per eligible (mode, key) the HIGHEST level wins (not
 *  the sum, so mixed controllers never double the drive), and the
 *  winner's texture applies. Ties go to the latest control event
 *  (each candidate carries a monotonically increasing sequence number
 *  of its last level/texture change). */
struct RubCandidate
{
    float    level = 0.0f;
    bool     clean = false;
    uint32_t seq   = 0;
};

inline RubCandidate ArbitrateRub(RubCandidate a, RubCandidate b)
{
    if(b.level > a.level || (b.level == a.level && b.seq > a.seq))
        return b;
    return a;
}

} // namespace bowls

/* ---------------------------------------------------------------------------
 * Daisy transport layer (firmware only; the host tests define
 * BOWLS_MIDI_HOST_TEST to stop at the pure layer above).
 * ------------------------------------------------------------------------- */
#if !defined(BOWLS_MIDI_HOST_TEST)

#include "daisy_seed.h"
#include "hid/midi.h"

namespace bowls
{
class BowlsMidi
{
  public:
    /** DIN/UART MIDI on USART1 (RX = D13, TX = D14) and the USB MIDI
     *  device on the EXTERNAL USB port (the panel USB-C). `usb_enable`
     *  = false skips USB entirely (the
     *  CPU_METER diagnostic build keeps USB serial logging instead -
     *  libDaisy's usbd_mode is a single global, so one USB device
     *  stack cannot present CDC logging and MIDI at the same time). */
    void Init(bool usb_enable)
    {
        usb_enable_ = usb_enable;
        router_.Init();
        out_router_.Init(usb_enable_);

        daisy::MidiUartHandler::Config uart_cfg; // defaults: USART_1, D13/D14
        uart_midi_.Init(uart_cfg);
        uart_midi_.StartReceive();
        // Pipes' recovery needs the raw UART handle (CheckError etc.).
        uart_ = uart_midi_.GetMutableTransport().GetUartHandle();

        if(usb_enable_)
        {
            daisy::MidiUsbHandler::Config usb_cfg;
            usb_cfg.transport_config.periph
                = daisy::MidiUsbTransport::Config::EXTERNAL;
            usb_cfg.transport_config.tx_retry_count = 0; // never block TX
            usb_midi_.Init(usb_cfg);
            usb_midi_.Listen();
        }
    }

    /** Main-loop service (NEVER the audio ISR): recover UART receive
     *  errors, drain both input parsers, and make bounded nonblocking
     *  progress on independent MIDI output queues. */
    void Service()
    {
        if(uart_.CheckError() != 0)
        {
            uart_.DmaListenStop();
            uart_midi_.StartReceive();
            router_.Queue(kDin).RequestCleanup();
        }
        else
        {
            uart_midi_.Listen(); // restarts a silently stopped listener
        }
        DrainSource(kDin, uart_midi_);
        if(usb_enable_)
            DrainSource(kUsb, usb_midi_);
        ServiceOutput(kDin);
        if(usb_enable_)
            ServiceOutput(kUsb);
    }

    MidiRouter&       Router() { return router_; }
    MidiOutRouter&    OutRouter() { return out_router_; }
    /** Diagnostics: dropped-event counter per source (overflows and
     *  backlog discards), main-loop reads only. */
    uint32_t Drops(int src) const { return router_.Queue(src).Drops(); }

  private:
    static void DinTxComplete(void* context, daisy::UartHandler::Result result)
    {
        BowlsMidi* self = static_cast<BowlsMidi*>(context);
        self->din_tx_result_ = result;
        self->din_tx_done_ = true;
    }

    void ServiceOutput(int dst)
    {
        MidiOutEventQueue<MidiOutRouter::kQueueCapacity>& queue
            = out_router_.Queue(dst);
        if(dst == kDin)
        {
            if(din_tx_pending_)
            {
                if(!din_tx_done_)
                    return;
                din_tx_pending_ = false;
                din_tx_done_ = false;
                if(din_tx_result_ != daisy::UartHandler::Result::OK)
                {
                    MarkOutputUncertain(kDin, din_tx_event_);
                    queue.RequestCleanup();
                }
                else
                {
                    ApplyOutputEvent(kDin, din_tx_event_);
                    if(!queue.CleanupRequested() && queue.HasEvent())
                        queue.Pop();
                }
            }
            if(queue.CleanupRequested())
            {
                if(StartDinCleanupNoteOff())
                    return;
                if(!HasActiveOutputNotes(kDin))
                    queue.FinishCleanup();
                return;
            }
            if(!queue.HasEvent())
                return;
            din_tx_event_ = queue.Peek();
            if(StartDinEvent(din_tx_event_))
                din_tx_pending_ = true;
            else
            {
                MarkOutputUncertain(kDin, din_tx_event_);
                queue.RequestCleanup();
            }
            return;
        }

        if(queue.CleanupRequested())
        {
            for(int key = 0; key < kNoteCount; ++key)
            {
                if(!active_notes_[kUsb][key])
                    continue;
                if(usb_midi_.SendNoteOff(MidiCcMap::kChannel,
                                         MidiCcMap::kNoteBase + key, 0))
                    active_notes_[kUsb][key] = false;
                return;
            }
            queue.FinishCleanup();
            return;
        }
        if(!queue.HasEvent())
            return;
        const MidiOutEvent event = queue.Peek();
        const bool sent = event.type == MidiOutEvent::kNoteOn
                              ? usb_midi_.SendNoteOn(MidiCcMap::kChannel,
                                                     event.note,
                                                     event.velocity)
                              : usb_midi_.SendNoteOff(MidiCcMap::kChannel,
                                                      event.note, 0);
        if(sent)
        {
            ApplyOutputEvent(kUsb, event);
            if(!queue.CleanupRequested())
                queue.Pop();
        }
        else
        {
            MarkOutputUncertain(kUsb, event);
            queue.RequestCleanup();
        }
    }

    bool StartDinEvent(const MidiOutEvent& event)
    {
        din_tx_buffer_[0] = static_cast<uint8_t>(
            (event.type == MidiOutEvent::kNoteOn ? 0x90 : 0x80)
            | MidiCcMap::kChannel);
        din_tx_buffer_[1] = event.note;
        din_tx_buffer_[2] = event.velocity;
        din_tx_done_ = false;
        return uart_.DmaTransmit(din_tx_buffer_, sizeof(din_tx_buffer_),
                                 nullptr, DinTxComplete, this)
               == daisy::UartHandler::Result::OK;
    }

    bool StartDinCleanupNoteOff()
    {
        for(int key = 0; key < kNoteCount; ++key)
        {
            if(!active_notes_[kDin][key])
                continue;
            const MidiOutEvent event = {
                MidiOutEvent::kNoteOff,
                static_cast<uint8_t>(MidiCcMap::kNoteBase + key), 0};
            if(StartDinEvent(event))
            {
                din_tx_event_ = event;
                din_tx_pending_ = true;
                return true;
            }
            return false;
        }
        return false;
    }

    bool HasActiveOutputNotes(int dst) const
    {
        for(int key = 0; key < kNoteCount; ++key)
            if(active_notes_[dst][key])
                return true;
        return false;
    }

    void ApplyOutputEvent(int dst, const MidiOutEvent& event)
    {
        const int key = MidiCcMap::NoteToKey(event.note);
        if(key >= 0)
            active_notes_[dst][key] = event.type == MidiOutEvent::kNoteOn;
    }

    void MarkOutputUncertain(int dst, const MidiOutEvent& event)
    {
        const int key = MidiCcMap::NoteToKey(event.note);
        if(key >= 0 && event.type == MidiOutEvent::kNoteOn)
            active_notes_[dst][key] = true;
    }

    /** Pop every parsed event from one libDaisy handler, filter to
     *  channel 1 and the supported messages, normalize (velocity-0
     *  Note On = Note Off) and enqueue. Parsing runs HERE (thread
     *  level); the audio ISR only pops the fixed queues. */
    template <typename Handler>
    void DrainSource(int src, Handler& handler)
    {
        while(handler.HasEvents())
        {
            const daisy::MidiEvent ev = handler.PopEvent();
            if(ev.channel != MidiCcMap::kChannel)
                continue; // fixed channel 1; all other channels ignored
            BowlsMidiEvent n;
            switch(ev.type)
            {
                case daisy::NoteOn:
                    if(MidiCcMap::NoteToKey(ev.data[0]) < 0)
                        continue; // outside the 48..72 key map
                    n.type = ev.data[1] > 0 ? BowlsMidiEvent::kNoteOn
                                            : BowlsMidiEvent::kNoteOff;
                    n.data1 = ev.data[0];
                    n.data2 = ev.data[1];
                    router_.Queue(src).Push(n);
                    break;
                case daisy::NoteOff:
                    if(MidiCcMap::NoteToKey(ev.data[0]) < 0)
                        continue;
                    n.type = BowlsMidiEvent::kNoteOff;
                    n.data1 = ev.data[0];
                    n.data2 = 0; // release velocity is ignored
                    router_.Queue(src).Push(n);
                    break;
                case daisy::ControlChange:
                    if(!MidiCcMap::IsSupportedCc(ev.data[0]))
                        continue; // unsupported CCs never fill the queue
                    n.type = BowlsMidiEvent::kCc;
                    n.data1 = ev.data[0];
                    n.data2 = ev.data[1];
                    router_.Queue(src).Push(n);
                    break;
                default:
                    break; // Program Change, pitch bend, pressure,
                           // SysEx, clock/transport: ignored
            }
        }
    }

    static constexpr int kNoteCount
        = MidiCcMap::kNoteLast - MidiCcMap::kNoteBase + 1;

    MidiRouter             router_;
    MidiOutRouter          out_router_;
    daisy::MidiUartHandler uart_midi_;
    daisy::MidiUsbHandler  usb_midi_;
    daisy::UartHandler     uart_;
    bool                   usb_enable_ = false;
    bool                   active_notes_[kMidiTransportCount][kNoteCount] = {};
    uint8_t                din_tx_buffer_[3] = {};
    MidiOutEvent           din_tx_event_{};
    volatile bool          din_tx_pending_ = false;
    volatile bool          din_tx_done_ = false;
    volatile daisy::UartHandler::Result din_tx_result_ = daisy::UartHandler::Result::OK;
};

} // namespace bowls

#endif // BOWLS_MIDI_HOST_TEST
