// firmware/src/core_idle.cpp — see core_idle.hpp.
#include "core_idle.hpp"

#include "esp_cpu.h"
#include "esp_freertos_hooks.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace depthcharge::fw {
namespace {
constexpr const char* kTag = "idle";
}  // namespace

IdleAccumulator CoreIdleProbe::core_[2]{};

bool CoreIdleProbe::idle_hook() noexcept {
    // xPortGetCoreID() is a macro over `rsr.prid`, and esp_cpu_get_ccount() is a
    // static inline `rsr.ccount`. Both are register reads, so this hook is a
    // handful of instructions and — importantly — touches no flash-resident
    // function. It runs in task context (the idle task), not in an ISR, so the
    // cache-disabled window during a flash write is not a hazard either way; it
    // is written this way so that it stays true if it is ever moved.
    core_[xPortGetCoreID() & 1].note_pass(esp_cpu_get_ccount());
    // FALSE, deliberately: it is what makes the idle task call this again
    // immediately rather than sleep, and therefore what makes the interval
    // between two calls a measure of idle time. See the header for the
    // perturbation this buys and why it is acceptable on this object.
    return false;
}

bool CoreIdleProbe::begin(std::uint32_t cpu_mhz) noexcept {
#if DC_IDLE_PROBE
    core_[0].configure(cpu_mhz);
    core_[1].configure(cpu_mhz);

    const esp_err_t e0 = esp_register_freertos_idle_hook_for_cpu(&CoreIdleProbe::idle_hook, 0);
    const esp_err_t e1 = esp_register_freertos_idle_hook_for_cpu(&CoreIdleProbe::idle_hook, 1);
    valid_ = (e0 == ESP_OK) && (e1 == ESP_OK);
    if (!valid_) {
        // Eight slots per core and ESP-IDF uses at most one of them here, so
        // this should not happen — but a probe that silently reported 0% idle
        // would read as "Core 0 starved" on every hole, which is the exact wrong
        // verdict. Better to say so and report unknown.
        //
        // Both hooks come back out on a partial failure, so that "not running"
        // means what it says: half a probe would still suppress waiti on one
        // core while reporting nothing, which is the worst of both.
        esp_deregister_freertos_idle_hook(&CoreIdleProbe::idle_hook);
        ESP_LOGE(kTag, "idle hook registration failed (core0=%s core1=%s) — idle unknown",
                 esp_err_to_name(e0), esp_err_to_name(e1));
        return false;
    }
    ESP_LOGI(kTag, "per-core idle probe up at %u MHz (continuity %u us); waiti suppressed while idle",
             static_cast<unsigned>(cpu_mhz), static_cast<unsigned>(kIdleContinuityUs));
    return true;
#else
    (void)cpu_mhz;
    valid_ = false;
    ESP_LOGW(kTag, "per-core idle probe COMPILED OUT (DC_IDLE_PROBE=0) — idle reported unknown");
    return false;
#endif
}

}  // namespace depthcharge::fw
