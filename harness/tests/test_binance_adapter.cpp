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

#include <depthcharge/age_estimator.hpp>
#include <depthcharge/anvil/anvil_adapter.hpp>
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
    // **NO SNAPSHOT EVER WENT OUT, AND THAT IS THE M5 STAGE C CHANGE.** This
    // case used to assert Snapshot-then-Gap — "what we adopted did happen, and
    // the Gap says it cannot be trusted" — which was the honest order for a
    // baseline that had already been published. It is a strictly better order
    // not to publish it: the bracket is the feed's corroboration, this seed
    // never got one, and a Snapshot the engine never received cannot be a
    // coloured ladder for the width of one event. The Gap still goes out,
    // because `drop_book` cannot know what the engine is holding.
    CHECK(ev.count(FeedEvent::Kind::Snapshot) == 0);
    CHECK(a->stats().seeds_unconfirmed == 1);
    REQUIRE(ev.count(FeedEvent::Kind::Gap) == 1);
    CHECK(ev.v.back().reason == GapReason::SeqGap);
}

TEST_CASE("the OTHER bracket site answers the same way — no buffered event, first diff misses") {
    // **THE SAME EVENT REACHES `drop_book` DOWN TWO PATHS, AND THEY MUST NOT
    // DISAGREE.** The case above brackets against a surviving BUFFERED event, in
    // `replay_buffer`; this one has nothing buffered, so the first diff after the
    // seed plays the survivor's role in `check_continuity`. Both are "a seed the
    // feed never confirmed", so both must publish nothing, raise `SeqGap` and
    // count one `seeds_unconfirmed`.
    //
    // It is a case rather than a line in the one above because that symmetry did
    // not hold when it was written: `check_continuity` set `bracket_checked_`
    // BEFORE testing, so `drop_book` — which counts off exactly that flag —
    // arrived with it already true and counted nothing. Harmless for three
    // milestones, because until M5 stage C nothing read the flag on the way out.
    Adapter a = make();
    Events ev;
    a->on_rest_body(seed_body(100, level("100.00000000", "1.00000000"), ""), ev);
    REQUIRE(a->has_baseline());
    REQUIRE_FALSE(a->seed_confirmed());
    // Nothing was buffered, so no bracket has been attempted yet and no Snapshot
    // has gone out.
    CHECK(ev.v.empty());

    // U = 200 leaves a hole: the seed named 100 and this event does not span 101.
    a->on_frame(diff(200, 201, level("100.00000000", "2.00000000"), ""), ev);

    CHECK(a->stats().seed_bracket_failed == 1);
    CHECK(a->stats().seed_bracket_ok == 0);
    CHECK(a->stats().seeds_unconfirmed == 1);
    CHECK(ev.count(FeedEvent::Kind::Snapshot) == 0);
    REQUIRE(ev.count(FeedEvent::Kind::Gap) == 1);
    CHECK(ev.v.back().reason == GapReason::SeqGap);
    CHECK_FALSE(a->has_baseline());
    CHECK(a->reseed_wanted());
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
    // CONFIRM THE SEED FIRST (M5 stage C). The Snapshot is published when a diff
    // brackets `lastUpdateId + 1`, so measuring `before` straight after the REST
    // body would put the seed's own Snapshot inside the frame under test and
    // this case would be counting it instead of the removal.
    a->on_frame(diff(11, 11, "", ""), ev);
    REQUIRE(a->seed_confirmed());
    const std::size_t before = ev.v.size();

    a->on_frame(diff(12, 13, level("42.00000000", "0.00000000"), ""), ev);

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
    // Same reason as the case above: the seed publishes on the bracketing diff.
    a->on_frame(diff(11, 11, "", ""), ev);
    REQUIRE(a->seed_confirmed());
    const std::size_t before = ev.v.size();
    a->on_frame(diff(12, 13, level("100.00000000", "1.00000000"), ""), ev);
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
// THE SILENT-STREAM FIXTURE — INVERTED AT M5 STAGE C, IN THE COMMIT THAT MADE
// THE REMEDY PASS, WHICH IS WHAT ITS OWN EXPIRY CLAUSE REQUIRED
// ---------------------------------------------------------------------------
//
// ============================ READ THIS FIRST ==============================
// **THIS CASE PINNED A DEFECT AND NOW PINS A CONTRACT.** It was written at B2
// asserting today's broken behaviour, with the instruction that when C landed
// any of DESIGN strain 26's four remedies the case MUST fail and the correct
// response was to INVERT it — not to delete it and not to relax it. It failed.
// The two assertions marked THE LIE are flipped below and nothing else about
// the case has moved, so what it now asserts is a red-before-green over the
// same file rather than a fresh claim.
//
// WHAT IT USED TO SAY, kept because a fixture with no memory of its own defect
// is just a passing test: replaying `binance_btcusdt_DEFECT_silent_stream_...`
// produced a populated, COLOURED, **LIVE** 100-level ladder over a feed that had
// never spoken — invariant #5's one forbidden output, staged and replayable, the
// first artefact in this project's history that could produce it on demand.
//
// THE FILE IS STILL NAMED `DEFECT`, and deliberately. It is a capture of a real
// defective CONDITION on the wire — a misspelled stream that returns HTTP 101,
// answers its pings and delivers nothing — and that condition is exactly as real
// after the remedy as before it. What changed is what DepthCharge does with it.
// Renaming it would cost the provenance and buy a word.
// ===========================================================================
TEST_CASE("the silent stream is grey, because a seed the feed never confirmed is never published") {
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

    // THE MECHANISM WAS THE EMISSION POINT RATHER THAN A MISSING DETECTOR, and
    // that is why the remedy is one line moving rather than a new object.
    // `adopt_seed()` used to emit the Snapshot from the REST body before any
    // WebSocket event arrived; it now holds it until a diff brackets
    // `lastUpdateId + 1`. Here no diff ever comes, so the bracket is never
    // satisfied and the Snapshot is never emitted.
    CHECK(r.binance.seed_bracket_ok == 0);
    CHECK(r.binance.seed_bracket_failed == 0);
    CHECK(r.book.snapshots_adopted == 0);
    CHECK(r.binance.seq_breaks == 0);

    // The liveness clock was fed normally throughout and never fired. That is
    // the 2026-08-25 ruling working exactly as written, on a signal that proves
    // the socket and never the feed — UNCHANGED BY THE REMEDY, and the reason
    // this case still matters. Nothing here detected anything; the panel is grey
    // because it was never given grounds to be anything else.
    CHECK(r.liveness_median_ms > 19000.0);
    CHECK(r.liveness_median_ms < 21000.0);
    CHECK(r.episodes.empty());   // no Gap, no firing — there was nothing to lose

    // ---- WHAT THE LIE BECAME. ----
    CHECK(r.final_snapshot.status == depthcharge::FeedStatus::Stale);
    // ...and the ladder is EMPTY rather than populated. Both halves are the
    // assertion: a full ladder drawn live is worse than an empty one, because it
    // looks like a market.
    CHECK(r.final_snapshot.bid_count == 0);
    CHECK(r.final_snapshot.ask_count == 0);

    // AND IT IS "NOTHING YET" RATHER THAN "NOTHING THERE", which is the
    // distinction `initialised` was published for at M4 stage C and the one D
    // needs in order to draw this state at all. A book that has been told
    // nothing and a book told the market is empty both read `bid_count == 0`,
    // and only the second is a statement anybody made.
    CHECK_FALSE(r.final_snapshot.initialised);
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

// ---------------------------------------------------------------------------
// THE AGE METER'S THIRD PER-VENUE COST, PINNED RATHER THAN DESCRIBED
// (M5 stage C, deliverable 5 — measured at B2, decided here)
// ---------------------------------------------------------------------------

TEST_CASE("age_ms has NO READING for the first 638.8 s of a Binance connection") {
    // ARCHITECTURE §9, 2026-08-26: a sample count in a venue-agnostic object is
    // a per-venue duration. `kBaselineSamples = 32` intervals is 16 s at Anvil,
    // 32 s at Kraken and **638.8 s at Binance** — over ten minutes, on every
    // connection, during which the header can show no number at all.
    //
    // **THE STATE THAT SAYS SO ALREADY EXISTS AND C DID NOT ADD ONE.**
    // `has_age` is exactly M4 stage A2's *no reading yet*, published for the
    // reason A2 gives: "no reading yet" and "the book is current" are different
    // statements and exactly one of them is reassuring. So C's whole job on item
    // 7 was to check that the engine state D needs is already there and to pin
    // the per-venue duration as a behaviour rather than a comment. **What the
    // header LOOKS like in that state is D's**, by this stage's scoping ruling.
    const dc::harness::ReplayResult r = liveness_replay();

    // The longest Binance capture in the corpus is 221 s, and the arithmetic
    // says the baseline needs 638.8 s — so the meter cannot possibly have
    // latched. Written as the inequality, so it stays true if the trace is
    // re-cut and becomes false only if the constant or the cadence moves.
    const double needed_ms =
        static_cast<double>(depthcharge::kBaselineSamples) * r.liveness_median_ms;
    CHECK(needed_ms > 638000.0);
    CHECK(needed_ms < 640000.0);
    CHECK(r.span_seconds() * 1000.0 < needed_ms);

    CHECK_FALSE(r.final_snapshot.has_age);
    CHECK(r.final_snapshot.age_ms == 0);
    CHECK(r.age_baseline_ms == doctest::Approx(0.0));

    // THE CONTROL, WITHOUT WHICH THE LINES ABOVE PROVE NOTHING. At Anvil the
    // same 32 intervals is 16 s, so a 2-minute capture latches comfortably and
    // the meter reads. A flag that is false everywhere is not evidence of a
    // per-venue cost — it is evidence of a broken meter.
    const dc::harness::ReplayResult anvil = dc::harness::run_replay_file(
        std::string(DC_REPLAY_DIR) + "/anvil_101_baseline.ndjson",
        depthcharge::anvil::kAnvilTicker101, dc::harness::ReplayOptions{});
    CHECK(anvil.final_snapshot.has_age);
    CHECK(anvil.age_baseline_ms > 0.0);
}

// ---------------------------------------------------------------------------
// THE RE-SEED-IN-FLIGHT STATE (M5 stage C, deliverable 3 — DESIGN strain 28)
// ---------------------------------------------------------------------------

TEST_CASE("a re-seed the adapter asked for and no layer served is PUBLISHED, not only counted") {
    // Strain 28: `reseed_wanted()` is a request nothing can answer on a live
    // book, because a `/api/v3/depth` body describes a past instant. B2 made the
    // adapter say so out loud on the report; C makes it say so in the state the
    // renderer reads, because D cannot choose what to draw during a fetch
    // without one — and the two candidate renderings differ in nothing a host
    // test can assert, which is the M4 triage split applied a third time.
    //
    // SYNTHESISED, and that is a fact about the corpus rather than a gap in it:
    // every committed capture that raises a Gap is re-seeded by the capture tool
    // moments later, so the published state passes through `Wanted` and out
    // again inside one file. The state at the END of such a file is `None`,
    // which is a true reading and not the one under test. Same reasoning as the
    // trigger-crossing case above: where the code and every available file
    // agree, synthesise the input that discriminates (ARCHITECTURE §9,
    // 2026-08-18).
    const std::string trace =
        R"({"captured_at":"2026-08-26T00:00:00Z","url":"wss://data-stream.binance.vision/ws/btcusdt@depth@100ms","venue":"binance","symbol":"BTCUSDT","tool_version":"0.1.0","clock":"perf_counter_ns"})"
        "\n"
        R"({"rx_ns":1000000,"kind":"rest","req":{"method":"GET","url":"https://data-api.binance.vision/api/v3/depth?symbol=BTCUSDT&limit=1000","limit":1000,"weight":50,"sent_ns":0,"status":200,"recv_ns":900000},"frame":{"lastUpdateId":100,"bids":[["100.00000000","1.00000000"]],"asks":[["101.00000000","1.00000000"]]}})"
        "\n"
        R"({"rx_ns":2000000,"frame":{"e":"depthUpdate","U":101,"u":101,"s":"BTCUSDT","b":[],"a":[]}})"
        "\n"
        R"({"rx_ns":3000000,"frame":{"e":"depthUpdate","U":200,"u":201,"s":"BTCUSDT","b":[],"a":[]}})"
        "\n";

    // Every published frame, in order, so the transition is asserted rather than
    // the end state alone.
    std::vector<depthcharge::ReseedState> seen;
    std::vector<depthcharge::FeedStatus> status;
    const dc::harness::ReplayResult r = dc::harness::run_replay_text(
        trace, kBinanceBtcUsdt.spec, dc::harness::ReplayOptions{},
        [&seen, &status](const dc::harness::ReplayStep&,
                         const depthcharge::DisplaySnapshot& s) {
            seen.push_back(s.reseed);
            status.push_back(s.status);
            return true;
        });

    // Two events: the Snapshot the bracketing diff released, and the Gap the
    // sequence hole raised.
    REQUIRE(seen.size() == 2);
    CHECK(r.binance.seed_bracket_ok == 1);
    CHECK(r.binance.seq_breaks == 1);

    // A live, bracketed book has nothing outstanding.
    CHECK(status[0] == depthcharge::FeedStatus::Live);
    CHECK(seen[0] == depthcharge::ReseedState::None);

    // ...and the moment the book is dropped, the adapter's unanswerable request
    // is visible to whatever draws.
    CHECK(status[1] == depthcharge::FeedStatus::Stale);
    CHECK(seen[1] == depthcharge::ReseedState::Wanted);
    CHECK(r.final_snapshot.reseed == depthcharge::ReseedState::Wanted);

    // **AND `InFlight` IS NEVER REACHED, WHICH IS THE CARD ITSELF.** Nothing in
    // this build issues a fetch, so the request is published and never
    // progresses. When D builds the adoption this line is what has to change,
    // and until then the state on the panel is the honest one: asked for, and
    // nobody answering.
    for (const depthcharge::ReseedState s : seen) {
        CHECK(s != depthcharge::ReseedState::InFlight);
    }
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

// ---------------------------------------------------------------------------
// D-A2 safety net: what a seed body is worth after the buffer has overflowed
// ---------------------------------------------------------------------------
//
// NOT A DESIGN GATE, AND RECORDED AS SUCH. M5 stage D-A2 sizes the pre-seed
// buffer to cover the whole 15 s fetch deadline (256 events / 32,768 levels
// against a measured worst 15 s window of 152 / 12,458), so on the corpus this
// case should not arise at all. This exists because "should not arise" is a
// claim about the market, and the transport's abandon-on-overflow rule rests on
// what the adapter does if it arises anyway.
//
// The question: a fetch is in flight, the buffer overflows, and the body then
// lands. Is that body still worth adopting, or is it dead on arrival?
//
// The mechanism, from `buffer_diff`: an overflow ZEROES the buffer
// (`buf_events_ = 0; buf_levels_ = 0`), raises `Gap{Overflow}` and re-latches
// `reseed_wanted_`. Diffs after it refill from a LATER point in the stream, so
// the body's `lastUpdateId` — named at an instant BEFORE the overflow — can no
// longer be adjacent to the first survivor. The bracket must therefore fail.
TEST_CASE("a seed body that arrives after a buffer overflow cannot bracket") {
    Adapter a = make();
    Events ev;

    // Fill the pre-seed buffer past `kBinanceBufferEvents` while a notional
    // fetch is in flight. Each diff carries one level a side so the LEVEL bound
    // is not what trips; this is the event bound, deliberately.
    const std::uint32_t over = depthcharge::binance::kBinanceBufferEvents + 4;
    for (std::uint32_t i = 0; i < over; ++i) {
        const std::int64_t U = 1000 + static_cast<std::int64_t>(i) * 2;
        a->on_frame(diff(U, U + 1, level("100.00", "1.0"), level("101.00", "1.0")),
                    [&ev](const FeedEvent& e) { ev(e); });
    }
    REQUIRE(a->stats().buffer_overflows > 0);
    CHECK(a->stats().reseeds_requested > 0);
    CHECK(ev.count(FeedEvent::Kind::Gap) > 0);

    // The body now lands, naming an instant from BEFORE the overflow.
    const std::size_t bracket_ok_before = a->stats().seed_bracket_ok;
    a->on_rest_body(seed_body(1001, level("100.00", "2.0"), level("101.00", "2.0")),
                    [&ev](const FeedEvent& e) { ev(e); });

    // ANSWERED, AND THE ANSWER IS SHARPER THAN "IT DOES NOT BRACKET".
    //
    // The body IS adopted — `adopt_seed` runs unconditionally while the adapter
    // is `Unseeded`, sets the ladder and latches the seeded bounds — and then
    // `replay_buffer` immediately walks the post-overflow buffer, whose first
    // survivor is no longer adjacent to `lastUpdateId + 1`. That raises
    // `Gap{SeqGap}` through `drop_book`, which reverts `seed_` to `Unseeded`
    // and re-latches the request. So the seed is not merely unconfirmed, it is
    // SPENT: 50 IP weight bought a ladder that lived for the duration of one
    // function call.
    CHECK_FALSE(a->has_baseline());
    CHECK(a->stats().seed_bracket_ok == bracket_ok_before);
    CHECK_FALSE(a->seed_confirmed());

    // `seeds_unconfirmed` is the counter M5 stage D-A1's brief asked the board
    // for and could not get, because that build had no REST client and so never
    // reached `Seeded`. This is the first path in the suite that exercises it.
    CHECK(a->stats().seeds_unconfirmed > 0);

    // And the adapter is still asking, which is what lets the transport retry
    // rather than sit grey over a healthy socket.
    CHECK(a->reseed_wanted());
}
