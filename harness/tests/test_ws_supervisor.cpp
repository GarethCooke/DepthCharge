// test_ws_supervisor.cpp — the reconnect policy, argued on the desk.
//
// The fourth firmware header the host build knows about, and the one that has
// cost the most to get wrong so far. Every previous version of this logic was
// evaluated by flashing a board, stopping Anvil by hand, and reading a serial
// log — about one sample an evening, which is how a trigger that never fired
// once shipped (the event-armed retry, ARCHITECTURE §9, 2026-08-09) and how a
// boot connection spent a year of bench runs with no handshake immunity.
//
// What is tested here is only the arithmetic: when an attempt goes out, which
// attempt it is, the guarantee the constants exist to provide — that an attempt
// is never disturbed inside its own budget — and, from 2026-08-16, when a socket
// that is UP but silent stops counting as alive. What the platform does with a
// decision is WsTransport's half and stays on the board.
#include <doctest/doctest.h>

#include <cstdint>

#include "ws_supervisor.hpp"

using depthcharge::fw::kHandshakeBudgetUs;
using depthcharge::fw::kReconnectBackoffUs;
using depthcharge::fw::kRetryCycleUs;
using depthcharge::fw::kSilenceRecycleUs;
using depthcharge::fw::kSupervisePeriodUs;
using depthcharge::fw::SupervisorAction;
using depthcharge::fw::SupervisorDecision;
using depthcharge::fw::SupervisorInput;
using depthcharge::fw::WsSupervisor;

namespace {

// A bench in a loop: polls at the real cadence and reports what happened, so a
// test reads as "how long was the panel grey" rather than as a sequence of
// poll() calls.
struct Bench {
    WsSupervisor sup;
    std::int64_t now = 0;
    bool socket = false;
    bool wifi = true;
    // The last-byte stamp the transport feeds in. Left at 0 — "nothing yet" — by
    // every test that predates the silence recycle, which is exactly the
    // behaviour a caller that never fills the field in must get: the supervisor
    // measures silence from the connect edge it observes for itself, so an unset
    // stamp can never age a socket that has only just come up.
    std::int64_t last_rx = 0;

    SupervisorDecision step() {
        SupervisorInput in;
        in.now_us = now;
        in.socket_connected = socket;
        in.wifi_associated = wifi;
        in.last_rx_us = last_rx;
        const SupervisorDecision d = sup.poll(in);
        now += kSupervisePeriodUs;
        return d;
    }

    // Runs until an attempt goes out, or `limit` polls have passed. Returns the
    // time of the attempt relative to `from`, or -1.
    std::int64_t run_to_attempt(std::int64_t from, int limit = 400) {
        for (int i = 0; i < limit; ++i) {
            const std::int64_t at = now;
            if (step().action == SupervisorAction::StartAttempt) { return at - from; }
        }
        return -1;
    }
};

}  // namespace

TEST_CASE("a healthy socket produces no decisions at all") {
    Bench b;
    b.socket = true;
    for (int i = 0; i < 100; ++i) {
        CHECK(b.step().action == SupervisorAction::None);
    }
    CHECK(b.sup.attempts() == 0u);
}

TEST_CASE("the first attempt of an outage falls one backoff after the feed died") {
    Bench b;
    b.socket = true;
    b.step();  // one healthy pass, so the outage has a `connected` state to leave
    const std::int64_t died = b.now;
    b.socket = false;

    const std::int64_t delay = b.run_to_attempt(died);
    REQUIRE(delay >= 0);
    // The pass that first sees the socket down can only record when it noticed,
    // so the attempt lands on the next pass at or after the backoff.
    CHECK(delay >= kReconnectBackoffUs);
    CHECK(delay < kReconnectBackoffUs + 2 * kSupervisePeriodUs);
    CHECK(b.sup.attempts() == 1u);
    CHECK(b.sup.attempt_in_flight());
}

TEST_CASE("an attempt is never disturbed inside its handshake budget") {
    Bench b;
    b.socket = true;
    b.step();
    b.socket = false;

    REQUIRE(b.run_to_attempt(0) >= 0);
    const std::int64_t attempt_at = b.now - kSupervisePeriodUs;

    // Every poll from here until the budget has expired must decide nothing.
    // This is the guarantee that stops the supervisor killing a connection a
    // beat before it succeeds — the one failure mode in this file that cannot
    // self-correct.
    while (b.now < attempt_at + kHandshakeBudgetUs) {
        CHECK(b.step().action == SupervisorAction::None);
    }
    CHECK(b.sup.attempts() == 1u);
}

TEST_CASE("later attempts come one full retry cycle apart") {
    Bench b;
    b.socket = true;
    b.step();
    b.socket = false;

    REQUIRE(b.run_to_attempt(0) >= 0);
    std::int64_t previous = b.now - kSupervisePeriodUs;

    for (int n = 2; n <= 4; ++n) {
        const std::int64_t gap = b.run_to_attempt(previous);
        REQUIRE(gap >= 0);
        CHECK(gap >= kRetryCycleUs);
        CHECK(gap < kRetryCycleUs + kSupervisePeriodUs);
        CHECK(b.sup.attempts() == static_cast<std::uint32_t>(n));
        previous += gap;
    }
}

// The two-handle case that used to sit here — "a retry never returns to a handle
// that is still dying" — went with the two handles on 2026-08-16. It asserted
// `first + second > kClientSelfExitUs`, a property of how long
// `esp_websocket_client`'s task slept before a handle became reusable, and this
// firmware now owns one esp-tls connection that `esp_tls_conn_destroy()` frees
// at once. Keeping it would have meant keeping a constant that describes a
// library the build no longer contains, in order to test a rule nothing obeys.

TEST_CASE("an unassociated station holds the attempt without spending a cycle") {
    Bench b;
    b.socket = true;
    b.step();
    const std::int64_t died = b.now;
    b.socket = false;
    b.wifi = false;

    // Long past the point where the attempt was due, and nothing goes out.
    bool holdoff_seen = false;
    int holdoffs = 0;
    while (b.now < died + 4 * kReconnectBackoffUs) {
        const SupervisorDecision d = b.step();
        CHECK(d.action != SupervisorAction::StartAttempt);
        if (d.wifi_holdoff) {
            ++holdoffs;
            holdoff_seen = true;
        }
    }
    CHECK(holdoff_seen);
    // Reported once per outage, not once per poll: this line costs a UART write
    // on loopTask, and one every 250 ms for the length of a Wi-Fi outage would
    // bury the log a bench session is reading.
    CHECK(holdoffs == 1);
    CHECK(b.sup.attempts() == 0u);

    // The station comes back: the attempt goes out on the NEXT poll, not a retry
    // cycle later. That difference is the whole point of holding rather than
    // deferring — on an AP-steering drop it is a 4 s grey against an 11 s one.
    b.wifi = true;
    const std::int64_t back = b.now;
    const std::int64_t delay = b.run_to_attempt(back);
    REQUIRE(delay >= 0);
    CHECK(delay < 2 * kSupervisePeriodUs);
}

TEST_CASE("recovery reports the attempt duration, once") {
    Bench b;
    b.socket = true;
    b.step();
    b.socket = false;
    REQUIRE(b.run_to_attempt(0) >= 0);
    const std::int64_t attempt_at = b.now - kSupervisePeriodUs;

    // Three quiet polls, then the socket comes up.
    for (int i = 0; i < 3; ++i) { b.step(); }
    b.socket = true;
    const std::int64_t up_at = b.now;

    const SupervisorDecision d = b.step();
    REQUIRE(d.action == SupervisorAction::ReportConnected);
    CHECK(d.elapsed_us == up_at - attempt_at);
    CHECK(d.attempt == 1u);

    // And not again on the next pass — a recovery is an edge, and a supervisor
    // that reported it every 250 ms would put the one line a bench reads for the
    // handshake measurement into a repeating block.
    CHECK(b.step().action == SupervisorAction::None);
    CHECK_FALSE(b.sup.attempt_in_flight());
}

TEST_CASE("a socket that comes back on its own clears the outage silently") {
    // No attempt was ever in flight, so there is nothing to report — but the
    // outage state must still be cleared, or the next drop would measure its
    // backoff from the first one.
    Bench b;
    b.socket = true;
    b.step();
    b.socket = false;
    b.step();  // notices
    b.socket = true;
    CHECK(b.step().action == SupervisorAction::None);
    CHECK(b.sup.attempts() == 0u);

    const std::int64_t died = b.now;
    b.socket = false;
    const std::int64_t delay = b.run_to_attempt(died);
    REQUIRE(delay >= 0);
    CHECK(delay < kReconnectBackoffUs + 2 * kSupervisePeriodUs);
}

TEST_CASE("the boot connection is attempt #1 and carries the same immunity") {
    // Without this the first poll sees a client that has never been connected,
    // cannot tell that from one that dropped, and opens the spare a backoff into
    // the cold TLS handshake. It recovers, which is why the bug survived a bench
    // run: the only evidence was an attempt counter reading 2.
    Bench b;
    b.sup.note_attempt_begun(b.now);
    CHECK(b.sup.attempts() == 1u);
    b.socket = false;

    while (b.now < kHandshakeBudgetUs) {
        CHECK(b.step().action == SupervisorAction::None);
    }
    CHECK(b.sup.attempts() == 1u);
}

TEST_CASE("the holdoff line returns after the feed recovers") {
    // Two separate Wi-Fi outages must each get their one line. The flag is
    // cleared by a healthy socket rather than by an attempt, because an attempt
    // is not evidence the station came back — only data is.
    Bench b;
    b.socket = true;
    b.step();
    b.socket = false;
    b.wifi = false;

    int first_run = 0;
    while (b.now < 4 * kReconnectBackoffUs) {
        if (b.step().wifi_holdoff) { ++first_run; }
    }
    CHECK(first_run == 1);

    b.wifi = true;
    b.socket = true;
    b.step();

    b.socket = false;
    b.wifi = false;
    int second_run = 0;
    const std::int64_t from = b.now;
    while (b.now < from + 4 * kReconnectBackoffUs) {
        if (b.step().wifi_holdoff) { ++second_run; }
    }
    CHECK(second_run == 1);
}

// ---------------------------------------------------------------------------
// The silence recycle — the 2026-08-16 half-open hole.
// ---------------------------------------------------------------------------
//
// A silent-but-open socket reads as connected, so before this the transport held
// it forever: the panel was honestly grey and nothing would ever try a fresh
// connect. These cases pin the three things the threshold has to get right —
// it fires, it does not fire early, and it does not fire on a socket that is
// merely slow.

namespace {

// A socket that is up and delivering. `feed()` advances the clock while keeping
// the last-byte stamp current, which is what a healthy connection looks like to
// the supervisor; `starve()` advances it while leaving the stamp where it was.
struct SocketBench : Bench {
    SocketBench() {
        socket = true;
        last_rx = now;
        step();   // the up-edge, so the supervisor has seen this socket connect
    }

    // Polls for `span` of wall clock with bytes arriving every pass. Returns the
    // first StartAttempt seen, or an action of None.
    SupervisorDecision feed(std::int64_t span) {
        return run(span, /*bytes=*/true);
    }
    // Polls for `span` of wall clock with the socket up and saying nothing.
    SupervisorDecision starve(std::int64_t span) {
        return run(span, /*bytes=*/false);
    }

private:
    SupervisorDecision run(std::int64_t span, bool bytes) {
        const std::int64_t until = now + span;
        while (now < until) {
            if (bytes) { last_rx = now; }
            const SupervisorDecision d = step();
            if (d.action == SupervisorAction::StartAttempt) { return d; }
        }
        return SupervisorDecision{};
    }
};

}  // namespace

TEST_CASE("a socket that keeps delivering is never recycled, however long it runs") {
    // The case that must never break: an object on a desk for a week.
    SocketBench b;
    const SupervisorDecision d = b.feed(20 * kSilenceRecycleUs);
    CHECK(d.action == SupervisorAction::None);
    CHECK(b.sup.attempts() == 0u);
}

TEST_CASE("a silent socket is recycled, and not one poll before the threshold") {
    SocketBench b;
    const std::int64_t last_byte = b.now - kSupervisePeriodUs;

    // Everything up to the threshold is a hold, not a decision. This is the half
    // that matters most, because the failure it prevents is tearing down a
    // healthy connection — see the 2m56s Anvil stall in the constant's note.
    const SupervisorDecision early = b.starve(kSilenceRecycleUs - 2 * kSupervisePeriodUs);
    CHECK(early.action == SupervisorAction::None);
    CHECK(b.sup.attempts() == 0u);

    const SupervisorDecision d = b.starve(4 * kSupervisePeriodUs);
    REQUIRE(d.action == SupervisorAction::StartAttempt);
    // It is labelled, because the transport has to tear a live socket down for
    // this one and merely open a socket for every other StartAttempt.
    CHECK(d.silence_recycle);
    CHECK(d.attempt == 1u);
    // And `feed down N` reports the silence rather than the poll period: the
    // outage is measured from the last byte, which is the honest number and the
    // one a bench reads to decide whether the threshold is right.
    CHECK(d.elapsed_us >= kSilenceRecycleUs);
    CHECK(b.now - last_byte < d.elapsed_us + 3 * kSupervisePeriodUs);
}

TEST_CASE("the 2026-08-16 Anvil stall costs nothing") {
    // THE MEASUREMENT THAT SET THE CONSTANT. At 00:12:24 the server went silent
    // mid-stream on a live socket and resumed 2 min 56 s later on the same TCP
    // connection — the board went LIVE the instant bytes flowed. The first draft
    // of this policy used a 15 s threshold and would have torn that connection
    // down and paid a reconnect plus a fresh snapshot for nothing.
    constexpr std::int64_t kAnvilStallUs = (2 * 60 + 56) * 1000 * 1000LL;
    SocketBench b;

    const SupervisorDecision during = b.starve(kAnvilStallUs);
    CHECK(during.action == SupervisorAction::None);
    CHECK(b.sup.attempts() == 0u);

    // Bytes resume on the held socket, and the silence clock resets with them.
    CHECK(b.feed(kSilenceRecycleUs).action == SupervisorAction::None);
    CHECK(b.sup.attempts() == 0u);
}

TEST_CASE("the recycle's attempt behaves like any other from there on") {
    // Once the decision is out, the transport tears the socket down and opens a
    // replacement — so the supervisor sees the socket go away and come back, and
    // must report the recovery exactly as it would after a real death.
    SocketBench b;
    REQUIRE(b.starve(kSilenceRecycleUs + 4 * kSupervisePeriodUs).action ==
            SupervisorAction::StartAttempt);

    // The socket is still up for a pass or two while the RX task notices; the
    // policy must not fire again inside the handshake budget.
    const std::int64_t attempt_at = b.now - kSupervisePeriodUs;
    while (b.now < attempt_at + kHandshakeBudgetUs) {
        CHECK(b.step().action != SupervisorAction::StartAttempt);
    }
    CHECK(b.sup.attempts() == 1u);

    // The transport has now torn the socket down, and the reconnect is failing.
    // THE LABEL MUST STOP: there is no live socket left to recycle, and a retry
    // that still claimed `socket up but silent` would print exactly that over a
    // socket that is definitively gone. The first version of this policy latched
    // "this outage began in silence" when the outage opened and never cleared
    // it, so every attempt from #2 on lied; the fix is to report the live
    // reading instead of the remembered one.
    b.socket = false;
    SupervisorDecision retry;
    while (retry.action != SupervisorAction::StartAttempt) {
        REQUIRE(b.now < attempt_at + 4 * kRetryCycleUs);
        retry = b.step();
    }
    CHECK(retry.attempt == 2u);
    CHECK_FALSE(retry.silence_recycle);

    // Down, then up with data: a clean ReportConnected and a clear outage.
    b.step();
    b.socket = true;
    b.last_rx = b.now;
    const SupervisorDecision up = b.step();
    REQUIRE(up.action == SupervisorAction::ReportConnected);
    CHECK(up.attempt == 2u);
    CHECK_FALSE(b.sup.attempt_in_flight());

    // And the recycle is not sticky: a healthy socket from here decides nothing.
    CHECK(b.feed(2 * kSilenceRecycleUs).action == SupervisorAction::None);
}

TEST_CASE("the silence clock starts at the connect, not at the epoch") {
    // A caller that never fills `last_rx_us` in — which is every test written
    // before this feature, and would be a transport that forgot the store — gets
    // a supervisor that measures from the up-edge it observed itself. Without
    // that, a stamp of 0 against a clock that is hours into a run would read as
    // "silent since boot" and recycle a socket on its first poll.
    Bench b;
    b.now = 9 * kSilenceRecycleUs;   // a board that has been up a long time
    b.socket = true;
    b.last_rx = 0;                   // never stamped

    const std::int64_t connected_at = b.now;
    for (int i = 0; i < 40; ++i) {
        CHECK(b.step().action == SupervisorAction::None);
    }
    CHECK(b.sup.attempts() == 0u);

    // ...and it still recycles once the connect itself is old enough, so the
    // fallback is a delay and not a hole in the cover.
    SupervisorDecision d;
    while (d.action != SupervisorAction::StartAttempt) {
        REQUIRE(b.now < connected_at + 2 * kSilenceRecycleUs);
        d = b.step();
    }
    CHECK(d.silence_recycle);
    CHECK(b.now - connected_at >= kSilenceRecycleUs);
    CHECK(b.sup.attempts() == 1u);
}

TEST_CASE("a socket that dies normally is not labelled a silence recycle") {
    // The label is branched on in the transport, so a false positive would print
    // "recycling a live socket" for a socket that had already gone.
    Bench b;
    b.socket = true;
    b.last_rx = b.now;
    b.step();
    b.socket = false;   // and the last-byte stamp stays where the feed left it

    SupervisorDecision d;
    while (d.action != SupervisorAction::StartAttempt) {
        REQUIRE(b.now < 10 * kSupervisePeriodUs);
        d = b.step();
    }
    CHECK_FALSE(d.silence_recycle);
    CHECK(d.attempt == 1u);
}

TEST_CASE("socket_is_silent is the one spelling both cores decide with") {
    // The predicate the RX task tests before it tears a socket down, tested
    // directly rather than only through the policy — because the transport's use
    // of it is the half that cannot be reached from the desk, and a constant
    // compared in two places is a constant that drifts.
    using depthcharge::fw::socket_is_silent;

    CHECK_FALSE(socket_is_silent(0, 0));
    CHECK_FALSE(socket_is_silent(kSilenceRecycleUs - 1, 0));
    CHECK(socket_is_silent(kSilenceRecycleUs, 0));
    CHECK(socket_is_silent(kSilenceRecycleUs + 1, 0));

    // Offset from an arbitrary epoch: it is a difference, not an absolute.
    const std::int64_t base = 9'876'543'210;
    CHECK_FALSE(socket_is_silent(base + kSilenceRecycleUs - 1, base));
    CHECK(socket_is_silent(base + kSilenceRecycleUs, base));

    // A stamp from the future — supervise() reads `now` before it loads the
    // atomic, so the last byte can land in between — must never read as silent.
    CHECK_FALSE(socket_is_silent(base, base + 5000));
}

TEST_CASE("the policy's invariants hold over an arbitrary walk of inputs") {
    // A property check rather than another example. The supervisor is a
    // deterministic reducer over (now, socket_connected, wifi_associated,
    // last_rx_us), and the two things that must be true of EVERY trace through
    // it are cheap to state and expensive to notice by example:
    //
    //   1. Two attempts are never closer together than one backoff. Anything
    //      tighter is the supervisor preempting its own handshake, which is the
    //      single way this policy could turn a recoverable outage permanent.
    //   2. A socket that is connected and delivering never produces a
    //      StartAttempt, whatever the Wi-Fi flag or the history says.
    //
    // The walk is a fixed pseudo-random sequence — same shape as the reassembler
    // suite's slot-leak walk, and deterministic for the same reason: a property
    // that fails once in twenty runs is a property nobody fixes.
    WsSupervisor sup;
    std::uint32_t rng = 20260816u;
    const auto next = [&rng](std::uint32_t n) {
        rng = rng * 1103515245u + 12345u;
        return (rng >> 16) % n;
    };

    std::int64_t now = 0;
    std::int64_t last_rx = 0;
    bool socket = false;
    std::int64_t previous_attempt = -1;
    int attempts = 0;

    for (int i = 0; i < 200000; ++i) {
        // Flip the socket occasionally; when it is up, deliver bytes most of the
        // time and go quiet the rest, so both the healthy and the silent paths
        // are exercised for long stretches.
        if (next(400) == 0) { socket = !socket; }
        const bool delivering = socket && next(10) != 0;
        if (delivering) { last_rx = now; }

        SupervisorInput in;
        in.now_us = now;
        in.socket_connected = socket;
        in.wifi_associated = (next(20) != 0);
        in.last_rx_us = last_rx;

        const SupervisorDecision d = sup.poll(in);

        if (d.action == SupervisorAction::StartAttempt) {
            ++attempts;
            if (previous_attempt >= 0) {
                INFO("attempt ", attempts, " at ", now, " after ", previous_attempt);
                CHECK(now - previous_attempt >= kReconnectBackoffUs);
            }
            previous_attempt = now;
            // An attempt is only ever raised against a station that could carry
            // it — the holdoff gate, checked as a property rather than by example.
            CHECK(in.wifi_associated);
        }
        if (delivering) {
            CHECK(d.action != SupervisorAction::StartAttempt);
            CHECK_FALSE(d.silence_recycle);
        }
        now += kSupervisePeriodUs;
    }

    // The walk has to have actually exercised the machinery, or the two
    // properties above are vacuously true over a trace where nothing happened.
    CHECK(attempts > 20);
}

// ---------------------------------------------------------------------------
// The association supervisor — the 2026-08-10 permanent grey.
// ---------------------------------------------------------------------------

namespace {

// Same shape as Bench above: a clock and a boolean, stepped at the poll period.
struct WifiBench {
    depthcharge::fw::WifiSupervisor sup;
    std::int64_t now = 1000;
    bool associated = true;

    depthcharge::fw::WifiSupervisor::Decision step() {
        now += kSupervisePeriodUs;
        return sup.poll(now, associated);
    }
};

}  // namespace

TEST_CASE("a station that comes back on its own is never touched") {
    // The common case, and the one that must stay silent: Arduino's own retry
    // handles a roam or a transient deauth well inside the takeover threshold.
    // A rejoin here would interrupt an association that was already succeeding.
    using depthcharge::fw::kWifiRejoinAfterUs;
    WifiBench b;
    b.step();

    b.associated = false;
    int rejoins = 0;
    const std::int64_t from = b.now;
    while (b.now < from + kWifiRejoinAfterUs - kSupervisePeriodUs) {
        if (b.step().rejoin) { ++rejoins; }
    }
    CHECK(rejoins == 0);

    b.associated = true;
    while (b.now < from + 4 * kWifiRejoinAfterUs) {
        if (b.step().rejoin) { ++rejoins; }
    }
    CHECK(rejoins == 0);
    CHECK(b.sup.rejoins() == 0u);
}

TEST_CASE("a station that stays down is rejoined, and keeps being rejoined") {
    // THE BENCH CASE. Deco blocks the board, the AP answers Arduino's one retry
    // with AUTH_FAIL, and the framework gives up permanently. Before this class
    // the panel stayed grey for the rest of the run; the property that matters
    // is not "it retries once" but "it never stops".
    using depthcharge::fw::kWifiRejoinAfterUs;
    using depthcharge::fw::kWifiRejoinCycleUs;
    WifiBench b;
    b.step();
    b.associated = false;

    const std::int64_t from = b.now;
    int rejoins = 0;
    std::int64_t first_at = 0;
    std::int64_t previous_at = 0;
    // Ten cycles of wall clock, whatever a cycle currently is. The first draft
    // of this asserted a hardcoded `rejoins >= 5`, which was really a statement
    // about kWifiRejoinCycleUs being 10 s; halving that constant turned a
    // correct implementation red for no reason. What is being tested is the
    // CADENCE, so measure the cadence.
    while (b.now < from + 10 * kWifiRejoinCycleUs) {
        const auto d = b.step();
        if (!d.rejoin) { continue; }
        ++rejoins;
        CHECK(d.attempt == static_cast<std::uint32_t>(rejoins));
        CHECK(d.down_us >= kWifiRejoinAfterUs);
        if (first_at == 0) {
            first_at = b.now;
        } else {
            // Never faster than the cycle — a retry landing on an association
            // still in flight is the one way this could make an outage worse.
            CHECK(b.now - previous_at >= kWifiRejoinCycleUs);
            CHECK(b.now - previous_at < kWifiRejoinCycleUs + 2 * kSupervisePeriodUs);
        }
        previous_at = b.now;
    }

    // First one a takeover threshold after the station went down, then one per
    // cycle for as long as it stays down — and the point is that it never stops.
    CHECK(first_at - from >= kWifiRejoinAfterUs);
    CHECK(first_at - from < kWifiRejoinAfterUs + 2 * kSupervisePeriodUs);
    CHECK(rejoins >= 8);
    CHECK(b.sup.rejoins() == static_cast<std::uint32_t>(rejoins));
}

TEST_CASE("the rejoin counter resets once the station is back") {
    // So a later outage is reported as attempt #1 rather than #37, and so the
    // log line says how bad THIS outage is rather than how long the board has
    // been up.
    using depthcharge::fw::kWifiRejoinCycleUs;
    WifiBench b;
    b.step();
    b.associated = false;
    while (b.now < 3 * kWifiRejoinCycleUs) { b.step(); }
    CHECK(b.sup.rejoins() > 0u);

    b.associated = true;
    b.step();
    CHECK(b.sup.rejoins() == 0u);

    b.associated = false;
    const std::int64_t from = b.now;
    std::uint32_t first_attempt = 0;
    while (b.now < from + 2 * kWifiRejoinCycleUs) {
        const auto d = b.step();
        if (d.rejoin && first_attempt == 0) { first_attempt = d.attempt; }
    }
    CHECK(first_attempt == 1u);
}

TEST_CASE("the two supervisors compose: rejoin brings the station back, then the socket") {
    // The end-to-end shape of the fix. While the station is down the WS
    // supervisor holds (one line, no wasted handles); the Wi-Fi supervisor
    // rejoins; and the moment association returns the held socket attempt goes
    // out on the very next poll rather than a retry cycle later.
    using depthcharge::fw::kWifiRejoinAfterUs;
    Bench ws;
    depthcharge::fw::WifiSupervisor wifi;

    ws.socket = true;
    ws.step();

    ws.socket = false;
    ws.wifi = false;
    int holdoffs = 0;
    int rejoins = 0;
    const std::int64_t from = ws.now;
    while (ws.now < from + 3 * kWifiRejoinAfterUs) {
        const auto d = ws.step();
        if (d.wifi_holdoff) { ++holdoffs; }
        if (d.action == SupervisorAction::StartAttempt) { FAIL("attempted with no station"); }
        if (wifi.poll(ws.now, ws.wifi).rejoin) { ++rejoins; }
    }
    CHECK(holdoffs == 1);
    CHECK(rejoins >= 1);

    // Station back. The next poll fires the socket attempt.
    ws.wifi = true;
    const auto d = ws.step();
    CHECK(d.action == SupervisorAction::StartAttempt);
    CHECK(wifi.poll(ws.now, true).rejoin == false);
}

TEST_CASE("no rejoin ever preempts an association that could still succeed") {
    // THE 2026-08-16 LIVELOCK, reduced to the one property that would have
    // caught it. A rejoin begins with WiFi.disconnect(), so it destroys whatever
    // association attempt is in flight; the cadence must therefore always leave
    // room for one to finish, unconditionally and whatever the station reports.
    //
    // The version of this file that shipped on 2026-08-10 asserted the opposite
    // for one case — see the case below for the bench run that convicted it.
    using depthcharge::fw::kObservedAssociationUs;
    using depthcharge::fw::kWifiRejoinCycleUs;

    depthcharge::fw::WifiSupervisor sup;
    std::int64_t now = 1000;

    std::int64_t previous = 0;
    int seen = 0;
    while (seen < 8) {
        now += kSupervisePeriodUs;
        // Two arguments and no third. The supervisor used to take a
        // WL_CONNECT_FAILED flag here and shorten the cadence on it; that
        // parameter is gone, so the property below is now structural rather than
        // conditional — there is no input that can buy a faster retry.
        if (sup.poll(now, /*associated=*/false).rejoin) {
            if (previous != 0) {
                CHECK(now - previous >= kWifiRejoinCycleUs);
                CHECK(now - previous >= kObservedAssociationUs);
            }
            previous = now;
            ++seen;
        }
    }
}

namespace {

// A station that behaves the way the 2026-08-16 bench log says the real one
// does, which is the whole point: the two outcomes take VERY different amounts
// of time, and that asymmetry is what the old fast path was built on and what
// broke it.
//
//   * a refusal comes back in ~60 ms   (measured 2026-08-10: rejoin #1 at
//     23:02:10.419, Reason 202 at 23:02:10.473)
//   * an association takes ~4 s        (measured 2026-08-16: begin() at
//     uptime 3786 ms, `wifi up` at 7821 ms)
//   * and WiFi.disconnect() throws away whatever was in flight
//
// The last line is the one nothing modelled before. `WL_CONNECT_FAILED` is
// STICKY: once the AP has refused once, the flag stays set for as long as no
// later attempt resolves — and if every attempt is aborted before it can
// resolve, that is forever.
struct FakeStation {
    static constexpr std::int64_t kRefusalUs = 60 * 1000;
    static constexpr std::int64_t kAssociateUs = depthcharge::fw::kObservedAssociationUs;

    std::int64_t opens_at = 0;        // when the AP stops refusing us
    std::int64_t attempt_started = -1;
    bool associated = false;
    bool refused_latched = false;
    int refusals = 0;                 // what the AP actually answered

    // WiFi.disconnect() then WiFi.begin(): the in-flight attempt dies here.
    void rejoin(std::int64_t now) {
        attempt_started = now;
    }

    void tick(std::int64_t now) {
        if (associated || attempt_started < 0) { return; }
        if (now < opens_at) {
            if (now - attempt_started >= kRefusalUs) {
                refused_latched = true;
                ++refusals;
                attempt_started = -1;
            }
            return;
        }
        if (now - attempt_started >= kAssociateUs) { associated = true; }
    }
};

}  // namespace

TEST_CASE("a station whose AP stops refusing gets back on, without being reset") {
    // THE BENCH RUN THIS FILE EXISTS TO PREVENT A SECOND TIME.
    //
    // 2026-08-16, Deco blocking the board: 388 rejoin calls produced TWO
    // AUTH_FAIL responses from the AP. If each call had been a real attempt
    // being refused there would have been ~388 of them, one per call, 60 ms
    // apart. They were not reaching the AP at all — the 1 s refused-retry
    // cadence was aborting each attempt before it could resolve, so the station
    // never re-associated and the only thing that recovered the board was a
    // power cycle (`rst:0x1 (POWERON)` in the log, four seconds before the
    // association that "worked").
    //
    // The fast path was measured on 2026-08-10 against an AP that was ACTIVELY
    // REFUSING, where a 60 ms answer really does make 1 s generous. It was then
    // read as covering the case it exists for — an AP that has STOPPED refusing
    // — where the attempt needs seconds and the retry destroys it. Same species
    // as the four supersessions in ARCHITECTURE §9: a measurement that answered
    // a nearby question, read as answering the one that mattered.
    using depthcharge::fw::kWifiRejoinAfterUs;

    FakeStation sta;
    depthcharge::fw::WifiSupervisor sup;
    std::int64_t now = 1000;

    // The AP refuses for the first two minutes, then quietly opens.
    sta.opens_at = now + 120 * 1000 * 1000LL;
    const std::int64_t opened_at = sta.opens_at;

    // Give it ten minutes of wall clock after that to get back on. The real
    // board had five and did not.
    const std::int64_t give_up_at = opened_at + 10 * 60 * 1000 * 1000LL;
    int rejoins = 0;
    while (!sta.associated && now < give_up_at) {
        now += kSupervisePeriodUs;
        sta.tick(now);
        const auto d = sup.poll(now, sta.associated);
        if (d.rejoin) {
            ++rejoins;
            sta.rejoin(now);
        }
    }

    REQUIRE(sta.associated);
    // Back on within a couple of cycles of the AP opening — not "eventually".
    CHECK(now - opened_at < kWifiRejoinAfterUs + 3 * depthcharge::fw::kWifiRejoinCycleUs);

    // And the counting check that names the real defect rather than its symptom:
    // while the AP was refusing, EVERY rejoin must have produced an answer from
    // it. A rejoin count far above the refusal count means attempts are being
    // thrown away before they arrive, which is exactly the 388-vs-2 signature.
    INFO("rejoins ", rejoins, " refusals ", sta.refusals);
    CHECK(sta.refusals >= rejoins - 1);
}
