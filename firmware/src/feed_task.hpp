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

#include <depthcharge/book.hpp>
#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/snapshot_channel.hpp>

// `venue_build.hpp` FIRST, because two of the includes below are guarded on
// DC_VENUE and a guard evaluated before its macro exists is a guard that is
// silently false. That cost one build: `RestFetch does not name a type`, from
// an include that was right and ordered wrong.
#include "venue_build.hpp"

#include "core_idle.hpp"
#include "frame_pipe.hpp"
#include "liveness_watchdog.hpp"
#include "reject_log.hpp"
#include "resync.hpp"
#include "stall_probe.hpp"
#if DC_VENUE == DC_VENUE_BINANCE
#include "seed_schedule.hpp"
#include "seed_task.hpp"
#endif

namespace depthcharge::fw {

// THE WATCHDOG IS NOT A CONSTANT ANY MORE, AND WHAT IT WATCHES HAS CHANGED.
//
// `kRxWatchdogMs = 1000` used to be declared here, with a `static_assert` tying
// it to `kHoleThresholdUs` and forty lines arguing that a watchdog must arm on
// an EVENT REACHING THE BOOK rather than on a frame arriving. M4 stage D
// deleted all of it. The argument was right about arrival and wrong about book
// events, and the 2026-08-17 ruling (ARCHITECTURE §9) says so outright: **no
// threshold on book silence can be correct**, because a quiet market and a dead
// subscription are identical on the wire. MINA/GBP's healthy 25,843 ms of book
// silence would have greyed this panel twenty-five times.
//
// So the rule now lives in `liveness_watchdog.hpp`, which watches the venue's
// declared liveness signal against a threshold that signal calibrates itself,
// and it is host-tested — the constant never was, because this header reaches
// FreeRTOS through `frame_pipe.hpp` and cannot compile on the desk.
//
// WHAT SURVIVED THE DELETION AND WHAT IT NOW MEANS. `kHoleThresholdUs` and the
// `GapScale` edges are untouched, and they are no longer the grey threshold —
// they are a FIXED MEASUREMENT SCALE, so that a hole bucketed today is
// comparable with one bucketed during the 23.6 h soak. `event_gaps`' >1 s
// column used to be readable as "occasions the panel had grounds to grey"; it
// is not, and `greys` on the serial line is that number now. `stall_probe.hpp`
// still opens a hole record at 1 s of book silence, which remains the right
// question for the instrument it belongs to — whether a stall is board-bound or
// link-bound is not a question about staleness.

class FeedTask {
public:
    struct Stats {
        std::uint32_t frames_in = 0;
        std::uint32_t watchdog_gaps = 0;    // the liveness signal went quiet past its threshold
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

        // WHAT THE REJECTED FRAMES WERE (the 2026-08-10 connect burst).
        //
        // `AnvilAdapter::Stats::parse_errors` says how many frames the parser
        // threw away; this says what they were. It is not in the adapter because
        // the adapter is engine code shared with the host replay, where the
        // question does not arise — a trace file's frames all parsed once
        // already, by definition of having been captured.
        //
        // Written on the reject path only, and only for the first
        // kRejectsPerConnect of each connect; a healthy run never touches it.
        RejectLog rejects{};

        // HOW MANY TIMES THE PANEL WENT GREY, and for how long in total.
        //
        // NEW AT M4 STAGE D, AND IT REPLACES AN INFERENCE. The bench used to
        // read the grey count off `event_gaps`' >1 s column, which worked only
        // while the watchdog threshold and that bucket edge were the same
        // number. They are not: the threshold is now calibrated per venue
        // (~2,000 ms at Anvil, ~4,000 at Kraken) and the edge is a fixed scale.
        // A derived number that quietly stops being derivable is worse than no
        // number, so it is counted directly.
        //
        // Counted on the FEED side, at the transition into stale, because that
        // is where the decision is taken. The render task prints its own
        // `*** STALE ***` / `*** LIVE ***` transition lines off the published
        // snapshot and the two must agree; a disagreement means a published
        // frame was superseded before the render task saw it, which is legal
        // (invariant #4) and worth knowing about.
        // Lifted into `GreyLedger` (liveness_watchdog.hpp) after review: new
        // counting logic living in a `.cpp` no host build compiles is new
        // counting logic with no coverage, and the transition rule has three
        // cases worth pinning — grey at boot, ONE episode however many gaps
        // produced it, and an open episode folded in by the reader so a line
        // taken mid-outage reports the outage rather than zero.
        GreyLedger grey{};

        std::uint32_t worst_queue_wait_us = 0;
        // The most messages ever left queued BEHIND the one being processed —
        // sampled after the dequeue, so it is one below the peak depth and can
        // never reach kReadyQueueDepth. Zero throughout means the feed task was
        // always ahead of the wire.
        std::uint32_t max_ready_backlog = 0;
    };

    // `idle` and `link` are read-only instruments this task samples as it goes;
    // neither can affect what it does, and both are const so that stays true by
    // construction rather than by review. `subscription` is the one thing this
    // task WRITES that leaves it — a statement about the book, not book state;
    // see resync.hpp.
    //
    // THE SYMBOL IS NO LONGER A PARAMETER (M4 stage D, A3). It used to be passed
    // in from `main.cpp`, which meant the ladder's scale and the adapter's scale
    // were two spellings that could disagree — and a wrong scale does not fail,
    // it draws a wrong ladder. Both now come from `venue_build.hpp`, which is
    // also where the adapter's type comes from, so there is one place to be
    // wrong and it is checked by `test_venue_build.cpp` on both arms.
    FeedTask(FramePipe& pipe, SnapshotChannel& channel, SubscriptionSignal& subscription,
             const CoreIdleProbe& idle, const LinkQuality& link) noexcept
        : pipe_(pipe), channel_(channel), subscription_(subscription), idle_(idle), link_(link),
          book_(venue::kSymbol, window::kWindowPolicy, venue::kValidatedDepth) {}

    // Creates the task pinned to Core 0. Returns false if FreeRTOS refused.
    // NOTE: ESP-IDF's xTaskCreate takes the stack size in BYTES, not words —
    // "Note that this differs from vanilla FreeRTOS" (task.h). 8 KiB covers the
    // parser (measured worst case ~600 B of stack at -Os), the book copy, and
    // vsnprintf inside ESP_LOGx, which is the greedy one.
    bool start(std::uint32_t stack_bytes = 8192, UBaseType_t priority = 5) noexcept;

    const Stats& stats() const noexcept { return stats_; }
    const venue::Adapter::Stats& adapter_stats() const noexcept { return adapter_.stats(); }

    // The two clocks and the grey decision taken on them (liveness_watchdog.hpp).
    // Const: the render task reads it to print the age and the threshold, and an
    // instrument that the console could steer is not an instrument.
    const LivenessWatchdog& liveness() const noexcept { return watchdog_; }

    // WHAT THE VENUE'S OWN CHECKSUM CONFIRMS, per side, as of the last publish.
    //
    // A4 asks the board for one number the bench cannot compute at a glance:
    // rows within the checksum's reach. It is `window::WindowStats::rows_validated`
    // — the count of RENDERED rows whose rank in the book is inside the venue's
    // validated depth — and it exists only because `Book` was constructed with
    // `venue::kValidatedDepth` above. Left at the default it would read 0, which
    // is indistinguishable from Anvil's honest 0, which is why the argument is
    // passed at the one construction site rather than at a call.
    const window::WindowStats& bid_window() const noexcept { return book_.bid_window(); }
    const window::WindowStats& ask_window() const noexcept { return book_.ask_window(); }

    // THE JOIN KEY, and the reason the age line carries it.
    //
    // Anvil's wire `seq` is a single GLOBAL engine counter, useless for ordering
    // (§4) — but that is exactly what makes it a join key: the same broadcast
    // carries the same value on every socket. Printing the board's latest one
    // beside its own clock lets a serial log and a simultaneous desk capture be
    // joined offline into a continuous lag curve, which is the measurement
    // `tools/anvil_freshness_probe.py` already performs across two sockets.
    // It replaces a human with a stopwatch, and unlike the stopwatch it measures
    // the age itself rather than the age divided by the drain fraction.
    //
    // KRAKEN HAS NO WIRE SEQ AT ALL (§4: "Kraken has no seq — its adapter
    // synthesises one"), so there is nothing to join on and this reports -1
    // rather than the synthesised counter. Printing the synthesised one would
    // look exactly like a join key and correlate with nothing outside this
    // board, which is the worst thing an instrument can do.
    std::int64_t last_wire_seq() const noexcept {
        // GUARDED ON `DC_VENUE_HAS_WIRE_SEQ` SINCE M5 STAGE D-A1, AND IT USED TO
        // BE `DC_VENUE_HAS_SUBSCRIPTION`. That was one macro answering two
        // questions, correct only while there were two venues; Binance has no
        // subscription and no wire seq, so it took the Anvil arm and failed to
        // compile on a member only Anvil's adapter has. See venue_build.hpp for
        // the full note — the failure was loud, and the same conflation in the
        // transport direction would not have been.
#if !DC_VENUE_HAS_WIRE_SEQ
        // -1 and not the synthesised counter: printing that would look exactly
        // like a join key and correlate with nothing outside this board.
        // Instruments that consume a COUNTER rather than displaying one are
        // given 0 instead, via `wire_seq_for_probe()` — review found -1 flowing
        // into `StallProbe`, where a step of ~0 against a nonzero expectation is
        // the documented signature of a stalled broadcaster.
        return -1;
#else
        return adapter_.last_wire_seq();
#endif
    }

    // The same fact, shaped for an instrument that differences it. 0 at a venue
    // with no wire seq, so the derived rate is an honest zero rather than a
    // negative step that reads as a fault.
    std::int64_t wire_seq_for_probe() const noexcept {
        return venue::kHasWireSeq ? last_wire_seq() : 0;
    }
    const Book::Stats& book_stats() const noexcept { return book_.stats(); }

private:
    static void trampoline(void* self) noexcept;
    void run() noexcept;

    // How long the loop may sleep: the SOONEST of everything that wants to
    // happen. Generalised at M5 stage D-A2 from "the wait IS the watchdog",
    // which was true while the watchdog was the only deadline. It no longer is,
    // and at Binance the watchdog is never armed at all.
    TickType_t nearest_deadline(std::int64_t now_us) const noexcept;

#if DC_VENUE == DC_VENUE_BINANCE
    // Step the fetch, then the schedule. Called once per loop pass, after the
    // slot has been recycled and outside the parse stopwatch.
    void service_seed(std::int64_t now_us) noexcept;
#endif

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

    // Raise Gap{Disconnect} at most once per outage. Re-raising every threshold
    // would republish an unchanged grey frame and bury the transition in the
    // log; the book is already Stale and only a Snapshot clears it.
    void raise_gap_once() noexcept;

    // Republish the current book without applying an event, so the age on the
    // panel and the beat pixel stay current through a quiet market. See the
    // definition for why this is not cosmetic.
    void republish_if_due(std::int64_t now_us) noexcept;

    FramePipe& pipe_;
    SnapshotChannel& channel_;
    SubscriptionSignal& subscription_;
    const CoreIdleProbe& idle_;
    const LinkQuality& link_;

    // The selected venue's adapter, constructed by `venue::make_adapter()` and
    // initialised directly from its prvalue — C++17's guaranteed elision, so no
    // copy and no move of an object that owns ~9 KiB of decoded-frame buffer and
    // has already produced one use-after-move defect (B1's review).
    venue::Adapter adapter_ = venue::make_adapter();
    Book book_;
    DisplaySnapshot staging_{};

    // The grey decision and the two clocks it is taken on. A member of the task
    // rather than of `Stats`, because it is state this task acts on rather than
    // a counter it reports — `Stats` is read across a core boundary and nothing
    // over there may steer what the panel does.
    LivenessWatchdog watchdog_;

#if DC_VENUE == DC_VENUE_BINANCE
    // THE SEED, AND IT IS THE FEED TASK'S BECAUSE OF INVARIANT #8. Only this
    // task may mutate the adapter, so only this task may call `on_rest_body`.
    // The fetch is therefore stepped from here in bounded slices rather than
    // run to completion — see `rest_fetch.hpp` for why a blocking fetch would
    // have destroyed the very thing it was fetching.
    // THE FETCH RUNS ON ITS OWN TASK, and this holds only the handle to it plus
    // the policy that decides when to ask. The buffer is transferred by
    // `SeedTask`'s slot state; the adapter is still touched only here
    // (invariant #8).
    SeedTask seed_;
    SeedSchedule schedule_;
    std::uint32_t seed_give_ups_ = 0;
    // What the feed task knows about the socket, learned from the pipe's own
    // Connected/Disconnected messages rather than from a new shared word. It
    // gates the seed: a fetch and a TLS reconnect must never overlap.
    bool socket_up_ = false;
    // The overflow count when the current fetch was issued, so `service_seed`
    // can tell an overflow UNDER this fetch from one that predates it.
    std::uint64_t overflows_at_issue_ = 0;
#endif

    // `gap_raised_` keeps one outage to one Gap. The arming state that used to
    // sit beside it moved into `LivenessWatchdog` with the rule that reads it.
    //
    // `last_event_us_` is now purely an INSTRUMENT: it dates the last event that
    // reached the book, which feeds `worst_gap_us`, `event_gaps` and the stall
    // probe's window. Nothing branches on it any more — book silence is a number
    // (`age_ms`), never a fault. It is deliberately NOT cleared anywhere, so the
    // first event after an outage measures the whole hole.
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
    // When the book was last published, so a quiet market still refreshes the
    // age and the heartbeat pixel. See `republish_if_due`.
    std::int64_t last_publish_us_ = 0;

    std::uint32_t prev_idle0_us_ = 0;
    std::uint32_t prev_idle1_us_ = 0;
    std::int64_t last_msg_arrival_us_ = 0;
    bool socket_dropped_pending_ = false;

    Stats stats_{};
};

}  // namespace depthcharge::fw
