// test_book.cpp — the phase-1 book and the publish seam.
//
// Two things are being defended here. The obvious one is "adopt the latest
// snapshot" semantics. The less obvious one is invariant #5: the book's own
// state machine is where a stale feed becomes an un-drawable-as-live fact, and
// the transitions are asserted individually because the reconnect golden can
// only ever exercise one path through them.
#include <doctest/doctest.h>

#include <vector>

#include <depthcharge/book.hpp>
#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/feed_event.hpp>
#include <depthcharge/snapshot_channel.hpp>

#include "alloc_probe.hpp"
#include "dc_test_support.hpp"

using depthcharge::Book;
using depthcharge::BookLevel;
using depthcharge::DisplaySnapshot;
using depthcharge::FeedEvent;
using depthcharge::FeedStatus;
using depthcharge::GapReason;
using depthcharge::Side;
using depthcharge::SnapshotChannel;
using dc::testing::gap_event;
using dc::testing::kSpec;
using dc::testing::snapshot_event;
using dc::testing::trade_event;

TEST_CASE("a fresh book is stale by construction — nothing adopted, nothing drawable") {
    Book book(kSpec);
    DisplaySnapshot snap{};
    book.publish(snap);

    CHECK(snap.status == FeedStatus::Stale);
    CHECK(snap.stale_reason == GapReason::Resync);  // "awaiting first snapshot"
    CHECK_FALSE(snap.live());
    CHECK_FALSE(snap.has_book());
    CHECK(snap.version == 1);
    CHECK(snap.symbol.id == 101);
}

TEST_CASE("Snapshot replaces the book wholesale; it never merges") {
    Book book(kSpec);
    const std::vector<BookLevel> bids_a{{99972, 9}, {99954, 101}, {99951, 185}};
    const std::vector<BookLevel> asks_a{{99979, 44}, {99981, 50}};
    book.apply(snapshot_event(1, bids_a, asks_a));

    DisplaySnapshot snap{};
    book.publish(snap);
    CHECK(snap.live());
    CHECK(snap.bid_count == 3);
    CHECK(snap.ask_count == 2);
    CHECK(snap.best_bid() == 99972);
    CHECK(snap.best_ask() == 99979);
    CHECK(snap.spread_ticks() == 7);
    CHECK(snap.mid_ticks_x2() == 199951);

    // A shallower snapshot must not leave the old tail behind.
    const std::vector<BookLevel> bids_b{{99960, 4}};
    const std::vector<BookLevel> asks_b{{99990, 1}};
    book.apply(snapshot_event(2, bids_b, asks_b));
    book.publish(snap);

    CHECK(snap.bid_count == 1);
    CHECK(snap.ask_count == 1);
    CHECK(snap.bids[0].px == 99960);
    CHECK(snap.asks[0].px == 99990);
    CHECK(snap.bids[1].px == 0);  // the tail is zeroed, not stale data
    CHECK(snap.seq == 2);
    CHECK(book.stats().snapshots_adopted == 2);
}

TEST_CASE("publish clamps to the display window and zero-fills the rest") {
    std::vector<BookLevel> deep;
    for (std::size_t i = 0; i < depthcharge::kDisplayLevels + 20; ++i) {
        deep.push_back({100000 - static_cast<depthcharge::PriceTicks>(i),
                        static_cast<depthcharge::Qty>(i + 1)});
    }
    Book book(kSpec);
    book.apply(snapshot_event(1, deep, deep));

    DisplaySnapshot snap{};
    book.publish(snap);

    CHECK(book.bid_count() == deep.size());              // the book keeps the depth
    CHECK(snap.bid_count == depthcharge::kDisplayLevels);  // the panel gets the top
    CHECK(snap.bids[0].px == 100000);
    CHECK(snap.bids[depthcharge::kDisplayLevels - 1].px ==
          100000 - static_cast<depthcharge::PriceTicks>(depthcharge::kDisplayLevels - 1));
}

TEST_CASE("the trade ring keeps the newest kTradeRingSize prints, newest first") {
    Book book(kSpec);
    const std::vector<BookLevel> b{{99972, 9}}, a{{99979, 44}};
    book.apply(snapshot_event(1, b, a));

    for (int i = 0; i < 12; ++i) {
        book.apply(trade_event(static_cast<depthcharge::Seq>(2 + i), 100000 + i,
                               10 + i, (i % 2 == 0) ? Side::Bid : Side::Ask));
    }

    DisplaySnapshot snap{};
    book.publish(snap);

    REQUIRE(snap.trade_count == depthcharge::kTradeRingSize);
    CHECK(snap.trades[0].px == 100011);  // newest
    CHECK(snap.trades[0].qty == 21);
    CHECK(snap.trades[0].aggressor == Side::Ask);
    CHECK(snap.trades[1].px == 100010);
    CHECK(snap.trades[depthcharge::kTradeRingSize - 1].px == 100004);  // oldest kept
    CHECK(snap.has_last);
    CHECK(snap.last_px == 100011);
    CHECK(book.stats().trades_applied == 12);
}

TEST_CASE("Gap marks the book stale, keeps the levels, and only a Snapshot clears it") {
    Book book(kSpec);
    const std::vector<BookLevel> b{{99972, 9}, {99954, 101}}, a{{99979, 44}};
    book.apply(snapshot_event(1, b, a));

    DisplaySnapshot snap{};
    book.publish(snap);
    REQUIRE(snap.live());

    book.apply(gap_event(2, GapReason::Disconnect));
    book.publish(snap);

    CHECK_FALSE(snap.live());
    CHECK(snap.stale_reason == GapReason::Disconnect);
    // The levels survive: a gap means the book is *unknown*, not empty, and a
    // blank panel would assert something the feed never said.
    CHECK(snap.bid_count == 2);
    CHECK(snap.best_bid() == 99972);

    // A trade during the outage is real tape and is recorded — but it does not
    // buy the book its Live status back.
    book.apply(trade_event(3, 99975, 5, Side::Bid));
    book.publish(snap);
    CHECK(snap.trade_count == 1);
    CHECK(snap.last_px == 99975);
    CHECK_FALSE(snap.live());

    book.apply(snapshot_event(4, b, a));
    book.publish(snap);
    CHECK(snap.live());
    CHECK(book.stats().gaps == 1);
}

TEST_CASE("every GapReason greys the panel; none of them is a soft warning") {
    for (const GapReason reason : {GapReason::SeqGap, GapReason::ChecksumFail,
                                   GapReason::Disconnect, GapReason::Overflow,
                                   GapReason::Resync}) {
        Book book(kSpec);
        const std::vector<BookLevel> b{{1, 1}}, a{{2, 1}};
        book.apply(snapshot_event(1, b, a));
        book.apply(gap_event(2, reason));

        DisplaySnapshot snap{};
        book.publish(snap);
        CHECK_FALSE(snap.live());
        CHECK(snap.stale_reason == reason);
    }
}

TEST_CASE("a Delta the phase-1 book cannot apply goes stale rather than lying") {
    Book book(kSpec);
    const std::vector<BookLevel> b{{99972, 9}}, a{{99979, 44}};
    book.apply(snapshot_event(1, b, a));

    FeedEvent delta{};
    delta.kind = FeedEvent::Kind::Delta;
    delta.seq = 2;
    delta.px = 99972;
    delta.qty = 0;
    delta.side = Side::Bid;
    book.apply(delta);

    DisplaySnapshot snap{};
    book.publish(snap);

    // Silently ignoring it would leave a level on the panel that the venue has
    // just removed — a stale ladder that looks live (invariant #5). Delta
    // support arrives with the dense window at M4.
    CHECK_FALSE(snap.live());
    CHECK(book.stats().deltas_rejected == 1);
}

TEST_CASE("the feed path allocates nothing after construction (invariant #7)") {
    // Everything that could allocate is built first: the book, the snapshot, and
    // the level vectors the events borrow from.
    Book book(kSpec);
    DisplaySnapshot snap{};
    std::vector<BookLevel> bids, asks;
    for (int i = 0; i < 100; ++i) {
        bids.push_back({100000 - i, 10 + i});
        asks.push_back({100010 + i, 20 + i});
    }
    const FeedEvent snap_ev = snapshot_event(1, bids, asks);
    const FeedEvent trade = trade_event(2, 100005, 7, Side::Bid);
    const FeedEvent gap = gap_event(3, GapReason::Disconnect);
    book.apply(snap_ev);
    book.publish(snap);

    const std::size_t before = dc::testing::allocation_count();
    for (int i = 0; i < 500; ++i) {
        book.apply(snap_ev);
        book.apply(trade);
        book.apply(gap);
        book.publish(snap);
    }
    const std::size_t after = dc::testing::allocation_count();

    CHECK(after == before);
}

TEST_CASE("SnapshotChannel: the render side takes complete frames and never blocks the writer") {
    Book book(kSpec);
    SnapshotChannel channel;
    DisplaySnapshot published{};
    DisplaySnapshot received{};

    CHECK_FALSE(channel.consume(received));  // nothing published yet

    const std::vector<BookLevel> b{{99972, 9}}, a{{99979, 44}};
    book.apply(snapshot_event(1, b, a));
    book.publish(published);
    channel.publish(published);

    REQUIRE(channel.consume(received));
    CHECK(received.version == published.version);
    CHECK(received.best_bid() == 99972);

    // Consuming twice is not an error and does not re-deliver: the render task
    // skips the redraw instead of spinning.
    CHECK_FALSE(channel.consume(received));

    // The writer can outrun the reader; the reader gets the latest, not a queue.
    for (int i = 0; i < 5; ++i) {
        book.apply(snapshot_event(static_cast<depthcharge::Seq>(2 + i), b, a));
        book.publish(published);
        channel.publish(published);
    }
    REQUIRE(channel.consume(received));
    CHECK(received.seq == 6);
    CHECK(channel.published_version() == channel.consumed_version());
}

TEST_CASE("the channel round-trip allocates nothing either") {
    Book book(kSpec);
    SnapshotChannel channel;
    DisplaySnapshot published{};
    DisplaySnapshot received{};
    const std::vector<BookLevel> b{{99972, 9}}, a{{99979, 44}};
    const FeedEvent ev = snapshot_event(1, b, a);
    book.apply(ev);
    book.publish(published);
    channel.publish(published);
    (void)channel.consume(received);

    const std::size_t before = dc::testing::allocation_count();
    for (int i = 0; i < 200; ++i) {
        book.apply(ev);
        book.publish(published);
        channel.publish(published);
        (void)channel.consume(received);
    }
    CHECK(dc::testing::allocation_count() == before);
}
