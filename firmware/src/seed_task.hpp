// firmware/src/seed_task.hpp — the third task, and the third cross-task hand-off.
//
// M5 stage D-A2. `frame_pipe.hpp` opens by saying there are *"exactly two
// cross-task hand-offs in this firmware, and they are deliberately the only
// two"*. This is a third, so it needs more than a design — it needs the
// argument for why the sentence changes, and that argument is in
// ARCHITECTURE §9 (2026-08-28). What follows is the mechanism.
//
// ===========================================================================
// WHY A THIRD TASK, WHEN THREE CHEAPER PLACES WERE TRIED FIRST
// ===========================================================================
//
// The seed is an HTTPS GET that must not run on any task that already exists.
//
//   * **The feed task** — measured, not argued. A blocking fetch stalls it for
//     **1.93 s** (board, `worst step 1933858 us`). At ~10 messages/second into
//     four `FramePipe` slots that drops ~15 messages, and at a DIFF venue a
//     dropped message is not a skipped refresh: it fails the next
//     `U == last_u + 1`, drops the book and asks for another seed. **The board
//     showed exactly that** — `bracket ok=0 FAIL=1 dropped=116`. The fetch
//     destroyed the thing it was fetching.
//   * **The RX task** — blocking it stops reading the socket, which is the same
//     failure with the pipe filled from the other end.
//   * **loopTask** — its own comment records that the ~4 s blocking connect was
//     deliberately moved OFF it to the RX task "where it belongs"; putting a
//     15 s fetch back would also make the supervisor that late to notice a dead
//     socket.
//
// And the non-blocking design that would have avoided all three **does not work
// on this build**: `esp_tls_conn_new_async` was polled 1,780 times over 15 s,
// four runs in a row, and never left `conn_state=1` (CONNECTING) with
// `errno=119` (EINPROGRESS). `rest_fetch.cpp` records the mechanism and how far
// it was corroborated. The synchronous call succeeds first time.
//
// So the fetch must block, and nothing that already exists may block. A third
// task is what is left.
//
// ===========================================================================
// OWNERSHIP, WHICH IS THE ONLY PART THAT CAN BE WRONG QUIETLY
// ===========================================================================
//
// The rule is `FramePipe`'s, because it is the rule that already works here:
// **one owner at a time, and the state transition IS the transfer.** The 96 KiB
// body buffer is written only by this task while `Fetching`, and read only by
// the feed task while `Ready`. Nothing is shared while it is being written, so
// there is no lock and nothing for either side to wait on.
//
//     Idle --request()--> Requested --[seed task]--> Fetching
//       ^                                               |
//       |                                               v
//       +----release()---- Ready / Failed <-------------+
//                (feed task)      (seed task publishes)
//
// **The adapter is never touched here** (invariant #8). This task produces
// bytes; only the feed task turns them into book events, by calling
// `on_rest_body` itself once it sees `Ready`. That is the whole reason the
// hand-off is a buffer and not an adapter call.
//
// ===========================================================================
// A SEED FETCH AND A TLS RECONNECT NEVER OVERLAP
// ===========================================================================
//
// Enforced in `seed_schedule.hpp`, which is host-tested, and honoured here.
// Two independent grounds, either sufficient:
//
//   * **Memory.** One fetch took the largest free internal block to **10,740 B**
//     (board), already under the **16,717 B** a single mbedTLS session needs.
//     A reconnect building a third session against that cannot fit — and the
//     failure would land on the reconnect, which is the half that matters.
//   * **Correctness.** A body is bracketed against the diff stream. If the
//     socket has dropped, that stream has ended and the body is worthless
//     before it arrives.
//
// So: no fetch is started while the socket is down, and one in flight is
// abandoned the instant it drops. **Abandon does not interrupt a syscall** — it
// sets a flag this task checks BETWEEN esp-tls calls, so the worst case is one
// `kFetchCallTimeoutMs`, and the result is discarded whenever it lands.
#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rest_fetch.hpp"
#include "venue_build.hpp"

#if DC_VENUE == DC_VENUE_BINANCE

namespace depthcharge::fw {

// 6,144 B, the same as the RX task's, and for the same reason: it has to hold
// an mbedTLS handshake. The two autopsies on record for that task show
// `stack_free = 1932 B` at that size, so this is a proven figure rather than a
// guessed one — and this task does strictly less besides.
inline constexpr std::uint32_t kSeedTaskStack = 6144;

// PRIORITY 4: BELOW THE FEED TASK'S 5, AND THAT IS THE SAFETY PROPERTY.
//
// The whole point of moving the fetch off the feed task is that the feed must
// never wait for it. A lower priority makes that structural rather than
// hopeful: the moment a message arrives the feed preempts this task, wherever
// it is. It is above the render task's 3, but that runs on the other core.
inline constexpr UBaseType_t kSeedTaskPriority = 4;

// How long a finished body may sit unconsumed before the feed task looks. NOT a
// step interval — nothing is stepped here any more — so it is allowed to be
// coarse: 50 ms against fetch round trips measured at 4.1-6.3 s. It exists at
// all only because at Binance the liveness watchdog is never armed, so a quiet
// stream would otherwise let the feed task sleep on `portMAX_DELAY` straight
// through its own result.
inline constexpr std::int64_t kSeedResultPollUs = 50'000;

class SeedTask {
public:
    enum class Slot : std::uint8_t {
        Idle = 0,   // nothing in flight; the feed may request
        Requested,  // the feed has asked; this task owns the buffer next
        Fetching,   // this task owns the buffer
        Ready,      // the feed owns the buffer; body() is valid
        Failed,     // the fetch ended without a body; report() says why
    };

    bool begin() noexcept;                                   // the 96 KiB buffer
    bool start(std::uint32_t stack_bytes = kSeedTaskStack,
               UBaseType_t priority = kSeedTaskPriority) noexcept;

    // ---- called from the FEED task only ------------------------------------

    // Ask for a fetch. `ipv4` is the resolved address in network byte order —
    // never a hostname, because `getaddrinfo` inside the connect blocks and DNS
    // on this board measured 14,000 ms. Returns false if a fetch is already in
    // flight or the address is not resolved yet.
    bool request(std::uint32_t ipv4, const char* path) noexcept;

    Slot slot() const noexcept { return slot_.load(std::memory_order_acquire); }
    bool busy() const noexcept {
        const Slot s = slot();
        return s == Slot::Requested || s == Slot::Fetching;
    }
    bool ready() const noexcept { return slot() == Slot::Ready; }
    bool failed() const noexcept { return slot() == Slot::Failed; }

    // Valid only while `Ready`, and only on the feed task.
    std::string_view body() const noexcept { return fetch_.body(); }

    // Hand the buffer back. `Ready`/`Failed` -> `Idle`.
    void release() noexcept;

    // Condemn whatever is in flight. Honoured between esp-tls calls, so the
    // worst-case latency is one `kFetchCallTimeoutMs`; the result is discarded
    // whenever it lands.
    void abandon() noexcept { abandon_.store(true, std::memory_order_release); }

    const RestFetchReport& report() const noexcept { return fetch_.report(); }
    std::uint32_t fetches() const noexcept { return fetch_.fetches(); }
    std::uint32_t failures() const noexcept { return fetch_.failures(); }
    std::uint32_t abandons() const noexcept { return abandons_; }
    FetchError error() const noexcept { return fetch_.error(); }

private:
    static void trampoline(void* self) noexcept;
    void run() noexcept;

    RestFetch fetch_;
    TaskHandle_t task_ = nullptr;

    // THE THREE WORDS THAT CROSS TASKS HERE, AND THE ONLY THREE.
    //
    // `slot_` is the ownership transfer and everything else follows from it.
    // `abandon_` is the feed task condemning a fetch. Both are branched on
    // rather than merely reported, which is the bar this firmware already uses
    // to decide what is atomic (`ws_transport.hpp`).
    //
    // `addr_` and `path_` are written by the feed task ONLY while `Idle` and
    // read by this task ONLY while `Fetching`, so the `slot_` release/acquire
    // pair is what publishes them — the same argument `FramePipe` makes for a
    // slot's bytes.
    std::atomic<Slot> slot_{Slot::Idle};
    std::atomic<bool> abandon_{false};
    char addr_[16] = {};
    const char* path_ = nullptr;
    std::uint32_t abandons_ = 0;
};

const char* seed_slot_name(SeedTask::Slot) noexcept;

}  // namespace depthcharge::fw

#endif  // DC_VENUE == DC_VENUE_BINANCE
