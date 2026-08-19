// dc_harness/replay_driver.cpp — see replay_driver.hpp for the design notes.
#include "dc_harness/replay_driver.hpp"

#include <algorithm>
#include <utility>

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
//
// A TEMPLATE FROM M4 STAGE B1, over the venue's decoder. The alternative was a
// virtual decoder interface, and it was refused for the reason ARCHITECTURE §4
// gives for the sink being a template: this object is the host stand-in for the
// firmware feed task, and a virtual call plus a heap-allocated decoder on that
// path is exactly what invariant #7 forbids on the board. One venue per build
// means the firmware instantiates one of these; the harness instantiates both,
// which costs a little code size on the desk and nothing where it matters.
template <typename Decoder>
class Replay {
public:
    Replay(Decoder decoder, const depthcharge::SymbolSpec& symbol,
           const ReplayOptions& opts, const ReplayObserver& observer)
        : opts_(opts), observer_(observer), decoder_(std::move(decoder)),
          book_(symbol, opts.window_policy,
                venue_traits(Decoder::kVenue).validated_depth) {}

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
        result.decoder = std::string(Decoder::name());
        result.threshold_ms = threshold_ms();
        result.worst_age_ms = age_.worst_ms();
        result.age_baseline_ms = age_.baseline_ms();
        result.liveness_median_ms = liveness_.median_ms();
        // The TOTAL, not the clock's 32-sample window: a report that says "32 x
        // summary" over a two-minute trace looks like a thin feed.
        result.liveness_arrivals = liveness_arrivals_;
        // `if constexpr` rather than an overload set: the two Stats types share
        // no base and never will, and the whole point of ReplayResult carrying
        // both is that exactly one is populated per run.
        if constexpr (Decoder::kVenue == Venue::Anvil) {
            result.adapter = decoder_.adapter().stats();
        } else {
            result.kraken = decoder_.adapter().stats();
        }
        result.book = book_.stats();
        result.episodes = episodes_;
        window_.policy = book_.window_policy();
        window_.validated_depth = venue_traits(Decoder::kVenue).validated_depth;
        result.window = window_;
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
        // This function owns the episode bookkeeping for its own gap, because a
        // watchdog gap is timed from the EXPIRY and not from a frame that never
        // arrived. The flag tells `on_event` to keep its hands off — see the
        // adapter-gap branch there.
        synthesising_watchdog_gap_ = true;
        decoder_.on_transport_gap(GapReason::Disconnect,
                                  [this](const FeedEvent& ev) { on_event(ev); });
        synthesising_watchdog_gap_ = false;

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

    // The window's per-frame figures, folded into the run's totals right after
    // the publish that produced them. Summed rather than sampled, because a
    // policy that drops levels on one frame in a hundred is exactly the thing a
    // final-frame reading cannot see.
    void note_window() noexcept {
        const auto& b = book_.bid_window();
        const auto& a = book_.ask_window();
        auto& w = window_;
        w.rows_filled += b.rows_filled + a.rows_filled;
        w.rows_unknown += b.rows_unknown + a.rows_unknown;
        w.levels_dropped += b.levels_dropped + a.levels_dropped;
        w.rows_validated += b.rows_validated + a.rows_validated;
        if (b.levels_dropped + a.levels_dropped > 0) { ++w.frames_with_drops; }
        w.worst_tick_span = std::max({w.worst_tick_span, b.tick_span, a.tick_span});
        w.final_bid = b;
        w.final_ask = a;
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

        if (ev.kind == FeedEvent::Kind::Gap) {
            const bool open = !episodes_.empty() && !episodes_.back().cleared;
            if (synthesising_watchdog_gap_) {
                if (open && episodes_.back().gap_seq == 0) {
                    episodes_.back().gap_seq = ev.seq;
                }
            } else if (!open) {
                // AN ADAPTER-ORIGINATED GAP OPENS AN EPISODE TOO (M4 stage B2).
                //
                // Until B2 every Gap in this harness came from the watchdog
                // above, so `episodes` and "windows the panel was grey for" were
                // the same list by accident. The Kraken adapter now raises three
                // of its own — `ChecksumFail` when the venue's CRC32 disagrees,
                // `Resync` when an unsubscribe ack says the venue has stopped
                // talking about this book, and `Disconnect` on a refused
                // subscribe — and every one of them greys the panel through
                // `Book::mark_stale` while leaving this list empty.
                //
                // That is an instrument pointed one inch to the left of the
                // thing it is named for: `StaleEpisode`'s own comment calls it
                // "the invariant-5 evidence the goldens assert on", and the
                // resync slice's entire purpose is a stale window it would have
                // reported as none. No committed golden moves, because no
                // committed slice yet contains an adapter-raised Gap — which is
                // also exactly why nothing caught it.
                //
                // `watchdog_rx_ns` is the arriving frame's own timestamp here,
                // not an expiry: this outage began when the message that proved
                // it arrived, so the grey window is measured from there.
                // `observed_gap_ms` stays 0, which is the true statement — no
                // silence caused this, a frame's CONTENT did.
                StaleEpisode ep;
                ep.reason = ev.reason;
                ep.frame_before = current_frame_;
                ep.watchdog_rx_ns = current_rx_ns_;
                ep.gap_events = 1;
                ep.gap_seq = ev.seq;
                episodes_.push_back(ep);
            } else {
                // Already grey, and told again — the same outage continuing, for
                // the same reason the watchdog folds its own repeats in.
                ++episodes_.back().gap_events;
            }
        }
        if (was_stale && !is_stale && !episodes_.empty() && !episodes_.back().cleared) {
            StaleEpisode& ep = episodes_.back();
            ep.cleared = true;
            ep.cleared_frame = current_frame_;
            ep.cleared_rx_ns = current_rx_ns_;
            ep.stale_ms = static_cast<double>(ep.cleared_rx_ns - ep.watchdog_rx_ns) / 1e6;
        }

        book_.publish(latest_);
        note_window();
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
    // Stage A shipped this as a seam with one filling; B1 is the stage that
    // proves a seam was the right shape, by filling it a second time without
    // moving it.
    Decoder decoder_;
    LivenessClock liveness_;
    AgeEstimator age_;
    Book book_;
    SnapshotChannel channel_;
    DisplaySnapshot latest_{};
    DisplaySnapshot first_stale_{};

    std::vector<StaleEpisode> episodes_;
    ReplayResult::WindowReport window_{};
    // True only while raise_watchdog_gap is driving the adapter, so on_event can
    // tell a gap this harness synthesised from one the adapter decided on.
    bool synthesising_watchdog_gap_ = false;
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
    switch (meta.venue) {
        case Venue::Anvil: {
            const auto& declared = depthcharge::anvil::kAnvilTicker101;
            return depthcharge::SymbolSpec{
                meta.ticker >= 0 ? static_cast<std::uint32_t>(meta.ticker) : declared.id,
                declared.price_decimals,
                declared.qty_decimals};
        }
        case Venue::Kraken: {
            depthcharge::kraken::SymbolConfig cfg{};
            if (!depthcharge::kraken::symbol_config_for(meta.symbol, cfg)) {
                throw TraceError(1, "this build declares no scale for Kraken symbol \"" +
                                        meta.symbol +
                                        "\" (engine/include/depthcharge/kraken/"
                                        "kraken_adapter.hpp). A guessed scale would not "
                                        "fail, it would draw a wrong ladder.");
            }
            return cfg.spec;
        }
    }
    return {};  // unreachable: venue_from_name rejects anything else
}

ReplayResult run_replay(TraceReader& reader, const depthcharge::SymbolSpec& symbol,
                        const ReplayOptions& opts, const ReplayObserver& observer) {
    // THE DRIVER FEEDS THE VENUE'S OWN ADAPTER (ARCHITECTURE §9, 2026-08-17),
    // and from M4 stage B1 there are two of them.
    //
    // The stage-A refusal that used to stand here is GONE, not relaxed: it threw
    // for any venue but Anvil, which was correct while Kraken's decoder emitted
    // nothing and is now simply the absence of the feature. What it was
    // protecting against — feeding Kraken's JSON to the Anvil parser and getting
    // a dead ladder over a 100% parse-error count — is protected instead by the
    // dispatch below plus `ReplayResult::decoder`, which makes the mistake
    // VISIBLE in every pinned output rather than merely impossible today.
    //
    // Two things went live the moment it came off, and both are fixed in this
    // same commit rather than left as loud defects: `symbol_for` returned
    // Anvil's scale for every venue (above), and the console ladder printed a
    // hardcoded " ANVIL " and a raw step count for every quantity
    // (console_ladder.cpp). The triage put them here for exactly this reason —
    // "that is when they break".
    switch (reader.venue()) {
        case Venue::Anvil: {
            Replay<AnvilTraceDecoder> replay(AnvilTraceDecoder(symbol), symbol, opts,
                                             observer);
            return replay.run(reader);
        }
        case Venue::Kraken: {
            const TraceMeta& m = reader.meta();
            // The subscribed depth is a property of the CAPTURE, not of the
            // build: the committed slices run at 10, 25 and 100, and a
            // compile-time depth would mean three of the five goldens could only
            // be run by rebuilding. A trace that recorded none falls back to the
            // firmware's constant.
            const std::int32_t depth =
                m.depth > 0 ? static_cast<std::int32_t>(m.depth)
                            : depthcharge::kraken::kKrakenSubscribeDepth;
            // The wire symbol is taken from the VENUE TABLE, not from the trace's
            // metadata string, and the difference is lifetime rather than value:
            // the table's entries are `inline constexpr` literals with static
            // storage, and `KrakenTraceDecoder` borrows the view (see the note on
            // its constructor). Resolving it here also means a capture of a
            // symbol this build declares no scale for is refused at the same
            // place and with the same message as `symbol_for`.
            depthcharge::kraken::SymbolConfig cfg{};
            if (!depthcharge::kraken::symbol_config_for(m.symbol, cfg)) {
                throw TraceError(reader.name(), 1,
                                 "this build declares no scale for Kraken symbol \"" +
                                     m.symbol + "\"");
            }
            Replay<KrakenTraceDecoder> replay(
                KrakenTraceDecoder(symbol, cfg.wire_symbol, depth), symbol, opts, observer);
            return replay.run(reader);
        }
    }
    throw TraceError(reader.name(), 1, "trace declares a venue this build cannot replay");
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
