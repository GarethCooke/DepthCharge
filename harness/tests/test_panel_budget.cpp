// test_panel_budget.cpp — the HUB75 allocation model, checked against the board.
//
// This file exists because the first version of the model was wrong by 44% and
// shipped, and the only reason it did no harm is that a conservative reserve
// happened to be bigger than the error. The 2026-08-10 bench printed three
// numbers side by side and they did not agree:
//
//     predicted=65536  measured=94468   (library's own figure: 65536)
//
// So the assertions here are not "the arithmetic is self-consistent". They are
// the numbers the board actually produced, pinned, so that a version bump or an
// edit to the model has to disagree with hardware rather than with an opinion.
#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>

#include "panel_budget.hpp"

using depthcharge::fw::panel_bookkeeping_bytes;
using depthcharge::fw::panel_descriptor_bytes;
using depthcharge::fw::panel_descriptors_per_row;
using depthcharge::fw::panel_framebuffer_bytes;
using depthcharge::fw::panel_refresh_hz;
using depthcharge::fw::panel_total_bytes;
using depthcharge::fw::panel_transition_bit;

namespace {
constexpr int kW = 64;
constexpr int kH = 64;
}  // namespace

TEST_CASE("the model reproduces the 2026-08-10 bench boot exactly") {
    // The board, at depth 8 double buffered on a 64x64 panel:
    //
    //   I2S-DMA: Allocating 65536 bytes memory for DMA BCM framebuffer(s).
    //   I2S-DMA: lsbMsbTransitionBit of 2 gives 100 Hz refresh rate.
    //   panel-hw: UP: ... refresh=100 Hz | predicted=65536 measured=94468B
    CHECK(panel_framebuffer_bytes(kW, kH, 8, true) == 65536u);
    CHECK(panel_transition_bit(kW, kH, 8) == 2);
    CHECK(panel_refresh_hz(kW, kH, 8, 2) == 100);
    CHECK(panel_descriptor_bytes(kW, kH, 8, true) == 24576u);

    // 65,536 + 24,576 + 4,352 = 94,464 against 94,468 measured. Four bytes
    // under, which is the fitted bookkeeping term rounding, and it is under in
    // the direction that matters: the model must not promise a fit it cannot
    // deliver, so the tolerance is asserted rather than left to be noticed.
    const std::uint32_t modelled = panel_total_bytes(kW, kH, 8, true);
    CHECK(modelled == 94464u);
    CHECK(modelled >= 94468u - 64u);
    CHECK(modelled <= 94468u + 512u);
}

TEST_CASE("the library's own refresh-rate ladder is reproduced step for step") {
    // The board printed all three rungs at depth 8, which is the whole search:
    //   lsbMsbTransitionBit of 0 gives 28 Hz
    //   lsbMsbTransitionBit of 1 gives 55 Hz
    //   lsbMsbTransitionBit of 2 gives 100 Hz   <- first >= 60, so it stops
    CHECK(panel_refresh_hz(kW, kH, 8, 0) == 28);
    CHECK(panel_refresh_hz(kW, kH, 8, 1) == 55);
    CHECK(panel_refresh_hz(kW, kH, 8, 2) == 100);
    CHECK(panel_transition_bit(kW, kH, 8) == 2);
}

TEST_CASE("depth 6 is the better rung, and this is why") {
    // The finding that motivated dropping the ceiling. At depth 8 the library
    // cannot hit 60 Hz without collapsing the bottom THREE bits onto one timing
    // slot (transition bit 2, and its own log warns "Percieved colour depth to
    // the eye may be reduced"). At depth 6 it clears 60 Hz with transition bit
    // 0 -- every bit correctly weighted -- at a HIGHER refresh rate.
    //
    // So depth 8 costs 16 KiB more for colour the panel was never showing.
    CHECK(panel_transition_bit(kW, kH, 6) == 0);
    CHECK(panel_refresh_hz(kW, kH, 6, 0) >= 100);
    CHECK(panel_refresh_hz(kW, kH, 6, 0) >= panel_refresh_hz(kW, kH, 8, 2));

    CHECK(panel_framebuffer_bytes(kW, kH, 6, true) == 49152u);
    CHECK(panel_framebuffer_bytes(kW, kH, 8, true) -
              panel_framebuffer_bytes(kW, kH, 6, true) == 16384u);

    // And the descriptors do NOT get cheaper, which is the counter-intuitive
    // part and the reason the model has to compute them rather than scale them:
    // both depths land on 32 descriptors a row by different routes.
    CHECK(panel_descriptors_per_row(kW, 8, 2) == 32);
    CHECK(panel_descriptors_per_row(kW, 6, 0) == 32);
    CHECK(panel_descriptor_bytes(kW, kH, 6, true) ==
          panel_descriptor_bytes(kW, kH, 8, true));

    // Net saving is therefore exactly the framebuffer difference.
    CHECK(panel_total_bytes(kW, kH, 8, true) - panel_total_bytes(kW, kH, 6, true) == 16384u);
}

TEST_CASE("the model is monotonic, and single buffering is exactly half the storage") {
    std::uint32_t previous = 0;
    for (int depth = 2; depth <= 12; ++depth) {
        const std::uint32_t total = panel_total_bytes(kW, kH, depth, true);
        CHECK(total > previous);
        previous = total;

        // Every term of the single-buffered cost is half of the double-buffered
        // one: one framebuffer, one descriptor chain, one set of row structs.
        CHECK(panel_framebuffer_bytes(kW, kH, depth, false) * 2u ==
              panel_framebuffer_bytes(kW, kH, depth, true));
        CHECK(panel_descriptor_bytes(kW, kH, depth, false) * 2u ==
              panel_descriptor_bytes(kW, kH, depth, true));
        CHECK(panel_bookkeeping_bytes(kH, false) * 2u == panel_bookkeeping_bytes(kH, true));

        // The refresh rate the library will settle on always clears its own
        // minimum, or is pinned at the deepest transition bit it can reach.
        const int t = panel_transition_bit(kW, kH, depth);
        CHECK((panel_refresh_hz(kW, kH, depth, t) >= 60 || t == depth - 1));
    }
}

TEST_CASE("the deepest rung does not overflow the arithmetic") {
    // Depth 12 with transition bit 0 is 2,047 latch periods a row and half a
    // billion nanoseconds a frame, which is where an int32 model would start
    // reporting a plausible wrong answer rather than failing.
    CHECK(panel_refresh_hz(kW, kH, 12, 0) > 0);
    CHECK(panel_refresh_hz(kW, kH, 12, 0) < 10);
    CHECK(panel_transition_bit(kW, kH, 12) > 0);
    CHECK(panel_total_bytes(kW, kH, 12, true) > panel_total_bytes(kW, kH, 8, true));
}

TEST_CASE("the S3 buckets the requested clock, so nominal and actual are different numbers") {
    using depthcharge::fw::kI2sClockHz;
    using depthcharge::fw::kS3LcdDivNum;
    using depthcharge::fw::panel_actual_clock_hz;
    using depthcharge::fw::panel_actual_refresh_hz;

    // The discovery this whole distinction exists for. gdma_lcd_parallel16.cpp
    // buckets bus_freq instead of using it:
    //
    //     auto _div_num = 16;                            // 160/16 = 10 MHz
    //     if (freq <= 10000000L)      { }
    //     else if (freq < 20000000L)  { _div_num = 10; }
    //     else                        { _div_num = 7;  }
    //
    // So HZ_8M and 4 MHz produce the SAME hardware clock, and the library's
    // refresh figure -- computed from the nominal 8 MHz -- has always been low.
    // The board's own `Clock divider is 16` line is the corroboration.
    const auto bucket = [](std::int64_t requested) {
        if (requested <= 10000000L) { return 16; }
        if (requested < 20000000L) { return 10; }
        return 7;
    };
    CHECK(bucket(4000000) == 16);
    CHECK(bucket(kI2sClockHz) == 16);      // asking for 8 MHz gets 10 MHz
    CHECK(bucket(16000000) == 10);
    CHECK(bucket(20000000) == 7);

    // Which is why lowering the clock needs S3_LCD_DIV_NUM and not i2sspeed.
    CHECK(panel_actual_clock_hz() == 160000000 / kS3LcdDivNum);
    CHECK(panel_actual_clock_hz() > 0);

    // The actual refresh scales with the actual clock, and has to clear two
    // separate bars: visible flicker, and DESIGN strain 15's margin -- one full
    // panel scan must fit inside the render task's 33 ms frame period, or the
    // S3's non-blocking buffer flip stops being safe by cadence.
    const int actual = panel_actual_refresh_hz(kW, kH, 6);
    CHECK(actual >= 60);                 // flicker
    CHECK(actual >= 1000 / 33);          // strain 15: a scan inside a render period

    // The host build does not carry the firmware's S3_LCD_DIV_NUM, so the
    // constant above only ever exercises the default. The dividers that matter
    // are therefore checked as arithmetic, and this is where the ghosting
    // experiment's cost is written down rather than estimated in a commit
    // message. Nominal refresh at depth 6 is 105 Hz; actual scales by
    // (160/div) / 8 MHz.
    const auto refresh_at = [](int div) {
        return static_cast<int>((105LL * (160000000LL / div)) / kI2sClockHz);
    };
    CHECK(refresh_at(16) == 131);   // today's default: what the panel has always run at
    CHECK(refresh_at(24) == 87);    // the experiment: a third off the edge rate
    CHECK(refresh_at(32) == 65);    // the floor worth trying if 24 is not enough

    // Every divider this firmware would consider must still clear both bars, so
    // a future session cannot slow the clock into flicker or past strain 15
    // without a test saying so.
    for (int div : {16, 20, 24, 32}) {
        CHECK(refresh_at(div) >= 60);
        CHECK(refresh_at(div) >= 1000 / 33);
    }
    // ...and 40 (4 MHz, ~52 Hz) is where it stops being safe, which is the point
    // of knowing the number.
    CHECK(refresh_at(40) < 60);
}
