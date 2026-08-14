// firmware/src/rx_budget.hpp — where the RX task's second actually goes.
//
// THE QUESTION THIS EXISTS TO ANSWER, dated 2026-08-14: the board drains Anvil
// at ~44 KiB/s while the same stack pulls 213 KiB/s of raw TCP and the diag's
// TLS+WS flow ~79 KiB/s (ARCHITECTURE §9). The pin is therefore somewhere in
// the production RX path — esp_tls read, WS deframe, reassembly, pipe publish —
// and every instrument this firmware had measured *stopped* or *behind*, never
// *how the running loop spends its time*. This header is that instrument: three
// stopwatches around the only three things rx_main() does, accumulated
// cumulatively and diffed by the statistics block into one line.
//
// Reading the line: `wait` is time inside esp_tls_conn_read that returned
// nothing (the SO_RCVTIMEO tick) — genuine starvation, the bytes were not here.
// `read` is time inside the read that produced bytes — the TLS decrypt path.
// `feed` is time inside WsFrameParser::feed — deframe + reassemble + pipe.
// `other` is the remainder of the wall-clock window: loop overhead, the write
// path, reconnects, and anything that preempted the task. A ceiling caused by
// upstream delivery shows as `wait`-dominated; a ceiling caused by this
// firmware's processing shows in `read`+`feed`; a ceiling caused by scheduling
// shows as `other`.
//
// ESP-IDF-free and host-tested (test_rx_budget.cpp), following the precedent of
// gap_histogram.hpp: the only firmware code the bench cannot check on a desk
// should be the code that touches a peripheral. Cumulative counters written by
// one task and diffed by another, the same loose cross-task convention every
// other bench counter here follows (invariant #7: no allocation anywhere).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace depthcharge::fw {

struct RxBudget {
    std::uint64_t wait_us = 0;   // reads that returned WANT_READ/WANT_WRITE
    std::uint64_t read_us = 0;   // reads that returned bytes
    std::uint64_t feed_us = 0;   // parser feed of those bytes
    std::uint64_t bytes = 0;
    std::uint32_t reads = 0;     // reads that returned bytes
    std::uint32_t waits = 0;
    std::uint32_t worst_read_us = 0;
    std::uint32_t worst_feed_us = 0;

    // Durations arrive as int64 differences of a monotonic clock; negative or
    // absurd values are clamped rather than trusted, because a wrapped or
    // misread stopwatch must never make the instrument lie loudly.
    static std::uint32_t clamp(std::int64_t us) noexcept {
        if (us < 0) { return 0; }
        if (us > 0x7FFFFFFF) { return 0x7FFFFFFF; }
        return static_cast<std::uint32_t>(us);
    }

    void on_wait(std::int64_t us) noexcept {
        wait_us += clamp(us);
        ++waits;
    }
    void on_read(std::int64_t us, std::uint32_t n) noexcept {
        const std::uint32_t c = clamp(us);
        read_us += c;
        bytes += n;
        ++reads;
        if (c > worst_read_us) { worst_read_us = c; }
    }
    void on_feed(std::int64_t us) noexcept {
        const std::uint32_t c = clamp(us);
        feed_us += c;
        if (c > worst_feed_us) { worst_feed_us = c; }
    }
};

// One statistics line from two snapshots and the wall-clock window between
// them. Percentages are of the WINDOW, not of their own sum, so time this task
// did not spend in its three states — scheduling, writes, reconnects — is
// visible as the shortfall (`other`) instead of being normalised away.
// Truncation is defined and harmless; returns the snprintf count.
inline int render_rx_budget(const RxBudget& now, const RxBudget& prev,
                            std::uint64_t window_us, char* out, std::size_t cap) noexcept {
    if (cap == 0) { return 0; }
    const std::uint64_t wait = now.wait_us - prev.wait_us;
    const std::uint64_t read = now.read_us - prev.read_us;
    const std::uint64_t feed = now.feed_us - prev.feed_us;
    const std::uint64_t bytes = now.bytes - prev.bytes;
    const std::uint32_t reads = now.reads - prev.reads;
    const std::uint32_t waits = now.waits - prev.waits;

    if (window_us == 0) { window_us = 1; }
    const auto pct = [window_us](std::uint64_t us) -> unsigned {
        return static_cast<unsigned>((us * 100u) / window_us);
    };
    const std::uint64_t accounted = wait + read + feed;
    const unsigned other = (accounted < window_us) ? 100u - pct(accounted) : 0u;
    const std::uint32_t mean_read = (reads != 0) ? static_cast<std::uint32_t>(bytes / reads) : 0;
    // bytes/s at µs resolution without overflow: bytes is bounded by ~50 KiB/s
    // windows in practice, but the arithmetic must not depend on that.
    const std::uint64_t kb_x10 = (bytes * 10000000ull / 1024ull) / window_us;

    return std::snprintf(out, cap,
                         "wait %u%% read %u%% feed %u%% other %u%% | %u reads %u waits"
                         " | mean %u B worst read %u feed %u us | %u.%u KiB/s",
                         pct(wait), pct(read), pct(feed), other,
                         static_cast<unsigned>(reads), static_cast<unsigned>(waits),
                         static_cast<unsigned>(mean_read), static_cast<unsigned>(now.worst_read_us),
                         static_cast<unsigned>(now.worst_feed_us),
                         static_cast<unsigned>(kb_x10 / 10), static_cast<unsigned>(kb_x10 % 10));
}

}  // namespace depthcharge::fw
