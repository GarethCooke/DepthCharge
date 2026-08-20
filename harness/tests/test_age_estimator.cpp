// test_age_estimator.cpp — M4 stage A2's definition of done, in assertions.
//
// WHY THESE ARE SYNTHETIC, AND WHY THAT IS NOT A SHORTCUT. The quantity under
// test is QUEUING LAG, and no committed trace can carry its true value: a
// backlog is a property of one client's socket rather than of the wire, so a
// capture's `rx_ns` records only when THIS client got the bytes, and two sockets
// on the same server at the same instant disagree about the book's age. The one
// measured figure this project owns — 111 s of lag over 150 s — exists because
// `tools/anvil_freshness_probe.py` seq-matched a THROTTLED socket against an
// UNTHROTTLED one, which is two sockets and therefore not something a trace file
// can be. Invariant #6 cannot be satisfied by a golden here, so it is satisfied
// the only other honest way: arrival patterns whose true answer is known BY
// CONSTRUCTION, including the kinematics of that 2026-08-11 run.
//
// The committed traces still appear, in test_replay_goldens.cpp — but what they
// pin is that a HEALTHY feed reads ~zero, which is a different claim and a
// weaker one.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <depthcharge/age_estimator.hpp>
#include <depthcharge/liveness_clock.hpp>

using depthcharge::AgeEstimator;
using depthcharge::AgeReading;
using depthcharge::AgeText;
using depthcharge::kAgeWindowSamples;
using depthcharge::kBaselineSamples;
using depthcharge::kMinSamples;
using depthcharge::LivenessClock;

namespace {

constexpr std::int64_t ms(double v) { return static_cast<std::int64_t>(v * 1e6); }

// A venue broadcasting one liveness frame every `interval_ms` of wall time, and
// a socket receiving them at some fraction of that rate. This is the kinematics
// the whole estimator rests on: under a lossless queue the panel's market-time
// cursor advances at the drain fraction f, so age(t) = (1 - f) x t.
//
// The clock and the estimator are driven together exactly as the replay driver
// drives them: both see every arrival, and each extracts its own quantity.
struct Feed {
    AgeEstimator age;
    LivenessClock clock;
    std::int64_t now = 0;

    void deliver_at(double received_interval_ms, int n) {
        for (int i = 0; i < n; ++i) {
            now += ms(received_interval_ms);
            clock.on_liveness(now);
            age.on_liveness(now);
        }
    }
    // Healthy: the socket keeps up, so received cadence == broadcast cadence.
    void healthy(double interval_ms, int n) { deliver_at(interval_ms, n); }
    // Throttled to a fraction of the broadcast rate.
    void drained(double interval_ms, double f, int n) { deliver_at(interval_ms / f, n); }

    AgeReading read() const { return age.read(now); }
    std::uint32_t age_ms() const { return age.read(now).ms; }
};

}  // namespace

TEST_CASE("age: a socket keeping up reads zero, and the sawtooth is its resolution") {
    Feed f;
    f.healthy(500.0, 60);

    CHECK(f.read().valid);
    CHECK(f.age_ms() == 0);
    CHECK(f.age.baseline_ms() == doctest::Approx(500.0));

    // Half an interval after the last arrival it reads half an interval. That is
    // the instrument's RESOLUTION, not a defect: the elapsed term grows between
    // arrivals while the delivered term does not, so anything under one interval
    // is noise (age_estimator.hpp).
    CHECK(static_cast<double>(f.age.read(f.now + ms(250.0)).ms) ==
          doctest::Approx(250.0).epsilon(0.01));
    CHECK(static_cast<double>(f.age.read(f.now + ms(500.0)).ms) ==
          doctest::Approx(500.0).epsilon(0.01));
}

TEST_CASE("age: no reading until the baseline has latched") {
    // kBaselineSamples INTERVALS, which is one more arrival than that — and it
    // is deliberately NOT the threshold clock's shorter gate. The two answer
    // different questions and the difference was set by a measurement: a stream
    // resumes with a burst, and an 8-interval median of the reconnect trace's
    // resumption reads 478 ms against a 500 ms broadcast (kBaselineSamples).
    Feed f;
    for (std::size_t i = 0; i < kBaselineSamples; ++i) {
        f.healthy(500.0, 1);
        // "No reading yet" and "the book is current" are different statements,
        // and printing 0.0 s for the first would be the reassuring one.
        CHECK_FALSE(f.read().valid);
        CHECK(f.age.baseline_ms() == 0.0);
    }
    f.healthy(500.0, 1);
    CHECK(f.age.samples() == kBaselineSamples + 1);
    CHECK(f.age.calibrated());
    CHECK(f.read().valid);

    // The threshold clock calibrated long before this one, which is the point of
    // them being separate gates: greying early is safe, guessing the venue's
    // clock early is not.
    CHECK(f.clock.calibrated());
    CHECK(kMinSamples < kBaselineSamples);

    // The latch is once per connection: a later cadence change must not move it,
    // because after the first window the queue is a confounder.
    const double latched = f.age.baseline_ms();
    f.drained(500.0, 0.25, 40);
    CHECK(f.age.baseline_ms() == latched);
}

TEST_CASE("age: the 2026-08-11 desk probe's kinematics, as a degradation after connect") {
    // The measurement this estimator exists to reproduce (ARCHITECTURE §9,
    // 2026-08-11): a socket receiving 25.7% of the summary broadcast saw its lag
    // rise LINEARLY to 111 s over 150 s with no plateau. The kinematic
    // prediction is age = (1 - f) x t = 0.743 x 150 = 111.5 s.
    //
    // ONE HONEST DIFFERENCE FROM THE PROBE, STATED RATHER THAN GLOSSED: that run
    // was throttled from its first message, which this estimator is blind to by
    // construction (see the blind-spot case below). Here the same f is applied
    // as a degradation AFTER the connection has measured its baseline, which is
    // the shape a board actually meets in the field — A3's unbounded server-side
    // queue building when the path slows, not a client that was never able to
    // keep up.
    constexpr double f = 0.257;
    Feed feed;
    feed.healthy(500.0, 40);                    // baseline latched at 500 ms
    REQUIRE(feed.age.baseline_ms() == doctest::Approx(500.0));
    REQUIRE(feed.age_ms() == 0);

    const std::int64_t throttle_start = feed.now;
    const int frames = static_cast<int>(150.0 / (0.5 / f));   // 150 s at the drained rate
    feed.drained(500.0, f, frames);

    const double seconds = static_cast<double>(feed.now - throttle_start) / 1e9;
    const double predicted_ms = (1.0 - f) * seconds * 1000.0;
    CHECK(static_cast<double>(feed.age_ms()) == doctest::Approx(predicted_ms).epsilon(0.02));

    // The headline: it lands on the desk probe's own figure, not merely on its
    // own arithmetic.
    CHECK(feed.age_ms() > 105000u);
    CHECK(feed.age_ms() < 118000u);

    // AND THE CONTROL THAT MAKES THE DESIGN DECISION VISIBLE. The socket's
    // ROLLING median has by now become the throttled interval, so an age
    // computed against it — the obvious implementation, sharing one median with
    // the grey threshold — would read ZERO through all 111 seconds of this. The
    // estimator does not share it, and this is the assertion that says so.
    CHECK(feed.clock.median_ms() == doctest::Approx(500.0 / f).epsilon(0.01));
    CHECK(feed.age.baseline_ms() == doctest::Approx(500.0));
}

TEST_CASE("age: the baseline does not survive the connection that measured it") {
    // THE RECONNECT CASE, AND IT IS WHY THE ESTIMATOR MEASURES ITS OWN BASELINE
    // RATHER THAN READING THE THRESHOLD CLOCK'S. LivenessClock is deliberately
    // not reset at a disconnect — cadence is a property of the venue, not of the
    // socket — so its rolling median still holds the DEAD connection's throttled
    // arrivals for up to a window afterwards. A baseline taken from it would make
    // a fresh, healthy socket report no lag for ever.
    Feed f;
    f.healthy(500.0, 40);
    f.drained(500.0, 0.25, 40);                 // this socket falls behind badly
    REQUIRE(f.age_ms() > 20000u);

    f.age.on_reconnect(f.now);
    const double stale_rolling_median = f.clock.median_ms();
    REQUIRE(stale_rolling_median > 1500.0);     // the dead socket's cadence, still there

    // The new connection is healthy from its first frame and measures 500 ms for
    // itself — not the 2,000 ms the clock is still carrying.
    f.healthy(500.0, 40);
    CHECK(f.age.baseline_ms() == doctest::Approx(500.0));
    CHECK(f.age_ms() == 0);

    // Now degrade the NEW connection and check the meter can still see it. With
    // an inherited baseline this would read zero.
    f.drained(500.0, 0.5, 60);
    CHECK(f.age_ms() > 25000u);
}

TEST_CASE("age: stall-then-burst is caught, not averaged away") {
    // The failure shape a CUMULATIVE estimator reports as healthy: a pause, then
    // a flood that repays the count. Over the whole episode the average rate is
    // right; for 20 s of it the book on screen was 20 s old.
    Feed f;
    f.healthy(500.0, 64);
    REQUIRE(f.age_ms() == 0);

    f.now += ms(20000.0);                       // 20 s of nothing
    CHECK(static_cast<double>(f.age_ms()) == doctest::Approx(20000.0).epsilon(0.01));
    const AgeReading peak = f.age.read_and_bank(f.now);   // as the feed side does, at publish
    REQUIRE(peak.valid);

    // The burst: 40 queued frames arriving 5 ms apart. That is 20 s of stream
    // time repaid, so the estimate must come back DOWN — an estimator that only
    // integrates would still be reading 20 s.
    //
    // DOWN TO 200 ms, NOT TO ZERO, and the residual is exactly right rather than
    // slop: the burst took 40 x 5 ms to arrive, and a socket is still that far
    // behind the moment it finishes draining. It is under one interval, which is
    // this instrument's resolution, so the panel reads 0.2s.
    f.deliver_at(5.0, 40);
    CHECK(static_cast<double>(f.age_ms()) == doctest::Approx(200.0).epsilon(0.01));
    CHECK(f.age_ms() < 500u);   // i.e. below the one-interval noise floor

    // The peak survived the recovery, because `sample()` banked it.
    CHECK(f.age.worst_ms() >= 19000u);

    // And the burst did not move the baseline: 5 ms is a repayment, not a
    // venue that suddenly broadcasts at 200 Hz. A reference that ratcheted down
    // to it would report the whole window as lag for ever after.
    CHECK(f.age.baseline_ms() == doctest::Approx(500.0));
}

TEST_CASE("age: a bias ages out of the window instead of ratcheting") {
    // One liveness frame that never arrives — dropped, or rejected by the parser
    // at a chunk boundary, which this project has measured — adds one interval
    // of apparent lag. A cumulative estimator carries that for the life of the
    // connection (staleness.hpp names the bias and can only ask the reader to
    // cross-check a counter). The window forgets it.
    Feed f;
    f.healthy(500.0, 40);
    f.deliver_at(1000.0, 1);                    // one interval missing, never repaid
    CHECK(static_cast<double>(f.age_ms()) == doctest::Approx(500.0).epsilon(0.05));

    f.healthy(500.0, static_cast<int>(kAgeWindowSamples));
    CHECK(f.age_ms() == 0);
}

TEST_CASE("age: the window bounds what can be reported, and the bound is stated") {
    // The ceiling is not a defect, but it must be known: the estimator reports
    // the lag accumulated WITHIN the last kAgeWindowSamples arrivals. A backlog
    // older than the window reads as the window's worth of it.
    Feed f;
    f.healthy(500.0, 40);
    f.drained(500.0, 0.5, static_cast<int>(kAgeWindowSamples) * 3);

    // At f = 0.5 the window spans N x 1000 ms and the lag accrued across it is
    // half that — the ceiling, reached and held.
    const double ceiling_ms = 0.5 * static_cast<double>(kAgeWindowSamples) * 1000.0;
    CHECK(static_cast<double>(f.age_ms()) == doctest::Approx(ceiling_ms).epsilon(0.02));

    // The true lag by now is three times larger, so the estimator UNDER-reports
    // when it is out of window. That is the right direction for a number printed
    // beside a live ladder: it never claims a book is older than it can show.
    CHECK(static_cast<double>(f.age_ms()) < 3.0 * ceiling_ms);
}

TEST_CASE("age: a total silence has no ceiling — the window stops refilling") {
    // The case that must NOT be confused with the ceiling above. The window is
    // over arrivals, not over time, so when nothing arrives the oldest retained
    // sample stays put and the sup grows without bound. A2b's 176,000 ms silence
    // of 2026-08-16 is this shape, and it is the reading the panel would have
    // shown for two minutes fifty-six seconds had it existed that day.
    Feed f;
    f.healthy(500.0, static_cast<int>(kAgeWindowSamples) + 40);   // window full

    const std::int64_t much_later = f.now + ms(176000.0);
    CHECK(static_cast<double>(f.age.read(much_later).ms) ==
          doctest::Approx(176000.0).epsilon(0.01));

    // (The panel is grey long before this — the liveness clock fires at 4x the
    // median. Age and grey are separate channels saying separate things.)
    CHECK(f.clock.threshold_ms() == doctest::Approx(2000.0).epsilon(0.01));
}

TEST_CASE("age: THE BLIND SPOT — a socket behind from birth reads zero, and that is a proof") {
    // A connection already behind when it latched its baseline cannot know it.
    // From one socket the two cases are not distinguishable at all: "the venue
    // broadcasts at 2 Hz and I am two minutes behind" and "the venue broadcasts
    // at 0.5 Hz and I am current" produce byte-identical wire behaviour.
    //
    // This is pinned as a KNOWN LIMIT so that a future reader meets it here
    // rather than discovering it as a bug — and so that the case for the client
    // ping (M4 triage decision (a), deferred to M6) rests on a test rather than
    // on a paragraph. Both `_local/drain-120ms.ndjson` and the 2026-08-11 desk
    // probe are this shape by construction: the capture tool sleeps from the
    // first message.
    Feed f;
    f.drained(500.0, 0.25, 80);                 // throttled before the first sample

    CHECK(f.age.baseline_ms() == doctest::Approx(2000.0));   // the throttled cadence
    CHECK(f.age_ms() == 0);                                  // ... so: no lag detected
    // Server-side this cannot happen — a queue that has not been created yet
    // cannot be full — so what is undetectable here is a CLIENT too slow to
    // drain the wire from birth. The board's own RX loop was instrumented over
    // the 23.6 h soak for exactly this and exonerated.
}

TEST_CASE("age: the backlog dies with the socket") {
    Feed f;
    f.healthy(500.0, 40);
    f.drained(500.0, 0.4, 60);
    REQUIRE(f.age_ms() > 10000u);
    const std::uint32_t before = f.age_ms();

    f.age.on_reconnect(f.now);

    // Nothing is claimed about the new connection until it has a window of its
    // own: a fresh socket gets a fresh queue, a fresh snapshot, and a fresh
    // measurement of the venue's clock.
    CHECK_FALSE(f.age.read(f.now).valid);
    CHECK(f.age.samples() == 0);
    CHECK(f.age.baseline_ms() == 0.0);

    // But the peak survived the event that erased the estimate. This is the
    // defect staleness.hpp shipped with for one afternoon: high-water marks
    // written only on arrival lose the whole episode at the drop.
    CHECK(f.age.worst_ms() >= before);
}

TEST_CASE("age: a burst cannot make the book come from the future") {
    Feed f;
    f.healthy(500.0, 40);
    f.deliver_at(10.0, 40);                     // everything far faster than nominal
    CHECK(f.read().valid);
    CHECK(f.age_ms() == 0);                     // clamped at zero, never negative
}

// ---------------------------------------------------------------------------
// PROPERTIES, over random arrival patterns rather than chosen ones.
//
// The cases above each pin one scenario, which is what makes them readable and
// also what makes them a sieve with large holes: every one of them was written
// by someone who already believed the implementation. These check the invariants
// instead, across 200 patterns built from a fixed seed — deterministic, because
// a test that fails one run in fifty is a test this project would learn to
// ignore, and reproducible from the seed when it does fail.
// ---------------------------------------------------------------------------
TEST_CASE("age: the invariants hold over random arrival patterns") {
    std::mt19937 rng(20260817u);            // fixed: a golden must not be a lottery
    std::uniform_real_distribution<double> interval(1.0, 4000.0);
    std::uniform_int_distribution<int> count(static_cast<int>(kBaselineSamples) + 2, 400);

    for (int trial = 0; trial < 200; ++trial) {
        AgeEstimator age;
        std::int64_t t = 0;
        std::int64_t oldest_retained = 0;
        const int n = count(rng);
        std::vector<std::int64_t> arrivals;
        for (int i = 0; i < n; ++i) {
            t += ms(interval(rng));
            age.on_liveness(t);
            arrivals.push_back(t);
        }
        oldest_retained = arrivals[arrivals.size() > kAgeWindowSamples
                                       ? arrivals.size() - kAgeWindowSamples
                                       : 0];
        CAPTURE(trial);
        CAPTURE(n);
        REQUIRE(age.calibrated());

        const AgeReading now = age.read(t);
        REQUIRE(now.valid);

        // 1 · NEVER NEGATIVE, AND NEVER OLDER THAN THE WINDOW ITSELF. The book
        //     cannot be more stale than the span of arrivals the estimator can
        //     still see — an age above that would be invented rather than
        //     measured.
        const double window_span_ms = static_cast<double>(t - oldest_retained) / 1e6;
        CHECK(static_cast<double>(now.ms) <= window_span_ms + 1.0);

        // 2 · MONOTONE IN TIME. With nothing arriving, the book only gets older;
        //     an estimator that could fall while the feed is silent would be
        //     rewarding the silence.
        std::uint32_t prev = now.ms;
        for (const double wait_ms : {10.0, 250.0, 3000.0, 60000.0}) {
            const AgeReading later = age.read(t + ms(wait_ms));
            CHECK(later.ms >= prev);
            prev = later.ms;
        }

        // 3 · AN ARRIVAL IS A REPAYMENT: delivering one more frame, at a fixed
        //     `now`, can never make the book look OLDER. This is the property
        //     that would break first if the suffix scan or the ring indexing were
        //     wrong, and it is not obvious from the code.
        const std::int64_t probe = t + ms(5000.0);
        const std::uint32_t before = age.read(probe).ms;
        AgeEstimator plus_one = age;
        plus_one.on_liveness(t + ms(1.0));
        CHECK(plus_one.read(probe).ms <= before);

        // 4 · THE BASELINE IS THE CONNECTION'S OWN FIRST WINDOW, always, whatever
        //     happened afterwards.
        std::vector<double> first_gaps;
        for (std::size_t i = 0; i < kBaselineSamples; ++i) {
            first_gaps.push_back(static_cast<double>(arrivals[i + 1] - arrivals[i]) / 1e6);
        }
        std::sort(first_gaps.begin(), first_gaps.end());
        CHECK(age.baseline_ms() ==
              doctest::Approx(first_gaps[(kBaselineSamples - 1) / 2]).epsilon(1e-9));
    }
}

TEST_CASE("age: a feed exactly at its own baseline reads zero at every arrival") {
    // The round-trip property: whatever cadence a connection measures for itself,
    // holding that cadence must read zero — at any interval, not just at 500 ms.
    // This is the one property that ties the two halves together, and it fails
    // immediately if the latch and the deficit disagree about what an interval is.
    std::mt19937 rng(20260818u);
    std::uniform_real_distribution<double> interval(20.0, 5000.0);

    for (int trial = 0; trial < 50; ++trial) {
        const double step = interval(rng);
        AgeEstimator age;
        std::int64_t t = 0;
        for (int i = 0; i < 60; ++i) {
            t += ms(step);
            age.on_liveness(t);
        }
        CAPTURE(step);
        CHECK(age.baseline_ms() == doctest::Approx(step).epsilon(0.001));
        CHECK(age.read(t).ms == 0);
    }
}

TEST_CASE("age text: renders to minutes and hours, and never saturates") {
    // THE REQUIREMENT, and its precedent: `SecondsText` took uint32
    // microseconds, which is 71.6 minutes, and the 23.6 h soak printed
    // `4294.9 s` for eleven straight hours while the true age climbed past it.
    CHECK(std::string(AgeText(0).buf) == "0.0s");
    CHECK(std::string(AgeText(499).buf) == "0.4s");
    CHECK(std::string(AgeText(1500).buf) == "1.5s");
    CHECK(std::string(AgeText(59999).buf) == "59.9s");

    CHECK(std::string(AgeText(60000).buf) == "1m00s");
    CHECK(std::string(AgeText(111000).buf) == "1m51s");   // the desk probe's figure
    CHECK(std::string(AgeText(176000).buf) == "2m56s");   // A2b's stall, had the panel existed
    CHECK(std::string(AgeText(3599999).buf) == "59m59s");

    CHECK(std::string(AgeText(3600000).buf) == "1h00m");
    CHECK(std::string(AgeText(4294900).buf) == "1h11m");  // where SecondsText's ceiling was
    CHECK(std::string(AgeText(4294967295u).buf) == "1193h02m");   // uint32 ms = 49.7 days

    // The absent reading is a dash, not a zero.
    CHECK(std::string(AgeText::unknown().buf) == "-");
}
