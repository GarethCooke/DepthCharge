// dc_harness/replay_driver.cpp — see replay_driver.hpp for the design notes.
#include "dc_harness/replay_driver.hpp"

#include <algorithm>

#include <depthcharge/feed_event.hpp>

namespace dc::harness {
namespace {

using depthcharge::Book;
using depthcharge::DisplaySnapshot;
using depthcharge::FeedEvent;
using depthcharge::FeedStatus;
using depthcharge::GapReason;
using depthcharge::SnapshotChannel;
using depthcharge::anvil::AnvilAdapter;

// Holds the wiring for one replay. The engine pieces are members, in the same
// arrangement the firmware feed task will own them (invariant #8: this object
// is the single writer).
class Replay {
public:
    Replay(const depthcharge::SymbolSpec& symbol, const ReplayOptions& opts,
           const ReplayObserver& observer)
        : opts_(opts), observer_(observer), adapter_(symbol), book_(symbol) {}

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
                if (gap_ms > opts_.disconnect_gap_ms) {
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

            current_frame_ = frame.index;
            current_rx_ns_ = frame.rx_ns;
            adapter_.on_frame(frame.frame_json, [this](const FeedEvent& ev) { on_event(ev); });
        }

        // The trace ended. If the caller told us how long the stream stayed
        // silent afterwards, apply the same watchdog rule to that silence — a
        // file has no "now", so this is the only way trailing silence can grey
        // the panel the way the M3 timer would.
        if (!stopped_ && have_prev_rx_ &&
            opts_.end_of_trace_silence_ms > opts_.disconnect_gap_ms) {
            raise_watchdog_gap(opts_.end_of_trace_silence_ms);
            result.last_rx_ns = prev_rx_ns_;
        }

        // Latest published state is the run's answer; if the trace produced no
        // event at all the channel still holds the stale-by-construction start.
        if (channel_.published_version() == 0) { book_.publish(latest_); }

        result.frames = frames_;
        result.events = events_;
        result.adapter = adapter_.stats();
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
            prev_rx_ns_ + static_cast<std::int64_t>(opts_.disconnect_gap_ms * 1e6);

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
        adapter_.on_transport_gap(GapReason::Disconnect,
                                  [this](const FeedEvent& ev) { on_event(ev); });
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

    AnvilAdapter adapter_;
    Book book_;
    SnapshotChannel channel_;
    DisplaySnapshot latest_{};
    DisplaySnapshot first_stale_{};

    std::vector<StaleEpisode> episodes_;
    std::size_t frames_ = 0;
    std::size_t events_ = 0;
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
        declared.qty_step};
}

ReplayResult run_replay(TraceReader& reader, const depthcharge::SymbolSpec& symbol,
                        const ReplayOptions& opts, const ReplayObserver& observer) {
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
