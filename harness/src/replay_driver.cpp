// dc_harness/replay_driver.cpp — see replay_driver.hpp for the design notes.
#include "dc_harness/replay_driver.hpp"

#include <algorithm>

#include <depthcharge/feed_event.hpp>

#include "dc_harness/age_estimator.hpp"
#include "dc_harness/liveness_clock.hpp"
#include "dc_harness/trace_decoder.hpp"

namespace dc::harness {
namespace {

using depthcharge::Book;
using depthcharge::DisplaySnapshot;
using depthcharge::FeedEvent;
using depthcharge::FeedStatus;
using depthcharge::GapReason;
using depthcharge::SnapshotChannel;

// Holds the wiring for one replay. The engine pieces are members, in the same
// arrangement the firmware feed task will own them (invariant #8: this object
// is the single writer).
class Replay {
public:
    Replay(const depthcharge::SymbolSpec& symbol, const ReplayOptions& opts,
           const ReplayObserver& observer)
        : opts_(opts), observer_(observer), decoder_(symbol), book_(symbol) {}

    // The threshold in force right now: the caller's override if it gave one,
    // otherwise whatever this venue's own liveness signal has calibrated to.
    double threshold_ms() const noexcept {
        return opts_.disconnect_gap_ms > 0.0 ? opts_.disconnect_gap_ms
                                             : liveness_.threshold_ms();
    }

    ReplayResult run(TraceReader& reader) {
        ReplayResult result;
        result.meta = reader.meta();

        TraceFrame frame;
        // Both stop conditions live in the loop condition, so neither reads a
        // line it will not use. They used to sit in the body after next() had
        // already parsed and validated one more line, which meant a malformed
        // line just past the stop point still threw at a caller that had asked
        // to stop.
        while (!stopped_ && (opts_.max_frames == 0 || frames_ < opts_.max_frames) &&
               reader.next(frame)) {
            if (have_prev_rx_) {
                const double gap_ms = static_cast<double>(frame.rx_ns - prev_rx_ns_) / 1e6;
                if (gap_ms > threshold_ms()) {
                    raise_watchdog_gap(gap_ms);
                    if (stopped_) { break; }
                }
            } else {
                result.first_rx_ns = frame.rx_ns;
                have_prev_rx_ = true;
            }
            prev_rx_ns_ = frame.rx_ns;
            result.last_rx_ns = frame.rx_ns;
            frames_ = frame.index;

            // Feed the calibrator AFTER the gap test, so the threshold a gap is
            // judged against is the one that was in force while it was open.
            // The age meter reads the SAME arrivals and extracts a different
            // quantity from them: the clock decides when to grey and must keep a
            // ROLLING median, the estimator says how far behind the book is and
            // needs a reference that does not move with the backlog it is
            // measuring. Neither derives its number from the other's window —
            // see age_estimator.hpp for the reconnect case where that would be a
            // silent wrong answer (M4 stage A2).
            if (decoder_.classify(frame).is_liveness) {
                ++liveness_arrivals_;
                liveness_.on_liveness(frame.rx_ns);
                age_.on_liveness(frame.rx_ns);
            }

            current_frame_ = frame.index;
            current_rx_ns_ = frame.rx_ns;
            decoder_.decode(frame, [this](const FeedEvent& ev) { on_event(ev); });
        }

        // The trace ended. If the caller told us how long the stream stayed
        // silent afterwards, apply the same watchdog rule to that silence — a
        // file has no "now", so this is the only way trailing silence can grey
        // the panel the way the M3 timer would.
        if (!stopped_ && have_prev_rx_ &&
            opts_.end_of_trace_silence_ms > threshold_ms()) {
            raise_watchdog_gap(opts_.end_of_trace_silence_ms);
            result.last_rx_ns = prev_rx_ns_;
        }

        // Latest published state is the run's answer; if the trace produced no
        // event at all the channel still holds the stale-by-construction start.
        if (channel_.published_version() == 0) {
            book_.publish(latest_);
            stamp_age(latest_);
        }

        result.frames = frames_;
        result.events = events_;
        result.threshold_ms = threshold_ms();
        result.worst_age_ms = age_.worst_ms();
        result.age_baseline_ms = age_.baseline_ms();
        result.liveness_median_ms = liveness_.median_ms();
        // The TOTAL, not the clock's 32-sample window: a report that says "32 x
        // summary" over a two-minute trace looks like a thin feed.
        result.liveness_arrivals = liveness_arrivals_;
        result.adapter = decoder_.adapter().stats();
        result.book = book_.stats();
        result.episodes = episodes_;
        result.final_snapshot = latest_;
        result.first_stale_snapshot = first_stale_;
        result.saw_stale = saw_stale_;
        return result;
    }

private:
    // The RX watchdog fires `disconnect_gap_ms` after the last frame, not when
    // the next one lands — so the stale window has the duration it would have
    // had on the panel.
    void raise_watchdog_gap(double gap_ms) {
        const std::int64_t watchdog_ns =
            prev_rx_ns_ + static_cast<std::int64_t>(threshold_ms() * 1e6);

        // Only a Snapshot clears stale, and frames that emit no event (summary)
        // or no re-baseline (trade) cannot. So a hole arriving while the panel
        // is already grey continues the same outage: fold it in rather than
        // opening a second window that would report the first as never cleared
        // and time the grey period from the wrong start.
        if (!episodes_.empty() && !episodes_.back().cleared) {
            StaleEpisode& open = episodes_.back();
            ++open.gap_events;
            open.observed_gap_ms = std::max(open.observed_gap_ms, gap_ms);
        } else {
            StaleEpisode ep;
            ep.reason = GapReason::Disconnect;
            ep.frame_before = frames_;
            ep.watchdog_rx_ns = watchdog_ns;
            ep.observed_gap_ms = gap_ms;
            ep.gap_events = 1;
            episodes_.push_back(ep);
        }

        current_frame_ = 0;  // no frame arrived; this event is the transport's
        current_rx_ns_ = watchdog_ns;
        decoder_.on_transport_gap(GapReason::Disconnect,
                                  [this](const FeedEvent& ev) { on_event(ev); });

        // THE BACKLOG DIED WITH THE SOCKET (M4 stage A2). A reconnect is given a
        // fresh server-side send queue and a fresh snapshot, so an estimate
        // carried across it would be measuring a queue that no longer exists.
        // Reset here rather than at the resync: the peak is banked on the way
        // down, and doing it AFTER the Gap has published leaves the grey frame
        // carrying the age the book had actually reached when the watchdog
        // fired. Everything after it reads "no reading yet" until the new
        // connection has delivered a window — which is the honest answer, and
        // the panel is grey throughout it anyway.
        //
        // The liveness clock is deliberately NOT reset: cadence is a property of
        // the venue, not of the connection, and the hole enters its median
        // window as a single sample that moves a rank rather than the median.
        age_.on_reconnect(watchdog_ns);
    }

    // The age, stamped one line after the publish that filled everything else.
    //
    // WHY THE BOOK DOES NOT DO IT: `engine/` has no clock and is not being given
    // one (invariant #1 keeps it host-buildable and I/O-free, and a book that
    // reads a clock is a book whose replay is no longer deterministic). So
    // `Book::publish` fills the market state and the feed side — the same single
    // writer, one line later (invariant #8) — stamps how far behind it is.
    //
    // Stamped at PUBLISH, so the number on screen is as fresh as the last
    // published frame. That is exact rather than approximate: age only moves
    // when the liveness signal thins, and a feed whose liveness signal has
    // thinned is still delivering the book frames that publish. When nothing
    // publishes at all, either the market is quiet and the age is genuinely
    // unchanged (Kraken's 25.8 s book hole with heartbeats on time), or the feed
    // has stopped and the panel is grey — where grey, not a number, is the
    // honesty channel.
    void stamp_age(DisplaySnapshot& snap) noexcept {
        const AgeReading r = age_.read_and_bank(current_rx_ns_);
        snap.has_age = r.valid;
        snap.age_ms = r.ms;
    }

    void on_event(const FeedEvent& ev) {
        ++events_;

        const bool was_stale = book_.status() == FeedStatus::Stale;
        book_.apply(ev);
        const bool is_stale = book_.status() == FeedStatus::Stale;

        if (ev.kind == FeedEvent::Kind::Gap && !episodes_.empty() &&
            episodes_.back().gap_seq == 0) {
            episodes_.back().gap_seq = ev.seq;
        }
        if (was_stale && !is_stale && !episodes_.empty() && !episodes_.back().cleared) {
            StaleEpisode& ep = episodes_.back();
            ep.cleared = true;
            ep.cleared_frame = current_frame_;
            ep.cleared_rx_ns = current_rx_ns_;
            ep.stale_ms = static_cast<double>(ep.cleared_rx_ns - ep.watchdog_rx_ns) / 1e6;
        }

        book_.publish(latest_);
        stamp_age(latest_);
        channel_.publish(latest_);

        if (is_stale && !saw_stale_ && ev.kind == FeedEvent::Kind::Gap) {
            first_stale_ = latest_;
            saw_stale_ = true;
        }

        if (observer_) {
            ReplayStep step;
            step.frame_index = current_frame_;
            step.event_index = events_;
            step.rx_ns = current_rx_ns_;
            step.kind = ev.kind;

            // The render side takes the published frame the way the M3 render
            // task will — through the channel, by copy — so the seam is
            // exercised by every replay, not just documented.
            DisplaySnapshot rendered{};
            if (!channel_.consume(rendered)) { rendered = latest_; }
            if (!observer_(step, rendered)) { stopped_ = true; }
        }
    }

    ReplayOptions opts_;
    const ReplayObserver& observer_;

    // The venue's decoder, not the adapter directly (M4 stage A, deliverable 2).
    // A pure forwarding wrapper today — that is the point: the driver now names
    // a seam a second venue can fill rather than naming Anvil, and the change
    // is provably behaviour-free because every committed trace was diffed
    // through the readers before and after it.
    AnvilTraceDecoder decoder_;
    LivenessClock liveness_;
    AgeEstimator age_;
    Book book_;
    SnapshotChannel channel_;
    DisplaySnapshot latest_{};
    DisplaySnapshot first_stale_{};

    std::vector<StaleEpisode> episodes_;
    std::size_t frames_ = 0;
    std::size_t events_ = 0;
    std::size_t liveness_arrivals_ = 0;
    std::size_t current_frame_ = 0;
    std::int64_t current_rx_ns_ = 0;
    std::int64_t prev_rx_ns_ = 0;
    bool have_prev_rx_ = false;
    bool saw_stale_ = false;
    bool stopped_ = false;
};

}  // namespace

depthcharge::SymbolSpec symbol_for(const TraceMeta& meta) {
    const auto& declared = depthcharge::anvil::kAnvilTicker101;
    return depthcharge::SymbolSpec{
        meta.ticker >= 0 ? static_cast<std::uint32_t>(meta.ticker) : declared.id,
        declared.price_decimals,
        declared.qty_decimals};
}

ReplayResult run_replay(TraceReader& reader, const depthcharge::SymbolSpec& symbol,
                        const ReplayOptions& opts, const ReplayObserver& observer) {
    // The driver feeds THE VENUE'S OWN ADAPTER (ARCHITECTURE §9, 2026-08-17),
    // and at stage A there is exactly one. Refusing here rather than feeding
    // Kraken's JSON to the Anvil parser is the difference between a loud stop
    // and a dead ladder over a 100% parse-error count — and the second is the
    // shape of failure invariant #5 exists to forbid, arriving through the test
    // rig instead of the panel.
    if (reader.venue() != Venue::Anvil) {
        throw TraceError(reader.name(), 1,
                         "replay driver has no adapter for venue \"" +
                             std::string(venue_traits(reader.venue()).name) +
                             "\" (M4 stage A ships its decoder as a classifier; the "
                             "adapter is stage B). Use dc_taxonomy to read this trace.");
    }
    Replay replay(symbol, opts, observer);
    return replay.run(reader);
}

ReplayResult run_replay_file(const std::string& path, const depthcharge::SymbolSpec& symbol,
                             const ReplayOptions& opts, const ReplayObserver& observer) {
    TraceReader reader(path);
    return run_replay(reader, symbol, opts, observer);
}

ReplayResult run_replay_text(std::string_view trace_text, const depthcharge::SymbolSpec& symbol,
                             const ReplayOptions& opts, const ReplayObserver& observer) {
    TraceReader reader(trace_text, in_memory);
    return run_replay(reader, symbol, opts, observer);
}

}  // namespace dc::harness
