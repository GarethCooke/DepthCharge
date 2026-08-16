// firmware/src/gap_histogram.hpp — fixed-bucket distributions for the feed path.
//
// WHY A DISTRIBUTION AND NOT ANOTHER HIGH-WATER MARK.
//
// `worst_gap_us` told the 2026-08-09 bench that the board had seen a 2,461 ms
// hole in the feed. It could not say how many, nor how big the typical one was,
// nor whether 2,461 ms was one reconnect artefact or the tail of a population of
// 1.2 s stalls — and those have different causes and different fixes. A single
// maximum is the least informative statistic a stall can produce: it is the one
// sample guaranteed to be an outlier.
//
// So this is a fixed array of counters with fixed edges. It answers "how often"
// and "how big, typically" in the same eight numbers, it costs one linear scan
// over six comparisons per sample, and it allocates nothing ever (invariant #7)
// because the storage is the object.
//
// NO ESP-IDF, DELIBERATELY. Same rule as frame_reassembler.hpp: this is
// arithmetic, arithmetic is testable on the desk, and a bucketing function that
// puts a sample in the wrong column would send a bench session chasing the wrong
// candidate. harness/tests/test_gap_histogram.cpp is the proof; the boundary
// convention below is the part worth pinning, because "is 1000 ms in the
// 500-1000 bucket or the 1000-1500 one" is exactly the kind of thing two
// readers assume differently.
//
// CONVENTION: edges are ascending and each is the INCLUSIVE LOWER BOUND of the
// bucket above it. A sample lands in bucket i when edge[i-1] <= v < edge[i].
// Labels therefore read "100-250" for [100 ms, 250 ms).
//
// CROSS-CORE READING. These are written by the task that owns the measurement
// (arrivals by the WebSocket client's task, events by the Core 0 feed task) and
// read by the console on Core 1 with no synchronisation. That is deliberate and
// it is the same trade every counter in this firmware already makes: the counts
// are 32-bit and naturally aligned, so a reader sees a stale value rather than a
// torn one, and the cost of being one sample behind in a log line is nothing.
// They are diagnostics. Nothing may branch on them, and no test may gate on
// them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace depthcharge::fw {

// Edges for inter-arrival and inter-event gaps.
//
// Chosen against what the desk already knows rather than round numbers for their
// own sake. Anvil at full rate has a p99 of 141 ms and a maximum of 391 ms over
// 20,418 frames, and a socket throttled to the board's own message rate has a
// worst book-event silence of 594 ms — so everything healthy must fall in the
// first three buckets, and anything in the fourth is already unusual. The 1000 ms
// edge is the watchdog threshold itself, which makes `count_from(kFirstLong)`
// literally "how many times the panel had grounds to grey". The 1500/2500 split
// above it exists to separate the population the bench saw (1,027 ms and
// 1,470 ms greys) from the single 2,461 ms outlier that may be the blocking
// `esp_websocket_client_stop()` rather than the same phenomenon.
struct GapScale {
    static constexpr std::size_t kEdges = 6;
    static constexpr std::uint32_t kEdgeUs[kEdges] = {
        100000, 250000, 500000, 1000000, 1500000, 2500000,
    };
    static constexpr const char* kLabel[kEdges + 1] = {
        "<100", "100-250", "250-500", "500-1k", "1-1.5k", "1.5-2.5k", ">2.5k",
    };
    // The first bucket at or above the RX watchdog threshold. Everything from
    // here up is a hole long enough to grey the panel.
    static constexpr std::size_t kFirstLong = 4;
};

// Edges for arrival -> event latency: how long a message that was already off
// the socket took to reach the book.
//
// A different scale because it is a different quantity. The healthy value is the
// queue hop plus one parse — `worst_frame` has read 7-19 ms on the bench, so the
// interesting resolution is at the bottom — while the failure this instrument
// exists to catch is a feed task that did not run for a second, which is three
// orders of magnitude away. Hence a log-ish spread rather than the gap scale's
// linear one.
struct LatencyScale {
    static constexpr std::size_t kEdges = 6;
    static constexpr std::uint32_t kEdgeUs[kEdges] = {
        1000, 5000, 25000, 100000, 500000, 1000000,
    };
    static constexpr const char* kLabel[kEdges + 1] = {
        "<1", "1-5", "5-25", "25-100", "100-500", "0.5-1k", ">1k",
    };
    static constexpr std::size_t kFirstLong = 6;
};

// Saturating narrow to 32 bits, for the high-water marks that sit beside these
// histograms.
//
// Shared rather than written twice because both call sites are "keep the worst
// value I have seen" on a target where an interval is measured as a 64-bit
// microsecond count and reported as a 32-bit one, and two hand-written clamps
// are two chances to drop the wrong end. 32 bits of microseconds is 71 minutes;
// anything longer has already said everything it can say.
inline std::uint32_t clamp_us_to_u32(std::uint64_t value_us) noexcept {
    return (value_us > 0xFFFFFFFFull) ? 0xFFFFFFFFu : static_cast<std::uint32_t>(value_us);
}

// Microseconds per millisecond, for every instrument that measures in the first
// and prints in the second.
//
// A bare `1000u` was inlined at four call sites across three headers before
// `ws_ping.hpp` arrived and made it five. That is the repeated-literal case, and
// the reason it is worth a name rather than a shrug is the same one every other
// shared helper here has: what these lines are actually doing — converting the
// unit an interval is *measured* in to the unit it is *read* in — is invisible
// in a bare divisor, and a site that used 1000000u by mistake would compile,
// print a plausible small number, and be wrong only on the bench.
//
// **32-BIT, AND THE WIDTH IS THE WHOLE DECISION.** Every existing call site
// divides a `std::uint32_t` (`gap_us`, `recovery_us[i]`, `window_us`, a clamped
// reject age). Declaring this `std::uint64_t` — which is what a "microseconds"
// constant looks like it should be — would promote all four to 64-bit division,
// and the LX7 has no 64-bit divide instruction, so each would become a libgcc
// call in code that runs per event. `ws_ping.hpp` divides a `std::uint64_t` by
// it and is unaffected: that division was already 64-bit and stays so.
inline constexpr std::uint32_t kUsPerMs = 1000;

// Append-with-truncation, for every fixed-buffer render in the firmware.
//
// `written` is what snprintf returned, `cap` the whole buffer and `at` the
// cursor, advanced in place. Returns true when there is no room for more, so a
// caller's loop reads `if (append_truncating(n, cap, at)) { break; }`.
//
// Shared for the same reason clamp_us_to_u32 above is: it was written twice —
// once here and once in the stall probe's renderer — and the failure mode of
// getting it wrong is not a crash but a log line that is silently one field
// short, in the one place a bench session is reading for a verdict. The
// off-by-one to get right is that snprintf returns the length it WANTED, so
// `written >= remaining` is truncation even though nothing was overrun.
inline bool append_truncating(int written, std::size_t cap, std::size_t& at) noexcept {
    if (written < 0) { return true; }
    if (static_cast<std::size_t>(written) >= cap - at) {
        at = cap - 1;
        return true;
    }
    at += static_cast<std::size_t>(written);
    return false;
}

template <typename Scale>
class Histogram {
public:
    static constexpr std::size_t kBuckets = Scale::kEdges + 1;

    void add(std::uint64_t value_us) noexcept {
        std::size_t i = 0;
        while (i < Scale::kEdges && value_us >= Scale::kEdgeUs[i]) { ++i; }
        ++counts_[i];
        ++total_;
        const std::uint32_t clamped = clamp_us_to_u32(value_us);
        if (clamped > worst_us_) { worst_us_ = clamped; }
    }

    std::uint32_t count(std::size_t bucket) const noexcept {
        return (bucket < kBuckets) ? counts_[bucket] : 0;
    }
    std::uint32_t total() const noexcept { return total_; }
    std::uint32_t worst_us() const noexcept { return worst_us_; }

    // How many samples landed in `first` or above — the "how often" the bench
    // could not answer. With Scale::kFirstLong this is the count of holes long
    // enough to have greyed the panel.
    std::uint32_t count_from(std::size_t first) const noexcept {
        std::uint32_t n = 0;
        for (std::size_t i = first; i < kBuckets; ++i) { n += counts_[i]; }
        return n;
    }

    // The fullest bucket at or above `first`, or kBuckets if every bucket from
    // there up is empty. Ties resolve to the LOWER bucket, so a mode of "1-1.5k"
    // is never reported for a distribution whose 1.5-2.5k bucket merely equals
    // it — the conservative direction for a number that ranks candidates.
    std::size_t mode_from(std::size_t first) const noexcept {
        std::size_t best = kBuckets;
        std::uint32_t best_n = 0;
        for (std::size_t i = first; i < kBuckets; ++i) {
            if (counts_[i] > best_n) {
                best_n = counts_[i];
                best = i;
            }
        }
        return best;
    }

    static const char* label(std::size_t bucket) noexcept {
        return (bucket < kBuckets) ? Scale::kLabel[bucket] : "-";
    }

    // Writes "<100:1234 100-250:56 ..." into `out`, always NUL-terminated,
    // truncating rather than overrunning. Returns the length written.
    //
    // Here rather than in the console because it is the half of the printing
    // that can be wrong in a way a human will not notice — a label paired with
    // the neighbouring bucket's count reads perfectly and means nothing — so it
    // is pinned by the host test with the rest of the bucketing.
    std::size_t render(char* out, std::size_t cap) const noexcept {
        if (out == nullptr || cap == 0) { return 0; }
        out[0] = '\0';
        std::size_t at = 0;
        for (std::size_t i = 0; i < kBuckets; ++i) {
            const int n = std::snprintf(out + at, cap - at, "%s%s:%u", (i == 0) ? "" : " ",
                                        Scale::kLabel[i], static_cast<unsigned>(counts_[i]));
            if (append_truncating(n, cap, at)) { break; }
        }
        return at;
    }

private:
    std::uint32_t counts_[kBuckets]{};
    std::uint32_t total_ = 0;
    std::uint32_t worst_us_ = 0;
};

using GapHistogram = Histogram<GapScale>;
using LatencyHistogram = Histogram<LatencyScale>;

}  // namespace depthcharge::fw
