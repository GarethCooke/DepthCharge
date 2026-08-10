// test_gap_histogram.cpp — the firmware's bucketed gap distributions, on the desk.
//
// The second firmware header the host build knows about, and here for the same
// reason as the first (frame_reassembler.hpp): it is arithmetic, arithmetic is
// cheap to test, and a bucketing function that is wrong by one column is the
// worst possible failure for this particular instrument. It does not crash, it
// does not stall the panel — it prints a plausible distribution that sends a
// bench session after the wrong candidate, and the only way anyone finds out is
// by measuring the same thing twice.
//
// The boundary convention is the part that most needs pinning: is a gap of
// exactly 1000 ms — the RX watchdog threshold itself, the most load-bearing
// value on the scale — in the 500-1k bucket or the 1-1.5k one? The header says
// each edge is the inclusive lower bound of the bucket above, so it is the
// latter, and `count_from(kFirstLong)` therefore counts exactly the holes long
// enough to have greyed the panel.
#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "gap_histogram.hpp"

using depthcharge::fw::GapHistogram;
using depthcharge::fw::GapScale;
using depthcharge::fw::LatencyHistogram;
using depthcharge::fw::LatencyScale;

namespace {

// Every edge, one microsecond below it, and the edge itself — the three samples
// per boundary that catch an off-by-one in either direction.
template <typename Hist, typename Scale>
void check_edges_land_upward() {
    for (std::size_t e = 0; e < Scale::kEdges; ++e) {
        Hist below;
        below.add(Scale::kEdgeUs[e] - 1);
        CHECK(below.count(e) == 1);
        CHECK(below.count(e + 1) == 0);

        Hist at;
        at.add(Scale::kEdgeUs[e]);
        CHECK(at.count(e) == 0);
        CHECK(at.count(e + 1) == 1);
    }
}

}  // namespace

TEST_CASE("an edge is the inclusive lower bound of the bucket above it") {
    check_edges_land_upward<GapHistogram, GapScale>();
    check_edges_land_upward<LatencyHistogram, LatencyScale>();
}

TEST_CASE("the watchdog threshold is the boundary of the >1s buckets") {
    // The one boundary with a meaning outside this file: kRxWatchdogMs is
    // 1000 ms, so a gap of exactly that is a gap the panel greys on, and it must
    // be counted with the long ones.
    GapHistogram h;
    h.add(999999);
    CHECK(h.count_from(GapScale::kFirstLong) == 0);
    h.add(1000000);
    CHECK(h.count_from(GapScale::kFirstLong) == 1);
    h.add(2500000);
    CHECK(h.count_from(GapScale::kFirstLong) == 2);
}

TEST_CASE("a healthy Anvil distribution puts nothing in the long buckets") {
    // The measured shape, from the 2026-08-09 desk capture: p50 63 ms, p99
    // 141 ms, max 391 ms over 20,418 frames. Every one of those is in the first
    // three buckets, which is what makes a single count in the fourth
    // interesting rather than noise.
    GapHistogram h;
    for (int i = 0; i < 100; ++i) { h.add(63000); }
    h.add(141000);
    h.add(391000);

    CHECK(h.total() == 102);
    CHECK(h.count(0) == 100);   // <100 ms — the whole steady state
    CHECK(h.count(1) == 1);     // 141 ms is 100-250
    CHECK(h.count(2) == 1);     // 391 ms is 250-500
    CHECK(h.count_from(GapScale::kFirstLong) == 0);
    CHECK(h.worst_us() == 391000);
}

TEST_CASE("the bench's own numbers land where the diagnosis needs them") {
    // The three greys the 2026-08-09 bench saw with the socket up, plus the
    // outlier the brief suspects is a reconnect artefact rather than the same
    // phenomenon. They must separate, or the histogram cannot tell the two
    // populations apart — which is the entire reason there are three buckets
    // above 1 s instead of one.
    GapHistogram h;
    h.add(1027000);   // 18:21 trip
    h.add(1470000);   // 18:21:35 grey
    h.add(2461000);   // the outlier
    h.add(9453000);   // the real socket outage

    CHECK(h.count(4) == 2);   // 1-1.5k
    CHECK(h.count(5) == 1);   // 1.5-2.5k
    CHECK(h.count(6) == 1);   // >2.5k
    CHECK(h.count_from(GapScale::kFirstLong) == 4);
    CHECK(h.worst_us() == 9453000);
}

TEST_CASE("the mode is the fullest long bucket, and ties resolve downward") {
    GapHistogram h;
    h.add(1100000);
    h.add(1200000);
    h.add(1600000);
    CHECK(std::string(GapHistogram::label(h.mode_from(GapScale::kFirstLong))) == "1-1.5k");

    // A tie must not promote the higher bucket: ranking candidates on a
    // distribution that says "mostly 1.5-2.5 s" when it is evenly split would
    // point at the blocking stop() rather than at the population.
    h.add(1700000);
    CHECK(std::string(GapHistogram::label(h.mode_from(GapScale::kFirstLong))) == "1-1.5k");
}

TEST_CASE("an empty range reports no mode rather than bucket zero") {
    GapHistogram h;
    h.add(50000);
    CHECK(h.mode_from(GapScale::kFirstLong) == GapHistogram::kBuckets);
    CHECK(std::string(GapHistogram::label(h.mode_from(GapScale::kFirstLong))) == "-");
}

TEST_CASE("render pairs every label with its own count") {
    // The failure this catches is invisible on a bench log: a label printed
    // beside the neighbouring bucket's count reads perfectly and means the
    // opposite thing.
    GapHistogram h;
    h.add(10000);     // <100
    h.add(120000);    // 100-250
    h.add(120000);
    h.add(3000000);   // >2.5k

    char line[208];
    const std::size_t n = h.render(line, sizeof line);
    CHECK(std::string(line) ==
          "<100:1 100-250:2 250-500:0 500-1k:0 1-1.5k:0 1.5-2.5k:0 >2.5k:1");
    CHECK(n == std::strlen(line));
}

TEST_CASE("render truncates rather than overruns") {
    // Seven labelled counts do not fit in twelve bytes. The buffer is bigger
    // than the capacity it is given so that the bytes past the capacity are a
    // guard region: the property is that nothing beyond `cap` is touched and
    // that what was written is still a NUL-terminated string.
    GapHistogram h;
    h.add(1000);

    char buf[64];
    std::memset(buf, 0x7f, sizeof buf);
    constexpr std::size_t kCap = 12;

    const std::size_t n = h.render(buf, kCap);
    CHECK(n < kCap);
    CHECK(std::strlen(buf) == n);
    for (std::size_t i = kCap; i < sizeof buf; ++i) { CHECK(buf[i] == 0x7f); }
}

TEST_CASE("render into a one-byte buffer writes only the terminator") {
    GapHistogram h;
    h.add(1000);

    char buf[8];
    std::memset(buf, 0x7f, sizeof buf);
    CHECK(h.render(buf, 1) == 0);
    CHECK(buf[0] == '\0');
    CHECK(buf[1] == 0x7f);

    // And a refusal is a refusal: no capacity, no write.
    CHECK(h.render(buf, 0) == 0);
    CHECK(h.render(nullptr, 64) == 0);
}

TEST_CASE("a fresh histogram is all zeroes") {
    // It lives in a statically-allocated Stats struct, so this is really a check
    // that nothing needs initialising at run time (invariant #7).
    GapHistogram h;
    CHECK(h.total() == 0);
    CHECK(h.worst_us() == 0);
    for (std::size_t i = 0; i < GapHistogram::kBuckets; ++i) { CHECK(h.count(i) == 0); }

    char line[208];
    h.render(line, sizeof line);
    CHECK(std::string(line) ==
          "<100:0 100-250:0 250-500:0 500-1k:0 1-1.5k:0 1.5-2.5k:0 >2.5k:0");
}

TEST_CASE("latency buckets resolve the millisecond end, where healthy lives") {
    // worst_frame on the bench reads 7-19 ms, so the useful resolution is at the
    // bottom; the failure mode this instrument hunts is a second of queue wait,
    // three orders of magnitude away.
    LatencyHistogram h;
    h.add(400);        // <1 ms: the queue hop alone
    h.add(8000);       // 5-25: a big book frame parsed
    h.add(18171);      // 5-25: the worst frame the bench has recorded
    h.add(1500000);    // >1k: the failure

    CHECK(h.count(0) == 1);
    CHECK(h.count(2) == 2);
    CHECK(h.count(6) == 1);
    CHECK(h.count_from(LatencyScale::kFirstLong) == 1);
    CHECK(h.worst_us() == 1500000);
}

TEST_CASE("the worst-value clamp saturates rather than wrapping") {
    // Shared with FeedTask's queue-wait high-water mark, and the failure it
    // guards against is a 71-minute gap reported as a few milliseconds — the one
    // direction a stall instrument must never be wrong in.
    using depthcharge::fw::clamp_us_to_u32;
    CHECK(clamp_us_to_u32(0) == 0);
    CHECK(clamp_us_to_u32(0xFFFFFFFEull) == 0xFFFFFFFEu);
    CHECK(clamp_us_to_u32(0xFFFFFFFFull) == 0xFFFFFFFFu);
    CHECK(clamp_us_to_u32(0x100000000ull) == 0xFFFFFFFFu);
    CHECK(clamp_us_to_u32(0xFFFFFFFFFFFFFFFFull) == 0xFFFFFFFFu);

    GapHistogram h;
    h.add(0x1FFFFFFFFull);
    CHECK(h.worst_us() == 0xFFFFFFFFu);
    CHECK(h.count(GapHistogram::kBuckets - 1) == 1);
}

TEST_CASE("an out-of-range bucket index reads zero rather than off the end") {
    GapHistogram h;
    h.add(1000);
    CHECK(h.count(GapHistogram::kBuckets) == 0);
    CHECK(h.count(999) == 0);
}
