// test_resync.cpp — the healing path's timing, on the desk.
//
// M4 stage D item A2. The adapter's half of a checksum heal has been covered
// since B2 (`test_kraken_adapter.cpp`: the mismatch drops the book, emits
// `Gap{ChecksumFail}` and latches `resync_wanted()`); this is the firmware's
// half — WHEN the transport acts, and what stops it acting in a loop.
//
// It is a host test at all because `resync.hpp` has no socket in it. What the
// bench still has to see is a real MINA/GBP book healing on real hardware, and
// that is B1's third criterion; what a desk can settle is that the sequence is
// unsubscribe-then-subscribe, that the gap between them is the one measured
// value this project has, that a heal in flight cannot be restarted, and that a
// venue serving corrupt snapshots cannot make the board spin.
//
// REWRITTEN AFTER REVIEW, WHICH FOUND THE OBJECT UNDER TEST TO BE THE WRONG
// SHAPE. The first version was edge-triggered — a one-shot flag the transport
// consumed with an `exchange(false)` — and `on_requested` refused a request by
// counting it and returning. Four separate paths therefore DESTROYED a request
// that nothing could ever re-raise, leaving the panel grey over a live
// heartbeating socket until a power cycle. The cases below are the ones that
// pin the replacement: a LEVEL the feed task republishes, polled by a transport
// for which refusing costs nothing.
//
// The tests that could not have caught it went too: the old "first heal of a
// connection is not throttled" built a fresh `ResyncPolicy` and so asserted
// about the first heal of an OBJECT, not of a CONNECTION — the name claimed
// exactly the property the code did not have, and the green said the opposite
// of the truth.
#include <doctest/doctest.h>

#include <cstdint>

#include "resync.hpp"

using depthcharge::fw::kResyncGapUs;
using depthcharge::fw::kResyncMinIntervalUs;
using depthcharge::fw::ResyncPolicy;
using depthcharge::fw::SubscriptionSignal;

using Action = ResyncPolicy::Action;

namespace {

// Drive the policy to steady state AND PAST THE FLOOR: a socket, the opening
// subscribe taken, and enough simulated time since it that a heal asked for at
// `now` is not paced by the connection's own subscribe.
//
// The offset is the point rather than an inconvenience. The floor anchors on the
// last subscribe frame, so a case that wants to exercise healing has to say when
// the subscription was made — and a case that wants to exercise the FLOOR
// (`a level with no socket…`) deliberately does not use this.
ResyncPolicy subscribed_at(std::int64_t now) {
    ResyncPolicy p;
    const std::int64_t connected = now - kResyncMinIntervalUs;
    p.on_connected(connected);
    REQUIRE(p.step(connected) == Action::SendSubscribe);
    REQUIRE(p.subscribed());
    return p;
}

// One pass of the RX loop: poll the level — up OR down — then act on whatever
// is owed. The level goes in either way, which is what lets `owed()` stop being
// true; A5's first Kraken run reported `owed=1` for its whole length because the
// policy was only ever told about the level when it was up.
Action pass(ResyncPolicy& p, const SubscriptionSignal& s, std::int64_t now) {
    p.on_poll(s.wanted(), now);
    return p.step(now);
}

}  // namespace

TEST_CASE("the signal is a level, and the edge count is a separate fact") {
    // The LEVEL is what the transport acts on, so it must survive being read.
    // The COUNT is what a bench reads against the adapter's own
    // `resyncs_requested`, so it must count transitions rather than polls.
    SubscriptionSignal s;
    CHECK_FALSE(s.wanted());
    CHECK(s.raised() == 0);

    s.set_wanted(true);
    CHECK(s.wanted());
    CHECK(s.raised() == 1);

    // Re-published on every frame while the book is unbaselined. The level
    // stays; the count does not move.
    s.set_wanted(true);
    s.set_wanted(true);
    CHECK(s.wanted());
    CHECK(s.raised() == 1);

    // The snapshot lands. Down, and then up again is a second occasion.
    s.set_wanted(false);
    CHECK_FALSE(s.wanted());
    s.set_wanted(true);
    CHECK(s.raised() == 2);
}

TEST_CASE("a refusal is a latch, because it is terminal for the socket") {
    SubscriptionSignal s;
    CHECK_FALSE(s.take_refused());
    CHECK(s.refusals() == 0);

    s.set_refused();
    s.set_refused();          // idempotent while still latched
    CHECK(s.refusals() == 1);
    CHECK(s.take_refused());
    CHECK_FALSE(s.take_refused());   // and it does not fire twice

    s.set_refused();
    CHECK(s.refusals() == 2);
}

TEST_CASE("a socket that comes up owes exactly one subscribe") {
    // Kraken's depth is not in the URL, so the upgrade alone subscribes to
    // nothing: without this the board would hold a live, heartbeating socket
    // and an empty book for ever — which is the failure stage 0 measured when a
    // refused `depth: 27` left exactly that state.
    ResyncPolicy p;
    CHECK(p.step(0) == Action::None);           // no socket, nothing owed
    CHECK(p.due_in_us(0) < 0);

    p.on_connected(1000);
    CHECK(p.due_in_us(1000) == 0);
    CHECK(p.step(1000) == Action::SendSubscribe);
    CHECK(p.subscribed());
    CHECK(p.subscribes() == 1);
    CHECK(p.step(1000) == Action::None);        // and only one
    CHECK(p.step(9'000'000) == Action::None);
}

// The title carries no comma on purpose: doctest's `-tc` filter splits on
// commas, so a case that has one in its name cannot be selected by a pattern
// containing it — which is how a mutation run reports a check as passing when
// in fact it never ran.
TEST_CASE("a heal is an unsubscribe then a gap then a subscribe — in that order") {
    // Unsubscribe FIRST rather than a bare re-subscribe: Kraken's answer to a
    // second subscribe on a channel it already holds is undocumented and was
    // never measured, while the unsubscribe/subscribe pair is the sequence the
    // committed resync slice was captured with.
    std::int64_t now = 10'000'000;
    ResyncPolicy p = subscribed_at(now);

    p.on_wanted(now);
    CHECK(p.healing());
    CHECK(p.due_in_us(now) == 0);
    CHECK(p.step(now) == Action::SendUnsubscribe);
    CHECK(p.unsubscribes() == 1);

    // The gap is a wait, not a poll: nothing happens until it has elapsed.
    CHECK(p.step(now + 1) == Action::None);
    CHECK(p.step(now + kResyncGapUs - 1) == Action::None);
    CHECK(p.due_in_us(now + kResyncGapUs / 2) == kResyncGapUs / 2);

    now += kResyncGapUs;
    CHECK(p.step(now) == Action::SendSubscribe);
    CHECK(p.subscribes() == 2);
    CHECK(p.heals() == 1);
    CHECK(p.subscribed());
    CHECK_FALSE(p.healing());
    CHECK(p.due_in_us(now) < 0);
}

TEST_CASE("a heal already in flight is not restarted by the level still being true") {
    // The level stays up until the healing snapshot lands, so `on_wanted` is
    // called on every pass right through the heal. It must not restart it.
    std::int64_t now = 10'000'000;
    ResyncPolicy p = subscribed_at(now);
    SubscriptionSignal s;
    s.set_wanted(true);

    REQUIRE(pass(p, s, now) == Action::SendUnsubscribe);

    // Polled again and again while the gap runs.
    for (int i = 1; i < 10; ++i) { CHECK(pass(p, s, now + i * 10'000) == Action::None); }
    CHECK(p.unsubscribes() == 1);    // no second unsubscribe

    now += kResyncGapUs;
    CHECK(pass(p, s, now) == Action::SendSubscribe);
    CHECK(p.heals() == 1);
}

TEST_CASE("THE CASE THE LEVEL EXISTS FOR: a throttled heal is served, not lost") {
    // The defect review found, as a test. A second checksum failure inside the
    // floor used to be counted as throttled and DISCARDED — and the adapter
    // could not re-raise it, because `verify_checksum` only latches while the
    // book is baselined and the drop had just cleared that. The panel stayed
    // grey over a healthy socket for the rest of the run.
    //
    // With a level, the transport simply asks again next pass.
    std::int64_t now = 100'000'000;
    ResyncPolicy p = subscribed_at(now);
    SubscriptionSignal s;

    // Heal #1 completes.
    s.set_wanted(true);
    REQUIRE(pass(p, s, now) == Action::SendUnsubscribe);
    now += kResyncGapUs;
    REQUIRE(pass(p, s, now) == Action::SendSubscribe);
    const std::int64_t subscribed_at_us = now;
    s.set_wanted(false);                       // the snapshot landed and verified
    REQUIRE(p.heals() == 1);

    // A second mismatch two seconds later — well inside the 5 s floor.
    now = subscribed_at_us + 2'000'000;
    s.set_wanted(true);
    CHECK(pass(p, s, now) == Action::None);
    CHECK(p.owed());                            // and the board SAYS it is waiting

    // The RX loop keeps coming round every second, as it does.
    for (int i = 1; i <= 2; ++i) {
        CHECK(pass(p, s, now + i * 1'000'000) == Action::None);
    }

    // The floor expires, and the heal happens without anything having had to
    // remember it.
    now = subscribed_at_us + kResyncMinIntervalUs;
    CHECK(pass(p, s, now) == Action::SendUnsubscribe);
    CHECK_FALSE(p.owed());
    now += kResyncGapUs;
    CHECK(pass(p, s, now) == Action::SendSubscribe);
    CHECK(p.heals() == 2);
}

TEST_CASE("a venue serving corrupt snapshots cannot make the board spin") {
    // THE CASE THE FLOOR EXISTS FOR. Every heal is answered by a snapshot that
    // fails its checksum, so the level never goes down. Unthrottled that is an
    // unsubscribe/subscribe pair every kResyncGapUs, for ever, at a venue behind
    // Cloudflare.
    std::int64_t now = 0;
    ResyncPolicy p = subscribed_at(now);
    SubscriptionSignal s;
    s.set_wanted(true);                        // and it never comes down

    const std::int64_t kRunUs = 200'000'000;   // 200 s of simulated time
    while (now < kRunUs) {
        (void)pass(p, s, now);
        now += 250'000;                        // an RX pass every 250 ms
    }

    // Progress is made — this is not a machine that has simply stopped.
    CHECK(p.heals() >= 1);

    // AND IT IS BOUNDED BY AN ABSOLUTE NUMBER, NOT BY THE CONSTANT UNDER TEST.
    // The first version of this line read `heals <= kRunUs / kResyncMinIntervalUs
    // + 1`, which follows the constant: set the floor to 1 us and the bound
    // becomes 200,000,001 and the assertion passes on a machine spinning as fast
    // as it can be polled. The mutation run caught it. 200 s at a 5 s floor is
    // at most 41 subscribe frames; anything near the poll rate (800 passes) is
    // the storm this bounds.
    CHECK(p.heals() <= 41u);
    CHECK(p.unsubscribes() == p.heals());
}

TEST_CASE("the floor is measured from the subscribe that ends a heal") {
    // Not from the unsubscribe that begins it, and not from the request: the
    // quantity being paced is subscribe frames reaching the venue.
    std::int64_t now = 100'000'000;
    ResyncPolicy p = subscribed_at(now);

    p.on_wanted(now);
    REQUIRE(p.step(now) == Action::SendUnsubscribe);
    now += kResyncGapUs;
    REQUIRE(p.step(now) == Action::SendSubscribe);   // the subscribe lands here
    const std::int64_t subscribed_at_us = now;

    p.on_wanted(subscribed_at_us + kResyncMinIntervalUs - 1);
    CHECK_FALSE(p.healing());
    CHECK(p.owed());

    p.on_wanted(subscribed_at_us + kResyncMinIntervalUs);
    CHECK(p.healing());
    CHECK_FALSE(p.owed());
}

TEST_CASE("owed stops being true when the book stops wanting a snapshot") {
    // A5, on hardware: `owed=1` sat on the soak line for a whole Kraken run over
    // a book that was live and verifying, because the policy was only ever told
    // about the level when it was UP. A field that means "a heal is waiting on
    // the floor" has to be able to stop meaning it.
    std::int64_t now = 10'000'000;
    ResyncPolicy p = subscribed_at(now);
    SubscriptionSignal s;

    // A heal, so the floor is armed from its subscribe.
    s.set_wanted(true);
    REQUIRE(pass(p, s, now) == Action::SendUnsubscribe);
    now += kResyncGapUs;
    REQUIRE(pass(p, s, now) == Action::SendSubscribe);

    // Still wanted, still inside the floor: owed.
    now += 1'000'000;
    (void)pass(p, s, now);
    CHECK(p.owed());

    // The snapshot lands and verifies. The level drops; so must the field, on
    // the very next pass and without waiting for the floor to expire.
    s.set_wanted(false);
    now += 250'000;
    (void)pass(p, s, now);
    CHECK_FALSE(p.owed());
}

TEST_CASE("a reconnect abandons a half-finished heal rather than completing it") {
    // Completing it would send an unsubscribe — or a second subscribe — for a
    // subscription that died with the old socket. A new connection subscribes on
    // its own, which is the same reason the adapter's `on_transport_gap`
    // deliberately does NOT set `resync_wanted`.
    std::int64_t now = 10'000'000;
    ResyncPolicy p = subscribed_at(now);

    p.on_wanted(now);
    REQUIRE(p.step(now) == Action::SendUnsubscribe);
    REQUIRE(p.healing());

    p.on_disconnected();
    CHECK_FALSE(p.healing());
    CHECK(p.step(now + kResyncGapUs * 10) == Action::None);

    p.on_connected(now + 5'000'000);
    CHECK(p.step(now + 5'000'000) == Action::SendSubscribe);
    CHECK(p.subscribed());
    CHECK(p.heals() == 0);       // the abandoned one never completed
}

TEST_CASE("the floor belongs to THIS connection and not to the one before it") {
    // Review found the floor measured against a heal belonging to a connection
    // that no longer existed, and found the old test for this asserting about a
    // freshly constructed OBJECT instead — so its name claimed the property the
    // code did not have.
    //
    // What the floor is now measured from is the last SUBSCRIBE FRAME, which a
    // reconnect resets and the opening subscribe re-arms. So the reconnect does
    // not inherit the old socket's pacing — but the new socket's own opening
    // subscribe does pace the heal that follows it, which is correct: asking for
    // a snapshot two seconds after asking for one is the churn the floor exists
    // to stop.
    std::int64_t now = 100'000'000;
    ResyncPolicy p = subscribed_at(now);

    p.on_wanted(now);
    REQUIRE(p.step(now) == Action::SendUnsubscribe);
    now += kResyncGapUs;
    REQUIRE(p.step(now) == Action::SendSubscribe);   // a subscribe at `now`
    REQUIRE(p.heals() == 1);

    // The socket dies half a second later and a new one comes up 2 s after that
    // — the whole sequence well inside the 5 s floor of the old connection.
    p.on_disconnected();
    now += 500'000;
    now += 2'000'000;
    p.on_connected(now);
    REQUIRE(p.step(now) == Action::SendSubscribe);
    const std::int64_t subscribed_at_us = now;

    // Inside the NEW subscribe's floor: owed, and waiting.
    p.on_wanted(subscribed_at_us + 2'000'000);
    CHECK_FALSE(p.healing());
    CHECK(p.owed());

    // Past it: served. Had the old connection's anchor survived, this would
    // still be throttled — the old subscribe was 2.5 s before the new one.
    p.on_wanted(subscribed_at_us + kResyncMinIntervalUs);
    CHECK(p.healing());
    CHECK_FALSE(p.owed());
}

TEST_CASE("a level with no socket is polled harmlessly and acted on at connect") {
    // The adapter can be unbaselined while the transport is between sockets. The
    // level is true throughout; nothing may be sent, and nothing may be
    // remembered as owed either — the new connection subscribes on its own.
    ResyncPolicy p;
    SubscriptionSignal s;
    s.set_wanted(true);

    for (int i = 0; i < 20; ++i) { CHECK(pass(p, s, i * 1'000'000) == Action::None); }
    CHECK_FALSE(p.owed());

    p.on_connected(20'000'000);
    CHECK(pass(p, s, 20'000'000) == Action::SendSubscribe);

    // THE CHURN THIS ANCHOR EXISTS TO PREVENT. The level is necessarily still
    // true here — the snapshot answering that subscribe has not landed yet — so
    // a floor that did not cover the opening subscribe would have the very next
    // pass unsubscribe the subscription just made, for ever.
    for (int i = 1; i <= 16; ++i) {
        CHECK(pass(p, s, 20'000'000 + i * 250'000) == Action::None);
    }
    CHECK(p.owed());
    CHECK(p.unsubscribes() == 0);
    CHECK(p.subscribes() == 1);
}
