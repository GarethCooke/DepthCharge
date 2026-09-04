// firmware/src/feed_task.cpp — see feed_task.hpp.
#include "feed_task.hpp"

#if DC_VENUE == DC_VENUE_BINANCE
#include "lwip/inet.h"
#endif

#include <string_view>

#include "esp_timer.h"

namespace depthcharge::fw {
namespace {

// How often a quiet book is republished so the age and the beat pixel stay
// current. One second: fast enough that a desk cannot see the panel stop, slow
// enough that it is invisible beside Anvil's ~13 publishes a second, and it
// matches the console's own detail-line cadence so the two tell one story.
constexpr std::int64_t kRepublishPeriodUs = 1'000'000;

}  // namespace

bool FeedTask::start(std::uint32_t stack_bytes, UBaseType_t priority) noexcept {
#if DC_VENUE == DC_VENUE_BINANCE
    // The 96 KiB seed buffer, taken BEFORE the task exists so that a board with
    // no room for it says so at boot rather than at the first re-seed — and so
    // that the allocation is nowhere near the steady state invariant #7 names.
    //
    // NOT FATAL. A board that cannot hold a seed body still runs the feed, still
    // draws an honestly grey ladder and still prints the whole serial evidence;
    // `RestFetch::start` declines with `NoBuffer` and the schedule counts the
    // failures. Same rule the panel follows.
    if (seed_.begin()) { (void)seed_.start(); }
#endif
    // Pinned to Core 0 (ARCHITECTURE §2). Core 1 is the render side's, and at
    // stage D the HUB75 DMA driver will want it to itself.
    const BaseType_t ok = xTaskCreatePinnedToCore(&FeedTask::trampoline, "dc_feed",
                                                  stack_bytes, this, priority, nullptr, 0);
    return ok == pdPASS;
}

void FeedTask::trampoline(void* self) noexcept {
    static_cast<FeedTask*>(self)->run();
}

// THE VENUE'S CLOCK, DIFFERENCED — and it is called from BOTH loop paths, which
// is the whole of M5 stage D-A3's deliverable 1 (the rest is plumbing).
//
// Which counter carries the clock is the only venue knowledge in this file, and
// it is one line in `venue_build.hpp`: Anvil's 2 Hz `summary`, Kraken's 1 Hz
// `heartbeat`, Binance's ~20 s server PING. The first two are application frames
// and reach the adapter as parsed messages; the third is a control frame counted
// on the RX task and carried across by `FramePipe`, which is already this
// firmware's RX -> feed boundary.
//
// **CALLED ON EVERY ITERATION, NOT ONLY AFTER A MESSAGE, AND THAT IS THE BUG
// THIS FUNCTION EXISTS TO NOT HAVE.** The old code differenced a local captured
// around `adapter_.on_frame`, so the check ran only when a message had been
// dequeued. At the other two venues that is harmless — their clock IS a message.
// At Binance it would have been useless in exactly the case the watchdog is for:
// the ping arrives while no depth frame does, so a stream that goes quiet would
// never have been checked and the watchdog would never have armed. The loop's
// own comment predicted it before the wire existed.
//
// Differencing a MEMBER rather than a local is what makes calling it twice per
// iteration harmless: the second call sees no change and does nothing.
void FeedTask::service_liveness(std::int64_t now_us) noexcept {
    const std::uint64_t n = venue::liveness_count(adapter_, pipe_.stats().server_pings);
    if (n == liveness_seen_) { return; }
    liveness_seen_ = n;

    // The mute is the bench's only way to stage a stopped heartbeat over a live
    // socket; it is compiled out of every shipping build. See
    // liveness_watchdog.hpp. Nothing is counted on the muted branch:
    // `DC_SOAK_TEST_TAG` names the mute uptime on every SOAK line and `up=` says
    // whether it has passed, so a counter would be a second way to say it and
    // one of the two would eventually be the one nobody printed.
    //
    // `liveness_seen_` advances either way, so a muted build does not re-stamp
    // the same arrival on the next pass.
    if (!test_liveness_muted(now_us)) {
        watchdog_.on_liveness(ns_from_us(now_us));
    }
}

void FeedTask::run() noexcept {
    // One frame before anything arrives, so the consumer has the honest
    // Stale{Resync} the book already holds. Without it consume() never returns
    // true until the first snapshot lands, and at stage D that is a dark panel —
    // which says nothing, where invariant #5 requires it to say "not trusted".
    publish_current();

    for (;;) {
        // The wait IS the watchdog. While the venue's liveness signal is
        // arriving we sleep only until the deadline the last one set; with
        // nothing to watch for we sleep indefinitely and let the queue wake us.
        //
        // The deadline comes from `LivenessWatchdog`, which is where the rule
        // now lives; the only thing left here is the unit conversion and the
        // rounding up to a whole tick. `threshold_ms()` is O(1) — the value is
        // cached on the liveness arrival that could change it — so recomputing
        // this on every wake costs an add and a compare rather than a soft-float
        // sort of thirty-two doubles.
        const std::int64_t loop_now = esp_timer_get_time();
        const TickType_t wait = nearest_deadline(loop_now);

        FeedMessage msg;
        if (!pipe_.receive(msg, wait)) {
            // A TIMEOUT IS NO LONGER NECESSARILY THE WATCHDOG. It used to be —
            // "the wait IS the watchdog" — and that was true while the watchdog
            // owned the only deadline. Since M5 stage D-A2 the seed schedule
            // owns one too, and `on_watchdog()` must not be called for a wake-up
            // that was the fetch's. It checks `armed()` itself, so an unarmed
            // watchdog makes this a no-op, which is exactly the Binance case:
            // that build's liveness signal is the server ping, which never
            // reaches this task, so the watchdog is never armed at all.
            // BEFORE `on_watchdog()`, because a ping that arrived during this
            // wait must refresh the deadline rather than be judged by it. At
            // Binance nothing else on this path can arm the watchdog at all.
            service_liveness(esp_timer_get_time());
            on_watchdog();
#if DC_VENUE == DC_VENUE_BINANCE
            service_seed(esp_timer_get_time());
#endif
            continue;
        }

        switch (msg.kind) {
            case FeedMessage::Kind::Frame:        on_frame(msg); break;
            case FeedMessage::Kind::Connected: {
                ++stats_.connects;
#if DC_VENUE == DC_VENUE_BINANCE
                // The seed may be asked for again. A new socket is also new
                // information for the schedule: it clears a give-up, because
                // the commonest cause of a run of failures is a link that has
                // since come back.
                socket_up_ = true;
                schedule_.note_socket_change();
#endif
                const std::int64_t at = esp_timer_get_time();
                // Each connect gets its own capture budget: the reject burst
                // recurs per connect and is smaller on a reconnect, so a budget
                // spent once at boot would hide the half of the phenomenon that
                // is hardest to reproduce on purpose.
                stats_.rejects.note_connect(at);
                // And the age estimate starts over, because the backlog dies
                // with the socket: both venues serve a fresh snapshot to a new
                // subscription, so whatever was queued behind the old one is
                // gone. Carrying it across would have made the 2026-08-09
                // 86-minute run — 21 reconnects — read as one long healthy
                // stream. The calibrated THRESHOLD deliberately survives; see
                // liveness_watchdog.hpp for why the two differ.
                watchdog_.on_socket_change(ns_from_us(at));
                // A new socket subscribes on its own; the level is re-published
                // from the book on the next frame either way.
                subscription_.set_wanted(false);
                // Deliberately does NOT clear stale. Only a fresh Snapshot can
                // (invariant #5), and on reconnect Anvil sends exactly one as
                // its first frame (protocol §4) — so the panel goes live on
                // data, never on the mere fact of a socket.
                break;
            }
            case FeedMessage::Kind::Disconnected:
#if DC_VENUE == DC_VENUE_BINANCE
                // THE OTHER HALF OF THE NON-OVERLAP RULE, and it is set BEFORE
                // `on_disconnected()` runs so that the very next
                // `service_seed()` sees the socket down and abandons. The
                // reconnect that follows must not have to contend with a fetch
                // holding two 16,717 B internal blocks — the board measured a
                // fetch taking the largest free block to 10,740 B, already
                // below what one session needs.
                socket_up_ = false;
#endif
                on_disconnected();
                break;
        }
    }
}

// HOW LONG THE LOOP MAY SLEEP: THE SOONEST OF EVERYTHING THAT WANTS TO HAPPEN.
//
// This used to be "the wait IS the watchdog", and the comment said so. That was
// correct while the watchdog was the only thing with a deadline. It is not any
// more, and **at Binance the watchdog is never armed** — `venue_build.hpp`'s
// `liveness_count()` returns a constant there because that venue's clock is the
// server's WebSocket ping, a control frame that never becomes a message. So on
// that build the old code left `wait = portMAX_DELAY` on every pass, and a seed
// fetch stepped only on frame arrival would have stalled the instant the stream
// went quiet — with nothing left to wake it.
//
// Generalised rather than special-cased: every deadline is folded in here, and
// adding a third means adding a line rather than finding this one.
TickType_t FeedTask::nearest_deadline(std::int64_t now_us) const noexcept {
    std::int64_t soonest_us = -1;   // -1 = nothing wants anything

    if (watchdog_.armed()) {
        const std::int64_t remaining_us =
            (watchdog_.deadline_ns() - ns_from_us(now_us)) / 1000;
        soonest_us = remaining_us > 0 ? remaining_us : 0;
    }

#if DC_VENUE == DC_VENUE_BINANCE
    SeedInput in;
    in.wanted = !adapter_.has_baseline() || adapter_.reseed_wanted();
    in.in_flight = seed_.busy();
    in.socket_up = socket_up_;
    in.now_us = now_us;
    const std::int64_t seed_us = schedule_.due_in_us(in);

    // WHILE THE SEED TASK HAS SOMETHING FOR US, COME BACK SOON — but not fast.
    // The fetch no longer runs here, so this is not a step interval: it is only
    // how long a finished body may sit unconsumed. 50 ms against measured fetch
    // round trips of 4.1-6.3 s is noise, and it matters at all only because at
    // Binance the liveness watchdog is never armed, so a quiet stream would
    // otherwise let this task sleep on `portMAX_DELAY` through its own result.
    const std::int64_t poll_us =
        (seed_.busy() || seed_.ready() || seed_.failed()) ? kSeedResultPollUs : -1;
    for (const std::int64_t candidate : {seed_us, poll_us}) {
        if (candidate < 0) { continue; }
        if (soonest_us < 0 || candidate < soonest_us) { soonest_us = candidate; }
    }
#endif

    if (soonest_us < 0) { return portMAX_DELAY; }
    if (soonest_us == 0) { return 0; }
    // Round UP to a whole tick: rounding down busy-waits, and this task at
    // priority above idle would spin a core doing it.
    return pdMS_TO_TICKS(static_cast<std::uint32_t>((soonest_us + 999) / 1000));
}

#if DC_VENUE == DC_VENUE_BINANCE
// CONSUME WHAT THE SEED TASK PRODUCED, THEN DECIDE WHETHER TO ASK AGAIN.
//
// Called once per loop pass. It never blocks and never touches the body buffer
// except while `SeedTask` says `Ready`, which is the ownership transfer.
void FeedTask::service_seed(std::int64_t now_us) noexcept {
    // The body first, so a completed fetch is consumed in the same pass it
    // lands and the schedule below sees an accurate `in_flight`.
    if (seed_.ready()) {
        // THE SAME SINK `on_frame` USES. `on_rest_body` may emit a Snapshot, a
        // Gap, or nothing at all — the bracket decides — and every one of those
        // must reach the book through the one path that applies. This is also
        // the only place a REST body ever becomes book state: invariant #8 says
        // one writer, and the seed task deliberately produces bytes only.
        adapter_.on_rest_body(seed_.body(), [this](const FeedEvent& ev) { apply_only(ev); });
        // A REST seed is one message: the whole body is one statement the venue
        // made, whether it becomes a Snapshot, a Gap or nothing.
        publish_message();
        schedule_.note_result(true, seed_.report().http_status, now_us);
        // Released immediately: `BinanceFrame` copies what it needs and retains
        // no `string_view` into the JSON, so the 96 KiB buffer is free the
        // moment this returns.
        seed_.release();
    } else if (seed_.failed()) {
        schedule_.note_result(false, seed_.report().http_status, now_us);
        seed_.release();
    }

    SeedInput in;
    // LEVEL, NOT EDGE, and read fresh every pass. `reseed_wanted()` is
    // re-raised from inside `drop_book`, so consuming it on an edge would
    // swallow the request the drop just made — the M4 stage A2 defect, and a
    // permanently grey panel over a healthy socket.
    in.wanted = !adapter_.has_baseline() || adapter_.reseed_wanted();
    in.in_flight = seed_.busy();
    // THE NON-OVERLAP RULE'S INPUT. Tracked from the pipe's own
    // Connected/Disconnected messages, which are ordered with the frames, so
    // this cannot see the socket up after the drop that killed it.
    in.socket_up = socket_up_;
    // The buffer overflowing under a fetch means the seed is already spent:
    // `test_binance_adapter.cpp` measures that the body would be adopted, fail
    // its bracket against the zeroed buffer and be dropped inside one call.
    in.buffer_overflowed =
        seed_.busy() && adapter_.stats().buffer_overflows > overflows_at_issue_;
    in.now_us = now_us;

    switch (schedule_.step(in)) {
        case SeedAction::Issue: {
            // CLEARED AT ISSUE, NEVER AT COMPLETION. `on_rest_body`'s failure
            // path calls `drop_book`, which re-raises the latch; clearing after
            // would erase the request it had just made.
            adapter_.clear_reseed_wanted();
            overflows_at_issue_ = adapter_.stats().buffer_overflows;
            schedule_.note_issued(now_us);
            if (!seed_.request(subscription_.seed_addr(), kBinanceSeedPath)) {
                // No address yet, or the task is not idle. Counted as a failure
                // so the schedule backs off rather than spinning on it.
                schedule_.note_result(false, 0, now_us);
            }
            break;
        }
        case SeedAction::Abandon:
            // Condemned. The seed task honours this between esp-tls calls, so
            // the buffer comes back within one `kFetchCallTimeoutMs` and the
            // result is discarded whenever it lands.
            seed_.abandon();
            break;
        case SeedAction::GiveUp:
            // Counted, not logged: this task does not log (see the header).
            ++seed_give_ups_;
            break;
        case SeedAction::None:
            break;
    }
}
#endif

void FeedTask::on_frame(const FeedMessage& msg) noexcept {
    const std::int64_t now = esp_timer_get_time();
    ++stats_.frames_in;

    // Sampled at ENTRY, before any work, because the question this answers is
    // "was the network stack busy on another task's behalf while this frame was
    // being handled" — and a fetch that finishes midway still spent most of
    // this frame's wall-clock competing with it.
#if DC_VENUE == DC_VENUE_BINANCE
    const bool fetch_in_flight = seed_.busy();
#else
    constexpr bool fetch_in_flight = false;
#endif
    if (fetch_in_flight) { ++stats_.frames_during_fetch; }

    // The per-core idle counters as of THIS instant, taken before any work so
    // the window they will be differenced over is the same window `event_gaps`
    // measures: previous event's `now` to this one's. Two 32-bit loads.
    const std::uint32_t idle0_now = idle_.idle_us(0);
    const std::uint32_t idle1_now = idle_.idle_us(1);

    // How long this message waited between being complete on the socket and
    // this task looking at it, and how much else was still queued behind it.
    // Both are sampled before any work, because both are measurements OF the
    // wait — taken afterwards they would include the parse and stop meaning
    // "scheduling".
    if (msg.arrival_us != 0 && now > msg.arrival_us) {
        const std::uint32_t waited =
            clamp_us_to_u32(static_cast<std::uint64_t>(now - msg.arrival_us));
        if (waited > stats_.worst_queue_wait_us) { stats_.worst_queue_wait_us = waited; }
    }
    const std::uint32_t backlog = pipe_.ready_waiting();
    if (backlog > stats_.max_ready_backlog) { stats_.max_ready_backlog = backlog; }

    // Inter-arrival against the previous message, for the recovery shape. Held
    // until after the adapter has run so it can be filed in the right order —
    // see the note at the call below.
    std::uint32_t arrival_delta_us = 0;
    if (msg.arrival_us != 0) {
        if (last_msg_arrival_us_ != 0 && msg.arrival_us > last_msg_arrival_us_) {
            arrival_delta_us =
                clamp_us_to_u32(static_cast<std::uint64_t>(msg.arrival_us - last_msg_arrival_us_));
        }
        last_msg_arrival_us_ = msg.arrival_us;
    }

    // Whether this frame reached the book. It is no longer the watchdog's
    // question — since M4 stage D the watchdog watches the venue's liveness
    // signal — but it is still the age's, the stall probe's and the gap
    // histogram's: bytes arriving is not the same as the ladder advancing.
    const std::uint64_t events_before = adapter_.stats().events_out;
    const std::string_view text(pipe_.buffer(msg.slot), msg.len);
    adapter_.on_frame(text, [this](const FeedEvent& ev) { apply_only(ev); });
    // THE MESSAGE BOUNDARY. Immediately after the adapter call and before
    // anything below it, because the reject capture and the slot recycle that
    // follow both have their own forced ordering and neither is allowed to sit
    // between a message and the frame it produces.
    publish_message();

    // A rejected frame is captured HERE — after the adapter, before the recycle
    // below — and the order is forced rather than chosen. `text` points into the
    // slot, so recycling first would hand the network task a buffer this is
    // still reading; and asking the adapter first is what supplies the status,
    // which is most of the diagnosis.
    //
    // Deliberately every non-Ok status, not only the ones `parse_errors` counts:
    // BadPrice and OtherTicker have their own counters and read zero today, and
    // if either ever moves the payload behind it is worth exactly as much as
    // these. The cost when nothing is wrong is one comparison.
    //
    // The work this does lands inside `worst_parse_us` below, which is correct —
    // it is time this task spent on this frame — and is bounded by the capture
    // budget, so it cannot grow with the size of a burst.
    if (adapter_.last_status() != venue::ParseStatus::Ok) {
        stats_.rejects.note(adapter_.last_status(), text.data(),
                            static_cast<std::uint32_t>(text.size()), now);
    }

    // The slot goes back only after the adapter is done with it: a Snapshot's
    // LevelSpan points into the adapter's decoded frame, not into this buffer,
    // but the decoded frame was produced *from* these bytes and the parser is
    // allowed to slice string_views out of them. Recycling earlier would be a
    // use-after-free the ladder would render.
    pipe_.recycle(msg.slot);

    const std::int64_t done = esp_timer_get_time();

    // ORDER MATTERS, and this is the one line of it worth a comment. A message
    // is offered as a recovery sample BEFORE it is allowed to open a hole of its
    // own, so the message that ends a hole is filed against the hole *before*
    // it, never as the first sample of its own recovery. Reversed, every hole
    // would report a first recovery gap equal to the hole and every recovery
    // would read as a resumed cadence.
    stats_.stall.note_message(arrival_delta_us);

    // BOTH CLOCKS, ONE STAMP. This is the line the whole of A1 exists to reach:
    // the same `on_liveness` the host replay driver calls, with the same
    // arrival, feeding the same two objects. Host and target now compute the
    // book's age — and the threshold the panel greys on — with one
    // implementation instead of two that agreed only because Anvil happened to
    // broadcast at the interval the deleted estimator had hardcoded.
    //
    // Stamped at `now` — when this task took the message — rather than at
    // `msg.arrival_us`. Both are defensible and the difference is the a->e
    // latency, which this run measures at 5-25 ms against a 500 ms broadcast
    // period: two orders of magnitude below the quantisation of the estimate, so
    // the simpler stamp is the honest one. If a->e ever reaches a sizeable
    // fraction of a period, this line is where that starts to matter and the
    // arrival stamp is the fix.
    //
    // A liveness frame that FAILED to parse never increments the counter and so
    // counts as missing, inflating the lag — cross-check `-- errors parse=`,
    // which is why both lines are in the same block.
    service_liveness(now);

    // AND THE HEALING PATH (A2), PUBLISHED AS A LEVEL RATHER THAN AN EDGE.
    //
    // `has_baseline()` is the question that actually matters: a Kraken book with
    // no baseline can only be restored by a snapshot, and Kraken sends one only
    // in answer to a subscribe. Stating that on every frame means a transport
    // that cannot act right now — because the floor has not expired, because a
    // heal is already in flight, because there is no socket — simply sees it
    // again next pass. The edge-triggered first draft lost the request in all
    // three of those cases and in a fourth nobody had thought of: the liveness
    // watchdog firing over a LIVE socket drops the book without latching
    // anything, and a 4 s RF fade is enough to reach it. Every one of them left
    // the panel grey over a healthy heartbeating socket until a power cycle.
    //
    // The adapter's own latch is still cleared here, because it is what counts
    // CRC failures (`resyncs_requested`) and it must not sit set for ever — but
    // nothing now depends on catching it.
    //
    // A REFUSED SUBSCRIPTION IS THE OTHER HALF, and it is a latch because it is
    // terminal: `success: false` means a delisted pair, an unsupported depth or
    // a rate limit, and the adapter's contract says the firmware turns that into
    // `die()` so the supervisor retries with backoff. Without it the board holds
    // a live socket over a permanently empty book — the exact failure stage 0
    // measured when `depth: 27` was refused.
    //
    // `#if` and not `if constexpr`: outside a template `if constexpr` still
    // INSTANTIATES the discarded branch, so an Anvil build would have to compile
    // `adapter_.resync_wanted()` against an adapter that has no such member.
#if DC_VENUE_HAS_SUBSCRIPTION
    if (adapter_.resync_wanted()) { adapter_.clear_resync_wanted(); }
    subscription_.set_wanted(!adapter_.has_baseline());
    if (adapter_.refused()) { subscription_.set_refused(); }
#endif

    if (adapter_.stats().events_out != events_before) {
        // Measured from the previous EVENT and not gated on any armed flag, so
        // the first event after an outage records the whole hole. Gating it was
        // a real defect: the watchdog cleared the flag, so the one number that
        // quantifies a gap was suppressed by the very thing that detected it.
        // `LivenessWatchdog::on_liveness` carries the same scar for the same
        // reason, and `test_liveness_watchdog.cpp` pins it there.
        if (last_event_us_ != 0) {
            const std::uint64_t gap = static_cast<std::uint64_t>(now - last_event_us_);
            if (gap > stats_.worst_gap_us) { stats_.worst_gap_us = gap; }
            // The distribution behind that maximum — book-event silence, which
            // since M4 stage D is a NUMBER and not a fault. Its >1 s column used
            // to be readable as "occasions the panel had grounds to grey" and is
            // not any more: the grey rule watches the liveness signal against a
            // calibrated threshold, and `greys` counts those directly. What this
            // distribution still answers is how quiet this market gets, which is
            // the raw material of `age_ms` and the reason MINA/GBP needed the
            // ruling in the first place.
            stats_.event_gaps.add(gap);
            // The same sample, taken apart. Sub-threshold windows build the
            // healthy-idle baseline the verdict is measured against; a window
            // over the threshold opens a hole record and is classified against
            // it. `idle_.valid()` is false when the probe could not register or
            // was compiled out, and then every hole reports its idle as unknown
            // rather than as zero — which would read as total starvation.
            stats_.stall.note_event(clamp_us_to_u32(gap),
                                    wrapping_delta_u32(idle0_now, prev_idle0_us_),
                                    wrapping_delta_u32(idle1_now, prev_idle1_us_),
                                    idle_.valid(), wire_seq_for_probe(), link_.last_dbm(),
                                    socket_dropped_pending_);
            socket_dropped_pending_ = false;
        }
        prev_idle0_us_ = idle0_now;
        prev_idle1_us_ = idle1_now;
        // Arrival -> event for this message: everything between the bytes being
        // complete on the socket and the book having moved. Recorded only when
        // an event actually came out, so it prices the path the panel depends
        // on rather than the cost of discarding a `summary`.
        if (msg.arrival_us != 0 && done > msg.arrival_us) {
            stats_.arrival_to_event.add(static_cast<std::uint64_t>(done - msg.arrival_us));
        }
        last_event_us_ = now;
        gap_raised_ = false;
    }

    republish_if_due(now);

    const std::uint32_t elapsed = static_cast<std::uint32_t>(done - now);
    if (elapsed > stats_.worst_parse_us) { stats_.worst_parse_us = elapsed; }
    // Every frame, not just the worst one. See the Stats member for why a bare
    // maximum was the wrong instrument.
    stats_.frame_times.add(elapsed);
    // ...and whether this one continued a run of slow ones. Note it on EVERY
    // frame, not only the slow ones: a fast frame is what ends a run, and a
    // tracker that only heard about the slow ones would report one unbroken run
    // for the life of the board.
    stats_.slow_run.note(elapsed >= FrameScale::kSlowUs);
    // The same measurement, attributed. See the Stats fields for why: this is
    // wall-clock, so a fetch on another task shows up here as though the feed
    // had done more work.
    if (fetch_in_flight) {
        if (elapsed > stats_.worst_parse_fetch_us) { stats_.worst_parse_fetch_us = elapsed; }
    } else {
        if (elapsed > stats_.worst_parse_quiet_us) { stats_.worst_parse_quiet_us = elapsed; }
    }
}

// A QUIET MARKET MUST NOT LOOK LIKE A DEAD RENDERER, and until review it did.
//
// `RenderTask` redraws only when `SnapshotChannel::consume()` reports a new
// version, and a new version only ever appeared when an event reached the book.
// So through MINA/GBP's 26 s of legitimate book silence — the exact condition
// M4's definition of done names, and the reason this build subscribes that pair
// — the panel froze completely: the ladder, the age, and the corner beat pixel
// whose entire job is to say the renderer is alive. A bench watching for
// "holds colour through 26 s" would have been unable to tell a healthy quiet
// market from a hung Core 1.
//
// It is not only cosmetic. `age_ms` is stamped at publish, so with no publishes
// the panel's age is frozen at whatever it was — and a socket that begins to
// back up shows its lag in the liveness cadence, which produces no events at
// all. The one number that would say "live, and ninety seconds behind" could
// not update in exactly the case it exists for.
//
// So a liveness arrival that produced no event republishes the unchanged book.
// The cost is at most one extra publish per liveness arrival — 2/s at Anvil,
// 1/s at Kraken — against a channel that is wait-free on this side by
// construction (invariant #4), and `SnapshotChannel` drops superseded frames
// silently, so a busy market never pays it at all.
void FeedTask::republish_if_due(std::int64_t now_us) noexcept {
    if (last_publish_us_ != 0 && now_us - last_publish_us_ < kRepublishPeriodUs) { return; }
    publish_current();
}

void FeedTask::on_watchdog() noexcept {
    // The queue wait ended without a message. That is only a watchdog expiry if
    // the deadline has genuinely passed — a tick's rounding, or a spurious wake,
    // must not grey the panel — so the object is asked rather than assumed.
    const std::int64_t now_ns = ns_from_us(esp_timer_get_time());
    if (!watchdog_.expired(now_ns)) { return; }
    ++stats_.watchdog_gaps;
    raise_gap_once();
    // Disarm until the venue speaks again, so one outage is one Gap rather than
    // one a threshold for as long as it is quiet. It also voids the age, for the
    // same reason a disconnect does — see liveness_watchdog.hpp.
    watchdog_.note_fired(now_ns);
}

void FeedTask::on_disconnected() noexcept {
    ++stats_.socket_gaps;
    // Bank whatever backlog this socket had accumulated BEFORE the outage starts
    // inflating the running estimate. The estimate keeps climbing while the feed
    // is down — honest about the screen, but that time is grey, not backlog —
    // and the reconnect would then zero the peak on the way back up, losing the
    // whole episode. See liveness_watchdog.hpp.
    watchdog_.on_socket_change(ns_from_us(esp_timer_get_time()));
    // Flagged, not acted on: the hole this outage is inside is only recorded
    // when data returns, and by then nothing else would remember that the
    // transport went down in the middle of it. A reconnect costs a ~2.5 s
    // blocking stop() on Core 1, so a hole carrying this flag must never be read
    // as a sample of the steady-state stall.
    socket_dropped_pending_ = true;
    raise_gap_once();
}

void FeedTask::raise_gap_once() noexcept {
    if (gap_raised_) { return; }
    gap_raised_ = true;
    // GapReason::Disconnect for both the watchdog and the socket callback: from
    // the book's point of view they are the same event, and ARCHITECTURE §4 says
    // a venue that sends no gap frame has one synthesised transport-side.
    // Neither venue here sends one — Anvil has no error frame at all, and
    // Kraken's silence is silence.
    adapter_.on_transport_gap(GapReason::Disconnect,
                              [this](const FeedEvent& ev) { apply_only(ev); });
    // A synthesised gap is a message too — one the transport wrote rather than
    // the venue.
    publish_message();
}

void FeedTask::apply_only(const FeedEvent& ev) noexcept {
    book_.apply(ev);
    pending_publish_ = true;
}

// ONE PUBLISH PER MESSAGE (M5 stage E), mirroring `publish_message` in
// `harness/src/replay_driver.cpp`. The two are the same rule in two places
// because this class and that one are the same object either side of the desk —
// and the host is where the rule is measurable, which is why the driver went
// first and this follows it rather than the other way round.
//
// WHY. One `depthUpdate` becomes N single-side `Delta` events, and publishing
// after each of them samples the book between the bid levels that lift the touch
// and the ask removals in the same message that pay for them. The 34.5 h soak
// drew 1,032 LIVE ladder lines whose best bid was at or above their best ask,
// 2.9% of the time the panel claimed to be live, worst spread -$39.79; the host
// corpus had 11,062 such publishes and now has none.
//
// AND IT CUTS THE WORK THIS TASK DOES PER MESSAGE BY THE DELTA COUNT, which is
// the other reason it is here: six task-watchdog aborts in that soak, six for
// six with IDLE on CPU 0 starved while `dc_feed` held it. That is a HYPOTHESIS
// and the soak is what tests it (stage E §8) — a `DisplaySnapshot` is 1,168
// bytes and it was being built, stamped and copied hundreds of times per
// message, but nothing here proves that was the cause.
//
// CONDITIONAL, and the condition is what keeps a quiet venue quiet: a frame the
// adapter ignored — a heartbeat, an ack, an out-of-bracket diff — publishes
// nothing, exactly as before. `republish_if_due` remains the floor beneath all
// of it, so a book that produces no message at all still refreshes the age and
// the beat pixel every `kRepublishPeriodUs`.
void FeedTask::publish_message() noexcept {
    if (!pending_publish_) { return; }
    pending_publish_ = false;
    publish_current();
}

// THE PUBLISH PATH, PLUS THE ONE LINE THE PANEL'S AGE FIELD HAS BEEN WAITING FOR
// SINCE STAGE A2.
//
// `DisplaySnapshot::age_ms` and `has_age` have existed since M4 stage A2 and
// nothing in `firmware/` has ever written either: the host replay driver stamped
// them and the board did not, so the field the panel would draw was structurally
// zero. The stamp below is `stamp_age()` from `replay_driver.cpp`, in the same
// position — after `Book::publish` and before the channel — for the same reason:
// the book has no clock (invariant #1) and the feed side is the single writer
// (invariant #8), so the field belongs to the two statements between them.
//
// `age_and_bank` rather than `age`, because this is the once-per-publish caller
// and the high-water mark is what survives a reconnect. **M5 stage E made that
// sentence more true rather than less:** a publish is now one per venue message
// rather than one per level, so "once per publish" and "once per thing the venue
// said" have become the same statement.
//
// AND THE TWO INSTRUMENTS BELOW SURVIVE THE THINNING, for different reasons and
// neither of them by accident. This clock is read live, so unlike the host
// driver — where the reading is constant within a message and the dropped calls
// were exact duplicates — the readings here genuinely differ. What is kept is
// the LAST of each message, and `now` only advances within one, so the reading
// that survives is the largest and `bank`'s high-water mark cannot fall.
// `GreyLedger::note` is edge-triggered: it acts only on a live/grey TRANSITION,
// and a transition is caused by an event, so the publish that follows that
// event's message is the same publish that used to catch it. What moves is the
// instant it is stamped at, by the width of one message.
//
// The clock is read ONCE and used for both the age and the grey ledger, so the
// two cannot disagree about which instant this frame belongs to.
void FeedTask::publish_current() noexcept {
    book_.publish(staging_);

    const std::int64_t now = esp_timer_get_time();
    const AgeReading r = watchdog_.age_and_bank(ns_from_us(now));
    staging_.has_age = r.valid;
    staging_.age_ms = r.ms;

    stats_.grey.note(staging_.live(), now);
    last_publish_us_ = now;
    channel_.publish(staging_);
}



}  // namespace depthcharge::fw
