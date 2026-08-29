// firmware/src/frame_pipe.hpp — the one place the network task and the feed task meet.
//
// There are exactly THREE cross-task hand-offs in this firmware, and they are
// deliberately the only three:
//
//   network task  --[ FramePipe ]-->  feed task (Core 0)  --[ SnapshotChannel ]-->  consumer (Core 1)
//                                          ^
//                                          |  [ SeedTask ]  (Binance only)
//                                     seed task (Core 0)
//
// **THIS SAID "EXACTLY TWO... DELIBERATELY THE ONLY TWO" UNTIL M5 STAGE D-A2**,
// and the sentence was load-bearing rather than decorative — it is why the
// third one had to be argued rather than added. The argument is in
// ARCHITECTURE §9 (2026-08-28) and the mechanism in `seed_task.hpp`; the short
// version is that Binance's seed is a blocking HTTPS GET, every existing task
// was measured to be the wrong place for it, and the non-blocking alternative
// does not work on this build's esp-tls.
//
// The third hand-off is the SAME SHAPE as this one, which is the reason it is
// tolerable: one owner at a time, the state transition IS the transfer, and
// nothing is shared while it is being written. It also carries BYTES and never
// book state — only the feed task turns a REST body into events, so invariant
// #8 is untouched.
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
// NO HEAP IN THE STEADY STATE — and since M5 stage D-A2 that is a narrower claim
// than "no heap", so it is written narrowly. The queues are still static storage
// and FreeRTOS copies small fixed-size items, so posting is a memcpy of eight
// bytes and never an allocation. **The slabs are now one heap block**, taken
// once in `begin()` before the socket exists so that they land in PSRAM rather
// than in the internal SRAM the panel needs (see `slots_` for the three problems
// that buys and for the latency objection it has to answer). Nothing is
// allocated per message, per frame or per connection, which is what invariant #7
// asks of this file.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

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
//
// ===========================================================================
// 16 KiB -> 64 KiB, M5 STAGE D-A2. THE INSTRUCTION ABOVE WAS FOLLOWED.
// ===========================================================================
//
// *"If `oversize` is ever non-zero on the bench, raise this constant"* — it was,
// so it is. The board logged `oversize=4` climbing within 90 s on Binance, and
// the corpus predicted it before the bench did.
//
// **AND THE PARAGRAPH ABOVE IS ANVIL'S REASONING, WHICH IS FALSE AT A DIFF
// VENUE.** (ARCHITECTURE §9, 2026-08-29, names the class: a RATIONALE that
// goes stale while the number it protects keeps testing green. Worse than a
// stale figure, because re-deriving the number confirms it — only re-reading
// the reason finds it.) *"A dropped frame costs one refresh"* is true only where every frame
// is an idempotent full replace. Binance sends DIFFS: a dropped message fails
// the next `U == last_u + 1`, so the adapter drops the whole book, greys the
// panel and spends a 50-weight REST seed rebuilding it. The board measured
// exactly that — `live=1` at 40 s and 60 s with `live=0` between and
// `resync_req` climbing 1→4, one cycle every ~20-30 s. The cost of a drop is
// not one refresh here; it is the ladder.
//
// THE MARGIN, STATED. Measured over every committed capture of the board's own
// stream shape (`btcusdt@depth@100ms`, the combined-stream wrapper removed so
// the figures are payloads this pipe would actually reassemble):
//
//     largest    28,639 B      p99   11,935 B      p50   607 B
//     over 16,384 B: 13 of 3,119 messages (0.417%) = one every ~23 s on BTCUSDT
//
//     64 KiB / 28,639 B = **2.29x the largest ever observed**
//
// That is deliberately near the 1.9x this constant was originally sized at for
// Anvil, so the venues are held to one standard rather than to whatever each
// happened to need. For reference at the other two: Anvil's largest message is
// 8,726 B (7.5x) and Kraken's is 1,970 B (33x), so 64 KiB is generous there and
// costs them nothing that matters — see the cost note below.
//
// WHY IT IS AFFORDABLE NOW AND WAS NOT BEFORE. 4 x 64 KiB is 262,144 B, against
// 65,536 B at the old size. In `.bss` that was unthinkable: D-A1 measured the
// Binance build reaching `Panel::begin()` with a 35,628 B budget, so +196,608 B
// would have taken the board past no-panel by a factor of five. **The slabs
// moved to PSRAM in this same stage**, and there this is 3.1% of 8 MB. The
// internal-SRAM cost of this change is zero, which is the only reason the
// measurement above could be acted on at all rather than merely recorded.
inline constexpr std::size_t kFrameCapacity = 64 * 1024;

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

// THE SLOW-FRAME EDGE IS DERIVED FROM THIS, and the compiler owns the
// derivation because this is the only place both numbers are visible —
// `FrameScale` cannot see `kFrameSlots` (that would drag FreeRTOS into a header
// the host suite compiles) and the host test cannot see it either.
//
// `FrameScale::kFirstLong` opens at 25 ms: four consecutive frames at that cost
// consume a whole ~99.2 ms arrival interval, which is the point where the pipe
// stops gaining ground and a run of slow frames starts costing slots. If either
// number moves, this fails here rather than quietly reporting `slow=` against a
// threshold that no longer means anything.
static_assert(FrameScale::kSlowUs * kFrameSlots >= 99'000u &&
                  FrameScale::kSlowUs * kFrameSlots <= 110'000u,
              "FrameScale's slow edge x kFrameSlots must be about one arrival interval; "
              "re-derive it if either moved");

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
    // rather than where they are consumed. A >1 s bucket that fills only in
    // FeedTask::Stats::event_gaps, with this one clean, means the bytes arrived
    // and something between here and the book sat on them.
    //
    // BE PRECISE ABOUT WHERE "HERE" IS — the 2026-08-09 draft of this comment
    // was not, and the 2026-08-10 bench cost a reading for it. The stamp is
    // taken in WsTransport::on_event, which runs on esp_websocket_client's own
    // task, so it is already downstream of the Wi-Fi driver, lwIP, the socket
    // read, the TLS record decrypt and the esp_event dispatch hop. A hole HERE
    // therefore means "no complete message was handed to our callback for a
    // second" — which is as consistent with that task not being scheduled on a
    // busy Core 0 as it is with a dry socket. It does NOT mean the bytes stopped
    // coming, and the pair of histograms cannot say that it does. What separates
    // the two is per-core idle across the hole: stall_probe.hpp.
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

    // Writable bytes of a slot the caller owns. The slabs are contiguous, so a
    // slot is an offset rather than a row of a 2-D array — see `slots_`.
    char* buffer(std::uint8_t slot) noexcept {
        return slots_.get() + static_cast<std::size_t>(slot) * kFrameCapacity;
    }

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

    // THE SLABS, AND SINCE M5 STAGE D-A2 THEY ARE IN PSRAM RATHER THAN `.bss`.
    //
    // 4 x 16 KiB = 65,536 B. As a member array of a namespace-scope `FramePipe`
    // this was internal SRAM claimed before the heap existed, and D-A1 measured
    // what that cost: the Binance build reached `Panel::begin()` with 117,548 B
    // free and a 35,628 B budget, which fits `panel_cost_bytes(3, true)` and
    // nothing better. One allocation in `begin()` moves all 64 KiB to PSRAM
    // (>= 4,097 B is tried there first on this build) and hands that back.
    //
    // WHY THIS IS THE ONE LEVER WORTH PULLING — three problems, one move, and
    // D-A1 measured all three:
    //
    //   1. The panel. 117,548 -> ~183,084 B free at `Panel::begin()`.
    //   2. `kFrameCapacity` itself. It is 16,384 B, sized at M3 as 1.9x Anvil's
    //      largest message (8,726 B). Binance's largest on the board's own
    //      `@depth@100ms` stream is **28,639 B** — 0.57x the slot — and 13 of
    //      3,119 messages over-run it, which on BTCUSDT is **one every ~23 s**.
    //      Each is a defined drop, so the next diff fails `U == last_u + 1` and
    //      the board greys and asks for a re-seed. In PSRAM the slot can afford
    //      to grow; in `.bss` it could not (4 x 32 KiB is +65,536 B against a
    //      35,628 B budget).
    //   3. A second concurrent TLS session. D-A2's REST seed needs one while the
    //      WebSocket holds the other, and mbedTLS is pinned internal
    //      (`CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC 1`, and `libmbedtls.a` is
    //      precompiled, so that is not a build flag) at two contiguous 16,717 B
    //      blocks per session. The internal heap this frees is where the second
    //      session's 33,434 B has to come from.
    //
    // AND THE OBJECTION, WHICH IS REAL AND WAS MEASURED RATHER THAN WAVED AWAY.
    // This is the per-message path — the RX task writes a slab as the message
    // arrives and the feed task parses out of it, ~10 times a second at this
    // venue — so PSRAM latency reaches it in a way it does not reach
    // `buf_lvl_`. That is exactly the objection that keeps `bids_`, `asks_` and
    // `frame_` internal, and D-A1 declined to move this pipe for it. The
    // measurement that settles it is `worst_frame` and the frame cadence, both
    // already printed on the `-- feed` line, taken against the D-A1 baseline of
    // **4,297 us and 10.0 frames/s** on this venue. See the session log.
    //
    // Not a breach of invariant #7: one allocation in `begin()`, before the
    // socket exists, never resized and never freed. `std::nothrow` because
    // `begin()` already returns a failure the caller handles, so a board with no
    // PSRAM reports it and runs without a pipe rather than dereferencing null.
    std::unique_ptr<char[]> slots_;

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
