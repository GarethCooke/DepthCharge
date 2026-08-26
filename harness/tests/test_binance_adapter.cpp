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
    const std::string path =
        std::string(DC_REPLAY_DIR) + "/" + DC_BINANCE_LIVENESS_TRACE;
    depthcharge::SymbolSpec spec = depthcharge::binance::kBinanceAtomEur.spec;
    const dc::harness::ReplayResult r =
        dc::harness::run_replay_file(path, spec, dc::harness::ReplayOptions{});

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
        std::string(DC_REPLAY_DIR) + "/binance_atomeur_d100ms_20260824.ndjson", spec,
        dc::harness::ReplayOptions{});
    CHECK(before.liveness_arrivals == 5);
    CHECK_FALSE(before.liveness_calibrated);
}

TEST_CASE("...and what it calibrates to is INERT, which is the measurement C needs") {
    // The ping is the most metronomic signal of the three venues, and this
    // capture measures it over ten intervals rather than four: 19,957.0 to
    // 20,068.0 ms, worst/median 1.005 against Kraken's 1.12.
    //
    // Four times that is ~79,900 ms and the clock clamps at
    // `kThresholdCeilingMs = 30,000` — which is the identical number
    // `kUncalibratedThresholdMs` already held. So calibrating changes the
    // threshold by exactly zero, and the self-calibration the 2026-08-17 ruling
    // rests on is, at this venue, a constant wearing a calibration's clothes.
    //
    // **THIS IS ASSERTED RATHER THAN OBSERVED SO THAT C'S CHANGE BREAKS IT.**
    // Raise the ceiling, or drop the multiplier below ~1.5, and this test goes
    // red and has to be rewritten deliberately — which is the whole point of
    // pinning an inertness. A run that merely reported 30,000 ms would stay
    // green through either change and say nothing.
    const std::string path =
        std::string(DC_REPLAY_DIR) + "/" + DC_BINANCE_LIVENESS_TRACE;
    depthcharge::SymbolSpec spec = depthcharge::binance::kBinanceAtomEur.spec;
    const dc::harness::ReplayResult r =
        dc::harness::run_replay_file(path, spec, dc::harness::ReplayOptions{});

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
    CHECK(r.liveness_median_ms * depthcharge::kThresholdMultiple >
          depthcharge::kThresholdCeilingMs);
    CHECK(r.threshold_ms == doctest::Approx(depthcharge::kThresholdCeilingMs));
    CHECK(depthcharge::kUncalibratedThresholdMs ==
          doctest::Approx(depthcharge::kThresholdCeilingMs));

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
    // NOT FIXED HERE, deliberately: `median_gap_ms` shares that code path and is
    // quoted across three NOTES files and a dozen briefs, so changing the
    // convention is a documentation sweep with its own scope, not a line in a
    // stage about the re-snapshot schedule. When it is fixed, this case must be
    // INVERTED rather than deleted or relaxed. ARCHITECTURE §9, 2026-08-26.
    const std::string path =
        std::string(DC_REPLAY_DIR) + "/" + DC_BINANCE_LIVENESS_TRACE;
    depthcharge::SymbolSpec spec = depthcharge::binance::kBinanceAtomEur.spec;
    const dc::harness::ReplayResult r =
        dc::harness::run_replay_file(path, spec, dc::harness::ReplayOptions{});
    const dc::harness::TraceStats t = dc::harness::read_trace(path);

    // Same signal, same ten intervals, same file.
    CHECK(r.liveness_arrivals == 11);
    CHECK(t.liveness_events == 11);

    CHECK(r.liveness_median_ms == doctest::Approx(19963.97).epsilon(1e-6));
    CHECK(t.median_liveness_gap_ms == doctest::Approx(19969.35).epsilon(1e-6));
    CHECK(r.liveness_median_ms != doctest::Approx(t.median_liveness_gap_ms));
}
