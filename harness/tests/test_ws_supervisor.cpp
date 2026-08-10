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
// attempt it is, and the two guarantees the constants exist to provide — that an
// attempt is never disturbed inside its own budget, and that a retry never comes
// back to a handle whose task has not finished dying. Which handle gets opened,
// and what the library does with it, is WsTransport's half and stays on the
// board.
#include <doctest/doctest.h>

#include <cstdint>

#include "ws_supervisor.hpp"

using depthcharge::fw::kClientSelfExitUs;
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

TEST_CASE("a retry never returns to a handle that is still dying") {
    // The two-handle invariant, checked as behaviour rather than only as the
    // static_assert in the header. Handles alternate, so attempt N and attempt
    // N+2 share one — and the one attempt N used became reusable
    // kClientSelfExitUs after the socket that triggered it aborted.
    Bench b;
    b.socket = true;
    b.step();
    const std::int64_t died = b.now;
    b.socket = false;

    const std::int64_t first = b.run_to_attempt(died);
    REQUIRE(first >= 0);
    const std::int64_t second = b.run_to_attempt(died + first);
    REQUIRE(second >= 0);

    // Attempt #2 reopens the handle that was live when the feed died at `died`,
    // whose task exits kClientSelfExitUs later.
    CHECK(first + second > kClientSelfExitUs);
}

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
