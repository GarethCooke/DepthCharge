// test_anvil_adapter.cpp — Anvil wire frames -> FeedEvents.
//
// The adapter is where every venue-specific decision lives (invariant #2), so
// this file is where each of those decisions is nailed down against synthetic
// frames shaped exactly like the captured ones. The real traces are exercised
// end-to-end in test_replay_goldens.cpp; here the point is the edge cases a
// 90-second capture happens not to contain.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include <depthcharge/anvil/anvil_adapter.hpp>
#include <depthcharge/decimal.hpp>
#include <depthcharge/feed_event.hpp>

using depthcharge::FeedEvent;
using depthcharge::GapReason;
using depthcharge::Side;
using depthcharge::anvil::AnvilAdapter;
using depthcharge::anvil::kAnvilTicker101;

namespace {

// Captured events, with the Snapshot spans deep-copied — the lifetime rule in
// feed_event.hpp says the borrowed levels die when the sink call returns, and a
// test that ignored that would be testing a dangling pointer.
struct CapturedEvent {
    FeedEvent::Kind kind{};
    depthcharge::Seq seq = 0;
    depthcharge::PriceTicks px = 0;
    depthcharge::Qty qty = 0;
    Side side{};
    GapReason reason{};
    std::vector<depthcharge::BookLevel> bids, asks;
};

struct Collector {
    std::vector<CapturedEvent> events;

    void operator()(const FeedEvent& ev) {
        CapturedEvent c;
        c.kind = ev.kind;
        c.seq = ev.seq;
        c.px = ev.px;
        c.qty = ev.qty;
        c.side = ev.side;
        c.reason = ev.reason;
        c.bids.assign(ev.bids.begin(), ev.bids.end());
        c.asks.assign(ev.asks.begin(), ev.asks.end());
        events.push_back(std::move(c));
    }
};

// Shaped exactly like the wire (docs/vendor/anvil-protocol.md §3.1).
const char* kSnapshot =
    R"({"type":"snapshot","seq":244492267,"ticker":101,)"
    R"("bids":[{"price":"9.9972","qty":9,"orders":1},{"price":"9.9954","qty":101,"orders":1}],)"
    R"("asks":[{"price":"9.9979","qty":44,"orders":2},{"price":"10.0","qty":7,"orders":1}]})";

const char* kBook =
    R"({"type":"book","seq":244492299,"ticker":101,)"
    R"("bids":[{"price":"9.9970","qty":5,"orders":1}],)"
    R"("asks":[{"price":"9.9981","qty":50,"orders":1}]})";

const char* kTradeBuy =
    R"({"type":"trade","seq":244492556,"ticker":101,"price":"9.9972","qty":9,"aggr":"B",)"
    R"("takerId":"fvjsg","makerId":"fvjms","ts":1784844923088})";

const char* kTradeSell =
    R"({"type":"trade","seq":244492557,"ticker":101,"price":"9.9950","qty":12,"aggr":"S",)"
    R"("takerId":"fvjsh","makerId":"fvjmt","ts":1784844923099})";

const char* kSummary =
    R"({"type":"summary","seq":244492218,"tickers":[{"ticker":101,"restingBuy":11179,)"
    R"("restingSell":8780,"last":"9.9975"},{"ticker":102,"restingBuy":9474,"restingSell":8577,)"
    R"("last":"10.017"}]})";

}  // namespace

TEST_CASE("snapshot and book are the same thing: both become a full-replace Snapshot") {
    AnvilAdapter adapter(kAnvilTicker101);
    Collector sink;

    adapter.on_frame(kSnapshot, sink);
    adapter.on_frame(kBook, sink);

    REQUIRE(sink.events.size() == 2);
    for (const auto& ev : sink.events) {
        CHECK(ev.kind == FeedEvent::Kind::Snapshot);
    }

    // Prices arrive as integer ticks at the declared 10^-4 scale, best-first.
    CHECK(sink.events[0].bids.size() == 2);
    CHECK(sink.events[0].bids[0].px == 99972);
    CHECK(sink.events[0].bids[0].qty == 9);
    CHECK(sink.events[0].bids[1].px == 99954);
    CHECK(sink.events[0].asks[0].px == 99979);
    CHECK(sink.events[0].asks[0].qty == 44);
    CHECK(sink.events[0].asks[1].px == 100000);  // "10.0"

    CHECK(sink.events[1].bids.size() == 1);
    CHECK(sink.events[1].bids[0].px == 99970);

    CHECK(adapter.stats().snapshot_frames == 1);
    CHECK(adapter.stats().book_frames == 1);
    CHECK(adapter.stats().events_out == 2);
}

TEST_CASE("trade: aggressor side is mapped, price is the resting price") {
    AnvilAdapter adapter(kAnvilTicker101);
    Collector sink;

    adapter.on_frame(kTradeBuy, sink);
    adapter.on_frame(kTradeSell, sink);

    REQUIRE(sink.events.size() == 2);
    CHECK(sink.events[0].kind == FeedEvent::Kind::Trade);
    CHECK(sink.events[0].px == 99972);
    CHECK(sink.events[0].qty == 9);
    CHECK(sink.events[0].side == Side::Bid);   // "B": a buy lifted the offer
    CHECK(sink.events[1].side == Side::Ask);   // "S": a sell hit the bid
    CHECK(adapter.stats().trade_frames == 2);
}

TEST_CASE("summary is tolerated and ignored — it is board-mode data, not ladder data") {
    AnvilAdapter adapter(kAnvilTicker101);
    Collector sink;

    adapter.on_frame(kSummary, sink);

    CHECK(sink.events.empty());
    CHECK(adapter.stats().summary_ignored == 1);
    CHECK(adapter.stats().parse_errors == 0);
    CHECK(adapter.stats().frames_in == 1);
}

TEST_CASE("Seq is synthesised from receive order and ignores the wire seq") {
    AnvilAdapter adapter(kAnvilTicker101);
    Collector sink;

    // Wire seqs here run 244492267 -> 244492299 -> 244492556 -> *backwards* to
    // 244492218 (the summary) -> 244492557. That is the M0 finding reproduced:
    // a global counter seen through one socket is not monotonic.
    adapter.on_frame(kSnapshot, sink);
    adapter.on_frame(kBook, sink);
    adapter.on_frame(kTradeBuy, sink);
    adapter.on_frame(kSummary, sink);      // seq goes backwards here
    adapter.on_frame(kTradeSell, sink);

    REQUIRE(sink.events.size() == 4);  // the summary emits nothing
    CHECK(sink.events[0].seq == 1);
    CHECK(sink.events[1].seq == 2);
    CHECK(sink.events[2].seq == 3);
    CHECK(sink.events[3].seq == 4);      // dense: the ignored summary burns no seq

    CHECK(adapter.stats().wire_seq_backward == 1);  // observed...
    for (const auto& ev : sink.events) {            // ...and never acted on
        CHECK(ev.kind != FeedEvent::Kind::Gap);
    }
}

TEST_CASE("no wire condition produces Gap{SeqGap} — only the transport gaps") {
    AnvilAdapter adapter(kAnvilTicker101);
    Collector sink;

    adapter.on_frame(kSnapshot, sink);
    adapter.on_transport_gap(GapReason::Disconnect, sink);
    adapter.on_frame(kSnapshot, sink);

    REQUIRE(sink.events.size() == 3);
    CHECK(sink.events[1].kind == FeedEvent::Kind::Gap);
    CHECK(sink.events[1].reason == GapReason::Disconnect);
    CHECK(sink.events[1].seq == 2);  // a Gap is an event and takes a seq
    CHECK(sink.events[2].seq == 3);
    CHECK(adapter.stats().transport_gaps == 1);
}

TEST_CASE("malformed input is counted and dropped, never turned into a Gap") {
    AnvilAdapter adapter(kAnvilTicker101);
    Collector sink;

    adapter.on_frame("this is not json", sink);
    adapter.on_frame("[1,2,3]", sink);                       // JSON, wrong shape
    adapter.on_frame(R"({"seq":1,"ticker":101})", sink);     // no type
    adapter.on_frame(R"({"type":"book","ticker":101})", sink);  // no bids/asks
    adapter.on_frame(R"({"type":"trade","ticker":101,"price":"1.0","qty":1.5,"aggr":"B"})",
                     sink);                                  // fractional qty
    adapter.on_frame(R"({"type":"trade","ticker":101,"price":"1.0","qty":1,"aggr":"X"})",
                     sink);                                  // bogus aggressor
    adapter.on_frame(R"({"type":"book","ticker":101,"bids":[{"price":1.0,"qty":1}],)"
                     R"("asks":[]})", sink);                 // price as a number
    adapter.on_frame(R"({"type":"book","ticker":101,)"
                     R"("bids":[{"price":"1.0","qty":-5}],"asks":[]})",
                     sink);                                  // negative resting size
    adapter.on_frame(R"({"type":"trade","ticker":101,"price":"1.0","qty":-1,"aggr":"B"})",
                     sink);                                  // negative fill size

    CHECK(sink.events.empty());
    CHECK(adapter.stats().parse_errors == 9);
    CHECK(adapter.stats().events_out == 0);
    // A dropped frame is not a stale panel: Anvil republishes the whole book
    // every ~80 ms, so the next frame heals it.
    CHECK(adapter.stats().transport_gaps == 0);
}

TEST_CASE("a price finer than the declared scale fails loudly instead of rounding") {
    AnvilAdapter adapter(kAnvilTicker101);
    Collector sink;

    adapter.on_frame(R"({"type":"book","seq":1,"ticker":101,)"
                     R"("bids":[{"price":"9.99725","qty":9,"orders":1}],"asks":[]})",
                     sink);

    CHECK(sink.events.empty());
    CHECK(adapter.stats().price_errors == 1);
    CHECK(adapter.stats().parse_errors == 0);  // distinct diagnosis, not lumped in
}

TEST_CASE("a frame for another ticker is dropped before any payload work") {
    AnvilAdapter adapter(kAnvilTicker101);
    Collector sink;

    adapter.on_frame(R"({"type":"book","seq":1,"ticker":107,)"
                     R"("bids":[{"price":"9.9","qty":1,"orders":1}],"asks":[]})",
                     sink);

    CHECK(sink.events.empty());
    CHECK(adapter.stats().other_ticker == 1);
}

TEST_CASE("an unknown frame type is tolerated, counted, and ignored") {
    AnvilAdapter adapter(kAnvilTicker101);
    Collector sink;

    // Protocol §3.4 reserves "error"; a future server may add more. Neither is
    // allowed to stop the ladder.
    adapter.on_frame(R"({"type":"error","seq":9,"code":"rejected","message":"nope"})", sink);
    adapter.on_frame(R"({"type":"something_new","seq":10})", sink);

    CHECK(sink.events.empty());
    CHECK(adapter.stats().unknown_kind == 2);
    CHECK(adapter.stats().parse_errors == 0);
}

TEST_CASE("a book deeper than the engine carries is truncated at the tail and flagged") {
    std::string frame = R"({"type":"book","seq":1,"ticker":101,"bids":[)";
    const std::size_t deep = depthcharge::kMaxSnapshotLevels + 40;
    for (std::size_t i = 0; i < deep; ++i) {
        if (i > 0) { frame += ','; }
        // Descending prices: 10.0000, 9.9999, ... so the *best* levels are the
        // ones that must survive.
        frame += R"({"price":")";
        char buf[32] = {};
        const auto n = depthcharge::format_scaled(
            100000 - static_cast<std::int64_t>(i), 4, buf, sizeof buf);
        frame.append(buf, n);
        frame += R"(","qty":)";
        frame += std::to_string(i + 1);
        frame += R"(,"orders":1})";
    }
    frame += R"(],"asks":[]})";

    AnvilAdapter adapter(kAnvilTicker101);
    Collector sink;
    adapter.on_frame(frame, sink);

    REQUIRE(sink.events.size() == 1);
    CHECK(sink.events[0].bids.size() == depthcharge::kMaxSnapshotLevels);
    CHECK(sink.events[0].bids[0].px == 100000);  // best level kept
    CHECK(sink.events[0].bids[0].qty == 1);
    CHECK(adapter.stats().truncated_frames == 1);
}

TEST_CASE("an empty book is a legal Snapshot, not an error") {
    AnvilAdapter adapter(kAnvilTicker101);
    Collector sink;

    adapter.on_frame(R"({"type":"book","seq":1,"ticker":101,"bids":[],"asks":[]})", sink);

    REQUIRE(sink.events.size() == 1);
    CHECK(sink.events[0].kind == FeedEvent::Kind::Snapshot);
    CHECK(sink.events[0].bids.empty());
    CHECK(sink.events[0].asks.empty());
}
