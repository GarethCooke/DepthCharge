// firmware/src/staleness.hpp — how OLD is the book on the panel?
//
// THE QUESTION NOTHING IN M3 EVER ASKED.
//
// Every instrument in this firmware before this one measures whether the feed is
// STOPPED (the RX watchdog, gap_histogram.hpp) or whether it is WRONG (the parse
// counters, the reject log) or WHOSE FAULT the stopping is (stall_probe.hpp).
// None of them can see a feed that is running perfectly, parsing perfectly, and
// showing a book that is a hundred seconds behind the market.
//
// That is not hypothetical. On 2026-08-11 a bench run with `parse=0 cont=0
// trunc=0 oversize=0 qfull=0 abandoned=0` and a single connection put a state on
// the panel **96-98 seconds** after the web client already had it, and every
// counter on the board read healthy throughout. Invariant #5 protects against
// stopped and against wrong; it has no concept of OLD, and neither did the
// stats block.
//
// THE MEASUREMENT IS FREE, AND THAT IS THE WHOLE TRICK.
//
// Anvil's `summary` is a fixed **2 Hz timer broadcast** — it is emitted by the
// clock, not by the market, so its rate is not a fact about trading, it is a
// fact about elapsed time. Every other frame kind is event-driven and therefore
// carries no clock at all: `book` at 5.60/s against 13.44/s tells you a ratio and
// nothing about seconds.
//
// So: over a window of wall-clock T, the server broadcast T / 500 ms summaries.
// We received N. If the missing ones are QUEUED rather than dropped, then the
// point in the stream we have reached is N x 500 ms after the point we started
// from, while the clock has moved T — and the difference is the age of the book
// in front of you:
//
//     lag = elapsed_since_anchor - received x 500 ms
//
// No desk capture, no server change, no new protocol field, one counter the
// adapter was already keeping (`AnvilAdapter::Stats::summary_ignored`) and one
// clock the feed task was already reading.
//
// ============================================================================
// THE ASSUMPTION, STATED PLAINLY BECAUSE THE NUMBER IS WORTHLESS WITHOUT IT
// ============================================================================
//
// **This measures lag only if the missing summaries are QUEUED. If Anvil DROPS
// them, the same deficit appears and there is no lag at all.** Rate and deficit
// cannot tell queuing from shedding — that is precisely the hole in the
// 2026-08-09 `anvil_drain_probe.py` finding this instrument exists beside, and
// repeating the mistake one layer up would be worse than not measuring.
//
// What settles it is freshness measured against a reference clock, which this
// firmware cannot do alone. `tools/anvil_freshness_probe.py` does it from the
// desk — two sockets, one throttled, the same wire `seq` matched across both —
// and **it has now been run, and the assumption holds on this venue**:
//
//     2026-08-11 desk, 150 s, subject sleeping 250 ms per message
//       book     3.48/s of 13.66/s = 25.5%      lag rose LINEARLY to 111 s
//       summary  0.51/s of  2.01/s = 25.7%      no plateau anywhere in the run
//       measured +0.745 s of lag per second; the drain fraction independently
//       predicts +0.745 s/s. Implied server-side queue after 150 s: ~12.4 MB.
//
// Every kind is thinned by the SAME fraction, which is a delayed byte stream and
// not per-kind coalescing; and the lag is linear with no ceiling to 111 s. So on
// this venue, today, the deficit IS an age. That is a property of Anvil rather
// than of arithmetic, so the probe is the thing to re-run if the venue changes
// — and M4/M5's venues get no such assumption for free.
//
// ============================================================================
// CALIBRATION: THE STOPWATCH DOES NOT MEASURE THIS QUANTITY. READ THIS FIRST.
// ============================================================================
//
// The obvious bench check — freeze the web client, time how long until the panel
// shows the same state — measures the CATCH-UP INTERVAL, not the age. They
// differ by a factor of 1/f, and at the board's f that is nearly two and a half.
//
// The kinematics, which are the same two lines the estimator is built on. Under
// queuing the panel's market-time cursor advances at the drain fraction f, so
// with the socket opened at t0:
//
//     displayed market time   M(t) = t0 + f (t - t0)
//     age                     L(t) = t - M(t) = (1 - f)(t - t0)      <- THIS
//
// Freeze a fresh reference at t_f and wait for the panel to reach it:
//
//     M(W) = t_f   =>   W - t_f = (t_f - t0)(1 - f)/f = L(t_f) / f   <- NOT THIS
//
// **Stopwatch = age / f.** Both relations are confirmed on this project's own
// committed desk probe (`hardware/bench-2026-08-11-feed-lag.md`, f = 0.2551):
// L(t)/t reads 0.743-0.748 across all eight bins against a predicted 1 - f =
// 0.745, and L/(f t) reads 2.91-2.92 against a predicted (1-f)/f = 2.9197.
//
// SO: A STOPWATCH OF S SECONDS CORROBORATES A PRINTED `age` OF S x f, NOT OF S.
// At the board's measured f ~ 0.41 the stopwatch should read about **2.4x**
// whatever this estimator prints, and that is agreement, not disagreement.
// Calibrating this constant against a raw stopwatch reading would detune a
// correct instrument by that factor. The bench rule, which the stats line
// carries all the terms for:  expected stopwatch ~ age x 100 / drain%.
//
// WHICH RETIRES THE ONLY PAIRED POINT THIS HEADER USED TO QUOTE. An earlier
// draft recorded "the deficit method gives ~124 s, the owner's stopwatch read
// ~96-98 s, so the estimator over-reads by ~26%". That comparison is void: the
// two numbers are different quantities. Under the same queuing model that
// justifies this file, a true age of 124 s would have produced a stopwatch of
// 124/0.41 = ~300 s, so the 98 s reading implies an age nearer **40 s** — which
// is what a socket only ~67 s old produces, and that run did carry `connects=2`.
// No over-read is demonstrated in either direction. The claim is withdrawn
// rather than restated with a different number.
//
// WHAT ACTUALLY CALIBRATES IT, and it needs no stopwatch and no human: the board
// prints its last wire `seq` on the age line, and Anvil's `seq` is a single
// GLOBAL counter, so the same broadcast carries the same value on every socket.
// Join the serial log against a simultaneous desk capture on that key and the
// lag is a subtraction, sampled at the publish rate for the whole run —
// `tools/anvil_freshness_probe.py` already performs exactly that subtraction
// across two sockets. That is deliverable 1's real instrument; the stopwatch was
// a proxy for it and a biased one.
//
// Two smaller biases in the same direction, both worth knowing at the bench:
//
//   * A `summary` frame that FAILS TO PARSE never reaches this estimator, so it
//     counts as missing and inflates the lag. The 2026-08-11 46-minute run
//     rejected summary fragments at a chunk boundary. Cross-check against
//     `-- errors parse=`: at 21 rejects in 15,688 frames the effect is noise, at
//     hundreds it is not.
//   * The reading has a **one-period sawtooth**. Between two summaries the
//     elapsed term grows while the received term does not, so a socket that is
//     perfectly current oscillates between 0 and 500 ms. Resolution is one
//     summary period; anything under a second is zero.
//
// ============================================================================
//
// NO ESP-IDF, DELIBERATELY — the fifth instrument written to that rule, after
// frame_reassembler.hpp, gap_histogram.hpp, stall_probe.hpp and
// ws_supervisor.hpp. The failure mode of an estimator is not a crash, it is a
// confident wrong number in the one line a bench session is reading for a
// verdict, and `harness/tests/test_staleness.cpp` is what stops that being
// discovered on the board. Every clock value is a parameter for the same reason.
//
// RESET ON CONNECT. The backlog dies with the socket — Anvil sends a fresh
// snapshot to a new connection — so the estimate must die with it too. This is
// also why the 86-minute run of 2026-08-09 looked healthy: 21 reconnects flushed
// the queue every four minutes and no instrument was watching the interval.
//
// SINGLE WRITER, LIKE EVERY OTHER COUNTER HERE. Written by the Core 0 feed task,
// read by the render task on Core 1 with no synchronisation.
//
// **AND THE TORN-READ CLAIM HERE IS NARROWER THAN THE ONE THE OTHER INSTRUMENTS
// MAKE — do not copy their sentence into this file.** `gap_histogram.hpp` and
// `stall_probe.hpp` can say "stale, never torn" because every field they expose
// across cores is 32-bit and naturally aligned. This object has four 64-bit
// members — `anchor_us_`, `connect_us_`, `last_summary_us_`, `summaries_total_`
// — and a 64-bit store on the 32-bit LX7 is two word stores, so a reader CAN
// observe a half-updated value. An earlier draft of this comment claimed
// otherwise and was wrong.
//
// What saves it, and for how long: `esp_timer_get_time()` stays below 2^32 us
// for the first **71.6 minutes** of uptime, so the high word of every timestamp
// here is zero on both sides of any store and a tear is unobservable. Past that
// — and a desk object is meant to run for weeks — a `note_connect` racing a
// `render()` can produce one absurd `age` on one log line. That is the same cost
// every counter in this firmware already accepts, so it is not a defect; it is a
// bound, and the bound needs stating because the 46-minute soak and the
// 65-minute lag run both sat just inside it and proved nothing about the case
// beyond it. `summaries_total_` needs 2^32 summaries (68 years) to reach its own
// high word and is unconditionally safe.
//
// **Nothing may branch on this.** In particular it does NOT raise a Gap: a
// staleness `Gap` would be a §4 change to the `FeedEvent` vocabulary, which is
// stop-and-raise and not a session's call (M3 stage E deliverable 3 writes up
// the proposal; the owner decides).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "gap_histogram.hpp"

namespace depthcharge::fw {

// Anvil's `summary` broadcast period.
//
// 2.00/s is a *timer*, not an observation of trading: `docs/vendor/
// anvil-protocol.md` describes summary as the cross-ticker roster broadcast, and
// M3's 20-minute desk capture held it at 2.00/s while the book stream moved
// between 5 and 13/s. Re-measured on an unthrottled desk socket on 2026-08-11:
// **3,600 summaries over 1,799.2 s = 2.0003/s, a 499.9 ms period** — exact to
// 0.02% over half an hour. Worth measuring rather than assuming, because this
// denominator scales every age the board prints and nothing on the board can
// check it; and worth measuring for MINUTES rather than seconds, because a
// 60-second run of the same socket appeared to read 2.017/s (see
// kAheadTolerancePermille). It is the only fixed clock on this wire, which is
// the only reason the arithmetic above works.
//
// If a future Anvil changes this period, every figure here scales by the ratio
// and nothing says so — so the denominator is printed on the stats line beside
// the numerator, and `tools/anvil_freshness_probe.py --reference-only` measures
// it from the desk in one minute.
inline constexpr std::uint32_t kSummaryPeriodUs = 500000;

// When to disbelieve the constant above, and why it is a RATIO rather than an
// absolute amount.
//
// A socket receiving summaries faster than 2 Hz for long means this period is
// not what the server is doing, and every age below is scaled by the error.
//
// The first version of this test fired on an absolute 2 s of accumulated
// "ahead". It was replaced on the strength of a measurement that said Anvil
// broadcasts at 2.017/s — a 496 ms period, which would have a healthy board bank
// 8.5 ms of false credit a second and cry wolf at 3.9 minutes into every good
// connection. **That measurement was wrong, and it was ours.** A 30-minute
// unthrottled desk socket reads **3,600 summaries over 1,799.2 s = 2.0003/s**,
// a period of 499.9 ms: the constant above is exact to 0.02%. The 2.017 was
// `n/span` where span holds n-1 intervals, on a 60-sample run — a +0.84% bias
// that vanishes by n=3,600. `tools/anvil_freshness_probe.py::rate` now carries
// the correction and the reason.
//
// **The ratio test is kept anyway, and not out of embarrassment.** It is
// scale-free, so it survives this constant being wrong by any amount, a venue
// that changes its period, and the board's own crystal error — none of which an
// absolute threshold survives. What changed is the justification: the drift it
// tolerates is 0.015%, not 0.85%, so 50 per mille is now ~3,000x headroom rather
// than 6x, and the test fires only on something structural. A 4 Hz broadcast
// reads 1000 per mille and trips on the second sample. The minimum window keeps
// the ratio out of the noise at the start of a connection, where two summaries
// and a jittery anchor can produce any number.
inline constexpr std::uint32_t kAheadTolerancePermille = 50;
inline constexpr std::uint32_t kAheadMinWindowUs = 30000000;

// Percentage of the summary broadcast actually delivered over a window.
//
// A free function rather than a method because the console owns the window: it
// already differences monotone counters across its own 10 s statistics period
// for every other rate it prints (`RenderTask::print_rates`), and a second
// windowing mechanism inside this object would be a second answer to "what is
// the current rate" in a firmware whose whole diagnostic method is that two
// instruments measuring the same thing must agree.
//
// 100 means keeping up. The 2026-08-11 bench read 41.
inline std::uint32_t drain_percent(std::uint32_t summaries, std::uint64_t window_us) noexcept {
    if (window_us == 0) { return 0; }
    const std::uint64_t expected = window_us / kSummaryPeriodUs;
    if (expected == 0) { return 0; }
    const std::uint64_t pct = (static_cast<std::uint64_t>(summaries) * 100ull) / expected;
    return (pct > 1000ull) ? 1000u : static_cast<std::uint32_t>(pct);
}

// Microseconds as "S.t" seconds, as one printf ARGUMENT rather than two.
//
// This exists because the two-argument form — `%u.%01u` fed from
// `us / 1000000u` and `(us / 100000u) % 10u` — appeared FIVE times in one
// render function, and the failure mode of that pattern is not a crash. It is a
// label printed beside the neighbouring field's number, which reads perfectly
// and is wrong, in the one line a bench evening is being read for a verdict.
// The same argument the histogram renderer is written from, applied to the same
// hazard one level down. With `%s` there is one argument per value and the
// mispairing is not expressible.
// 64-BIT, AND THE 2026-08-15 SOAK IS WHY. The first version took uint32
// microseconds, which is 71.6 minutes, and every `age`, `worst`, `run` and
// `over` field on the age line inherited that ceiling: a day-long soak printed
// `4294.9 s` for eleven straight hours while the true age climbed past it, and
// three of four connection segments hit the wall. `%llu` here and 64-bit
// arithmetic in every accessor below cost nothing on the LX7 and buy the only
// thing an age instrument owes — that a big number is a big number.
struct SecondsText {
    char buf[24]{};
    explicit SecondsText(std::uint64_t us) noexcept {
        std::snprintf(buf, sizeof buf, "%llu.%01u",
                      static_cast<unsigned long long>(us / 1000000ull),
                      static_cast<unsigned>((us / 100000ull) % 10ull));
    }
};

class StalenessEstimator {
public:
    // A socket came up. Everything about the previous connection's backlog is
    // gone with it, including this estimate.
    //
    // Takes the clock rather than reading one, and does NOT anchor here: the
    // anchor is the first summary that arrives (see note_summary), because a
    // connect lands at a uniformly random phase within the 500 ms broadcast
    // period and anchoring on it would bake in up to a full period of bias —
    // a mean of 250 ms of lag reported on a socket that has none.
    void note_connect(std::int64_t at_us) noexcept {
        connect_us_ = at_us;
        anchor_us_ = 0;
        received_ = 0;
        last_summary_us_ = 0;
        worst_lag_us_ = 0;
        worst_ahead_us_ = 0;
        worst_ahead_permille_ = 0;
        ++connections_;
    }

    // One `summary` frame parsed. Called from the feed task by differencing
    // `AnvilAdapter::Stats::summary_ignored` across the adapter call — the
    // adapter is engine code shared with the host replay and is deliberately not
    // modified to carry a firmware instrument (ARCHITECTURE §6 #1).
    void note_summary(std::int64_t at_us) noexcept {
        ++summaries_total_;
        if (anchor_us_ == 0) {
            // The first summary of this connection starts the clock and is not
            // itself a delivery: the deficit is measured BETWEEN summaries, so N
            // summaries after the anchor represent N x period of stream time.
            anchor_us_ = at_us;
            last_summary_us_ = at_us;
            return;
        }
        if (at_us < anchor_us_) { return; }  // clock went backwards; ignore
        ++received_;
        last_summary_us_ = at_us;

        const std::uint64_t elapsed = static_cast<std::uint64_t>(at_us - anchor_us_);
        const std::uint64_t delivered = delivered_us();
        if (elapsed >= delivered) {
            bank_peak(elapsed - delivered);
        } else {
            // DELIVERED MORE STREAM TIME THAN THE CLOCK ALLOWS — the instrument
            // checking its own premise. A socket cannot receive summaries faster
            // than they are broadcast for long, so a large or growing value here
            // means kSummaryPeriodUs is not what this server is doing, and every
            // lag figure is wrong by the same ratio. A backlog FLUSHING produces
            // it too, briefly and legitimately, which is why it is reported as a
            // high-water mark rather than treated as an error.
            const std::uint32_t ahead = clamp_us_to_u32(delivered - elapsed);
            if (ahead > worst_ahead_us_) { worst_ahead_us_ = ahead; }
            // And the ratio, which is the figure the warning actually tests —
            // see kAheadTolerancePermille. Taken against the window this sample
            // sits in rather than against a later one, so a genuine premise
            // error does not get diluted by simply waiting.
            if (elapsed >= kAheadMinWindowUs) {
                const std::uint32_t permille =
                    static_cast<std::uint32_t>(((delivered - elapsed) * 1000ull) / elapsed);
                if (permille > worst_ahead_permille_) { worst_ahead_permille_ = permille; }
            }
        }
    }

    // The socket went down.
    //
    // WHY THIS EXISTS, and it is a defect this file shipped with for one
    // afternoon. The high-water marks were only ever written inside
    // note_summary, so two things were lost: any peak reached between the last
    // summary and the drop, and — worse — the whole episode, because
    // note_connect then zeroed `worst` on the way back up. The one field
    // documented as "the headline number for a whole run" did not survive the
    // event it exists to capture.
    //
    // Folding the peak in HERE rather than at the reconnect is deliberate.
    // `lag_us` keeps climbing while the socket is down, which is honest about
    // the SCREEN (a book nobody is updating really is getting older) but is not
    // backlog — the queue died with the socket. Sampling at the drop keeps
    // `worst`/`run` meaning "the largest backlog this feed accumulated" and
    // leaves the outage visible in `age` alone, where it belongs.
    void note_disconnect(std::int64_t at_us) noexcept {
        bank_peak(lag_us(at_us));
        ++disconnects_;
    }

    // The accumulated age of the book, right now.
    //
    // `now_us` is included rather than measured to the last summary, and that is
    // the honest reading: if nothing has arrived for ten seconds the book on the
    // panel is ten seconds older than the last summary said, whatever the reason.
    // It is also what makes the number a sawtooth on a healthy socket — see the
    // header.
    std::uint64_t lag_us(std::int64_t now_us) const noexcept {
        if (anchor_us_ == 0 || now_us <= anchor_us_) { return 0; }
        const std::uint64_t elapsed = static_cast<std::uint64_t>(now_us - anchor_us_);
        const std::uint64_t delivered = delivered_us();
        return (elapsed >= delivered) ? (elapsed - delivered) : 0ull;
    }

    // How many summaries the broadcast should have produced since the anchor —
    // the denominator behind every figure above, printed so a reader never has
    // to trust a derived number without it.
    std::uint32_t expected(std::int64_t now_us) const noexcept {
        if (anchor_us_ == 0 || now_us <= anchor_us_) { return 0; }
        return static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(now_us - anchor_us_) / kSummaryPeriodUs);
    }

    // True once a summary has been seen on this connection. Until then there is
    // no estimate at all, which the console must say rather than print 0.0 s —
    // "no reading yet" and "the book is current" are different statements and
    // exactly one of them is reassuring.
    bool anchored() const noexcept { return anchor_us_ != 0; }

    // How long this estimate has been accumulating. A 2 s lag over 4 s of
    // connection and a 2 s lag over four minutes are different diagnoses.
    std::uint64_t window_us(std::int64_t now_us) const noexcept {
        if (anchor_us_ == 0 || now_us <= anchor_us_) { return 0; }
        return static_cast<std::uint64_t>(now_us - anchor_us_);
    }

    std::uint32_t received() const noexcept { return received_; }
    std::uint64_t summaries_total() const noexcept { return summaries_total_; }
    std::uint32_t connections() const noexcept { return connections_; }
    std::uint64_t worst_lag_us() const noexcept { return worst_lag_us_; }
    // Retained across connects: the headline number for a whole run, which the
    // per-connection figure destroys every time the socket blinks. That erasure
    // is exactly how the 86-minute run of 2026-08-09 looked healthy.
    std::uint64_t worst_lag_ever_us() const noexcept { return worst_lag_ever_us_; }
    std::uint32_t worst_ahead_us() const noexcept { return worst_ahead_us_; }
    std::uint32_t worst_ahead_permille() const noexcept { return worst_ahead_permille_; }
    std::uint32_t disconnects() const noexcept { return disconnects_; }
    std::int64_t last_summary_us() const noexcept { return last_summary_us_; }
    std::int64_t connect_us() const noexcept { return connect_us_; }

    // "age 97.4 s (worst 124.0 s, run 124.0 s) | summary 172 of 420 over 210 s"
    //
    // Rendered here rather than in the console for the same reason the histogram
    // renders itself: the half of the printing that can be wrong in a way nobody
    // notices is the pairing of a label with the wrong number, and this is the
    // line a bench evening will be read off. Tenths of a second throughout —
    // integer arithmetic only (invariant #3's habit; a float here would drag in
    // soft-float for a log line).
    std::size_t render(std::int64_t now_us, char* out, std::size_t cap) const noexcept {
        if (out == nullptr || cap == 0) { return 0; }
        out[0] = '\0';
        std::size_t at = 0;
        if (!anchored()) {
            const int n = std::snprintf(out, cap, "no reading yet — no summary since connect");
            (void)append_truncating(n, cap, at);
            return at;
        }
        const SecondsText lag{lag_us(now_us)};
        const SecondsText worst{worst_lag_us_};
        const SecondsText run{worst_lag_ever_us_};
        const SecondsText win{window_us(now_us)};
        int n = std::snprintf(out, cap, "age %s s (worst %s s, run %s s)",
                              lag.buf, worst.buf, run.buf);
        if (append_truncating(n, cap, at)) { return at; }
        n = std::snprintf(out + at, cap - at, " | summary %u of %u over %s s",
                          static_cast<unsigned>(received_),
                          static_cast<unsigned>(expected(now_us)), win.buf);
        if (append_truncating(n, cap, at)) { return at; }
        if (premise_suspect()) {
            // Both tests must pass: big enough to matter in seconds, and out of
            // proportion to the window it accrued over. Either alone misfires —
            // see kAheadTolerancePermille for which way, and why the absolute
            // one was believed to misfire far more often than it does.
            const SecondsText ahead{worst_ahead_us_};
            n = std::snprintf(out + at, cap - at, " | AHEAD %s s (%u%%) — 2 Hz premise suspect",
                              ahead.buf, static_cast<unsigned>(worst_ahead_permille_ / 10u));
            (void)append_truncating(n, cap, at);
        }
        return at;
    }

    // Exposed so the test can pin the rule rather than grep the rendered line.
    bool premise_suspect() const noexcept {
        return worst_ahead_us_ >= kSummaryPeriodUs * 4u &&
               worst_ahead_permille_ >= kAheadTolerancePermille;
    }

private:
    // Stream time this connection has actually been given, in microseconds.
    //
    // Shared because it was written twice — once in note_summary and once in
    // lag_us — and the two copies had already drifted: one compared `elapsed >=
    // delivered` and the other `elapsed > delivered`. Both answer 0 at equality
    // so nothing was wrong yet, which is exactly how near-duplicates survive
    // long enough to matter.
    std::uint64_t delivered_us() const noexcept {
        return static_cast<std::uint64_t>(received_) * kSummaryPeriodUs;
    }

    // One place where a lag becomes a high-water mark, for the same reason: the
    // pair was written out twice, and a future third caller that updated only
    // one of them would produce a `worst` that is quietly smaller than a `run`
    // it is supposed to bound.
    void bank_peak(std::uint64_t lag) noexcept {
        if (lag > worst_lag_us_) { worst_lag_us_ = lag; }
        if (lag > worst_lag_ever_us_) { worst_lag_ever_us_ = lag; }
    }

    // The first summary of this connection. Zero means "not anchored", which is
    // safe because esp_timer_get_time() is microseconds since boot and the first
    // summary cannot land at boot instant zero.
    std::int64_t anchor_us_ = 0;
    std::int64_t connect_us_ = 0;
    std::int64_t last_summary_us_ = 0;

    // Summaries since the anchor, NOT including it.
    std::uint32_t received_ = 0;
    std::uint64_t summaries_total_ = 0;
    std::uint32_t connections_ = 0;

    std::uint64_t worst_lag_us_ = 0;
    std::uint64_t worst_lag_ever_us_ = 0;
    std::uint32_t worst_ahead_us_ = 0;
    std::uint32_t worst_ahead_permille_ = 0;
    std::uint32_t disconnects_ = 0;
};

}  // namespace depthcharge::fw
