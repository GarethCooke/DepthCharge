// firmware/src/core_idle.hpp — how much idle time each core actually had.
//
// The verdict signal for the M3 stall characterisation, and the one part of it
// that cannot be host-tested because it is entirely a claim about this platform.
// The arithmetic it feeds lives in stall_probe.hpp, which has no ESP-IDF in it
// and is where the rules are proven; this file is the sampling.
//
// WHY NOT FreeRTOS RUN-TIME STATS, WHICH IS THE OBVIOUS ANSWER.
//
// `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS is not set` in the precompiled
// Arduino framework — read out of
// `framework-arduinoespressif32/tools/sdk/esp32s3/sdkconfig`, together with
// `CONFIG_FREERTOS_USE_TRACE_FACILITY is not set`. It is a compile-time switch
// that changes the TCB layout and is baked into the shipped `libfreertos.a`, so
// defining it in our own build flags would not enable it; it would make our
// translation units disagree with the archive about the shape of a task. That is
// the FOURTH design this vintage has decided here, after `esp_crt_bundle`,
// `heap_trace_start` and `reconnect_timeout_ms` — the standing lesson being to
// read the shipped config and headers before planning around a feature.
//
// HOW THIS MEASURES IDLE INSTEAD.
//
// ESP-IDF lets a callback be registered on each core's idle task
// (`esp_register_freertos_idle_hook_for_cpu`). The hook's return value decides
// whether that core is then allowed to sleep, and the shipped code is explicit
// about it — disassembling `esp_vApplicationIdleHook` in the qio_opi
// `libesp_system.a` shows the loop over eight hook slots, `moveqz` clearing a
// `can_go_idle` flag for any hook that returns zero, and `esp_pm_impl_waiti`
// called only if the flag survives. `prvIdleTask` in `libfreertos.a` calls that
// hook every pass round its loop.
//
// So a hook that returns **false** is called in a tight loop for as long as its
// core has nothing else to run, and the interval between two consecutive calls
// is idle time. Differencing the core's own cycle counter across those calls
// measures it directly, in microseconds, with no calibration constant and no
// statistical sampling. Intervals longer than `kIdleContinuityUs` are not idle —
// they are a real task having run — and are dropped (stall_probe.hpp).
//
// THE PERTURBATION, STATED RATHER THAN BURIED. Returning false suppresses
// `waiti` on both cores, so an idle core spins instead of sleeping. Three
// reasons that is acceptable here and one guard:
//
//   * `CONFIG_PM_ENABLE is not set`, so there is no frequency scaling or
//     automatic light sleep for this to interfere with; `esp_pm_impl_waiti` is
//     bare `waiti` and buys only power, on a mains-powered desk object.
//   * The idle task runs at priority 0, so the spin cannot delay any real work.
//     Nothing about task scheduling, interrupt priority or Wi-Fi behaviour
//     changes — modem sleep is a radio state and is unaffected.
//   * The task watchdog still sees the idle task; it is fed more often, not
//     less. (`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y` with a 5 s panic
//     timeout is also a free second opinion: a Core 0 that were starved for more
//     than 5 s would have panicked, so the 1-2.5 s holes are not total
//     starvation whatever the idle figure says.)
//
// The guard is `-D DC_IDLE_PROBE=0`, which compiles the registration out and
// leaves every idle figure reported as unknown rather than as zero. If a bench
// result ever looks like it might be the instrument, that switch re-runs the
// same firmware without it.
//
// WHAT THE NUMBER INCLUDES, precisely, because a verdict rests on it: time spent
// in an interrupt handler that fired while the core was idle is counted as idle,
// because the hook cannot see it. A core saturated by ISRs rather than by tasks
// would therefore read as idle. On this firmware the heavy work — TLS record
// decrypt, lwIP, the esp_event dispatch, the parser — is all task context, so
// the reading is sound; it is stated because it is the one way this instrument
// could point at the wrong half of the board.
#pragma once

#include <cstdint>

#include "stall_probe.hpp"

// Default on. See the perturbation note above for what turning it off buys.
#ifndef DC_IDLE_PROBE
#define DC_IDLE_PROBE 1
#endif

namespace depthcharge::fw {

// Both cores' accumulators, plus the registration. One instance, in main.cpp.
class CoreIdleProbe {
public:
    // Registers the idle hook on both cores. `cpu_mhz` comes from the running
    // system (main.cpp reads it) rather than from a constant, because a wrong
    // value scales every idle percentage without changing its shape — the least
    // detectable kind of wrong. Returns false if either registration failed, in
    // which case `valid()` stays false and every reading is reported as unknown
    // rather than as zero.
    bool begin(std::uint32_t cpu_mhz) noexcept;

    bool valid() const noexcept { return valid_; }
    std::uint32_t cpu_mhz() const noexcept { return core_[0].cpu_mhz(); }

    // Free-running per-core idle microseconds; wraps at ~71 minutes, so callers
    // difference with wrapping_delta_u32 and never read it absolute.
    std::uint32_t idle_us(std::uint32_t core) const noexcept {
        return core_[core & 1u].idle_us();
    }

    const IdleAccumulator& accumulator(std::uint32_t core) const noexcept {
        return core_[core & 1u];
    }

private:
    // The registered hook, one function for both cores; it reads the core id
    // itself. Must never block (the ESP-IDF contract for idle callbacks) and
    // must never allocate (invariant #7) — it does neither: one register read,
    // one subtraction, one add.
    static bool idle_hook() noexcept;

    // Static because the hook is a plain C callback with no argument, and there
    // is exactly one probe in the firmware. Written only by each core's own idle
    // task and read by the feed and console tasks — 32-bit, naturally aligned,
    // so a reader gets a stale value rather than a torn one. Same trade as every
    // other counter in this firmware.
    static IdleAccumulator core_[2];

    bool valid_ = false;
};

}  // namespace depthcharge::fw
