// test_stall_probe.cpp — the board's stall verdict, argued on the desk first.
//
// The third firmware header the host build knows about, and the one with the
// most to lose from being wrong. `frame_reassembler.hpp` fails loudly (a leaked
// slot, a truncated frame); `gap_histogram.hpp` fails by one column. This one
// fails by printing **BOARD-BOUND** under a hole that was link-bound, which
// sends the next session to pull `esp_websocket_client` buffer levers at a
// problem living in the 2.4 GHz band — a whole milestone round, spent on the
// strength of a label nobody could check.
//
// So every rule that turns numbers into that label is tested at its boundaries:
// the idle arithmetic (including the 32-bit cycle-counter wrap that would
// otherwise manufacture a hole-sized idle figure roughly every 18 seconds), the
// verdict's absolute rails and its relative rule, the burst-vs-cadence split,
// the ordering that decides which hole a message's arrival belongs to, and the
// rendering — because a verdict printed beside the wrong hole's numbers reads
// perfectly and means nothing.
#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

#include "gap_histogram.hpp"
#include "stall_probe.hpp"

using depthcharge::fw::classify_shape;
using depthcharge::fw::GapHistogram;
using depthcharge::fw::GapScale;
using depthcharge::fw::classify_verdict;
using depthcharge::fw::HoleRecord;
using depthcharge::fw::HoleVerdict;
using depthcharge::fw::IdleAccumulator;
using depthcharge::fw::idle_percent;
using depthcharge::fw::kBurstGapUs;
using depthcharge::fw::kHoleThresholdUs;
using depthcharge::fw::kIdleContinuityUs;
using depthcharge::fw::kIdleDropPct;
using depthcharge::fw::kPlentyIdlePct;
using depthcharge::fw::kRecoverySamples;
using depthcharge::fw::kStarvedIdlePct;
using depthcharge::fw::LinkQuality;
using depthcharge::fw::RecoveryShape;
using depthcharge::fw::StallProbe;
using depthcharge::fw::wrapping_delta_u32;

namespace {

constexpr std::uint32_t kMhz = 240;

// Drive an accumulator across a synthetic timeline. `busy_us` is a stretch where
// the idle task did not run at all — no passes happen inside it — and `idle_us`
// is a stretch of continuous idle, sampled at the loop period the real hook runs
// at. Returns the cycle count it left off at, so timelines can be chained.
std::uint32_t run_idle(IdleAccumulator& acc, std::uint32_t at_cycles, std::uint32_t idle_us,
                       std::uint32_t loop_us = 1) {
    const std::uint32_t step = loop_us * kMhz;
    for (std::uint32_t us = 0; us < idle_us; us += loop_us) {
        at_cycles += step;
        acc.note_pass(at_cycles);
    }
    return at_cycles;
}

// A probe with a healthy baseline already established: `windows` sub-threshold
// windows of 80 ms each, `idle_pct` of every one of them idle.
StallProbe probe_with_baseline(std::uint8_t idle_pct, std::uint32_t windows = 100) {
    StallProbe p;
    const std::uint32_t window_us = 80000;
    const std::uint32_t idle_us = (window_us / 100u) * idle_pct;
    for (std::uint32_t i = 0; i < windows; ++i) {
        p.note_event(window_us, idle_us, window_us, /*idle_valid=*/true,
                     /*wire_seq=*/static_cast<std::int64_t>(i) * 4, /*rssi=*/-60,
                     /*socket_dropped=*/false);
    }
    return p;
}

// Feed a hole's recovery series. Most cases only need the shape to be "tight"
// or "cadence", and spelling four note_message calls out per case buried the one
// thing each of them was testing; the explicit form stays for the cases where
// the individual intervals are the point.
void recover(StallProbe& p, std::uint32_t each_us, std::size_t n = kRecoverySamples) {
    for (std::size_t i = 0; i < n; ++i) { p.note_message(each_us); }
}

void recover_with(StallProbe& p, std::initializer_list<std::uint32_t> intervals_us) {
    for (std::uint32_t us : intervals_us) { p.note_message(us); }
}

}  // namespace

// --- the arithmetic under the verdict ---------------------------------------

TEST_CASE("the cycle counter's 32-bit wrap does not manufacture idle") {
    // At 240 MHz each core's counter wraps every ~17.9 s, so a run of any length
    // crosses it repeatedly. An accumulator that got this wrong would post one
    // enormous interval — rejected by the continuity rule, so the visible damage
    // is not a huge idle figure but a LOST one: idle silently stops being
    // counted across the wrap, and the hole that happens to span it reads as
    // starved. Both directions are checked.
    IdleAccumulator acc;
    acc.configure(kMhz);

    // Start 1 ms of cycles before the wrap and walk 2 ms of continuous idle
    // straight through it.
    std::uint32_t at = 0u - (1000u * kMhz);
    acc.note_pass(at);  // first pass ever: no interval yet
    at = run_idle(acc, at, 2000);

    CHECK(acc.idle_us() == 2000);
    CHECK(acc.resumes() == 1);  // only the very first pass
}

TEST_CASE("only continuous stretches count as idle") {
    IdleAccumulator acc;
    acc.configure(kMhz);
    std::uint32_t at = 1000 * kMhz;
    acc.note_pass(at);

    at = run_idle(acc, at, 5000);  // 5 ms idle
    CHECK(acc.idle_us() == 5000);

    // Now a task runs for 8 ms: the idle hook is simply not called, and the next
    // pass is 8 ms after the previous one. That interval is work, not idle.
    at += 8000 * kMhz;
    acc.note_pass(at);
    CHECK(acc.idle_us() == 5000);
    CHECK(acc.resumes() == 2);  // the first pass, and coming back from the task

    at = run_idle(acc, at, 3000);
    CHECK(acc.idle_us() == 8000);
}

TEST_CASE("the continuity threshold is the boundary between idle and work") {
    IdleAccumulator acc;
    acc.configure(kMhz);
    acc.note_pass(0);

    // Exactly at the threshold is still idle; one microsecond past it is not.
    acc.note_pass(kIdleContinuityUs * kMhz);
    CHECK(acc.idle_us() == kIdleContinuityUs);

    acc.note_pass(kIdleContinuityUs * kMhz + (kIdleContinuityUs + 1) * kMhz);
    CHECK(acc.idle_us() == kIdleContinuityUs);
}

TEST_CASE("sub-microsecond passes accumulate instead of flooring to zero") {
    // The real loop period is a fraction of a microsecond. A naive
    // cycles-to-microseconds division per pass would floor every one of them to
    // zero and report a fully idle core as fully busy — the single most
    // dangerous rounding in this file, because it fabricates board-bound.
    IdleAccumulator acc;
    acc.configure(kMhz);
    std::uint32_t at = 0;
    acc.note_pass(at);
    for (int i = 0; i < 10000; ++i) {  // 10,000 passes of 100 cycles = 4166 us
        at += 100;
        acc.note_pass(at);
    }
    CHECK(acc.idle_us() == (10000u * 100u) / kMhz);
}

TEST_CASE("idle_percent saturates rather than reporting more than a full window") {
    CHECK(idle_percent(0, 1000) == 0);
    CHECK(idle_percent(500, 1000) == 50);
    CHECK(idle_percent(1000, 1000) == 100);
    CHECK(idle_percent(1001, 1000) == 100);
    CHECK(idle_percent(100, 0) == 0);
    // A two-and-a-half second hole: the multiply must not overflow 32 bits.
    CHECK(idle_percent(2500000, 2500000) == 100);
    CHECK(idle_percent(250000, 2500000) == 10);
}

TEST_CASE("wrapping_delta_u32 is the unsigned difference, in both directions") {
    CHECK(wrapping_delta_u32(10, 4) == 6);
    CHECK(wrapping_delta_u32(4, 0xFFFFFFFAu) == 10);
    CHECK(wrapping_delta_u32(0, 0) == 0);
}

// --- the verdict rule --------------------------------------------------------

TEST_CASE("the absolute rails hold whatever the baseline says") {
    // A core with no headroom is board-bound even on a run whose baseline is
    // itself low, and a core with headroom to spare is link-bound even on a run
    // that is normally idler still. Without the rails, a saturated board would
    // classify as "mixed" forever.
    CHECK(classify_verdict(kStarvedIdlePct, /*baseline=*/30, true) == HoleVerdict::BoardBound);
    CHECK(classify_verdict(kStarvedIdlePct, /*baseline=*/20, true) == HoleVerdict::BoardBound);
    CHECK(classify_verdict(kPlentyIdlePct, /*baseline=*/99, true) == HoleVerdict::LinkBound);
    CHECK(classify_verdict(100, /*baseline=*/99, true) == HoleVerdict::LinkBound);
}

TEST_CASE("between the rails the run's own healthy idle is the reference") {
    const std::uint8_t baseline = 70;
    // A material drop below the baseline is the board doing more work than usual.
    CHECK(classify_verdict(static_cast<std::uint8_t>(baseline - kIdleDropPct), baseline, true) ==
          HoleVerdict::BoardBound);
    // Just above that drop is honestly ambiguous and is reported as such.
    CHECK(classify_verdict(static_cast<std::uint8_t>(baseline - kIdleDropPct + 1), baseline,
                           true) == HoleVerdict::Mixed);
    // At or above baseline the core was no busier than normal with nothing to do.
    CHECK(classify_verdict(baseline, baseline, true) == HoleVerdict::LinkBound);
    CHECK(classify_verdict(74, baseline, true) == HoleVerdict::LinkBound);
}

TEST_CASE("with no baseline yet, only the rails may speak") {
    CHECK(classify_verdict(50, 0, /*have_baseline=*/false) == HoleVerdict::Mixed);
    CHECK(classify_verdict(10, 0, /*have_baseline=*/false) == HoleVerdict::BoardBound);
    CHECK(classify_verdict(90, 0, /*have_baseline=*/false) == HoleVerdict::LinkBound);
}

TEST_CASE("recovery shape separates a backlog flush from a resumed cadence") {
    // A TCP backlog flushing: the frames existed and arrive back-to-back.
    const std::uint32_t burst[kRecoverySamples] = {2000, 3000, 5000, 81000};
    CHECK(classify_shape(burst, kRecoverySamples) == RecoveryShape::Burst);

    // Anvil's ~80 ms republish simply resuming: the middle was never sent.
    const std::uint32_t cadence[kRecoverySamples] = {79000, 80000, 78000, 81000};
    CHECK(classify_shape(cadence, kRecoverySamples) == RecoveryShape::Cadence);

    // One tight gap is a coincidence, not a flush.
    const std::uint32_t single[kRecoverySamples] = {3000, 80000, 79000, 80000};
    CHECK(classify_shape(single, kRecoverySamples) == RecoveryShape::Cadence);

    // The boundary itself, on both sides.
    const std::uint32_t at_edge[2] = {kBurstGapUs, kBurstGapUs};
    CHECK(classify_shape(at_edge, 2) == RecoveryShape::Burst);
    const std::uint32_t past_edge[2] = {kBurstGapUs + 1, kBurstGapUs + 1};
    CHECK(classify_shape(past_edge, 2) == RecoveryShape::Cadence);

    // Too few samples to say anything is said as nothing.
    CHECK(classify_shape(burst, 0) == RecoveryShape::Unknown);
    CHECK(classify_shape(burst, 1) == RecoveryShape::Unknown);
}

// --- the probe's bookkeeping -------------------------------------------------

TEST_CASE("healthy windows build the baseline and are not holes") {
    StallProbe p = probe_with_baseline(90);
    CHECK(p.holes() == 0);
    CHECK(p.completed() == 0);
    CHECK(p.baseline_windows() == 100);
    CHECK(p.baseline0_pct() == 90);
}

TEST_CASE("a hole is exactly what the watchdog greys on") {
    StallProbe p;
    // One microsecond under the threshold is a healthy window.
    p.note_event(kHoleThresholdUs - 1, 0, 0, true, 100, -60, false);
    CHECK(p.holes() == 0);
    // The threshold itself is a hole — the same boundary convention the gap
    // histogram uses, so `holes n=` and `event >1s=` on the bench log count the
    // same events and can be compared line to line.
    p.note_event(kHoleThresholdUs, 0, 0, true, 200, -60, false);
    CHECK(p.holes() == 1);
}

TEST_CASE("a starved Core 0 through the hole reads board-bound, with a burst recovery") {
    StallProbe p = probe_with_baseline(90);
    // 1.4 s of silence in which Core 0 was idle for only 120 ms.
    p.note_event(1400000, 120000, 1380000, true, /*seq=*/1000, /*rssi=*/-70, false);
    // Then four messages back-to-back: the frames existed and were delivered late.
    recover_with(p, {2000, 3000, 4000, 80000});

    REQUIRE(p.completed() == 1);
    const HoleRecord* r = p.completed_at(0);
    REQUIRE(r != nullptr);
    CHECK(r->ordinal == 1);
    CHECK(r->gap_ms == 1400);
    CHECK(r->core0_idle_pct == 8);
    CHECK(r->core1_idle_pct == 98);
    CHECK(r->baseline0_pct == 90);
    CHECK(r->rssi_dbm == -70);
    CHECK(r->verdict == HoleVerdict::BoardBound);
    CHECK(r->shape == RecoveryShape::Burst);
    CHECK(p.board_bound() == 1);
    CHECK(p.bursts() == 1);
    CHECK(p.link_bound() == 0);
}

TEST_CASE("an idle Core 0 through the hole reads link-bound, cadence resuming") {
    StallProbe p = probe_with_baseline(88);
    p.note_event(1200000, 1190000, 1190000, true, 1000, -78, false);
    recover_with(p, {80000, 79000, 81000, 80000});

    REQUIRE(p.completed() == 1);
    const HoleRecord* r = p.completed_at(0);
    REQUIRE(r != nullptr);
    CHECK(r->verdict == HoleVerdict::LinkBound);
    CHECK(r->shape == RecoveryShape::Cadence);
    CHECK(p.link_bound() == 1);
    CHECK(p.cadences() == 1);
}

TEST_CASE("without a working idle probe the verdict is unknown, never zero-idle") {
    // The failure that must not happen: a probe that did not register reports
    // 0 us of idle, which would classify every hole in the run as board-bound
    // and read as an unusually decisive result.
    StallProbe p = probe_with_baseline(90);
    p.note_event(1400000, 0, 0, /*idle_valid=*/false, 1000, -70, false);
    p.note_message(2000);
    p.note_message(3000);
    p.note_message(4000);
    p.note_message(5000);

    REQUIRE(p.completed() == 1);
    const HoleRecord* r = p.completed_at(0);
    REQUIRE(r != nullptr);
    CHECK(r->idle_valid == false);
    CHECK(r->verdict == HoleVerdict::Unknown);
    CHECK(p.unknown() == 1);
    CHECK(p.board_bound() == 0);
    // The shape is still measurable without an idle probe and is still recorded.
    CHECK(r->shape == RecoveryShape::Burst);
}

TEST_CASE("the message that ends a hole belongs to the previous hole's recovery") {
    // The ordering bug this pins: if a hole's own closing message were counted
    // as its first recovery sample, every hole would report a first gap equal to
    // itself and every recovery would classify as a cadence.
    StallProbe p = probe_with_baseline(90);
    p.note_message(80000);  // healthy traffic, no hole open, ignored
    p.note_event(1400000, 100000, 1390000, true, 1000, -70, false);

    const HoleRecord* r = p.completed_at(0);
    CHECK(r == nullptr);  // still open, not yet classified

    p.note_message(2000);
    p.note_message(2000);
    REQUIRE(p.completed() == 0);  // still gathering
    p.note_message(2000);
    p.note_message(2000);
    REQUIRE(p.completed() == 1);
    r = p.completed_at(0);
    REQUIRE(r != nullptr);
    CHECK(r->recovery_n == kRecoverySamples);
    CHECK(r->recovery_us[0] == 2000);
    CHECK(r->shape == RecoveryShape::Burst);
}

TEST_CASE("a hole cut short by the next hole is finalised with what it has") {
    StallProbe p = probe_with_baseline(90);
    p.note_event(1400000, 100000, 1390000, true, 1000, -70, false);
    p.note_message(2000);
    // The feed stalls again before the recovery series completed.
    p.note_event(1100000, 90000, 1090000, true, 1100, -70, false);

    CHECK(p.holes() == 2);
    CHECK(p.completed() == 1);
    const HoleRecord* first = p.completed_at(0);
    REQUIRE(first != nullptr);
    CHECK(first->recovery_n == 1);
    CHECK(first->shape == RecoveryShape::Unknown);  // one sample says nothing
    CHECK(first->verdict == HoleVerdict::BoardBound);
}

TEST_CASE("the wire seq step separates a stale frame from a fresh one") {
    // Anvil's global seq advances ~50 per second in this timeline: 100 windows
    // of 80 ms, 4 seq steps each.
    StallProbe p = probe_with_baseline(90);
    CHECK(p.seq_per_s() == 50);

    // A hole ended by a frame whose seq has barely moved: it was sitting
    // somewhere and was delivered late.
    p.note_event(1000000, 100000, 990000, true, /*seq=*/396 + 2, -70, false);
    recover(p, 2000);
    const HoleRecord* stale = p.completed_at(0);
    REQUIRE(stale != nullptr);
    CHECK(stale->seq_step == 2);
    // What a fresh frame would have carried, priced off the rate as it stood
    // BEFORE this hole. Folding the hole's own second into that rate first — the
    // first version of this code did — pulls the prediction down toward whatever
    // was observed (44 here), so a long stale hole would look less stale than a
    // short one. The prediction has to come from the healthy stretch it is
    // predicting against.
    CHECK(stale->seq_expected == 50);
}

TEST_CASE("a socket drop inside a hole is carried on the record") {
    // A reconnect costs a ~2.5 s blocking stop() on Core 1, which is a different
    // phenomenon from the steady-state stall and must never be pooled with it.
    StallProbe p = probe_with_baseline(90);
    p.note_event(2400000, 100000, 100000, true, 1000, -70, /*socket_dropped=*/true);
    recover(p, 2000);
    const HoleRecord* r = p.completed_at(0);
    REQUIRE(r != nullptr);
    CHECK(r->socket_dropped);
}

TEST_CASE("the ring keeps the most recent holes and says which it dropped") {
    StallProbe p = probe_with_baseline(90);
    for (int i = 0; i < 20; ++i) {
        p.note_event(1200000, 100000, 1190000, true, 1000 + i * 60, -70, false);
        recover(p, 2000);
    }
    CHECK(p.holes() == 20);
    CHECK(p.completed() == 20);
    // One slot short of the ring's depth: the last is reserved for whichever
    // hole is currently open, so it can never be handed out as a finalised one.
    CHECK(p.oldest_retained() == 20 - (depthcharge::fw::kHoleLogDepth - 1));
    CHECK(p.completed_at(0) == nullptr);                    // long since evicted
    CHECK(p.completed_at(p.oldest_retained()) != nullptr);  // the oldest still held
    CHECK(p.completed_at(p.oldest_retained() - 1) == nullptr);
    const HoleRecord* newest = p.completed_at(19);
    REQUIRE(newest != nullptr);
    CHECK(newest->ordinal == 20);
    // Every hole is tallied whether or not its record survived — the counts are
    // the run's answer, the ring is only the detail.
    CHECK(p.board_bound() == 20);
}

TEST_CASE("the two instruments count the same holes — the bench log's own cross-check") {
    // `firmware/README.md` tells the reader that `holes n=` must equal `>1s=` on
    // the `event` line, and that a disagreement is a bug rather than a nuance.
    // They are computed by different code from the same samples, so that claim
    // is worth being a test: it is the one assertion the person at the bench is
    // asked to make by eye.
    StallProbe p;
    GapHistogram h;
    // A plausible mixed population: healthy cadence, a few near misses either
    // side of the threshold, and holes of each size the scale distinguishes.
    const std::uint32_t gaps_us[] = {
        63000,   79000,   141000,  391000,  594000,  999999,  kHoleThresholdUs,
        1027000, 1470000, 640200,  1893000, 78000,   2461000, 80000,
    };
    for (std::uint32_t g : gaps_us) {
        h.add(g);
        p.note_event(g, g / 2, g / 2, true, 0, -60, false);
        recover(p, 80000);
    }
    CHECK(p.holes() == h.count_from(GapScale::kFirstLong));
    CHECK(p.holes() == 5);
}

TEST_CASE("the tally is closed under any sequence of events and messages") {
    // A property rather than an example, in the shape frame_reassembler's random
    // walk established: whatever arrives in whatever order, the counts have to
    // stay consistent with each other. The invariants are the ones a reader of
    // the log line implicitly assumes, and each has a way of quietly breaking —
    // a hole finalised twice would double-count a verdict; one never finalised
    // would leave the tally short of `n`.
    StallProbe p;
    std::uint32_t seed = 0x5EED1234u;
    const auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };

    // The walk asserts once at the end rather than per step: 20,000 steps of
    // CHECK would bury the suite's assertion count under one test and say
    // nothing extra, so a violation records what broke and where and stops.
    std::string broke;
    int broke_at = -1;
    const auto require_that = [&](bool ok, const char* what, int step) {
        if (ok || broke_at >= 0) { return ok; }
        broke = what;
        broke_at = step;
        return false;
    };

    std::uint32_t prev_ordinal = 0;
    for (int step = 0; step < 20000 && broke_at < 0; ++step) {
        if ((next() % 4u) == 0u) {
            // An event: mostly healthy, occasionally a hole of some size.
            const bool hole = (next() % 8u) == 0u;
            const std::uint32_t gap =
                hole ? (kHoleThresholdUs + next() % 2000000u) : (next() % kHoleThresholdUs);
            const std::uint32_t idle = next() % (gap + 1u);
            p.note_event(gap, idle, idle, (next() % 16u) != 0u,
                         static_cast<std::int64_t>(next() % 100u), -60, (next() % 32u) == 0u);
        } else {
            p.note_message(next() % 200000u);
        }

        // Every finalised hole is classified into exactly one verdict bucket and
        // at most one shape bucket.
        require_that(p.board_bound() + p.link_bound() + p.mixed() + p.unknown() == p.completed(),
                     "verdict buckets do not sum to the finalised count", step);
        require_that(p.bursts() + p.cadences() <= p.completed(), "more shapes than holes", step);
        // A hole is finalised at most once and only after it is opened, so at
        // most one can be outstanding at any moment.
        require_that(p.completed() <= p.holes(), "finalised more holes than were opened", step);
        require_that(p.holes() - p.completed() <= 1u, "more than one hole open at once", step);
        // Ordinals are dense and increasing across the retained window, and no
        // record ever holds more samples than it has room for.
        for (std::uint32_t i = p.oldest_retained(); i < p.completed(); ++i) {
            const HoleRecord* r = p.completed_at(i);
            if (!require_that(r != nullptr, "a retained record was null", step)) { break; }
            require_that(r->ordinal == i + 1, "ordinals are not dense", step);
            require_that(r->recovery_n <= kRecoverySamples, "recovery series overran", step);
            require_that(r->core0_idle_pct <= 100, "idle above 100%", step);
        }
        if (p.completed() > 0) {
            const HoleRecord* newest = p.completed_at(p.completed() - 1);
            if (require_that(newest != nullptr, "the newest record was null", step)) {
                require_that(newest->ordinal >= prev_ordinal, "ordinals went backwards", step);
                prev_ordinal = newest->ordinal;
            }
        }
    }
    CHECK_MESSAGE(broke_at < 0, broke << " at step " << broke_at);
    // The walk has to have actually exercised the thing: a property test that
    // never produced a hole would pass vacuously.
    CHECK(p.holes() > 100);
    CHECK(p.unknown() > 0);  // the idle_valid=false path was reached too
}

// --- rendering ---------------------------------------------------------------

TEST_CASE("a rendered hole carries every number its verdict was derived from") {
    StallProbe p = probe_with_baseline(90);
    p.note_event(1400000, 120000, 1380000, true, 1000, -70, false);
    recover_with(p, {2000, 3000, 4000, 80000});

    const HoleRecord* r = p.completed_at(0);
    REQUIRE(r != nullptr);
    char buf[208];
    const std::size_t n = StallProbe::render_hole(*r, buf, sizeof buf);
    const std::string s(buf);
    CHECK(s.size() == n);
    CHECK(s.find("#1 1400 ms") != std::string::npos);
    CHECK(s.find("c0=8%/90%") != std::string::npos);   // the hole and its baseline
    CHECK(s.find("c1=98%") != std::string::npos);
    CHECK(s.find("rssi=-70") != std::string::npos);
    CHECK(s.find("recov 2,3,4,80 ms") != std::string::npos);
    CHECK(s.find("BOARD-BOUND") != std::string::npos);
    CHECK(s.find("burst") != std::string::npos);
}

TEST_CASE("an unmeasured idle renders as unknown rather than as zero per cent") {
    StallProbe p;
    p.note_event(1400000, 0, 0, false, 1000, -70, false);
    recover(p, 2000);
    const HoleRecord* r = p.completed_at(0);
    REQUIRE(r != nullptr);
    char buf[208];
    StallProbe::render_hole(*r, buf, sizeof buf);
    const std::string s(buf);
    CHECK(s.find("c0=?/?") != std::string::npos);
    CHECK(s.find("0%") == std::string::npos);
    CHECK(s.find("unknown") != std::string::npos);
}

TEST_CASE("rendering truncates rather than overruns") {
    // Same property the histogram render was pinned to, and pinned the same way:
    // a guard region either side, checked byte for byte. The earlier version of
    // this test asserted the last byte was untouched, which snprintf never
    // promised — it is entitled to put the NUL there.
    StallProbe p = probe_with_baseline(90);
    p.note_event(1400000, 120000, 1380000, true, 1000, -70, false);
    recover(p, 2000);
    const HoleRecord* r = p.completed_at(0);
    REQUIRE(r != nullptr);

    for (std::size_t cap = 1; cap < 64; ++cap) {
        char guarded[128];
        std::memset(guarded, '#', sizeof guarded);
        const std::size_t n = StallProbe::render_hole(*r, guarded, cap);
        CHECK(n < cap);
        CHECK(std::strlen(guarded) < cap);
        for (std::size_t i = cap; i < sizeof guarded; ++i) {
            CHECK(guarded[i] == '#');
        }
    }
}

TEST_CASE("the tally names every bucket it counts") {
    StallProbe p = probe_with_baseline(90);
    p.note_event(1400000, 100000, 1390000, true, 1000, -70, false);
    recover(p, 2000);
    p.note_event(1200000, 1190000, 1190000, true, 2000, -70, false);
    for (std::size_t i = 0; i < kRecoverySamples; ++i) { p.note_message(80000); }

    char buf[160];
    p.render_tally(buf, sizeof buf);
    const std::string s(buf);
    CHECK(s.find("n=2") != std::string::npos);
    CHECK(s.find("board=1") != std::string::npos);
    CHECK(s.find("link=1") != std::string::npos);
    CHECK(s.find("burst=1") != std::string::npos);
    CHECK(s.find("cadence=1") != std::string::npos);
}

// --- rssi --------------------------------------------------------------------

TEST_CASE("rssi tracks its own extremes and ignores the not-associated answer") {
    LinkQuality link;
    CHECK(link.samples() == 0);
    link.note(0);  // the driver's "no association" answer, not 0 dBm of signal
    CHECK(link.samples() == 0);

    link.note(-69);
    CHECK(link.last_dbm() == -69);
    CHECK(link.min_dbm() == -69);
    CHECK(link.max_dbm() == -69);

    link.note(-78);
    link.note(-64);
    CHECK(link.last_dbm() == -64);
    CHECK(link.min_dbm() == -78);
    CHECK(link.max_dbm() == -64);
    CHECK(link.samples() == 3);
}
