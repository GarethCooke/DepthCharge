// depthcharge/liveness_clock.hpp — the self-calibrating staleness threshold.
//
// M4 stage A ruling, 2026-08-17 (ARCHITECTURE.md §9). Staleness stops counting
// BOOK EVENTS and counts the venue's declared LIVENESS SIGNAL — Anvil's
// `summary`, Kraken's `heartbeat` — and the threshold stops being a declared
// per-venue constant.
//
// WHY IT CANNOT BE A CONSTANT. Both intervals are values DepthCharge does not
// control and cannot read back: Anvil's is `ANVIL_SUMMARY_HZ`, operator config
// on a server we do not own; Kraken's is a protocol constant. A hardcoded
// threshold is therefore coupled to a number that can change with no client
// change and no error — it would simply start crying wolf, or stop crying at
// all. So the threshold is derived at runtime from the signal itself: a rolling
// median of the observed inter-arrival, times `kThresholdMultiple`.
//
// ESP-IDF-free and allocation-free after construction, deliberately — and since
// M4 stage D (2026-08-20) that is load-bearing rather than aspirational: this
// header lives in `engine/` and is linked by BOTH the harness and the firmware,
// so the threshold the panel greys on and the threshold a golden is scored
// against are one implementation. Fixed-size ring, no heap, no floating-point
// state beyond the samples. Invariant #7 applies on the target.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <depthcharge/sample_window.hpp>

namespace depthcharge {

// ---------------------------------------------------------------------------
// THE ONE CONSTANT, AND WHAT IT IS MADE OF
// ---------------------------------------------------------------------------
//
// `k` multiplies the observed median, so it is dimensionless and transfers
// between venues — which is the point. It is derived from the worst HEALTHY
// inter-arrival expressed as a MULTIPLE of that venue's median, measured
// 2026-08-17 over every committed trace plus the two 2026-08-17 captures:
//
//   venue   signal      samples   median      worst healthy   worst/median
//   -----   ---------   -------   ---------   -------------   ------------
//   Anvil   summary       1,191   500.0 ms      968.8 ms         1.937x
//   Kraken  heartbeat       834   1000.3 ms   1,119.0 ms         1.119x
//
// Anvil's 1.937x is the number that sizes this: it is ONE MISSED TICK, on the
// M0 baseline, in otherwise healthy data. A threshold at k <= 2 would grey the
// panel every time a single `summary` publish slipped. Kraken's heartbeat is
// far tighter (1.119x over two hours at two different hours of day), so Anvil
// is the binding case and the constant is set by it.
//
// k = 4 therefore means "three consecutive liveness ticks missed", and clears
// the worst measured healthy multiple by 2.07x — the same order of margin
// `kRxWatchdogMs` was derived at (2.6x over Anvil's 391 ms worst healthy gap,
// ARCHITECTURE §9 2026-08-09), but taken against a MULTIPLE rather than an
// absolute, which is what lets one number serve both venues.
//
// Resulting thresholds, for orientation only — nothing reads these:
//   Anvil   4 x 500 ms  = 2,000 ms
//   Kraken  4 x 1000 ms = 4,000 ms
//
// The excluded sample, named so it is not read as a gap in the evidence:
// `anvil_101_reconnect`'s 4,747.7 ms summary gap (9.5x) is the capture's
// deliberate socket drop. It is the fault case the threshold exists to catch,
// not a healthy sample, and it is 2.4x above the k=4 threshold — so the golden
// still fires exactly once.
inline constexpr double kThresholdMultiple = 4.0;

// FLOOR AND CEILING, so a pathological median cannot produce a threshold that
// fires constantly or never fires.
//
// THE FLOOR IS LOAD-BEARING, and not for the reason it looks like. The obvious
// case is a fast venue — 20 Hz would yield a 200 ms threshold and ordinary
// scheduling jitter would grey the panel. The case that actually bites is the
// opposite one, and it is a failure this object has already met at Anvil:
//
//   **when a BACKLOGGED SOCKET DRAINS, the queued frames arrive as a burst with
//   near-zero inter-arrivals. The rolling median collapses toward zero, and an
//   uncapped k x median would grey a healthy feed at the exact moment it
//   recovered** — punishing the recovery instead of the outage, and doing it
//   just as the panel finally has fresh data to show.
//
// That is not hypothetical here: Anvil queues and never drops per socket (no
// cap, no drop policy, no coalescing), so a backlogged client receives every
// frame late rather than losing any, and the drain is a burst by construction.
// Measured: 111 s of accumulated lag over 150 s (ARCHITECTURE §9, 2026-08-11).
// The floor is what makes the threshold survive its own recovery.
//
// **RAISED 1,000 -> 2,000 ms AT M4 STAGE D, BECAUSE 1,000 WAS SIZED AGAINST THE
// WRONG QUANTITY.** The original number was `kRxWatchdogMs`, "the smallest
// threshold this project has ever run" — but that constant bounded BOOK-EVENT
// silence at Anvil, where the worst healthy gap is 391-640 ms. This one bounds
// LIVENESS-signal silence, and the table above measures that at **968.8 ms worst
// healthy at Anvil and 1,119.0 ms at Kraken**. A 1,000 ms floor is BELOW Kraken's
// worst healthy heartbeat interval, so a window still clamped from a drain burst
// would grey a perfectly healthy feed on an ordinary heartbeat — the exact false
// grey the floor exists to prevent, produced by the floor itself.
//
// 2,000 ms clears Anvil's worst healthy by 2.06x and Kraken's by 1.79x. It is
// also exactly `kThresholdMultiple x 500 ms`, i.e. the threshold a 2 Hz venue
// warrants anyway, so the clamped state is never tighter than the tightest
// cadence this project ships against. The exposure it leaves is bounded and
// stated: a burst-clamped window refills in `kWindowSamples` arrivals — 16 s at
// Anvil, 32 s at Kraken — and only during that does the 1.79x margin apply.
//
// **Do not tune it away as belt-and-braces** — `test_trace_venue.cpp` pins the
// drain-burst case and `test_liveness_watchdog.cpp` sweeps it at both venues'
// cadences, which is what would have caught the original number.
//
// The ceiling is the more important of the two, and it is what stops a SUSTAINED
// SLOWDOWN from disabling the detector: as the median grows the threshold grows,
// so a feed that decays gradually would keep buying itself more tolerance. The
// ceiling caps that. 30,000 ms is set against the only real fault this project
// has observed — Anvil's 176,000 ms silence of 2026-08-16 (A2b) — which it
// catches at 30 s rather than never, and against MINA/GBP's healthy 25,843 ms
// BOOK silence, which it does not touch because the heartbeat never stopped.
inline constexpr double kThresholdFloorMs = 2000.0;
inline constexpr double kThresholdCeilingMs = 30000.0;

// The generous default used until the window has `kMinSamples` intervals.
//
// It is the CEILING, deliberately: before calibration the object knows nothing
// about this venue's cadence, and the honest failure direction is to be slow to
// grey rather than to grey a healthy feed. Choosing it this way is also what
// keeps the ruling free of any new rendered state — there is no *calibrating*
// state to draw, no §4 change and no §5 change, because a threshold always
// exists and is always a number. Anvil delivers a snapshot and a summary
// immediately on connect and Kraken a status frame and a heartbeat within a
// second, so calibration starts at t=0 and the default is short-lived.
inline constexpr double kUncalibratedThresholdMs = kThresholdCeilingMs;

// Intervals needed before the median is trusted, and the ring's size.
//
// 8 is enough to have a median that is not one sample's opinion; 32 is the
// window. At Anvil's 2 Hz the window holds 16 s and calibrates in 4 s; at
// Kraken's 1 Hz, 32 s and 8 s.
inline constexpr std::size_t kMinSamples = 8;
inline constexpr std::size_t kWindowSamples = 32;

// ---------------------------------------------------------------------------
// THE THREE NUMBERS AS ONE OBJECT, BECAUSE TWO OF THEM ARE ABSOLUTES AND AN
// ABSOLUTE IN A VENUE-AGNOSTIC OBJECT IS A PER-VENUE QUANTITY (M5 stage C)
// ---------------------------------------------------------------------------
//
// The multiplier above is dimensionless and that is the whole reason it
// transfers. The floor and the ceiling are milliseconds, and milliseconds do
// not transfer: expressed as a MULTIPLE of each venue's own median, the same
// 30,000 ms ceiling is
//
//     Anvil     60x the median      never binds
//     Kraken    30x                 never binds
//     Binance    1.50x              BINDS ON A HEALTHY FEED
//
// A ceiling that sits at 1.5x the healthy median is not a ceiling. It IS the
// threshold, and the self-calibration the 2026-08-17 ruling rests on becomes a
// constant wearing a calibration's clothes — measured at M5 stage A and carried
// to here (ARCHITECTURE §9, 2026-08-25). This is the exact mirror of the class
// §9 recorded on 2026-08-26 for `kMinSamples`, `kBaselineSamples` and
// `kAgeWindowSamples`: **a sample count in a venue-agnostic object is a
// per-venue duration, and an absolute duration in one is a per-venue multiple.**
// Both directions of that rule are now instances rather than one.
//
// SO THE POLICY IS A PARAMETER, WITH THE DEFAULTS EQUAL TO WHAT SHIPPED. The
// default-constructed clock is byte-for-byte the object M4 stage D put on the
// board, so no venue that already has a shipping threshold has its number moved
// by the existence of this struct — which is what ARCHITECTURE §9's *changing
// the constant and the venue in one step leaves no way to attribute a
// regression* asks for, in the strongest available form: nothing to attribute.
// `firmware/` constructs `LivenessClock` with no argument and is untouched.
//
// The precedent for a per-venue number reaching `engine/` through a constructor
// is `Book`, which already takes `venue_traits(...).validated_depth` the same
// way; the harness's table is in `dc_harness/venue.hpp`.
struct LivenessPolicy {
    double multiple = kThresholdMultiple;
    double floor_ms = kThresholdFloorMs;
    double ceiling_ms = kThresholdCeilingMs;

    // The generous default before calibration is this venue's CEILING, for the
    // reason `kUncalibratedThresholdMs` gives above — the honest failure
    // direction is to be slow to grey rather than to grey a healthy feed. It is
    // a function rather than a fourth field so the two cannot drift apart.
    constexpr double uncalibrated_ms() const noexcept { return ceiling_ms; }
};

// A rolling median of the liveness signal's inter-arrival, and the threshold
// derived from it.
//
// HOW IT SURVIVES A SUSTAINED RATE CHANGE, WHICH IS THE CASE THAT MATTERS:
// the window is the last `kWindowSamples` intervals and nothing else, so a
// permanent halving of the broadcast rate refills the window at the new rate
// and the median follows it within one window — while a single late frame moves
// a rank, not the median. That distinction is the whole reason this is a median
// over a bounded window rather than a mean or an all-time statistic. It matters
// at Anvil specifically because upstream coalescing is GLOBAL: if the
// broadcaster falls behind the engine, intermediate rosters are skipped for
// every socket at once, so a lagging server presents as a LOWER RATE rather
// than as a gap — a mean would drag, and an unbounded statistic would never
// catch up at all.
class LivenessClock {
public:
    // The shipping object, unchanged: the defaults ARE the constants above.
    LivenessClock() noexcept = default;

    // ...and the same object told what this venue warrants. `explicit` so a
    // policy can never be passed where a clock was meant.
    explicit LivenessClock(LivenessPolicy policy) noexcept
        : policy_(policy), threshold_ms_(policy.uncalibrated_ms()) {}

    const LivenessPolicy& policy() const noexcept { return policy_; }

    // =====================================================================
    // WHAT A GREEN CLOCK IS ENTITLED TO MEAN, AND IT IS NOT THE SAME THING AT
    // EVERY VENUE (M5 stage C, deliverable 4)
    // =====================================================================
    //
    // **THIS OBJECT KNOWS ONLY THAT SOMETHING ARRIVED. IT CANNOT KNOW WHAT
    // SUBSYSTEM SENT IT, AND AT ONE OF THE THREE VENUES THAT DISTINCTION IS THE
    // WHOLE ANSWER.** Stated here rather than only in ARCHITECTURE §9 and a
    // strain card, because the next reader of a green clock is reading this
    // file, and a limit recorded two documents away is a limit nobody meets in
    // time.
    //
    //   Anvil    `summary`   emitted by the application that publishes the book
    //   Kraken   `heartbeat` emitted by the application that publishes the book
    //   Binance  `ping`      emitted by the WEBSOCKET LAYER, BELOW the
    //                        subscription
    //
    // At the first two, *the feed is alive* and *the socket is alive* coincide,
    // and the 2026-08-17 ruling never had to distinguish them. At Binance they
    // do not coincide, so a calibrated, un-fired, entirely green clock proves
    // THE SOCKET IS UP and proves nothing whatever about the subscription. B1
    // measured it on the wire: a misspelled stream returns HTTP 101, answers its
    // pings in 0.107 ms, and delivers no depth frame ever.
    //
    // **THERE IS NO FIX AND THIS STAGE DID NOT INVENT ONE.** The venue publishes
    // no subscription-state signal on the market-data streams at all, so no care
    // with stream names closes it — a server-side subscription drop on a socket
    // that stays up presents identically to health. What M5 stage C did was
    // bound the CONNECT case, where evidence is available: the Binance adapter
    // will not publish its seed until a diff has bracketed it (DESIGN strain 26,
    // remedy (a)), so a subscription that never starts is grey rather than
    // permanently frozen. **Mid-session, the hole is open**, and invariant #5's
    // line is drawn at *bounded* rather than at *closed*.
    //
    // The same emission point costs `age_ms` its other half: a deficit measured
    // against a signal that cannot itself fall behind reads zero. B2 narrowed
    // that rather than confirming it — a SOCKET backlog delays the ping too,
    // because it is a control frame on the same TCP stream, and the meter tracks
    // it at 1.00x; a FEED backlog is invisible, 0.3 s read through 1,797 s
    // injected. See age_estimator.hpp.
    //
    // ARCHITECTURE §9, 2026-08-25 (M5 stage B1, both rows) and 2026-08-26.

    // Record one liveness-signal arrival. Pass the arrival stamp in
    // nanoseconds; the first call establishes the origin and yields no interval.
    void on_liveness(std::int64_t rx_ns) noexcept {
        if (have_prev_) {
            intervals_.push(static_cast<double>(rx_ns - prev_ns_) / 1e6);
            threshold_ms_ = compute_threshold_ms();
        }
        prev_ns_ = rx_ns;
        have_prev_ = true;
    }

    std::size_t samples() const noexcept { return intervals_.size(); }
    bool calibrated() const noexcept { return intervals_.size() >= kMinSamples; }

    // The observed median inter-arrival, or 0 before any sample. Lower median by
    // nearest rank — the convention, and the reason for it, live with the ring
    // in sample_window.hpp, because the age meter takes a median of the same
    // signal and the two must not answer differently.
    double median_ms() const noexcept {
        std::array<double, kWindowSamples> scratch{};
        for (std::size_t i = 0; i < intervals_.size(); ++i) { scratch[i] = intervals_.at(i); }
        return lower_median(scratch.data(), intervals_.size());
    }

    // The staleness threshold this venue currently warrants.
    //
    // O(1), AND THE CACHE IS WHAT MAKES THE TARGET AFFORD IT. The value is a
    // pure function of `intervals_`, and `intervals_` changes in exactly one
    // place — `on_liveness` — so recomputing it there and returning the stored
    // number here is exactly equal to computing it on demand, for every possible
    // history. That equality is not an argument, it is `liveness clock: the
    // cached threshold equals the recomputed one at every step` in
    // test_liveness_watchdog.cpp, swept over a randomised walk.
    //
    // The reason it matters is the firmware. `median_ms()` copies the ring onto
    // the stack and `std::sort`s 32 doubles; the feed task recomputes its queue
    // deadline on EVERY wake, which is per received message (~13/s at Anvil,
    // more in a burst) and again on every watchdog expiry. On a part with no
    // double-precision FPU that is a soft-float sort in the feed loop, for a
    // number that can only have changed on a liveness arrival (~2/s at Anvil,
    // 1/s at Kraken). The host driver queries it just as often and the saving is
    // real there too; it is simply not the reason.
    double threshold_ms() const noexcept { return threshold_ms_; }

private:
    // The uncached form, called only from `on_liveness`. Kept as its own
    // function so the test above has something to compare the cache against —
    // a cache checked against itself checks nothing.
    double compute_threshold_ms() const noexcept {
        if (!calibrated()) { return policy_.uncalibrated_ms(); }
        const double t = policy_.multiple * median_ms();
        return t < policy_.floor_ms   ? policy_.floor_ms
             : t > policy_.ceiling_ms ? policy_.ceiling_ms
                                      : t;
    }

    LivenessPolicy policy_{};
    SampleRing<double, kWindowSamples> intervals_;
    std::int64_t prev_ns_ = 0;
    bool have_prev_ = false;
    // Starts at the uncalibrated default, which is what `compute_threshold_ms()`
    // returns for an empty window — so the cache is correct before the first
    // arrival as well as after it.
    double threshold_ms_ = kUncalibratedThresholdMs;
};

}  // namespace depthcharge
