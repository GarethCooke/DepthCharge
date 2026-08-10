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
                  HeapProbe& heap, const CoreIdleProbe& idle, const LinkQuality& link) noexcept
        : channel_(channel), feed_(feed), pipe_(pipe), heap_(heap), idle_(idle), link_(link) {}

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

    // The arrival / event / latency distributions — M3's stall instrument.
    // Separate from print_stats() because it is three lines with a shared
    // buffer and a shared reading, and because the block is meant to be
    // greppable out of a bench log as a unit.
    void print_distributions(const FeedTask::Stats& f, const FramePipeStats& p) noexcept;
    void print_gap_line(const char* what, const GapHistogram& h) noexcept;

    // The per-status breakdown of what the parser rejected. Prints nothing at
    // all on a healthy run — see the note at the definition for why that is
    // deliberate rather than a missing else.
    void print_rejects(const RejectLog& rejects) noexcept;

    // The stall verdict block: per-core idle, rssi, and the classified tally of
    // >1 s book-holes. Separate from print_stats() for the same reason the
    // distributions are — it is a unit, meant to be grepped out of a bench log
    // as one, and read as one.
    void print_stall(const FeedTask::Stats& f) noexcept;

    // One line per finalised hole, printed as it completes rather than saved for
    // the 10 s block: at the bench the useful moment is while the grey is still
    // on screen. Called from the run loop, so it must print at most a couple of
    // lines per pass — which it does, holes being a twice-a-minute event.
    void drain_holes(const StallProbe& stall) noexcept;

    // One line per captured rejected payload, printed as it happens for the same
    // reason the holes are: at a bench the useful moment is while the burst is
    // still running. Bounded by kRejectsPerConnect, so it cannot flood the log
    // even when the parser is rejecting everything.
    void drain_rejects(const RejectLog& rejects) noexcept;

    // Per-window rates, derived from the running counters. Separate from
    // print_stats() because it is the only part that carries state across calls.
    void print_rates(const FramePipeStats& p, std::uint64_t events_out) noexcept;

    SnapshotChannel& channel_;
    const FeedTask& feed_;
    const FramePipe& pipe_;
    HeapProbe& heap_;
    const CoreIdleProbe& idle_;
    const LinkQuality& link_;

    DisplaySnapshot received_{};

    // How far this task has got through the probe's finalised hole records, and
    // the per-core idle counters at the last statistics block so the block can
    // report idle over ITS window as well — a cheap independent check on the
    // per-hole figures, since the two are computed from the same counters by
    // different tasks over different windows and must tell the same story.
    std::uint32_t holes_printed_ = 0;
    std::uint32_t rejects_printed_ = 0;
    std::uint32_t idle0_at_block_ = 0;
    std::uint32_t idle1_at_block_ = 0;
    std::int64_t block_started_us_ = 0;

    // Transition tracking: the log's job is to make the stale window obvious.
    bool have_seen_frame_ = false;
    FeedStatus last_status_ = FeedStatus::Stale;
    std::int64_t stale_since_us_ = 0;

    std::uint32_t frames_drawn_ = 0;
    std::uint32_t frames_at_baseline_ = 0;
    std::int64_t last_line_us_ = 0;
    std::int64_t last_stats_us_ = 0;

    // Previous window's totals, so the stats block can report RATES rather than
    // running counts. The first bench run could only be compared against M0's
    // July figures and left "is the server slower, or are we missing frames?"
    // unanswerable from the log alone. A rate printed beside a simultaneous host
    // capture answers it.
    struct Window {
        std::int64_t at_us = 0;
        std::uint32_t published = 0;
        std::uint32_t attempted = 0;   // published + no_slot + oversize
        std::uint64_t bytes = 0;
        std::uint32_t chunks = 0;
        std::uint64_t events = 0;
        std::uint32_t drawn = 0;
    };
    Window prev_{};
    bool have_prev_ = false;
};

}  // namespace depthcharge::fw
