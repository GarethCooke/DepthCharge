// firmware/src/liveness_watchdog.hpp — WHEN THE PANEL GREYS, as a policy object.
//
// M4 stage D item A1. This replaces `kRxWatchdogMs` and the rule built on it,
// and the replacement is a behavioural INVERSION rather than a new constant.
//
// WHAT CHANGED, AND WHY THE OLD ARGUMENT IS DELETED RATHER THAN AMENDED.
//
// Until this file the firmware armed its watchdog on an EVENT REACHING THE BOOK
// and greyed after 1,000 ms of silence, and `feed_task.hpp` argued the case at
// length: a watchdog armed on frame arrival would sit happily while the ladder
// froze, which is the one output invariant #5 forbids. That argument was right
// about arrival and wrong about book events, and the 2026-08-17 ruling
// (ARCHITECTURE §9) says so outright:
//
//     **No threshold on book silence can be correct.** A quiet market and a
//     dead subscription are identical on the wire. MINA/GBP's healthy 25,843 ms
//     of book silence is a property of the market, not of the connection.
//
// So book silence stops being a fault and becomes a NUMBER — `age_ms`, rendered
// and never branched on — and the fault signal becomes the silence of the
// venue's own declared LIVENESS SIGNAL: Anvil's 2 Hz `summary`, Kraken's 1 Hz
// `heartbeat`. Both are emitted by a clock rather than by the market, which is
// exactly what makes their absence mean something.
//
// AND THE THRESHOLD IS MEASURED, NOT DECLARED. `LivenessClock` derives it from
// the signal's own observed median (`kThresholdMultiple` x median, floored and
// capped), because both intervals are values DepthCharge cannot read back:
// Anvil's is operator config on a server we do not own, and `PROTOCOL.md` §3.5
// now says in as many words that a client "must derive it from the cadence it
// observes on the connection it is using, not from a number read out of this
// document". Orientation only, since nothing reads these: Anvil lands near
// 2,000 ms and Kraken near 4,000 ms.
//
// WHY THIS IS A HEADER OF ITS OWN, AND ESP-IDF-FREE.
//
// The brief says of this rewire that "no host test can check" it, and that is
// true of the BEHAVIOUR — a panel greying four seconds after a heartbeat stops
// is a bench observation. It is not true of the POLICY, and this project's own
// pattern (`ws_supervisor.hpp`, `gap_histogram.hpp`, `ws_ping.hpp`) is to put
// the decidable half in an ESP-IDF-free header and test it on the desk, leaving
// the platform half — a FreeRTOS queue timeout and an `esp_timer` reading — in
// the .cpp where there is nothing left to get wrong. `harness/tests/
// test_liveness_watchdog.cpp` is that test, and it is where the deleted
// `test_staleness.cpp`'s still-live cases were ported to.
//
// NANOSECONDS, BECAUSE THE ENGINE CLOCKS TAKE NANOSECONDS. The firmware's own
// clock is `esp_timer_get_time()` in MICROseconds and every other instrument in
// this build is in µs, so the conversion is a real hazard: both parameters are
// `std::int64_t` and a forgotten x1000 is a 1,000x-wrong threshold with no
// compile error. It is conceded to exactly once, at `FeedTask`'s entry points,
// by way of `ns_from_us()` below — which exists so that grepping for it finds
// every place the two domains meet.
#pragma once

#include <cstdint>

#include <depthcharge/age_estimator.hpp>
#include <depthcharge/liveness_clock.hpp>

namespace depthcharge::fw {

// The one conversion, spelled once. See the header comment.
constexpr std::int64_t ns_from_us(std::int64_t us) noexcept { return us * 1000; }

// ---------------------------------------------------------------------------
// THE ONE THING NO ROUTER ON THIS DESK CAN STAGE
// ---------------------------------------------------------------------------
//
// M4's definition of done requires the panel to grey within the calibrated
// liveness threshold **of the heartbeat stopping**. Staging that needs the
// venue's clock to fall silent while the SOCKET STAYS UP — the half-open case
// this watchdog exists for — and on 2026-08-20 two attempts established that
// the bench cannot produce it from the network side:
//
//   * disabling the interface kills the TLS read, so `on_disconnected()` raises
//     the Gap within about a second and the threshold never runs;
//   * the mesh's own per-client "pause" DEAUTHENTICATES rather than dropping
//     packets, and does exactly the same thing.
//
// Both were measured: `sock` incremented and `wd` did not. So the condition is
// staged HERE instead, by muting the feed task's liveness arrivals after a
// stated uptime while everything else — the socket, the reads, the book, the
// panel — carries on untouched. What that exercises is the real object, the
// real calibrated threshold and the real queue-wait arithmetic on real silicon;
// the only difference from the shipping image is one suppressed call.
//
// **IT IS OFF UNLESS ASKED FOR, AND IT ANNOUNCES ITSELF WHEN IT IS NOT.**
// `DC_TEST_MUTE_LIVENESS` is undefined in every environment in
// `platformio.ini` on purpose — a test-only flag that lives in a build
// environment is a test-only flag somebody eventually ships. It is passed for
// one run through `PLATFORMIO_BUILD_FLAGS`, which is this project's established
// way of doing exactly that (see firmware/README.md), and `main.cpp` prints a
// banner at boot so no capture taken from such an image can be mistaken for a
// capture of the shipping one.
//
// A BOOT BANNER IS NOT ENOUGH, and the run of 2026-08-20 proved it: captures are
// routinely attached WITHOUT resetting the board, because resetting to attach
// destroys the uptime the log is about. A bare `pio device monitor` — the
// `DepthCharge: Monitor` task — does not reset it, and `firmware/logs/` holds
// captures that open mid-uptime rather than at `ESP-ROM:`; only the
// `-t upload -t monitor` form resets, and then because the UPLOAD does. A marker
// that only prints at boot is therefore invisible to exactly the captures that
// matter. (This sentence named a `capture_noreset.py` until 2026-08-30. No such
// file has ever existed here; the finding was sound and the mechanism cited for
// it was not. See ARCHITECTURE §9, 2026-08-29 row.) `DC_SOAK_TEST_TAG` puts it on
// every SOAK line instead, so any ten-second window of the output identifies the
// image. It is a string LITERAL chosen by the preprocessor, so the shipping
// binary does not merely skip printing it — it does not contain it.
#ifndef DC_TEST_MUTE_LIVENESS
#define DC_TEST_MUTE_LIVENESS 0
#endif

#define DC_STRINGIFY_(x) #x
#define DC_STRINGIFY(x) DC_STRINGIFY_(x)

#if DC_TEST_MUTE_LIVENESS
#define DC_SOAK_TEST_TAG \
    " *** TEST IMAGE: liveness MUTED after " DC_STRINGIFY(DC_TEST_MUTE_LIVENESS) " s — NOT SHIPPING ***"
#else
#define DC_SOAK_TEST_TAG ""
#endif

inline constexpr bool kTestMutesLiveness = (DC_TEST_MUTE_LIVENESS != 0);
inline constexpr std::int64_t kTestMuteLivenessAfterUs =
    static_cast<std::int64_t>(DC_TEST_MUTE_LIVENESS) * 1000 * 1000;

// True once a muted build has passed its uptime and should stop feeding the
// watchdog. Always false in a shipping image, at compile time.
constexpr bool test_liveness_muted(std::int64_t uptime_us) noexcept {
    return kTestMutesLiveness && uptime_us >= kTestMuteLivenessAfterUs;
}

// The two engine clocks, plus the single decision the firmware takes on them.
//
// ONE OBJECT AND NOT TWO MEMBERS, because the two clocks must see every arrival
// and must see it at the same stamp — that is how the host replay driver drives
// them (`liveness_.on_liveness(rx_ns); age_.on_liveness(rx_ns);`, one after the
// other, same value) and a firmware that fed one and forgot the other would
// produce an age against a threshold measured from a different sample set. A
// single `on_liveness` makes that unforgettable rather than conventional.
class LivenessWatchdog {
public:
    // A liveness signal arrived. This is the ONLY thing that arms the watchdog:
    // a book event does not, a byte does not, and a control frame does not.
    // The gap is recorded against `last_ns_` and NOT gated on `armed_`, which
    // is the one line here with a scar behind it. The object this replaces
    // gated its equivalent on `watching_`, so the watchdog clearing that flag
    // suppressed the very number that quantified the gap it had just detected —
    // every outage reported its own size as zero. `last_ns_ != 0` instead, so
    // the first arrival after an outage measures the whole hole, exactly as
    // `feed_task.cpp` already does for book events.
    void on_liveness(std::int64_t at_ns) noexcept {
        // A STAMP THAT GOES BACKWARDS IS REFUSED OUTRIGHT, not merely excluded
        // from the worst-gap instrument. Review found the earlier version
        // guarded only its own subtraction and then handed the stamp on: the
        // clock pushed a NEGATIVE interval into the median window, the
        // estimator pushed a non-monotone arrival into its ring, and — worst —
        // `last_ns_` regressed, so `deadline_ns()` moved into the past and the
        // very next `expired()` greyed a healthy panel.
        //
        // `esp_timer_get_time()` is monotonic, so this defends against a caller
        // mistake rather than against the platform: two stamps exist in this
        // firmware (`msg.arrival_us` and the feed task's own `now`) and a future
        // path that mixes them is one edit away. Counted rather than silent,
        // because a firmware that is silently discarding its liveness signal
        // looks exactly like a venue that has stopped sending one.
        if (last_ns_ != 0 && at_ns <= last_ns_) {
            ++non_monotone_;
            return;
        }
        if (last_ns_ != 0) {
            const std::uint64_t gap = static_cast<std::uint64_t>(at_ns - last_ns_);
            if (gap > worst_gap_ns_) { worst_gap_ns_ = gap; }
        }
        clock_.on_liveness(at_ns);
        age_.on_liveness(at_ns);
        last_ns_ = at_ns;
        armed_ = true;
        refresh_report();
    }

    // The socket came up or went down. Both do the same two things and the
    // symmetry is deliberate.
    //
    // The AGE dies either way, because the backlog it measures is server-side
    // state on ONE connection: a new socket gets a fresh queue and a fresh
    // snapshot, so carrying an estimate across would have made the 86-minute
    // run of 2026-08-09 — 21 reconnects — read as one long healthy stream. The
    // peak is banked on the way down by `on_reconnect`, or the one episode the
    // instrument exists to capture is the one that erases it.
    //
    // The THRESHOLD does not die, and that asymmetry is the point: cadence is a
    // property of the venue, not of the connection, so `LivenessClock` keeps its
    // window across a reconnect. A clock that recalibrated per socket would
    // spend the first eight arrivals of every outage recovery at the 30 s
    // uncalibrated default.
    void on_socket_change(std::int64_t at_ns) noexcept {
        age_.on_reconnect(at_ns);
        armed_ = false;
        refresh_report();
    }

    // The liveness signal has been silent for longer than this venue currently
    // warrants. False when nothing has ever arrived: a board that has never
    // connected is already `Stale{Resync}` and needs no Gap to say so.
    bool expired(std::int64_t now_ns) const noexcept {
        return armed_ && now_ns >= deadline_ns();
    }

    // The instant `expired()` will become true, or 0 when there is nothing to
    // watch. The feed task turns this into a queue-wait timeout, which is why it
    // is an absolute stamp rather than a duration.
    std::int64_t deadline_ns() const noexcept {
        if (!armed_) { return 0; }
        return last_ns_ + threshold_ns();
    }

    // It fired. Same shape as the old `watching_ = false`: one outage is one
    // Gap, not one a second for as long as the venue is quiet.
    //
    // It also voids the current age READING, because the estimator genuinely
    // cannot say whether the frames it missed were queued or never sent.
    //
    // `on_stall` AND NOT `on_reconnect`, and the difference is a defect review
    // found rather than a nicety. `on_reconnect` also discards the BASELINE,
    // which is correct only on a fresh socket — a baseline is measurable just
    // once, at connect, when the server-side queue is empty. A watchdog firing
    // on a still-live socket is not that moment: re-latching there measures the
    // DRAIN. Anvil queues and never drops per socket, so a 40 s stall flushes
    // ~80 summaries ~2.5 ms apart, and a baseline latched from those turns a
    // perfectly healthy feed into one reporting 99.5% of wall-clock as lag, for
    // the life of the connection. The host driver models a watchdog gap AS a
    // disconnect because in a trace file it is one; on a board it is not, and
    // this is the one place the two must differ. See age_estimator.hpp.
    //
    // `last_ns_` is deliberately NOT moved: the next arrival then measures the
    // whole outage rather than the sliver of it after the alarm.
    void note_fired(std::int64_t at_ns) noexcept {
        ++firings_;
        age_.on_stall(at_ns);
        armed_ = false;
        refresh_report();
    }

    // The book's estimated queuing lag right now, or the honest absence of one.
    // `read` for anyone who must not disturb the high-water mark; `read_and_bank`
    // is what the publish path calls.
    AgeReading age(std::int64_t now_ns) const noexcept { return age_.read(now_ns); }
    AgeReading age_and_bank(std::int64_t now_ns) noexcept { return age_.read_and_bank(now_ns); }

    // --- WHAT THE SERIAL LINE READS, AND WHY IT IS ALL 32-BIT ----------------
    //
    // These are read by the RENDER task on Core 1 while the feed task mutates
    // this object on Core 0, unsynchronised — the same trade every diagnostic
    // counter in this firmware already takes, and defensible for the same
    // reason: nothing branches on them, so the cost of a bad sample is one
    // wrong number on one log line.
    //
    // IT IS ONLY DEFENSIBLE AT 32 BITS, WHICH IS WHY THE DOUBLES DO NOT CROSS.
    // Review found the console reaching straight through to `clock_.median_ms()`
    // and `age_.read()`: the first copies and `std::sort`s a 32-sample ring the
    // other core is pushing into, the second walks 256 `int64_t` arrivals — and
    // `baseline_ms_` is a `double`, which the LX7 stores as two 32-bit writes,
    // so a torn read yields a nonsense reference cadence and therefore a
    // nonsense age. A wrong number is affordable; a number wrong by 2^32 on the
    // one line an overnight soak is read from is not.
    //
    // So the feed task converts them once per liveness arrival (1-2 per second)
    // and the console reads plain `uint32_t` milliseconds, which this core loads
    // atomically. `AgeEstimator::read` is not exposed here at all: the console
    // gets the age from the `DisplaySnapshot` it consumed, which crossed the
    // boundary through the wait-free channel that exists for exactly that.
    bool armed() const noexcept { return armed_; }
    bool calibrated() const noexcept { return clock_.calibrated(); }
    std::uint32_t samples() const noexcept { return samples_; }
    std::uint32_t median_ms() const noexcept { return median_ms_; }
    std::uint32_t threshold_ms() const noexcept { return threshold_ms_; }
    std::uint32_t baseline_ms() const noexcept { return baseline_ms_; }
    bool age_calibrated() const noexcept { return baseline_ms_ != 0; }
    std::uint32_t worst_age_ms() const noexcept { return age_.worst_ms(); }
    std::uint32_t firings() const noexcept { return firings_; }
    std::uint32_t non_monotone() const noexcept { return non_monotone_; }
    std::uint64_t worst_gap_ns() const noexcept { return worst_gap_ns_; }
    std::int64_t last_liveness_ns() const noexcept { return last_ns_; }

    // The exact doubles, for the FEED side and for host tests. Never called
    // across the core boundary — see above.
    double threshold_ms_exact() const noexcept { return clock_.threshold_ms(); }
    double median_ms_exact() const noexcept { return clock_.median_ms(); }
    double baseline_ms_exact() const noexcept { return age_.baseline_ms(); }

private:
    // The threshold in the deadline's own units. Rounded to the nearest
    // nanosecond from a double that is a whole number of milliseconds in every
    // reachable case (k x a median of ms-valued samples), so the conversion is
    // exact where it matters and the cast is not hiding a policy.
    std::int64_t threshold_ns() const noexcept {
        return static_cast<std::int64_t>(clock_.threshold_ms() * 1e6);
    }

    // Fold the doubles down to the millisecond integers the other core reads.
    // Called on every state change that can move one of them, which is at most
    // twice a second.
    void refresh_report() noexcept {
        threshold_ms_ = to_ms(clock_.threshold_ms());
        median_ms_ = to_ms(clock_.median_ms());
        baseline_ms_ = to_ms(age_.baseline_ms());
        samples_ = static_cast<std::uint32_t>(clock_.samples());
    }

    static std::uint32_t to_ms(double v) noexcept {
        if (v <= 0.0) { return 0; }
        return v >= 4294967295.0 ? 4294967295u : static_cast<std::uint32_t>(v + 0.5);
    }

    LivenessClock clock_;
    AgeEstimator age_;

    std::int64_t last_ns_ = 0;
    bool armed_ = false;
    std::uint32_t firings_ = 0;
    std::uint32_t non_monotone_ = 0;
    std::uint64_t worst_gap_ns_ = 0;

    // The 32-bit mirror the console reads. Initialised to what an uncalibrated
    // clock reports, so the first line printed before any arrival is honest.
    std::uint32_t threshold_ms_ = static_cast<std::uint32_t>(kUncalibratedThresholdMs);
    std::uint32_t median_ms_ = 0;
    std::uint32_t baseline_ms_ = 0;
    std::uint32_t samples_ = 0;
};

// HOW OFTEN THE PANEL WENT GREY, AND FOR HOW LONG IN TOTAL.
//
// M4 stage D item A4, and it is here rather than in `FeedTask` because review
// pointed out that new counting logic living in a `.cpp` no host build compiles
// is new counting logic with no coverage at all — while the rule itself is four
// lines of arithmetic over a boolean, which is exactly the kind of thing this
// project puts on the testable side of the line.
//
// IT COUNTS EPISODES, NOT EVENTS, and the distinction is not pedantic: a Kraken
// heal produces `Gap{ChecksumFail}` and then `Gap{Resync}` back to back, so a
// counter keyed on gaps reads 2 where the panel showed one continuous grey. The
// bench reads this number to decide whether a night was quiet, and a doubled
// count reads as a second fault that never happened.
//
// The open episode is folded in by the READER (`total_ms`), not accumulated by
// the writer, so a line printed in the middle of a four-hour outage reports four
// hours rather than zero. That is the reading an unattended soak most needs to
// be honest about, and the naive version gets it exactly backwards.
class GreyLedger {
public:
    // One published frame. `live` is `DisplaySnapshot::live()`.
    void note(bool live, std::int64_t now_us) noexcept {
        if (!live && !grey_) {
            ++episodes_;
            grey_ = true;
            grey_since_us_ = now_us;
        } else if (live && grey_) {
            if (now_us > grey_since_us_) {
                closed_us_ += static_cast<std::uint64_t>(now_us - grey_since_us_);
            }
            grey_ = false;
            total_ms_ = static_cast<std::uint32_t>(closed_us_ / 1000);
        }
    }

    std::uint32_t episodes() const noexcept { return episodes_; }
    bool grey_now() const noexcept { return grey_; }

    // Total grey INCLUDING the episode still open at `now_us`. Takes the clock
    // rather than reading one, so it is testable and so the caller can use the
    // same instant for this and for the age beside it.
    std::uint64_t total_ms(std::int64_t now_us) const noexcept {
        std::uint64_t us = closed_us_;
        if (grey_ && now_us > grey_since_us_) {
            us += static_cast<std::uint64_t>(now_us - grey_since_us_);
        }
        return us / 1000;
    }

    // The closed total only, as a 32-bit value the other core can load
    // atomically. A console that cannot take the clock reads this and knows it
    // excludes any open episode.
    std::uint32_t closed_ms() const noexcept { return total_ms_; }

private:
    // A SEPARATE FLAG AND NOT A SENTINEL TIMESTAMP. The first draft used
    // `grey_since_us_ == 0` to mean "live", and its own first test caught it: a
    // board that boots stale publishes its first frame at uptime 0, so the
    // episode this ledger exists to record was the one it could not see. Same
    // class as `last_ns_ != 0` above, and this is the second time in one file.
    bool grey_ = false;
    std::uint32_t episodes_ = 0;
    std::uint64_t closed_us_ = 0;
    std::int64_t grey_since_us_ = 0;
    std::uint32_t total_ms_ = 0;
};

}  // namespace depthcharge::fw
