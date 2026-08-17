// dc_harness/liveness_clock.hpp — the self-calibrating staleness threshold.
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
// ESP-IDF-free and allocation-free after construction, deliberately: this is the
// half of the rule stage B lifts into `firmware/`, and invariant #7 applies
// there. Fixed-size ring, no heap, no floating-point state beyond the samples.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace dc::harness {

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
// 1,000 ms is the smallest threshold this project has ever run (`kRxWatchdogMs`),
// which makes it the smallest one with evidence behind it. **Do not tune it away
// as belt-and-braces** — `test_trace_venue.cpp` pins the drain-burst case.
//
// The ceiling is the more important of the two, and it is what stops a SUSTAINED
// SLOWDOWN from disabling the detector: as the median grows the threshold grows,
// so a feed that decays gradually would keep buying itself more tolerance. The
// ceiling caps that. 30,000 ms is set against the only real fault this project
// has observed — Anvil's 176,000 ms silence of 2026-08-16 (A2b) — which it
// catches at 30 s rather than never, and against MINA/GBP's healthy 25,843 ms
// BOOK silence, which it does not touch because the heartbeat never stopped.
inline constexpr double kThresholdFloorMs = 1000.0;
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
    // Record one liveness-signal arrival. Pass the arrival stamp in
    // nanoseconds; the first call establishes the origin and yields no interval.
    void on_liveness(std::int64_t rx_ns) noexcept {
        if (have_prev_) {
            const double gap_ms = static_cast<double>(rx_ns - prev_ns_) / 1e6;
            samples_[next_] = gap_ms;
            next_ = (next_ + 1) % kWindowSamples;
            if (count_ < kWindowSamples) { ++count_; }
        }
        prev_ns_ = rx_ns;
        have_prev_ = true;
    }

    std::size_t samples() const noexcept { return count_; }
    bool calibrated() const noexcept { return count_ >= kMinSamples; }

    // The observed median inter-arrival, or 0 before any sample.
    double median_ms() const noexcept {
        if (count_ == 0) { return 0.0; }
        std::array<double, kWindowSamples> sorted{};
        std::copy(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(count_),
                  sorted.begin());
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count_));
        // Lower median on an even count, matching gap_stats.py's nearest-rank
        // convention — an interpolated value would invent an interval that never
        // occurred, and at these sample sizes the rank is what matters.
        return sorted[(count_ - 1) / 2];
    }

    // The staleness threshold this venue currently warrants.
    double threshold_ms() const noexcept {
        if (!calibrated()) { return kUncalibratedThresholdMs; }
        const double t = kThresholdMultiple * median_ms();
        return t < kThresholdFloorMs   ? kThresholdFloorMs
             : t > kThresholdCeilingMs ? kThresholdCeilingMs
                                       : t;
    }

private:
    std::array<double, kWindowSamples> samples_{};
    std::size_t next_ = 0;
    std::size_t count_ = 0;
    std::int64_t prev_ns_ = 0;
    bool have_prev_ = false;
};

}  // namespace dc::harness
