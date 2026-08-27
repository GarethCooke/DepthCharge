// firmware/src/heap_probe.cpp — see heap_probe.hpp.
#include "heap_probe.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"  // esp_get_free_heap_size

namespace depthcharge::fw {
namespace {
constexpr const char* kTag = "heap";

// MALLOC_CAP_INTERNAL: on-chip SRAM. The feed path must live here — PSRAM is
// slower, is not DMA-capable for every peripheral, and stage D wants what is
// left of it for the panel.
//
// AMENDED 2026-08-27 (M5 stage D-A1), because the rule above was about to be
// stepped around and a rule stepped around silently is worse than a rule
// changed. It says "the feed path", and all three of its grounds are about the
// STEADY-STATE feed path: latency matters to something read on every diff, the
// DMA capability matters to something a peripheral reaches, and the internal
// SRAM stage D wants for the panel is the SRAM this array would otherwise sit
// in. None of the three reaches a buffer that is written once while a REST
// fetch is in flight and drained once when the body lands.
//
// `BinanceAdapter::buf_lvl_` is that buffer, and it is now a heap block rather
// than 32,768 bytes of `.bss` (see the constructor in
// `engine/include/depthcharge/binance/binance_adapter.hpp` for the argument and
// for why `bids_`, `asks_` and `frame_` do NOT move). The constitution was
// already on side and had been all along: ARCHITECTURE §5 says *"on target the
// window lives in internal SRAM, the tail in PSRAM"*. The window is what the
// rule above protects. `buf_lvl_` is the tail.
//
// So the rule as it now stands: the feed path's STEADY STATE lives in internal
// SRAM, and PSRAM is for what the steady state does not touch. That is a
// narrower claim than the sentence it replaces, and it is the one the code has
// always actually needed.
constexpr std::uint32_t kCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
}  // namespace

HeapSample sample_heap() noexcept {
    HeapSample s;
    s.free_internal = static_cast<std::uint32_t>(heap_caps_get_free_size(kCaps));
    s.low_water_internal = static_cast<std::uint32_t>(heap_caps_get_minimum_free_size(kCaps));
    s.largest_block_internal =
        static_cast<std::uint32_t>(heap_caps_get_largest_free_block(kCaps));
    s.free_total = static_cast<std::uint32_t>(esp_get_free_heap_size());
    return s;
}

void HeapProbe::mark_baseline() noexcept {
    baseline_ = sample_heap();
    armed_ = true;
    // `total` is `free_total`, and it is printed for the first time at M5 stage
    // D-A1. It was always sampled and never shown, which was harmless while
    // nothing in this firmware was deliberately in PSRAM. `buf_lvl_` now is, so
    // this is the only field that can see it: the three `*_internal` figures
    // are masked to on-chip SRAM and a PSRAM block is invisible to all of them.
    // One format specifier, not a new measurement — see `heap_probe.hpp` for
    // why `esp_get_free_heap_size()` is the PSRAM-inclusive mask on this build.
    ESP_LOGI(kTag,
             "baseline at steady state: free=%u low=%u largest=%u (internal, bytes)"
             " | total=%u (PSRAM-inclusive)",
             static_cast<unsigned>(baseline_.free_internal),
             static_cast<unsigned>(baseline_.low_water_internal),
             static_cast<unsigned>(baseline_.largest_block_internal),
             static_cast<unsigned>(baseline_.free_total));
    // Deliberately not "invariant #7 holds if...": the transport allocates once
    // per event by design (see the header), so what these numbers can show is
    // that the churn is balanced, not that there is none.
    ESP_LOGI(kTag, "watching for a LEAK (free_delta != 0) or FRAGMENTATION (largest falling);"
                   " balanced per-event churn is expected and invisible here");
}

void HeapProbe::report(const char* tag, std::uint32_t frames_since_baseline) noexcept {
    if (!armed_) { return; }
    const HeapSample now = sample_heap();

    // Signed, because a negative delta (the heap grew) is as interesting as a
    // positive one and printing it unsigned would hide a leak as a huge number.
    const std::int32_t free_delta =
        static_cast<std::int32_t>(now.free_internal) -
        static_cast<std::int32_t>(baseline_.free_internal);
    const std::int32_t block_delta =
        static_cast<std::int32_t>(now.largest_block_internal) -
        static_cast<std::int32_t>(baseline_.largest_block_internal);
    // How far below the baseline the heap ever went since boot. Churn shows here
    // and nowhere else, which is the whole reason this line exists.
    const std::int32_t low_below_baseline =
        static_cast<std::int32_t>(baseline_.free_internal) -
        static_cast<std::int32_t>(now.low_water_internal);

    // Signed for the same reason as `free_delta`: a PSRAM leak would show here
    // and nowhere else, and unsigned would print it as a very large number.
    const std::int32_t total_delta =
        static_cast<std::int32_t>(now.free_total) -
        static_cast<std::int32_t>(baseline_.free_total);

    ESP_LOGI(kTag,
             "%s after %u frames: free=%u (%+d) largest=%u (%+d) low=%u (%d below baseline)"
             " total=%u (%+d)",
             tag, static_cast<unsigned>(frames_since_baseline),
             static_cast<unsigned>(now.free_internal), static_cast<int>(free_delta),
             static_cast<unsigned>(now.largest_block_internal), static_cast<int>(block_delta),
             static_cast<unsigned>(now.low_water_internal),
             static_cast<int>(low_below_baseline),
             static_cast<unsigned>(now.free_total), static_cast<int>(total_delta));
}

}  // namespace depthcharge::fw
