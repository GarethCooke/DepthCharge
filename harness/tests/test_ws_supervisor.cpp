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
// attempt it is, and the guarantee the constants exist to provide — that an
// attempt is never disturbed inside its own budget. What the platform does with
// a decision is WsTransport's half and stays on the board.
#include <doctest/doctest.h>

#include <cstdint>

#include "ws_supervisor.hpp"

using depthcharge::fw::kHandshakeBudgetUs;
using depthcharge::fw::kReconnectBackoffUs;
using depthcharge::fw::kRetryCycleUs;
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

    SupervisorDecision step() {
        SupervisorInput in;
        in.now_us = now;
        in.socket_connected = socket;
        in.wifi_associated = wifi;
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

TEST_CASE("a refused attempt is retried in a second, not a cycle") {
    // THE 2026-08-10 RECOVERY TERM. An AP that is refusing answers in ~60 ms, so
    // waiting out kWifiRejoinCycleUs before trying again means noticing that it
    // has stopped refusing up to a whole cycle late. That was worth more of the
    // 25.6 s grey than anything else still under this file's control.
    using depthcharge::fw::kWifiRefusedRetryUs;
    using depthcharge::fw::kWifiRejoinAfterUs;
    using depthcharge::fw::kWifiRejoinCycleUs;

    depthcharge::fw::WifiSupervisor sup;
    std::int64_t now = 1000;
    const auto step = [&](bool associated, bool failed) {
        now += kSupervisePeriodUs;
        return sup.poll(now, associated, failed);
    };

    step(true, false);

    // Station down, and the framework's own WL_CONNECT_FAILED is already
    // latched. That must NOT shorten the first takeover: it is left over from
    // the framework's retry and says nothing about an attempt of ours.
    std::int64_t first_at = 0;
    const std::int64_t from = now;
    while (first_at == 0) {
        if (step(false, true).rejoin) { first_at = now; }
        REQUIRE(now - from < 2 * kWifiRejoinAfterUs);
    }
    CHECK(first_at - from >= kWifiRejoinAfterUs);

    // From here every attempt is refused, so retries come at the fast cadence.
    std::int64_t previous = first_at;
    for (int k = 0; k < 5; ++k) {
        std::int64_t at = 0;
        while (at == 0) {
            if (step(false, true).rejoin) { at = now; }
        }
        CHECK(at - previous >= kWifiRefusedRetryUs);
        CHECK(at - previous < kWifiRejoinCycleUs);
        previous = at;
    }
}

TEST_CASE("an attempt that is merely still running is never preempted") {
    // The other side of the same gate, and the one that could do harm: while
    // WL_CONNECT_FAILED is NOT set the attempt may still be in flight, and a
    // retry on top of it is how this code could turn a recoverable outage into a
    // permanent one. That case keeps the full cycle.
    using depthcharge::fw::kObservedAssociationUs;
    using depthcharge::fw::kWifiRejoinCycleUs;

    depthcharge::fw::WifiSupervisor sup;
    std::int64_t now = 1000;
    const auto step = [&](bool failed) {
        now += kSupervisePeriodUs;
        return sup.poll(now, false, failed);
    };

    std::int64_t previous = 0;
    int seen = 0;
    while (seen < 4) {
        if (step(false).rejoin) {
            if (previous != 0) {
                CHECK(now - previous >= kWifiRejoinCycleUs);
                CHECK(now - previous >= kObservedAssociationUs);
            }
            previous = now;
            ++seen;
        }
    }
}
