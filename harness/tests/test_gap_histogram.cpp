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

TEST_CASE("the 1 s bucket edge is inclusive, and it is now a scale rather than a threshold") {
    // THIS BOUNDARY USED TO HAVE A MEANING OUTSIDE THIS FILE AND NO LONGER DOES.
    // `kRxWatchdogMs` was 1,000 ms, so a gap of exactly that was a gap the panel
    // greyed on and `count_from(kFirstLong)` read as "occasions the panel had
    // grounds to grey". M4 stage D deleted the constant: the grey threshold is
    // now calibrated per venue (~2 s at Anvil, ~4 s at Kraken) and this edge is
    // a FIXED MEASUREMENT SCALE, deliberately unmoved so a hole bucketed today
    // is comparable with one bucketed during the 23.6 h soak.
    //
    // What is still pinned here is the CONVENTION — each edge is the inclusive
    // lower bound of the bucket above it — because that is what makes two
    // histograms from different runs comparable at all. The grey count itself is
    // now `GreyLedger::episodes()`, counted rather than inferred.
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

// ---------------------------------------------------------------------------
// percentile_bucket — the distribution that replaced a bare maximum
// ---------------------------------------------------------------------------
//
// M5, after `worst_parse_us` read 4,297 us at D-A1 and 109,798 at D-A2 and
// could not distinguish "the path got 23x slower" from "the path is unchanged
// and one frame in a couple of thousand is an outlier". It was the second. A
// maximum is a sample of size one taken from the worst possible place.
//
// What is pinned here is the ROUNDING, because "which bucket is p99" is exactly
// the kind of boundary two readers assume differently — the same reason this
// file already pins the bucketing convention.
TEST_CASE("percentile_bucket reports the bucket the percentile falls in") {
    using depthcharge::fw::FrameScale;
    using depthcharge::fw::Histogram;

    Histogram<FrameScale> h;

    SUBCASE("an empty distribution reports nothing rather than a confident zero") {
        CHECK(h.percentile_bucket(99) == Histogram<FrameScale>::kBuckets);
        CHECK(std::string(h.label(h.percentile_bucket(99))) == "-");
    }

    SUBCASE("99 fast frames and one slow one put p99 in the FAST bucket") {
        // The 99th of 100 samples is still fast; only the 100th is not. This is
        // the case the whole instrument exists for: a maximum would report
        // 60,000 us and say nothing about the other ninety-nine.
        for (int i = 0; i < 99; ++i) { h.add(100); }     // <0.5ms
        h.add(60'000);                                    // >50ms
        CHECK(std::string(h.label(h.percentile_bucket(99))) == "<0.5ms");
        CHECK(h.worst_us() == 60'000);
        CHECK(h.count_from(FrameScale::kFirstLong) == 1);
        CHECK(h.total() == 100);
    }

    SUBCASE("two slow frames in a hundred push p99 into the slow bucket") {
        for (int i = 0; i < 98; ++i) { h.add(100); }
        h.add(60'000);
        h.add(60'000);
        CHECK(std::string(h.label(h.percentile_bucket(99))) == ">50ms");
    }

    SUBCASE("p99 rounds UP, so it is never the 98th sample's bucket") {
        // ceil(100 * 99 / 100) = 99, so the answer is the 99th sample's bucket.
        for (int i = 0; i < 98; ++i) { h.add(100); }
        h.add(30'000);      // sample 99 -> 25-50
        h.add(60'000);      // sample 100 -> >50ms
        CHECK(std::string(h.label(h.percentile_bucket(99))) == "25-50");
    }

    SUBCASE("p50 and p100 bracket the distribution") {
        for (int i = 0; i < 50; ++i) { h.add(100); }
        for (int i = 0; i < 50; ++i) { h.add(60'000); }
        CHECK(std::string(h.label(h.percentile_bucket(50))) == "<0.5ms");
        CHECK(std::string(h.label(h.percentile_bucket(100))) == ">50ms");
    }

    SUBCASE("a nonsense percentile reports nothing rather than guessing") {
        h.add(100);
        CHECK(h.percentile_bucket(101) == Histogram<FrameScale>::kBuckets);
    }
}

// THE SLOW THRESHOLD IS DERIVED, NOT CHOSEN. The derivation — four consecutive
// frames at this edge consume one ~99.2 ms arrival interval, so the pipe stops
// gaining ground — is asserted in `frame_pipe.hpp`, which is the only place
// that can see `kFrameSlots` as well; this header cannot, because that one
// reaches FreeRTOS and the host suite could not compile it.
//
// What is pinned HERE is the value and the label, so that a change to either
// has to be deliberate in two places rather than silently re-scaling what
// `slow=` on the bench line counts.
TEST_CASE("FrameScale's slow edge and its label are pinned") {
    using depthcharge::fw::FrameScale;
    CHECK(FrameScale::kEdgeUs[FrameScale::kFirstLong - 1] == 25'000);
    CHECK(std::string(FrameScale::kLabel[FrameScale::kFirstLong]) == "25-50");
}

// ---------------------------------------------------------------------------
// ConsecutiveRun — what turns "slow" into "dropped"
// ---------------------------------------------------------------------------
//
// `slow(>25ms)=10 of 1,808` is two different boards depending on the
// arrangement. Ten slow frames spread out cost nothing: the pipe's four slots
// absorb one long frame and drain again. Four in a row consume a whole ~99.2 ms
// arrival interval — which is how the 25 ms edge was derived — so the pipe stops
// gaining ground, and beyond that it drops messages. At a diff venue a dropped
// message is not a skipped refresh; it costs the book and a 50-weight re-seed.
TEST_CASE("ConsecutiveRun reports the longest run, not the count") {
    using depthcharge::fw::ConsecutiveRun;

    SUBCASE("nothing seen reports nothing") {
        ConsecutiveRun r;
        CHECK(r.current() == 0);
        CHECK(r.worst() == 0);
    }

    // THE CASE THE COUNTER EXISTS FOR. Same number of slow frames, opposite
    // verdicts — and `slow=` alone cannot tell them apart.
    SUBCASE("the same count of slow frames gives opposite answers") {
        ConsecutiveRun spread;
        for (int i = 0; i < 4; ++i) {
            spread.note(true);
            spread.note(false);
            spread.note(false);
        }
        CHECK(spread.worst() == 1);      // absorbed; the pipe never falls behind

        ConsecutiveRun burst;
        for (int i = 0; i < 8; ++i) { burst.note(false); }
        for (int i = 0; i < 4; ++i) { burst.note(true); }
        CHECK(burst.worst() == 4);       // four slots' worth, back to back
    }

    SUBCASE("a fast frame ends the run") {
        ConsecutiveRun r;
        r.note(true);
        r.note(true);
        r.note(true);
        CHECK(r.current() == 3);
        r.note(false);
        CHECK(r.current() == 0);
        CHECK(r.worst() == 3);           // the maximum survives the reset
    }

    SUBCASE("a later shorter run does not lower the maximum") {
        ConsecutiveRun r;
        for (int i = 0; i < 5; ++i) { r.note(true); }
        r.note(false);
        r.note(true);
        r.note(true);
        CHECK(r.current() == 2);
        CHECK(r.worst() == 5);
    }

    SUBCASE("an unbroken run is counted whole") {
        ConsecutiveRun r;
        for (int i = 0; i < 100; ++i) { r.note(true); }
        CHECK(r.current() == 100);
        CHECK(r.worst() == 100);
    }
}

// ---------------------------------------------------------------------------
// ResidencyScale — the quantity frame time only approximates
// ---------------------------------------------------------------------------
//
// The board showed the gap: a worst frame-run of 3 against 4 slots, and the pipe
// dropped anyway (`no_slot=9`, `oversize=0`). Frame time is the LAST term of a
// slot's occupancy — a slot is held from acquire, through however many RX reads
// reassemble the message, through the ready queue, and only then through the
// parse. So residency is measured directly rather than inferred.
TEST_CASE("ResidencyScale's held edge is one arrival interval, and is pinned") {
    using depthcharge::fw::ResidencyScale;
    CHECK(ResidencyScale::kHeldUs == 100'000);
    CHECK(std::string(ResidencyScale::kLabel[ResidencyScale::kFirstLong]) == "100-250");

    // The two instruments are anchored to the SAME physical number, reached from
    // opposite sides: one slot's whole life against one frame's parse times the
    // slot count. `frame_pipe.hpp` asserts it where `kFrameSlots` is visible;
    // this pins the residency half so a change has to be deliberate in both.
    CHECK(ResidencyScale::kHeldUs == depthcharge::fw::FrameScale::kSlowUs * 4);
}

TEST_CASE("residency buckets separate a parse from a whole slot life") {
    using depthcharge::fw::Histogram;
    using depthcharge::fw::ResidencyScale;

    Histogram<ResidencyScale> h;
    // A healthy slot: reassembled and parsed well inside one arrival interval.
    for (int i = 0; i < 99; ++i) { h.add(3'000); }
    // One slot held across a stall.
    h.add(180'000);

    CHECK(std::string(h.label(h.percentile_bucket(99))) == "1-5");
    CHECK(h.count_from(ResidencyScale::kFirstLong) == 1);
    CHECK(h.worst_us() == 180'000);
}
