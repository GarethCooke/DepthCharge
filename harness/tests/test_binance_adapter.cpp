// test_binance_adapter.cpp — the third adapter (M5 stage B1).
//
// What is asserted here and what is asserted elsewhere, because the split is
// deliberate:
//
//   * `dc_binance_oracle --check` grades this adapter's BOOK against the venue's
//     own top-20 over every committed slice, pinned per file. That is the
//     correctness statement and it is a whole-corpus one.
//   * this file covers the cases no committed capture contains — a failed seed
//     fetch, a sequence break, a scale disagreement, the emitted-window edge —
//     which is the same division `slice_trace.py --selfcheck` and
//     `tracefile.py --selfcheck` already make, and for the same reason: every
//     committed capture is well-formed, so a corpus-driven test of the error
//     paths would be seven copies of one observation.
//
// MUTATION-VERIFIED 2026-08-25 against `dc_binance_oracle`, by hand, because the
// mutants are deliberate breakages of the adapter and this project does not ship
// mutation code inside the thing being mutated. Each was applied for real,
// rebuilt with the timestamps forced forward, run, and reverted with the
// baseline re-confirmed between each:
//
//   qty 0 stored as a level rather than removing one
//       -> 235 matched -> 0 matched / 235 failed
//   bid and ask sides swapped at the seed
//       -> 235 matched -> 0 matched / 235 failed
//   the maintained book bounded to kBinanceEmitDepth (256) instead of 1,024
//       -> 235 matched -> 0 matched / 235 failed
//
// THE THIRD FIGURE IS NOT THE ONE THE SWEEP PREDICTS, and the difference is
// worth writing down rather than rounding to "it went red". `binance_oracle.py
// --window-sweep` reports 202 matched / 33 failed for a 256-level book, and this
// mutant reports 0/235 — because the two bound the book at different moments.
// The sweep TRUNCATES after each update, keeping the best 256 it has; this
// mutant bounds `insert_at`, so the 1,000-level SEED is cut to 256 on arrival
// and the book starts life shallower as well as staying shallow. Both are red
// and neither is the other, which is precisely why the prediction was checked
// against a run instead of being written down as though it had been.
//
// The third mutant is still worth more than the other two: it is not an invented
// breakage but the ACCIDENTAL one — a book bounded at the obvious constant,
// `kMaxSnapshotLevels` — and it is the defect the sweep says a real client falls
// into without meaning to.
#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include <depthcharge/binance/binance_adapter.hpp>
#include <depthcharge/binance/binance_frame.hpp>

using depthcharge::FeedEvent;
using depthcharge::GapReason;
using depthcharge::Side;
using depthcharge::binance::BinanceAdapter;
using depthcharge::binance::BinanceFrame;
using depthcharge::binance::FrameKind;
using depthcharge::binance::FrameSource;
using depthcharge::binance::kBinanceBtcUsdt;
using depthcharge::binance::ParseStatus;
using depthcharge::binance::SymbolConfig;

namespace {

// The adapter is ~96 KiB and must never be a local.
using Adapter = std::unique_ptr<BinanceAdapter>;
Adapter make() { return std::make_unique<BinanceAdapter>(kBinanceBtcUsdt); }

struct Events {
    std::vector<FeedEvent> v;
    void operator()(const FeedEvent& e) { v.push_back(e); }
    std::size_t count(FeedEvent::Kind k) const {
        std::size_t n = 0;
        for (const FeedEvent& e : v) {
            if (e.kind == k) { ++n; }
        }
        return n;
    }
};

std::string level(const char* px, const char* qty) {
    return std::string("[\"") + px + "\",\"" + qty + "\"]";
}

std::string seed_body(std::int64_t last_id, const std::string& bids,
                      const std::string& asks) {
    return "{\"lastUpdateId\":" + std::to_string(last_id) + ",\"bids\":[" + bids +
           "],\"asks\":[" + asks + "]}";
}

std::string diff(std::int64_t U, std::int64_t u, const std::string& bids,
                 const std::string& asks) {
    return "{\"e\":\"depthUpdate\",\"E\":1,\"s\":\"BTCUSDT\",\"U\":" + std::to_string(U) +
           ",\"u\":" + std::to_string(u) + ",\"b\":[" + bids + "],\"a\":[" + asks + "]}";
}

}  // namespace

// ---------------------------------------------------------------------------
// The seed
// ---------------------------------------------------------------------------

TEST_CASE("a REST fetch that produced no body means the seed has not arrived yet") {
    // DELIVERABLE 3, and the shape is Kraken's *absence of a subscribe is not
    // failure of a subscribe*. M5 stage A found that `capture_binance.py`
    // RECORDS a failed fetch rather than dropping it, and that a reader which
    // refused one refused a trace its own capture loop had written. The
    // adapter's answer has to match: nothing is broken, the seed is simply not
    // here.
    Adapter a = make();
    Events ev;

    a->on_frame(diff(10, 11, level("100.00000000", "1.00000000"), ""), ev);
    a->on_rest_missing();
    a->on_frame(diff(12, 13, level("101.00000000", "2.00000000"), ""), ev);

    // Buffering CONTINUES and nothing is dropped.
    CHECK(a->stats().buffered_events == 2);
    CHECK(a->stats().deltas_before_seed == 0);
    // The book stays uninitialised, and NO Gap is raised — there is nothing to
    // be stale about, because there was never a book.
    CHECK_FALSE(a->has_baseline());
    CHECK(ev.count(FeedEvent::Kind::Gap) == 0);
    CHECK(ev.v.empty());
    // It does ask for another fetch, because the one that would have supplied a
    // seed did not.
    CHECK(a->stats().rest_no_body == 1);
    CHECK(a->reseed_wanted());

    // ...and when a seed finally lands, the buffered events are still there to
    // be reconciled. That is the whole point of not dropping them.
    a->on_rest_body(seed_body(9, level("99.00000000", "5.00000000"), ""), ev);
    CHECK(a->has_baseline());
    CHECK(a->stats().seed_bracket_ok == 1);
    CHECK(a->last_update_id() == 13);
    CHECK(ev.count(FeedEvent::Kind::Snapshot) == 1);
}

TEST_CASE("the documented bracketing: drop what the snapshot contains, require U <= L+1 <= u") {
    Adapter a = make();
    Events ev;
    // Three buffered events. The snapshot at L=20 already contains the first
    // two (u <= 20) and the third brackets L+1 == 21.
    a->on_frame(diff(10, 15, level("100.00000000", "1.00000000"), ""), ev);
    a->on_frame(diff(16, 20, level("100.00000000", "2.00000000"), ""), ev);
    a->on_frame(diff(21, 25, level("100.00000000", "3.00000000"), ""), ev);
    CHECK(a->stats().buffered_events == 3);

    a->on_rest_body(seed_body(20, level("100.00000000", "9.00000000"), ""), ev);
    CHECK(a->stats().buffered_dropped_by_seed == 2);
    CHECK(a->stats().seed_bracket_ok == 1);
    CHECK(a->stats().seed_bracket_failed == 0);
    // The surviving event was applied on top of the seed, so the book carries
    // ITS quantity and not the snapshot's.
    REQUIRE(a->bid_count() == 1);
    CHECK(a->bids()[0].qty == 300000000);  // 3.0 at 8 decimals
    CHECK(a->last_update_id() == 25);
}

TEST_CASE("a snapshot older than the first surviving event re-fetches rather than proceeds") {
    // The hole case: the seed names an instant BEFORE the buffered stream
    // begins, so applying the survivors would build a book whose provenance has
    // a gap in it. The venue's procedure says fetch again, and so does this.
    Adapter a = make();
    Events ev;
    a->on_frame(diff(100, 110, level("100.00000000", "1.00000000"), ""), ev);
    a->on_rest_body(seed_body(50, level("100.00000000", "9.00000000"), ""), ev);

    CHECK(a->stats().seed_bracket_failed == 1);
    CHECK(a->stats().seed_bracket_ok == 0);
    CHECK_FALSE(a->has_baseline());
    CHECK(a->reseed_wanted());
    // The Snapshot went out and was immediately followed by the Gap that
    // withdraws it — the honest order, and the same one Kraken's checksum path
    // uses: what we adopted did happen, and the Gap says it cannot be trusted.
    CHECK(ev.count(FeedEvent::Kind::Snapshot) == 1);
    REQUIRE(ev.count(FeedEvent::Kind::Gap) == 1);
    CHECK(ev.v.back().reason == GapReason::SeqGap);
}

// ---------------------------------------------------------------------------
// The diff stream
// ---------------------------------------------------------------------------

TEST_CASE("U/u continuity raises Gap{SeqGap} and nothing else") {
    // DELIVERABLE 4. §4 wrote `SeqGap` down for this venue at M0, before any of
    // this code existed; this is the fourth asking across three venues and it
    // needed no new word.
    Adapter a = make();
    Events ev;
    a->on_rest_body(seed_body(10, level("100.00000000", "1.00000000"), ""), ev);
    a->on_frame(diff(11, 12, level("100.00000000", "2.00000000"), ""), ev);
    CHECK(a->stats().seq_breaks == 0);

    // A hole: 13 is expected and 20 arrives.
    a->on_frame(diff(20, 21, level("100.00000000", "3.00000000"), ""), ev);
    CHECK(a->stats().seq_breaks == 1);
    REQUIRE(ev.count(FeedEvent::Kind::Gap) == 1);
    CHECK(ev.v.back().reason == GapReason::SeqGap);
    CHECK_FALSE(a->has_baseline());
    CHECK(a->reseed_wanted());
}

TEST_CASE("removing a level we do not hold is a counted no-op, never an error") {
    // DELIVERABLE 5, and documented venue behaviour rather than an edge case:
    // the venue's book is deeper than anything it sends, so a book seeded at
    // 1,000 levels receives removals for levels outside that window
    // continuously. Measured on the deep-seed slice: 1,497 of them.
    Adapter a = make();
    Events ev;
    a->on_rest_body(seed_body(10, level("100.00000000", "1.00000000"), ""), ev);
    const std::size_t before = ev.v.size();

    a->on_frame(diff(11, 12, level("42.00000000", "0.00000000"), ""), ev);

    CHECK(a->stats().levels_absent_removals == 1);
    CHECK(a->stats().levels_removed == 0);
    CHECK(a->stats().seq_breaks == 0);
    CHECK(ev.count(FeedEvent::Kind::Gap) == 0);
    CHECK(ev.v.size() == before);   // nothing emitted at all
    CHECK(a->has_baseline());       // and certainly no re-seed
}

TEST_CASE("a level re-sent at the quantity we already hold emits nothing") {
    Adapter a = make();
    Events ev;
    a->on_rest_body(seed_body(10, level("100.00000000", "1.00000000"), ""), ev);
    const std::size_t before = ev.v.size();
    a->on_frame(diff(11, 12, level("100.00000000", "1.00000000"), ""), ev);
    CHECK(a->stats().levels_unchanged == 1);
    CHECK(a->stats().levels_applied == 0);
    CHECK(ev.v.size() == before);
}

// ---------------------------------------------------------------------------
// The scale, and its detector
// ---------------------------------------------------------------------------

TEST_CASE("the scale is a venue constant and tickSize is a validator") {
    // DELIVERABLE 2. Every price and quantity this venue sends carries exactly 8
    // decimals whatever the instrument, so the scale is a constant; the declared
    // filters are checked as MULTIPLES and a violation is reported, never
    // rounded (ARCHITECTURE §4).
    BinanceFrame frame{};
    SymbolConfig cfg = kBinanceBtcUsdt;
    CHECK(cfg.spec.price_decimals == 8);
    CHECK(cfg.spec.qty_decimals == 8);

    // A price on the declared 0.01 tick.
    CHECK(parse_binance_frame(seed_body(1, level("78764.46000000", "1.00000000"), ""),
                              FrameSource::RestBody, cfg, frame) == ParseStatus::Ok);
    REQUIRE(frame.bid_count == 1);
    CHECK(frame.bids[0].px == 7876446000000);   // no float anywhere in that path

    // A price OFF the tick: 78764.465 is a whole number of wire units and NOT a
    // whole multiple of 0.01. Reported, not rounded to either neighbour.
    CHECK(parse_binance_frame(seed_body(1, level("78764.46500000", "1.00000000"), ""),
                              FrameSource::RestBody, cfg, frame) == ParseStatus::BadPrice);
    // And the postcondition holds: a rejected frame is left empty, so a caller
    // that ignores the status cannot read half a book out of it.
    CHECK(frame.kind == FrameKind::Unknown);
    CHECK(frame.bid_count == 0);

    // A quantity off the 0.00001 lot step.
    CHECK(parse_binance_frame(seed_body(1, level("78764.46000000", "1.000005"), ""),
                              FrameSource::RestBody, cfg, frame) == ParseStatus::BadQty);

    // THE PRECISION DETECTOR the brief asks to be named: a ninth decimal is the
    // wire changing under us, and the first thing that notices is a named status
    // on the value that disagreed, not a book that drifts.
    CHECK(parse_binance_frame(seed_body(1, level("1.000000005", "1.00000000"), ""),
                              FrameSource::RestBody, cfg, frame) == ParseStatus::BadPrice);
}

TEST_CASE("a diff for another instrument is not an error") {
    BinanceFrame frame{};
    const std::string other =
        "{\"e\":\"depthUpdate\",\"E\":1,\"s\":\"ETHUSDT\",\"U\":1,\"u\":2,\"b\":[],\"a\":[]}";
    CHECK(parse_binance_frame(other, FrameSource::WsFrame, kBinanceBtcUsdt, frame) ==
          ParseStatus::OtherSymbol);
}

TEST_CASE("the combined-stream envelope is seen through, and never required") {
    BinanceFrame frame{};
    const std::string bare = diff(1, 2, level("100.00000000", "1.00000000"), "");
    const std::string wrapped =
        "{\"stream\":\"btcusdt@depth@100ms\",\"data\":" + bare + "}";
    CHECK(parse_binance_frame(bare, FrameSource::WsFrame, kBinanceBtcUsdt, frame) ==
          ParseStatus::Ok);
    CHECK(frame.kind == FrameKind::DepthUpdate);
    const std::int64_t bare_px = frame.bids[0].px;

    CHECK(parse_binance_frame(wrapped, FrameSource::WsFrame, kBinanceBtcUsdt, frame) ==
          ParseStatus::Ok);
    CHECK(frame.kind == FrameKind::DepthUpdate);
    REQUIRE(frame.bid_count == 1);
    CHECK(frame.bids[0].px == bare_px);
}

TEST_CASE("the record says which, not the payload") {
    // A `@depth20` partial and a `/api/v3/depth` body are byte-shape identical.
    // Only the record that carried them says which, and the difference is
    // whether the book is fully known afterwards.
    BinanceFrame frame{};
    const std::string body = seed_body(7, level("100.00000000", "1.00000000"), "");
    CHECK(parse_binance_frame(body, FrameSource::WsFrame, kBinanceBtcUsdt, frame) ==
          ParseStatus::Ok);
    CHECK(frame.kind == FrameKind::PartialDepth);
    CHECK(parse_binance_frame(body, FrameSource::RestBody, kBinanceBtcUsdt, frame) ==
          ParseStatus::Ok);
    CHECK(frame.kind == FrameKind::RestSnapshot);
}

TEST_CASE("the audit stream is compared against, never applied") {
    // A partial replaces the top 20 and re-baselines nothing below it, so
    // adopting one would leave the levels outside it at whatever they had — a
    // book that is right at the touch and silently wrong underneath, which is
    // worse than one that is honestly stale.
    Adapter a = make();
    Events ev;
    a->on_rest_body(seed_body(10, level("100.00000000", "1.00000000"), ""), ev);
    const std::size_t before = ev.v.size();
    a->on_frame(seed_body(10, level("999.00000000", "7.00000000"), ""), ev);

    CHECK(a->stats().partial_frames == 1);
    CHECK(ev.v.size() == before);        // nothing emitted
    REQUIRE(a->bid_count() == 1);
    CHECK(a->bids()[0].px == 10000000000);  // still 100.0, not 999.0
}

TEST_CASE("a transport gap drops the book and asks for a seed") {
    Adapter a = make();
    Events ev;
    a->on_rest_body(seed_body(10, level("100.00000000", "1.00000000"), ""), ev);
    REQUIRE(a->has_baseline());
    a->on_transport_gap(GapReason::Disconnect, ev);

    CHECK_FALSE(a->has_baseline());
    CHECK(a->bid_count() == 0);
    CHECK(a->reseed_wanted());
    REQUIRE(ev.count(FeedEvent::Kind::Gap) == 1);
    CHECK(ev.v.back().reason == GapReason::Disconnect);
}
