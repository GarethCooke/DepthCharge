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
    ESP_LOGI(kTag,
             "baseline at steady state: free=%u low=%u largest=%u (internal, bytes)",
             static_cast<unsigned>(baseline_.free_internal),
             static_cast<unsigned>(baseline_.low_water_internal),
             static_cast<unsigned>(baseline_.largest_block_internal));
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

    ESP_LOGI(kTag,
             "%s after %u frames: free=%u (%+d) largest=%u (%+d) low=%u (%d below baseline)",
             tag, static_cast<unsigned>(frames_since_baseline),
             static_cast<unsigned>(now.free_internal), static_cast<int>(free_delta),
             static_cast<unsigned>(now.largest_block_internal), static_cast<int>(block_delta),
             static_cast<unsigned>(now.low_water_internal),
             static_cast<int>(low_below_baseline));
}

}  // namespace depthcharge::fw
