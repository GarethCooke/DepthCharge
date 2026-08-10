// firmware/src/feed_task.hpp — the Core 0 feed pipeline.
//
//   verbatim wire bytes -> parse_anvil_frame -> AnvilAdapter -> Book -> SnapshotChannel
//
// This is the same chain the host harness runs in dc::harness::run_replay, with
// a socket where the trace file was. That is the claim M3 exists to prove, and
// the reason it holds is that everything in the chain above is `engine/` code
// linked as-is (invariant #1) — this file adds a task, a clock and a watchdog,
// and nothing else.
//
// INVARIANT #8, ONE WRITER. Everything that mutates the book lives inside this
// object and runs on this one task: frames, and the Gap the watchdog raises.
// Nothing else in the firmware may call apply() or publish(). That is enforced
// by construction — the adapter and the book are private members of a class only
// this task's entry point touches — rather than by convention.
//
// INVARIANT #4. The publish end is SnapshotChannel, which is wait-free on the
// writer; a stalled consumer cannot stall this task. Stage A measured that.
//
// THIS TASK DOES NOT LOG, AND THAT IS DELIBERATE — do not add an ESP_LOGx here
// at stage D. Arduino routes ESP_LOGx to log_printfv, which mallocs for any line
// over 64 characters and takes the UART bus mutex with portMAX_DELAY around a
// busy-wait on the TX FIFO. One ESP_LOGI on this task would therefore make the
// feed block on a mutex the console task holds on the other core — which is
// exactly the coupling invariant #4 forbids, and it would do it through a heap
// allocation invariant #7 forbids as well. Nothing is lost by staying silent:
// every event worth reporting is already a counter in Stats, and the
// Live<->Stale transitions that are the acceptance evidence are printed by the
// console off the published DisplaySnapshot, which is where they belong.
//
// INVARIANT #7. Nothing here allocates after construction. The adapter carries
// its own 8 KiB decoded-frame buffer, the book its 8 KiB of levels, and the
// staging DisplaySnapshot is a member — all placed once, at init.
#pragma once

#include <cstdint>

#include <depthcharge/anvil/anvil_adapter.hpp>
#include <depthcharge/book.hpp>
#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/snapshot_channel.hpp>

#include "core_idle.hpp"
#include "frame_pipe.hpp"
#include "stall_probe.hpp"

namespace depthcharge::fw {

// The RX watchdog, in milliseconds.
//
// M1's measured number, not a tuned one: across both 5-minute local captures
// (6,494 frames) the worst healthy inter-frame silence is 640 ms and the median
// is ~69 ms, while the observed disconnect left a 4,468 ms hole. 1000 ms sits
// 1.6x above the loudest healthy quiet and 4.5x below the real outage. The host
// replay uses the identical constant (ReplayOptions::disconnect_gap_ms), which
// is what makes the M1 goldens a preview of this firmware rather than an
// analogy.
inline constexpr std::uint32_t kRxWatchdogMs = 1000;

// The stall probe's definition of a hole is the histogram's >1 s bucket edge,
// and the watchdog's threshold is this constant. They have always been the same
// number and every reading of the bench log assumes it — `holes n=`, `event
// >1s=` and `wd_gaps` are compared against each other line by line. Assert it
// rather than leave three files agreeing by habit.
static_assert(kHoleThresholdUs == kRxWatchdogMs * 1000,
              "the stall probe must count exactly the silences the watchdog greys on");

// WHAT THE WATCHDOG WATCHES — the one place this firmware is deliberately
// STRICTER than the host replay driver, and the reason is invariant #5.
//
// The host raises Gap{Disconnect} on a hole in `rx_ns` between any two frames,
// because in a captured trace every frame parses and "a frame arrived" and
// "the book advanced" are the same statement. On a real socket they are not.
// Bytes can keep arriving that decode to nothing — a server that starts sending
// a shape the parser rejects, or nothing but `summary` frames — and a watchdog
// armed on *frame arrival* would sit there happily while the ladder froze,
// still reading Live. That is precisely the one output invariant #5 forbids:
// a frozen ladder that looks live.
//
// So this watchdog is armed by an EVENT reaching the book, not by a frame
// reaching the parser. It costs nothing to do so, which is the part that had to
// be measured rather than argued — over both committed captures the worst
// healthy gap is identical whichever way you count it:
//
//     any frame                        640.2 ms
//     event-producing (book/snap/trade) 640.2 ms
//     book-affecting (book/snapshot)    640.2 ms
//
// They agree because the book stream is the dense one (~12 Hz) and the 2 Hz
// summaries never fill a hole the book frames left. The 1.6x margin under
// 1000 ms therefore survives intact, and the firmware gets a rule that also
// covers the two failure modes a trace file cannot contain.
//
// The divergence from the host driver is recorded in the stage C session log
// as a candidate for aligning the host at M4; it is not done here because that
// driver is golden-covered and stage C's scope is firmware.

class FeedTask {
public:
    struct Stats {
        std::uint32_t frames_in = 0;
        std::uint32_t watchdog_gaps = 0;    // silence exceeded kRxWatchdogMs
        std::uint32_t socket_gaps = 0;      // transport reported the socket down
        std::uint32_t connects = 0;
        std::uint64_t worst_gap_us = 0;     // largest observed inter-frame silence
        std::uint32_t worst_parse_us = 0;   // slowest frame -> publish, whole chain

        // THE EVENT HALF OF THE ARRIVAL-VS-EVENT SPLIT (strain 12).
        //
        // `event_gaps` is the distribution behind `worst_gap_us`: the same
        // quantity the RX watchdog measures — silence between events reaching
        // the book — bucketed, so the bench can finally say how OFTEN a hole
        // long enough to grey the panel happens, and how big the typical one is.
        // Its >1 s buckets against FramePipeStats::arrival_gaps' >1 s buckets
        // is the reading the whole instrument exists for.
        //
        // Its >1 s count is `watchdog_gaps + socket_gaps` and not `watchdog_gaps`
        // alone: a hole is recorded when the next event lands, whatever ended
        // it, so an outage the socket reported (which raises no watchdog gap)
        // still leaves its silence here. On the steady-state run this instrument
        // was built for — `sock_gaps=0`, `connects=1` — the two agree, minus any
        // hole still open when the block is printed.
        //
        // `arrival_to_event` closes the loop for a single message: the interval
        // from the bytes being complete on the socket to the event they carry
        // reaching the book. Arrival smooth + this large = the stall is ours.
        //
        // The two scalars beside it split that latency where the causes differ.
        // `worst_queue_wait_us` is time the message sat in the pipe before this
        // task looked at it, which is scheduling — Core 0 starvation, or a
        // higher-priority Wi-Fi/lwIP task holding the CPU. `worst_parse_us`
        // above is the work itself. A second of the first and microseconds of
        // the second name a different candidate than the reverse.
        GapHistogram event_gaps{};
        LatencyHistogram arrival_to_event{};

        // THE VERDICT HALF (strain 12, the stage C→D interlude).
        //
        // The histograms above say a hole happened and how long it was. They
        // cannot say which half of the board it came from, and neither can the
        // arrival-vs-event split — the arrival stamp is taken inside the
        // WebSocket client's task, so it is already downstream of the Wi-Fi
        // driver, lwIP and the TLS decrypt, and a hole in it is as consistent
        // with a busy Core 0 as with a dry socket (stall_probe.hpp).
        //
        // This is the signal from outside the data path: per-core idle across
        // exactly the window that greyed the panel, the shape of the recovery
        // that ended it, and the rssi at the time. Board-bound is firmware's to
        // fix; link-bound is not, and guessing costs a milestone round.
        StallProbe stall{};
        std::uint32_t worst_queue_wait_us = 0;
        // The most messages ever left queued BEHIND the one being processed —
        // sampled after the dequeue, so it is one below the peak depth and can
        // never reach kReadyQueueDepth. Zero throughout means the feed task was
        // always ahead of the wire.
        std::uint32_t max_ready_backlog = 0;
    };

    // `idle` and `link` are read-only instruments this task samples as it goes;
    // neither can affect what it does, and both are const so that stays true by
    // construction rather than by review.
    FeedTask(FramePipe& pipe, SnapshotChannel& channel, const SymbolSpec& symbol,
             const CoreIdleProbe& idle, const LinkQuality& link) noexcept
        : pipe_(pipe), channel_(channel), idle_(idle), link_(link), adapter_(symbol),
          book_(symbol) {}

    // Creates the task pinned to Core 0. Returns false if FreeRTOS refused.
    // NOTE: ESP-IDF's xTaskCreate takes the stack size in BYTES, not words —
    // "Note that this differs from vanilla FreeRTOS" (task.h). 8 KiB covers the
    // parser (measured worst case ~600 B of stack at -Os), the book copy, and
    // vsnprintf inside ESP_LOGx, which is the greedy one.
    bool start(std::uint32_t stack_bytes = 8192, UBaseType_t priority = 5) noexcept;

    const Stats& stats() const noexcept { return stats_; }
    const anvil::AnvilAdapter::Stats& adapter_stats() const noexcept { return adapter_.stats(); }
    const Book::Stats& book_stats() const noexcept { return book_.stats(); }

private:
    static void trampoline(void* self) noexcept;
    void run() noexcept;

    void on_frame(const FeedMessage& msg) noexcept;
    void on_watchdog() noexcept;
    void on_disconnected() noexcept;

    // The single door every event leaves by. Book, then publish, then channel —
    // in that order, once per event, exactly as the host replay driver does it.
    void apply_and_publish(const FeedEvent& ev) noexcept;

    // Push the book's current state to the channel without applying an event.
    // Used once at start-up so the consumer has an honest Stale{Resync} frame to
    // draw before any data arrives — otherwise consume() returns false forever
    // and a booting device is indistinguishable from a hung one.
    void publish_current() noexcept;

    // Raise Gap{Disconnect} at most once per outage. Re-raising every second
    // would republish an unchanged grey frame and bury the transition in the
    // log; the book is already Stale and only a Snapshot clears it.
    void raise_gap_once() noexcept;

    FramePipe& pipe_;
    SnapshotChannel& channel_;
    const CoreIdleProbe& idle_;
    const LinkQuality& link_;

    anvil::AnvilAdapter adapter_;
    Book book_;
    DisplaySnapshot staging_{};

    // Watchdog state. `watching_` means "the book has advanced, so silence from
    // here is meaningful"; it is false before the first event — a device that
    // has never connected is already Stale{Resync} and does not need a Gap to
    // say so. `last_event_us_` is deliberately NOT cleared when the watchdog
    // fires, so the next event can measure the whole outage rather than the
    // sliver of it after the alarm.
    bool watching_ = false;
    bool gap_raised_ = false;
    std::int64_t last_event_us_ = 0;

    // Stall-probe state, all of it sampled and none of it consulted.
    //
    // `prev_idle*_us_` are the free-running per-core idle counters as they stood
    // at the PREVIOUS event, so differencing them at this one measures idle over
    // exactly the window `event_gaps` just bucketed — the two numbers describe
    // the same interval or the verdict means nothing.
    //
    // `last_msg_arrival_us_` is the previous message's arrival stamp, giving the
    // inter-arrival series that names a recovery a burst or a resumed cadence.
    // Measured from the arrival stamps rather than from when this task got to
    // them, because this task's own lateness is precisely what would smear it.
    //
    // `socket_dropped_pending_` marks a hole that spanned a transport gap, so a
    // reconnect's blocking `stop()` is never read as the steady-state stall.
    std::uint32_t prev_idle0_us_ = 0;
    std::uint32_t prev_idle1_us_ = 0;
    std::int64_t last_msg_arrival_us_ = 0;
    bool socket_dropped_pending_ = false;

    Stats stats_{};
};

}  // namespace depthcharge::fw
