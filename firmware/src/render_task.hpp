// firmware/src/render_task.hpp — the Core 1 consumer. M3 stage D: it draws.
//
// This was serial_console.{hpp,cpp} through stage C, where its body was a
// placeholder for exactly this. The name changed with the body because a file
// called serial_console that drives a HUB75 panel is a lie the next session
// reads; what did NOT change is the shape, which is the part stage C proved on
// real silicon and which stage D was told not to break:
//
//   * it runs on Core 1, so SnapshotChannel is exercised as a genuine cross-core
//     hand-off on the LX7 rather than as the same-core call it is on the desk;
//   * it consumes with SnapshotChannel::consume and redraws only when that
//     returns true, which is the idle-rather-than-spin contract stage A built;
//   * it reads nothing but the DisplaySnapshot it was handed (invariant #8), so
//     it cannot accidentally become a second reader of the book.
//
// IT IS STILL THE SINGLE CONSUMER, and that is why the serial output stayed here
// rather than moving to a task of its own. The panel and the bench log want the
// same frame, so they get it from the same consume() in the same task; a second
// reader would be a second participant at the one meeting point invariant #8
// allows exactly two.
//
// THE SERIAL OUTPUT IS NOT DEBUG NOISE — IT IS THE ACCEPTANCE EVIDENCE. The
// `*** STALE (reason) at vN ***` / `*** LIVE at vN ***` transitions and the 10 s
// statistics block are the only way to read a bench run after the fact, and the
// pull-the-Wi-Fi test is scored off them. They cost this task a few blocked
// milliseconds; the HUB75 DMA refreshes from the framebuffer autonomously, so a
// blocked render task is one skipped redraw, never a dark panel.
//
// PRIORITY 3, ABOVE loopTask's 1. loopTask is Core 1 too and runs
// WsTransport::supervise(); the two-handle reconnect no longer calls the 2.5 s
// blocking esp_websocket_client_stop() (ARCHITECTURE §9, 2026-08-10), but the
// margin stays because the panel must keep drawing the grey through whatever
// loopTask is doing. No supervisor work belongs on this task.
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
#include "ladder_render.hpp"
#include "panel.hpp"
#include "rx_budget.hpp"
#include "ws_ping.hpp"

namespace depthcharge::fw {

// The panel's frame period. Stage C polled at a flat 20 ms; the brief asks for
// the loop to be driven off the panel instead, at the concept's ~30 fps.
// 33 ms is 30.3 Hz at Arduino-ESP32's 1000 Hz tick, it is 30x finer than the
// 1000 ms watchdog deadline the acceptance test measures against, and it is
// coarse enough that this task sleeps for almost all of it. Anvil publishes
// ~13 frames/s, so most periods have nothing new and cost one consume() that
// returns false.
inline constexpr std::uint32_t kFramePeriodMs = 33;

class RenderTask {
public:
    RenderTask(SnapshotChannel& channel, const FeedTask& feed, const FramePipe& pipe,
               HeapProbe& heap, const CoreIdleProbe& idle, const LinkQuality& link,
               const RxBudget& rx, const PingProbe& ping, Panel& panel) noexcept
        : channel_(channel), feed_(feed), pipe_(pipe), heap_(heap), idle_(idle), link_(link),
          rx_(rx), ping_(ping), panel_(panel) {}

    // Creates the task pinned to Core 1 — the other half of the two-core split.
    // Bytes, not words (see feed_task.hpp). The task formats several 64-bit
    // values per line through vsnprintf, which is the greedy part; the renderer
    // itself is a fixed ~600 B frame plus LadderView, which is a member of this
    // object and not of the stack.
    bool start(std::uint32_t stack_bytes = 6144, UBaseType_t priority = 3) noexcept;

private:
    static void trampoline(void* self) noexcept;
    void run() noexcept;

    // Ladder to the panel, transitions and the 1 Hz line to the log — one
    // consumed frame, both outputs, in that order so the panel is never waiting
    // behind a UART.
    void draw(const DisplaySnapshot& snap) noexcept;

    // THE ONE PALETTE SELECTION (invariant #5). Nothing else in this firmware
    // reads snap.status to choose a colour, and ladder_render.hpp's renderer
    // cannot express one.
    void paint(const DisplaySnapshot& snap) noexcept;

    void print_stats() noexcept;

    // The arrival / event / latency distributions — M3's stall instrument.
    // Separate from print_stats() because it is three lines with a shared
    // buffer and a shared reading, and because the block is meant to be
    // greppable out of a bench log as a unit.
    void print_distributions(const FeedTask::Stats& f, const FramePipeStats& p) noexcept;
    void print_gap_line(const char* what, const GapHistogram& h) noexcept;

    // What the panel itself cost and what it is doing: the colour depth and
    // double-buffer answer stage D was told to measure rather than assume, plus
    // the draw rate and the worst single frame. The second pair is the feed-side
    // regression check's twin — if the LCD_CAM DMA and the Wi-Fi/TLS stack are
    // starving each other, it shows up as a draw rate below the publish rate.
    void print_panel() noexcept;

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
    //
    // It also prints the age line, because the age and the drain fraction it is
    // derived from must be read together — a lag figure with no ratio beside it
    // is the shape of number this milestone has already been misled by twice.
    void print_rates(const FramePipeStats& p, const FeedTask::Stats& f,
                     std::uint64_t events_out) noexcept;
    void print_rx() noexcept;

    SnapshotChannel& channel_;
    const FeedTask& feed_;
    const FramePipe& pipe_;
    HeapProbe& heap_;
    const CoreIdleProbe& idle_;
    const LinkQuality& link_;
    const RxBudget& rx_;
    // Const, and §6 #5 is the reason rather than tidiness — see
    // WsTransport::ping_probe(). This task draws the panel; handing it anything
    // it could read as "the venue is answering, so we are live" is exactly the
    // frozen-ladder-reading-LIVE outcome the invariant forbids. It prints it and
    // does nothing else with it.
    const PingProbe& ping_;
    Panel& panel_;

    // The RX budget's previous snapshot and window anchor — this task's own,
    // like drawn_at_block_ below, because the diffing instrument lives with
    // the printer, not with the counter it reads.
    RxBudget rx_prev_{};
    std::int64_t rx_block_us_ = 0;

    DisplaySnapshot received_{};

    // The render side's own memory: the sparkline ring and the trade flash.
    // Deliberately here and never a DisplaySnapshot field — see ladder_render.hpp.
    LadderView view_{};

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

    // The panel's own cost, measured rather than assumed: the slowest single
    // paint, and the draw count at the previous statistics block so the rate is
    // a rate. worst_paint_us_ is the number that says whether a 30 fps target
    // has headroom — at 33 ms a period, anything approaching 33,000 has not.
    // Its own window clock rather than print_stall()'s: that one is re-stamped
    // inside print_stall(), which runs first, so sharing it would report every
    // panel window as zero milliseconds long and every draw rate as zero.
    std::uint32_t worst_paint_us_ = 0;
    std::uint32_t drawn_at_block_ = 0;
    std::int64_t panel_block_us_ = 0;

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
        // `summary` frames, whose rate is a CLOCK rather than a market
        // observation (staleness.hpp) — so its per-window fraction of 2.00/s is
        // the instantaneous drain rate the accumulated age is integrated from.
        std::uint64_t summaries = 0;
    };
    Window prev_{};
    bool have_prev_ = false;
};

}  // namespace depthcharge::fw
