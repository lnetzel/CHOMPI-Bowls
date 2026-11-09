/** @file MidiHandler.h
 *  @brief MIDI input transport for CHOMPI-BOWLS: hardware MIDI in
 *  (DIN/UART) and USB MIDI device, fixed channel 1, receive-only
 *  (plans/midi-input-and-cc-control.md).
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
    /** DIN/UART MIDI in (libDaisy default pins: USART1 RX = D13, TX =
     *  D14 - TX is configured but never used, this feature is
     *  receive-only) and the USB MIDI device on the EXTERNAL USB port
     *  (the panel USB-C). `usb_enable` = false skips USB entirely (the
     *  CPU_METER diagnostic build keeps USB serial logging instead -
     *  libDaisy's usbd_mode is a single global, so one USB device
     *  stack cannot present CDC logging and MIDI at the same time). */
    void Init(bool usb_enable)
    {
        usb_enable_ = usb_enable;
        router_.Init();

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

    /** Main-loop service (NEVER the audio ISR): Pipes' UART recovery -
     *  after any RX error HAL aborts the listen DMA and libDaisy never
     *  restarts it, so stop+start recovers; a recovered (or errored)
     *  stream can hold half-dropped notes, so the source's claims are
     *  released via the queue cleanup path. Then both parsers drain
     *  into the normalized per-source queues. */
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
    }

    MidiRouter&       Router() { return router_; }
    /** Diagnostics: dropped-event counter per source (overflows and
     *  backlog discards), main-loop reads only. */
    uint32_t Drops(int src) const { return router_.Queue(src).Drops(); }

  private:
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

    MidiRouter             router_;
    daisy::MidiUartHandler uart_midi_;
    daisy::MidiUsbHandler  usb_midi_;
    daisy::UartHandler     uart_;
    bool                   usb_enable_ = false;
};

} // namespace bowls

#endif // BOWLS_MIDI_HOST_TEST
