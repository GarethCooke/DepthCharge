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

// Two slots: the network task fills one while the feed task parses the other.
// That is the minimum that lets neither stall the other, and it is ample here —
// frames arrive ~80 ms apart and a parse is tens of microseconds. Each slot
// costs kFrameCapacity of internal SRAM, so this is 32 KiB.
inline constexpr std::size_t kFrameSlots = 2;

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
};

// Counters for things that can only go wrong on the transport side. All of them
// should read zero on a healthy bench run; each is a distinct diagnosis rather
// than one "errors" number, because they call for different fixes.
struct FramePipeStats {
    std::uint32_t frames_published = 0;   // complete messages handed to the feed
    std::uint32_t oversize = 0;           // message > kFrameCapacity, dropped
    std::uint32_t no_slot = 0;            // both slots busy when a message began
    std::uint32_t queue_full = 0;         // feed task not draining (should never)
    std::uint32_t abandoned = 0;          // a new message began mid-message
    std::uint32_t continuation = 0;       // WS continuation frames seen (see .cpp)
    std::uint32_t control = 0;            // ping/pong/close opcodes
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
    bool publish(std::uint8_t slot, std::uint32_t len) noexcept;

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

    // --- feed side ----------------------------------------------------------

    // Block for at most `ticks` (portMAX_DELAY to wait forever). Returning false
    // means the wait expired with nothing to do — which is precisely the RX
    // watchdog firing, and is why this signature exists.
    bool receive(FeedMessage& out, TickType_t ticks) noexcept;

    // Return a parsed slot to the free list.
    void recycle(std::uint8_t slot) noexcept;

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
};

}  // namespace depthcharge::fw
