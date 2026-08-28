// test_seed_schedule.cpp — the arithmetic that decides how often this board
// asks Binance for a seed.
//
// M5 stage D-A2, and this is the highest-value test file in the stage. Every
// other failure here is grey-panel-and-recoverable; getting THIS wrong asks too
// often and earns an IP ban, which is the one failure in the milestone that
// cannot be undone from the desk.
#include <doctest/doctest.h>

#include "seed_schedule.hpp"

using depthcharge::fw::kSeedDeadlineUs;
using depthcharge::fw::kSeedGiveUpAfter;
using depthcharge::fw::kSeedRateLimitBackoffUs;
using depthcharge::fw::kSeedRetryCycleUs;
using depthcharge::fw::SeedAction;
using depthcharge::fw::SeedHold;
using depthcharge::fw::SeedInput;
using depthcharge::fw::SeedSchedule;

namespace {
SeedInput want(std::int64_t now, bool in_flight = false, bool overflowed = false) {
    SeedInput in;
    in.wanted = true;
    in.in_flight = in_flight;
    in.buffer_overflowed = overflowed;
    in.socket_up = true;      // the ordinary case; the rule has its own cases below
    in.now_us = now;
    return in;
}

SeedInput want_no_socket(std::int64_t now, bool in_flight = false) {
    SeedInput in = want(now, in_flight);
    in.socket_up = false;
    return in;
}
}  // namespace

TEST_CASE("nothing is issued while nothing wants a seed") {
    SeedSchedule s;
    SeedInput in;
    in.now_us = 1'000;
    CHECK(s.step(in) == SeedAction::None);
    CHECK(s.due_in_us(in) == -1);
}

TEST_CASE("the first fetch is issued immediately") {
    SeedSchedule s;
    CHECK(s.due_in_us(want(0)) == 0);
    CHECK(s.step(want(0)) == SeedAction::Issue);
}

// LEVEL, NOT EDGE. The adapter re-raises `wanted` from inside `drop_book`, so a
// schedule that consumed the request on an edge would swallow the one the drop
// just made and leave the panel grey over a healthy socket with nothing
// climbing. That is the M4 stage A2 defect (ARCHITECTURE §9, 2026-08-20).
TEST_CASE("the request is read as a level and is never consumed") {
    SeedSchedule s;
    REQUIRE(s.step(want(0)) == SeedAction::Issue);
    s.note_issued(0);
    s.note_result(false, 0, 100);

    // Still wanted after a failure, and issued again once the cycle elapses —
    // without anything having to re-assert it.
    CHECK(s.step(want(kSeedRetryCycleUs - 1)) == SeedAction::None);
    CHECK(s.step(want(kSeedRetryCycleUs)) == SeedAction::Issue);
}

// THE CENTRAL SAFETY PROPERTY. A retry must not be able to begin inside the
// fetch it is retrying: two seeds in flight is two TLS sessions on a board
// sized for one spare, and two 50-weight requests where the policy meant one.
TEST_CASE("a retry can never start inside its own fetch's deadline") {
    static_assert(kSeedRetryCycleUs > kSeedDeadlineUs,
                  "the header's own assertion, restated where a reader will see it");

    SeedSchedule s;
    REQUIRE(s.step(want(0)) == SeedAction::Issue);
    s.note_issued(0);

    // While in flight, nothing is ever issued, at any instant inside the deadline.
    for (std::int64_t t = 0; t < kSeedDeadlineUs; t += kSeedDeadlineUs / 32) {
        const SeedAction a = s.step(want(t, /*in_flight=*/true));
        CAPTURE(t);
        CHECK(a != SeedAction::Issue);
    }
}

TEST_CASE("a fetch that outruns the deadline is abandoned") {
    SeedSchedule s;
    REQUIRE(s.step(want(0)) == SeedAction::Issue);
    s.note_issued(0);
    CHECK(s.step(want(kSeedDeadlineUs - 1, true)) == SeedAction::None);
    CHECK(s.step(want(kSeedDeadlineUs, true)) == SeedAction::Abandon);
}

// ABANDON BEATS THE DEADLINE. `test_binance_adapter.cpp` measures why: once the
// pre-seed buffer has overflowed, the body is adopted, fails its bracket against
// the zeroed buffer and is dropped inside one call — so finishing the transfer
// buys nothing and costs the socket and the rest of the deadline.
TEST_CASE("a fetch whose pre-seed buffer overflowed is abandoned at once") {
    SeedSchedule s;
    REQUIRE(s.step(want(0)) == SeedAction::Issue);
    s.note_issued(0);
    CHECK(s.step(want(1'000, /*in_flight=*/true, /*overflowed=*/true)) == SeedAction::Abandon);
}

// 429 IS NOT A TRANSPORT FAILURE, IT IS THE VENUE SAYING THE SCHEDULE IS WRONG.
TEST_CASE("a 429 lengthens the interval well past the ordinary cycle") {
    SeedSchedule s;
    REQUIRE(s.step(want(0)) == SeedAction::Issue);
    s.note_issued(0);
    s.note_result(false, 429, 500);
    CHECK(s.rate_limited() == 1);

    // The ordinary cycle would have re-issued here. It must not.
    CHECK(s.step(want(kSeedRetryCycleUs)) == SeedAction::None);
    CHECK(s.hold() == SeedHold::RateLimited);
    CHECK(s.step(want(kSeedRateLimitBackoffUs - 1)) == SeedAction::None);
    CHECK(s.step(want(kSeedRateLimitBackoffUs)) == SeedAction::Issue);
}

TEST_CASE("a 418 stops the loop and a reconnect does not restart it") {
    SeedSchedule s;
    REQUIRE(s.step(want(0)) == SeedAction::Issue);
    s.note_issued(0);
    s.note_result(false, 418, 500);
    CHECK(s.banned());

    CHECK(s.step(want(kSeedRateLimitBackoffUs * 10)) == SeedAction::None);
    CHECK(s.hold() == SeedHold::Banned);
    CHECK(s.due_in_us(want(kSeedRateLimitBackoffUs * 10)) == -1);

    // A 418 is the venue's decision about this IP. Reconnecting does not change
    // its mind, so unlike a give-up it survives a new socket.
    s.note_socket_change();
    CHECK(s.banned());
    CHECK(s.step(want(kSeedRateLimitBackoffUs * 20)) == SeedAction::None);
}

TEST_CASE("consecutive failures give up, and the give-up is reported exactly once") {
    SeedSchedule s;
    std::int64_t t = 0;
    std::size_t give_ups = 0;
    for (std::uint32_t i = 0; i < kSeedGiveUpAfter; ++i) {
        REQUIRE(s.step(want(t)) == SeedAction::Issue);
        s.note_issued(t);
        s.note_result(false, 0, t + 100);
        t += kSeedRetryCycleUs;
    }
    // The next pass gives up...
    for (int pass = 0; pass < 8; ++pass) {
        if (s.step(want(t)) == SeedAction::GiveUp) { ++give_ups; }
        t += kSeedRetryCycleUs;
    }
    CHECK(s.gave_up());
    CHECK(give_ups == 1);        // reported once, not every pass
    CHECK(s.hold() == SeedHold::GaveUp);
    CHECK(s.due_in_us(want(t)) == -1);
}

// A new socket is new information: the commonest cause of five consecutive
// failures is a link that has since come back.
TEST_CASE("a new socket re-arms a give-up") {
    SeedSchedule s;
    std::int64_t t = 0;
    for (std::uint32_t i = 0; i < kSeedGiveUpAfter; ++i) {
        REQUIRE(s.step(want(t)) == SeedAction::Issue);
        s.note_issued(t);
        s.note_result(false, 0, t + 100);
        t += kSeedRetryCycleUs;
    }
    REQUIRE(s.step(want(t)) == SeedAction::GiveUp);
    REQUIRE(s.gave_up());

    s.note_socket_change();
    CHECK_FALSE(s.gave_up());
    CHECK(s.consecutive_failures() == 0);
    CHECK(s.step(want(t)) == SeedAction::Issue);
}

TEST_CASE("a success clears the failure run") {
    SeedSchedule s;
    REQUIRE(s.step(want(0)) == SeedAction::Issue);
    s.note_issued(0);
    s.note_result(false, 0, 100);
    CHECK(s.consecutive_failures() == 1);

    REQUIRE(s.step(want(kSeedRetryCycleUs)) == SeedAction::Issue);
    s.note_issued(kSeedRetryCycleUs);
    s.note_result(true, 200, kSeedRetryCycleUs + 100);
    CHECK(s.consecutive_failures() == 0);
}

// `due_in_us` is what the feed task folds into its wait. A wrong answer here is
// a feed task that sleeps through its own fetch, which at Binance would be
// permanent: the liveness watchdog is never armed on that build, so nothing
// else would ever wake it.
TEST_CASE("due_in_us never sleeps through a deadline or a cycle") {
    SeedSchedule s;
    CHECK(s.due_in_us(want(0)) == 0);
    s.note_issued(0);

    SUBCASE("in flight, it counts down to the deadline and never past it") {
        for (std::int64_t t = 0; t <= kSeedDeadlineUs; t += kSeedDeadlineUs / 16) {
            const std::int64_t d = s.due_in_us(want(t, /*in_flight=*/true));
            CAPTURE(t);
            CHECK(d >= 0);
            CHECK(t + d <= kSeedDeadlineUs);
        }
    }
    SUBCASE("cooling down, it counts down to the cycle and never past it") {
        s.note_result(false, 0, 10);
        for (std::int64_t t = 0; t <= kSeedRetryCycleUs; t += kSeedRetryCycleUs / 16) {
            const std::int64_t d = s.due_in_us(want(t));
            CAPTURE(t);
            CHECK(d >= 0);
            CHECK(t + d <= kSeedRetryCycleUs);
        }
    }
}

// THE BUDGET, AS ARITHMETIC RATHER THAN AS A HOPE. /api/v3/depth?limit=1000
// costs 50 weight against 6,000/minute.
TEST_CASE("the worst-case request rate stays far inside the venue's weight budget") {
    constexpr std::int64_t kUsPerMinute = 60'000'000;
    constexpr int kWeightPerFetch = 50;
    constexpr int kBudgetPerMinute = 6'000;

    // Every attempt failing is the worst case: the cycle never lengthens and
    // the schedule never stops asking (until the give-up, which only helps).
    const int fetches_per_minute = static_cast<int>(kUsPerMinute / kSeedRetryCycleUs);
    const int weight = fetches_per_minute * kWeightPerFetch;
    CHECK(weight <= kBudgetPerMinute / 10);   // an order of magnitude of headroom
}

// ---------------------------------------------------------------------------
// A SEED FETCH AND A TLS RECONNECT MUST NEVER OVERLAP
// ---------------------------------------------------------------------------
//
// The board measured why: one fetch takes the largest free internal block to
// **10,740 B**, already below the 16,717 B a TLS session needs. A reconnect
// building a third session against that is the one arrangement guaranteed not
// to fit — and the failure lands on the RECONNECT, which is the half that
// matters. The correctness ground is independent and just as strong: a body is
// bracketed against the diff stream, so a body whose stream has ended is
// worthless before it arrives.

TEST_CASE("no fetch is issued while the socket is down") {
    SeedSchedule s;
    CHECK(s.step(want_no_socket(0)) == SeedAction::None);
    CHECK(s.hold() == SeedHold::SocketDown);

    // ...and it is reported as the SOCKET, not as the schedule, so a bench
    // reading the hold line is not sent to look at the retry cycle.
    CHECK(s.hold() != SeedHold::CoolingDown);

    // The moment the socket returns, it issues — no extra cooling-off, because
    // nothing was spent.
    CHECK(s.step(want(0)) == SeedAction::Issue);
}

TEST_CASE("a fetch in flight is abandoned the instant the socket drops") {
    SeedSchedule s;
    REQUIRE(s.step(want(0)) == SeedAction::Issue);
    s.note_issued(0);

    // Healthy socket, well inside the deadline: keep going.
    CHECK(s.step(want(1'000'000, /*in_flight=*/true)) == SeedAction::None);

    // Socket drops: abandon at once, not at the deadline 14 s later.
    CHECK(s.step(want_no_socket(1'000'001, /*in_flight=*/true)) == SeedAction::Abandon);
}

// ABANDON-ON-DROP OUTRANKS EVERY REASON TO KEEP GOING, including a buffer that
// has not overflowed and a deadline that has not expired.
TEST_CASE("the drop outranks the deadline and the buffer") {
    SeedSchedule s;
    REQUIRE(s.step(want(0)) == SeedAction::Issue);
    s.note_issued(0);
    for (std::int64_t t = 0; t < kSeedDeadlineUs; t += kSeedDeadlineUs / 8) {
        CAPTURE(t);
        CHECK(s.step(want_no_socket(t, /*in_flight=*/true)) == SeedAction::Abandon);
    }
}

// `due_in_us` must ask to be woken IMMEDIATELY when the socket is down under a
// fetch, or the abandon waits for the deadline and the reconnect contends with
// a fetch that is already condemned.
TEST_CASE("due_in_us asks for an immediate wake-up to abandon on a drop") {
    SeedSchedule s;
    REQUIRE(s.step(want(0)) == SeedAction::Issue);
    s.note_issued(0);
    CHECK(s.due_in_us(want_no_socket(1'000, /*in_flight=*/true)) == 0);

    SUBCASE("and nothing at all is wanted while the socket is down and idle") {
        SeedSchedule s2;
        CHECK(s2.due_in_us(want_no_socket(0)) == -1);
    }
}
