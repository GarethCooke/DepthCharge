// firmware/src/resync.hpp — HEALING A KRAKEN BOOK, which is two tasks' work.
//
// M4 stage D item A2. B2 shipped the adapter's half and said so in as many
// words:
//
//     The firmware does not yet act on `resync_wanted()`: nothing in firmware/
//     reads it, because the transport half is D's. The adapter is honest without
//     it — the book is dropped and the panel greys — but the RECOVERY needs the
//     transport to unsubscribe and re-subscribe, and until D wires that, a CRC
//     failure on the board would grey permanently rather than heal.
//
// Kraken never re-snapshots unasked. After the book is dropped the socket is
// perfectly healthy, the heartbeat keeps arriving at 1 Hz, and nothing whatever
// will deliver a fresh baseline unless this client asks for one.
//
// ============================================================================
// IT IS A LEVEL AND NOT AN EDGE, AND THAT IS THE WHOLE DESIGN
// ============================================================================
//
// The first version of this file was edge-triggered: the adapter latched
// `resync_wanted()`, the feed task consumed that latch and set a one-shot flag,
// and the transport consumed the flag with an `exchange(false)`. Review killed
// it, and the reason is worth keeping because it is a general one.
//
// **An edge can be refused, and a refused edge is gone.** Three separate paths
// produced a book with no baseline that nobody would ever ask a snapshot for:
//
//   1. **The floor.** A second checksum failure inside `kResyncMinIntervalUs` of
//      the last heal was counted as throttled and dropped. Nothing retained it;
//      nothing retried. The adapter could not re-raise it either, because
//      `verify_checksum` only latches while `baselined_` is TRUE and the drop
//      had just cleared that — so every subsequent update counted as
//      `deltas_before_baseline` and every checksum as `unverifiable`, for ever.
//   2. **A request with no socket**, or one arriving mid-heal: same disposal.
//   3. **The liveness watchdog firing on a LIVE socket.** `on_transport_gap`
//      drops the book and deliberately does NOT latch `resync_wanted` — the
//      adapter's own comment explains that a reconnect subscribes on its own.
//      True for a dead socket; a watchdog expiry over a live one is a third case
//      neither the adapter nor the edge covered, and a 4 s RF fade is enough to
//      reach it. This board has measured 3.9 s fades that killed nothing.
//
// In all three the panel stayed grey over a healthy, heartbeating socket until
// somebody power-cycled the terminal. The five-minute silence recycle cannot
// save it, because the heartbeat is data and keeps that clock fresh.
//
// So the signal is now a LEVEL — *"the book has no baseline and only a subscribe
// can give it one"* — republished by the feed task on every frame and merely
// READ by the transport. A refusal costs nothing: the level is still true on the
// next pass. `ResyncPolicy` then decides WHEN, and the floor bounds the rate
// rather than the request. Nothing has to remember anything, which is the
// property the edge version could not have.
//
// WHY THE TWO OBJECTS. The detection happens on the FEED task (Core 0), inside
// the adapter, mid-parse. The sending happens on the RX task, because that task
// owns `tls_` and no other may write to the socket — the same rule that puts the
// pong write inside the parser's callback and the connect inside `rx_main`. So
// something has to cross, and `SubscriptionSignal` is that something: two atomic
// flags and two plain counters, exactly the shape and exactly the justification
// of the transport's existing `connect_requested_`.
//
// IT IS NOT A THIRD DATA PATH, and invariant #4 is not bent by it. What crosses
// is a request to perform an action, not book state: no `FeedEvent`, no
// `DisplaySnapshot`, nothing the ladder is drawn from. The book is dropped by
// the adapter before the flag is ever read, and it comes back only through the
// ordinary `FramePipe` -> parse -> `Snapshot` path every other frame takes.
//
// ============================================================================
// THE TWO NUMBERS, AND NEITHER IS MEASURED — STATED SO THE BENCH CAN FALSIFY
// ============================================================================
//
// B2 explicitly declined to choose them and handed them to D.
//
//   `kResyncGapUs` = 1 s, between the unsubscribe and the re-subscribe. The one
//   resync this project has ever captured (`kraken_minagbp_d25_resync_
//   20260818.ndjson`) used ~985 ms — the unsubscribe ack came back in 14.5 ms
//   and the re-subscribe went out 985 ms after the unsubscribe — and produced a
//   snapshot byte-shape identical to an on-connect one. That is the only value
//   with any evidence behind it, so it is the one used, rounded.
//
//   `kResyncMinIntervalUs` = 5 s, the floor between one SUBSCRIBE FRAME and the
//   next. Three arguments, none of them a measurement: a heal costs a measured
//   3,548 ms hole in book events, so back-to-back heals would spend most of
//   their time grey; `tools/capture_kraken.py` guards its reconnect path with
//   `MIN_RECONNECT_GAP_S = 5.0` and is the only Kraken-side pacing number this
//   project has written down; and the endpoint is behind Cloudflare, which has
//   opinions about clients that retry in a tight loop. The adapter deliberately
//   refuses to pace itself ("the transport owns how often it acts on one"), so
//   if this number is absent there is no other.
//
//   **IT ANCHORS ON THE LAST SUBSCRIBE AND NOT ON THE LAST COMPLETED HEAL**, and
//   writing the level-triggered tests is what exposed the difference. The level
//   is necessarily TRUE from the moment a subscribe is sent until the snapshot
//   answering it lands — that is what "the book has no baseline" means — so a
//   floor measured from completed heals, which is zero on a fresh connection,
//   would let the very next RX pass unsubscribe the subscription it had just
//   made. Anchoring on the subscribe covers the snapshot's flight time with the
//   same number, and it is the honest formulation anyway: both justifications
//   above are about the rate of subscribe frames, and the opening subscribe is
//   one.
//
//   The cost, stated: a checksum failure in the first few seconds of a
//   connection waits out the remainder of the floor before it is healed. It is
//   not lost — that is the whole point of the level — and the soak line says
//   `owed=1` while it waits.
#pragma once

#include <atomic>
#include <cstdint>

namespace depthcharge::fw {

inline constexpr std::int64_t kResyncGapUs = 1'000'000;
inline constexpr std::int64_t kResyncMinIntervalUs = 5'000'000;

// What the feed task tells the transport about the subscription. Written on
// Core 0, read on Core 0's other task; the counters are additionally read by the
// console on Core 1, which is the same unsynchronised-diagnostics trade every
// counter in this firmware takes.
class SubscriptionSignal {
public:
    // THE LEVEL. Published on every frame, so it is a statement about the book
    // as it stands rather than a memory of an event. `wants` is
    // `!adapter.has_baseline()` at a venue that must ask for its snapshots.
    //
    // `raised_` counts the 0 -> 1 transitions, which is the number a bench reads
    // against the adapter's own `resyncs_requested`: the level would otherwise
    // report how often the loop ran.
    void set_wanted(bool wants) noexcept {
        if (wants && !wanted_.load(std::memory_order_relaxed)) { ++raised_; }
        wanted_.store(wants, std::memory_order_release);
    }

    bool wanted() const noexcept { return wanted_.load(std::memory_order_acquire); }

    // THE LATCH, and it is a latch because the condition is terminal for this
    // socket. `KrakenAdapter::refused()` means the venue answered the subscribe
    // with `success: false` — a delisted pair, an unsupported depth, a rate
    // limit — and the adapter's own contract says the firmware turns that into
    // `die()` so the supervisor retries with backoff. Without it the board holds
    // a live heartbeating socket over a permanently empty book, which is
    // precisely the failure stage 0 measured when `depth: 27` was refused.
    void set_refused() noexcept {
        if (!refused_.exchange(true, std::memory_order_acq_rel)) { ++refusals_; }
    }

    // One reader, so the exchange is not defending against a race between
    // readers — it makes "saw it" and "cleared it" one step.
    bool take_refused() noexcept { return refused_.exchange(false, std::memory_order_acq_rel); }

    // Reported, never branched on.
    std::uint32_t raised() const noexcept { return raised_; }
    std::uint32_t refusals() const noexcept { return refusals_; }

private:
    std::atomic<bool> wanted_{false};
    std::atomic<bool> refused_{false};
    std::uint32_t raised_ = 0;
    std::uint32_t refusals_ = 0;
};

// WHEN the RX task sends what. A four-state machine over one clock, with no
// socket and no ESP-IDF in it, so `test_resync.cpp` can drive it.
class ResyncPolicy {
public:
    enum class Action : std::uint8_t {
        None,
        SendSubscribe,     // the opening subscribe, or the second half of a heal
        SendUnsubscribe,   // the first half of a heal
    };

    // A socket came up. The subscription died with the old one, so the next step
    // sends a fresh subscribe — and any half-finished heal is abandoned, because
    // a new connection re-subscribes on its own and completing the old sequence
    // would send an unsubscribe for a subscription that no longer exists.
    //
    // `last_subscribe_us_` is reset TOO, and review found it was not. The
    // floor's whole justification — the cost of the book-event hole a heal opens
    // in THIS subscription, and retry pacing towards Cloudflare on THIS socket —
    // does not survive a reconnect that has already paid a full supervisor
    // backoff. Left stale, a subscribe sent seconds before a drop would throttle
    // the first heal of the NEXT connection, which is the one most likely to be
    // needed. The opening subscribe then re-arms it a moment later, which is
    // what covers the fresh snapshot's flight time.
    void on_connected(std::int64_t now_us) noexcept {
        step_ = Step::OpeningSubscribe;
        at_us_ = now_us;
        last_subscribe_us_ = 0;
        owed_ = false;
    }

    // The socket died. Nothing to send and nothing to wait for.
    void on_disconnected() noexcept {
        step_ = Step::Idle;
        last_subscribe_us_ = 0;
        owed_ = false;
    }

    // The state of the book, as of now. Called on EVERY RX pass — with the
    // level up or down — so this is a POLL and not an event: refusing costs
    // nothing because the next pass asks again. That is what makes the floor
    // safe.
    //
    // IT TAKES THE LEVEL RATHER THAN BEING CALLED ONLY WHEN IT IS TRUE, and A5
    // is why. `owed_` was set when a poll was refused and cleared only when a
    // heal began; since a poll only happened while the level was up, nothing
    // cleared it once the snapshot landed, and the first Kraken run reported
    // `owed=1` for its whole length on a book that was live and verifying. A
    // field that means "a heal is waiting" must be able to stop meaning it.
    void on_poll(bool wanted, std::int64_t now_us) noexcept {
        if (!wanted) {
            owed_ = false;
            return;
        }
        if (step_ != Step::Subscribed) { return; }
        if (last_subscribe_us_ != 0 && now_us - last_subscribe_us_ < kResyncMinIntervalUs) {
            owed_ = true;    // reported, so a bench can see a heal waiting on the floor
            return;
        }
        owed_ = false;
        step_ = Step::Unsubscribe;
        at_us_ = now_us;
    }

    // The level is up. Kept as the name the tests read, in terms of the poll.
    void on_wanted(std::int64_t now_us) noexcept { on_poll(true, now_us); }

    // What to do now, if anything. Every returning action advances the machine,
    // so a caller that acts on the return value cannot be asked to do it twice;
    // a caller whose write FAILS is about to lose the socket anyway, and
    // `on_disconnected` resets everything.
    Action step(std::int64_t now_us) noexcept {
        switch (step_) {
            case Step::OpeningSubscribe:
                step_ = Step::Subscribed;
                last_subscribe_us_ = now_us;
                ++subscribes_;
                return Action::SendSubscribe;
            case Step::Unsubscribe:
                step_ = Step::WaitingGap;
                at_us_ = now_us;
                ++unsubscribes_;
                return Action::SendUnsubscribe;
            case Step::WaitingGap:
                if (now_us - at_us_ < kResyncGapUs) { return Action::None; }
                step_ = Step::Subscribed;
                last_subscribe_us_ = now_us;
                ++subscribes_;
                ++heals_;
                return Action::SendSubscribe;
            case Step::Idle:
            case Step::Subscribed:
                break;
        }
        return Action::None;
    }

    // How long the RX task may sleep before `step()` has something to do, in
    // microseconds; 0 means "now" and a negative return means "nothing pending".
    // The read loop already wakes every `kReadTimeoutMs`, so this exists to stop
    // the 1 s gap being rounded up to 2 s by that cadence rather than to create
    // a new timer.
    std::int64_t due_in_us(std::int64_t now_us) const noexcept {
        switch (step_) {
            case Step::OpeningSubscribe:
            case Step::Unsubscribe:
                return 0;
            case Step::WaitingGap: {
                const std::int64_t remaining = kResyncGapUs - (now_us - at_us_);
                return remaining > 0 ? remaining : 0;
            }
            case Step::Idle:
            case Step::Subscribed:
                break;
        }
        return -1;
    }

    bool healing() const noexcept {
        return step_ == Step::Unsubscribe || step_ == Step::WaitingGap;
    }
    bool subscribed() const noexcept { return step_ == Step::Subscribed; }
    // A heal is wanted and the floor has not run out. Reported on the soak line,
    // because "grey, and waiting out a 5 s floor" and "grey, and nothing is
    // going to happen" look identical otherwise.
    bool owed() const noexcept { return owed_; }

    std::uint32_t subscribes() const noexcept { return subscribes_; }
    std::uint32_t unsubscribes() const noexcept { return unsubscribes_; }
    std::uint32_t heals() const noexcept { return heals_; }

private:
    enum class Step : std::uint8_t {
        Idle,               // no socket
        OpeningSubscribe,   // a socket came up; the subscribe is owed
        Subscribed,         // steady state
        Unsubscribe,        // a heal was accepted; the unsubscribe is owed
        WaitingGap,         // unsubscribe sent; kResyncGapUs to wait
    };

    Step step_ = Step::Idle;
    std::int64_t at_us_ = 0;
    std::int64_t last_subscribe_us_ = 0;
    bool owed_ = false;

    std::uint32_t subscribes_ = 0;
    std::uint32_t unsubscribes_ = 0;
    std::uint32_t heals_ = 0;
};

}  // namespace depthcharge::fw
