// test_ws_ping_disabled.cpp — the pingless arm's one branch.
//
// WHY THIS IS A SEPARATE BINARY AND NOT THREE MORE CASES NEXT DOOR.
//
// `kClientPingEnabled` is a compile-time constant (`DC_WS_PING`), so the branch
// it guards cannot be reached by any test in a binary built with the ping on —
// and `test_ws_ping.cpp`'s cases all assume it is on, so they cannot simply be
// linked twice the way `test_anvil_adapter.cpp` is across the two parsers. The
// project already owns this shape: `dc_tests_streaming` exists to run one source
// against a second link configuration. This is the same trick with one flag.
//
// WHAT IT PROTECTS, because one snprintf is a thin thing to build a target for.
// The pingless arm used to print `no round-trip yet` on every statistics block
// forever, which on the line is indistinguishable from a venue that IS being
// asked and is not answering. Those are opposite findings — one says "we chose
// not to measure", the other says "the venue has gone quiet on a live socket",
// and the second is the 2026-08-16 00:12 stall. A build flag that makes a
// diagnostic line lie about which of the two you are reading is worse than no
// flag, so the property is worth a target: **in this arm the line never claims a
// reading, whatever state the object is in.**
#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "ws_ping.hpp"

using depthcharge::fw::kClientPingEnabled;
using depthcharge::fw::kPingPeriodUs;
using depthcharge::fw::PingProbe;

namespace {

std::string rendered(const PingProbe& p, std::int64_t now_us) {
    char buf[160];
    p.render(now_us, buf, sizeof buf);
    return std::string(buf);
}

}  // namespace

TEST_CASE("this binary really is the pingless arm") {
    // Guards the guard: if the build flag stopped reaching this target, every
    // case below would pass vacuously against the enabled render and prove
    // nothing at all.
    CHECK_FALSE(kClientPingEnabled);
}

TEST_CASE("the pingless arm says it is not asking, never that nothing came back") {
    PingProbe p;
    p.note_connect(0);
    const std::string line = rendered(p, 5 * kPingPeriodUs);
    CHECK(line.find("disabled at build") != std::string::npos);
    CHECK(line.find("no round-trip yet") == std::string::npos);
    CHECK(line.find("rtt ") == std::string::npos);
    CHECK(line.find("waiting") == std::string::npos);
}

TEST_CASE("and it holds whatever state the object is carrying") {
    // The transport in this arm never calls note_ping_sent, so a probe with
    // readings in it cannot occur on the board. Asserted anyway, because the
    // property that matters is of the LINE and not of the caller: this arm must
    // not be able to print a round-trip, and a future edit that made `render`
    // fall through to the measured branch on a non-empty probe would be caught
    // here rather than on a bench evening.
    PingProbe p;
    p.note_connect(0);
    p.note_ping_sent(kPingPeriodUs);
    p.note_pong(kPingPeriodUs + 87'000);
    CHECK(p.measured());                       // the object recorded it...
    CHECK(p.last_rtt_us() == 87'000);
    const std::string line = rendered(p, 2 * kPingPeriodUs);
    CHECK(line.find("disabled at build") != std::string::npos);
    CHECK(line.find("rtt 87 ms") == std::string::npos);   // ...and the line does not show it

    // An outstanding ping is likewise not reported as one.
    p.note_ping_sent(3 * kPingPeriodUs);
    CHECK(rendered(p, 3 * kPingPeriodUs + 40'000'000).find("waiting") == std::string::npos);
}

TEST_CASE("render still truncates rather than overruns in this arm") {
    // The disabled branch is its own snprintf and its own append_truncating
    // call, so it inherits none of the other branch's coverage.
    PingProbe p;
    p.note_connect(0);
    for (std::size_t cap = 1; cap < 64; ++cap) {
        char buf[96];
        std::memset(buf, '\xAB', sizeof buf);
        const std::size_t n = p.render(5'000'000, buf, cap);
        CHECK(n < cap);
        CHECK(std::strlen(buf) < cap);
        for (std::size_t i = cap; i < sizeof buf; ++i) {
            CHECK(static_cast<unsigned char>(buf[i]) == 0xABu);
        }
    }
    CHECK(p.render(5'000'000, nullptr, 32) == 0);
}
