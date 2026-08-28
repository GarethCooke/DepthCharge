// firmware/src/rest_fetch.hpp — one HTTPS GET, as a state machine the feed task
// steps, for the seed Binance will not push.
//
// M5 stage D-A2 §2. This is the first time this firmware has ever ASKED a venue
// for anything over anything other than the WebSocket it was already holding,
// and the reason is structural: Binance's diff stream carries no snapshot at
// all. `U`/`u` bracket a book the client must obtain somewhere else, so without
// this the adapter buffers diffs for ever and the panel is honestly grey —
// exactly what D-A1 shipped and demonstrated.
//
// ===========================================================================
// WHY A STATE MACHINE AND NOT A FUNCTION THAT FETCHES
// ===========================================================================
//
// A blocking fetch takes ~1.0-1.5 s. Run on the feed task that stalls the only
// consumer of `FramePipe`, whose four slots hold ~0.4 s at this venue's 10
// messages/second — so the pipe drops, and at a DIFF venue a dropped message is
// not a skipped refresh. `frame_pipe.hpp`'s justification for tolerating drops
// (*"book frames are idempotent full replaces"*) is Anvil's and is FALSE here:
// a lost diff fails the next `U == last_u + 1`, drops the book and asks for
// another seed. A blocking fetch would therefore have destroyed the very thing
// it was fetching.
//
// The other two contexts are worse. `loopTask`'s own comment records that the
// ~4 s blocking connect was deliberately moved OFF it; the RX task blocking
// stops the reads entirely. And a third task would be a third cross-task
// hand-off past `frame_pipe.hpp`'s *"exactly two... deliberately the only two"*.
//
// So: the feed task steps this, in bounded slices, between messages.
//
// ===========================================================================
// THE esp-tls CONTRACT THIS IS WRITTEN AGAINST — VERIFIED BY DISASSEMBLY
// ===========================================================================
//
// No esp-tls sources exist on this machine; the framework ships headers and a
// precompiled `libesp-tls.a`. Everything below was read out of that archive
// with `objdump`, because the header is wrong about one of them.
//
//   1. `esp_tls_conn_new_async` returns 1 done / 0 in progress / -1 fail, and
//      dispatches on `tls->conn_state`. **Latch on the first 1**: `DONE` has no
//      case in the dispatcher, so polling a finished session returns -1.
//   2. **A `select()` timeout returns 0 with `conn_state` untouched** — so the
//      connect really is pollable. (`15d: beqz a10, b4` / `b4: mov a2, a10` /
//      `b6: retw`.) This was the largest open question and it resolved well.
//   3. **`cfg.timeout_ms <= 0` passes NULL to `select` and blocks for ever**,
//      even with `non_block = true` (`14f: bgei a13, 1, 155` / `152: movi
//      a14, 0`). It must be a small positive number.
//   4. `cfg` is **not copied into the handle** — `non_block` and `timeout_ms`
//      are re-read on every call and `cfg` is passed to the handshake each
//      time. It must outlive the session, so it is a member here, not a local.
//   5. **The header is wrong about `non_block`.** It says the socket goes
//      non-blocking *"after tls session is established"*; the code sets
//      `O_NONBLOCK` before `lwip_connect` always, and `non_block` only decides
//      whether it is switched back. So the HANDSHAKE is pollable too.
//   6. `esp_tls_conn_destroy` never reads `conn_state` and is safe at any
//      phase, including mid-handshake. Abandon is always legal.
//   7. **`getaddrinfo` is called unconditionally inside the connect**, before
//      the socket exists, with no `AI_NUMERICHOST` — and DNS on this board was
//      measured at 14,000 ms. See `start()`: this takes a DOTTED QUAD, never a
//      hostname, and `cfg.common_name` restores SNI and CN verification.
//      Disassembly of `set_client_config` confirms `mbedtls_ssl_set_hostname`
//      is given `cfg->common_name` when non-NULL, so the real hostname is still
//      what goes out as SNI and still what the certificate is checked against.
//   8. Reads: `WANT_READ`/`WANT_WRITE` come back verbatim, so **"no data" can
//      never surface as 0**. A 0 is the session dying — and it is the same 0
//      for a clean `close_notify` and a bare FIN, which is why the body is
//      framed by `Content-Length` and never by EOF.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string_view>

#include "http_response.hpp"
#include "seed_schedule.hpp"
#include "venue_build.hpp"

// See the note in `rest_fetch.cpp`: one venue per build, and this is Binance's.
// The declaration is guarded with the definition so that a use at another venue
// is a compile error naming this file, rather than a link error naming a symbol.
#if DC_VENUE == DC_VENUE_BINANCE

namespace depthcharge::fw {

// THE BODY BUFFER, AND WHY IT IS NOT 64,046 B PLUS A LITTLE.
//
// Eleven committed BTCUSDT bodies at `limit=1000` are **exactly 64,046 B**, and
// the stage-D scoping brief established both why that constant length is real
// and why it is fragile: it is constant because this pair's INTEGER-DIGIT WIDTH
// is constant on this tape — 22,000 entries, every price string 14 chars and
// every quantity 10 — not because of decimal padding, which gives no constant
// length at all.
//
// So the obvious size is the wrong one. **A single power-of-ten crossing in the
// BTC price adds a character to every one of ~11,000 price strings**, growing
// the body by ~11 KB from a market move that is in no sense exceptional. A
// buffer sized to the measured worst would start refusing seeds on a Tuesday,
// and the failure would not be loud in the right place: the fetch refuses, the
// adapter never baselines, and the panel greys for a reason nothing names.
//
// 96 KiB is 1.53x the measured body — room for a full extra digit on every
// price AND every quantity, with slack. It is in PSRAM, where this board has
// 8 MB and this is 1.2% of it, so the usual argument for sizing tightly does
// not apply.
inline constexpr std::size_t kRestBodyCapacity = 96u * 1024u;

// THE CEILING ON ONE BLOCKING esp-tls CALL, and it is doing two jobs.
//
// It bounds the connect (contract clause 3 — zero or negative blocks for ever,
// which is the one value that must never be set). And because the seed task
// checks its abandon flag BETWEEN phases rather than inside them, it is also
// the worst-case latency for honouring an abandon: a fetch condemned by a
// socket drop releases its two 16,717 B internal blocks within one of these.
//
// 5 s against measured round trips of 4.1-6.3 s for the WHOLE fetch, of which
// the connect and handshake are the smaller part. Long enough not to time out a
// healthy connect to Tokyo; short enough that a reconnect does not wait on a
// condemned fetch.
inline constexpr int kFetchCallTimeoutMs = 5'000;

// THE PER-STEP BUDGET. Derived, not chosen: the pipe holds `kFrameSlots` = 4
// messages, this venue delivers one per ~99.2 ms, and the feed task also spends
// `worst_frame` on each. `4 x (worst_frame + S) <= 4 x 99.2 ms` with the board's
// logged `worst_frame = 4,297 us` gives `S <= 94.9 ms`; 20 ms is a fifth of
// that, and `worst_step_us` is REPORTED so the assumption is measured rather
// than trusted. The same run logged one unexplained 29,583 us `worst_frame`
// outlier, which is why the budget is not sized to the headroom.
inline constexpr std::int64_t kFetchStepBudgetUs = 20'000;

// HOW OFTEN THE FEED TASK COMES BACK WHILE A FETCH IS IN FLIGHT.
//
// 5 ms, and it is short ON PURPOSE. The obvious value is the frame cadence
// (~100 ms), because that is when the task wakes anyway — but the machine is
// PACED by its steps: a TLS handshake is several round trips and the body is
// several reads, so at 100 ms per step a fetch that blocks for 1.2 s would take
// several seconds instead. The pre-seed buffer now covers the whole 15 s
// deadline, but the fetch still competes with it, and a non-blocking fetch that
// finished SLOWER than the blocking one it replaced would be a regression
// dressed as an improvement.
//
// The cost is bounded and small: an empty `step()` on a would-block socket is
// one `select` of `kConnectSelectMs` or one `esp_tls_conn_read` returning
// WANT_READ, and it only happens while a fetch is running — which is a few
// seconds out of every re-seed, not the steady state.
inline constexpr std::int64_t kFetchPollUs = 5'000;

// The seed request's path. `limit` is `kBinanceRestLimit` (1,000), whose lower
// bound the engine already enforces: `kBinanceReseedCoverLevels <
// kBinanceRestLimit`, so a seed shallower than the re-seed trigger cannot
// compile. Spelled here so the one place that issues the request is the one
// place that names it.
inline constexpr const char* kBinanceSeedPath =
    "/api/v3/depth?symbol=BTCUSDT&limit=1000";

// The two spellings of the fetch deadline, tied together. `kSeedDeadlineUs` is
// the schedule's (ESP-IDF-free, host-tested); `kBinanceFetchDeadlineMs` is the
// engine's, which the pre-seed buffer is now sized to cover. A third number
// here would be the same defect ARCHITECTURE §9 (2026-08-28) records.
static_assert(kSeedDeadlineUs ==
                  static_cast<std::int64_t>(binance::kBinanceFetchDeadlineMs) * 1000,
              "the schedule's deadline and the engine's must be one number: the pre-seed "
              "buffer is sized to cover exactly this interval");

enum class FetchPhase : std::uint8_t {
    Idle = 0,
    Connect,    // esp_tls_conn_new_async is returning 0
    Request,    // writing the GET
    Body,       // reading; headers and body are HttpResponse's phases
    Complete,   // body() is valid until release()
    Failed,     // error() says why; release() clears
};

enum class FetchError : std::uint8_t {
    None = 0,
    NoAddress,   // start() was not given a literal address
    NoBuffer,    // begin() never succeeded
    TlsInit,     // esp_tls_init returned null - no internal heap for a session
    Connect,     // esp_tls_conn_new_async returned -1
    Write,       // the request could not be sent
    Read,        // the session died mid-response
    Http,        // the response was refused; http_error() says which
    Deadline,    // the fetch outran kSeedDeadlineUs
    Abandoned,   // the caller tore it down (pre-seed buffer overflow)
};

const char* fetch_phase_name(FetchPhase) noexcept;
const char* fetch_error_name(FetchError) noexcept;

// What the fetch cost, in the terms D-A2 §4 asks for. Printed on every fetch,
// because a second concurrent TLS session is precisely what this board has
// never done and the reserve's forecast rests on numbers nobody has measured.
struct RestFetchReport {
    bool ok = false;
    int http_status = 0;
    std::uint32_t body_bytes = 0;
    std::uint32_t elapsed_ms = 0;
    std::uint32_t steps = 0;          // how many slices it took
    std::uint32_t worst_step_us = 0;  // the budget, measured rather than assumed

    // Internal SRAM, sampled with the panel's mask so these compare with
    // `panel.cpp`'s and not with `heap_probe.cpp`'s. The two differ in two bits
    // and `heap_probe.hpp` says so; this file needs the pool the TLS session
    // actually comes out of.
    std::uint32_t free_before = 0;
    std::uint32_t free_during = 0;
    std::uint32_t free_after = 0;
    std::uint32_t largest_before = 0;
    std::uint32_t largest_during = 0;   // THE number the D-C check watches
    std::uint32_t largest_after = 0;
    std::uint32_t total_before = 0;     // PSRAM-inclusive
    std::uint32_t total_after = 0;
    std::uint32_t session_draw = 0;     // free_before - free_during
};

class RestFetch {
public:
    // Allocates the body buffer. Separate from the constructor for the reason
    // `FramePipe::begin()` is: it can fail, and the caller must be able to say
    // so rather than discover it on the first fetch.
    bool begin() noexcept;
    bool ready() const noexcept { return body_ != nullptr; }

    // Starts a fetch. `dotted_quad` MUST be a literal address — see contract
    // clause 7. Returns false (and sets `NoAddress`) if it is null or empty,
    // which is how a board whose DNS warm has not yet succeeded declines
    // without stalling the feed task for 14 seconds.
    bool start(const char* dotted_quad, const char* path, std::int64_t now_us) noexcept;

    // One bounded slice. `Idle` in, `Idle` out, at zero cost. `Complete` and
    // `Failed` are sticky until `release()`.
    FetchPhase step(std::int64_t now_us) noexcept;

    // Tear down whatever is in flight. Always legal (contract clause 6).
    void abandon(FetchError why, std::int64_t now_us) noexcept;

    // Back to Idle. The caller must consume `body()` before calling this.
    void release() noexcept;

    FetchPhase phase() const noexcept { return phase_; }
    FetchError error() const noexcept { return error_; }
    HttpError http_error() const noexcept { return http_.error(); }
    bool in_flight() const noexcept {
        return phase_ == FetchPhase::Connect || phase_ == FetchPhase::Request ||
               phase_ == FetchPhase::Body;
    }
    // Valid only in `Complete`.
    std::string_view body() const noexcept { return http_.body(); }
    const RestFetchReport& report() const noexcept { return report_; }
    std::uint32_t fetches() const noexcept { return fetches_; }
    std::uint32_t failures() const noexcept { return failures_; }

private:
    FetchPhase fail(FetchError why, std::int64_t now_us) noexcept;
    void close_session() noexcept;
    void sample_after() noexcept;

    // PSRAM, on D-A1's precedent and for its reason: written once by the
    // socket, read once by the parser, never DMA'd, not the per-diff path.
    std::unique_ptr<char[]> body_;
    HttpResponse http_{};

    // `esp_tls_t*`, held as void* so this header does not drag `esp_tls.h` —
    // and with it a `struct esp_tls` whose layout depends on a Kconfig — into
    // every TU that merely wants to know a fetch is running. The `.cpp` asserts
    // the layout it depends on.
    void* tls_ = nullptr;

    // The config OUTLIVES the session by contract clause 4: esp-tls re-reads it
    // on every `conn_new_async` call and dereferences it unguarded on its hard
    // error path. A local would be a use-after-scope on the second step.
    // Declared as raw storage for the same header-hygiene reason as `tls_`.
    alignas(8) unsigned char cfg_[256] = {};

    // THE ADDRESS IS COPIED, NOT BORROWED, AND THE BOARD TAUGHT ME WHY.
    //
    // This was `const char* addr_` pointing at the caller's buffer. The caller
    // formats the dotted quad into a local, so the pointer dangled the moment
    // `start()` returned and every later `step()` read dead stack — which the
    // board reported as
    //
    //     E esp-tls: couldn't get hostname for :16.7<garbage>:
    //               getaddrinfo() returns 202
    //
    // i.e. a truncated, corrupted address. `esp_tls_conn_new_async` is called
    // on EVERY step, not once, so any borrowed string has to outlive the whole
    // fetch — the same lifetime trap as `cfg_` two members down, which the
    // disassembly warned about and this did not.
    //
    // 16 bytes holds "255.255.255.255" and its NUL.
    char addr_[16] = {};
    // The path is a `constexpr` string literal with static lifetime, so this one
    // may borrow.
    const char* path_ = nullptr;
    std::size_t request_sent_ = 0;
    char request_[192] = {};
    std::size_t request_len_ = 0;

    std::int64_t started_us_ = 0;
    FetchPhase phase_ = FetchPhase::Idle;
    FetchError error_ = FetchError::None;
    RestFetchReport report_{};
    // Diagnostics captured at failure, before the handle is destroyed.
    int stuck_state_ = -1;
    int stuck_code_ = 0;
    int stuck_flags_ = 0;
    int stuck_errno_ = 0;
    std::uint32_t fetches_ = 0;
    std::uint32_t failures_ = 0;
};

}  // namespace depthcharge::fw

#endif  // DC_VENUE == DC_VENUE_BINANCE
