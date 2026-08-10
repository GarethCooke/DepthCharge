// firmware/src/feed_task.cpp — see feed_task.hpp.
#include "feed_task.hpp"

#include <string_view>

#include "esp_timer.h"

namespace depthcharge::fw {
namespace {
constexpr std::int64_t kWatchdogUs = static_cast<std::int64_t>(kRxWatchdogMs) * 1000;
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
        // The wait IS the watchdog. While the book is advancing we sleep only
        // until the deadline the last event set; with nothing to watch for we
        // sleep indefinitely and let the queue wake us.
        TickType_t wait = portMAX_DELAY;
        if (watching_) {
            const std::int64_t remaining_us = (last_event_us_ + kWatchdogUs) - esp_timer_get_time();
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
            case FeedMessage::Kind::Connected:
                ++stats_.connects;
                // Deliberately does NOT clear stale. Only a fresh Snapshot can
                // (invariant #5), and on reconnect Anvil sends exactly one as
                // its first frame (protocol §4) — so the panel goes live on
                // data, never on the mere fact of a socket.
                break;
            case FeedMessage::Kind::Disconnected: on_disconnected(); break;
        }
    }
}

void FeedTask::on_frame(const FeedMessage& msg) noexcept {
    const std::int64_t now = esp_timer_get_time();
    ++stats_.frames_in;

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

    // Whether this frame reaches the book is the question the watchdog cares
    // about, so ask the adapter rather than assuming. Bytes arriving is not the
    // same as the ladder advancing — see the note on kRxWatchdogMs.
    const std::uint64_t events_before = adapter_.stats().events_out;

    const std::string_view text(pipe_.buffer(msg.slot), msg.len);
    adapter_.on_frame(text, [this](const FeedEvent& ev) { apply_and_publish(ev); });

    // The slot goes back only after the adapter is done with it: a Snapshot's
    // LevelSpan points into the adapter's decoded frame, not into this buffer,
    // but the decoded frame was produced *from* these bytes and the parser is
    // allowed to slice string_views out of them. Recycling earlier would be a
    // use-after-free the ladder would render.
    pipe_.recycle(msg.slot);

    const std::int64_t done = esp_timer_get_time();

    if (adapter_.stats().events_out != events_before) {
        // Measured from the previous EVENT and not gated on `watching_`, so the
        // first event after an outage records the whole hole. Gating it was a
        // real defect: the watchdog clears `watching_`, so the one number that
        // quantifies a gap was suppressed by the very thing that detected it.
        if (last_event_us_ != 0) {
            const std::uint64_t gap = static_cast<std::uint64_t>(now - last_event_us_);
            if (gap > stats_.worst_gap_us) { stats_.worst_gap_us = gap; }
            // The distribution behind that maximum. Same sample, same instant,
            // same rule as the watchdog — so the >1 s buckets are literally a
            // count of the occasions the panel had grounds to grey, and they
            // must agree with `watchdog_gaps` up to the one hole that is still
            // open when the log is read.
            stats_.event_gaps.add(gap);
        }
        // Arrival -> event for this message: everything between the bytes being
        // complete on the socket and the book having moved. Recorded only when
        // an event actually came out, so it prices the path the panel depends
        // on rather than the cost of discarding a `summary`.
        if (msg.arrival_us != 0 && done > msg.arrival_us) {
            stats_.arrival_to_event.add(static_cast<std::uint64_t>(done - msg.arrival_us));
        }
        last_event_us_ = now;
        watching_ = true;
        gap_raised_ = false;
    }

    const std::uint32_t elapsed = static_cast<std::uint32_t>(done - now);
    if (elapsed > stats_.worst_parse_us) { stats_.worst_parse_us = elapsed; }
}

void FeedTask::on_watchdog() noexcept {
    if (!watching_) { return; }
    ++stats_.watchdog_gaps;
    raise_gap_once();
    // Stop watching until data returns, so one outage is one Gap rather than one
    // a second for as long as the Wi-Fi is out.
    watching_ = false;
}

void FeedTask::on_disconnected() noexcept {
    ++stats_.socket_gaps;
    raise_gap_once();
    watching_ = false;
}

void FeedTask::raise_gap_once() noexcept {
    if (gap_raised_) { return; }
    gap_raised_ = true;
    // GapReason::Disconnect for both the watchdog and the socket callback: from
    // the book's point of view they are the same event, and ARCHITECTURE §4 says
    // this venue synthesises Gap{Disconnect} transport-side because Anvil never
    // sends one.
    adapter_.on_transport_gap(GapReason::Disconnect,
                              [this](const FeedEvent& ev) { apply_and_publish(ev); });
}

void FeedTask::apply_and_publish(const FeedEvent& ev) noexcept {
    book_.apply(ev);
    publish_current();
}

void FeedTask::publish_current() noexcept {
    book_.publish(staging_);
    channel_.publish(staging_);
}

}  // namespace depthcharge::fw
