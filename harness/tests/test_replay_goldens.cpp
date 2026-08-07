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

#include <string>
#include <string_view>
#include <vector>

#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/feed_event.hpp>

#include "dc_harness/console_ladder.hpp"
#include "dc_harness/replay_driver.hpp"
#include "dc_harness/trace.hpp"

using depthcharge::FeedEvent;
using depthcharge::FeedStatus;
using depthcharge::GapReason;
using depthcharge::Side;
using depthcharge::anvil::kAnvilTicker101;
using dc::harness::ReplayOptions;
using dc::harness::ReplayResult;
using dc::harness::run_replay_file;
using dc::harness::run_replay_text;

namespace {

std::string baseline_path() {
    return std::string(DC_REPLAY_DIR) + "/anvil_101_baseline.ndjson";
}
std::string reconnect_path() {
    return std::string(DC_REPLAY_DIR) + "/anvil_101_reconnect.ndjson";
}

ReplayResult replay(const std::string& path, std::size_t max_frames = 0) {
    ReplayOptions opts;
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
    CHECK(r.book.deltas_rejected == 0);
}

TEST_CASE("baseline trace: the wire seq misbehaves and the synthesised Seq does not") {
    std::vector<depthcharge::Seq> seqs;
    std::size_t gaps = 0;
    ReplayOptions opts;
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
    REQUIRE(seqs.size() == 1225);
    for (std::size_t i = 0; i < seqs.size(); ++i) {
        REQUIRE(seqs[i] == i + 1);
    }
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

    // Prints are strictly older as you walk down the tape.
    for (std::size_t i = 1; i < depthcharge::kTradeRingSize; ++i) {
        CHECK(s.trades[i - 1].seq > s.trades[i].seq);
    }
}

TEST_CASE("baseline trace: the ladder never goes stale on a healthy feed") {
    std::size_t stale_publishes = 0;
    ReplayOptions opts;
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

TEST_CASE("reconnect trace: exactly one published frame is stale, and it is the Gap's") {
    std::vector<std::size_t> stale_events;
    std::size_t first_live_after_stale = 0;
    ReplayOptions opts;
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
        ReplayOptions opts;
        const ReplayResult r = run_replay_text(trace, kAnvilTicker101, opts);
        REQUIRE(r.episodes.size() == 1);
        CHECK(r.episodes[0].observed_gap_ms == doctest::Approx(5000.0));
        CHECK(r.episodes[0].stale_ms == doctest::Approx(4000.0));
        CHECK(r.episodes[0].cleared_frame == 2);
        CHECK(r.final_snapshot.live());
        CHECK(r.final_snapshot.best_bid() == 100020);
    }

    SUBCASE("a 10 s watchdog: the same hole is just a quiet market") {
        ReplayOptions opts;
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

    const ReplayResult r = run_replay_text(trace, kAnvilTicker101, ReplayOptions{});

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

    const ReplayResult r = run_replay_text(trace, kAnvilTicker101, ReplayOptions{});

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
            reader, kAnvilTicker101, ReplayOptions{},
            [&](const dc::harness::ReplayStep&, const depthcharge::DisplaySnapshot&) -> bool {
                ++seen;
                return seen < 2;   // stop after the second event
            });
        CHECK(seen == 2);
        CHECK(r.frames == 2);
        CHECK(reader.frames_read() == 2);   // frame 3 was never read
    }

    SUBCASE("stopping via --at") {
        ReplayOptions opts;
        opts.max_frames = 2;
        dc::harness::TraceReader reader(trace, dc::harness::in_memory, "<at>");
        const ReplayResult r = dc::harness::run_replay(reader, kAnvilTicker101, opts);
        CHECK(r.frames == 2);
        CHECK(reader.frames_read() == 2);   // the two stop paths agree
    }

    SUBCASE("running to the end does still reject the malformed line") {
        CHECK_THROWS_AS(run_replay_text(trace, kAnvilTicker101, ReplayOptions{}),
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
        const ReplayResult r = run_replay_text(trace, kAnvilTicker101, ReplayOptions{});
        CHECK(r.episodes.empty());
        CHECK(r.final_snapshot.live());
    }

    SUBCASE("silence shorter than the watchdog is just a quiet market") {
        ReplayOptions opts;
        opts.end_of_trace_silence_ms = 500.0;   // under the 1000 ms threshold
        const ReplayResult r = run_replay_text(trace, kAnvilTicker101, opts);
        CHECK(r.episodes.empty());
        CHECK(r.final_snapshot.live());
    }

    SUBCASE("silence past the watchdog greys it, and nothing clears it") {
        ReplayOptions opts;
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
    CHECK(dc::harness::symbol_for(meta).qty_step == kAnvilTicker101.qty_step);

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
