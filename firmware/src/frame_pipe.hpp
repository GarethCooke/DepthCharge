// firmware/src/frame_pipe.hpp — the one place the network task and the feed task meet.
//
// There are exactly two cross-task hand-offs in this firmware, and they are
// deliberately the only two:
//
//   network task  --[ FramePipe ]-->  feed task (Core 0)  --[ SnapshotChannel ]-->  consumer (Core 1)
//
// SnapshotChannel is the engine's, built at stage A. This is its transport-side
// twin, and it exists for a reason invariant #8 forces rather than for tidiness.
//
// WHY THE FEED IS A TASK AND NOT THE WEBSOCKET CALLBACK.
//
// The obvious design is to run the whole pipeline inside the esp_websocket_client
// event callback: no queue, no copy, one writer by construction. Two facts kill
// it, and both were checked in the installed headers rather than assumed.
//
//   1. The RX watchdog must fire when data STOPS. A callback-driven design has
//      nothing running during silence, so the watchdog would have to live in a
//      timer or a second task — and it raises Gap{Disconnect}, which means
//      applying an event to the book. That would be a second writer to the book
//      and a straight violation of invariant #8. The watchdog and the frames
//      must therefore be serviced by the *same* context, which means a task that
//      can block with a timeout: the timeout IS the watchdog, exactly as the M3
//      brief suggests.
//   2. esp_websocket_client_config_t in this IDF vintage has no task_core_id, so
//      the client's own task cannot be pinned. "Feed pipeline on Core 0" is only
//      achievable in a task we create ourselves.
//
// So: the callback fills a slot and posts it; the feed task owns the engine.
//
// OWNERSHIP. A slot is owned by exactly one side at a time, and the queues are
// the transfer. The network task acquires a free slot, fills it across however
// many WebSocket chunks the message takes, and publishes it; the feed task
// parses it and recycles it. Nothing is shared while it is being written, so
// there is no lock and nothing for the writer to wait on.
//
// NO HEAP. Both slots and both queues are static storage, sized at compile time.
// FreeRTOS queues copy small fixed-size items, so posting is a memcpy of eight
// bytes, not an allocation.
#pragma once

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "gap_histogram.hpp"

namespace depthcharge::fw {

// The largest WebSocket message we will reassemble.
//
// Measured, not guessed: across the two committed captures (2,694 frames) the
// largest Anvil message is 8,726 bytes and the mean is 6,486 — both `book`
// frames at 84-126 levels a side. 16 KiB is 1.9x the largest ever observed,
// which is roughly double the depth Anvil has ever published.
//
// A message that does not fit is a DEFINED DROP: counted in `oversize`, never a
// reallocation and never a partial frame handed to the parser. That is the right
// failure — Anvil republishes the whole book every ~80 ms, so a dropped frame
// costs one refresh, whereas a growing buffer on a microcontroller costs the
// device. If `oversize` is ever non-zero on the bench, raise this constant;
// do not make it dynamic.
inline constexpr std::size_t kFrameCapacity = 16 * 1024;

// Four slots — and the first bench run is why it is not two.
//
// Two is the minimum that lets the network task and the feed task avoid stalling
// each other, and the reasoning for it ("frames arrive ~80 ms apart and a parse
// is tens of microseconds") was right about the averages and wrong about the
// distribution. Measured on the board over 30 s: 190 messages published, **33
// dropped for want of a free slot — 16% of everything that arrived**, climbing
// linearly rather than as a start-up transient.
//
// The mechanism is burst arrival against slot residency. An ~8 KB message is
// held across the three DATA events it takes to reassemble at a 4 KiB RX buffer,
// while the feed task spends up to 8.5 ms (measured `worst_frame`) on the
// previous one; Anvil coalesces its book stream, so three messages landing
// back-to-back is routine, not rare. Two slots have no cushion for that at all.
//
// Losing those messages costs nothing in correctness — book frames are
// idempotent full replaces and `parse_errors` stayed 0 — but it lengthens the
// observed gap between events, and that gap is what the RX watchdog measures.
// The first run showed `worst_gap` at 721 ms against a 1000 ms threshold sized
// for M1's measured 640 ms: a 1.39x margin where the design assumed 1.6x. Every
// dropped message pushes it further toward a **false STALE**, which is exactly
// the lie invariant #5 exists to prevent. That, not the wasted bandwidth, is why
// this constant moved.
//
// Cost: 4 x kFrameCapacity = 64 KiB of internal SRAM, against 30.7% used at two
// slots. The alternative lever is kWsRxBufferBytes — one DATA event per message
// instead of three would cut slot residency and the esp_event allocation rate
// together — and it is deliberately NOT pulled here, because chunk reassembly
// being exercised on every frame is worth keeping now that the wire has proven
// it works (190 multi-chunk messages, zero parse errors).
inline constexpr std::size_t kFrameSlots = 4;

// How deep the ready queue is: one entry per slot, plus two.
//
// The two are headroom for status events. A Connected/Disconnected that failed
// to enqueue would be an outage the book never hears about, which is the one
// failure invariant #5 does not tolerate, so it must always be possible to post
// one even with every slot in flight.
//
// Named because the console prints the backlog against it, and a queue depth
// that is `kFrameSlots + 2` in one file and `kFrameSlots + 2` in another is one
// edit away from a log line that reports a fraction of the wrong denominator.
inline constexpr std::size_t kReadyQueueDepth = kFrameSlots + 2;

// What the feed task receives. Frame data and connection state travel the same
// queue on purpose: they must be handled in the order they happened, or a Gap
// could overtake the last frame before it and the book would go stale over data
// it had already applied.
struct FeedMessage {
    enum class Kind : std::uint8_t {
        Frame,         // `slot` holds `len` bytes of verbatim wire text
        Connected,     // WS upgrade accepted
        Disconnected,  // socket closed, errored, or cleanly shut
    };

    Kind kind = Kind::Frame;
    std::uint8_t slot = 0;
    std::uint32_t len = 0;
    // When this message finished arriving off the socket, stamped on the
    // client's task. It rides the queue rather than being looked up because the
    // gap this firmware is hunting is precisely the interval between here and
    // the feed task reading it, and a timestamp taken on the reading side
    // cannot measure the wait it is inside.
    std::int64_t arrival_us = 0;
};

// Counters for things that can only go wrong on the transport side. All of them
// should read zero on a healthy bench run; each is a distinct diagnosis rather
// than one "errors" number, because they call for different fixes.
struct FramePipeStats {
    std::uint32_t frames_published = 0;   // complete messages handed to the feed
    std::uint32_t oversize = 0;           // message > kFrameCapacity, dropped
    std::uint32_t no_slot = 0;            // every slot busy when a message began
    std::uint32_t queue_full = 0;         // feed task not draining (should never)
    std::uint32_t abandoned = 0;          // a new message began mid-message
    std::uint32_t continuation = 0;       // WS continuation frames seen (see .cpp)
    std::uint32_t control = 0;            // ping/pong/close opcodes

    // Inbound volume, so the next bench run MEASURES the wire instead of
    // inferring it. The first run could only be compared against M0's July
    // figures (15.5 frames/s, 8,726 B max) and came out at ~6.7 messages/s
    // attempted — but with no way to tell a slower server from a client that is
    // missing frames upstream of the reassembler. These four make that a
    // reading rather than an argument: the console prints per-window rates from
    // them, and a simultaneous host capture gives the other end of the
    // comparison.
    std::uint64_t bytes_published = 0;    // total payload bytes handed to the feed
    std::uint32_t largest_message = 0;    // vs kFrameCapacity — is 16 KiB still enough?
    std::uint32_t smallest_message = 0;   // 0 until the first message
    std::uint32_t chunks = 0;             // DATA events accepted; /messages = chunks per frame

    // THE ARRIVAL HALF OF THE ARRIVAL-VS-EVENT SPLIT.
    //
    // Inter-arrival gaps between whole messages, measured where the bytes land
    // rather than where they are consumed. This is the histogram that decides
    // which half of the board the 2026-08 stall lives in, and the reading is
    // blunt: a >1 s bucket that fills HERE means the bytes stopped coming —
    // Wi-Fi, TLS, the socket, the server's egress to this client. A >1 s bucket
    // that fills only in FeedTask::Stats::event_gaps, with this one clean, means
    // the bytes arrived and something on the board sat on them.
    //
    // `messages_arrived` counts every whole message including those dropped for
    // want of a slot, so it is deliberately >= frames_published.
    std::uint32_t messages_arrived = 0;
    GapHistogram arrival_gaps{};
};

class FramePipe {
public:
    // Must be called before either task starts. Returns false if FreeRTOS could
    // not create the queues, which is fatal and reported by the caller.
    bool begin() noexcept;

    // --- network side -------------------------------------------------------

    // Take ownership of a free slot. Non-blocking: returns false when both slots
    // are in flight, and the caller must then drop the message rather than wait,
    // because this runs in the WebSocket client's callback.
    bool acquire(std::uint8_t& slot) noexcept;

    // Writable bytes of a slot the caller owns.
    char* buffer(std::uint8_t slot) noexcept { return slots_[slot]; }

    // Hand a completed message to the feed task. Ownership passes on success; on
    // failure the slot is returned to the free list and `queue_full` counted.
    // `arrival_us` travels with it so the feed task can price its own lateness.
    bool publish(std::uint8_t slot, std::uint32_t len, std::int64_t arrival_us) noexcept;

    // A whole message finished arriving, whether or not a slot could be found
    // for it. This is the only writer of the arrival histogram and it runs on
    // the WebSocket client's task — the same single writer that owns every other
    // counter in FramePipeStats.
    void note_arrival(std::int64_t at_us) noexcept;

    // Give a slot back without publishing (message abandoned or oversize).
    void release(std::uint8_t slot) noexcept;

    // Connection state, which the feed task turns into Gap{Disconnect}.
    bool post_status(FeedMessage::Kind kind) noexcept;

    // Things only the reassembler can observe, counted here so that every
    // transport-side diagnosis lives in one struct the console can print.
    void count_oversize() noexcept { ++stats_.oversize; }
    void count_abandoned() noexcept { ++stats_.abandoned; }
    void count_continuation() noexcept { ++stats_.continuation; }
    void count_control() noexcept { ++stats_.control; }
    void count_chunk() noexcept { ++stats_.chunks; }

    // --- feed side ----------------------------------------------------------

    // Block for at most `ticks` (portMAX_DELAY to wait forever). Returning false
    // means the wait expired with nothing to do — which is precisely the RX
    // watchdog firing, and is why this signature exists.
    bool receive(FeedMessage& out, TickType_t ticks) noexcept;

    // Return a parsed slot to the free list.
    void recycle(std::uint8_t slot) noexcept;

    // How many complete messages are queued for the feed task right now.
    //
    // A direct instrument for one of the three candidates: if the feed task is
    // being starved on Core 0, work piles up HERE first and the slot pool empties
    // second. A backlog that never exceeds 1 while the event histogram shows
    // seconds of silence rules Core-0 starvation out, which is worth as much as
    // ruling it in.
    std::uint32_t ready_waiting() const noexcept {
        return (ready_q_ == nullptr) ? 0u
                                     : static_cast<std::uint32_t>(uxQueueMessagesWaiting(ready_q_));
    }

    // --- diagnostics --------------------------------------------------------
    const FramePipeStats& stats() const noexcept { return stats_; }

private:
    // Both queues hold plain indices/structs, so FreeRTOS copies them; no
    // pointer to task-local storage ever crosses.
    QueueHandle_t free_q_ = nullptr;
    QueueHandle_t ready_q_ = nullptr;

    // Static storage: the whole point of the fixed capacity above.
    char slots_[kFrameSlots][kFrameCapacity]{};

    // Written only by the network task except `recycle`, which does not touch
    // it; the counters are diagnostics and a torn read of one costs a wrong log
    // line, not a wrong ladder.
    FramePipeStats stats_{};

    // The previous message's arrival instant, so note_arrival() can difference
    // it. Zero until the first message, which is why the first arrival records
    // no gap rather than a gap measured from boot.
    std::int64_t last_arrival_us_ = 0;
};

}  // namespace depthcharge::fw
