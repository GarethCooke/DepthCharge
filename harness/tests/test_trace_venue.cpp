// test_trace_venue.cpp — the M4 stage-A metadata contract and decoder seam.
//
// Everything here is new behaviour from stage A. The pre-existing rules it
// touches are asserted too, in the form "still true at Anvil", because the
// whole claim of this stage is that a second venue cost the first one nothing:
//
//   * `venue` is additive and an absent tag reads as anvil;
//   * `ticker` is venue-conditional and `symbol` is Kraken's half of it;
//   * a frame with no string `type` is still fatal at Anvil and legal at Kraken;
//   * an unknown venue is a DIFFERENT failure from a malformed trace;
//   * the clock is surfaced, never inferred;
//   * `"dir": "tx"` is a record we sent, counted but never timed;
//   * the resync rule is one rule and gives Anvil's old answers;
//   * the replay driver refuses a venue it has no adapter for;
//   * the sink and decoder contracts are what stage B has to fit.
//
// The committed-file counts are NOT re-asserted here — dc_taxonomy's pin table
// owns those, and duplicating them would mean two places to update when a trace
// is re-captured. These are synthetic traces exercising the rules.
#include <doctest/doctest.h>

#include <cstdio>
#include <initializer_list>
#include <utility>
#include <vector>
#include <string>
#include <type_traits>

#include "dc_harness/liveness_clock.hpp"
#include "dc_harness/replay_driver.hpp"
#include "dc_harness/trace.hpp"
#include "dc_harness/trace_decoder.hpp"
#include "dc_harness/venue.hpp"

using namespace dc::harness;

namespace {

// A minimal Kraken trace: the shape capture_kraken.py writes, cut to the bone.
// The subscribe is `dir: tx`, the status frame and heartbeat carry no `type`,
// and the ack carries neither `channel` nor `type` — which is the combination
// that made `dc_replay` reject 61 of 1,599 records before this stage.
constexpr const char* kKrakenTrace =
    R"({"captured_at":"2026-08-17T00:00:00Z","url":"wss://ws.kraken.com/v2","venue":"kraken","symbol":"BTC/USD","depth":25,"tool_version":"0.1.0","clock":"perf_counter_ns"})"
    "\n"
    R"({"rx_ns":1000000,"dir":"tx","frame":{"method":"subscribe","params":{"channel":"book","symbol":["BTC/USD"],"depth":25}}})"
    "\n"
    R"({"rx_ns":2000000,"frame":{"channel":"status","type":"update","data":[{"system":"online"}]}})"
    "\n"
    R"({"rx_ns":3000000,"frame":{"method":"subscribe","result":{"channel":"book"},"success":true}})"
    "\n"
    R"({"rx_ns":4000000,"frame":{"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD"}]}})"
    "\n"
    R"({"rx_ns":5000000,"frame":{"channel":"heartbeat"}})"
    "\n"
    R"({"rx_ns":6000000,"frame":{"channel":"book","type":"update","data":[{"symbol":"BTC/USD"}]}})"
    "\n";

std::string anvil_header(const char* extra = "") {
    return std::string(
               R"({"captured_at":"t","url":"u","ticker":101,"tool_version":"0.1.0")") +
           extra + "}\n";
}

}  // namespace

// ---------------------------------------------------------------------------
// The metadata contract
// ---------------------------------------------------------------------------

TEST_CASE("an absent venue tag reads as anvil, and says it was inferred") {
    const TraceStats s = read_trace_text(
        anvil_header() + R"({"rx_ns":1,"frame":{"type":"book"}})" + "\n");
    CHECK(s.meta.venue == Venue::Anvil);
    CHECK(s.meta.venue_present == false);  // inherited from the rule, not declared
    CHECK(s.meta.complete());
}

TEST_CASE("a declared anvil tag is accepted and reported as declared") {
    const TraceStats s = read_trace_text(
        anvil_header(R"(,"venue":"anvil")") + R"({"rx_ns":1,"frame":{"type":"book"}})" +
        "\n");
    CHECK(s.meta.venue == Venue::Anvil);
    CHECK(s.meta.venue_present == true);
}

TEST_CASE("ticker is venue-conditional") {
    SUBCASE("anvil still requires it") {
        CHECK_THROWS_AS(
            read_trace_text(
                R"({"captured_at":"t","url":"u","tool_version":"v","venue":"anvil"})"
                "\n"),
            TraceError);
    }
    SUBCASE("kraken does not, and requires symbol instead") {
        const TraceStats s = read_trace_text(kKrakenTrace);
        CHECK(s.meta.venue == Venue::Kraken);
        CHECK(s.meta.ticker_present == false);
        CHECK(s.meta.symbol == "BTC/USD");
        CHECK(s.meta.complete());
    }
    SUBCASE("a kraken trace with no symbol is incomplete") {
        CHECK_THROWS_AS(
            read_trace_text(
                R"({"captured_at":"t","url":"u","tool_version":"v","venue":"kraken"})"
                "\n"),
            TraceError);
    }
}

TEST_CASE("an unknown venue is a different failure from a malformed trace") {
    // The known unknown named in the stage-A brief. Before this, a Kraken trace
    // failed as "metadata line missing a required field", which reads as a
    // corrupt file — and only one of these two is a bug.
    const std::string t =
        R"({"captured_at":"t","url":"u","tool_version":"v","venue":"binance"})"
        "\n";
    CHECK_THROWS_AS(read_trace_text(t), UnknownVenueError);
    CHECK_THROWS_AS(read_trace_text(t), TraceError);  // still a TraceError, so
                                                      // existing catch sites hold
    try {
        read_trace_text(t);
        FAIL("expected UnknownVenueError");
    } catch (const UnknownVenueError& e) {
        CHECK(e.venue_name == "binance");
        CHECK(std::string(e.what()).find("well-formed") != std::string::npos);
    }
}

TEST_CASE("the rx_ns clock is surfaced, never inferred") {
    SUBCASE("declared") {
        const TraceStats s = read_trace_text(kKrakenTrace);
        CHECK(s.meta.clock_present);
        CHECK(s.meta.clock == "perf_counter_ns");
        CHECK(s.meta.clock_name() == "perf_counter_ns");
    }
    SUBCASE("absent reads as undeclared, NOT as the venue's usual clock") {
        // capture_anvil.py has always used monotonic_ns, so inferring it here
        // would be sound today. It is deliberately not inferred: the traces
        // committed before 2026-08-17 do not say, and a reader that answers a
        // question the file did not answer is how a comparison across two
        // clocks gets made silently.
        const TraceStats s = read_trace_text(
            anvil_header() + R"({"rx_ns":1,"frame":{"type":"book"}})" + "\n");
        CHECK(s.meta.clock_present == false);
        CHECK(s.meta.clock_name() == "undeclared");
    }
}

// ---------------------------------------------------------------------------
// The frame contract
// ---------------------------------------------------------------------------

TEST_CASE("a frame with no string type: fatal at anvil, legal at kraken") {
    SUBCASE("anvil is held to exactly the rule it was held to before") {
        const std::string t = anvil_header() + R"({"rx_ns":1,"frame":{"seq":1}})" + "\n";
        CHECK_THROWS_AS(read_trace_text(t), TraceError);
    }
    SUBCASE("kraken's untyped records are read and counted") {
        const TraceStats s = read_trace_text(kKrakenTrace);
        CHECK(s.frame_count == 6);
        // tx:subscribe, ack:subscribe, heartbeat — the three shapes stage 0
        // found and deliberately did not reshape away.
        CHECK(s.untyped_records == 3);
        CHECK(s.count("tx:subscribe") == 1);
        CHECK(s.count("ack:subscribe") == 1);
        CHECK(s.count("heartbeat") == 1);
        CHECK(s.count("status/update") == 1);
        CHECK(s.count("book/snapshot") == 1);
        CHECK(s.count("book/update") == 1);
    }
}

TEST_CASE("a record we sent is counted but never timed") {
    const TraceStats s = read_trace_text(kKrakenTrace);
    CHECK(s.tx_count == 1);
    CHECK(s.frame_count == 6);
    CHECK(s.received_count() == 5);
    // The span starts at the first RECEIVED record (2 ms), not at the subscribe
    // (1 ms): our own upload is not the venue's traffic, and the interval
    // between the subscribe and the first reply is not an inter-message gap.
    CHECK(s.first_rx_ns == 2000000);
    CHECK(s.last_rx_ns == 6000000);
}

TEST_CASE("a refused subscribe is named, not folded into the ack count") {
    // The stage-0 trap: a live socket, 1 Hz heartbeats and a permanently empty
    // book. An instrument that files this as an ordinary ack cannot find it.
    const std::string t =
        R"({"captured_at":"t","url":"u","venue":"kraken","symbol":"BTC/USD","tool_version":"v"})"
        "\n"
        R"({"rx_ns":1,"frame":{"method":"subscribe","error":"Subscription depth not supported","success":false}})"
        "\n";
    const TraceStats s = read_trace_text(t);
    CHECK(s.count("ack:subscribe REFUSED") == 1);
    CHECK(s.count("ack:subscribe") == 0);
}

// ---------------------------------------------------------------------------
// The resync rule — one rule, and it reproduces Anvil's answers
// ---------------------------------------------------------------------------

TEST_CASE("a snapshot is a resync exactly when a book event preceded it") {
    SUBCASE("anvil: an on-connect snapshot at record 1 is not a resync") {
        const TraceStats s = read_trace_text(
            anvil_header() + R"({"rx_ns":1,"frame":{"type":"snapshot"}})" + "\n" +
            R"({"rx_ns":2,"frame":{"type":"book"}})" + "\n");
        CHECK(s.snapshot_count == 1);
        CHECK(s.mid_stream_snapshots == 0);
    }
    SUBCASE("anvil: a snapshot after book frames is a resync") {
        const TraceStats s = read_trace_text(
            anvil_header() + R"({"rx_ns":1,"frame":{"type":"book"}})" + "\n" +
            R"({"rx_ns":2,"frame":{"type":"snapshot"}})" + "\n");
        CHECK(s.mid_stream_snapshots == 1);
    }
    SUBCASE("anvil: a summary before the snapshot does NOT make it a resync") {
        // The one case where this rule and the old index rule disagree, and the
        // new one is right: `summary` is roster data the adapter ignores, so
        // there was no book to re-baseline. No committed trace exercises it —
        // all four open with a snapshot — which is why it is pinned here.
        const TraceStats s = read_trace_text(
            anvil_header() + R"({"rx_ns":1,"frame":{"type":"summary"}})" + "\n" +
            R"({"rx_ns":2,"frame":{"type":"snapshot"}})" + "\n");
        CHECK(s.mid_stream_snapshots == 0);
    }
    SUBCASE("kraken: the on-connect snapshot arrives third and is not a resync") {
        // Anvil's old "not the trace's first record" rule would have called this
        // a reconnect, and therefore called EVERY Kraken capture a reconnect.
        const TraceStats s = read_trace_text(kKrakenTrace);
        CHECK(s.snapshot_count == 1);
        CHECK(s.mid_stream_snapshots == 0);
    }
    SUBCASE("kraken: a second snapshot after updates is a resync") {
        const std::string t =
            std::string(kKrakenTrace) +
            R"({"rx_ns":7000000,"frame":{"method":"subscribe","result":{},"success":true}})"
            "\n"
            R"({"rx_ns":8000000,"frame":{"channel":"book","type":"snapshot","data":[]}})"
            "\n";
        const TraceStats s = read_trace_text(t);
        CHECK(s.snapshot_count == 2);
        CHECK(s.mid_stream_snapshots == 1);
    }
}

// BOTH BRANCHES AT KRAKEN, MINIMALLY AND IN ISOLATION.
//
// These exist because **no committed trace exercises the positive branch at
// Kraken** — stage 0's capture recipe deliberately excluded healing events, so
// all four slices score resync 0 and the only thing they can prove is that the
// rule stays silent. A rule verified only where it must not fire is a rule with
// half a test.
//
// Deliberately stripped of the subscribe machinery the case above carries: no
// `tx:subscribe`, no ack, no `status`. What separates them is a book event and
// nothing else, which is the whole claim of the venue-free predicate. If a
// future session reintroduces subscription bookkeeping into the rule, the
// positive case here keeps passing and the *negative* one breaks — which is the
// direction that fails loudly.
namespace {
constexpr const char* kKrakenHeader =
    R"({"captured_at":"t","url":"u","venue":"kraken","symbol":"BTC/USD","tool_version":"v"})"
    "\n";
constexpr const char* kKrakenUpdate =
    R"({"rx_ns":%d,"frame":{"channel":"book","type":"update","data":[{"symbol":"BTC/USD"}]}})"
    "\n";
constexpr const char* kKrakenSnapshot =
    R"({"rx_ns":%d,"frame":{"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD"}]}})"
    "\n";

std::string kraken_stream(std::initializer_list<const char*> records) {
    std::string out = kKrakenHeader;
    int rx = 0;
    char buf[256];
    for (const char* fmt : records) {
        rx += 1000000;
        std::snprintf(buf, sizeof buf, fmt, rx);
        out += buf;
    }
    return out;
}
}  // namespace

TEST_CASE("kraken: a snapshot AFTER book events is a resync") {
    const TraceStats s = read_trace_text(
        kraken_stream({kKrakenUpdate, kKrakenUpdate, kKrakenSnapshot}));
    CHECK(s.book_events == 3);
    CHECK(s.snapshot_count == 1);
    CHECK(s.mid_stream_snapshots == 1);
}

TEST_CASE("kraken: a snapshot BEFORE any book event is not a resync") {
    const TraceStats s = read_trace_text(
        kraken_stream({kKrakenSnapshot, kKrakenUpdate, kKrakenUpdate}));
    CHECK(s.book_events == 3);
    CHECK(s.snapshot_count == 1);
    CHECK(s.mid_stream_snapshots == 0);
}

TEST_CASE("kraken: the second of two snapshots is a resync, the first is not") {
    // The snapshot is itself a book event, so a bare snapshot-then-snapshot
    // stream — no update between them — must still score exactly one resync.
    // This is the clause an implementation drops first if it reads
    // `is_book_event` as "an update".
    const TraceStats s = read_trace_text(
        kraken_stream({kKrakenSnapshot, kKrakenSnapshot}));
    CHECK(s.book_events == 2);
    CHECK(s.snapshot_count == 2);
    CHECK(s.mid_stream_snapshots == 1);
}

// ---------------------------------------------------------------------------
// The venue table and the venue-declared threshold (deliverable 4)
// ---------------------------------------------------------------------------

TEST_CASE("venue_traits carries the liveness signal's NAME, not an interval") {
    // The rename is the assertion. A duration here is what the 2026-08-17 ruling
    // forbids: no threshold on book silence can be correct, so a venue row must
    // not be able to carry one.
    CHECK(venue_traits(Venue::Anvil).liveness_signal == "summary");
    CHECK(venue_traits(Venue::Kraken).liveness_signal == "heartbeat");
    static_assert(
        std::is_same_v<decltype(VenueTraits::liveness_signal), std::string_view>,
        "the liveness signal is a wire fact (a name), never an interval");
}

TEST_CASE("the staleness threshold calibrates itself from the liveness signal") {
    // RE-DERIVED 2026-08-17, replacing `> 9007 * 1.5`. That assertion was
    // premised on a beaten figure — a HEALTHY 25,843 ms book silence was measured
    // the same day — and on the wrong quantity: it tested a book-silence
    // threshold, which the ruling says cannot be correct at any value.
    //
    // What replaces it is a claim about the MULTIPLE, because that is what the
    // constant is now made of, and a multiple is the only form that survives a
    // venue whose cadence nobody here controls.
    SUBCASE("k clears the worst measured healthy multiple, with margin") {
        // Anvil is the binding case: 1.937x its median (968.8 ms against 500.0),
        // which is one missed `summary` tick in otherwise healthy M0 data.
        // Kraken's worst is 1.119x over 834 intervals at two hours of day.
        CHECK(kThresholdMultiple > 1.937);
        CHECK(kThresholdMultiple / 1.937 > 2.0);   // margin, as kRxWatchdogMs had
        CHECK(kThresholdMultiple <= 6.0);          // and still prompt on a real outage
    }

    SUBCASE("floor and ceiling bracket it, so no median can disable it") {
        CHECK(kThresholdFloorMs < kThresholdCeilingMs);
        // The ceiling is what stops a SUSTAINED slowdown buying unlimited
        // tolerance, so it must still catch the only real outage on record:
        // Anvil's 176 s silence of 2026-08-16 (A2b).
        CHECK(kThresholdCeilingMs < 176000.0);
        // The floor must not go below the smallest threshold this project has
        // ever run on evidence.
        CHECK(kThresholdFloorMs >= 1000.0);
    }

    SUBCASE("an uncalibrated clock is generous, and needs no new rendered state") {
        LivenessClock c;
        CHECK(c.samples() == 0);
        CHECK_FALSE(c.calibrated());
        // A threshold always exists, so there is no "calibrating" state to draw.
        // That is what keeps this ruling out of the boundary contract entirely.
        CHECK(c.threshold_ms() == doctest::Approx(kUncalibratedThresholdMs));
        CHECK(c.threshold_ms() >= kThresholdCeilingMs);
    }

    SUBCASE("it reaches Anvil's 2 Hz and Kraken's 1 Hz from the signal alone") {
        LivenessClock anvil;
        for (int i = 0; i <= 40; ++i) { anvil.on_liveness(std::int64_t(i) * 500000000LL); }
        CHECK(anvil.calibrated());
        CHECK(anvil.median_ms() == doctest::Approx(500.0));
        CHECK(anvil.threshold_ms() == doctest::Approx(2000.0));

        LivenessClock kraken;
        for (int i = 0; i <= 40; ++i) { kraken.on_liveness(std::int64_t(i) * 1000000000LL); }
        CHECK(kraken.threshold_ms() == doctest::Approx(4000.0));
        // One constant, two venues, and no per-venue number anywhere.
        CHECK(kraken.threshold_ms() == doctest::Approx(2.0 * anvil.threshold_ms()));
    }

    SUBCASE("one late tick moves a rank, not the median") {
        LivenessClock c;
        std::int64_t t = 0;
        for (int i = 0; i < 20; ++i) { c.on_liveness(t); t += 500000000LL; }
        const double before = c.median_ms();
        t += 4000000000LL;            // one badly late arrival
        c.on_liveness(t);
        CHECK(c.median_ms() == doctest::Approx(before));
    }

    SUBCASE("a SUSTAINED rate change is followed, which is the case that matters") {
        // Anvil's upstream coalescing is GLOBAL: a lagging broadcaster skips
        // intermediate rosters for every socket, so it presents as a lower RATE
        // rather than as a gap. A mean would drag and an all-time statistic
        // would never catch up; a bounded window re-converges within one window.
        LivenessClock c;
        std::int64_t t = 0;
        for (int i = 0; i < 40; ++i) { c.on_liveness(t); t += 500000000LL; }
        CHECK(c.median_ms() == doctest::Approx(500.0));
        for (std::size_t i = 0; i < kWindowSamples; ++i) { t += 2000000000LL; c.on_liveness(t); }
        CHECK(c.median_ms() == doctest::Approx(2000.0));
        CHECK(c.threshold_ms() == doctest::Approx(8000.0));
    }

    SUBCASE("a DRAINING BACKLOG cannot grey a feed that just recovered") {
        // The floor's real job, and the reason it is not belt-and-braces.
        // Anvil queues and never drops per socket, so a backlogged client gets
        // every frame late rather than losing any — and when it drains, the
        // queue arrives as a burst with near-zero inter-arrivals. The rolling
        // median collapses; an uncapped k x median would then grey a perfectly
        // healthy feed at the exact moment it recovered.
        LivenessClock c;
        std::int64_t t = 0;
        for (int i = 0; i < 12; ++i) { c.on_liveness(t); t += 500000000LL; }
        CHECK(c.threshold_ms() == doctest::Approx(2000.0));

        // The backlog drains: a full window of frames 2 ms apart.
        for (std::size_t i = 0; i < kWindowSamples; ++i) { t += 2000000LL; c.on_liveness(t); }
        CHECK(c.median_ms() == doctest::Approx(2.0));       // the median HAS collapsed
        // Uncapped this would be 8 ms. The floor holds it at 1,000.
        CHECK(kThresholdMultiple * c.median_ms() < 10.0);
        CHECK(c.threshold_ms() == doctest::Approx(kThresholdFloorMs));

        // ...and the next normal 500 ms tick, on a feed that is now perfectly
        // healthy, must NOT be a grey. Without the floor it would be, 125x over.
        CHECK(500.0 < c.threshold_ms());
    }

    SUBCASE("the floor and the ceiling actually clamp") {
        LivenessClock fast;
        std::int64_t t = 0;
        for (int i = 0; i < 20; ++i) { fast.on_liveness(t); t += 10000000LL; }  // 100 Hz
        CHECK(fast.median_ms() == doctest::Approx(10.0));
        CHECK(fast.threshold_ms() == doctest::Approx(kThresholdFloorMs));

        LivenessClock slow;
        t = 0;
        for (int i = 0; i < 20; ++i) { slow.on_liveness(t); t += 60000000000LL; }  // 1/min
        CHECK(slow.threshold_ms() == doctest::Approx(kThresholdCeilingMs));
    }
}

namespace {
constexpr const char* kHeartbeat = R"({"channel":"heartbeat"})";
constexpr const char* kBookUpdate = R"({"channel":"book","type":"update","data":[]})";
}  // namespace

TEST_CASE("the liveness clock holds through a legitimate book silence") {
    // The MINA/GBP case in miniature, and the ruling's whole argument: the book
    // says nothing for 40 s while the heartbeat keeps perfect time. A book-armed
    // rule greys here at any threshold below 40 s; the liveness rule does not
    // grey at all, because nothing about the feed stopped.
    std::vector<std::pair<long long, const char*>> steps;
    steps.emplace_back(0, kBookUpdate);
    for (int i = 0; i < 40; ++i) { steps.emplace_back(1000, kHeartbeat); }
    steps.emplace_back(0, kBookUpdate);

    std::string t =
        R"({"captured_at":"t","url":"u","venue":"kraken","symbol":"X/Y","tool_version":"v"})"
        "\n";
    long long rx = 0;
    char buf[256];
    for (const auto& [delay_ms, body] : steps) {
        rx += delay_ms * 1000000LL;
        std::snprintf(buf, sizeof buf, R"({"rx_ns":%lld,"frame":%s})" "\n", rx, body);
        t += buf;
    }

    const TraceStats s = read_trace_text(t);
    CHECK(s.liveness_events == 40);
    CHECK(s.median_liveness_gap_ms == doctest::Approx(1000.0));
    CHECK(s.liveness_threshold_ms == doctest::Approx(4000.0));
    CHECK(s.max_book_gap_ms == doctest::Approx(40000.0));  // 40 s of book silence
    CHECK(s.liveness_firings == 0);                        // and NOT ONE grey
}

TEST_CASE("the liveness clock DOES fire when the signal itself stops") {
    // The other half, and the reason a threshold exists at all. Same shape,
    // except the heartbeat stops dead — which is Anvil's A2b signature, and the
    // case book silence could never tell apart from a quiet market.
    std::string t =
        R"({"captured_at":"t","url":"u","venue":"kraken","symbol":"X/Y","tool_version":"v"})"
        "\n";
    long long rx = 0;
    char buf[256];
    for (int i = 0; i < 20; ++i) {
        rx += 1000000000LL;
        std::snprintf(buf, sizeof buf, R"({"rx_ns":%lld,"frame":%s})" "\n", rx, kHeartbeat);
        t += buf;
    }
    rx += 30000000000LL;   // the feed dies for 30 s
    std::snprintf(buf, sizeof buf, R"({"rx_ns":%lld,"frame":%s})" "\n", rx, kHeartbeat);
    t += buf;

    const TraceStats s = read_trace_text(t);
    CHECK(s.liveness_threshold_ms == doctest::Approx(4000.0));
    CHECK(s.max_liveness_gap_ms == doctest::Approx(30000.0));
    CHECK(s.liveness_firings == 1);
}

TEST_CASE("the M1 rule invents disconnects on a venue that publishes on change") {
    // Deliverable 4, in miniature: one legitimate 9 s silence between book
    // updates, of the kind the quiet pair does 12 times a minute.
    const std::string t =
        R"({"captured_at":"t","url":"u","venue":"kraken","symbol":"MINA/GBP","tool_version":"v"})"
        "\n"
        R"({"rx_ns":1000000000,"frame":{"channel":"book","type":"update","data":[]}})"
        "\n"
        R"({"rx_ns":10000000000,"frame":{"channel":"book","type":"update","data":[]}})"
        "\n";
    const TraceStats s = read_trace_text(t);
    CHECK(s.max_gap_ms == doctest::Approx(9000.0));
    CHECK(s.watchdog_firings_at_anvil_threshold == 1);  // a disconnect that never happened
    CHECK(s.watchdog_firings_legacy == 0);              // at the withdrawn 15,000 ms
    CHECK(s.book_watchdog_firings_at_anvil_threshold == 1);
    CHECK(s.book_watchdog_firings_legacy == 0);
    // And the rule that replaced both: this trace carries no heartbeat at all,
    // so the liveness clock never calibrates and never fires. A trace with no
    // liveness signal is one this rule cannot judge — a true statement about the
    // trace rather than a silent pass.
    CHECK(s.liveness_events == 0);
    CHECK(s.liveness_firings == 0);
    CHECK(s.liveness_threshold_ms == doctest::Approx(kUncalibratedThresholdMs));
}

TEST_CASE("the replay driver dispatches to the venue's own adapter") {
    // THIS CASE ASSERTED THE OPPOSITE UNTIL M4 STAGE B1, and the inversion is
    // the deliverable. Stage A shipped Kraken's decoder as a classifier, so the
    // driver refused the venue loudly rather than feeding Kraken's JSON to the
    // Anvil parser — a dead ladder over a 100% parse-error count, which is the
    // shape of failure invariant #5 exists to forbid. B1 supplies the adapter,
    // so the refusal is not relaxed, it is unnecessary.
    //
    // What replaces the guard is not trust: it is `decoder`, pinned here and in
    // every Kraken golden, so a dispatch that sent this trace the other way
    // would be visible in the output rather than merely absent from the code.
    TraceReader reader(kKrakenTrace, in_memory, "kraken-synthetic");
    CHECK(reader.venue() == Venue::Kraken);
    const ReplayResult r =
        run_replay(reader, depthcharge::kraken::kKrakenBtcUsd.spec, ReplayOptions{});
    CHECK(r.decoder == "kraken");
    CHECK(r.kraken.snapshot_frames == 1);
    CHECK(r.kraken.heartbeats == 1);
    CHECK(r.adapter.frames_in == 0);   // Anvil's counters stay untouched
}

TEST_CASE("a Kraken symbol this build declares no scale for is refused, not guessed") {
    // A wrong scale does not fail — it draws. So there is no default here, and
    // the loud refusal is the feature: `symbol_for` returned Anvil's scale for
    // every venue until B1, and the driver's guard was the only thing keeping
    // that from reaching a Kraken trace.
    const std::string t =
        std::string(
            R"({"captured_at":"t","url":"u","venue":"kraken","symbol":"DOGE/XBT",)"
            R"("tool_version":"v"})") +
        "\n";
    TraceReader reader(t, in_memory, "unknown-symbol");
    TraceMeta meta = reader.meta();
    CHECK(meta.symbol == "DOGE/XBT");
    CHECK_THROWS_AS(symbol_for(meta), TraceError);

    // ...and the two it does declare resolve to the scales the `instrument`
    // channel publishes, which is what makes every price on this wire exactly
    // representable (kraken_adapter.hpp).
    meta.symbol = "BTC/USD";
    CHECK(symbol_for(meta).price_decimals == 1);
    CHECK(symbol_for(meta).qty_decimals == 8);
    meta.symbol = "MINA/GBP";
    CHECK(symbol_for(meta).price_decimals == 4);
    CHECK(symbol_for(meta).qty_decimals == 8);
}

// ---------------------------------------------------------------------------
// The decoder / sink contracts (strain 3, option (ii))
// ---------------------------------------------------------------------------

namespace {

// What the contract is supposed to accept and reject. These are compile-time
// facts; the TEST_CASE below exists so the file reads as coverage rather than
// as a header nobody runs.
struct GoodSink {
    void operator()(const depthcharge::FeedEvent&) {}
};
struct WrongArgSink {
    void operator()(int) {}
};
struct ReturningSink {
    int operator()(const depthcharge::FeedEvent&) { return 0; }
};

static_assert(std::is_invocable_v<GoodSink&, const depthcharge::FeedEvent&>);
static_assert(!std::is_invocable_v<WrongArgSink&, const depthcharge::FeedEvent&>);
static_assert(!std::is_void_v<
              std::invoke_result_t<ReturningSink&, const depthcharge::FeedEvent&>>);

// A decoder missing decode() must fail DecoderContract. Checked through the
// detection traits rather than by instantiating the contract, because a failing
// static_assert is a compile error and cannot be tested from inside the build
// it breaks — this is the price of option (ii) over a concept, and naming it
// here is cheaper than rediscovering it.
struct NotADecoder {
    static constexpr Venue kVenue = Venue::Anvil;
};
static_assert(!dc_decodes_into_sink<NotADecoder>::value);
static_assert(!dc_classifies<NotADecoder>::value);
static_assert(dc_decodes_into_sink<AnvilTraceDecoder>::value);
static_assert(dc_decodes_into_sink<KrakenTraceDecoder>::value);
static_assert(dc_classifies<AnvilTraceDecoder>::value);
static_assert(dc_classifies<KrakenTraceDecoder>::value);
static_assert(dc_reports_transport_gap<AnvilTraceDecoder>::value);
static_assert(dc_reports_transport_gap<KrakenTraceDecoder>::value);

}  // namespace

TEST_CASE("kraken's decoder is an ADAPTER now, and the seam did not move") {
    // The stage-A version of this case asserted `events == 0` and called that
    // coverage. It was a true statement about a decoder that emitted nothing by
    // design, and its real job was to hold the SHAPE — decode() taking a sink it
    // did not use — so that B1 could fill the body without reshaping the seam.
    // This is the same trace through the same call, asserting the opposite.
    KrakenTraceDecoder decoder(depthcharge::kraken::kKrakenBtcUsd.spec, "BTC/USD", 25);
    std::vector<depthcharge::FeedEvent> events;
    auto sink = [&events](const depthcharge::FeedEvent& ev) { events.push_back(ev); };

    TraceReader reader(kKrakenTrace, in_memory, "kraken-synthetic");
    TraceFrame frame;
    while (reader.next(frame)) { decoder.decode(frame, sink); }

    const auto& st = decoder.adapter().stats();
    CHECK(st.status_frames == 1);
    CHECK(st.acks == 1);
    CHECK(st.heartbeats == 1);
    CHECK(st.snapshot_frames == 1);
    CHECK(st.update_frames == 1);
    // The subscribe THIS SIDE SENT is not the venue speaking, and the decoder
    // drops it before the adapter sees it — otherwise it lands in unknown_kind
    // and a counter describing the venue would be describing us.
    CHECK(st.unknown_kind == 0);
    CHECK(decoder.adapter().subscribe_state() ==
          depthcharge::kraken::KrakenAdapter::SubscribeState::Subscribed);

    // One event: the snapshot. This fixture's book messages carry no levels at
    // all, so the snapshot is an empty full-replace and the update amends
    // nothing — which is exactly the assertion worth having here, because it
    // separates "the frame was decoded" from "levels were found".
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == depthcharge::FeedEvent::Kind::Snapshot);
    CHECK(events[0].seq == 1);
    CHECK(events[0].bids.size == 0);

    decoder.on_transport_gap(depthcharge::GapReason::Disconnect, sink);
    REQUIRE(events.size() == 2);
    CHECK(events[1].kind == depthcharge::FeedEvent::Kind::Gap);
    CHECK(events[1].reason == depthcharge::GapReason::Disconnect);
    // The gap dropped the baseline: a resync must start from a snapshot, not
    // from a book whose provenance is a hole.
    CHECK_FALSE(decoder.adapter().has_baseline());
}

TEST_CASE("absence of a subscribe is not failure of a subscribe") {
    // The extreme quiet-pair slice begins MID-STREAM: no status, no subscribe,
    // no ack anywhere in the file. An adapter that required an ack before
    // accepting data would emit nothing for the one trace carrying the 25,843 ms
    // healthy book silence the whole staleness ruling rests on.
    KrakenTraceDecoder decoder(depthcharge::kraken::kKrakenBtcUsd.spec, "BTC/USD", 25);
    std::size_t events = 0;
    auto sink = [&events](const depthcharge::FeedEvent&) { ++events; };

    CHECK(decoder.adapter().subscribe_state() ==
          depthcharge::kraken::KrakenAdapter::SubscribeState::Unknown);
    CHECK_FALSE(decoder.adapter().refused());

    // No checksum: this case is about the subscribe state, and from B2 a frame
    // that carries one is making a claim the adapter now checks. See the rule
    // at test_kraken_adapter.cpp's truncation case.
    decoder.adapter().on_frame(
        R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":62807.0,"qty":0.65540712}],"asks":[]}]})",
        sink);

    CHECK(events == 1);                                   // accepted, unsubscribed
    CHECK(decoder.adapter().stats().snapshot_frames == 1);
    CHECK(decoder.adapter().subscribe_state() ==
          depthcharge::kraken::KrakenAdapter::SubscribeState::Unknown);
}

TEST_CASE("a REFUSED subscribe is fatal, and it greys the panel immediately") {
    // The trap stage 0 measured: after `success:false` the socket stays UP,
    // `status` has already arrived, and heartbeats keep coming at 1 Hz. Nothing
    // else on this connection will ever say anything is wrong.
    KrakenTraceDecoder decoder(depthcharge::kraken::kKrakenBtcUsd.spec, "BTC/USD", 25);
    std::vector<depthcharge::FeedEvent> events;
    auto sink = [&events](const depthcharge::FeedEvent& ev) { events.push_back(ev); };

    decoder.adapter().on_frame(
        R"({"error":"Subscription depth not supported","method":"subscribe",)"
        R"("success":false,"time_in":"t","time_out":"t"})",
        sink);

    CHECK(decoder.adapter().refused());
    REQUIRE(events.size() == 1);
    // Not a new GapReason — the question has been asked three times and answered
    // no twice (ARCHITECTURE §9, 2026-08-17). It is a fatal transport error, and
    // Disconnect is what the panel already knows how to draw.
    CHECK(events[0].kind == depthcharge::FeedEvent::Kind::Gap);
    CHECK(events[0].reason == depthcharge::GapReason::Disconnect);
}

TEST_CASE("the venue tag round-trips through the name lookup") {
    Venue v{};
    CHECK(venue_from_name("", v));
    CHECK(v == Venue::Anvil);  // the additive rule, in one place
    CHECK(venue_from_name("anvil", v));
    CHECK(v == Venue::Anvil);
    CHECK(venue_from_name("kraken", v));
    CHECK(v == Venue::Kraken);
    CHECK_FALSE(venue_from_name("binance", v));
    CHECK_FALSE(venue_from_name("Anvil", v));  // the tag is the wire spelling
}
