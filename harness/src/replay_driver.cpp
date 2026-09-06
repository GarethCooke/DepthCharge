// dc_harness/replay_driver.cpp — see replay_driver.hpp for the design notes.
#include "dc_harness/replay_driver.hpp"

#include <algorithm>
#include <utility>

#include <depthcharge/feed_event.hpp>

#include <depthcharge/age_estimator.hpp>
#include <depthcharge/liveness_clock.hpp>
#include "dc_harness/trace_decoder.hpp"

namespace dc::harness {
namespace {

using depthcharge::AgeEstimator;
using depthcharge::AgeReading;
using depthcharge::Book;
using depthcharge::DisplaySnapshot;
using depthcharge::FeedEvent;
using depthcharge::FeedStatus;
using depthcharge::GapReason;
using depthcharge::LivenessClock;
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
          // The clamp the grey threshold is computed with, from the same table
          // the book takes `validated_depth` from. The THRESHOLD is still
          // calibrated from the signal's own median and is still never a
          // constant in that table (M5 stage C; the 2026-08-17 ruling stands).
          liveness_(venue_traits(Decoder::kVenue).liveness),
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

        TraceRecord frame;
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
                // STAMPED FROM `event_ns`, AND THIS LINE WAS WRONG FROM M5 STAGE
                // A UNTIL THE SILENT-STREAM FIXTURE FOUND IT.
                //
                // Stage A moved the statistics pass (trace.cpp's accumulate) to
                // `event_ns` and wrote the rule into ARCHITECTURE §9 as "only
                // the liveness clock reads it" — and then changed one of the TWO
                // liveness clocks. This is the other one, and it is the one that
                // matters more: `accumulate` produces a report, while this
                // decides `threshold_ms()` and therefore when the panel greys.
                //
                // That is ARCHITECTURE §9's oldest drift shape (2026-08-07:
                // `read_trace()` and `TraceReader` written separately, drifted,
                // and gave two answers to one question) recurring inside the
                // stage that quoted it. Nothing caught it because at Anvil and
                // Kraken `event_ns == rx_ns`, so the two implementations agreed
                // on every committed trace that existed — the coincidence class,
                // again.
                //
                // What surfaced it: a capture in which three pings 20 s apart
                // share ONE `rx_ns`, because a silent stream means the main loop
                // never flushes until the end. Read from `rx_ns` this clock sees
                // three arrivals 0 ms apart and reports a median of zero.
                liveness_.on_liveness(frame.event_ns);
                // THE AGE ESTIMATOR DELIBERATELY STAYS ON `rx_ns`, and that is a
                // limitation rather than an oversight. It reads its deficit
                // against `current_rx_ns_` in `stamp_age`, so feeding it
                // `event_ns` arrivals would mix two clocks in one subtraction and
                // can go negative. The honest note is larger than the mechanism:
                // `age_ms` is the book's queuing lag measured against the venue's
                // liveness signal, and at Binance that signal is a TRANSPORT ping
                // with no relationship to the book's queue at all — so the
                // quantity is questionable at this venue whichever stamp feeds
                // it. That belongs with C's threshold work (ARCHITECTURE §9,
                // 2026-08-25, the transport-versus-feed row), not here.
                age_.on_liveness(frame.rx_ns);
            }

            current_frame_ = frame.index;
            current_rx_ns_ = frame.rx_ns;
            decoder_.decode(frame, [this](const FeedEvent& ev) { on_event(ev); });
            // THE MESSAGE BOUNDARY, AND IT NEEDED NO NEW SEAM (M5 stage E): it
            // is the return of the decode call, and it always was.
            publish_message();
            // AFTER the publish, so the frame that carried the request is drawn
            // with `Wanted` and the fetch shows as `InFlight` from the next one.
            // That is the board's order too: `FeedTask` services the seed once
            // per loop pass, outside the message it just handled.
            service_reseed();
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
            stamp_reseed(latest_);
        }

        result.frames = frames_;
        result.events = events_;
        result.decoder = std::string(Decoder::name());
        result.threshold_ms = threshold_ms();
        result.worst_age_ms = age_.worst_ms();
        result.age_baseline_ms = age_.baseline_ms();
        result.liveness_median_ms = liveness_.median_ms();
        result.liveness_calibrated = liveness_.calibrated();
        // The TOTAL, not the clock's 32-sample window: a report that says "32 x
        // summary" over a two-minute trace looks like a thin feed.
        result.liveness_arrivals = liveness_arrivals_;
        // `if constexpr` rather than an overload set: the two Stats types share
        // no base and never will, and the whole point of ReplayResult carrying
        // both is that exactly one is populated per run.
        //
        // DELIBERATELY NOT WIDENED TO THREE AT M5 STAGE A. Binance has no
        // adapter, so `Replay<BinanceTraceDecoder>` is never instantiated —
        // `run_replay` refuses that venue by name. If B1 forgets to add its
        // branch here, the `else` below asks a decoder with no `adapter()` for
        // one and the build fails, which is the failure this stage wants.
        // Adding an empty third branch now would replace that with silence.
        if constexpr (Decoder::kVenue == Venue::Anvil) {
            result.adapter = decoder_.adapter().stats();
        } else if constexpr (Decoder::kVenue == Venue::Kraken) {
            result.kraken = decoder_.adapter().stats();
        } else {
            result.binance = decoder_.adapter().stats();
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
        // A synthesised gap is a message too — one the transport wrote rather
        // than the venue — so it publishes at its own boundary and BEFORE the
        // age reset below, which the next paragraph depends on.
        publish_message();

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

    // WHETHER A FRESH BASELINE IS OUTSTANDING (M5 stage C, DESIGN strain 28).
    //
    // Stamped beside the age and for the same reason: the adapter latches a
    // REQUEST it cannot serve — it has no clock and no socket — and the layer
    // that could is the feed side, which is this object on the desk and
    // `FeedTask` on the board. So the request is `Wanted` here and can only
    // become `InFlight` where a fetch is actually issued.
    //
    // **`InFlight` IS REACHED SINCE M5 STAGE D-A4, AND IT IS THE ADAPTER'S HOLD
    // THAT SAYS SO — not a fetch this object models.** That is the whole reason
    // the state is reachable on a desk with no socket: `reseed_holding()` is a
    // fact about the adapter (it is keeping the interval a body will be rolled
    // forward across), so the host and the board answer the same question with
    // the same expression. `FeedTask::publish_current` carries the identical
    // three-way choice, and a divergence between them would be a state no golden
    // could pin.
    //
    // The ORDER matters and is not arbitrary: a hold is open only after
    // `on_reseed_issued()` cleared `reseed_wanted_`, so the two are already
    // mutually exclusive — testing `InFlight` first states the precedence
    // rather than relying on it.
    //
    // BINANCE ONLY, DELIBERATELY. Kraken latches `resync_wanted()` too, and it
    // is NOT the same question: that is a re-SUBSCRIBE, it is already served by
    // B2's healing path, and its rendering question was settled at M4. Giving
    // two venues one vocabulary on the strength of one card's need is how a
    // venue fact becomes a universal rule — which is the mistake `venue.hpp`
    // exists to record.
    void stamp_reseed(DisplaySnapshot& snap) noexcept {
        if constexpr (Decoder::kVenue == Venue::Binance) {
            // `reseed_state_for` (display_snapshot.hpp) and not a second copy of
            // the ternary: `FeedTask::publish_current` calls the identical
            // function, so the board and this driver cannot drift apart on what
            // a given adapter state publishes.
            snap.reseed = depthcharge::reseed_state_for(decoder_.adapter().reseed_holding(),
                                                        decoder_.adapter().reseed_wanted());
        } else {
            snap.reseed = depthcharge::ReseedState::None;
        }
    }

    // ISSUE THE FETCH THE TRACE CANNOT ISSUE FOR ITSELF (M5 stage D-A4).
    //
    // On the board `SeedSchedule` decides when to ask and `SeedTask` asks; this
    // object has neither, and it must not grow them — the schedule is arithmetic
    // that is already host-tested on its own (`test_seed_schedule.cpp`), and a
    // second copy of it here would be a second answer to the same question.
    //
    // **OFF BY DEFAULT, AND THE DEFAULT IS WHAT KEEPS EVERY EXISTING GOLDEN
    // STILL.** A captured trace's REST bodies were fetched by
    // `tools/capture_binance.py` on its own 15 s cadence, not in answer to
    // anything this adapter wanted, so a driver that opened a hold on every
    // capture would be modelling a fetch that did not happen and re-baselining
    // books off bodies that had no relationship to the request. With the flag
    // clear, a mid-stream body is declined exactly as it was before this stage
    // and the eleven committed Binance replay tests do not move.
    //
    // With it set, this reproduces the board's rule and nothing more: the
    // adapter wants a re-seed, the book is live, no fetch is outstanding — so
    // one is issued. The synthesised trace that exercises the mechanism turns it
    // on; see `reseed_trace()` in `test_binance_adapter.cpp`, which builds it in
    // memory rather than committing a file — what is pinned is the transition,
    // not a capture.
    void service_reseed() noexcept {
        if constexpr (Decoder::kVenue == Venue::Binance) {
            if (!opts_.issue_reseed_fetch) { return; }
            auto& a = decoder_.adapter();
            if (a.reseed_wanted() && a.has_baseline() && !a.reseed_holding()) {
                a.on_reseed_issued();
            }
        }
    }

    // ONE PUBLISH PER MESSAGE (M5 stage E), and the whole of the change on this
    // side of the boundary.
    //
    // WHY THE PANEL WAS DRAWING A BOOK THAT CANNOT EXIST. One `depthUpdate`
    // becomes N single-side `Delta` events, and a publish after each of them
    // samples the book between the bid levels that lift the touch and the ask
    // removals in the same message that pay for them. 11,062 crossed publishes
    // across the committed Binance corpus, and 1,066 crossed LIVE ladder lines
    // of 35,177 (3.03%) in the 34.5 h soak — quoted as 1,032 until 2026-09-05,
    // which is the same run counted as `bid > ask` rather than as `Book::publish`'s
    // own `>=`, and so misses the 34 LOCKED frames.
    // The message is the smallest unit at which the venue
    // has told us something whole, so it is the unit the panel is entitled to
    // see.
    //
    // AND IT IS NOT A BINANCE FACT. Kraken emits one `Delta` per level too
    // (`kraken_adapter.hpp`, `emit_delta`), so it had the same exposure all
    // along and the committed captures simply never straddled the touch —
    // measured, stage E §2. Anvil is the only venue where this is a no-op,
    // because one frame there yields one whole-book Snapshot.
    //
    // CONDITIONAL ON THE MESSAGE HAVING SAID ANYTHING. A frame that emits no
    // event — Anvil's summary, a heartbeat, an ack — publishes nothing, exactly
    // as before. Publishing unconditionally per decode call would take Anvil
    // from 1,225 publishes to 1,406 and move every Anvil golden, for a frame
    // whose content the book never saw.
    void publish_message() {
        if (!pending_publish_) { return; }
        pending_publish_ = false;

        book_.publish(latest_);
        note_window();
        stamp_age(latest_);
        stamp_reseed(latest_);
        channel_.publish(latest_);

        // §3: the frame the panel would actually have shown at the moment it
        // went grey is this one — the publish that follows the message
        // containing the Gap — not the book as it stood part-way through it.
        if (first_stale_pending_) {
            first_stale_ = latest_;
            saw_stale_ = true;
            first_stale_pending_ = false;
        }

        if (observer_) {
            ReplayStep step;
            step.frame_index = current_frame_;
            step.event_index = events_;
            step.rx_ns = current_rx_ns_;
            // The last event of the message, because that is the one this frame
            // shows. A step was a FeedEvent before this stage and is a PUBLISH
            // now, which is the honest reading of a callback that is handed a
            // rendered snapshot: it fires when there is a new frame to render.
            step.kind = last_kind_;

            // The render side takes the published frame the way the M3 render
            // task will — through the channel, by copy — so the seam is
            // exercised by every replay, not just documented.
            DisplaySnapshot rendered{};
            if (!channel_.consume(rendered)) { rendered = latest_; }
            if (!observer_(step, rendered)) { stopped_ = true; }
        }
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

        // THE PUBLISH USED TO BE HERE, AND THAT WAS THE DEFECT (M5 stage E).
        // What is left is the message's own bookkeeping; the frame the panel
        // sees is built once, after the last event of the message, by
        // `publish_message` above.
        pending_publish_ = true;
        last_kind_ = ev.kind;

        // The first-stale capture becomes a REQUEST here and is served at the
        // boundary — §3 of the stage brief, and the one place this change is not
        // a refactor. Capturing `latest_` on this line would record the previous
        // message's book, because this message has not published yet.
        if (is_stale && !saw_stale_ && ev.kind == FeedEvent::Kind::Gap) {
            first_stale_pending_ = true;
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
    // Set by any event, cleared by the publish at the end of its message. This
    // is what makes the publish conditional rather than per decode call.
    bool pending_publish_ = false;
    bool first_stale_pending_ = false;
    FeedEvent::Kind last_kind_{};
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
        case Venue::Binance: {
            // B1 BROUGHT THE ADAPTER AND ITS SYMBOL TABLE, so the refusal stage A
            // stood here is gone the way Kraken's was: not relaxed, simply the
            // absence of the feature it was protecting against.
            //
            // The scale is a VENUE constant of 8 decimals rather than a
            // per-symbol one, which is this venue's inversion of §4's
            // declare-and-verify rule — see binance_adapter.hpp, where the
            // declarations and the corpus GCDs that verify them live together.
            depthcharge::binance::SymbolConfig cfg{};
            if (!depthcharge::binance::symbol_config_for(meta.symbol, cfg)) {
                throw TraceError(1, "this build declares no scale for Binance symbol \"" +
                                        meta.symbol +
                                        "\" (engine/include/depthcharge/binance/"
                                        "binance_adapter.hpp). A guessed scale would not "
                                        "fail, it would draw a wrong ladder.");
            }
            return cfg.spec;
        }
    }
    unhandled_venue(meta.venue, "symbol_for");
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
        case Venue::Binance: {
            // The refusal stage A stood here is gone at B1, exactly as Kraken's
            // was at M4 B1 — and `ReplayResult::decoder` is what keeps the
            // dispatch honest now that it is silent rather than loud.
            depthcharge::binance::SymbolConfig cfg{};
            if (!depthcharge::binance::symbol_config_for(reader.meta().symbol, cfg)) {
                throw TraceError(reader.name(), 1,
                                 "this build declares no scale for Binance symbol \"" +
                                     reader.meta().symbol + "\"");
            }
            Replay<BinanceTraceDecoder> replay(BinanceTraceDecoder(cfg), symbol, opts,
                                               observer);
            return replay.run(reader);
        }
    }
    unhandled_venue(reader.venue(), "run_replay");
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
