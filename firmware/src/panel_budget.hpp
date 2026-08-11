// firmware/src/panel_budget.hpp — what the HUB75 driver will really cost, before it takes it.
//
// The colour depth has to be chosen BEFORE begin(), because a failed begin()
// leaks (panel.cpp says why). That makes this arithmetic load-bearing: get it
// wrong and the ladder picks a rung that does not fit, the allocation fails
// halfway, and most of a framebuffer is gone for the rest of the boot.
//
// THE FIRST VERSION OF THIS WAS WRONG BY 44%, AND THE BOARD CAUGHT IT.
// It counted the framebuffer and nothing else:
//
//   2026-08-10 bench:  predicted=65536  measured=94468
//
// The 28,932 byte difference is not slack, it is two things the framebuffer
// figure does not include and the allocator does:
//
//   DMA descriptors   24,576 B   1,024 per buffer x 2 buffers x 12 B
//   bookkeeping        ~4,356 B   64 shared_ptr row structs, two vectors,
//                                 and the heap's own per-block headers
//
// It only worked because kReserveInternalBytes happened to be larger than the
// error. That is not a safety margin, it is a coincidence, so the descriptor
// count is now modelled here instead of hoped for.
//
// ESP-IDF-FREE, and it is the point: this is the library's own integer
// arithmetic, replayed. harness/tests/test_panel_budget.cpp pins it against the
// numbers the board actually printed, so the model and the hardware are checked
// against each other on the desk rather than at the bench.
#pragma once

#include <cstdint>

namespace depthcharge::fw {

// The library's constants, from the vendored 3.0.15 sources. Named here so the
// arithmetic below reads as the library's rather than as ours, and so a version
// bump has one place to disagree with.
//
//   MATRIX_ROWS_IN_PARALLEL   ESP32-HUB75-MatrixPanel-I2S-DMA.h:67
//   DMA_MAX                   platforms/esp32s3/gdma_lcd_parallel16.hpp:84
//   HUB75_DMA_DESCRIPTOR_T    ...:87  -> dma_descriptor_t, 12 bytes on the S3
//   ESP32_I2S_DMA_STORAGE_TYPE  ...-I2S-DMA.h:87 -> uint16_t
inline constexpr int kRowsInParallel = 2;
inline constexpr int kDmaMaxPayload = 4096 - 4;
inline constexpr int kDmaDescriptorBytes = 12;
inline constexpr int kDmaStorageBytes = 2;

// The NOMINAL clock: what HUB75_I2S_CFG::i2sspeed is set to, and what the
// library's own refresh-rate loop computes from. Everything below that predicts
// the transition bit and the descriptor count uses this, correctly — because the
// library uses it too, wrong or not.
inline constexpr std::int64_t kI2sClockHz = 8000000;   // HZ_8M
inline constexpr int kMinRefreshHz = 60;

// THE ACTUAL PIXEL CLOCK IS NOT THE NOMINAL ONE, AND ON THE S3 IT NEVER WAS.
//
// `i2sspeed` reaches the S3 backend as `bus_freq` and is then BUCKETED, not used
// (gdma_lcd_parallel16.cpp:140-147):
//
//     auto _div_num = 16;                            // 160/16 = 10 MHz
//     if (freq <= 10000000L)      { }                // <= 10 MHz all land here
//     else if (freq < 20000000L)  { _div_num = 10; } // 16 MHz
//     else                        { _div_num = 7;  } // 22 MHz
//
// So asking for HZ_8M gives 10 MHz, and asking for 4 MHz would ALSO give 10 MHz.
// The board confirms it in its own log — `Clock divider is 16` — while the
// library reports a refresh rate computed from 8 MHz. Every `refresh=105 Hz` this
// firmware has printed is therefore nominal and about 25% low; the real figure
// has been ~131 Hz.
//
// The only real knob is S3_LCD_DIV_NUM, a compile-time override the library
// checks right after the bucket (:149). platformio.ini sets it and explains why.
inline constexpr std::int64_t kS3LcdSourceHz = 160000000;

#ifdef S3_LCD_DIV_NUM
inline constexpr int kS3LcdDivNum = S3_LCD_DIV_NUM;
#else
inline constexpr int kS3LcdDivNum = 16;  // the library's own default bucket
#endif

static_assert(kS3LcdDivNum >= 2 && kS3LcdDivNum <= 255,
              "LCD_CAM's clkm_div_num is an 8-bit field and 1 disables the divider");

// What the panel is really clocked at, and therefore what the real refresh rate
// and the real edge rate are. Reported beside the nominal figure at boot so the
// two can never be confused again.
constexpr std::int64_t panel_actual_clock_hz() noexcept {
    return kS3LcdSourceHz / kS3LcdDivNum;
}

constexpr int ceil_div(int a, int b) noexcept { return (a + b - 1) / b; }

// The panel's scan rate at a given colour depth and transition bit, in Hz.
// Replays setupDMA()'s loop (ESP32-HUB75-MatrixPanel-I2S-DMA.cpp:149-176) in
// 64-bit, because at depth 12 with transition bit 0 the nanosecond count for one
// frame passes 500 million and the library's own `int` is closer to the edge
// than anybody looking at it would guess.
constexpr int panel_refresh_hz(int width, int height, int depth, int transition_bit) noexcept {
    const std::int64_t ps_per_clock = 1000000000000LL / kI2sClockHz;
    const std::int64_t ns_per_latch = (static_cast<std::int64_t>(width) * ps_per_clock) / 1000;

    std::int64_t ns_per_row = static_cast<std::int64_t>(depth) * ns_per_latch;
    for (int i = transition_bit + 1; i < depth; ++i) {
        ns_per_row += (1LL << (i - transition_bit - 1)) * ns_per_latch;
    }

    const std::int64_t ns_per_frame = ns_per_row * (height / kRowsInParallel);
    if (ns_per_frame <= 0) { return 0; }
    return static_cast<int>(1000000000LL / ns_per_frame);
}

// Which bit the library ends up collapsing the low colour bits onto in order to
// clear kMinRefreshHz. Same loop, same exit conditions.
//
// This is the value that decides the descriptor count, and it is why the
// descriptor cost is NOT proportional to depth: at depth 8 the library settles on
// transition bit 2 and at depth 6 on bit 0, and both produce exactly 32
// descriptors a row.
constexpr int panel_transition_bit(int width, int height, int depth) noexcept {
    int t = 0;
    for (;;) {
        if (panel_refresh_hz(width, height, depth, t) >= kMinRefreshHz) { return t; }
        if (t >= depth - 1) { return t; }
        ++t;
    }
}

// setupDMA()'s descriptor count per row (…-I2S-DMA.cpp:194-218).
constexpr int panel_descriptors_per_row(int width, int depth, int transition_bit) noexcept {
    const int all_depths = ceil_div(width * depth * kDmaStorageBytes, kDmaMaxPayload);
    const int one_depth = ceil_div(width * 1 * kDmaStorageBytes, kDmaMaxPayload);
    int per_row = all_depths;
    for (int i = transition_bit + 1; i < depth; ++i) {
        per_row += (1 << (i - transition_bit - 1)) * one_depth;
    }
    return per_row;
}

// The BCM framebuffer itself: one row buffer per scan row per frame buffer.
constexpr std::uint32_t panel_framebuffer_bytes(int width, int height, int depth,
                                                bool double_buffered) noexcept {
    return static_cast<std::uint32_t>(height / kRowsInParallel) *
           static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(depth) *
           static_cast<std::uint32_t>(kDmaStorageBytes) * (double_buffered ? 2u : 1u);
}

constexpr std::uint32_t panel_descriptor_bytes(int width, int height, int depth,
                                               bool double_buffered) noexcept {
    const int t = panel_transition_bit(width, height, depth);
    const int per_row = panel_descriptors_per_row(width, depth, t);
    const std::uint32_t per_buffer =
        static_cast<std::uint32_t>(per_row) * static_cast<std::uint32_t>(height / kRowsInParallel);
    return per_buffer * static_cast<std::uint32_t>(kDmaDescriptorBytes) * (double_buffered ? 2u : 1u);
}

// The allocator's own tax, and the one term here that is FITTED rather than
// derived: 64 make_shared row structs, two vectors of shared_ptr, and a heap
// header on each. Taken from the single measurement that exposed the original
// error (94,468 - 65,536 - 24,576 = 4,356 at depth 8 double buffered) and
// rounded up to the next 512 B, because being wrong upward here costs a colour
// bit and being wrong downward costs a failed begin().
//
// It scales with the number of row buffers, not with depth: the allocations are
// per row per frame buffer whatever the colour depth is.
constexpr std::uint32_t panel_bookkeeping_bytes(int height, bool double_buffered) noexcept {
    const std::uint32_t rows =
        static_cast<std::uint32_t>(height / kRowsInParallel) * (double_buffered ? 2u : 1u);
    return rows * 68u;  // 4,352 at 64 rows, against 4,356 measured
}

// What begin() will take out of the internal DMA-capable heap, all in.
constexpr std::uint32_t panel_total_bytes(int width, int height, int depth,
                                          bool double_buffered) noexcept {
    return panel_framebuffer_bytes(width, height, depth, double_buffered) +
           panel_descriptor_bytes(width, height, depth, double_buffered) +
           panel_bookkeeping_bytes(height, double_buffered);
}

// The refresh rate the panel ACTUALLY runs at: the library's own figure scaled by
// how far the real clock differs from the nominal one it computed with. This is
// the number that matters for flicker, and for DESIGN strain 15's "safe by
// cadence" margin against the render task's frame period.
constexpr int panel_actual_refresh_hz(int width, int height, int depth) noexcept {
    const int nominal = panel_refresh_hz(width, height, depth,
                                         panel_transition_bit(width, height, depth));
    return static_cast<int>((static_cast<std::int64_t>(nominal) * panel_actual_clock_hz()) /
                            kI2sClockHz);
}

}  // namespace depthcharge::fw
