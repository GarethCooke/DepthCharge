// test_ws_ping.cpp — the client ping round-trip, on the desk.
//
// The sixth firmware header the host build knows about. What it produces is a
// number that will be read as "Anvil has N milliseconds of our frames queued",
// and every way it can be wrong produces a plausible N: pairing a pong with the
// wrong ping halves or doubles it, treating an unsolicited pong as an answer
// invents a small one, measuring the cadence from the send instead of the
// arrival quietly shortens it under load, and a socket that dies mid-flight
// silently drops the largest reading the instrument can take.
//
// So the shape of this file is: pin the pairing rules first, because they are
// the correctness argument for the whole file, then the arithmetic against
// hand-computed answers, then the two REAL numbers this project has measured —
// the 87 ms transatlantic baseline and the 111 s of queued backlog — because a
// resolution that cannot show the first or a range that cannot show the second
// makes the instrument useless at exactly the two points it exists for.
#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "ws_ping.hpp"

using depthcharge::fw::kPingPeriodUs;
using depthcharge::fw::PingProbe;

namespace {

std::string rendered(const PingProbe& p, std::int64_t now_us) {
    char buf[160];
    p.render(now_us, buf, sizeof buf);
    return std::string(buf);
}

// One complete round-trip: wait out the cadence, send, answer `rtt_us` later.
// Returns the instant the pong landed, so a caller can chain them.
std::int64_t round_trip(PingProbe& p, std::int64_t from_us, std::int64_t rtt_us) {
    const std::int64_t send = from_us + kPingPeriodUs;
    REQUIRE(p.ping_due(send));
    p.note_ping_sent(send);
    p.note_pong(send + rtt_us);
    return send + rtt_us;
}

}  // namespace

TEST_CASE("a probe that has measured nothing says so rather than reporting zero") {
    PingProbe p;
    CHECK_FALSE(p.measured());
    CHECK_FALSE(p.in_flight());
    CHECK(p.last_rtt_us() == 0);
    CHECK(p.outstanding_us(5'000'000) == 0);
    // "no reading yet" and "the queue is empty" are different statements and
    // exactly one of them is reassuring. The console must not be able to print
    // the reassuring one by accident.
    CHECK(rendered(p, 5'000'000).find("no round-trip yet") != std::string::npos);
    CHECK(rendered(p, 5'000'000).find("rtt ") == std::string::npos);
}

TEST_CASE("the first ping waits a period after the connect, not zero") {
    // A connect is followed by Anvil's snapshot and the first book frames. A
    // ping racing them measures the snapshot's own drain and reports it as
    // backlog — a real number about the wrong thing.
    PingProbe p;
    p.note_connect(1'000'000);
    CHECK_FALSE(p.ping_due(1'000'000));
    CHECK_FALSE(p.ping_due(1'000'000 + kPingPeriodUs - 1));
    CHECK(p.ping_due(1'000'000 + kPingPeriodUs));
}

TEST_CASE("ONE PING IN FLIGHT: no second ping goes out until the first is answered") {
    // The correctness argument for the whole file. RFC 6455 §5.5.2 lets a peer
    // answer several outstanding pings with ONE pong, and nothing on the wire
    // says which it answered — so a second ping in flight makes every
    // subsequent round-trip a guess. This is the rule that stops the guess
    // being possible rather than making it carefully.
    PingProbe p;
    p.note_connect(0);
    p.note_ping_sent(kPingPeriodUs);

    // An hour later, with no pong, still no second ping.
    CHECK_FALSE(p.ping_due(kPingPeriodUs + 3600ll * 1'000'000ll));
    CHECK(p.in_flight());
    CHECK(p.pings() == 1);

    // The pong frees it, and the next one is a full period after the PONG.
    p.note_pong(kPingPeriodUs + 5'000'000);
    CHECK_FALSE(p.in_flight());
    CHECK_FALSE(p.ping_due(kPingPeriodUs + 5'000'000 + kPingPeriodUs - 1));
    CHECK(p.ping_due(kPingPeriodUs + 5'000'000 + kPingPeriodUs));
}

TEST_CASE("the cadence runs from the PONG, not from the send") {
    // Measuring from the send would let a slow round-trip shorten the gap
    // between its own completion and the next ping — so the sicker the socket,
    // the harder this instrument would push on it. Backwards, and it would show
    // up only under exactly the load the reading exists to measure.
    PingProbe p;
    p.note_connect(0);
    const std::int64_t send = kPingPeriodUs;
    p.note_ping_sent(send);
    // A round-trip of nine periods: measured from the send, the next ping would
    // already be overdue the instant the pong lands.
    const std::int64_t pong = send + 9 * kPingPeriodUs;
    p.note_pong(pong);
    CHECK_FALSE(p.ping_due(pong));
    CHECK_FALSE(p.ping_due(pong + kPingPeriodUs - 1));
    CHECK(p.ping_due(pong + kPingPeriodUs));
}

TEST_CASE("AN UNSOLICITED PONG IS COUNTED, NEVER TREATED AS AN ANSWER") {
    // RFC 6455 §5.5.3: "A Pong frame MAY be sent unsolicited. This serves as a
    // unidirectional heartbeat." Treating one as the answer to a ping we never
    // sent computes a round-trip against a stale or zero timestamp — a small,
    // plausible, entirely invented number, which is the worst kind this
    // instrument could print.
    PingProbe p;
    p.note_connect(0);
    p.note_pong(5'000'000);
    CHECK(p.unsolicited() == 1);
    CHECK(p.pongs() == 0);
    CHECK_FALSE(p.measured());
    CHECK(p.last_rtt_us() == 0);
    CHECK(rendered(p, 5'000'000).find("no round-trip yet") != std::string::npos);

    // And it does not disturb a real round-trip that follows it.
    const std::int64_t pong = round_trip(p, 5'000'000, 87'000);
    CHECK(p.pongs() == 1);
    CHECK(p.last_rtt_us() == 87'000);
    CHECK(p.unsolicited() == 1);

    // A stray pong AFTER a completed round-trip is also not a reading, and must
    // not overwrite the good one.
    p.note_pong(pong + 1'000'000);
    CHECK(p.unsolicited() == 2);
    CHECK(p.pongs() == 1);
    CHECK(p.last_rtt_us() == 87'000);
}

TEST_CASE("the 87 ms transatlantic baseline is visible, which tenths of a second are not") {
    // Anvil is 52.204.246.224, AWS us-east-1: 87.4 ms median over eight TCP
    // connects (2026-08-11). This is the healthy reading, and the whole reason
    // this file reports milliseconds while every other duration in the firmware
    // reports tenths of a second — SecondsText renders 87 ms as "0.0 s", i.e.
    // as nothing at all, and an instrument whose healthy value is invisible
    // cannot show a change from it.
    PingProbe p;
    p.note_connect(0);
    round_trip(p, 0, 87'000);
    CHECK(p.last_rtt_us() == 87'000);
    CHECK(p.worst_rtt_us() == 87'000);
    const std::string line = rendered(p, 2 * kPingPeriodUs);
    CHECK(line.find("rtt 87 ms") != std::string::npos);
    CHECK(line.find("ping 1/1") != std::string::npos);
}

TEST_CASE("the 111 s backlog is visible too — the range spans five orders of magnitude") {
    // The other real number: `tools/anvil_freshness_probe.py`, 2026-08-11, a
    // socket throttled to 25% of the stream reached 111 s of lag with no
    // plateau. Under the queuing model this file is built on, a pong behind
    // that backlog takes the same 111 s to come back.
    PingProbe p;
    p.note_connect(0);
    round_trip(p, 0, 111'000'000);
    CHECK(p.last_rtt_us() == 111'000'000);
    CHECK(rendered(p, 0).find("rtt 111000 ms") != std::string::npos);
}

TEST_CASE("worst is a high-water mark and a later fast round-trip does not erase it") {
    PingProbe p;
    p.note_connect(0);
    std::int64_t t = round_trip(p, 0, 40'000'000);
    CHECK(p.worst_rtt_us() == 40'000'000);
    t = round_trip(p, t, 90'000);
    CHECK(p.last_rtt_us() == 90'000);
    CHECK(p.worst_rtt_us() == 40'000'000);   // the episode survives its own recovery
    CHECK(p.pings() == 2);
    CHECK(p.pongs() == 2);
}

TEST_CASE("an outstanding ping is a growing lower bound, and it is reported before it completes") {
    // The live half, and the more useful one during an incident: `last_rtt` can
    // be minutes old, whereas this says what the queue is doing right now.
    PingProbe p;
    p.note_connect(0);
    const std::int64_t send = kPingPeriodUs;
    p.note_ping_sent(send);
    CHECK(p.outstanding_us(send) == 0);
    CHECK(p.outstanding_us(send + 31'200'000) == 31'200'000);
    // ...and it is on the line even though no round-trip has completed, because
    // on a badly queued socket THAT is the finding and "no reading yet" alone
    // would hide it.
    //
    // ONE SPELLING, BOTH BRANCHES. An earlier draft said "outstanding" here and
    // "waiting" once a reading existed — two words for one quantity, on a line
    // whose whole job is to be read quickly under a second line using the same
    // vocabulary. The restructure that fixed the early return collapsed them.
    const std::string line = rendered(p, send + 31'200'000);
    CHECK(line.find("no round-trip yet") != std::string::npos);
    CHECK(line.find("waiting 31200 ms") != std::string::npos);

    // Once one has completed, an outstanding second appears beside it.
    p.note_pong(send + 31'200'000);
    const std::int64_t send2 = send + 31'200'000 + kPingPeriodUs;
    p.note_ping_sent(send2);
    const std::string line2 = rendered(p, send2 + 4'000'000);
    CHECK(line2.find("rtt 31200 ms") != std::string::npos);
    CHECK(line2.find("waiting 4000 ms") != std::string::npos);
}

TEST_CASE("a socket that dies mid-flight banks the outstanding time and counts it lost") {
    // The largest reading this instrument can take is the one that never
    // completes — a queue deep enough to outlive the socket. The staleness
    // estimator shipped with exactly this defect for an afternoon (its
    // high-water marks were only written inside note_summary), and it would
    // have arrived here identically.
    PingProbe p;
    p.note_connect(0);
    const std::int64_t send = kPingPeriodUs;
    p.note_ping_sent(send);
    p.note_disconnect(send + 74'000'000);

    CHECK(p.unanswered() == 1);
    CHECK_FALSE(p.in_flight());
    CHECK(p.worst_rtt_us() == 74'000'000);   // a lower bound on a real round-trip
    // ...but NOT a completed measurement: `pongs` is what says a number was
    // actually observed, and the pair is what stops the bound being read as one.
    CHECK(p.pongs() == 0);
    CHECK_FALSE(p.measured());

    // A disconnect with nothing in flight is not an event.
    PingProbe q;
    q.note_connect(0);
    q.note_disconnect(60'000'000);
    CHECK(q.unanswered() == 0);
    CHECK(q.worst_rtt_us() == 0);

    // ...and a short outstanding time never LOWERS an existing peak.
    PingProbe r;
    r.note_connect(0);
    std::int64_t t = round_trip(r, 0, 90'000'000);
    r.note_ping_sent(t + kPingPeriodUs);
    r.note_disconnect(t + kPingPeriodUs + 500'000);
    CHECK(r.worst_rtt_us() == 90'000'000);
}

TEST_CASE("THE DEEPEST READING SURVIVES THE RECONNECT THAT FOLLOWS IT") {
    // The sibling instrument's scar, and the hole this file had until it was
    // reviewed. A round-trip deep enough to outlive its socket is the single
    // most interesting datum this probe can take — and it arrives at
    // note_disconnect, moments before the note_connect that used to erase it.
    // StalenessEstimator lost exactly this way: the 86-minute run of 2026-08-09
    // looked healthy because 21 reconnects wiped the finding every four minutes.
    PingProbe p;
    p.note_connect(0);
    p.note_ping_sent(kPingPeriodUs);
    p.note_disconnect(kPingPeriodUs + 74'000'000);
    CHECK(p.worst_rtt_us() == 74'000'000);
    CHECK(p.worst_rtt_ever_us() == 74'000'000);

    // It is also VISIBLE in the window between the death and the next connect,
    // where the socket has produced no reading of its own. An early return on
    // `measured()` printed a shrug over a 74-second finding.
    const std::string dead = rendered(p, kPingPeriodUs + 74'000'000);
    CHECK(dead.find("no round-trip yet") != std::string::npos);
    CHECK(dead.find("run 74000 ms") != std::string::npos);
    CHECK(dead.find("lost 1") != std::string::npos);

    // The new socket clears its own figures and keeps the run's.
    const std::int64_t reconnect = kPingPeriodUs + 80'000'000;
    p.note_connect(reconnect);
    CHECK(p.worst_rtt_us() == 0);
    CHECK(p.unanswered() == 0);
    CHECK(p.worst_rtt_ever_us() == 74'000'000);
    CHECK(rendered(p, reconnect).find("run 74000 ms") != std::string::npos);

    // A healthy connection after it does not erase the headline either.
    round_trip(p, reconnect, 87'000);
    CHECK(p.worst_rtt_us() == 87'000);
    CHECK(p.worst_rtt_ever_us() == 74'000'000);
    const std::string line = rendered(p, reconnect + 2 * kPingPeriodUs);
    CHECK(line.find("rtt 87 ms (worst 87 ms, run 74000 ms)") != std::string::npos);
}

TEST_CASE("a connect erases the readings — the queue died with the socket") {
    PingProbe p;
    p.note_connect(0);
    std::int64_t t = round_trip(p, 0, 55'000'000);
    p.note_ping_sent(t + kPingPeriodUs);
    CHECK(p.in_flight());

    const std::int64_t reconnect = t + 2 * kPingPeriodUs;
    p.note_connect(reconnect);
    CHECK_FALSE(p.measured());
    CHECK_FALSE(p.in_flight());
    CHECK(p.last_rtt_us() == 0);
    CHECK(p.worst_rtt_us() == 0);
    CHECK(p.pings() == 0);
    CHECK(p.pongs() == 0);
    CHECK(p.connect_attempts() == 2);
    // The cadence restarts too, so the new socket's snapshot is not raced.
    CHECK_FALSE(p.ping_due(reconnect + kPingPeriodUs - 1));
    CHECK(p.ping_due(reconnect + kPingPeriodUs));
}

TEST_CASE("a clock that goes backwards is ignored rather than wrapped") {
    // esp_timer_get_time() is monotonic, so this is defence against a caller
    // mistake — but an unsigned subtraction in the wrong order here prints an
    // 18-quintillion-millisecond round-trip and reads as a catastrophic finding.
    PingProbe p;
    p.note_connect(0);
    p.note_ping_sent(10'000'000);
    p.note_pong(9'000'000);
    CHECK(p.pongs() == 0);
    CHECK(p.last_rtt_us() == 0);
    CHECK(p.worst_rtt_us() == 0);
    // The ping is still consumed — the probe is free again, not wedged.
    CHECK_FALSE(p.in_flight());
    CHECK(p.outstanding_us(20'000'000) == 0);
}

TEST_CASE("render truncates rather than overruns, and always terminates") {
    PingProbe p;
    p.note_connect(0);
    const std::int64_t t = round_trip(p, 0, 111'000'000);
    p.note_ping_sent(t + kPingPeriodUs);
    p.note_disconnect(t + kPingPeriodUs + 9'000'000);
    p.note_ping_sent(t + 3 * kPingPeriodUs);
    p.note_pong(t + 3 * kPingPeriodUs);   // unsolicited counter exercised below
    p.note_pong(t + 3 * kPingPeriodUs);
    const std::int64_t now = t + 4 * kPingPeriodUs;
    // Every optional clause is present, so the longest line this can produce is
    // the one being truncated.
    CHECK(rendered(p, now).find("lost 1") != std::string::npos);

    for (std::size_t cap = 1; cap < 128; ++cap) {
        char buf[192];
        std::memset(buf, '\xAB', sizeof buf);
        const std::size_t n = p.render(now, buf, cap);
        CHECK(n < cap);
        CHECK(std::strlen(buf) < cap);
        for (std::size_t i = cap; i < sizeof buf; ++i) {
            CHECK(static_cast<unsigned char>(buf[i]) == 0xABu);
        }
    }
    CHECK(p.render(now, nullptr, 32) == 0);
}

TEST_CASE("PROPERTIES: the invariants hold over a swept space, not just at the worked examples") {
    // Four properties every reading depends on, checked across a sweep of
    // round-trip times spanning the whole range the instrument claims — the
    // 87 ms baseline through the 111 s backlog.
    const std::int64_t rtts[] = {87'000, 250'000, 1'000'000, 12'000'000, 111'000'000};
    for (const std::int64_t rtt : rtts) {
        PingProbe p;
        p.note_connect(0);
        std::int64_t t = 0;
        std::uint64_t prev_worst = 0;
        for (int i = 0; i < 12; ++i) {
            const std::int64_t send = t + kPingPeriodUs;

            // 1. A ping is due exactly at the period and never before it, and
            //    never while one is in flight.
            CHECK_FALSE(p.ping_due(send - 1));
            CHECK(p.ping_due(send));
            p.note_ping_sent(send);
            CHECK_FALSE(p.ping_due(send + rtt));

            // 2. The outstanding reading only grows, and never exceeds the
            //    round-trip it is a lower bound on.
            std::uint64_t prev_out = 0;
            for (int k = 0; k <= 4; ++k) {
                const std::uint64_t out = p.outstanding_us(send + (rtt * k) / 4);
                CHECK(out >= prev_out);
                CHECK(out <= static_cast<std::uint64_t>(rtt));
                prev_out = out;
            }

            p.note_pong(send + rtt);
            t = send + rtt;

            // 3. The high-water marks never go down, bound the last reading,
            //    and the run-level one bounds the per-connection one — the
            //    ordering that stops a `worst` being quietly larger than the
            //    `run` it is printed inside.
            CHECK(p.worst_rtt_us() >= prev_worst);
            CHECK(p.worst_rtt_us() >= p.last_rtt_us());
            CHECK(p.worst_rtt_ever_us() >= p.worst_rtt_us());
            prev_worst = p.worst_rtt_us();

            // 4. Every ping is accounted for exactly once: answered, lost, or
            //    still out. Nothing is invented and nothing evaporates.
            CHECK(p.pongs() + p.unanswered() + (p.in_flight() ? 1u : 0u) == p.pings());
        }
        CHECK(p.last_rtt_us() == static_cast<std::uint64_t>(rtt));
        CHECK(p.pings() == 12);
        CHECK(p.pongs() == 12);
        CHECK(p.unsolicited() == 0);
    }
}
