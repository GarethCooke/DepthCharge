// test_liveness_watchdog.cpp — WHEN THE PANEL GREYS, on the desk.
//
// M4 stage D item A1 replaced `kRxWatchdogMs` and the book-event rule built on
// it with `LivenessWatchdog`: silence of the venue's declared liveness signal,
// measured against a threshold the signal itself calibrates. The brief says of
// that rewire that "no host test can check" it, and the distinction this file
// exists to draw is between the two halves of that claim:
//
//   * The BEHAVIOUR is a bench observation. A panel going grey ~4 s after a
//     Kraken heartbeat stops needs a panel, a heartbeat and someone with a
//     stopwatch, and Part B does it.
//   * The POLICY is arithmetic, and arithmetic is cheap to test — the same
//     argument that put ws_supervisor.hpp, gap_histogram.hpp and ws_ping.hpp on
//     this side of the line. What is left in feed_task.cpp after this file is a
//     FreeRTOS queue timeout and an esp_timer reading.
//
// It is also where the deleted `test_staleness.cpp`'s still-live cases were
// ported to: the backwards-clock guard, and the property that a reconnect
// erases per-connection state whatever preceded it. Everything else that file
// covered went with the estimator it covered — `drain_percent`, the `AHEAD`
// premise warning and `SecondsText` are all gone, and coverage of a deleted
// thing is not coverage lost.
//
// THE COINCIDENCE THIS FILE IS BUILT TO AVOID (ARCHITECTURE §9, 2026-08-18).
// Anvil broadcasts every 500 ms and the deleted estimator hardcoded 500 ms, so
// EVERY available Anvil input made the wrong answer coincide with the right one
// — which is exactly why the divergence survived two milestones invisibly. So
// no case below runs at 500 ms alone: each cadence-sensitive one is swept over
// several, including cadences neither venue has, and the two real ones are
// checked to produce DIFFERENT thresholds rather than the same one.
#include <doctest/doctest.h>

#include <cstdint>
#include <random>
#include <vector>

#include <depthcharge/venue_liveness.hpp>

#include "liveness_watchdog.hpp"

using depthcharge::AgeReading;
using depthcharge::kMinSamples;
using depthcharge::kThresholdCeilingMs;
using depthcharge::kThresholdFloorMs;
using depthcharge::kThresholdMultiple;
using depthcharge::kUncalibratedThresholdMs;
using depthcharge::LivenessClock;
using depthcharge::fw::GreyLedger;
using depthcharge::fw::LivenessWatchdog;
using depthcharge::fw::ns_from_us;

namespace {

constexpr std::int64_t ms_ns(double v) { return static_cast<std::int64_t>(v * 1e6); }

// A venue emitting its liveness signal at a fixed cadence, and nothing else.
// The book is deliberately absent from every case in this file: after the
// 2026-08-17 ruling the watchdog cannot see book events at all, and a fixture
// that fed it any would be testing a rule this object does not have.
struct Signal {
    LivenessWatchdog wd;
    std::int64_t now = 0;

    // Default-constructed is the shipping object every case below M5 stage D-A3
    // was written against; the policy overload is what the board runs now.
    Signal() = default;
    explicit Signal(depthcharge::LivenessPolicy p) noexcept : wd(p) {}

    void tick(double interval_ms, int n) {
        for (int i = 0; i < n; ++i) {
            now += ms_ns(interval_ms);
            wd.on_liveness(now);
        }
    }
    // Wall time passes with nothing arriving.
    void silence(double for_ms) { now += ms_ns(for_ms); }
};

}  // namespace

TEST_CASE("the one conversion is spelled once, and it is the one that would be forgotten") {
    // Both domains are std::int64_t, so a missing x1000 is a 1,000x-wrong
    // threshold that compiles. `ns_from_us` exists so grep finds every site.
    CHECK(ns_from_us(0) == 0);
    CHECK(ns_from_us(1) == 1000);
    CHECK(ns_from_us(1'000'000) == 1'000'000'000);   // one second, both ways
    static_assert(ns_from_us(4'000'000) == 4'000'000'000LL, "4 s of us is 4 s of ns");
}

TEST_CASE("the liveness mute is OFF, and the check that says so runs in every build") {
    // A test-only flag that lives in a build environment is a test-only flag
    // somebody eventually ships. `DC_TEST_MUTE_LIVENESS` lives in no
    // `platformio.ini` environment and is passed for one run through
    // `PLATFORMIO_BUILD_FLAGS`; this is the assertion that the DEFAULT is off,
    // compiled into the same suite the shipping build is gated on.
    //
    // It exists because the bench cannot stage a stopped heartbeat over a live
    // socket from the network side: disabling the interface and pausing the
    // client in the mesh app BOTH deauthenticate, so the socket dies first and
    // the threshold never runs. Measured twice on 2026-08-20, `sock`
    // incrementing and `wd` not.
    using depthcharge::fw::kTestMutesLiveness;
    using depthcharge::fw::test_liveness_muted;

    static_assert(!kTestMutesLiveness,
                  "DC_TEST_MUTE_LIVENESS must be off in any build this suite gates");
    CHECK_FALSE(test_liveness_muted(0));
    CHECK_FALSE(test_liveness_muted(1));
    CHECK_FALSE(test_liveness_muted(86'400'000'000LL));   // a day of uptime

    // And the SOAK line's marker is a preprocessor-chosen literal, so a shipping
    // image does not contain the string at all rather than merely declining to
    // print it. `sizeof` an empty literal is 1 — the NUL. If this ever fires,
    // something has shipped a build that announces itself as a test image, which
    // is the good failure; the bad one is the reverse and the static_assert above
    // catches that.
    static_assert(sizeof(DC_SOAK_TEST_TAG) == 1,
                  "a shipping image must carry no test-image tag");
}

TEST_CASE("a board that has never heard the venue does not raise a Gap to say so") {
    // It is already Stale{Resync} — the book has never been told anything — and
    // a Gap there would be a second way of saying the same thing. Same rule the
    // old `watching_` flag carried.
    LivenessWatchdog wd;
    CHECK_FALSE(wd.armed());
    CHECK_FALSE(wd.expired(0));
    CHECK_FALSE(wd.expired(ms_ns(1'000'000)));   // and still not after 1,000 s
    CHECK(wd.deadline_ns() == 0);
    CHECK_FALSE(wd.age(0).valid);
    CHECK(wd.firings() == 0);
}

TEST_CASE("before calibration the threshold is the ceiling, and that is the honest direction") {
    // Fewer than kMinSamples intervals: the object knows nothing about this
    // venue's cadence, so it is slow to grey rather than quick to cry wolf.
    Signal s;
    s.tick(500.0, static_cast<int>(kMinSamples));   // kMinSamples arrivals = kMinSamples-1 intervals
    CHECK_FALSE(s.wd.calibrated());
    CHECK(s.wd.threshold_ms() == doctest::Approx(kUncalibratedThresholdMs));

    // One more arrival closes the gate.
    s.tick(500.0, 1);
    CHECK(s.wd.calibrated());
    CHECK(s.wd.threshold_ms() == doctest::Approx(kThresholdMultiple * 500.0));
}

TEST_CASE("the threshold is MEASURED, and the two venues therefore get different ones") {
    // The whole point of the ruling. If these came out equal the object would be
    // reading a constant, and Anvil's 500 ms coinciding with a hardcoded 500 ms
    // is precisely how the deleted estimator stayed wrong invisibly.
    Signal anvil;
    anvil.tick(500.0, 40);
    Signal kraken;
    kraken.tick(1000.0, 40);

    CHECK(anvil.wd.threshold_ms() == doctest::Approx(2000.0));
    CHECK(kraken.wd.threshold_ms() == doctest::Approx(4000.0));
    CHECK(kraken.wd.threshold_ms() > anvil.wd.threshold_ms());

    // And a cadence neither venue has, so the rule is not two special cases.
    for (const double cadence : {120.0, 333.0, 750.0, 2500.0, 6000.0}) {
        Signal s;
        s.tick(cadence, 40);
        const double want = kThresholdMultiple * cadence;
        const double clamped = want < kThresholdFloorMs   ? kThresholdFloorMs
                             : want > kThresholdCeilingMs ? kThresholdCeilingMs
                                                          : want;
        CHECK(s.wd.threshold_ms() == doctest::Approx(clamped));
    }
}

TEST_CASE("it greys AT the threshold and not before it, at every cadence") {
    for (const double cadence : {500.0, 1000.0, 333.0, 2000.0}) {
        Signal s;
        s.tick(cadence, 40);
        const std::int64_t threshold = ms_ns(s.wd.threshold_ms());

        CHECK(s.wd.armed());
        CHECK_FALSE(s.wd.expired(s.now));                     // the instant it arrived
        CHECK_FALSE(s.wd.expired(s.now + threshold - 1));     // one ns short
        CHECK(s.wd.expired(s.now + threshold));               // and there
        CHECK(s.wd.deadline_ns() == s.now + threshold);
    }
}

TEST_CASE("MINA/GBP: 26 seconds of book silence does not grey a healthy socket") {
    // THE HEADLINE CRITERION OF M4, AS FAR AS A DESK CAN TAKE IT. The extreme
    // slice's 25,843 ms of legitimate book silence is a property of a quiet
    // market, and the deleted 1,000 ms book-event watchdog would have greyed the
    // panel 25 times through it. This object cannot: the book is not one of its
    // inputs, so a 30 s book hole with the heartbeat running is 30 heartbeats
    // and no expiry.
    //
    // What the bench still has to do is confirm that the socket really does keep
    // heartbeating through such a hole on live MINA/GBP — the capture says so,
    // and a capture is one afternoon.
    Signal s;
    s.tick(1000.0, 40);                       // calibrate on Kraken's heartbeat
    CHECK(s.wd.threshold_ms() == doctest::Approx(4000.0));

    for (int second = 0; second < 30; ++second) {
        s.tick(1000.0, 1);                    // a heartbeat, and no book event
        CHECK_FALSE(s.wd.expired(s.now));
    }
    CHECK(s.wd.firings() == 0);
}

TEST_CASE("...and the same socket greys when the heartbeat itself stops") {
    // The other half of the same criterion, and the reason the first one is not
    // just an object that never fires.
    Signal s;
    s.tick(1000.0, 40);
    s.silence(3999.0);
    CHECK_FALSE(s.wd.expired(s.now));
    s.silence(1.0);
    CHECK(s.wd.expired(s.now));
}

TEST_CASE("one outage is one Gap, and the next arrival still measures the whole hole") {
    // `note_fired` disarms so the panel greys once rather than once a threshold
    // for as long as the venue is quiet — and it deliberately does NOT move the
    // last-arrival stamp, so the silence that ended is reported in full. Gating
    // that was a real defect in the object this replaces: the watchdog cleared
    // the flag, and the one number that quantifies a gap was suppressed by the
    // very thing that detected it.
    Signal s;
    s.tick(1000.0, 40);
    const std::int64_t before = s.wd.worst_gap_ns();

    s.silence(10'000.0);
    REQUIRE(s.wd.expired(s.now));
    s.wd.note_fired(s.now);
    CHECK(s.wd.firings() == 1);
    CHECK_FALSE(s.wd.armed());
    CHECK(s.wd.deadline_ns() == 0);

    // Still quiet, still one firing.
    s.silence(60'000.0);
    CHECK_FALSE(s.wd.expired(s.now));
    CHECK(s.wd.firings() == 1);

    // Data returns: the recorded hole is the whole 70 s, not the 60 s after the
    // alarm and not the 10 s before it.
    s.tick(1.0, 1);
    CHECK(s.wd.worst_gap_ns() > before);
    CHECK(static_cast<double>(s.wd.worst_gap_ns()) == doctest::Approx(70'001.0 * 1e6).epsilon(0.001));
    CHECK(s.wd.armed());
}

TEST_CASE("a firing voids the age and banks its peak — the same rule a disconnect gets") {
    // The host driver's `raise_watchdog_gap` does exactly this, and the two must
    // agree or the number on the panel and the number in a golden are computed
    // by different rules again — which is the divergence stage D closed.
    Signal s;
    s.tick(1000.0, 40);
    // Fall behind: arrivals at half the baseline rate accrue lag.
    for (int i = 0; i < 40; ++i) { s.tick(2000.0, 1); }
    const AgeReading behind = s.wd.age(s.now);
    REQUIRE(behind.valid);
    REQUIRE(behind.ms > 0);

    s.silence(10'000.0);
    s.wd.note_fired(s.now);
    // NO READING, and specifically not 0.0s. The window is empty, so there is
    // nothing to measure against — which is a different claim from "the book is
    // current" and is the only honest one at the instant the panel greys.
    CHECK_FALSE(s.wd.age(s.now).valid);
    CHECK(s.wd.worst_age_ms() >= behind.ms);       // but the episode survived

    // The BASELINE survives, which is what separates a stall from a reconnect —
    // see the drain-burst case below. So the reading returns as soon as the
    // venue speaks again, rather than after another 32 arrivals.
    CHECK(s.wd.baseline_ms() == 1000u);
    s.tick(1000.0, 1);
    CHECK(s.wd.age(s.now).valid);
}

TEST_CASE("the age dies with the socket and the threshold does not — the asymmetry is the point") {
    // Cadence is a property of the venue; a backlog is a property of one
    // connection. A clock that recalibrated per socket would spend the first
    // eight arrivals of every recovery at the 30 s uncalibrated default.
    Signal s;
    s.tick(1000.0, 40);
    const double threshold_before = s.wd.threshold_ms();
    REQUIRE(s.wd.age_calibrated());

    s.wd.on_socket_change(s.now);
    CHECK(s.wd.threshold_ms() == doctest::Approx(threshold_before));
    CHECK(s.wd.calibrated());
    CHECK_FALSE(s.wd.age_calibrated());
    CHECK_FALSE(s.wd.age(s.now).valid);
    CHECK_FALSE(s.wd.armed());
}

TEST_CASE("PROPERTY: a socket change erases the per-connection state, whatever preceded it") {
    // Ported from the deleted test_staleness.cpp, which swept the same property
    // over the estimator this replaces.
    std::mt19937 rng(20260820u);
    std::uniform_real_distribution<double> cadence(50.0, 4000.0);
    std::uniform_int_distribution<int> count(0, 120);
    std::uniform_int_distribution<int> quiet(0, 40'000);

    for (int trial = 0; trial < 200; ++trial) {
        Signal s;
        s.tick(cadence(rng), count(rng));
        s.silence(static_cast<double>(quiet(rng)));
        if (s.wd.expired(s.now)) { s.wd.note_fired(s.now); }
        s.wd.on_socket_change(s.now);

        CHECK_FALSE(s.wd.armed());
        CHECK_FALSE(s.wd.age_calibrated());
        CHECK(s.wd.deadline_ns() == 0);
        CHECK_FALSE(s.wd.expired(s.now + ms_ns(1'000'000.0)));
    }
}

TEST_CASE("PROPERTY: the deadline is exactly the last arrival plus the threshold, always") {
    // ADDED AFTER REVIEW, which pointed out that the sweep above checks five
    // consequences of two assignments and nothing that varies with its three
    // swept parameters — mutate `threshold_ns()` to a fixed 30 s and it stays
    // green through all 200 trials, because nothing it reads touches the
    // threshold.
    //
    // This is the invariant that actually constrains the object, and it is the
    // one that would have caught the backwards-stamp regression on its own:
    // while armed, the deadline is a pure function of the last accepted arrival
    // and the current threshold, and it never moves backwards under a monotone
    // input.
    std::mt19937 rng(20260820u);
    std::uniform_real_distribution<double> cadence(1.0, 9000.0);

    LivenessWatchdog wd;
    std::int64_t now = 0;
    std::int64_t previous_deadline = 0;
    for (int i = 0; i < 400; ++i) {
        now += ms_ns(cadence(rng));
        wd.on_liveness(now);

        REQUIRE(wd.armed());
        // The deadline is the arrival plus the threshold in force, to the
        // nanosecond — this is what the queue-wait arithmetic in feed_task.cpp
        // subtracts a clock from.
        const std::int64_t want = now + static_cast<std::int64_t>(wd.threshold_ms_exact() * 1e6);
        REQUIRE(wd.deadline_ns() == want);
        // And it is not expired at the instant it arrived, for any cadence.
        REQUIRE_FALSE(wd.expired(now));
        REQUIRE(wd.expired(want));
        // Monotone input, monotone deadline: the threshold can fall as the
        // median falls, but never below the floor, so a deadline can only
        // retreat by less than the arrival advanced.
        REQUIRE(wd.deadline_ns() >= previous_deadline - ms_ns(kThresholdCeilingMs));
        previous_deadline = wd.deadline_ns();
    }
}

TEST_CASE("a clock that goes backwards changes nothing at all") {
    // Ported from the deleted test_staleness.cpp, and then REWRITTEN after
    // review, because the ported form asserted where the right answer and the
    // wrong one coincide. It checked `expired()` AT the backwards stamp — which
    // is `last_ns_` itself, so the comparison is trivially false whatever the
    // code does — and `age().ms == 0`, which `AgeEstimator::read` clamps to by
    // construction for any input. Only the worst-gap check could ever fail, and
    // the guard it covered was the one that mattered least.
    //
    // What the title claims, and what is now asserted, is that a backwards stamp
    // is inert: it moves no deadline, no threshold, no sample count. The
    // implementation at the time did NOT have that property — it excluded the
    // stamp from its own instrument and then handed it to both clocks and
    // regressed `last_ns_`, so `deadline_ns()` moved into the past and the very
    // next `expired(now)` greyed a healthy panel.
    Signal s;
    s.tick(1000.0, 40);

    const std::uint64_t worst = s.wd.worst_gap_ns();
    const std::int64_t deadline = s.wd.deadline_ns();
    const std::uint32_t threshold = s.wd.threshold_ms();
    const std::uint32_t median = s.wd.median_ms();
    const std::uint32_t samples = s.wd.samples();
    const std::int64_t backwards = s.now - ms_ns(5000.0);

    s.wd.on_liveness(backwards);

    CHECK(s.wd.worst_gap_ns() == worst);
    CHECK(s.wd.deadline_ns() == deadline);      // THE ONE THAT GREYS A PANEL
    CHECK(s.wd.threshold_ms() == threshold);    // no negative interval in the window
    CHECK(s.wd.median_ms() == median);
    CHECK(s.wd.samples() == samples);
    CHECK(s.wd.non_monotone() == 1);            // and it is counted, not silent
    CHECK_FALSE(s.wd.expired(s.now));           // at the REAL clock, not the stale one

    // An equal stamp is refused for the same reason: a zero interval is not a
    // measurement of a 1 Hz venue.
    s.wd.on_liveness(s.now);
    CHECK(s.wd.non_monotone() == 2);
    CHECK(s.wd.samples() == samples);
}

TEST_CASE("the drain burst cannot grey the recovery it is part of — at EITHER venue") {
    // The floor is load-bearing and this is the case that bites, not the fast
    // venue. When a backlogged socket drains, the queued frames arrive nearly
    // together, the rolling median collapses toward zero, and an uncapped
    // k x median would grey a healthy feed at the exact moment it recovered.
    //
    // SWEPT OVER BOTH CADENCES AFTER REVIEW, and the sweep is what found the
    // floor to be wrong. It was 1,000 ms — chosen as "the smallest threshold
    // this project has ever run", which was kRxWatchdogMs, a bound on BOOK-EVENT
    // silence at Anvil. This bounds LIVENESS silence, whose worst healthy values
    // are 968.8 ms at Anvil and 1,119.0 ms at KRAKEN. A 1,000 ms floor is below
    // the second of those, so a burst-clamped window greyed a healthy Kraken feed
    // on an ordinary heartbeat — the exact false grey the floor exists to
    // prevent, produced by the floor itself. Proving it only at 500 ms hid it.
    struct Venue {
        double cadence_ms;
        double worst_healthy_ms;
    };
    const Venue venues[] = {{500.0, 968.8}, {1000.0, 1119.0}};

    for (const Venue& v : venues) {
        Signal s;
        s.tick(v.cadence_ms, 40);
        s.tick(1.0, 40);                              // the flush: 1 ms apart
        CHECK(s.wd.median_ms() == 1u);
        CHECK(s.wd.threshold_ms() == static_cast<std::uint32_t>(kThresholdFloorMs));

        // The venue's WORST HEALTHY interval, arriving into a still-clamped
        // window, must not grey the panel. This is the assertion the old floor
        // failed at Kraken.
        s.silence(v.worst_healthy_ms);
        CHECK_FALSE(s.wd.expired(s.now));
    }
}

TEST_CASE("a stall does not re-latch the age baseline from the burst that ends it") {
    // FOUND BY REVIEW, and it is the quietest failure in this file's subject.
    // `note_fired` used to call `AgeEstimator::on_reconnect`, which discards the
    // BASELINE as well as the reading. A baseline is measurable exactly once,
    // at connect, when the server-side queue is empty; re-latching it after a
    // stall measures the DRAIN instead.
    //
    // Anvil queues and never drops per socket, so a 40 s stall accumulates ~80
    // summaries that flush in ~200 ms. A baseline latched from those reads ~2.5
    // ms against a true 500, and every age afterwards accrues 99.5% of wall
    // clock as phantom lag — on a feed that is perfectly current.
    Signal s;
    s.tick(500.0, 40);
    const std::uint32_t baseline = s.wd.baseline_ms();
    REQUIRE(baseline == 500u);

    s.silence(40'000.0);
    REQUIRE(s.wd.expired(s.now));
    s.wd.note_fired(s.now);
    CHECK(s.wd.baseline_ms() == baseline);       // survives the stall

    // The drain: 80 queued summaries, ~2.5 ms apart.
    s.tick(2.5, 80);
    CHECK(s.wd.baseline_ms() == baseline);       // and survives the burst

    // Healthy again. The age must read ~0, not "99.5% of everything since".
    s.tick(500.0, 100);
    const AgeReading r = s.wd.age(s.now);
    REQUIRE(r.valid);
    CHECK(r.ms < 2000u);
}

TEST_CASE("the grey ledger counts episodes, not gaps, and folds in the open one") {
    // A4's soak line is read from these three numbers, and until review they
    // lived in a `.cpp` no host build compiles.
    GreyLedger g;

    // A device that boots stale opens an episode at its first published frame —
    // there is no "was live" to transition from.
    g.note(false, 0);
    CHECK(g.episodes() == 1);
    CHECK(g.grey_now());

    // A LINE TAKEN MID-OUTAGE REPORTS THE OUTAGE, which is the reading an
    // overnight run most needs and the one the naive version gets backwards.
    //
    // AND THE EPISODE STARTS AT UPTIME ZERO, which is the whole point of this
    // case: a board that boots stale publishes its first frame at 0, and 0 was
    // the sentinel the first draft used for "live". Every assertion in this
    // block is therefore also an assertion that a real timestamp of 0 is not
    // being read as an absent one.
    CHECK(g.total_ms(4'000'000) == 4000);
    CHECK(g.closed_ms() == 0);                 // nothing has closed yet

    g.note(true, 5'000'000);
    CHECK(g.episodes() == 1);
    CHECK_FALSE(g.grey_now());
    CHECK(g.total_ms(9'000'000) == 5000);      // and it stops accruing when live
    CHECK(g.closed_ms() == 5000);

    // ONE EPISODE HOWEVER MANY GAPS PRODUCED IT. A Kraken heal publishes
    // Gap{ChecksumFail} and then Gap{Resync} back to back; counting gaps would
    // read 2 and put a second fault in the log that never happened.
    g.note(false, 10'000'000);
    g.note(false, 10'000'100);
    g.note(false, 10'000'200);
    CHECK(g.episodes() == 2);
    g.note(true, 12'000'000);
    CHECK(g.episodes() == 2);
    CHECK(g.total_ms(12'000'000) == 7000);

    // Repeated live frames do not reopen or double-close.
    g.note(true, 13'000'000);
    g.note(true, 14'000'000);
    CHECK(g.episodes() == 2);
    CHECK(g.total_ms(14'000'000) == 7000);
}

TEST_CASE("liveness clock: the cached threshold equals the recomputed one at every step") {
    // `threshold_ms()` is O(1) because the value is cached in `on_liveness`, and
    // the firmware's feed loop queries it on every queue wake — on a part with
    // no double-precision FPU, where the uncached form is a soft-float sort of
    // 32 doubles. The cache is only legitimate if it is exactly equal, so this
    // recomputes the answer independently and compares at every step.
    //
    // Deliberately NOT a smooth cadence: a random walk moves the median's rank
    // around, which is the only way a stale cache shows up.
    std::mt19937 rng(20260820u);
    std::uniform_real_distribution<double> jitter(0.5, 9000.0);

    LivenessClock clock;
    std::vector<double> intervals;
    std::int64_t now = 0;
    for (int i = 0; i < 500; ++i) {
        const double gap = jitter(rng);
        now += ms_ns(gap);
        clock.on_liveness(now);
        if (i > 0) {
            intervals.push_back(gap);
            if (intervals.size() > depthcharge::kWindowSamples) { intervals.erase(intervals.begin()); }
        }

        // The independent recomputation: lower median by nearest rank over the
        // retained window, times k, clamped.
        double expected = kUncalibratedThresholdMs;
        if (intervals.size() >= kMinSamples) {
            std::vector<double> sorted = intervals;
            std::sort(sorted.begin(), sorted.end());
            const double median = sorted[(sorted.size() - 1) / 2];
            const double t = kThresholdMultiple * median;
            expected = t < kThresholdFloorMs   ? kThresholdFloorMs
                     : t > kThresholdCeilingMs ? kThresholdCeilingMs
                                               : t;
        }
        REQUIRE(clock.threshold_ms() == doctest::Approx(expected));
    }
}

// =====================================================================
// M5 STAGE D-A3, DELIVERABLE 2 — THE POLICY REACHES THE CLOCK
// =====================================================================
//
// Stage C derived a per-venue policy and put it in the HARNESS venue table.
// `firmware/` cannot include `dc_harness/`, so the board never received it and
// ran the shipping defaults while every document quoted the derived number.
// These cases are the difference, asserted rather than described.

TEST_CASE("D-A3: the venue's policy reaches the clock, and the derived threshold replaces the clamp") {
    // The Binance server-ping median, measured at stage B2 over the committed
    // 221 s calibration capture and the number every derived figure rests on.
    constexpr double kBinanceMedianMs = 19'963.97;

    // WHAT THE BOARD ACTUALLY RAN UNTIL THIS STAGE, and it is the defect rather
    // than a baseline: a default-constructed clock takes Anvil's 4.0, so
    // 4.0 x 19,963.97 = 79,855.88 ms is clamped to the 30,000 ms ceiling — the
    // identical number `kUncalibratedThresholdMs` already held. The
    // self-calibration was a constant wearing a calibration's clothes.
    Signal defaulted;
    defaulted.tick(kBinanceMedianMs, static_cast<int>(kMinSamples) + 1);
    CHECK(defaulted.wd.calibrated());
    CHECK(defaulted.wd.threshold_ms_exact() == doctest::Approx(kThresholdCeilingMs));

    // WHAT IT RUNS NOW: 2.0 x the same median, under a ceiling that clears it.
    Signal binance{depthcharge::venue_liveness::kBinance};
    binance.tick(kBinanceMedianMs, static_cast<int>(kMinSamples) + 1);
    CHECK(binance.wd.calibrated());
    CHECK(binance.wd.threshold_ms_exact() == doctest::Approx(39'927.94));

    // And the 32-bit mirror the console reads, because that is the number a
    // soak is read from and it rounds rather than truncates.
    CHECK(binance.wd.threshold_ms() == 39'928u);

    // The two are not merely different — the clamp was BELOW the derived
    // threshold, which is what made it a threshold rather than a ceiling.
    CHECK(defaulted.wd.threshold_ms_exact() < binance.wd.threshold_ms_exact());
}

TEST_CASE("D-A3: the pre-calibration threshold is the VENUE's ceiling, from the first line of the boot") {
    // `uncalibrated_ms()` IS the ceiling, so a policy-constructed watchdog must
    // report 60,000 ms before it has heard anything — not the 30,000 ms global.
    // This is the first line of every boot and the first 159.7 s of every
    // connection at a 20 s cadence, so getting it from the global constant would
    // put a threshold this build does not run on the log D-C reads.
    Signal binance{depthcharge::venue_liveness::kBinance};
    CHECK_FALSE(binance.wd.calibrated());
    CHECK(binance.wd.threshold_ms() == 60'000u);

    Signal shipping;
    CHECK_FALSE(shipping.wd.calibrated());
    CHECK(shipping.wd.threshold_ms() == static_cast<std::uint32_t>(kUncalibratedThresholdMs));
}

TEST_CASE("D-A3: routing a policy moves NOTHING at Anvil and Kraken — checked, because it is no longer structural") {
    // Stage C could say Anvil and Kraken *"do not move at all, by construction"*
    // because `firmware/` passed nothing. Deliverable 2 makes it pass something
    // at EVERY venue, so the construction is gone and "unchanged" becomes a
    // claim that has to be tested. `venue_liveness.hpp`'s static_asserts hold the
    // constants equal to the defaults; this holds the BEHAVIOUR equal, which is
    // the part a reader of a soak log cares about.
    for (const double cadence_ms : {500.0, 1'000.0, 4'006.0, 19'963.97}) {
        Signal shipping;
        Signal anvil{depthcharge::venue_liveness::kAnvil};
        Signal kraken{depthcharge::venue_liveness::kKraken};

        // Before calibration, during, and after.
        for (int n = 1; n <= static_cast<int>(kMinSamples) + 3; ++n) {
            shipping.tick(cadence_ms, 1);
            anvil.tick(cadence_ms, 1);
            kraken.tick(cadence_ms, 1);

            CHECK(anvil.wd.threshold_ms_exact() == doctest::Approx(shipping.wd.threshold_ms_exact()));
            CHECK(kraken.wd.threshold_ms_exact() == doctest::Approx(shipping.wd.threshold_ms_exact()));
            CHECK(anvil.wd.calibrated() == shipping.wd.calibrated());
            CHECK(kraken.wd.calibrated() == shipping.wd.calibrated());
            CHECK(anvil.wd.deadline_ns() == shipping.wd.deadline_ns());
            CHECK(kraken.wd.deadline_ns() == shipping.wd.deadline_ns());
        }
    }
}

// =====================================================================
// M5 STAGE D-A3, DELIVERABLE 3 — THE INTERVAL DISTRIBUTION D-C READS
// =====================================================================

TEST_CASE("D-A3: healthy intervals are counted, and the falsifier's bucket stays empty") {
    using depthcharge::fw::PingScale;
    Signal s{depthcharge::venue_liveness::kBinance};

    // A metronome at the measured cadence. The first arrival cannot produce an
    // interval, so n is one fewer than the arrivals.
    s.tick(19'964.0, 20);
    const auto& iv = s.wd.intervals();
    CHECK(iv.total() == 19);
    CHECK(iv.count_from(PingScale::kFirstLong) == 0);   // the falsifier has not fired
    CHECK(iv.worst_us() / 1000u == 19'964u);
}

TEST_CASE("D-A3: an interval reaching 2x the median lands in the falsifier's bucket") {
    using depthcharge::fw::PingScale;
    Signal s{depthcharge::venue_liveness::kBinance};

    s.tick(19'964.0, 10);
    CHECK(s.wd.intervals().count_from(PingScale::kFirstLong) == 0);

    // One interval at 2x median = 39,928 ms. The bucket edge is 40 s, so this
    // sits just below it -- deliberately checked, because "reaching 2x median"
    // and "landing in the >=40s bucket" are the same claim only if the edge is
    // where the derivation put it.
    s.tick(39'928.0, 1);
    CHECK(s.wd.intervals().count_from(PingScale::kFirstLong) == 0);

    // And one clearly past it.
    s.tick(45'000.0, 1);
    CHECK(s.wd.intervals().count_from(PingScale::kFirstLong) == 1);
    CHECK(s.wd.intervals().worst_us() / 1000u == 45'000u);
}

TEST_CASE("D-A3: an outage-spanning interval is NOT a sample of the venue's cadence") {
    // THE ONE THAT WOULD HAVE MADE THE INSTRUMENT LIE. `on_socket_change` does
    // not reset `last_ns_` -- deliberately, so the next arrival measures the
    // whole hole for `worst_gap_ns_`. Ungated, that same hole would be a ~300 s
    // interval in the falsifier's bucket on the first reconnect of any run, and
    // D-C would read a permanently tripped falsifier as a finding.
    using depthcharge::fw::PingScale;
    Signal s{depthcharge::venue_liveness::kBinance};

    s.tick(19'964.0, 5);
    const std::uint32_t healthy = s.wd.intervals().total();
    CHECK(healthy == 4);

    // The socket dies, five minutes pass, and it comes back.
    s.wd.on_socket_change(s.now);
    s.silence(300'000.0);
    s.tick(19'964.0, 1);   // the first arrival of the new connection

    // The whole-hole measurement still happens, because a test above pins it...
    CHECK(s.wd.worst_gap_ns() > static_cast<std::uint64_t>(300'000) * 1'000'000);
    // ...and the distribution did NOT take it.
    CHECK(s.wd.intervals().total() == healthy);
    CHECK(s.wd.intervals().count_from(PingScale::kFirstLong) == 0);
    CHECK(s.wd.intervals().worst_us() / 1000u == 19'964u);

    // The next arrival IS within the new connection and is counted normally.
    s.tick(19'964.0, 1);
    CHECK(s.wd.intervals().total() == healthy + 1);
}

TEST_CASE("D-A3: a firing also breaks the chain, for the same reason a disconnect does") {
    // `note_fired` clears `armed_` without moving `last_ns_`, so the arrival that
    // ends a stall measures the stall. That is the right answer for the worst-gap
    // instrument and the wrong one for a cadence sample.
    using depthcharge::fw::PingScale;
    Signal s{depthcharge::venue_liveness::kBinance};
    s.tick(19'964.0, 5);
    const std::uint32_t healthy = s.wd.intervals().total();

    s.silence(80'000.0);
    s.wd.note_fired(s.now);
    s.tick(19'964.0, 1);
    CHECK(s.wd.intervals().total() == healthy);          // the stall is not a sample
    CHECK(s.wd.intervals().count_from(PingScale::kFirstLong) == 0);
}
