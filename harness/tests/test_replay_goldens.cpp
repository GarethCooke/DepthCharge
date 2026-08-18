// test_replay_goldens.cpp — M1's definition of done, in assertions.
//
// Both committed traces are driven through the whole chain (verbatim frame ->
// adapter -> phase-1 book -> DisplaySnapshot) and pinned. The expected values
// were derived independently from the trace files rather than recorded from
// this code's own output, so a regression in the engine shows up as a
// disagreement with the capture, not as a golden that quietly moved.
//
// The headline assertion is the reconnect one: a Gap appears at the watchdog,
// the panel is stale for the duration of the outage, and only the resync
// Snapshot clears it. That is the executable form of invariant #5.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/feed_event.hpp>

#include "dc_harness/age_estimator.hpp"
#include "dc_harness/console_ladder.hpp"
#include "dc_harness/replay_driver.hpp"
#include "dc_harness/trace.hpp"

using depthcharge::FeedEvent;
using depthcharge::FeedStatus;
using depthcharge::GapReason;
using depthcharge::Side;
using depthcharge::anvil::kAnvilTicker101;
using dc::harness::ReplayOptions;

// EVERY GOLDEN IN THIS FILE PINS THE M1 RULE EXPLICITLY, via
// `ReplayOptions::legacy_anvil()` (1,000 ms, record-arrival armed) — WITH ONE
// DELIBERATE EXCEPTION, which is the point of the exception: see
// "reconnect trace: THE CALIBRATED PATH" below. Invariant #6 has no clause
// exempting new behaviour that was merely reported in a session log, so the
// shipped default path carries a golden of its own.
//
// Since the 2026-08-17 ruling the driver's DEFAULT is to calibrate its threshold
// from the venue's liveness signal, which at Anvil settles at 4 x 500 ms =
// 2,000 ms. That is the right default and it is deliberately not what these
// tests use: a golden whose expectations depend on a calibration would move the
// day a trace is re-captured with slightly different cadence, and these
// expectations are pinned M1/M3 measurements. Concretely, the reconnect trace's
// grey window is `cleared_rx - (prev_rx + threshold)`, so calibrating would
// shorten the pinned 3,468 ms to 2,468 ms — a moved golden, for no finding.
//
// One trace therefore carries TWO pins, and the pair documents the ruling better
// than either alone: the same 4,468 ms hole, judged by the constant it used to be
// judged by and by the rule that replaced it, with the only difference being how
// much of the hole is grey.
using dc::harness::ReplayResult;
using dc::harness::run_replay_file;
using dc::harness::run_replay_text;

namespace {

// One rule for turning a trace's name into its path, so a new case cannot spell
// the directory a fourth way.
std::string trace_path(std::string_view name) {
    return std::string(DC_REPLAY_DIR) + "/" + std::string(name) + ".ndjson";
}

std::string baseline_path() {
    return trace_path("anvil_101_baseline");
}
std::string reconnect_path() {
    return trace_path("anvil_101_reconnect");
}
// The 2026-08 re-measurement of Anvil's cadence (M3). See NOTES.md's M3 addendum
// for why a second healthy trace exists rather than the first one being replaced:
// the M1 trace is what the goldens above pin, and it is kept exactly as it was.
std::string baseline_2026_08_path() {
    return trace_path("anvil_101_baseline_20260809");
}

ReplayResult replay(const std::string& path, std::size_t max_frames = 0) {
    ReplayOptions opts = ReplayOptions::legacy_anvil();
    opts.max_frames = max_frames;
    return run_replay_file(path, kAnvilTicker101, opts);
}

}  // namespace

TEST_CASE("baseline trace: every frame is consumed and nothing is malformed") {
    const ReplayResult r = replay(baseline_path());

    CHECK(r.frames == 1406);
    CHECK(r.events == 1225);  // 1089 snapshots + 136 trades, no gaps

    CHECK(r.adapter.snapshot_frames == 1);
    CHECK(r.adapter.book_frames == 1088);
    CHECK(r.adapter.trade_frames == 136);
    CHECK(r.adapter.summary_ignored == 181);
    CHECK(r.adapter.events_out == 1225);

    // Nothing in 90 s of live capture confuses the adapter.
    CHECK(r.adapter.parse_errors == 0);
    CHECK(r.adapter.price_errors == 0);   // 10^-4 holds every price on this wire
    CHECK(r.adapter.other_ticker == 0);
    CHECK(r.adapter.unknown_kind == 0);
    CHECK(r.adapter.truncated_frames == 0);  // <= 126 levels/side, cap is 256
    CHECK(r.adapter.transport_gaps == 0);

    CHECK(r.book.snapshots_adopted == 1089);
    CHECK(r.book.trades_applied == 136);
    CHECK(r.book.gaps == 0);
    // Anvil emits no Deltas at all -- `book` and `snapshot` are both full
    // replaces on this wire. Kept as a zero rather than deleted with the old
    // `deltas_rejected` counter: it is the assertion that the venue whose
    // adapter did NOT change still produces no deltas after the book learned to
    // apply them, which is the property this commit could most easily break.
    CHECK(r.book.deltas_applied == 0);
    CHECK(r.book.deltas_absent == 0);
}

TEST_CASE("baseline trace: the wire seq misbehaves and the synthesised Seq does not") {
    std::vector<depthcharge::Seq> seqs;
    std::size_t gaps = 0;
    ReplayOptions opts = ReplayOptions::legacy_anvil();
    dc::harness::TraceReader reader(baseline_path());
    const ReplayResult r = dc::harness::run_replay(
        reader, kAnvilTicker101, opts,
        [&](const dc::harness::ReplayStep& step,
            const depthcharge::DisplaySnapshot& snap) -> bool {
            seqs.push_back(snap.seq);  // the synthesised Seq the book just applied
            if (step.kind == FeedEvent::Kind::Gap) { ++gaps; }
            return true;
        });

    // The M0 finding, re-measured by a completely different code path than
    // harness/src/trace.cpp: 14 backward steps in the committed slice.
    CHECK(r.adapter.wire_seq_backward == 14);

    // ...and the boundary Seq the engine actually sees is dense and monotonic.
    //
    // std::ranges::mismatch rather than ranges::equal: equal returns a bare
    // bool, which on a golden pinning 1225 synthesised Seqs would say only that
    // something was wrong. This names the first index that diverged and both
    // values, the way the per-element loop it replaces did — in one assertion
    // instead of 1225.
    REQUIRE(seqs.size() == 1225);
    const auto dense = std::views::iota(depthcharge::Seq{1}, depthcharge::Seq{1226});
    const auto [got, want] = std::ranges::mismatch(seqs, dense);
    if (got != seqs.end()) {
        const std::string at = "first divergence at index " +
                               std::to_string(got - seqs.begin()) + ": got " +
                               std::to_string(*got) + ", want " + std::to_string(*want);
        INFO(at);
    }
    CHECK(got == seqs.end());
    CHECK(gaps == 0);  // a non-monotonic wire seq never becomes a Gap
}

TEST_CASE("baseline trace: top of book at the on-connect snapshot") {
    const ReplayResult r = replay(baseline_path(), /*max_frames=*/1);
    const auto& s = r.final_snapshot;

    CHECK(r.events == 1);
    CHECK(s.live());
    CHECK(s.symbol.id == 101);
    CHECK(s.symbol.price_decimals == 4);
    CHECK(s.best_bid() == 99972);     // "9.9972"
    CHECK(s.bids[0].qty == 9);
    CHECK(s.best_ask() == 99979);     // "9.9979"
    CHECK(s.asks[0].qty == 44);
    CHECK(s.spread_ticks() == 7);
    CHECK(s.bid_count == depthcharge::kDisplayLevels);  // 115 levels -> top 27
    CHECK(s.ask_count == depthcharge::kDisplayLevels);
    CHECK_FALSE(s.has_last);          // no print has crossed yet
    CHECK(s.trade_count == 0);
}

TEST_CASE("baseline trace: mid-trace checkpoint after 700 frames") {
    const ReplayResult r = replay(baseline_path(), /*max_frames=*/700);
    const auto& s = r.final_snapshot;

    CHECK(r.events == 609);
    CHECK(s.live());
    CHECK(s.best_bid() == 100243);
    CHECK(s.bids[0].qty == 73);
    CHECK(s.best_ask() == 100244);
    CHECK(s.asks[0].qty == 158);
    CHECK(s.spread_ticks() == 1);
    CHECK(s.has_last);
    CHECK(s.last_px == 100242);
    CHECK(s.trade_count == depthcharge::kTradeRingSize);
}

TEST_CASE("baseline trace: final book and the whole trade ring") {
    const ReplayResult r = replay(baseline_path());
    const auto& s = r.final_snapshot;

    CHECK(s.live());
    CHECK(s.status == FeedStatus::Live);
    CHECK(s.best_bid() == 100037);
    CHECK(s.bids[0].qty == 33);
    CHECK(s.best_ask() == 100077);
    CHECK(s.asks[0].qty == 117);
    CHECK(s.last_px == 100037);
    CHECK(s.seq == 1225);

    // Newest first. Sides: "S" (sell aggressor) -> Ask, "B" -> Bid.
    struct Print { depthcharge::PriceTicks px; depthcharge::Qty qty; Side side; };
    const Print expected[] = {
        {100037, 9, Side::Ask},  {100039, 26, Side::Ask}, {100045, 11, Side::Ask},
        {100045, 4, Side::Ask},  {100075, 9, Side::Ask},  {100129, 5, Side::Bid},
        {100129, 25, Side::Bid}, {100127, 22, Side::Bid},
    };
    REQUIRE(s.trade_count == depthcharge::kTradeRingSize);
    for (std::size_t i = 0; i < depthcharge::kTradeRingSize; ++i) {
        CHECK(s.trades[i].px == expected[i].px);
        CHECK(s.trades[i].qty == expected[i].qty);
        CHECK(s.trades[i].aggressor == expected[i].side);
    }

    // Prints are strictly older as you walk down the tape. is_sorted_until
    // rather than is_sorted: the same call, but it names the position where the
    // ordering broke instead of returning a bare false.
    const auto* inversion =
        std::ranges::is_sorted_until(s.trades, std::greater{}, &depthcharge::TradePrint::seq);
    if (inversion != std::end(s.trades)) {
        const std::string at = "tape ordering breaks at position " +
                               std::to_string(inversion - std::begin(s.trades));
        INFO(at);
    }
    CHECK(inversion == std::end(s.trades));
}

TEST_CASE("baseline trace: the ladder never goes stale on a healthy feed") {
    std::size_t stale_publishes = 0;
    ReplayOptions opts = ReplayOptions::legacy_anvil();
    dc::harness::TraceReader reader(baseline_path());
    const ReplayResult r = dc::harness::run_replay(
        reader, kAnvilTicker101, opts,
        [&](const dc::harness::ReplayStep&, const depthcharge::DisplaySnapshot& snap) -> bool {
            if (!snap.live()) { ++stale_publishes; }
            return true;
        });

    CHECK(r.episodes.empty());
    CHECK_FALSE(r.saw_stale);
    CHECK(stale_publishes == 0);
}

// --- the watchdog threshold, re-measured against the live server -------------

// M3 asked whether `disconnect_gap_ms = 1000` had been outlived: the board was
// greying on a live socket, and the theory was that Anvil had slowed from M0's
// 15.5 frames/s to ~6, stretching healthy silences past the threshold. A fresh
// 20-minute capture says otherwise — 17.02 frames/s, worst gap 391 ms — so the
// premise was wrong and the constant did not move (ARCHITECTURE §9, 2026-08-09).
//
// This is that measurement made executable. It is the guard the original
// derivation never had: if Anvil's cadence ever really does stretch, this goes
// red on a trace rather than on a panel, which is the whole point of invariant
// #6. The numbers come from tools/gap_stats.py over the committed slice, not
// from this code's output.
TEST_CASE("2026-08 capture: Anvil's cadence still clears the watchdog by 6x") {
    std::vector<std::int64_t> event_rx;
    ReplayOptions opts = ReplayOptions::legacy_anvil();
    dc::harness::TraceReader reader(baseline_2026_08_path());
    const ReplayResult r = dc::harness::run_replay(
        reader, kAnvilTicker101, opts,
        [&](const dc::harness::ReplayStep& step,
            const depthcharge::DisplaySnapshot&) -> bool {
            event_rx.push_back(step.rx_ns);
            return true;
        });

    CHECK(r.frames == 1513);
    CHECK(r.events == 1332);  // 1211 snapshots + 121 trades, no gaps

    CHECK(r.adapter.snapshot_frames == 1);
    CHECK(r.adapter.book_frames == 1210);
    CHECK(r.adapter.trade_frames == 121);
    CHECK(r.adapter.summary_ignored == 181);
    CHECK(r.adapter.parse_errors == 0);
    CHECK(r.adapter.price_errors == 0);   // 10^-4 still holds every price
    CHECK(r.adapter.truncated_frames == 0);

    // 90.0 s of capture at 16.81 frames/s — Anvil is if anything faster than the
    // 15.5 M0 measured, not slower.
    CHECK(r.span_seconds() > 89.9);
    CHECK(r.span_seconds() < 90.1);

    // THE ASSERTION THAT MATTERS. Not one hole in the stream comes close to the
    // threshold, so a healthy feed cannot grey the panel.
    CHECK(r.episodes.empty());
    CHECK_FALSE(r.saw_stale);
    CHECK(r.final_snapshot.live());

    // ...and by how much, so a slow erosion is visible rather than only a breach.
    // 156 ms against a 1000 ms threshold: 6.4x of margin.
    REQUIRE(event_rx.size() > 2);
    double worst_gap_ms = 0.0;
    for (std::size_t i = 1; i < event_rx.size(); ++i) {
        worst_gap_ms = std::max(
            worst_gap_ms, static_cast<double>(event_rx[i] - event_rx[i - 1]) / 1e6);
    }
    CHECK(worst_gap_ms > 155.0);
    CHECK(worst_gap_ms < 157.0);
    CHECK(worst_gap_ms < opts.disconnect_gap_ms / 6.0);
}

// --- the invariant-5 proof ---------------------------------------------------

TEST_CASE("reconnect trace: the panel goes stale at the outage and recovers on resync") {
    const ReplayResult r = replay(reconnect_path());

    CHECK(r.frames == 1288);
    CHECK(r.events == 1117);  // 1013 snapshots + 103 trades + 1 gap
    CHECK(r.adapter.snapshot_frames == 1);
    CHECK(r.adapter.book_frames == 1012);
    CHECK(r.adapter.trade_frames == 103);
    CHECK(r.adapter.summary_ignored == 172);
    CHECK(r.adapter.parse_errors == 0);
    CHECK(r.adapter.price_errors == 0);

    REQUIRE(r.episodes.size() == 1);
    const auto& ep = r.episodes.front();
    CHECK(ep.reason == GapReason::Disconnect);
    CHECK(ep.frame_before == 382);      // last frame before the hole
    CHECK(ep.cleared);
    CHECK(ep.cleared_frame == 383);     // the resync snapshot
    CHECK(ep.gap_seq == 332);           // synthesised Seq of the Gap event
    CHECK(ep.observed_gap_ms > 4468.0);
    CHECK(ep.observed_gap_ms < 4469.0);
    // The watchdog fires 1000 ms into the silence, so the panel would have been
    // grey for the remaining ~3.47 s — not for an instant between two frames.
    CHECK(ep.stale_ms > 3468.0);
    CHECK(ep.stale_ms < 3469.0);

    CHECK(r.book.gaps == 1);
    CHECK(r.book.snapshots_adopted == 1013);

    // The stale frame still carries the last known book — greyed, not blanked.
    CHECK(r.saw_stale);
    CHECK(r.first_stale_snapshot.status == FeedStatus::Stale);
    CHECK(r.first_stale_snapshot.stale_reason == GapReason::Disconnect);
    CHECK(r.first_stale_snapshot.bid_count == depthcharge::kDisplayLevels);
    CHECK(r.first_stale_snapshot.has_top());

    // ...and the trace ends live again, on the far side of the reconnect.
    CHECK(r.final_snapshot.live());
    CHECK(r.final_snapshot.best_bid() == 100103);
    CHECK(r.final_snapshot.bids[0].qty == 22);
    CHECK(r.final_snapshot.best_ask() == 100104);
    CHECK(r.final_snapshot.asks[0].qty == 51);
    CHECK(r.final_snapshot.last_px == 100105);
    CHECK(r.final_snapshot.seq == 1117);
}

// --- the same outage, on the SHIPPED DEFAULT path ---------------------------
//
// This is the only golden in the file that runs the driver as it actually ships:
// `ReplayOptions{}`, threshold 0, meaning "calibrate from the venue's liveness
// signal". Everything else pins `legacy_anvil()` so that a re-capture cannot move
// it, and that was the right trade fourteen times — but it left the default path
// with no replay coverage at all, and invariant #6 does not have an exemption for
// behaviour that was reported rather than covered.
//
// What it pins, and why these numbers: Anvil's `summary` runs at a 499.9 ms
// median in this trace, so `kThresholdMultiple = 4` calibrates to 2,000 ms
// against the 1,000 ms the case above uses. The hole is the same 4,468 ms, the
// episode count is the same 1, and the ONE thing that moves is how much of the
// hole is grey: 4,468 - 2,000 = 2,469 ms rather than 3,468 ms.
//
// The calibration is not a free variable here. It is 4 x a median this trace
// establishes 172 times, so if Anvil's cadence ever changed, this golden and the
// legacy one above would move in opposite directions — the legacy grey window
// would stretch while this one held at 4x the new median. That divergence is
// worth more than either number.
TEST_CASE("reconnect trace: THE CALIBRATED PATH greys for 2,469 ms, not 3,468") {
    const ReplayResult r = run_replay_file(reconnect_path(), kAnvilTicker101,
                                           ReplayOptions{});

    // The threshold was derived, not passed in.
    CHECK(r.threshold_ms == doctest::Approx(2000.0).epsilon(0.001));

    // Everything the adapter sees is identical to the legacy run — the ruling
    // changed when the panel greys, not what the book is told.
    CHECK(r.frames == 1288);
    CHECK(r.events == 1117);
    CHECK(r.adapter.snapshot_frames == 1);
    CHECK(r.adapter.book_frames == 1012);
    CHECK(r.adapter.trade_frames == 103);
    CHECK(r.adapter.summary_ignored == 172);
    CHECK(r.book.gaps == 1);
    CHECK(r.book.snapshots_adopted == 1013);

    // One episode, same hole, same frames either side.
    REQUIRE(r.episodes.size() == 1);
    const auto& ep = r.episodes.front();
    CHECK(ep.reason == GapReason::Disconnect);
    CHECK(ep.frame_before == 382);
    CHECK(ep.cleared);
    CHECK(ep.cleared_frame == 383);
    CHECK(ep.observed_gap_ms > 4468.0);
    CHECK(ep.observed_gap_ms < 4469.0);

    // THE ASSERTION THAT DISTINGUISHES THIS FROM THE CASE ABOVE.
    CHECK(ep.stale_ms > 2468.0);
    CHECK(ep.stale_ms < 2470.0);

    // And the invariant-5 half still holds: greyed, not blanked, then live again.
    CHECK(r.saw_stale);
    CHECK(r.first_stale_snapshot.status == FeedStatus::Stale);
    CHECK(r.first_stale_snapshot.bid_count == depthcharge::kDisplayLevels);
    CHECK(r.final_snapshot.live());
}

// ---------------------------------------------------------------------------
// THE AGE METER (M4 stage A2)
//
// What these can and cannot prove is the whole reason they are worded carefully.
// A committed trace CANNOT carry a true queuing lag — a backlog belongs to one
// client's socket, and a capture's rx_ns records only when that client got the
// bytes — so the arithmetic is proven against synthesised patterns in
// test_age_estimator.cpp. What the traces prove is the other half, and it is not
// nothing: **on four healthy Anvil captures the meter reads its noise floor, and
// on the one trace with a real outage it does not.** A meter that read minutes
// on a healthy feed would be as useless as one that read zero on a stalled one.
// ---------------------------------------------------------------------------

TEST_CASE("age meter: four healthy Anvil traces read the noise floor, not minutes") {
    struct Case {
        const char* name;
        std::string path;
        std::uint32_t worst_lo, worst_hi;   // ms, from the rendered tenths
    };
    const Case cases[] = {
        {"M1 baseline", baseline_path(), 900, 1000},
        {"2026-08-09 baseline", baseline_2026_08_path(), 500, 600},
        {"2026-08-16 depth 27", trace_path("anvil_101_depth27_20260816"), 600, 700},
        {"2026-08-17 feeder off", trace_path("anvil_101_feederoff_20260817"), 500, 600},
    };

    for (const Case& c : cases) {
        CAPTURE(c.name);
        const ReplayResult r = replay(c.path);

        // The baseline latched at Anvil's broadcast cadence, from the trace's
        // own first window rather than from any constant in this repo.
        CHECK(r.age_baseline_ms > 490.0);
        CHECK(r.age_baseline_ms < 510.0);

        CHECK(r.final_snapshot.has_age);
        CHECK(r.worst_age_ms >= c.worst_lo);
        CHECK(r.worst_age_ms < c.worst_hi);

        // AND THE POINT OF THE FLOOR BEING WHERE IT IS, rather than at zero:
        // one liveness interval is the instrument's resolution (the elapsed term
        // grows between arrivals while the delivered term does not), and Anvil's
        // occasional slipped `summary` tick is never repaid — a fixed engine
        // deadline does not run fast afterwards to catch up — so a slip stays in
        // the window until it ages out of it. 0.9 s on the M1 trace is a slipped
        // tick plus the sawtooth, and it is real: from one socket a late
        // broadcast and a late delivery are the same observation (the ruling's
        // point 7).
        CHECK(r.worst_age_ms < 1000u);   // the floor, bounded — under two intervals
        CHECK(r.episodes.empty());       // and NOTHING greyed: age is not a grey signal
    }
}

TEST_CASE("age meter: the reconnect trace peaks at the outage and dies with the socket") {
    // The calibrated path, because the reset is timed off the watchdog: the
    // estimate is voided when the Gap is raised at prev_rx + threshold.
    const ReplayResult r = run_replay_file(reconnect_path(), kAnvilTicker101, ReplayOptions{});
    REQUIRE(r.episodes.size() == 1);

    // 2.3 s: the 2,000 ms the watchdog waited, plus however far into the summary
    // period the last frame fell. Two and a half times the healthy floor above,
    // which is the separation that makes the number worth printing.
    CHECK(r.worst_age_ms >= 2300u);
    CHECK(r.worst_age_ms < 2400u);

    // THE BACKLOG DIED WITH THE SOCKET. Everything published between the Gap and
    // the new connection's first window reads "no reading yet" rather than a
    // number carried across a queue that no longer exists — and the panel is
    // grey throughout that stretch anyway.
    std::size_t after_gap = 0;
    std::size_t unknown_after_gap = 0;
    bool seen_gap = false;
    dc::harness::TraceReader reader(reconnect_path());
    run_replay(reader, kAnvilTicker101, ReplayOptions{},
               [&](const dc::harness::ReplayStep& step,
                   const depthcharge::DisplaySnapshot& snap) {
                   if (step.kind == FeedEvent::Kind::Gap) {
                       // The grey frame itself still carries the age the book
                       // had reached when the watchdog fired.
                       CHECK(snap.has_age);
                       CHECK(snap.age_ms >= 2300u);
                       seen_gap = true;
                       return true;
                   }
                   if (seen_gap) {
                       ++after_gap;
                       if (!snap.has_age) { ++unknown_after_gap; }
                   }
                   return true;
               });
    REQUIRE(seen_gap);
    REQUIRE(after_gap > 0);
    // At least the new connection's whole baseline window: the meter says
    // nothing until it has measured this socket's clock for itself.
    CHECK(unknown_after_gap >= dc::harness::kBaselineSamples);

    // By the end of the trace the new connection has re-measured the venue's
    // clock for itself and the meter is back on the floor.
    CHECK(r.final_snapshot.has_age);
    CHECK(r.final_snapshot.age_ms < 1000u);
    CHECK(r.age_baseline_ms > 490.0);
    CHECK(r.age_baseline_ms < 510.0);
}

TEST_CASE("age meter: the age travels in the snapshot, not beside it") {
    // §5's list is the render side's entire universe (invariant #8), so a number
    // the panel must draw has to be IN the published frame. This is the same
    // argument `status` was added on: a field that arrives by a side channel can
    // be drawn without it.
    const ReplayResult r = replay(baseline_path());
    CHECK(r.final_snapshot.has_age);

    // And the console draws what it was handed rather than recomputing it.
    dc::harness::LadderStyle style;
    style.color = false;
    style.unicode = false;
    const std::string text = dc::harness::render_ladder(r.final_snapshot, style);
    CHECK(text.find(" age ") != std::string::npos);
}

TEST_CASE("reconnect trace: exactly one published frame is stale, and it is the Gap's") {
    std::vector<std::size_t> stale_events;
    std::size_t first_live_after_stale = 0;
    ReplayOptions opts = ReplayOptions::legacy_anvil();
    dc::harness::TraceReader reader(reconnect_path());
    const ReplayResult r = dc::harness::run_replay(
        reader, kAnvilTicker101, opts,
        [&](const dc::harness::ReplayStep& step,
            const depthcharge::DisplaySnapshot& snap) -> bool {
            if (!snap.live()) {
                stale_events.push_back(step.event_index);
                CHECK(step.kind == FeedEvent::Kind::Gap);
            } else if (!stale_events.empty() && first_live_after_stale == 0) {
                first_live_after_stale = step.event_index;
            }
            return true;
        });

    REQUIRE(stale_events.size() == 1);
    CHECK(stale_events.front() == 332);
    // The very next event re-baselines the book: stale -> live, nothing between.
    CHECK(first_live_after_stale == 333);
    CHECK(r.events == 1117);
}

TEST_CASE("reconnect trace: the rendered ladders differ across the outage") {
    const ReplayResult r = replay(reconnect_path());
    dc::harness::LadderStyle style;
    style.color = false;

    const std::string stale = dc::harness::render_ladder(r.first_stale_snapshot, style);
    const std::string live = dc::harness::render_ladder(r.final_snapshot, style);

    CHECK(stale.find("STALE") != std::string::npos);
    CHECK(stale.find("disconnect") != std::string::npos);
    CHECK(live.find("STALE") == std::string::npos);
    CHECK(live.find("LIVE") != std::string::npos);
}

// --- the detection rule itself ----------------------------------------------

TEST_CASE("the watchdog threshold is what decides a disconnect, and it is tunable") {
    // Two frames 5 s apart: a hole no healthy Anvil stream ever shows.
    const std::string trace =
        R"({"captured_at":"t","url":"u","ticker":101,"tool_version":"0.1.0"})"
        "\n"
        R"({"rx_ns":1000000000,"frame":{"type":"book","seq":1,"ticker":101,)"
        R"("bids":[{"price":"10.0","qty":5,"orders":1}],)"
        R"("asks":[{"price":"10.001","qty":6,"orders":1}]}})"
        "\n"
        R"({"rx_ns":6000000000,"frame":{"type":"snapshot","seq":2,"ticker":101,)"
        R"("bids":[{"price":"10.002","qty":7,"orders":1}],)"
        R"("asks":[{"price":"10.003","qty":8,"orders":1}]}})"
        "\n";

    SUBCASE("default 1000 ms watchdog: the hole is a disconnect") {
        ReplayOptions opts = ReplayOptions::legacy_anvil();
        const ReplayResult r = run_replay_text(trace, kAnvilTicker101, opts);
        REQUIRE(r.episodes.size() == 1);
        CHECK(r.episodes[0].observed_gap_ms == doctest::Approx(5000.0));
        CHECK(r.episodes[0].stale_ms == doctest::Approx(4000.0));
        CHECK(r.episodes[0].cleared_frame == 2);
        CHECK(r.final_snapshot.live());
        CHECK(r.final_snapshot.best_bid() == 100020);
    }

    SUBCASE("a 10 s watchdog: the same hole is just a quiet market") {
        ReplayOptions opts = ReplayOptions::legacy_anvil();
        opts.disconnect_gap_ms = 10000.0;
        const ReplayResult r = run_replay_text(trace, kAnvilTicker101, opts);
        CHECK(r.episodes.empty());
        CHECK(r.events == 2);
    }
}

TEST_CASE("consecutive outages with no resync between them are one grey window") {
    // Frame 2 and 3 are `summary` frames: they emit no event, so they cannot
    // clear stale — the panel is grey continuously from the first watchdog to
    // the snapshot in frame 4, through two watchdog firings.
    const std::string trace =
        R"({"captured_at":"t","url":"u","ticker":101,"tool_version":"0.1.0"})"
        "\n"
        R"({"rx_ns":1000000000,"frame":{"type":"book","seq":1,"ticker":101,)"
        R"("bids":[{"price":"10.0","qty":5,"orders":1}],"asks":[]}})"
        "\n"
        R"({"rx_ns":7000000000,"frame":{"type":"summary","seq":2,"tickers":[]}})"
        "\n"
        R"({"rx_ns":7500000000,"frame":{"type":"summary","seq":3,"tickers":[]}})"
        "\n"
        R"({"rx_ns":14000000000,"frame":{"type":"snapshot","seq":4,"ticker":101,)"
        R"("bids":[{"price":"10.002","qty":7,"orders":1}],"asks":[]}})"
        "\n";

    const ReplayResult r = run_replay_text(trace, kAnvilTicker101, ReplayOptions::legacy_anvil());

    REQUIRE(r.episodes.size() == 1);
    const auto& ep = r.episodes.front();
    CHECK(ep.gap_events == 2);            // two watchdog firings, one outage
    CHECK(ep.frame_before == 1);          // the window starts at the first hole
    CHECK(ep.cleared);
    CHECK(ep.cleared_frame == 4);
    CHECK(ep.observed_gap_ms == doctest::Approx(6500.0));  // the larger hole
    // Grey from the first watchdog (t=2 s) to the resync (t=14 s) — measuring
    // from the *second* watchdog would understate it by half.
    CHECK(ep.stale_ms == doctest::Approx(12000.0));
    CHECK(r.book.gaps == 2);              // the stats still count both firings
    CHECK(r.final_snapshot.live());
}

TEST_CASE("an outage that never resyncs leaves the panel stale to the last frame") {
    const std::string trace =
        R"({"captured_at":"t","url":"u","ticker":101,"tool_version":"0.1.0"})"
        "\n"
        R"({"rx_ns":1000000000,"frame":{"type":"book","seq":1,"ticker":101,)"
        R"("bids":[{"price":"10.0","qty":5,"orders":1}],"asks":[]}})"
        "\n"
        R"({"rx_ns":9000000000,"frame":{"type":"trade","seq":2,"ticker":101,)"
        R"("price":"10.0","qty":3,"aggr":"B","takerId":"a","makerId":"b","ts":1}})"
        "\n";

    const ReplayResult r = run_replay_text(trace, kAnvilTicker101, ReplayOptions::legacy_anvil());

    REQUIRE(r.episodes.size() == 1);
    CHECK_FALSE(r.episodes[0].cleared);
    // A trade arriving after the gap is tape, and tape does not re-baseline a
    // book: the panel stays grey until a Snapshot lands.
    CHECK_FALSE(r.final_snapshot.live());
    CHECK(r.final_snapshot.trade_count == 1);
    CHECK(r.final_snapshot.stale_reason == GapReason::Disconnect);
}

// --- stopping early, and the end of the trace -------------------------------

TEST_CASE("an observer that stops the replay does not consume the next line") {
    // Frame 3 is deliberately malformed. Before the stop conditions moved into
    // the loop condition, the driver read and fully validated one line past the
    // stop point, so an observer stopping at frame 2 still threw on frame 3.
    const std::string trace =
        R"({"captured_at":"t","url":"u","ticker":101,"tool_version":"0.1.0"})"
        "\n"
        R"({"rx_ns":1000000000,"frame":{"type":"book","seq":1,"ticker":101,)"
        R"("bids":[{"price":"10.0","qty":5}],"asks":[]}})"
        "\n"
        R"({"rx_ns":1100000000,"frame":{"type":"book","seq":2,"ticker":101,)"
        R"("bids":[{"price":"10.001","qty":6}],"asks":[]}})"
        "\n"
        R"({"rx_ns":1200000000,"frame":{"seq":3,"ticker":101}})"   // no "type"
        "\n";

    SUBCASE("stopping via the observer") {
        std::size_t seen = 0;
        dc::harness::TraceReader reader(trace, dc::harness::in_memory, "<stop>");
        const ReplayResult r = dc::harness::run_replay(
            reader, kAnvilTicker101, ReplayOptions::legacy_anvil(),
            [&](const dc::harness::ReplayStep&, const depthcharge::DisplaySnapshot&) -> bool {
                ++seen;
                return seen < 2;   // stop after the second event
            });
        CHECK(seen == 2);
        CHECK(r.frames == 2);
        CHECK(reader.frames_read() == 2);   // frame 3 was never read
    }

    SUBCASE("stopping via --at") {
        ReplayOptions opts = ReplayOptions::legacy_anvil();
        opts.max_frames = 2;
        dc::harness::TraceReader reader(trace, dc::harness::in_memory, "<at>");
        const ReplayResult r = dc::harness::run_replay(reader, kAnvilTicker101, opts);
        CHECK(r.frames == 2);
        CHECK(reader.frames_read() == 2);   // the two stop paths agree
    }

    SUBCASE("running to the end does still reject the malformed line") {
        CHECK_THROWS_AS(run_replay_text(trace, kAnvilTicker101, ReplayOptions::legacy_anvil()),
                        dc::harness::TraceError);
    }
}

TEST_CASE("trailing silence greys the panel only when the caller reports it") {
    // Two frames 100 ms apart, then the trace simply stops. A file has no "now",
    // so the watchdog — which is edge-triggered by the next frame — cannot see
    // what happened afterwards.
    const std::string trace =
        R"({"captured_at":"t","url":"u","ticker":101,"tool_version":"0.1.0"})"
        "\n"
        R"({"rx_ns":1000000000,"frame":{"type":"snapshot","seq":1,"ticker":101,)"
        R"("bids":[{"price":"10.0","qty":5}],"asks":[{"price":"10.001","qty":6}]}})"
        "\n"
        R"({"rx_ns":1100000000,"frame":{"type":"book","seq":2,"ticker":101,)"
        R"("bids":[{"price":"10.002","qty":7}],"asks":[{"price":"10.003","qty":8}]}})"
        "\n";

    SUBCASE("default: unknown silence, so the replay ends live") {
        const ReplayResult r = run_replay_text(trace, kAnvilTicker101, ReplayOptions::legacy_anvil());
        CHECK(r.episodes.empty());
        CHECK(r.final_snapshot.live());
    }

    SUBCASE("silence shorter than the watchdog is just a quiet market") {
        ReplayOptions opts = ReplayOptions::legacy_anvil();
        opts.end_of_trace_silence_ms = 500.0;   // under the 1000 ms threshold
        const ReplayResult r = run_replay_text(trace, kAnvilTicker101, opts);
        CHECK(r.episodes.empty());
        CHECK(r.final_snapshot.live());
    }

    SUBCASE("silence past the watchdog greys it, and nothing clears it") {
        ReplayOptions opts = ReplayOptions::legacy_anvil();
        opts.end_of_trace_silence_ms = 30000.0;
        const ReplayResult r = run_replay_text(trace, kAnvilTicker101, opts);
        REQUIRE(r.episodes.size() == 1);
        const auto& ep = r.episodes.front();
        CHECK(ep.reason == GapReason::Disconnect);
        CHECK(ep.frame_before == 2);
        CHECK_FALSE(ep.cleared);            // no frame follows to re-baseline
        CHECK(ep.observed_gap_ms == doctest::Approx(30000.0));
        CHECK_FALSE(r.final_snapshot.live());
        CHECK(r.final_snapshot.stale_reason == GapReason::Disconnect);
        // The book is greyed, not blanked.
        CHECK(r.final_snapshot.has_top());
    }
}

TEST_CASE("symbol_for is the single rule both the ladder and the goldens use") {
    dc::harness::TraceMeta meta;
    meta.ticker = 101;
    CHECK(dc::harness::symbol_for(meta).id == kAnvilTicker101.id);
    CHECK(dc::harness::symbol_for(meta).price_decimals == kAnvilTicker101.price_decimals);
    CHECK(dc::harness::symbol_for(meta).qty_decimals == kAnvilTicker101.qty_decimals);

    // A trace for another ticker takes its id from the metadata but keeps the
    // declared scale — Anvil publishes no tick metadata, so the scale is
    // DepthCharge's declaration and a wrong one must fail loudly on the first
    // price rather than be silently re-interpreted (ARCHITECTURE §4).
    meta.ticker = 107;
    CHECK(dc::harness::symbol_for(meta).id == 107);
    CHECK(dc::harness::symbol_for(meta).price_decimals == kAnvilTicker101.price_decimals);

    // Metadata with no ticker falls back to the declared constant.
    meta.ticker = -1;
    CHECK(dc::harness::symbol_for(meta).id == kAnvilTicker101.id);
}

TEST_CASE("the verbatim frame text is what reaches the adapter") {
    // slice_frame_json must hand over the server's bytes untouched — key order
    // included — or the harness would be testing a re-serialised frame.
    const std::string line =
        R"({"rx_ns": 51368066959854, "frame": {"type":"trade","seq":7,"ticker":101,)"
        R"("price":"9.9972","qty":9,"aggr":"S","takerId":"fv{jsg","makerId":"fvjms",)"
        R"("ts":1784844923088}})";
    const std::string_view frame = dc::harness::slice_frame_json(line);

    CHECK(frame.substr(0, 15) == R"({"type":"trade")");
    CHECK(frame.back() == '}');
    CHECK(frame.find(R"("takerId":"fv{jsg")") != std::string_view::npos);  // brace in a string
    CHECK(frame.size() == line.size() - line.find(R"({"type")") - 1);
}
