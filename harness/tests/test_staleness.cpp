// test_staleness.cpp — the summary-deficit age estimator, on the desk.
//
// The fifth firmware header the host build knows about, and the one with the
// most to lose from being wrong quietly. `staleness.hpp` produces a single
// number that a bench evening will read as "the panel is N seconds behind the
// market", and every way it can be wrong produces a plausible N: an off-by-one
// on the anchor adds half a second to a healthy socket, counting the anchor as a
// delivery subtracts one, an unsigned subtraction in the wrong order turns a
// small lag into 4,294 seconds, and a reset that does not reset carries a dead
// connection's backlog into a live one.
//
// So the shape of this file is: pin the arithmetic against hand-computed
// answers, then drive it with two *simulated wires* whose true lag is known by
// construction — a perfectly-drained socket, which must read ~0, and a socket
// receiving a fixed fraction of the broadcast, which must read the deficit the
// fraction implies. The second one is the 2026-08-11 bench reproduced on the
// desk: 41% of a 2 Hz broadcast for 210 s must come out at ~124 s, which is the
// figure the header calibrates against the owner's stopwatch.
#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "staleness.hpp"

using depthcharge::fw::drain_percent;
using depthcharge::fw::kSummaryPeriodUs;
using depthcharge::fw::StalenessEstimator;

namespace {

constexpr std::int64_t kPeriod = static_cast<std::int64_t>(kSummaryPeriodUs);

// A socket that receives one summary in every `divisor` broadcast slots,
// starting at `start_us`, for `slots` slots. divisor == 1 is a socket keeping up
// exactly; divisor == 2 is one receiving half of them.
//
// Deliberately drives the estimator through its real entry point rather than
// setting fields: the anchor rule and the "the first summary is not a delivery"
// rule are the two most likely places for an off-by-one, and both live on that
// path.
void feed_fraction(StalenessEstimator& est, std::int64_t start_us, int slots, int divisor) {
    for (int i = 0; i < slots; ++i) {
        if (i % divisor == 0) { est.note_summary(start_us + static_cast<std::int64_t>(i) * kPeriod); }
    }
}

std::string rendered(const StalenessEstimator& est, std::int64_t now_us) {
    char buf[192];
    est.render(now_us, buf, sizeof buf);
    return std::string(buf);
}

}  // namespace

TEST_CASE("an estimator that has seen nothing reports no reading, not zero lag") {
    StalenessEstimator est;
    CHECK_FALSE(est.anchored());
    CHECK(est.lag_us(5'000'000) == 0);
    CHECK(est.expected(5'000'000) == 0);
    CHECK(est.window_us(5'000'000) == 0);
    // "no reading yet" and "the book is current" are different statements and
    // exactly one of them is reassuring; the console must not be able to print
    // the reassuring one by accident.
    CHECK(rendered(est, 5'000'000).find("no reading yet") != std::string::npos);

    est.note_connect(1'000'000);
    CHECK_FALSE(est.anchored());  // a connect is not an anchor — the phase is unknown
    CHECK(est.lag_us(9'000'000) == 0);
}

TEST_CASE("the first summary anchors and is not itself a delivery") {
    StalenessEstimator est;
    est.note_connect(1'000'000);
    est.note_summary(1'234'567);

    CHECK(est.anchored());
    CHECK(est.received() == 0);
    CHECK(est.summaries_total() == 1);
    // At the anchor instant nothing has elapsed, so nothing is owed.
    CHECK(est.lag_us(1'234'567) == 0);
    // ANCHORING ON THE CONNECT INSTEAD WOULD HAVE REPORTED 234 ms HERE, on a
    // socket that has not yet had a chance to be late. A connect lands at a
    // uniformly random phase inside the 500 ms broadcast period, so that bias is
    // 250 ms on average and is pure fiction.
    CHECK(est.window_us(1'234'567) == 0);
}

TEST_CASE("a socket keeping up reads a sawtooth between zero and one period") {
    StalenessEstimator est;
    est.note_connect(0);
    feed_fraction(est, 1'000'000, 200, 1);  // 100 s of a perfectly drained 2 Hz stream

    const std::int64_t last = 1'000'000 + 199 * kPeriod;
    CHECK(est.received() == 199);
    CHECK(est.lag_us(last) == 0);                        // read at a summary: exactly current
    CHECK(est.lag_us(last + kPeriod / 2) == 250'000);    // half a period later
    CHECK(est.lag_us(last + kPeriod - 1) == 499'999);    // just before the next one
    // The high-water mark is taken only at summary arrivals, so a socket that is
    // never late records nothing however often it is read.
    CHECK(est.worst_lag_us() == 0);
    CHECK(est.worst_lag_ever_us() == 0);
    CHECK(est.worst_ahead_us() == 0);
}

TEST_CASE("a socket receiving half the broadcast falls behind at half a second per second") {
    StalenessEstimator est;
    est.note_connect(0);

    // Fed in two halves, and read between them, because LINEARITY is the whole
    // claim — a plateau and a straight line are the two answers deliverable 1
    // exists to separate, and an estimator read only at the end cannot tell them
    // apart. (lag_us() reports the state as it stands NOW, so a later call with
    // an earlier timestamp is not a look backwards; the reading has to be taken
    // at the time.)
    feed_fraction(est, 1'000'000, 100, 2);  // slots 0,2,..,98 => 50 summaries
    const std::int64_t half = 1'000'000 + 99 * kPeriod;
    CHECK(est.received() == 49);
    CHECK(est.lag_us(half) == 25'000'000);  // 49.5 s of clock, 24.5 s delivered

    feed_fraction(est, 1'000'000 + 100 * kPeriod, 100, 2);
    const std::int64_t full = 1'000'000 + 199 * kPeriod;
    CHECK(est.received() == 99);
    CHECK(est.lag_us(full) == 50'000'000);  // 99.5 s of clock, 49.5 s delivered

    // Twice the elapsed time, twice the lag: the queuing signature, and the
    // estimator must not smooth it away.
    CHECK(est.worst_lag_us() == 49'500'000);  // taken at the last summary itself
}

TEST_CASE("the 2026-08-11 bench reproduced: 41% of a 2 Hz broadcast over 210 s") {
    // The board read `summary` at 0.82/s against 2.00/s at 210 s of uptime and
    // the owner's stopwatch put the panel 96-98 s behind. This pins what the
    // estimator WOULD have said, which is the number the header's calibration
    // note is written from — and it is deliberately a test of the estimator, not
    // a claim about the board: the ~26% over-read is the finding, and it is
    // recorded rather than tuned away.
    StalenessEstimator est;
    est.note_connect(0);
    // 172 summaries evenly spread over 210 s — 0.82/s, the figure the board
    // printed — rather than a fraction of the broadcast slots, so the numbers
    // here are the bench's own and not a re-derivation of them.
    constexpr int kSummaries = 172;
    constexpr std::int64_t kSpanUs = 210'000'000;
    const std::int64_t t0 = 1'000'000;
    for (int i = 0; i < kSummaries; ++i) {
        est.note_summary(t0 + (kSpanUs * i) / kSummaries);
    }
    CHECK(est.received() == kSummaries - 1);  // the first one anchors

    const std::int64_t now = t0 + kSpanUs;
    const std::uint32_t lag = est.lag_us(now);
    // ~124 s, against the stopwatch's ~96-98 s: the estimator over-reads that
    // one paired point by ~26%, which is recorded in the header and is what
    // deliverable 1 exists to explain rather than something to tune away.
    CHECK(lag > 122'000'000);
    CHECK(lag < 126'000'000);
    CHECK(est.expected(now) == 420);  // 2.00/s x 210 s, the denominator
    CHECK(rendered(est, now).find("age 12") != std::string::npos);
}

TEST_CASE("a connect resets the estimate — the backlog dies with the socket") {
    StalenessEstimator est;
    est.note_connect(0);
    feed_fraction(est, 1'000'000, 200, 4);  // badly behind: 25% delivered

    const std::int64_t before = 1'000'000 + 196 * kPeriod;
    CHECK(est.lag_us(before) > 70'000'000);
    const std::uint32_t peak = est.worst_lag_ever_us();
    CHECK(peak > 70'000'000);

    // A fresh socket: Anvil sends a new snapshot and whatever was queued behind
    // the old one is gone with it. THIS IS WHY THE 86-MINUTE RUN OF 2026-08-09
    // LOOKED HEALTHY — 21 reconnects flushed the queue every four minutes.
    const std::int64_t reconnect = before + 1'000'000;
    est.note_connect(reconnect);
    CHECK_FALSE(est.anchored());
    CHECK(est.received() == 0);
    CHECK(est.lag_us(reconnect + 30'000'000) == 0);
    CHECK(est.worst_lag_us() == 0);
    CHECK(est.connections() == 2);
    // ...and the run-level peak survives it, which is the whole point of keeping
    // two high-water marks. A per-connection figure alone would have erased the
    // finding 21 times.
    CHECK(est.worst_lag_ever_us() == peak);
    CHECK(est.summaries_total() == 50);
}

TEST_CASE("receiving faster than the broadcast is reported, not counted as negative lag") {
    // The instrument checking its own premise. If summaries arrive at 4 Hz then
    // kSummaryPeriodUs is not what this server is doing and every figure above is
    // wrong by the same ratio — so it must be loud rather than clamped silently.
    StalenessEstimator est;
    est.note_connect(0);
    // Long enough to clear kAheadMinWindowUs, at double rate throughout.
    for (int i = 0; i < 400; ++i) {
        est.note_summary(1'000'000 + static_cast<std::int64_t>(i) * (kPeriod / 4));
    }
    const std::int64_t last = 1'000'000 + 399 * (kPeriod / 4);
    CHECK(est.lag_us(last) == 0);
    CHECK(est.worst_lag_us() == 0);
    CHECK(est.worst_ahead_us() > 45'000'000);
    // 4 Hz against an assumed 2 Hz is 1000 per mille — an order of magnitude
    // past the tolerance, which is the case this warning exists for.
    CHECK(est.worst_ahead_permille() > 900);
    CHECK(est.premise_suspect());
    CHECK(rendered(est, last).find("2 Hz premise suspect") != std::string::npos);
}

TEST_CASE("a HEALTHY board does not cry wolf — the measured wire rate must not fire the warning") {
    // Anvil's `summary` period, measured over half an hour on an unthrottled desk
    // socket: 3,600 broadcasts spanning 1,799.2 s = 499.92 ms, i.e. 2.0003/s.
    // Twenty minutes of exactly that must leave the premise warning silent.
    //
    // WHY THIS TEST EXISTS AT ALL, since the answer is now "the constant is
    // right": a 60-second measurement of the same socket appeared to read
    // 2.017/s — `n/span` where the span holds `n-1` intervals — and a whole
    // analysis of estimator drift was built on that 0.84% artefact, including a
    // predicted false alarm at 3.9 minutes into every good connection. The
    // artefact is gone; the test stays, because it pins the property that
    // matters (a correct wire never trips the alarm) against whatever the
    // constant and the venue turn out to be.
    StalenessEstimator est;
    est.note_connect(0);
    constexpr std::int64_t kMeasuredPeriodUs = 499'920;  // 2.0003/s, 2026-08-11
    constexpr int kN = 2400;                             // ~20 minutes
    for (int i = 0; i < kN; ++i) {
        est.note_summary(1'000'000 + static_cast<std::int64_t>(i) * kMeasuredPeriodUs);
    }
    const std::int64_t last = 1'000'000 + (kN - 1) * kMeasuredPeriodUs;

    // The credit is real, tiny, and still reported — only the ALARM is
    // suppressed, so the drift stays visible to anyone who looks for it.
    CHECK(est.worst_ahead_us() > 0);
    CHECK(est.worst_ahead_us() < 1'000'000);   // ~0.2 s over twenty minutes
    CHECK(est.worst_ahead_permille() <= 1);
    CHECK_FALSE(est.premise_suspect());
    CHECK(rendered(est, last).find("premise suspect") == std::string::npos);
}

TEST_CASE("the ratio test would still hold if the wire drifted where the old absolute one broke") {
    // The scale-free property, which is the whole reason the ratio was kept
    // after the 2.017/s figure evaporated. A 0.85% fast wire — the drift that
    // was wrongly believed real — banks well past the old absolute 2 s threshold
    // over twenty minutes and must STILL not fire, because 8.5 per mille is far
    // inside the 50 per mille tolerance.
    StalenessEstimator est;
    est.note_connect(0);
    constexpr std::int64_t kFastPeriodUs = 495'785;   // 2.017/s
    constexpr int kN = 2420;
    for (int i = 0; i < kN; ++i) {
        est.note_summary(1'000'000 + static_cast<std::int64_t>(i) * kFastPeriodUs);
    }
    CHECK(est.worst_ahead_us() > 9'000'000);          // >9 s: the old test fired here
    CHECK(est.worst_ahead_permille() <= 10);          // but the ratio is 0.85%
    CHECK_FALSE(est.premise_suspect());
}

TEST_CASE("a brief flush does not trip the premise warning") {
    // A backlog draining is a legitimate burst of summaries and must not be
    // reported as a broken premise, or the warning trains the reader to skip it.
    StalenessEstimator est;
    est.note_connect(0);
    feed_fraction(est, 1'000'000, 60, 2);                        // fall behind 15 s
    const std::int64_t after = 1'000'000 + 60 * kPeriod;
    for (int i = 0; i < 3; ++i) {                                // three back-to-back
        est.note_summary(after + static_cast<std::int64_t>(i) * 10'000);
    }
    CHECK(est.worst_ahead_us() == 0);   // still far behind overall
    CHECK_FALSE(est.premise_suspect());
    CHECK(rendered(est, after).find("premise suspect") == std::string::npos);
}

TEST_CASE("the peak backlog is banked at the DROP, and survives the reconnect that follows") {
    // The defect this covers: the high-water marks were only ever written inside
    // note_summary, so the peak reached between the last summary and the drop was
    // lost — and note_connect then zeroed `worst` on the way back up, destroying
    // the whole episode. The one field documented as "the headline number for a
    // whole run" did not survive the event it exists to capture.
    StalenessEstimator est;
    est.note_connect(0);
    feed_fraction(est, 1'000'000, 120, 2);      // 60 s of clock at half rate
    const std::int64_t last_summary = 1'000'000 + 118 * kPeriod;
    const std::uint32_t at_last = est.worst_lag_us();
    CHECK(at_last == 29'500'000);

    // Four more seconds pass with nothing arriving, then the socket drops. The
    // backlog at the drop is larger than at the last summary, and only
    // note_disconnect can see it.
    const std::int64_t drop = last_summary + 4'000'000;
    CHECK(est.lag_us(drop) == 33'500'000);
    est.note_disconnect(drop);
    CHECK(est.worst_lag_us() == 33'500'000);
    CHECK(est.worst_lag_ever_us() == 33'500'000);
    CHECK(est.disconnects() == 1);

    // The outage itself keeps inflating `age` — honest about the screen, since a
    // book nobody is updating really is getting older — but it is NOT backlog,
    // and it must not contaminate the high-water marks.
    const std::int64_t reconnect = drop + 20'000'000;
    CHECK(est.lag_us(reconnect) == 53'500'000);
    CHECK(est.worst_lag_us() == 33'500'000);    // unmoved by the grey

    est.note_connect(reconnect);
    CHECK(est.worst_lag_us() == 0);             // per-connection figure resets
    CHECK(est.worst_lag_ever_us() == 33'500'000);  // the run's headline survives
}

TEST_CASE("a clock that goes backwards is ignored rather than wrapped") {
    // esp_timer_get_time() is monotonic, so this is defence against a caller
    // mistake, not against the platform — but an unsigned subtraction in the
    // wrong order here would print 4,294 seconds of lag and read as a
    // catastrophic finding.
    StalenessEstimator est;
    est.note_connect(0);
    est.note_summary(10'000'000);
    est.note_summary(9'000'000);
    CHECK(est.received() == 0);
    CHECK(est.lag_us(9'000'000) == 0);
    CHECK(est.lag_us(10'000'000) == 0);
}

TEST_CASE("drain_percent is the ratio the age is derived from") {
    // 100 = keeping up. The bench read 41.
    CHECK(drain_percent(20, 10'000'000) == 100);
    CHECK(drain_percent(8, 10'000'000) == 40);
    CHECK(drain_percent(0, 10'000'000) == 0);
    // A window too short to contain a single broadcast slot has no ratio to
    // report, and reporting 0 would read as a dead feed.
    CHECK(drain_percent(0, 100'000) == 0);
    CHECK(drain_percent(1, 100'000) == 0);
    CHECK(drain_percent(0, 0) == 0);
    // Saturated rather than overflowing, for the flush case above.
    CHECK(drain_percent(4000, 10'000'000) == 1000);
}

TEST_CASE("PROPERTIES: the invariants hold over a swept space, not just at the worked examples") {
    // The cases above are examples. These are the four properties every reading
    // of this instrument depends on, checked across a sweep — and the first one
    // is not academic: `age <= window` is the hard ceiling the 2026-08-11 bench
    // log was read against, and it is what makes "the board cannot print an age
    // of 120 s at 2 minutes unless it received nothing" a checkable statement
    // rather than an intuition.
    for (int divisor = 1; divisor <= 8; ++divisor) {
        for (int slots = 2; slots <= 200; slots += 7) {
            StalenessEstimator est;
            est.note_connect(0);
            feed_fraction(est, 1'000'000, slots, divisor);
            if (!est.anchored()) { continue; }

            std::uint32_t prev_lag = 0;
            std::uint32_t prev_run = 0;
            for (int t = 0; t <= slots + 20; ++t) {
                const std::int64_t now = 1'000'000 + static_cast<std::int64_t>(t) * kPeriod;

                // 1. The book can never be older than the connection is.
                CHECK(est.lag_us(now) <= est.window_us(now));

                // 2. With no new summaries, age only grows — it is elapsed minus
                //    a quantity that is not changing.
                if (t > slots) {
                    CHECK(est.lag_us(now) >= prev_lag);
                }
                prev_lag = est.lag_us(now);

                // 3. The run-level high-water mark never goes down.
                CHECK(est.worst_lag_ever_us() >= prev_run);
                prev_run = est.worst_lag_ever_us();
            }

            // 4. The identity the whole estimate is: what was delivered plus
            //    what is owed equals the window, whenever the socket is behind.
            const std::int64_t now = 1'000'000 + static_cast<std::int64_t>(slots) * kPeriod;
            const std::uint64_t delivered =
                static_cast<std::uint64_t>(est.received()) * kSummaryPeriodUs;
            if (est.lag_us(now) > 0) {
                CHECK(delivered + est.lag_us(now) == est.window_us(now));
            }
        }
    }
}

TEST_CASE("PROPERTY: a connect always erases the per-connection state, whatever preceded it") {
    // Swept because the reset is the one operation that runs at an arbitrary
    // point in another connection's life, and the bench depends on it being
    // total: a residue would carry a dead socket's backlog into a live one and
    // read as a board that never recovers.
    for (int slots = 0; slots <= 120; slots += 11) {
        StalenessEstimator est;
        est.note_connect(0);
        feed_fraction(est, 1'000'000, slots, 3);
        const std::int64_t drop = 1'000'000 + static_cast<std::int64_t>(slots + 5) * kPeriod;
        est.note_disconnect(drop);
        const std::uint32_t banked = est.worst_lag_ever_us();

        est.note_connect(drop + 1'000'000);
        CHECK_FALSE(est.anchored());
        CHECK(est.received() == 0);
        CHECK(est.worst_lag_us() == 0);
        CHECK(est.worst_ahead_us() == 0);
        CHECK_FALSE(est.premise_suspect());
        // Nothing accrues before the first summary of the new connection,
        // however long the gap.
        CHECK(est.lag_us(drop + 600'000'000) == 0);
        CHECK(est.window_us(drop + 600'000'000) == 0);
        // ...and the run-level peak is the one thing that must survive.
        CHECK(est.worst_lag_ever_us() == banked);
    }
}

TEST_CASE("render truncates rather than overruns, and always terminates") {
    StalenessEstimator est;
    est.note_connect(0);
    feed_fraction(est, 1'000'000, 100, 2);
    const std::int64_t now = 1'000'000 + 100 * kPeriod;

    for (std::size_t cap = 1; cap < 96; ++cap) {
        char buf[128];
        std::memset(buf, '\xAB', sizeof buf);
        const std::size_t n = est.render(now, buf, cap);
        CHECK(n < cap);
        CHECK(std::strlen(buf) < cap);
        // Nothing past the declared capacity may be touched.
        for (std::size_t i = cap; i < sizeof buf; ++i) {
            CHECK(static_cast<unsigned char>(buf[i]) == 0xABu);
        }
    }
    CHECK(est.render(now, nullptr, 32) == 0);
}
