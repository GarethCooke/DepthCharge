// dc_harness/replay_driver.hpp — trace -> adapter -> book -> DisplaySnapshot.
//
// This is the host stand-in for the M3 firmware feed task, and it is deliberately
// the *only* place the harness wires the engine together, so the goldens and the
// console ladder can never disagree about what a trace means.
//
//     TraceReader ──► <venue adapter> ──► FeedEvent ──► Book ──► SnapshotChannel
//                        (parse)                        (apply)     (publish)
//
// M4 STAGE B1: THERE ARE NOW TWO ADAPTERS AND THE DRIVER PICKS ONE. Until
// tonight it named AnvilAdapter and refused every other venue loudly, which was
// the right holding position while Kraken's decoder was a classifier. The
// refusal is gone; `run_replay` switches on the trace's venue tag and
// instantiates that venue's decoder. See DECODER IDENTITY on ReplayResult for
// what keeps a pinned output honest about which of the two produced it.
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
#include <depthcharge/kraken/kraken_adapter.hpp>
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
//
// M4 STAGE A RULING (2026-08-17): THE THRESHOLD IS SELF-CALIBRATING, AND IT IS
// ARMED ON THE VENUE'S LIVENESS SIGNAL — NOT ON BOOK EVENTS AND NOT ON A
// CONSTANT.
//
// The paragraph below records the previous step and is left standing, because
// it is right about the problem and wrong only about the fix. What changed:
// a second quiet-pair window measured a HEALTHY 25,843 ms book silence, 1.72x
// the 15,000 ms this driver had just declared. No threshold on book silence can
// be correct — a quiet market and a dead subscription are identical on the wire.
// So the driver now watches the venue's liveness signal (`summary` at Anvil,
// `heartbeat` at Kraken) and derives its own threshold from that signal's
// observed median (liveness_clock.hpp). `disconnect_gap_ms` becomes an OVERRIDE
// rather than the policy: 0 means "calibrate", which is the default.
//
// M4 STAGE A: THE THRESHOLD IS VENUE-DECLARED, AND IT HAD TO BE.
//
// Everything above is Anvil's derivation and stays true of Anvil. It is false
// of Kraken by a factor of nine: that venue publishes on change, its quiet pair
// is legitimately silent for 9,007 ms with a p90 of 8,480 ms, and the M1 rule
// applied to a Kraken trace synthesises disconnects that never happened —
// which, pinned into a golden, would become truth. Measured on the committed
// slices: the 1000 ms rule fires 25 times on the quiet pair in 60 s and 5-7
// times on BTC/USD, every one of them spurious.
//
// So the number now comes from `venue_traits(v).stale_gap_ms` (venue.hpp),
// which is the same source of truth the metadata tag selects. Anvil's row IS
// 1000 ms, so no committed golden moves; the constant simply stopped being
// written here. `for_venue()` is how a caller that has read a trace asks for
// the right one.
//
// The firmware half of this is stage B's — `kRxWatchdogMs` is untouched. This
// is decision 2 (ARCHITECTURE §9, 2026-08-17) arriving in the harness first,
// which is the cheapest place to find out whether the split clock was right.
struct ReplayOptions {
    // 0 = calibrate from the trace's own liveness signal (the ruling's rule).
    // A non-zero value is an explicit override and is what every committed
    // golden passes, so no golden's meaning depends on the calibration.
    double disconnect_gap_ms = 0.0;

    // Cap on frames processed (0 = whole trace). Used by `dc_ladder --at`.
    std::size_t max_frames = 0;

    // How long the stream stayed silent AFTER the last frame, in milliseconds.
    //
    // A trace file has no "now", so trailing silence is invisible to a replay:
    // the watchdog above is edge-triggered by the arrival of the next frame, and
    // a capture that ended with thirty seconds of nothing still replays as Live
    // to the last frame. On the panel the watchdog is a timer and would have
    // fired. Setting this tells the replay how long the silence actually lasted,
    // so the same rule applies at the end of the trace as in the middle of it.
    //
    // 0 (the default) keeps the historical behaviour, and therefore every
    // committed golden. Nothing infers it: the capture tool does not record when
    // it stopped listening, so a caller that knows must say.
    double end_of_trace_silence_ms = 0.0;

    // The M1 constant, for a caller that deliberately wants the pre-ruling
    // behaviour — the committed goldens do, so that what they pin is a fixed
    // rule rather than a calibration that could drift with a re-capture.
    static ReplayOptions legacy_anvil() noexcept {
        ReplayOptions o;
        o.disconnect_gap_ms = venue_traits(Venue::Anvil).legacy_book_threshold_ms;
        return o;
    }
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

    // ---- DECODER IDENTITY (M4 triage item 1) ------------------------------
    // WHICH ADAPTER PRODUCED EVERYTHING BELOW. Empty only for a result nobody
    // ran.
    //
    // The hole this closes opened the moment a second adapter linked: two
    // decoders, one reader, and a pinned output that named neither. A golden
    // full of Kraken numbers would still be a golden full of numbers if the
    // dispatch sent that trace to the Anvil parser — it would simply pin a
    // different set, and the first person to look would be reading a report of
    // what the WRONG decoder did to the right file.
    //
    // Taken from `Decoder::name()`, which is derived from the decoder's own
    // `kVenue` (trace_decoder.hpp) rather than spelled as a literal, so it
    // cannot be right while the dispatch is wrong. The mutation that proves it
    // is `test_kraken_goldens.cpp`'s "the dispatch is pinned by identity" —
    // swap the two decoder cases in run_replay and that case goes red, which is
    // the standing condition of ARCHITECTURE §9, 2026-08-18.
    std::string decoder;

    std::size_t frames = 0;
    std::size_t events = 0;

    // Per-venue adapter statistics. TWO FIELDS RATHER THAN A VARIANT, and the
    // reason is the goldens: Anvil's numbers have been pinned since M1 and a
    // sum type would have moved every one of those call sites to prove nothing.
    // Exactly one of the two is populated, and `decoder` says which.
    depthcharge::anvil::AnvilAdapter::Stats adapter;
    depthcharge::kraken::KrakenAdapter::Stats kraken;

    depthcharge::Book::Stats book;
    std::vector<StaleEpisode> episodes;

    // State at the end of the replay, and at the first Gap (empty if none) —
    // the two ladders `dc_ladder` prints and the goldens pin.
    depthcharge::DisplaySnapshot final_snapshot{};
    depthcharge::DisplaySnapshot first_stale_snapshot{};
    bool saw_stale = false;

    // The threshold actually in force at the end of the run — the caller's
    // override, or whatever the venue's liveness signal calibrated to. Reported
    // so a tool never has to guess which of the two produced its episodes.
    double threshold_ms = 0.0;

    // ---- THE AGE METER (M4 stage A2) --------------------------------------
    // The book's estimated queuing lag. `final_snapshot.age_ms` is the reading
    // at the end of the run; these two are what a whole-run report needs and a
    // single snapshot cannot carry.
    //
    // `worst_age_ms` survives reconnects deliberately — the per-connection peak
    // is destroyed every time the socket blinks, which is how the 86-minute run
    // of 2026-08-09 looked healthy.
    //
    // TWO CADENCES, AND THEY ARE NOT THE SAME NUMBER. `liveness_median_ms` is
    // the ROLLING median the grey threshold is derived from, at the end of the
    // run. `age_baseline_ms` is the interval the age was measured against —
    // latched from the first calibrated window of the last connection and frozen
    // there, because a fresh socket's send queue is empty and that is the only
    // moment a single client can measure the venue's clock. On a healthy trace
    // they agree; where they DISAGREE the socket's cadence has moved since it
    // connected, which is the whole signal (age_estimator.hpp).
    std::uint32_t worst_age_ms = 0;
    double age_baseline_ms = 0.0;
    double liveness_median_ms = 0.0;
    std::size_t liveness_arrivals = 0;

    std::int64_t first_rx_ns = 0;
    std::int64_t last_rx_ns = 0;
    double span_seconds() const {
        return static_cast<double>(last_rx_ns - first_rx_ns) / 1e9;
    }
};

// Called after every publish. Return false to stop the replay early.
//
// LIFETIME: the observer is borrowed for the duration of the run_replay call
// and never stored beyond it. It is deliberately taken by const reference rather
// than by value — a std::function copy is a heap allocation, and this object is
// documented above as the host stand-in for the M3 firmware feed task, where
// that is exactly the thing invariant #7 forbids.
using ReplayObserver = std::function<bool(const ReplayStep&, const depthcharge::DisplaySnapshot&)>;

// The one rule for turning trace metadata into a SymbolSpec.
//
// dc_ladder derived the id from the trace while the goldens passed
// kAnvilTicker101 wholesale, so the same trace could in principle be replayed
// two ways. The id comes from the metadata when it has one; the scale does not,
// because Anvil publishes no tick metadata and DepthCharge declares it
// (ARCHITECTURE §4) — a trace for another ticker is decoded at the declared
// scale and fails loudly on the first price that will not fit, which is the
// intended behaviour and not a silent re-interpretation.
//
// M4 STAGE B1: IT IS NOW VENUE-AWARE, AND IT WAS A LATENT WRONG ANSWER UNTIL IT
// WAS. The function returned Anvil's declared scale for EVERY trace, and
// `run_replay`'s refusal was the only thing keeping that from being reached: with
// the guard off, a Kraken trace decoded at Anvil's 10^-4 price scale and 10^0 qty
// scale would put BTC/USD's `0.65540712` through a scale that cannot hold it —
// a loud BadQty on every level, which is the good case — and MINA/GBP's 4-decimal
// prices through a scale that CAN, which is the bad one: a ladder that parses
// clean and is wrong by a factor of 10^8 in size. That is the shape of failure
// invariant #5 exists to forbid, and it was one deleted `throw` away.
//
// A venue whose symbol this build declares no scale for throws rather than
// guessing. There is no defensible default: a wrong scale does not fail, it
// draws.
depthcharge::SymbolSpec symbol_for(const TraceMeta& meta);

ReplayResult run_replay(TraceReader& reader, const depthcharge::SymbolSpec& symbol,
                        const ReplayOptions& opts, const ReplayObserver& observer = {});

ReplayResult run_replay_file(const std::string& path, const depthcharge::SymbolSpec& symbol,
                             const ReplayOptions& opts,
                             const ReplayObserver& observer = {});

ReplayResult run_replay_text(std::string_view trace_text, const depthcharge::SymbolSpec& symbol,
                             const ReplayOptions& opts,
                             const ReplayObserver& observer = {});

}  // namespace dc::harness
