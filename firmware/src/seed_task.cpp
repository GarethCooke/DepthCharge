// firmware/src/seed_task.cpp — see seed_task.hpp for the ownership rule and for
// why a third task exists at all.
#include "seed_task.hpp"

#if DC_VENUE == DC_VENUE_BINANCE

#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/inet.h"

namespace depthcharge::fw {
namespace {
constexpr const char* kTag = "seed";
}  // namespace

const char* seed_slot_name(SeedTask::Slot s) noexcept {
    switch (s) {
        case SeedTask::Slot::Idle:      return "idle";
        case SeedTask::Slot::Requested: return "requested";
        case SeedTask::Slot::Fetching:  return "fetching";
        case SeedTask::Slot::Ready:     return "ready";
        case SeedTask::Slot::Failed:    return "failed";
    }
    return "?";
}

bool SeedTask::begin() noexcept { return fetch_.begin(); }

bool SeedTask::start(std::uint32_t stack_bytes, UBaseType_t priority) noexcept {
    // Core 0, with the feed and the RX task. Not core 1: that is the render
    // side's, and a 64 KB TLS decrypt landing on it would show up as dropped
    // panel frames — the one cost this whole arrangement exists to avoid
    // moving around rather than removing.
    const BaseType_t ok = xTaskCreatePinnedToCore(&SeedTask::trampoline, "dc_seed",
                                                  stack_bytes, this, priority, &task_, 0);
    if (ok != pdPASS) {
        ESP_LOGE(kTag, "could not start the seed task — the ladder will stay grey");
        return false;
    }
    ESP_LOGI(kTag, "seed task up on core 0, prio %u (below the feed's), %u B stack",
             static_cast<unsigned>(priority), static_cast<unsigned>(stack_bytes));
    return true;
}

bool SeedTask::request(std::uint32_t ipv4, const char* path) noexcept {
    if (ipv4 == 0 || path == nullptr) { return false; }
    // Only from `Idle`, so a request can never overwrite a buffer this task is
    // reading or the feed task is consuming.
    Slot expected = Slot::Idle;
    if (!slot_.compare_exchange_strong(expected, Slot::Requested, std::memory_order_acq_rel)) {
        return false;
    }
    in_addr a{};
    a.s_addr = ipv4;
    addr_[0] = '\0';
    (void)::inet_ntoa_r(a, addr_, sizeof(addr_));
    path_ = path;
    abandon_.store(false, std::memory_order_release);
    // Published by the `Requested` store above; the notify only wakes the task.
    if (task_ != nullptr) { xTaskNotifyGive(task_); }
    return true;
}

void SeedTask::release() noexcept {
    const Slot s = slot();
    if (s != Slot::Ready && s != Slot::Failed) { return; }
    // Logged HERE and not on the seed task, so the one line a bench greps is
    // emitted by whoever consumed the result rather than by whoever produced
    // it — and so a `Ready` that the feed never consumed would be visible as a
    // missing line rather than as a line that lied.
    fetch_.release();
    slot_.store(Slot::Idle, std::memory_order_release);
}

void SeedTask::trampoline(void* self) noexcept {
    static_cast<SeedTask*>(self)->run();
}

void SeedTask::run() noexcept {
    for (;;) {
        // Sleeps until the feed task asks. No polling: a fetch happens at most
        // once per retry cycle, and a task that woke 50 times a second to find
        // nothing to do would be 50 wake-ups a second of nothing.
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        Slot expected = Slot::Requested;
        if (!slot_.compare_exchange_strong(expected, Slot::Fetching,
                                           std::memory_order_acq_rel)) {
            continue;   // a spurious wake, or the request was withdrawn
        }

        const std::int64_t t0 = esp_timer_get_time();
        bool ok = false;
        if (fetch_.start(addr_, path_, t0)) {
            // The phase machine, driven to completion. It is the same machine
            // the feed task used to step; the only difference is that here each
            // phase may block, which is what a task of its own is for.
            //
            // THE ABANDON IS CHECKED BETWEEN PHASES, never inside one. There is
            // no way to interrupt a blocking mbedTLS call without reaching into
            // esp-tls's private socket, so the honest bound is one
            // `kFetchCallTimeoutMs` — stated in the header rather than implied.
            for (;;) {
                if (abandon_.load(std::memory_order_acquire)) {
                    fetch_.abandon(FetchError::Abandoned, esp_timer_get_time());
                    ++abandons_;
                    break;
                }
                const FetchPhase p = fetch_.step(esp_timer_get_time());
                if (p == FetchPhase::Complete) { ok = true; break; }
                if (p == FetchPhase::Failed) { break; }
            }
        }

        // THE TRANSFER. Everything written into the body buffer above happens
        // before this store; the feed task's acquire on `slot()` is what makes
        // it visible. After this line the buffer belongs to the feed task and
        // this one must not touch it again until `release()`.
        slot_.store(ok ? Slot::Ready : Slot::Failed, std::memory_order_release);
    }
}

}  // namespace depthcharge::fw

#endif  // DC_VENUE == DC_VENUE_BINANCE
