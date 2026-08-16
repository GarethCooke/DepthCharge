// firmware/src/stall_probe.hpp — which half of the board a >1 s book-hole came from.
//
// WHAT THE PREVIOUS INSTRUMENT COULD NOT SAY.
//
// gap_histogram.hpp answered "how often" and "how big". It could not answer the
// only question that chooses the next fix: when the board falls behind, is it
// BOARD-BOUND (Core 0 cannot drain the stream) or LINK-BOUND (the frames are not
// arriving over the air)? Those are not the same as the arrival-vs-event split,
// and reading them off that split is the trap this file exists to close —
//
//   THE ARRIVAL STAMP IS NOT THE WIRE. `FramePipeStats::arrival_gaps` is stamped
//   in `WsTransport::on_event`, i.e. inside esp_websocket_client's own task,
//   after the Wi-Fi driver, lwIP, the socket read, the TLS record decrypt and
//   the esp_event dispatch hop have all happened. A hole in `arrive` therefore
//   means "no complete message was handed to our callback for a second" — which
//   is equally consistent with a dry socket AND with that task not being
//   scheduled, or lwIP not being drained, on a busy Core 0. `arrive` and `event`
//   filling together does NOT prove transport; it only excludes the FramePipe
//   queue hop and the feed task's own work, which is the one stretch the stamp
//   is downstream of.
//
// So the verdict needs a signal from outside the data path entirely: how much
// idle time each core actually had while the panel had no book.
//
// THE THREE SIGNALS, AND WHAT EACH ONE DECIDES.
//
//   1. Per-core idle across the hole window — the verdict.
//      Core 0 starved (idle far below its own healthy baseline) => the board
//      could not drain the stream => board-bound, firmware-fixable.
//      Core 0 idle (at or above baseline) => it was waiting on data that was not
//      there => link-bound, and no firmware lever reaches it.
//      This is a DIFFERENT claim from `a->e <= 8 ms`: that says one frame's
//      book-apply is cheap; this asks whether aggregate Core-0 headroom (TLS
//      decrypt + the esp_event hop + the Wi-Fi/lwIP tasks) was exhausted.
//
//   2. Recovery shape — what ended the hole.
//      A tight burst of arrivals with the wire `seq` catching up = the frames
//      existed and were delivered late (a TCP backlog flush). A single frame at
//      the usual cadence, carrying a `seq` that has already moved on = Anvil
//      coalesced the middle away upstream and the board never had them.
//      Crossed with (1): burst + Core-0 idle = the link delayed them; burst +
//      Core-0 pegged = they sat unread in the socket buffer because Core 0 was
//      busy; no burst = shed upstream, which for an ~80 ms republisher still
//      traces back to this socket backing up (ARCHITECTURE §7, strain 10).
//
//   3. rssi at the hole — so a link verdict can be checked against signal.
//
// WHAT THIS FILE IS AND IS NOT. It is arithmetic and bookkeeping: fixed storage,
// no allocation (invariant #7), no ESP-IDF, and therefore host-testable — the
// same rule frame_reassembler.hpp and gap_histogram.hpp are written to, and for
// the same reason. The failure mode of a classifier is not a crash; it is a
// plausible verdict pointing at the wrong half of the board, which costs a bench
// evening and a wrong fix. `harness/tests/test_stall_probe.cpp` is the proof.
//
// The ESP-IDF half — reading each core's idle time — is `core_idle.*`, which is
// where the one platform claim lives.
//
// NOTHING HERE MAY BRANCH ANY BEHAVIOUR. Same rule as the histograms: these are
// diagnostics, written by the feed task and read by the console across cores
// with no synchronisation, and a stale read costs one wrong log line.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "gap_histogram.hpp"

namespace depthcharge::fw {

// A "hole" is exactly what the gap histograms' >1 s buckets count, and the
// threshold is taken FROM them rather than restated. That is deliberate: two
// independent instruments then have to agree, so `holes n=` on the console must
// equal `event >1s=` on the line above it, and a disagreement is a bug in one of
// them rather than something a reader has to reconcile at the bench.
inline constexpr std::uint32_t kHoleThresholdUs = GapScale::kEdgeUs[GapScale::kFirstLong - 1];

// --- cycle-counter arithmetic ------------------------------------------------

// The one subtraction the idle probe depends on. Each core's cycle counter is
// 32 bits and wraps every ~17.9 s at 240 MHz, so a plain `now - before` on
// signed or wider types would occasionally produce a hole-sized garbage figure
// exactly when the instrument is being read. Unsigned wrap-around is the correct
// arithmetic and is valid for any interval shorter than the wrap period.
inline std::uint32_t wrapping_delta_u32(std::uint32_t now, std::uint32_t before) noexcept {
    return static_cast<std::uint32_t>(now - before);
}

// How far apart two consecutive passes through the idle hook may be and still
// count as one continuous stretch of idle.
//
// The hook is called in a tight loop while its core has nothing to run (see
// core_idle.hpp for why that loop exists), so consecutive passes are a fraction
// of a microsecond apart; the interval that follows a real task running is
// milliseconds. 100 us sits two orders of magnitude above the first and two
// below the second. The cost of the rule is that a busy period SHORTER than this
// is counted as idle, which biases the reading toward "idle" — the conservative
// direction for a starvation claim, because it makes "board-bound" harder to
// conclude rather than easier.
inline constexpr std::uint32_t kIdleContinuityUs = 100;

// One core's idle time, accumulated from inside that core's idle task.
//
// Why not FreeRTOS run-time stats, which is what this measurement is normally
// for: `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS is not set` in the precompiled
// Arduino framework (checked in the shipped sdkconfig, alongside
// `CONFIG_FREERTOS_USE_TRACE_FACILITY is not set`), and it is a compile-time
// switch baked into `libfreertos.a` — the fourth time this vintage has decided a
// design here, after the certificate bundle, heap tracing and
// `reconnect_timeout_ms`. Defining it in our own flags would produce a TCB
// layout disagreeing with the archive's, which is worse than not measuring.
//
// So idle is measured directly, in microseconds, by differencing the core's own
// cycle counter across passes. `idle_us_` is 32 bits and wraps every ~71
// minutes; every consumer differences it with wrapping_delta_u32 over windows of
// at most a few seconds, so the wrap is invisible. Keeping it 32-bit is what
// makes the cross-core read stale-but-never-torn.
class IdleAccumulator {
public:
    // `cpu_mhz` converts cycles to microseconds. Read from the running system
    // rather than assumed (main.cpp passes getCpuFrequencyMhz()), because a
    // wrong constant here scales every idle figure silently.
    void configure(std::uint32_t cpu_mhz) noexcept { mhz_ = (cpu_mhz == 0) ? 1u : cpu_mhz; }

    std::uint32_t cpu_mhz() const noexcept { return mhz_; }
    std::uint32_t continuity_cycles() const noexcept { return kIdleContinuityUs * mhz_; }

    // One pass through the idle hook, carrying that core's cycle count.
    //
    // The remainder is carried rather than divided away: at 240 MHz a pass is a
    // few hundred cycles, so dividing each one by 240 in isolation would floor
    // most of them to zero and report a fully idle core as fully busy.
    void note_pass(std::uint32_t now_cycles) noexcept {
        const std::uint32_t delta = wrapping_delta_u32(now_cycles, last_cycles_);
        last_cycles_ = now_cycles;
        ++passes_;
        if (delta > continuity_cycles()) {
            // Either the first pass ever, or the core has just come back to idle
            // after running something. Either way the interval is not idle time
            // and is dropped, not accumulated.
            ++resumes_;
            return;
        }
        if (delta > worst_pass_cycles_) { worst_pass_cycles_ = delta; }
        rem_cycles_ += delta;
        if (rem_cycles_ >= mhz_) {
            idle_us_ += rem_cycles_ / mhz_;
            rem_cycles_ %= mhz_;
        }
    }

    // Free-running, wraps at ~71 minutes; difference it, never read it absolute.
    std::uint32_t idle_us() const noexcept { return idle_us_; }
    std::uint32_t passes() const noexcept { return passes_; }
    std::uint32_t resumes() const noexcept { return resumes_; }
    // The longest interval ever ACCEPTED as idle, in cycles. Printed so the
    // bench can see the loop period the continuity rule is separating against;
    // if this ever approaches continuity_cycles() the rule is at its limit and
    // the threshold, not the verdict, is what needs revisiting.
    std::uint32_t worst_pass_cycles() const noexcept { return worst_pass_cycles_; }

private:
    std::uint32_t mhz_ = 240;
    std::uint32_t last_cycles_ = 0;
    std::uint32_t rem_cycles_ = 0;
    std::uint32_t idle_us_ = 0;
    std::uint32_t passes_ = 0;
    std::uint32_t resumes_ = 0;
    std::uint32_t worst_pass_cycles_ = 0;
};

// Percentage of a window spent idle, saturating at 100.
//
// One function for both the per-hole figures (32-bit microseconds) and the
// running baseline sums (64-bit), rather than one of each: they are the same
// arithmetic and the two ways to get it wrong are the same both times —
// overflowing the multiply on a multi-second window, and reporting more than
// 100% when the continuity rule has over-counted by a few microseconds. Either
// produces a number that looks exactly like a verdict.
inline std::uint8_t idle_percent(std::uint64_t idle_us, std::uint64_t window_us) noexcept {
    if (window_us == 0) { return 0; }
    const std::uint64_t pct = (idle_us * 100u) / window_us;
    return (pct > 100u) ? 100u : static_cast<std::uint8_t>(pct);
}

// --- the link's own signal ---------------------------------------------------

// rssi through the run, so a link verdict can be checked against signal rather
// than asserted. The banner already prints it once at association; a hole two
// hundred seconds later needs a number from near that hole.
//
// Written by whichever task owns the Wi-Fi driver call (loopTask, in
// `WsTransport::supervise`) and read by the feed task and the console. One
// writer, and the fields are 8- and 32-bit, so the cross-core read is stale at
// worst — the same trade as every other counter here.
//
// The reading to hold in mind: -69 dBm is fine-ish, so second-long holes at that
// level point at airtime or congestion rather than at raw weak signal, and a
// link verdict that is NOT accompanied by a poor rssi is a statement about the
// channel, not about the antenna.
class LinkQuality {
public:
    void note(std::int8_t dbm) noexcept {
        if (dbm == 0) { return; }  // the driver's "not associated" answer
        last_dbm_ = dbm;
        if (samples_ == 0 || dbm < min_dbm_) { min_dbm_ = dbm; }
        if (samples_ == 0 || dbm > max_dbm_) { max_dbm_ = dbm; }
        ++samples_;
    }

    std::int8_t last_dbm() const noexcept { return last_dbm_; }
    std::int8_t min_dbm() const noexcept { return min_dbm_; }
    std::int8_t max_dbm() const noexcept { return max_dbm_; }
    std::uint32_t samples() const noexcept { return samples_; }

private:
    std::int8_t last_dbm_ = 0;
    std::int8_t min_dbm_ = 0;
    std::int8_t max_dbm_ = 0;
    std::uint32_t samples_ = 0;
};

// --- the classification ------------------------------------------------------

enum class HoleVerdict : std::uint8_t {
    Unknown,     // not yet classified, or no idle measurement available
    BoardBound,  // Core 0 was starved through the hole
    LinkBound,   // Core 0 had headroom and nothing to do
    Mixed,       // between the two — reported as such rather than rounded
};

enum class RecoveryShape : std::uint8_t {
    Unknown,  // too few messages after the hole to say
    Burst,    // several arrivals back-to-back: frames existed, delivered late
    Cadence,  // the ~80 ms republish simply resumed: the middle was never sent
};

// Absolute rails. A core below the first had no headroom whatever its baseline;
// one above the second plainly had headroom to spare.
inline constexpr std::uint8_t kStarvedIdlePct = 25;
inline constexpr std::uint8_t kPlentyIdlePct = 75;
// And the relative rule between them: a hole this many points below the run's
// own healthy-window idle is the core doing materially more work than usual,
// which is the signature of a drain that has fallen behind.
inline constexpr std::uint8_t kIdleDropPct = 15;

// Two arrivals this close together are a backlog flushing, not Anvil's ~80 ms
// republish cadence resuming. 25 ms is under a third of the cadence and well
// over the 3-4 ms a chunked message takes to land.
inline constexpr std::uint32_t kBurstGapUs = 25000;
inline constexpr std::uint8_t kBurstMinSamples = 2;

// How many message arrivals after a hole define its recovery shape, and how many
// ring slots the hole log has. Both are fixed storage (invariant #7). One slot
// is always the open record's, so the log retains kHoleLogDepth - 1 finalised
// holes — seven, which covers several stats blocks at the observed rate of ~2
// holes a minute, and the console drains it one line at a time long before that.
inline constexpr std::size_t kRecoverySamples = 4;
inline constexpr std::size_t kHoleLogDepth = 8;

// One >1 s book-silence, with everything needed to argue about it later.
struct HoleRecord {
    std::uint32_t ordinal = 0;   // 1-based, across the whole run
    std::uint32_t gap_ms = 0;    // the silence itself
    std::uint8_t core0_idle_pct = 0;
    std::uint8_t core1_idle_pct = 0;
    std::uint8_t baseline0_pct = 0;  // the healthy-window idle it is judged against
    std::int8_t rssi_dbm = 0;
    bool socket_dropped = false;  // the transport reported the socket down in this hole
    bool idle_valid = false;      // false when the idle probe is not running

    // Wire seq, which separates "delivered late" from "shed upstream". Anvil's
    // seq is one GLOBAL counter across tickers and frame types (ARCHITECTURE
    // §4), so it is useless for ordering — but its RATE is a clock, and that is
    // exactly what is needed here: if the frame that ends the hole carries a seq
    // that has advanced by roughly rate x hole, it is a fresh frame and the
    // middle was never sent to us; if it has advanced by about one step, it is
    // an old frame that was sitting somewhere.
    std::int32_t seq_step = 0;      // advance across the hole
    std::int32_t seq_expected = 0;  // what the run's own seq rate predicts for a fresh frame

    std::uint8_t recovery_n = 0;
    std::uint32_t recovery_us[kRecoverySamples]{};

    HoleVerdict verdict = HoleVerdict::Unknown;
    RecoveryShape shape = RecoveryShape::Unknown;
};

inline const char* verdict_name(HoleVerdict v) noexcept {
    switch (v) {
        case HoleVerdict::BoardBound: return "BOARD-BOUND";
        case HoleVerdict::LinkBound:  return "LINK-BOUND";
        case HoleVerdict::Mixed:      return "mixed";
        case HoleVerdict::Unknown:    break;
    }
    return "unknown";
}

inline const char* shape_name(RecoveryShape s) noexcept {
    switch (s) {
        case RecoveryShape::Burst:   return "burst";
        case RecoveryShape::Cadence: return "cadence";
        case RecoveryShape::Unknown: break;
    }
    return "unknown";
}

// The verdict rule, as one function so it can be tested at its boundaries rather
// than inspected inside a longer method.
//
// Order matters and is not arbitrary: the absolute rails come first because they
// are true regardless of what the run's baseline turned out to be — a core at 9%
// idle had no headroom even if it never has much — and the relative rule then
// resolves the band between them against the workload actually observed. A run
// with no healthy baseline yet (the first hole of a run that started inside one)
// gets the rails only, and Mixed otherwise, which is the honest answer.
inline HoleVerdict classify_verdict(std::uint8_t core0_idle_pct, std::uint8_t baseline_pct,
                                    bool have_baseline) noexcept {
    if (core0_idle_pct <= kStarvedIdlePct) { return HoleVerdict::BoardBound; }
    if (core0_idle_pct >= kPlentyIdlePct) { return HoleVerdict::LinkBound; }
    if (!have_baseline) { return HoleVerdict::Mixed; }
    if (core0_idle_pct + kIdleDropPct <= baseline_pct) { return HoleVerdict::BoardBound; }
    if (core0_idle_pct >= baseline_pct) { return HoleVerdict::LinkBound; }
    return HoleVerdict::Mixed;
}

inline RecoveryShape classify_shape(const std::uint32_t* recovery_us, std::uint8_t n) noexcept {
    if (n < kBurstMinSamples) { return RecoveryShape::Unknown; }
    std::uint8_t tight = 0;
    for (std::uint8_t i = 0; i < n; ++i) {
        if (recovery_us[i] <= kBurstGapUs) { ++tight; }
    }
    return (tight >= kBurstMinSamples) ? RecoveryShape::Burst : RecoveryShape::Cadence;
}

// The bookkeeping around those two rules: healthy-window baseline, the run's
// wire-seq rate, the ring of records, and the tally the console prints.
//
// SINGLE WRITER — every mutating call is made from the Core 0 feed task, which
// is the same task that owns the book (invariant #8). The console only reads.
class StallProbe {
public:
    // One event reached the book, `gap_us` after the previous one, with the idle
    // time both cores had over exactly that window.
    //
    // Called for EVERY event, not only the long ones: the sub-threshold windows
    // are what build the healthy baseline the verdict is measured against, and a
    // baseline sampled only when things are already wrong is not a baseline.
    void note_event(std::uint32_t gap_us, std::uint32_t idle0_us, std::uint32_t idle1_us,
                    bool idle_valid, std::int64_t wire_seq, std::int8_t rssi_dbm,
                    bool socket_dropped) noexcept {
        const std::int64_t prev_seq = last_seq_;
        const bool had_seq = have_seq_;
        // What a FRESH frame ending this window would have carried, priced off
        // the rate as it stood BEFORE this sample. Taken here rather than at the
        // hole record below because folding the hole's own second into the rate
        // that predicts it drags the prediction toward the observation — a hole
        // whose step is genuinely stale would then look less stale the longer it
        // was, which is backwards.
        const std::int64_t seq_expected = expected_seq_advance(gap_us);

        // The seq clock. Backward steps are Anvil's global counter being global
        // (M0: 42 in five minutes) and are dropped rather than subtracted, so
        // the rate stays a rate.
        if (had_seq && wire_seq > prev_seq) {
            seq_advance_ += static_cast<std::uint64_t>(wire_seq - prev_seq);
            seq_elapsed_us_ += gap_us;
        }
        last_seq_ = wire_seq;
        have_seq_ = true;

        if (gap_us < kHoleThresholdUs) {
            if (idle_valid) {
                baseline_us_ += gap_us;
                baseline_idle0_us_ += idle0_us;
                baseline_idle1_us_ += idle1_us;
                ++baseline_windows_;
                // Two 64-bit divides per healthy event — ~26 a second at
                // Anvil's cadence, against the ~3,000 the parser's qty scaling
                // already does, so the cost is noted rather than optimised. The
                // alternative (recompute only when a hole opens) would leave the
                // console printing 0% on a run that has not stalled yet, which
                // is the number it most needs to be able to trust.
                baseline0_pct_ = idle_percent(baseline_idle0_us_, baseline_us_);
                baseline1_pct_ = idle_percent(baseline_idle1_us_, baseline_us_);
            }
            return;
        }

        // A hole. Close whatever was still collecting recovery samples first —
        // one hole per record, and a recovery that was cut short by the next
        // hole is finalised with what it has rather than left dangling.
        finalise_open();

        HoleRecord& r = ring_[completed_ % kHoleLogDepth];
        r = HoleRecord{};
        r.ordinal = ++holes_;
        r.gap_ms = gap_us / kUsPerMs;
        r.idle_valid = idle_valid;
        r.core0_idle_pct = idle_valid ? idle_percent(idle0_us, gap_us) : 0u;
        r.core1_idle_pct = idle_valid ? idle_percent(idle1_us, gap_us) : 0u;
        r.baseline0_pct = baseline0_pct_;
        r.rssi_dbm = rssi_dbm;
        r.socket_dropped = socket_dropped;
        r.seq_step = clamp_i32(had_seq ? (wire_seq - prev_seq) : 0);
        r.seq_expected = clamp_i32(seq_expected);
        open_ = true;
    }

    // A whole message came off the socket, `since_prev_us` after the previous
    // one. Ignored unless a hole is collecting its recovery shape.
    //
    // Measured from the ARRIVAL stamps, not from when the feed task got to them:
    // the question is whether the frames came back in a burst on the wire, and
    // the feed task's own lateness would smear exactly that. The caveat is that
    // a message dropped for want of a slot never reaches this call, so on a run
    // with `no_slot` non-zero the recovery series has holes in it — which is why
    // `no_slot` is on the checklist for this run.
    void note_message(std::uint32_t since_prev_us) noexcept {
        if (!open_) { return; }
        HoleRecord& r = ring_[completed_ % kHoleLogDepth];
        if (r.recovery_n < kRecoverySamples) {
            r.recovery_us[r.recovery_n] = since_prev_us;
            ++r.recovery_n;
        }
        if (r.recovery_n >= kRecoverySamples) { finalise_open(); }
    }

    // --- what the console reads ---------------------------------------------

    std::uint32_t holes() const noexcept { return holes_; }
    std::uint32_t board_bound() const noexcept { return board_bound_; }
    std::uint32_t link_bound() const noexcept { return link_bound_; }
    std::uint32_t mixed() const noexcept { return mixed_; }
    std::uint32_t unknown() const noexcept { return unknown_; }
    std::uint32_t bursts() const noexcept { return bursts_; }
    std::uint32_t cadences() const noexcept { return cadences_; }
    std::uint8_t baseline0_pct() const noexcept { return baseline0_pct_; }
    std::uint8_t baseline1_pct() const noexcept { return baseline1_pct_; }
    std::uint32_t baseline_windows() const noexcept { return baseline_windows_; }

    // Finalised records, oldest retained first. `completed()` only counts
    // records that have been classified, so a record still gathering its
    // recovery samples is never printed half-formed.
    std::uint32_t completed() const noexcept { return completed_; }

    // kHoleLogDepth slots, but only kHoleLogDepth - 1 finalised records: the
    // remaining slot is where the OPEN record lives while it gathers its
    // recovery series. Retaining the full depth was the first version of this
    // and it was wrong in the way that matters — the moment a new hole opened,
    // the oldest "retained" index pointed at the slot that hole was being
    // written into, so the console could print an unclassified record as though
    // it were the oldest finalised one. Found by the property test rather than
    // by reading, which is the argument for having it.
    std::uint32_t oldest_retained() const noexcept {
        constexpr std::uint32_t kRetained = static_cast<std::uint32_t>(kHoleLogDepth) - 1u;
        return (completed_ > kRetained) ? (completed_ - kRetained) : 0u;
    }
    // `index` is a 0-based position in the completed sequence; null once evicted.
    //
    // The pointer is into the live ring, so a reader on the other core is racing
    // a writer that would have to produce kHoleLogDepth holes between two polls
    // to catch it — twice a minute against a 20 ms poll. Accepted, and the same
    // trade as every other counter here: the cost is one garbled log line, and
    // the console reports the eviction count so a reader is never quietly short
    // of a verdict.
    const HoleRecord* completed_at(std::uint32_t index) const noexcept {
        if (index >= completed_ || index < oldest_retained()) { return nullptr; }
        return &ring_[index % kHoleLogDepth];
    }

    // Anvil's global seq advance per second, as this socket sees it. Printed
    // because it is the denominator of every `seq_expected` and a reader should
    // not have to trust a derived number without it.
    std::uint32_t seq_per_s() const noexcept {
        if (seq_elapsed_us_ == 0) { return 0; }
        return static_cast<std::uint32_t>((seq_advance_ * 1000000ull) / seq_elapsed_us_);
    }

    // "n=17 board=12 link=3 mixed=2 unknown=0 | burst=10 cadence=7 | seq 41/s"
    std::size_t render_tally(char* out, std::size_t cap) const noexcept {
        if (out == nullptr || cap == 0) { return 0; }
        std::size_t at = 0;
        const int n = std::snprintf(
            out, cap,
            "n=%u board=%u link=%u mixed=%u unknown=%u | burst=%u cadence=%u | seq %u/s",
            static_cast<unsigned>(holes_), static_cast<unsigned>(board_bound_),
            static_cast<unsigned>(link_bound_), static_cast<unsigned>(mixed_),
            static_cast<unsigned>(unknown_), static_cast<unsigned>(bursts_),
            static_cast<unsigned>(cadences_), static_cast<unsigned>(seq_per_s()));
        (void)append_truncating(n, cap, at);
        return at;
    }

    // "#12 1240 ms c0=9%/88% c1=93% rssi=-70 seq+38 of +51 | recov 3,4,6,78 ms
    //  -> BOARD-BOUND burst"
    //
    // Every derived judgement is printed beside the numbers it came from —
    // `c0=9%/88%` is the hole against its baseline — so the verdict can be
    // re-argued from the log line alone rather than believed.
    static std::size_t render_hole(const HoleRecord& r, char* out, std::size_t cap) noexcept {
        if (out == nullptr || cap == 0) { return 0; }
        out[0] = '\0';
        std::size_t at = 0;
        int n = std::snprintf(out, cap, "#%u %u ms ", static_cast<unsigned>(r.ordinal),
                              static_cast<unsigned>(r.gap_ms));
        if (append_truncating(n, cap, at)) { return at; }
        if (r.idle_valid) {
            n = std::snprintf(out + at, cap - at, "c0=%u%%/%u%% c1=%u%% ",
                              static_cast<unsigned>(r.core0_idle_pct),
                              static_cast<unsigned>(r.baseline0_pct),
                              static_cast<unsigned>(r.core1_idle_pct));
        } else {
            n = std::snprintf(out + at, cap - at, "c0=?/? c1=? ");
        }
        if (append_truncating(n, cap, at)) { return at; }
        n = std::snprintf(out + at, cap - at, "rssi=%d seq%+d of %+d | recov ",
                          static_cast<int>(r.rssi_dbm), static_cast<int>(r.seq_step),
                          static_cast<int>(r.seq_expected));
        if (append_truncating(n, cap, at)) { return at; }
        if (r.recovery_n == 0) {
            n = std::snprintf(out + at, cap - at, "-");
            if (append_truncating(n, cap, at)) { return at; }
        }
        for (std::uint8_t i = 0; i < r.recovery_n; ++i) {
            n = std::snprintf(out + at, cap - at, "%s%u", (i == 0) ? "" : ",",
                              static_cast<unsigned>(r.recovery_us[i] / kUsPerMs));
            if (append_truncating(n, cap, at)) { return at; }
        }
        n = std::snprintf(out + at, cap - at, " ms -> %s %s%s", verdict_name(r.verdict),
                          shape_name(r.shape), r.socket_dropped ? " (socket dropped)" : "");
        (void)append_truncating(n, cap, at);
        return at;
    }

private:
    static std::int32_t clamp_i32(std::int64_t v) noexcept {
        if (v > 0x7FFFFFFFll) { return 0x7FFFFFFF; }
        if (v < -0x7FFFFFFFll) { return -0x7FFFFFFF; }
        return static_cast<std::int32_t>(v);
    }

    std::int64_t expected_seq_advance(std::uint32_t gap_us) const noexcept {
        if (seq_elapsed_us_ == 0) { return 0; }
        return static_cast<std::int64_t>((seq_advance_ * gap_us) / seq_elapsed_us_);
    }

    void finalise_open() noexcept {
        if (!open_) { return; }
        HoleRecord& r = ring_[completed_ % kHoleLogDepth];
        r.shape = classify_shape(r.recovery_us, r.recovery_n);
        r.verdict = r.idle_valid ? classify_verdict(r.core0_idle_pct, baseline0_pct_,
                                                    baseline_windows_ != 0)
                                 : HoleVerdict::Unknown;
        switch (r.verdict) {
            case HoleVerdict::BoardBound: ++board_bound_; break;
            case HoleVerdict::LinkBound:  ++link_bound_; break;
            case HoleVerdict::Mixed:      ++mixed_; break;
            case HoleVerdict::Unknown:    ++unknown_; break;
        }
        switch (r.shape) {
            case RecoveryShape::Burst:   ++bursts_; break;
            case RecoveryShape::Cadence: ++cadences_; break;
            case RecoveryShape::Unknown: break;
        }
        open_ = false;
        ++completed_;
    }

    HoleRecord ring_[kHoleLogDepth]{};
    std::uint32_t holes_ = 0;
    std::uint32_t completed_ = 0;
    bool open_ = false;

    std::uint32_t board_bound_ = 0;
    std::uint32_t link_bound_ = 0;
    std::uint32_t mixed_ = 0;
    std::uint32_t unknown_ = 0;
    std::uint32_t bursts_ = 0;
    std::uint32_t cadences_ = 0;

    std::uint64_t baseline_us_ = 0;
    std::uint64_t baseline_idle0_us_ = 0;
    std::uint64_t baseline_idle1_us_ = 0;
    std::uint32_t baseline_windows_ = 0;
    // Kept as ready-made percentages so the console never has to read the
    // 64-bit sums across cores, where a torn read is possible on a 32-bit target.
    std::uint8_t baseline0_pct_ = 0;
    std::uint8_t baseline1_pct_ = 0;

    std::uint64_t seq_advance_ = 0;
    std::uint64_t seq_elapsed_us_ = 0;
    std::int64_t last_seq_ = 0;
    bool have_seq_ = false;
};

}  // namespace depthcharge::fw
