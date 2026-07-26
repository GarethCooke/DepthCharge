// dc_harness/replay_driver.hpp — trace -> adapter -> book -> DisplaySnapshot.
//
// This is the host stand-in for the M3 firmware feed task, and it is deliberately
// the *only* place the harness wires the engine together, so the goldens and the
// console ladder can never disagree about what a trace means.
//
//     TraceReader ──► AnvilAdapter ──► FeedEvent ──► Book ──► SnapshotChannel
//                       (parse)                     (adopt)     (publish)
//
// The one rule that is not mechanical is how a *transport* gap is recognised in
// a file that has no disconnect marker — see DISCONNECT DETECTION below.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <depthcharge/anvil/anvil_adapter.hpp>
#include <depthcharge/book.hpp>
#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/snapshot_channel.hpp>
#include <depthcharge/symbol.hpp>

#include "dc_harness/trace.hpp"

namespace dc::harness {

// DISCONNECT DETECTION (M1 decision; previews the M3 transport contract).
//
// The M0 reconnect capture holds no explicit marker: the client dropped the
// socket, waited, reconnected, and the only trace of it is a 4.47 s hole in
// rx_ns followed by a fresh snapshot. Two rules were available:
//
//   (a) "a mid-stream snapshot means we reconnected" — rejected. It is
//       retrospective: the Gap would be raised in the same breath as the
//       Snapshot that clears it, so the book would never actually be stale, and
//       the invariant-5 proof would be vacuous. It is also Anvil-specific
//       (Kraken sends snapshots for other reasons).
//   (b) "no data for longer than the venue can healthily be silent" — chosen.
//
// So: an rx_ns hole longer than `disconnect_gap_ms` raises Gap{Disconnect},
// timestamped at the moment the watchdog would have fired (prev_rx + threshold),
// not when the next frame eventually arrived. That timestamp is what makes the
// stale window real — 3.47 s of it in the reconnect trace — instead of an
// instant blip between two frames.
//
// The 1000 ms default is measured, not guessed: across both 5-minute local
// captures (4658 + 1836 frames) the largest healthy inter-frame gap is 640 ms,
// against a ~70 ms median at Anvil's ~15 frames/s. 1000 ms sits 1.6x above the
// worst healthy silence and 4.5x below the observed drop.
//
// This is exactly the rule the M3 firmware net task will implement as an RX
// watchdog alongside the real socket-close callback (which the host has no
// equivalent of); keeping the same number in both places is what makes the
// replay a preview rather than an analogy.
struct ReplayOptions {
    double disconnect_gap_ms = 1000.0;

    // Cap on frames processed (0 = whole trace). Used by `dc_ladder --at`.
    std::size_t max_frames = 0;
};

// One stale window, opened by a Gap and closed by the Snapshot that re-baselined
// the book. This is the invariant-5 evidence the M1 goldens assert on.
//
// An episode spans the whole grey period, not one watchdog firing. A second hole
// while the panel is already grey is the same outage continuing — the book has
// not been re-baselined in between — so it folds into the open episode and bumps
// `gap_events`. Opening a second episode instead would leave the first one
// permanently "uncleared" and measure `stale_ms` from the wrong start.
struct StaleEpisode {
    depthcharge::Seq gap_seq = 0;         // synthesised seq of the first Gap event
    depthcharge::GapReason reason{};
    std::size_t frame_before = 0;         // last frame received before the hole
    std::size_t cleared_frame = 0;        // frame whose Snapshot cleared it (0 = never)
    std::size_t gap_events = 0;           // watchdog firings folded into this window
    std::int64_t watchdog_rx_ns = 0;      // when the gap was raised (virtual time)
    std::int64_t cleared_rx_ns = 0;
    double observed_gap_ms = 0.0;         // the largest hole in the capture
    double stale_ms = 0.0;                // how long the panel would have been grey
    bool cleared = false;
};

// Context handed to the observer with every published snapshot.
struct ReplayStep {
    std::size_t frame_index = 0;   // 0 for a synthesised Gap (no frame arrived)
    std::size_t event_index = 0;   // 1-based count of FeedEvents emitted
    std::int64_t rx_ns = 0;        // frame arrival, or watchdog expiry for a Gap
    depthcharge::FeedEvent::Kind kind{};
};

struct ReplayResult {
    TraceMeta meta;
    std::size_t frames = 0;
    std::size_t events = 0;
    depthcharge::anvil::AnvilAdapter::Stats adapter;
    depthcharge::Book::Stats book;
    std::vector<StaleEpisode> episodes;

    // State at the end of the replay, and at the first Gap (empty if none) —
    // the two ladders `dc_ladder` prints and the goldens pin.
    depthcharge::DisplaySnapshot final_snapshot{};
    depthcharge::DisplaySnapshot first_stale_snapshot{};
    bool saw_stale = false;

    std::int64_t first_rx_ns = 0;
    std::int64_t last_rx_ns = 0;
    double span_seconds() const {
        return static_cast<double>(last_rx_ns - first_rx_ns) / 1e9;
    }
};

// Called after every publish. Return false to stop the replay early.
using ReplayObserver = std::function<bool(const ReplayStep&, const depthcharge::DisplaySnapshot&)>;

ReplayResult run_replay(TraceReader& reader, const depthcharge::SymbolSpec& symbol,
                        const ReplayOptions& opts, const ReplayObserver& observer = {});

ReplayResult run_replay_file(const std::string& path, const depthcharge::SymbolSpec& symbol,
                             const ReplayOptions& opts,
                             const ReplayObserver& observer = {});

ReplayResult run_replay_text(std::string_view trace_text, const depthcharge::SymbolSpec& symbol,
                             const ReplayOptions& opts,
                             const ReplayObserver& observer = {});

}  // namespace dc::harness
