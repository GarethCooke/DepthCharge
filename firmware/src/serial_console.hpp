// firmware/src/serial_console.hpp — the Core 1 consumer, and stage C's evidence.
//
// This is a placeholder for the render task, and it is a deliberate one. Stage D
// replaces the body with the HUB75 driver; what it must NOT change is the shape,
// because the shape is what stage C is proving on real silicon:
//
//   * it runs on Core 1, so SnapshotChannel is exercised as a genuine cross-core
//     hand-off on the LX7 rather than as the same-core call it is on the desk;
//   * it consumes with SnapshotChannel::consume and redraws only when that
//     returns true, which is the idle-rather-than-spin contract stage A built;
//   * it reads nothing but the DisplaySnapshot it was handed (invariant #8), so
//     it cannot accidentally become a second reader of the book.
//
// The output is also the acceptance evidence for the pull-the-Wi-Fi test, so it
// is shaped for a human reading a serial log rather than for a parser: every
// Live<->Stale transition prints immediately and loudly, steady state prints at
// about 1 Hz, and the statistics block prints every 10 s so a long run can be
// read at a glance.
//
// PRICES ARE FORMATTED WITH depthcharge::format_scaled INTO A STACK BUFFER.
// Not with String, not with printf("%f"), and not with the harness's
// format_px — which returns a std::string and is flagged in DESIGN.html §08 as
// exactly the idiom a firmware renderer must not copy. Integer ticks reach the
// display edge and are turned into text there, without a float and without a
// heap allocation (invariants #3 and #7).
#pragma once

#include <cstdint>

#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/snapshot_channel.hpp>

#include "feed_task.hpp"
#include "frame_pipe.hpp"
#include "heap_probe.hpp"

namespace depthcharge::fw {

class SerialConsole {
public:
    SerialConsole(SnapshotChannel& channel, const FeedTask& feed, const FramePipe& pipe,
                  HeapProbe& heap) noexcept
        : channel_(channel), feed_(feed), pipe_(pipe), heap_(heap) {}

    // Creates the task pinned to Core 1 — the other half of the two-core split.
    // Bytes, not words (see feed_task.hpp). The console formats several
    // 64-bit values per line through vsnprintf, so it is not as small a task
    // as it looks.
    bool start(std::uint32_t stack_bytes = 6144, UBaseType_t priority = 3) noexcept;

private:
    static void trampoline(void* self) noexcept;
    void run() noexcept;

    void draw(const DisplaySnapshot& snap) noexcept;
    void print_stats() noexcept;

    SnapshotChannel& channel_;
    const FeedTask& feed_;
    const FramePipe& pipe_;
    HeapProbe& heap_;

    DisplaySnapshot received_{};

    // Transition tracking: the log's job is to make the stale window obvious.
    bool have_seen_frame_ = false;
    FeedStatus last_status_ = FeedStatus::Stale;
    std::int64_t stale_since_us_ = 0;

    std::uint32_t frames_drawn_ = 0;
    std::uint32_t frames_at_baseline_ = 0;
    std::int64_t last_line_us_ = 0;
    std::int64_t last_stats_us_ = 0;
};

}  // namespace depthcharge::fw
