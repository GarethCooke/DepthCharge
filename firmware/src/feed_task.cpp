// firmware/src/feed_task.cpp — see feed_task.hpp.
#include "feed_task.hpp"

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
    // Pinned to Core 0 (ARCHITECTURE §2). Core 1 is the render side's, and at
    // stage D the HUB75 DMA driver will want it to itself.
    const BaseType_t ok = xTaskCreatePinnedToCore(&FeedTask::trampoline, "dc_feed",
                                                  stack_bytes, this, priority, nullptr, 0);
    return ok == pdPASS;
}

void FeedTask::trampoline(void* self) noexcept {
    static_cast<FeedTask*>(self)->run();
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
        TickType_t wait = portMAX_DELAY;
        if (watchdog_.armed()) {
            const std::int64_t remaining_ns =
                watchdog_.deadline_ns() - ns_from_us(esp_timer_get_time());
            const std::int64_t remaining_us = remaining_ns / 1000;
            wait = (remaining_us <= 0)
                       ? 0
                       : pdMS_TO_TICKS(static_cast<std::uint32_t>((remaining_us + 999) / 1000));
        }

        FeedMessage msg;
        if (!pipe_.receive(msg, wait)) {
            on_watchdog();
            continue;
        }

        switch (msg.kind) {
            case FeedMessage::Kind::Frame:        on_frame(msg); break;
            case FeedMessage::Kind::Connected: {
                ++stats_.connects;
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
            case FeedMessage::Kind::Disconnected: on_disconnected(); break;
        }
    }
}

void FeedTask::on_frame(const FeedMessage& msg) noexcept {
    const std::int64_t now = esp_timer_get_time();
    ++stats_.frames_in;

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
    // And whether it was the venue's LIVENESS SIGNAL — Anvil's 2 Hz `summary`,
    // Kraken's 1 Hz `heartbeat` — which is the one frame kind on either wire
    // that carries a CLOCK rather than a market event. Which counter that is, is
    // the only venue knowledge in this function, and it is one line in
    // `venue_build.hpp`. Taken by differencing the adapter's own counter rather
    // than by adding a firmware instrument to the adapter: `engine/` is shared
    // with the host replay and is not modified from here (invariant #1).
    const std::uint64_t liveness_before = venue::liveness_count(adapter_);

    const std::string_view text(pipe_.buffer(msg.slot), msg.len);
    adapter_.on_frame(text, [this](const FeedEvent& ev) { apply_and_publish(ev); });

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
    if (venue::liveness_count(adapter_) != liveness_before) {
        watchdog_.on_liveness(ns_from_us(now));
    }

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
                              [this](const FeedEvent& ev) { apply_and_publish(ev); });
}

void FeedTask::apply_and_publish(const FeedEvent& ev) noexcept {
    book_.apply(ev);
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
// and the high-water mark is what survives a reconnect.
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
