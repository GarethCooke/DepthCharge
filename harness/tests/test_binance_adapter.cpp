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

#include "dc_harness/replay_driver.hpp"
#include "dc_harness/trace.hpp"
#include "dc_harness/venue.hpp"

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

// The 221 s calibration capture, replayed. Four cases below need it and each
// used to spell the path and the options itself; one spelling means a re-cut of
// that file cannot leave three cases reading it and one reading something else.
std::string liveness_trace_path() {
    return std::string(DC_REPLAY_DIR) + "/" + DC_BINANCE_LIVENESS_TRACE;
}

dc::harness::ReplayResult liveness_replay() {
    return dc::harness::run_replay_file(
        liveness_trace_path(), depthcharge::binance::kBinanceAtomEur.spec,
        dc::harness::ReplayOptions{});
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

// ---------------------------------------------------------------------------
// THE SILENT-STREAM DEFECT FIXTURE
// ---------------------------------------------------------------------------
//
// ============================ READ THIS FIRST ==============================
// **THIS CASE PINS A DEFECT, NOT A CONTRACT, AND IT IS EXPECTED TO INVERT.**
//
// A test that asserts broken behaviour is a specification to whoever reads it
// next, so it gets the expiry treatment ARCHITECTURE §9 already uses for the
// untracked-evidence clause on the 2026-08-25 audit-stream ruling:
//
//   * WHAT IT PINS. Today, replaying `binance_btcusdt_DEFECT_silent_stream_...`
//     produces a populated, COLOURED, **LIVE** ladder over a feed that has never
//     spoken. That is invariant #5's one forbidden output — a frozen ladder that
//     looks live — and this file is the first artefact in this project's history
//     that can produce it ON DEMAND. M3's frozen ladder was real but incidental;
//     this one is staged and replayable.
//   * WHY IT IS ASSERTED RATHER THAN LEFT ALONE. Whichever of DESIGN strain 26's
//     four remedies C picks, it must have a red-before-green. Without this case
//     the remedy ships against an argument instead of a measurement, and strain
//     26 closes on the same.
//   * **WHEN C LANDS A REMEDY, THIS CASE MUST FAIL, AND THE CORRECT RESPONSE IS
//     TO INVERT IT — NOT TO DELETE IT AND NOT TO RELAX IT.** Flip the two
//     assertions marked THE LIE below to `Stale` and a non-empty episode list,
//     and the fixture becomes the remedy's regression test in the same commit
//     that makes it pass.
//   * **EXPIRY.** If C ships without inverting this, the fixture does NOT lapse:
//     it moves to whichever stage next touches liveness, with this clause
//     attached, exactly as the audit-stream ruling's untracked-evidence clause
//     moves rather than lapsing. A defect fixture nobody ever flipped is a
//     defect nobody ever fixed, and it should be as hard to lose as the ruling
//     it is evidence for.
// ===========================================================================
TEST_CASE("the silent stream draws a LIVE ladder over a feed that never spoke") {
    const std::string path = std::string(DC_REPLAY_DIR) + "/" + DC_BINANCE_FIXTURE;
    depthcharge::SymbolSpec spec = kBinanceBtcUsdt.spec;
    const dc::harness::ReplayResult r =
        dc::harness::run_replay_file(path, spec, dc::harness::ReplayOptions{});

    // What the capture contains: a seed, three pings, and NOTHING ELSE. If these
    // move, the fixture has been re-captured and every claim below is about a
    // different file.
    REQUIRE(r.meta.venue == dc::harness::Venue::Binance);
    CHECK(r.binance.rest_snapshots == 1);
    CHECK(r.binance.diff_frames == 0);
    CHECK(r.binance.partial_frames == 0);
    CHECK(r.liveness_arrivals == 3);

    // THE MECHANISM, and it is the emission point rather than a missing
    // detector. `adopt_seed()` emits the Snapshot from the REST body before any
    // WebSocket event arrives, so the engine's book initialises off the seed
    // alone — while `bracket_checked_` stays false for ever, because no diff
    // ever comes to satisfy it.
    CHECK(r.binance.seed_bracket_ok == 0);
    CHECK(r.binance.seed_bracket_failed == 0);
    CHECK(r.book.snapshots_adopted == 1);
    CHECK(r.binance.seq_breaks == 0);

    // The liveness clock was fed normally throughout and never fired. That is
    // the 2026-08-25 ruling working exactly as written, on a signal that proves
    // the socket and never the feed.
    CHECK(r.liveness_median_ms > 19000.0);
    CHECK(r.liveness_median_ms < 21000.0);

    // ---- THE LIE. These two are the ones C's remedy must flip. ----
    CHECK(r.final_snapshot.status == depthcharge::FeedStatus::Live);
    CHECK(r.episodes.empty());
    // ...and it is a POPULATED ladder, not an empty one. An empty book drawn
    // live would be bad; a full one is worse, because it looks like a market.
    CHECK(r.final_snapshot.bid_count > 0);
    CHECK(r.final_snapshot.ask_count > 0);
}

// ---------------------------------------------------------------------------
// The re-snapshot trigger (M5 stage B2)
// ---------------------------------------------------------------------------
//
// **NO COMMITTED CAPTURE EXERCISES THE FIRING PATH, AND THAT IS A FACT ABOUT
// THE CORPUS RATHER THAN A GAP IN IT.** Measured across all nine Binance files:
// every `limit=1000` seed keeps coverage at 771 or better against a trigger at
// 448, so the trigger correctly never fires; every `limit=100` seed arrives
// below its own margin and is correctly never armed. So the trigger is real on
// the corpus in the only two ways the corpus can show it — it stays silent where
// it should — and the crossing itself has to be synthesised.
//
// That is ARCHITECTURE §9's 2026-08-18 rule applied exactly as written: where
// the code and every available file agree, synthesise the input that
// discriminates. Same reason `slice_trace.py --selfcheck` takes no trace.

namespace {

// A price on BTCUSDT's 0.01 tick, at the venue's 8-decimal scale.
std::string px_at(std::int64_t cents) {
    return std::to_string(cents / 100) + "." +
           (cents % 100 < 10 ? "0" : "") + std::to_string(cents % 100) + "000000";
}

// `n` levels a side: bids descending from `top_cents`, asks ascending from
// `top_cents + 1`. Deep enough to arm the trigger when `n` says so.
std::string deep_seed(std::int64_t last_id, std::int64_t top_cents, int n) {
    std::string bids;
    std::string asks;
    for (int i = 0; i < n; ++i) {
        if (i) {
            bids += ",";
            asks += ",";
        }
        bids += level(px_at(top_cents - i).c_str(), "1.00000000");
        asks += level(px_at(top_cents + 1 + i).c_str(), "1.00000000");
    }
    return seed_body(last_id, bids, asks);
}

constexpr std::int64_t kTopCents = 5000000;  // $50,000.00
constexpr int kDeepLevels = 500;             // >= the 448 trigger, so armed

// ONE STEP OF THE WALK THAT MATTERS, and it is a helper because all three cases
// below need exactly it: retire the best bid and introduce one new price below
// the seeded floor. That is the shape the wire actually has, and it is what
// makes held depth CONSTANT while coverage falls by one — the whole distinction
// under test. `id` advances by two so every frame brackets the last.
void retire_best_bid(BinanceAdapter& a, Events& ev, int step, int seed_levels,
                     std::int64_t& id) {
    const std::int64_t floor_cents = kTopCents - (seed_levels - 1);
    const std::string gone = level(px_at(kTopCents - step).c_str(), "0.00000000");
    const std::string fresh = level(px_at(floor_cents - 1 - step).c_str(), "1.00000000");
    a.on_frame(diff(id + 1, id + 2, gone + "," + fresh, ""), ev);
    id += 2;
}

}  // namespace

TEST_CASE("seeded coverage erodes where held depth does not, and the trigger fires on it") {
    // THE MEASUREMENT THAT MOTIVATED THIS CODE, TURNED INTO AN ASSERTION.
    // B1's `min_bid_levels` counts levels HELD and reads a flat, healthy 100
    // through both committed captures in which the bid side walks clean out of
    // the seeded range. It cannot fall: every diff that removes a level near the
    // touch arrives alongside others adding prices the seed never contained.
    // Reproduced here by construction — one removal and one new deeper price per
    // frame, so held is CONSTANT to the level while coverage falls to zero.
    Adapter a = make();
    Events ev;
    a->on_rest_body(deep_seed(1000, kTopCents, kDeepLevels), ev);

    REQUIRE(a->has_baseline());
    REQUIRE(a->cover_known());
    REQUIRE(a->cover_trigger_armed());
    CHECK(a->stats().seeds_below_margin == 0);
    CHECK(a->bid_cover() == kDeepLevels);
    CHECK(a->ask_cover() == kDeepLevels);
    CHECK(a->stats().cover_triggers == 0);
    CHECK_FALSE(a->reseed_wanted());

    const std::uint32_t held_at_seed = a->bid_count();
    std::int64_t id = 1000;
    bool held_ever_fell = false;

    for (int step = 0; step < kDeepLevels; ++step) {
        retire_best_bid(*a, ev, step, kDeepLevels, id);
        if (a->bid_count() != held_at_seed) { held_ever_fell = true; }
        // COVERAGE FALLS, by exactly one a frame.
        CHECK(a->bid_cover() == static_cast<std::uint32_t>(kDeepLevels - step - 1));
    }

    // HELD NEVER FELL — one out, one in, every frame.
    CHECK_FALSE(held_ever_fell);
    CHECK(a->stats().min_bid_levels == held_at_seed);
    // B2's instrument saw it go to zero, and asked for a seed on the way down.
    CHECK(a->stats().min_bid_cover == 0);
    CHECK(a->stats().cover_triggers == 1);
    CHECK(a->reseed_wanted());
    // The ask side never moved, so it is not what fired.
    CHECK(a->stats().min_ask_cover == static_cast<std::uint32_t>(kDeepLevels));
}

TEST_CASE("the trigger fires at the sized threshold and only once per seed") {
    Adapter a = make();
    Events ev;
    a->on_rest_body(deep_seed(1000, kTopCents, kDeepLevels), ev);
    REQUIRE(a->cover_trigger_armed());

    const int to_threshold =
        kDeepLevels - static_cast<int>(depthcharge::binance::kBinanceReseedCoverLevels);
    REQUIRE(to_threshold > 0);
    std::int64_t id = 1000;
    auto retire_one = [&](int step) { retire_best_bid(*a, ev, step, kDeepLevels, id); };

    // One short of the threshold: coverage is exactly kBinanceReseedCoverLevels,
    // which is `>=` and therefore NOT a crossing. The boundary is asserted in
    // both directions, because a trigger that fires one level early and one that
    // fires one level late are otherwise the same green test.
    for (int step = 0; step < to_threshold; ++step) { retire_one(step); }
    CHECK(a->bid_cover() == depthcharge::binance::kBinanceReseedCoverLevels);
    CHECK(a->stats().cover_triggers == 0);
    CHECK_FALSE(a->reseed_wanted());

    // One more crosses it.
    retire_one(to_threshold);
    CHECK(a->bid_cover() == depthcharge::binance::kBinanceReseedCoverLevels - 1);
    CHECK(a->stats().cover_triggers == 1);
    CHECK(a->reseed_wanted());

    // ONCE PER SEED EPOCH, not once per frame. The adapter has no clock and
    // cannot rate-limit itself; a re-fetch on every frame while coverage stayed
    // low would spend 50 IP weight ten times a second, and the venue bans on
    // breach. Bounding the SEEDS is the transport's job and is recorded as a
    // required property of it.
    a->clear_reseed_wanted();
    for (int step = to_threshold + 1; step < to_threshold + 40; ++step) {
        retire_one(step);
    }
    CHECK(a->stats().cover_triggers == 1);
    CHECK_FALSE(a->reseed_wanted());
}

TEST_CASE("a seed that arrives below its own margin is reported, never re-asked") {
    // THE SIZING RESULT, STATED AS A STATE. A 100-level seed cannot satisfy a
    // 448-level margin however often it is fetched, so asking again would return
    // the identical shortfall at 50 IP weight a time. On BTCUSDT at limit=100
    // this fires on arrival — roughly a minute before the book actually goes
    // wrong — which is the 82.4% failure class caught at the seed rather than at
    // the ladder.
    Adapter a = make();
    Events ev;
    a->on_rest_body(deep_seed(1000, kTopCents, 100), ev);

    REQUIRE(a->has_baseline());
    CHECK(a->cover_known());                // the bounds are still measured...
    CHECK_FALSE(a->cover_trigger_armed());  // ...and the trigger is not armed on them
    CHECK(a->stats().seeds_below_margin == 1);
    CHECK(a->stats().cover_triggers == 0);
    CHECK_FALSE(a->reseed_wanted());

    // Coverage is still REPORTED as it collapses. That is the whole diagnostic
    // value of the committed limit=100 captures, where it reaches zero.
    std::int64_t id = 1000;
    for (int step = 0; step < 100; ++step) {
        a->on_frame(
            diff(id + 1, id + 2, level(px_at(kTopCents - step).c_str(), "0.00000000"), ""),
            ev);
        id += 2;
    }
    CHECK(a->bid_cover() == 0);
    CHECK(a->stats().min_bid_cover == 0);
    // Still no request: a deeper seed is the answer, and this adapter cannot ask
    // for one it is not configured to fetch.
    CHECK(a->stats().cover_triggers == 0);
}

TEST_CASE("the seeded bounds die with the book") {
    Adapter a = make();
    Events ev;
    a->on_rest_body(deep_seed(1000, kTopCents, kDeepLevels), ev);
    REQUIRE(a->cover_known());
    REQUIRE(a->cover_trigger_armed());

    a->on_transport_gap(GapReason::Disconnect, ev);

    // A coverage count against the dead seed's floor, taken over levels the next
    // seed will replace outright, is a number about nothing.
    CHECK_FALSE(a->cover_known());
    CHECK_FALSE(a->cover_trigger_armed());
    CHECK(a->bid_cover() == 0);
    CHECK(a->ask_cover() == 0);
}

TEST_CASE("a re-snapshot on a live book is counted, and its loss is measured") {
    // B1 discarded these in silence. B2 does not adopt them either — rolling one
    // forward needs the diffs it is behind by, and buffering those for a 15 s
    // fetch deadline is ~128 KiB, so the mechanism is D's. What this stage owes D
    // is the measurement: how far behind the stream a body actually is when it
    // lands, because that is what decides whether a buffer is needed at all.
    Adapter a = make();
    Events ev;
    a->on_rest_body(deep_seed(1000, kTopCents, kDeepLevels), ev);
    a->on_frame(diff(1001, 1010, level("49000.00000000", "1.00000000"), ""), ev);
    REQUIRE(a->last_update_id() == 1010);

    // Behind the stream: adopting would rewind the book past update ids this
    // client has already applied and no longer holds the diffs for.
    a->on_rest_body(deep_seed(1005, kTopCents, kDeepLevels), ev);
    CHECK(a->stats().resnapshots_declined == 1);
    CHECK(a->stats().resnapshots_adoptable == 0);

    // Not behind: nothing sits between the body and the book, so adopting would
    // lose nothing. Measured at 13 of 13 across the quiet pair and every
    // limit=100 fetch in the corpus, and 0 of 7 at limit=1000 on BTCUSDT.
    a->on_rest_body(deep_seed(1010, kTopCents, kDeepLevels), ev);
    CHECK(a->stats().resnapshots_declined == 2);
    CHECK(a->stats().resnapshots_adoptable == 1);

    // Declining is not ignoring: the book is untouched and still the one the
    // diffs built.
    CHECK(a->last_update_id() == 1010);
    CHECK(a->has_baseline());
}

// ---------------------------------------------------------------------------
// The calibrated liveness path, reachable on the host for the first time
// (M5 stage B2, deliverable 6)
// ---------------------------------------------------------------------------
//
// **THE MECHANISM C IS BEING ASKED TO TUNE HAD NO TEST THAT COULD ENTER IT.**
// `kMinSamples = 8` intervals is ~160 s of wall clock at this venue's ~20 s ping
// cadence, and the longest committed Binance capture was 88 s with 5 pings — so
// every slice ran on `kUncalibratedThresholdMs` and the self-calibrating branch
// was dead code as far as the suite was concerned. C has to decide, against
// measurement, whether the ceiling rises, the multiplier falls, or the two
// become per-venue (ARCHITECTURE §9, 2026-08-25, stage A). None of those is a
// decision anybody should take against a path no test executes.
//
// `binance_atomeur_d100ms_liveness_20260826` exists only to close that: 221 s on
// the quiet pair, single-stream, one seed, 11 pings, deliberately not gradeable.
// 20 KiB.

TEST_CASE("the calibrated liveness path is reachable, and a committed trace enters it") {
    const dc::harness::ReplayResult r = liveness_replay();

    REQUIRE(r.meta.venue == dc::harness::Venue::Binance);
    // What the capture is. If these move it has been re-captured and every claim
    // below is about a different file — the length IS the property being pinned.
    CHECK(r.liveness_arrivals == 11);
    CHECK(r.span_seconds() > 160.0);
    REQUIRE(r.liveness_arrivals > depthcharge::kMinSamples);

    // THE ASSERTION THE FILE EXISTS FOR.
    CHECK(r.liveness_calibrated);

    // AND THE CONTROL, WITHOUT WHICH THE LINE ABOVE PROVES NOTHING. The longest
    // Binance capture that predates this one carries 5 pings — 4 intervals
    // against kMinSamples' 8 — and must NOT calibrate. A flag that is true
    // everywhere is not evidence that a path was entered.
    const dc::harness::ReplayResult before = dc::harness::run_replay_file(
        std::string(DC_REPLAY_DIR) + "/binance_atomeur_d100ms_20260824.ndjson",
        depthcharge::binance::kBinanceAtomEur.spec, dc::harness::ReplayOptions{});
    CHECK(before.liveness_arrivals == 5);
    CHECK_FALSE(before.liveness_calibrated);
}

TEST_CASE("...and what it calibrates to is 39,928 ms, which is a decision and not an inheritance") {
    // **THIS CASE WAS "...AND WHAT IT CALIBRATES TO IS INERT" UNTIL M5 STAGE C,
    // AND IT WENT RED EXACTLY AS ITS AUTHOR INTENDED.** B2 wrote it to assert an
    // inertness rather than observe one, so that whichever of the three shapes C
    // chose — ceiling rises, multiplier falls, both become per-venue — the suite
    // would refuse to stay green through it. It refused. What it asserted, kept
    // here because a rewritten test with no memory of what it used to say is a
    // test that lost its argument: 4.0 x 19,963.97 = 79,855.9 ms clamped to
    // `kThresholdCeilingMs` = 30,000, the identical number
    // `kUncalibratedThresholdMs` already held, so calibrating changed the
    // threshold by exactly zero and the self-calibration the 2026-08-17 ruling
    // rests on was a constant wearing a calibration's clothes.
    //
    // WHAT C DECIDED, AND THE ONE-LINE REASON. Both the multiplier and the
    // ceiling become per-venue (`dc_harness/venue.hpp`), and Binance's
    // multiplier is derived the way `liveness_clock.hpp` derives Anvil's rather
    // than inherited from it: the venue's worst HEALTHY inter-arrival as a
    // multiple of its own median, times ~2 of margin. Anvil 1.937x -> 4.0; this
    // signal 1.005x over these ten intervals -> 2.0. Same rule, third venue.
    //
    // **THE COST OF THE OTHER TWO SHAPES, STATED WHERE THE NUMBER LIVES.**
    // A global ceiling raised to clear 79,855.9 also raises
    // `kUncalibratedThresholdMs` — it IS the ceiling — at Anvil and Kraken, the
    // two venues where nothing is wrong. A global multiplier cannot reach a live
    // calibration here at all: it would have to fall to <= 1.503 to come under
    // today's 30,000 ceiling, which is below Anvil's measured 1.937x worst
    // healthy multiple and would grey that panel on one slipped `summary`.
    const dc::harness::ReplayResult r = liveness_replay();

    REQUIRE(r.liveness_calibrated);
    // 19,963.97 AND NOT 19,969.4, AND THE DIFFERENCE IS THE POINT.
    //
    // `LivenessClock` takes the LOWER median by nearest rank — the convention
    // `sample_window.hpp` says has one home, because an interpolated median
    // invents an interval that never occurred on the wire. Over these ten
    // intervals the two answer 19,964.0 and 19,969.4, and the first draft of
    // this test asserted the second with a relative epsilon wide enough to
    // accept the first. A tolerance that spans both candidate answers is a test
    // that cannot fail; this one is tight enough to name which instrument
    // produced the number.
    CHECK(r.liveness_median_ms == doctest::Approx(19963.97).epsilon(1e-6));

    // THE ASSERTION THIS CASE NOW EXISTS FOR: the calibrated branch was entered
    // and this is the number that came out of it. Written as the arithmetic
    // rather than as 39927.94, so a reader can see it is `multiple x median` and
    // not a figure somebody wrote down.
    const depthcharge::LivenessPolicy& p =
        dc::harness::venue_traits(dc::harness::Venue::Binance).liveness;
    CHECK(p.multiple == doctest::Approx(2.0));
    CHECK(r.threshold_ms == doctest::Approx(p.multiple * r.liveness_median_ms));
    CHECK(r.threshold_ms == doctest::Approx(39927.94).epsilon(1e-6));

    // AND IT IS NO LONGER CLAMPED, WHICH IS THE PROPERTY AND NOT THE VALUE. A
    // threshold that equals its own ceiling tells you nothing about the venue;
    // this one is strictly inside floor and ceiling, so it tracks the signal's
    // observed cadence the way the 2026-08-17 ruling requires. If a later change
    // makes it touch either bound again, that is the inertness returning and
    // this line is what says so.
    CHECK(r.threshold_ms > p.floor_ms);
    CHECK(r.threshold_ms < p.ceiling_ms);

    // The two venues that were already right are untouched, by construction:
    // their rows are the shipping defaults, so a regression cannot be
    // attributed to this change because there is nothing to attribute
    // (ARCHITECTURE §9, 2026-08-25).
    for (const dc::harness::Venue v :
         {dc::harness::Venue::Anvil, dc::harness::Venue::Kraken}) {
        const depthcharge::LivenessPolicy& q = dc::harness::venue_traits(v).liveness;
        CHECK(q.multiple == doctest::Approx(depthcharge::kThresholdMultiple));
        CHECK(q.floor_ms == doctest::Approx(depthcharge::kThresholdFloorMs));
        CHECK(q.ceiling_ms == doctest::Approx(depthcharge::kThresholdCeilingMs));
    }

    // AND THE DEFERRED MEDIAN CONVENTION (strain 29) CANNOT REACH THIS
    // DECISION, which is the argument that the M5 close-out's deferral costs
    // this stage nothing. Nearest rank gives 19,963.97 and the interpolated copy
    // in `trace.cpp` gives 19,969.35; at a multiplier of 2.0 the two thresholds
    // differ by 10.8 ms, 0.027% — and both land strictly inside the same bounds,
    // so no branch anywhere reads them differently.
    const dc::harness::TraceStats t = dc::harness::read_trace(liveness_trace_path());
    const double other = p.multiple * t.median_liveness_gap_ms;
    CHECK(other > p.floor_ms);
    CHECK(other < p.ceiling_ms);
    CHECK(other - r.threshold_ms < 11.0);

    // And it never fires, over 221 s containing a 26.8 s book silence on a
    // socket whose pings never missed a beat. The Binance twin of Kraken's
    // MINA/GBP 25,843 ms hole, and the same verdict: market information, not the
    // book's age, and never a grey signal.
    CHECK(r.episodes.empty());
}

TEST_CASE("the report's median and the clock's median are two conventions, and they differ here") {
    // **PINS A DIVERGENCE, NOT A CONTRACT, AND IS EXPECTED TO INVERT.** Same
    // shape as the silent-stream fixture's expiry clause, and for the same
    // reason: the thing recorded is wrong, and a test is how the correction
    // announces itself instead of going unnoticed.
    //
    // `sample_window.hpp` says the median convention "matters enough to have one
    // home" — a LOWER median by nearest rank, because an interpolated one
    // invents an interval that never occurred on the wire — and
    // `liveness_clock.hpp` and `age_estimator.hpp` both use it.
    // **`harness/src/trace.cpp`'s statistics pass carries a second copy and
    // interpolates.** The two agree to 0.1 ms at Anvil and Kraken, whose liveness
    // cadences are flat, so the divergence was invisible for three milestones —
    // the coincidence class again. At Binance they disagree on every committed
    // capture with an even interval count, and **every Binance cadence figure
    // this project has quoted came from the interpolated one**: 19,951.7,
    // 20,011.6, 20,013.3 in `taxonomy_pins.inc`, and 20,004.8 in B1's log.
    //
    // **NOT FIXED HERE, AND THE REASON IS GOLDEN MOVEMENT RATHER THAN SWEEP
    // SIZE.** Adopting the shared convention rewrites the cadence figures quoted
    // inside `taxonomy_pins.inc` — and *no existing golden moves* is precisely
    // what makes a seven-commit split reviewable. Bundled into this stage,
    // nothing in the diff would distinguish a convention change from a defect.
    // **A convention change that moves pins must be its own stage, so that the
    // moved pins have nothing else in the diff to hide behind.**
    //
    // OWNER: **M5 close-out** — not C, whose evening is already the threshold,
    //   the ceiling's changed role, the panel decisions and strain 26's four
    //   unbuilt remedies.
    // EXPIRY: when `harness/` and `engine/` compute the median by one convention.
    // TRIPWIRE: if any stage before the close-out needs to quote or re-pin a
    //   Binance cadence figure, this closes first.
    //
    // **THE CORRECT RESPONSE IS TO INVERT THIS CASE, NOT TO DELETE OR RELAX IT.**
    // If the close-out ships without inverting it, the clause moves to whichever
    // stage next touches a cadence figure rather than lapsing. Carried in three
    // places — here, `taxonomy_pins.inc`'s Binance comment, and DESIGN strain 29
    // — which is the shape the silent-stream fixture already uses.
    // ARCHITECTURE §9, 2026-08-26.
    const dc::harness::ReplayResult r = liveness_replay();
    const dc::harness::TraceStats t = dc::harness::read_trace(liveness_trace_path());

    // Same signal, same ten intervals, same file.
    CHECK(r.liveness_arrivals == 11);
    CHECK(t.liveness_events == 11);

    CHECK(r.liveness_median_ms == doctest::Approx(19963.97).epsilon(1e-6));
    CHECK(t.median_liveness_gap_ms == doctest::Approx(19969.35).epsilon(1e-6));
    CHECK(r.liveness_median_ms != doctest::Approx(t.median_liveness_gap_ms));
}
