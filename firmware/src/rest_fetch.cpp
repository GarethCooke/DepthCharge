// firmware/src/rest_fetch.cpp — see rest_fetch.hpp for the esp-tls contract
// this is written against, all of it read out of the precompiled archive.
#include "rest_fetch.hpp"

#include "heap_probe.hpp"

// ONE VENUE PER BUILD, and this file is Binance's. `build_src_filter` is
// `+<*>`, so every `firmware/src` TU is compiled into every image — the parser
// TUs are swapped, but these are not. Without this guard an Anvil or Kraken
// image would carry a REST client for a host it has no constant for: the build
// fails on `kBinanceRestHost`, which is the loud version, but even compiling it
// would be ~2 KB of .text in an image whose flash figure is meant to be
// comparable. Same reasoning as the `#if DC_VENUE` arms in `render_task.cpp`.
#if DC_VENUE == DC_VENUE_BINANCE

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"   // esp_get_free_heap_size
#include "esp_timer.h"
#include "esp_tls.h"

// For `kReserveInternalBytes`: D-A2 §4's whole point is that the second
// session's draw is measured against the number `panel.hpp` forecast for it.
#include "panel.hpp"

namespace depthcharge::fw {
namespace {

constexpr const char* kTag = "rest";

// THE CONFIG LAYOUT THIS FILE DEPENDS ON, ASSERTED RATHER THAN ASSUMED.
//
// `struct esp_tls` and `esp_tls_cfg_t` layouts depend on Kconfig values being
// visible in THIS translation unit; if they were not, `cfg->non_block` would be
// written at an offset the precompiled archive does not read, and the connect
// would silently run in the wrong mode. These are the offsets the disassembly
// of `libesp-tls.a` actually reads. Verified, and kept: it cost four flash
// cycles to rule this out by hand.
static_assert(offsetof(esp_tls_cfg_t, non_block) == 36, "non_block offset");
static_assert(offsetof(esp_tls_cfg_t, timeout_ms) == 40, "timeout_ms offset");

// The panel's mask — see the header's note on `free_before`.
constexpr std::uint32_t kCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;

std::uint32_t free_internal() noexcept {
    return static_cast<std::uint32_t>(heap_caps_get_free_size(kCaps));
}
std::uint32_t largest_internal() noexcept {
    return static_cast<std::uint32_t>(heap_caps_get_largest_free_block(kCaps));
}
std::uint32_t free_total() noexcept {
    return static_cast<std::uint32_t>(esp_get_free_heap_size());
}

// `kTlsBlockBytes` moved to heap_probe.hpp at M5 stage D-A3 -- the reconnect
// path needs the same threshold and two copies would drift.

// "no data yet" vs "this session is dead". Contract clause 8: WANT_READ and
// WANT_WRITE come back verbatim, and a 0 is never "not yet".
enum class Io : std::uint8_t { Data, WouldBlock, Eof, Fatal };

Io classify(ssize_t rc) noexcept {
    if (rc > 0) { return Io::Data; }
    if (rc == 0) { return Io::Eof; }
    if (rc == ESP_TLS_ERR_SSL_WANT_READ || rc == ESP_TLS_ERR_SSL_WANT_WRITE) {
        return Io::WouldBlock;
    }
    return Io::Fatal;
}

}  // namespace

const char* fetch_phase_name(FetchPhase p) noexcept {
    switch (p) {
        case FetchPhase::Idle:     return "idle";
        case FetchPhase::Connect:  return "connect";
        case FetchPhase::Request:  return "request";
        case FetchPhase::Body:     return "body";
        case FetchPhase::Complete: return "complete";
        case FetchPhase::Failed:   return "failed";
    }
    return "?";
}

const char* fetch_error_name(FetchError e) noexcept {
    switch (e) {
        case FetchError::None:      return "none";
        case FetchError::NoAddress: return "no-address";
        case FetchError::NoBuffer:  return "no-buffer";
        case FetchError::TlsInit:   return "tls-init";
        case FetchError::Connect:   return "connect";
        case FetchError::Write:     return "write";
        case FetchError::Read:      return "read";
        case FetchError::Http:      return "http";
        case FetchError::Deadline:  return "DEADLINE";
        case FetchError::Abandoned: return "abandoned";
    }
    return "?";
}

bool RestFetch::begin() noexcept {
    body_.reset(new (std::nothrow) char[kRestBodyCapacity]);
    if (body_ == nullptr) {
        ESP_LOGE(kTag, "no room for a %u B seed buffer — the seed cannot be fetched,"
                       " so the ladder stays grey and says so",
                 static_cast<unsigned>(kRestBodyCapacity));
        return false;
    }
    ESP_LOGI(kTag, "seed buffer %u B (PSRAM; free_total %u)",
             static_cast<unsigned>(kRestBodyCapacity), static_cast<unsigned>(free_total()));
    return true;
}

bool RestFetch::start(const char* dotted_quad, const char* path, std::int64_t now_us) noexcept {
    if (body_ == nullptr) { error_ = FetchError::NoBuffer; return false; }
    if (dotted_quad == nullptr || dotted_quad[0] == '\0') {
        // DECLINED, NOT STALLED. `getaddrinfo` inside the connect blocks
        // unconditionally (contract clause 7) and DNS on this board measured
        // 14,000 ms, so a fetch without a cached address would freeze the feed
        // task for the length of a resolve. The caller warms the address off
        // this task; until it has, there is no fetch.
        error_ = FetchError::NoAddress;
        return false;
    }

    report_ = RestFetchReport{};
    report_.free_before = free_internal();
    report_.largest_before = largest_internal();
    report_.total_before = free_total();

    http_.begin(body_.get(), kRestBodyCapacity);
    path_ = path;
    // Copied, because esp-tls re-reads it on every step. See the member.
    std::strncpy(addr_, dotted_quad, sizeof(addr_) - 1);
    addr_[sizeof(addr_) - 1] = '\0';
    request_sent_ = 0;
    started_us_ = now_us;
    error_ = FetchError::None;
    ++fetches_;

    // `Connection: close` is load-bearing: one response per session, no
    // remainder to carry, and the session's two 16,717 B internal blocks are
    // certainly gone before the next fetch — which is what the reserve assumes.
    //
    // No `Accept-Encoding`: a gzipped body would need an inflate this firmware
    // does not have and must not grow (invariant #7 — it would allocate per
    // fetch). Omitting the header is how you ask a server not to compress;
    // sending `identity` is the same request said in a way some servers get
    // wrong.
    const int n = std::snprintf(request_, sizeof(request_),
                                "GET %s HTTP/1.1\r\n"
                                "Host: %s\r\n"
                                "User-Agent: DepthCharge/1.0\r\n"
                                "Accept: application/json\r\n"
                                "Connection: close\r\n"
                                "\r\n",
                                path_, kBinanceRestHost);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(request_)) {
        error_ = FetchError::Write;
        phase_ = FetchPhase::Failed;
        ++failures_;
        return false;
    }
    request_len_ = static_cast<std::size_t>(n);

    // The config is a MEMBER and is written here, once, because esp-tls re-reads
    // it on every `conn_new_async` call and dereferences it unguarded on its
    // hard error path (contract clause 4). A local would be a use-after-scope on
    // the second step.
    static_assert(sizeof(esp_tls_cfg_t) <= sizeof(RestFetch::cfg_),
                  "esp_tls_cfg_t outgrew its storage in rest_fetch.hpp");
    esp_tls_cfg_t* cfg = new (cfg_) esp_tls_cfg_t{};
    cfg->cacert_buf = reinterpret_cast<const unsigned char*>(venue::kRootCaPem);
    cfg->cacert_bytes = venue::kRootCaPemBytes;
    cfg->non_block = false;
    // A REAL BOUND NOW, because this blocks. It is also what bounds how long an
    // ABANDON takes to be honoured: the seed task checks the abandon flag
    // between phases, so the worst case is one esp-tls call, and this is that
    // call's ceiling. Zero or negative would block for ever (contract clause 3),
    // which is the one value that must never be set here.
    cfg->timeout_ms = kFetchCallTimeoutMs;
    // SNI AND CN VERIFICATION, restored over a literal-IP connect. Disassembly
    // of `set_client_config` shows `mbedtls_ssl_set_hostname` — which sets both
    // — is given `cfg->common_name` when non-NULL. So the real hostname goes out
    // as SNI and the certificate is still checked against it, even though the
    // connect is addressed numerically.
    cfg->common_name = kBinanceRestHost;

    tls_ = esp_tls_init();
    if (tls_ == nullptr) {
        ESP_LOGW(kTag, "esp_tls_init failed — no internal heap for a second session?"
                       " largest=%u, a session needs 2 x %u B",
                 static_cast<unsigned>(report_.largest_before),
                 static_cast<unsigned>(kTlsBlockBytes));
        error_ = FetchError::TlsInit;
        ++failures_;
        phase_ = FetchPhase::Failed;
        return false;
    }
    phase_ = FetchPhase::Connect;
    return true;
}

FetchPhase RestFetch::step(std::int64_t now_us) noexcept {
    if (phase_ == FetchPhase::Idle || phase_ == FetchPhase::Complete ||
        phase_ == FetchPhase::Failed) {
        return phase_;
    }
    if (now_us - started_us_ >= kSeedDeadlineUs) { return fail(FetchError::Deadline, now_us); }

    const std::int64_t slice_start = esp_timer_get_time();
    ++report_.steps;
    auto* tls = static_cast<esp_tls_t*>(tls_);
    auto* cfg = reinterpret_cast<esp_tls_cfg_t*>(cfg_);

    // One slice: keep going until would-block, done, or the budget is spent.
    // Draining rather than doing one record per call is what keeps the body
    // phase to a handful of steps instead of tens — at one `read()` per step a
    // 64,046 B body would need whatever the MSS gave us, ~45 steps.
    for (;;) {
        if (esp_timer_get_time() - slice_start >= kFetchStepBudgetUs) { break; }

        if (phase_ == FetchPhase::Connect) {
            // BLOCKING, AND THIS IS THE WHOLE OF WHAT THE THIRD TASK BOUGHT.
            //
            // `esp_tls_conn_new_async` does not converge on this build. The
            // board polled it 1,780 times over 15 s, four runs in a row, and it
            // stayed at `conn_state=1` (CONNECTING) with `errno=119`
            // (EINPROGRESS) every time. The dispatcher for CONNECTING jumps
            // straight past the `FD_ZERO`/`FD_SET` block that arms its
            // `fd_set`s, and `select()` zeroes those sets on timeout — so every
            // poll after the first selects on empty sets. (Not fully
            // corroborated: raising `timeout_ms` 2 -> 600 ms did not change the
            // step duration, which the empty-set account alone does not
            // explain. The OUTCOME is certain and was measured four times; the
            // mechanism is partly inferred, and is recorded that way.)
            //
            // The synchronous call works first time: HTTP 200 and a byte-exact
            // 64,046 B body. It is used here because this runs on a task that
            // may block — see `seed_task.hpp`. On the feed task it stalled for
            // **1.93 s**, dropped pipe messages and broke the very bracket the
            // seed exists to satisfy.
            const int rc = esp_tls_conn_new_sync(addr_, static_cast<int>(std::strlen(addr_)),
                                                 venue::kPort, cfg, tls);
            if (rc < 0) { return fail(FetchError::Connect, now_us); }
            // LATCH ON THE FIRST 1. `DONE` has no case in esp-tls's dispatcher,
            // so one more poll would return -1 on a perfectly healthy session.
            report_.free_during = free_internal();
            report_.largest_during = largest_internal();
            report_.session_draw = report_.free_before > report_.free_during
                                       ? report_.free_before - report_.free_during
                                       : 0;
            phase_ = FetchPhase::Request;
            continue;
        }

        if (phase_ == FetchPhase::Request) {
            const ssize_t rc = esp_tls_conn_write(tls, request_ + request_sent_,
                                                  request_len_ - request_sent_);
            const Io io = classify(rc);
            if (io == Io::WouldBlock) { break; }
            if (io != Io::Data) { return fail(FetchError::Write, now_us); }
            // Partial writes are normal and WANT_WRITE is only visible when
            // ZERO bytes were accepted, so the retry resends from an offset
            // rather than re-issuing the whole request.
            request_sent_ += static_cast<std::size_t>(rc);
            if (request_sent_ >= request_len_) { phase_ = FetchPhase::Body; }
            continue;
        }

        // Body. Read into a scratch window and hand it to the parser, which owns
        // the framing. Never read with zero free space: a zero-length read
        // returns 0, which this file reads as the session dying.
        char chunk[1024];
        const ssize_t rc = esp_tls_conn_read(tls, chunk, sizeof(chunk));
        const Io io = classify(rc);
        if (io == Io::WouldBlock) { break; }
        if (io != Io::Data) {
            // 0 is the same for a clean close_notify and a bare FIN, so it can
            // never mean "body complete" — `Content-Length` is what completes a
            // body. Reaching here with the parser still reading is a SHORT
            // response, which is a failure and not a smaller book.
            return fail(FetchError::Read, now_us);
        }

        const HttpPhase hp = http_.feed(chunk, static_cast<std::size_t>(rc));
        if (hp == HttpPhase::Failed) {
            report_.http_status = http_.status();
            return fail(FetchError::Http, now_us);
        }
        if (hp == HttpPhase::Complete) {
            report_.http_status = http_.status();
            report_.body_bytes = static_cast<std::uint32_t>(http_.body_bytes());
            report_.ok = true;
            phase_ = FetchPhase::Complete;
            close_session();
            sample_after();
            report_.elapsed_ms = static_cast<std::uint32_t>((now_us - started_us_) / 1000);
            break;
        }
    }

    const std::uint32_t took =
        static_cast<std::uint32_t>(esp_timer_get_time() - slice_start);
    if (took > report_.worst_step_us) { report_.worst_step_us = took; }
    return phase_;
}

void RestFetch::abandon(FetchError why, std::int64_t now_us) noexcept {
    if (phase_ == FetchPhase::Idle || phase_ == FetchPhase::Complete ||
        phase_ == FetchPhase::Failed) {
        return;
    }
    (void)fail(why, now_us);
}

FetchPhase RestFetch::fail(FetchError why, std::int64_t now_us) noexcept {
    // WHERE IT GOT STUCK, read straight out of the handle before it is
    // destroyed. `conn_state` is the only thing that distinguishes "the TCP
    // connect never completed" (CONNECTING=1) from "the TLS handshake never
    // completed" (HANDSHAKE=2), and the two have completely different causes.
    // Offset asserted in `step()`; esp-tls exposes no accessor.
    if (tls_ != nullptr) {
        auto* t = static_cast<esp_tls_t*>(tls_);
        stuck_state_ = static_cast<int>(t->conn_state);
        int tls_code = 0;
        int tls_flags = 0;
        if (t->error_handle != nullptr) {
            (void)esp_tls_get_and_clear_last_error(t->error_handle, &tls_code, &tls_flags);
        }
        stuck_code_ = tls_code;
        stuck_flags_ = tls_flags;
        stuck_errno_ = errno;
    }
    error_ = why;
    if (report_.http_status == 0) { report_.http_status = http_.status(); }
    report_.ok = false;
    ++failures_;
    close_session();
    sample_after();
    report_.elapsed_ms = static_cast<std::uint32_t>((now_us - started_us_) / 1000);
    phase_ = FetchPhase::Failed;
    return phase_;
}

void RestFetch::close_session() noexcept {
    if (tls_ != nullptr) {
        // Safe at any phase, including mid-handshake (contract clause 6), and
        // called on EVERY exit — a leaked session is 33 KB of internal SRAM and
        // the next fetch would fail for a reason that has nothing to do with it.
        esp_tls_conn_destroy(static_cast<esp_tls_t*>(tls_));
        tls_ = nullptr;
    }
}

void RestFetch::sample_after() noexcept {
    report_.free_after = free_internal();
    report_.largest_after = largest_internal();
    report_.total_after = free_total();
}

void RestFetch::release() noexcept {
    close_session();

    // D-A2 §4's line, printed once per fetch on release rather than per step,
    // and printed for failures too — a failed fetch's heap figures are the more
    // interesting ones. `largest_during` is what the D-C check reads.
    ESP_LOGI(kTag,
             "fetch %s(%s) HTTP %d %u B in %u ms over %u steps, worst step %u us"
             " | internal free %u/%u/%u largest %u/%u/%u | draw=%u vs reserve=%u"
             " | total %u -> %u",
             report_.ok ? "OK" : "FAILED", fetch_error_name(error_), report_.http_status,
             static_cast<unsigned>(report_.body_bytes), static_cast<unsigned>(report_.elapsed_ms),
             static_cast<unsigned>(report_.steps), static_cast<unsigned>(report_.worst_step_us),
             static_cast<unsigned>(report_.free_before), static_cast<unsigned>(report_.free_during),
             static_cast<unsigned>(report_.free_after),
             static_cast<unsigned>(report_.largest_before),
             static_cast<unsigned>(report_.largest_during),
             static_cast<unsigned>(report_.largest_after),
             static_cast<unsigned>(report_.session_draw),
             static_cast<unsigned>(kReserveInternalBytes),
             static_cast<unsigned>(report_.total_before),
             static_cast<unsigned>(report_.total_after));

    if (!report_.ok) {
        // 0=INIT 1=CONNECTING 2=HANDSHAKE 3=FAIL 4=DONE
        ESP_LOGW(kTag, "  stuck at conn_state=%d, esp_tls code=-0x%04x flags=0x%08x, errno=%d",
                 stuck_state_, -stuck_code_, static_cast<unsigned>(stuck_flags_), stuck_errno_);
    }

    if (report_.largest_during != 0 && report_.largest_during < kTlsBlockBytes) {
        ESP_LOGW(kTag, "largest internal block %u B is BELOW the %u B a TLS session needs"
                       " — the reserve is wrong for two sessions (see panel.hpp)",
                 static_cast<unsigned>(report_.largest_during),
                 static_cast<unsigned>(kTlsBlockBytes));
    }
    if (error_ == FetchError::Http && http_.error() != HttpError::None) {
        ESP_LOGW(kTag, "response refused: %s (status %d)", http_error_name(http_.error()),
                 report_.http_status);
    }
    error_ = FetchError::None;
    phase_ = FetchPhase::Idle;
}

}  // namespace depthcharge::fw

#endif  // DC_VENUE == DC_VENUE_BINANCE
