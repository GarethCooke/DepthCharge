// test_window.cpp — the row policies, and the invariants that hold for all of them.
//
// M4 stage C. There is no corpus behind this file and there cannot be: which
// levels of a 25-level book occupy 27 rendered rows is a decision, not a wire
// fact, and no capture can adjudicate it. Two ARCHITECTURE §9 rows point the
// same way — the coincidence class (2026-08-18) and the never-observed one — so
// **generated input is the instrument here, not a substitute for one.**
//
// ============================================================================
// ONE SUITE, PARAMETERISED BY POLICY. NOT THREE SUITES.
// ============================================================================
//
// The invariants belong to the WINDOW, not to any policy, and three suites would
// be three chances to write a weaker version of the same check — with the weakest
// one silently covering whichever policy a future session adds. So every
// generated book is driven through every policy and the same assertions run on
// all of them; a fourth policy costs one line in `kPolicies` and inherits the
// whole file.
//
// ============================================================================
// WHAT THE GENERATOR HAS TO PRODUCE, AND WHY THESE SHAPES
// ============================================================================
//
// Measured before any of this was written (harness/replay/NOTES.md, stage C):
// real books are SPARSE — only one adjacent level pair in five is a single tick
// from its neighbour, and 25 levels span 182 ticks on BTC/USD and 6,613 on the
// quiet pair. A generator that emitted contiguous ticks would be testing a book
// this project has never seen. So gaps are the default and contiguity is the
// special case, which is the opposite of the obvious way round.
#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <depthcharge/book.hpp>
#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/window.hpp>

#include "dc_harness/replay_driver.hpp"
#include "dc_harness/trace.hpp"

using depthcharge::Book;
using depthcharge::BookLevel;
using depthcharge::DisplaySnapshot;
using depthcharge::FeedEvent;
using depthcharge::GapReason;
using depthcharge::LevelSpan;
using depthcharge::PriceTicks;
using depthcharge::Qty;
using depthcharge::Side;
using depthcharge::SymbolSpec;
using depthcharge::window::Policy;
using depthcharge::window::WindowStats;
using depthcharge::window::kMaxRows;
using depthcharge::window::policy_name;

namespace {

constexpr Policy kPolicies[] = {Policy::TopOfBook, Policy::LargestFirst, Policy::ThinnedTail};

// A deterministic generator. `Math.random` has no place in a suite whose job is
// to be re-runnable: a property that fails once in fifty seeds and cannot be
// reproduced is worse than no property at all, because it teaches the next
// session to re-run until green.
struct Lcg {
    std::uint64_t s;
    std::uint64_t next() noexcept { s = s * 6364136223846793005ull + 1442695040888963407ull; return s >> 33; }
    std::uint32_t below(std::uint32_t n) noexcept { return n == 0 ? 0 : static_cast<std::uint32_t>(next() % n); }
};

// One side of a book, best-first, with the sparsity real books have.
//
// `max_gap` of 1 gives the contiguous case the measurements say is rare; the
// default spreads levels the way BTC/USD does. Quantities vary by three orders
// of magnitude so `LargestFirst` has something to prefer — a generator with flat
// quantities would make that policy indistinguishable from TopOfBook and the
// suite would pass while testing one policy twice.
std::vector<BookLevel> make_side(Lcg& rng, std::uint32_t count, Side side,
                                 PriceTicks start = 1'000'000, std::uint32_t max_gap = 40) {
    std::vector<BookLevel> out;
    out.reserve(count);
    PriceTicks px = start;
    for (std::uint32_t i = 0; i < count; ++i) {
        const Qty qty = 1 + static_cast<Qty>(rng.below(1'000'000));
        out.push_back(BookLevel{px, qty});
        const PriceTicks step = 1 + static_cast<PriceTicks>(rng.below(max_gap));
        px += (side == Side::Bid ? -step : +step);
    }
    return out;
}

// `BookLevel` is a plain aggregate with no operator==, and giving it one would
// put a comparison into `engine/` for a test's convenience. Compared here
// instead, on both fields.
bool same_rows(const std::vector<BookLevel>& a, const std::vector<BookLevel>& b,
               std::uint32_t n) {
    for (std::uint32_t i = 0; i < n; ++i) {
        if (a[i].px != b[i].px || a[i].qty != b[i].qty) { return false; }
    }
    return true;
}

// ---------------------------------------------------------------------------
// THE INVARIANTS. One function, so a policy cannot be checked more weakly than
// its neighbours, and so a new invariant lands on every policy at once.
// ---------------------------------------------------------------------------
void check_invariants(Policy p, const std::vector<BookLevel>& src, Side side,
                      std::uint32_t cap) {
    CAPTURE(std::string(policy_name(p)));
    CAPTURE(src.size());
    CAPTURE(cap);
    CAPTURE(side == Side::Bid ? "bid" : "ask");

    std::vector<BookLevel> dst(kMaxRows, BookLevel{});
    const WindowStats st = depthcharge::window::select(
        p, src.data(), static_cast<std::uint32_t>(src.size()), dst.data(), cap, /*validated=*/10);

    const std::uint32_t room = cap < kMaxRows ? cap : kMaxRows;
    const std::uint32_t expect = static_cast<std::uint32_t>(src.size()) < room
                                     ? static_cast<std::uint32_t>(src.size())
                                     : room;

    // #4 — the window fills every row it can and says where knowledge stops.
    // A policy that quietly rendered fewer rows than it had levels for would
    // satisfy every other invariant here.
    REQUIRE(st.rows_filled == expect);
    CHECK(st.rows_unknown == room - st.rows_filled);
    CHECK(st.levels_offered == src.size());
    CHECK(st.levels_dropped == src.size() - st.rows_filled);

    // #3 — row 0 is the touch. `best_bid`, `best_ask` and `spread_ticks` all
    // read it, so a policy free to drop a thin touch would make all three lie.
    if (!src.empty() && room > 0) {
        CHECK(dst[0].px == src[0].px);
        CHECK(dst[0].qty == src[0].qty);
    }

    std::uint32_t validated = 0;
    for (std::uint32_t i = 0; i < st.rows_filled; ++i) {
        // #1 — every rendered row is a level the book holds, matched on BOTH
        // fields. Matching on price alone would pass a window that rendered the
        // right prices with invented sizes.
        bool found = false;
        std::uint32_t rank = 0;
        for (std::uint32_t k = 0; k < src.size(); ++k) {
            if (src[k].px == dst[i].px && src[k].qty == dst[i].qty) { found = true; rank = k; break; }
        }
        CAPTURE(i);
        CHECK(found);
        if (rank < 10) { ++validated; }

        // #1 again, from the other side: never a zero-quantity row. A level with
        // no size is not a level, and "render it as zero" is the tidy-looking
        // mistake stage 0's decision exists to forbid.
        CHECK(dst[i].qty > 0);

        // #2 — monotonic in price, best-first, and STRICT: two rows at one price
        // would be one level rendered twice.
        if (i > 0) {
            if (side == Side::Bid) { CHECK(dst[i].px < dst[i - 1].px); }
            else                   { CHECK(dst[i].px > dst[i - 1].px); }
        }
    }
    CHECK(st.rows_validated == validated);

    // The span is the rendered range, and it is 0 only for an empty window.
    if (st.rows_filled == 0) {
        CHECK(st.tick_span == 0);
    } else {
        const PriceTicks a = dst[0].px;
        const PriceTicks b = dst[st.rows_filled - 1].px;
        CHECK(st.tick_span == (a < b ? b - a : a - b) + 1);
        CHECK(st.tick_span >= static_cast<PriceTicks>(st.rows_filled));
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. The shapes the brief names, one at a time and exactly
// ---------------------------------------------------------------------------

TEST_CASE("every policy holds every invariant on the named book shapes") {
    Lcg rng{0x5EED};
    for (const Policy p : kPolicies) {
        for (const Side side : {Side::Bid, Side::Ask}) {
            // empty / uninitialised — the same input here, and a different fact
            // about the world; the DIFFERENCE is Book's, not the window's, and
            // it is asserted in the Book cases below.
            check_invariants(p, {}, side, kMaxRows);

            // single level, and the under-filled cases either side of the panel
            for (const std::uint32_t n : {1u, 2u, 13u, 26u, 27u, 28u, 40u, 100u, 256u}) {
                check_invariants(p, make_side(rng, n, side), side, kMaxRows);
            }

            // dense: every level one tick from its neighbour — the shape real
            // books almost never have, kept because it is where a stride or a
            // rank calculation collapses.
            check_invariants(p, make_side(rng, 100, side, 1'000'000, 1), side, kMaxRows);

            // gaps far larger than the window: MINA/GBP's shape, where 25 levels
            // span 6,613 ticks.
            check_invariants(p, make_side(rng, 40, side, 1'000'000, 5'000), side, kMaxRows);

            // caps the panel never uses, which is where the off-by-ones live.
            for (const std::uint32_t cap : {0u, 1u, 2u, 3u, 26u, 27u}) {
                check_invariants(p, make_side(rng, 60, side), side, cap);
            }
        }
    }
}

TEST_CASE("every policy holds every invariant over many generated books") {
    // The property run. 3 policies x 2 sides x 200 books, each with a random
    // depth and a random sparsity — the point is not the count, it is that the
    // same assertions run on inputs nobody chose.
    Lcg rng{0xC0FFEE};
    for (int i = 0; i < 200; ++i) {
        const std::uint32_t n = rng.below(140);
        const std::uint32_t gap = 1 + rng.below(300);
        for (const Side side : {Side::Bid, Side::Ask}) {
            const auto src = make_side(rng, n, side, 1'000'000, gap);
            for (const Policy p : kPolicies) { check_invariants(p, src, side, kMaxRows); }
        }
    }
}

// ---------------------------------------------------------------------------
// 2. THE COINCIDENCE, MADE A PROPERTY RATHER THAN AN OBSERVATION
// ---------------------------------------------------------------------------

TEST_CASE("a book no deeper than the panel makes all three policies the same window") {
    // This is stage C's headline finding stated as a test, and it is the reason
    // the committed Kraken goldens below are identical across policies. At the
    // shipped depth of 25 into 27 rows there is nothing to choose: every policy
    // renders every level, so a corpus of depth-25 traces cannot distinguish
    // them and stage D cannot judge them on one.
    //
    // Asserted rather than noted, because the day someone raises the subscribed
    // depth above 27 this stops being true — and that is exactly the moment the
    // policy choice starts to matter and somebody should be told.
    Lcg rng{0xD09};
    for (const Side side : {Side::Bid, Side::Ask}) {
        for (std::uint32_t n = 0; n <= kMaxRows; ++n) {
            const auto src = make_side(rng, n, side);
            std::vector<BookLevel> top(kMaxRows), largest(kMaxRows), thinned(kMaxRows);
            const auto a = depthcharge::window::select(Policy::TopOfBook, src.data(), n,
                                                       top.data(), kMaxRows);
            const auto b = depthcharge::window::select(Policy::LargestFirst, src.data(), n,
                                                       largest.data(), kMaxRows);
            const auto c = depthcharge::window::select(Policy::ThinnedTail, src.data(), n,
                                                       thinned.data(), kMaxRows);
            CAPTURE(n);
            REQUIRE(a.rows_filled == b.rows_filled);
            REQUIRE(a.rows_filled == c.rows_filled);
            CHECK(a.tick_span == b.tick_span);
            CHECK(a.tick_span == c.tick_span);
            CHECK(same_rows(top, largest, a.rows_filled));
            CHECK(same_rows(top, thinned, a.rows_filled));
        }
    }
}

TEST_CASE("a book deeper than the panel makes them different windows") {
    // The paired assertion, and without it the case above would pass on three
    // policies that were secretly one. A hundred levels into 27 rows: the
    // largest-first window must reach further out than the top-of-book one,
    // because it is picking size rather than proximity.
    Lcg rng{0xDEEB};
    const auto src = make_side(rng, 100, Side::Bid);
    std::vector<BookLevel> top(kMaxRows), largest(kMaxRows), thinned(kMaxRows);
    const auto a = depthcharge::window::select(Policy::TopOfBook, src.data(), 100,
                                               top.data(), kMaxRows);
    const auto b = depthcharge::window::select(Policy::LargestFirst, src.data(), 100,
                                               largest.data(), kMaxRows);
    const auto c = depthcharge::window::select(Policy::ThinnedTail, src.data(), 100,
                                               thinned.data(), kMaxRows);
    CHECK(a.rows_filled == kMaxRows);
    CHECK(b.rows_filled == kMaxRows);
    CHECK(c.rows_filled == kMaxRows);
    CHECK_FALSE(same_rows(top, largest, kMaxRows));
    CHECK_FALSE(same_rows(top, thinned, kMaxRows));
    // Both alternatives cover more of the price axis than proximity does, which
    // is the whole of what they are for.
    CHECK(b.tick_span > a.tick_span);
    CHECK(c.tick_span > a.tick_span);
}

// ---------------------------------------------------------------------------
// 3. UNINITIALISED IS A STATE, NOT AN EMPTY BOOK
// ---------------------------------------------------------------------------

TEST_CASE("a book that has received nothing is distinguishable from an empty one") {
    // Triage item 11's engine half. The two look identical in every count the
    // snapshot carries — `bid_count == 0`, `ask_count == 0`, `has_book()` false
    // — and only one of them is a statement the venue made.
    const SymbolSpec spec{/*id=*/1, /*price_decimals=*/1, /*qty_decimals=*/8};
    DisplaySnapshot snap{};

    SUBCASE("nothing received") {
        Book book(spec);
        CHECK_FALSE(book.initialised());
        book.publish(snap);
        CHECK_FALSE(snap.initialised);
        CHECK_FALSE(snap.live());
        CHECK_FALSE(snap.has_book());
        CHECK(snap.stale_reason == GapReason::Resync);
    }

    SUBCASE("an EMPTY snapshot is knowledge, and says so") {
        Book book(spec);
        FeedEvent ev{};
        ev.kind = FeedEvent::Kind::Snapshot;   // both spans empty
        book.apply(ev);
        book.publish(snap);
        CHECK(book.initialised());
        CHECK(snap.initialised);
        CHECK(snap.live());
        CHECK_FALSE(snap.has_book());          // ...and still no levels
    }

    SUBCASE("THE CASE THE INFERENCE GETS WRONG: empty, then a gap") {
        // `!live() && !has_book()` is the cheap way to guess "uninitialised",
        // and here it guesses wrong: this book HAS been told something. That is
        // why the flag is published rather than inferred.
        Book book(spec);
        FeedEvent snapshot{};
        snapshot.kind = FeedEvent::Kind::Snapshot;
        book.apply(snapshot);
        FeedEvent gap{};
        gap.kind = FeedEvent::Kind::Gap;
        gap.reason = GapReason::ChecksumFail;
        book.apply(gap);
        book.publish(snap);

        CHECK_FALSE(snap.live());
        CHECK_FALSE(snap.has_book());
        CHECK(snap.initialised);               // the inference would say false
        CHECK(book.initialised());
    }

    SUBCASE("a gap does not un-initialise a book that holds levels") {
        // The deliberate asymmetry with the Kraken adapter, which DOES drop its
        // ladder on a gap: the adapter must not amend a book whose provenance is
        // a hole, and the panel must not blank a ladder the feed never retracted.
        Book book(spec);
        const BookLevel bids[] = {{100, 5}};
        FeedEvent ev{};
        ev.kind = FeedEvent::Kind::Snapshot;
        ev.bids = LevelSpan{bids, 1};
        book.apply(ev);
        FeedEvent gap{};
        gap.kind = FeedEvent::Kind::Gap;
        gap.reason = GapReason::Disconnect;
        book.apply(gap);
        book.publish(snap);

        CHECK(book.initialised());
        CHECK(snap.initialised);
        CHECK(snap.has_book());                // levels kept, for the renderer to grey
        CHECK_FALSE(snap.live());
    }
}

// ---------------------------------------------------------------------------
// 4. THE PUBLISHED WINDOW, AND THE ROWS BEYOND IT
// ---------------------------------------------------------------------------

TEST_CASE("rows the window did not fill are blanked, not left from the last frame") {
    // `bid_count` is what says where the knowledge stops (stage 0's decision:
    // depth beyond N is unknown, not zero). The zero-fill is the second half of
    // that: without it a deeper frame's tail survives into a shallower one and a
    // renderer that trusted the array over the count would draw a level the book
    // no longer holds.
    const SymbolSpec spec{/*id=*/1, /*price_decimals=*/1, /*qty_decimals=*/8};
    Book book(spec);
    DisplaySnapshot snap{};

    Lcg rng{7};
    const auto deep = make_side(rng, 27, Side::Bid);
    FeedEvent full{};
    full.kind = FeedEvent::Kind::Snapshot;
    full.bids = LevelSpan{deep.data(), 27};
    book.apply(full);
    book.publish(snap);
    REQUIRE(snap.bid_count == 27);

    const BookLevel one[] = {{deep[0].px, deep[0].qty}};
    FeedEvent shallow{};
    shallow.kind = FeedEvent::Kind::Snapshot;
    shallow.bids = LevelSpan{one, 1};
    book.apply(shallow);
    book.publish(snap);

    REQUIRE(snap.bid_count == 1);
    for (std::size_t i = 1; i < depthcharge::kDisplayLevels; ++i) {
        CAPTURE(i);
        CHECK(snap.bids[i].px == 0);
        CHECK(snap.bids[i].qty == 0);
    }
}

// ---------------------------------------------------------------------------
// 5. THE COMMITTED TRACES, THROUGH EVERY POLICY
// ---------------------------------------------------------------------------

namespace {

dc::harness::ReplayResult replay_with(std::string_view name, Policy p) {
    dc::harness::TraceReader reader(std::string(DC_REPLAY_DIR) + "/" + std::string(name) +
                                    ".ndjson");
    dc::harness::ReplayOptions opts;
    opts.window_policy = p;
    return dc::harness::run_replay(reader, dc::harness::symbol_for(reader.meta()), opts);
}

// Two identities that make the pinned totals below something other than a
// transcription of what the code said. Both are properties of the run, derivable
// without running it:
//
//   * every publish offers 27 rows a side, so filled + unknown is exactly
//     54 x publishes -- a policy that silently rendered a row it was not given,
//     or lost one, breaks this without moving any single figure much;
//   * `TopOfBook` renders the book's best levels in order, so the rows within
//     the venue's checksum reach are exactly min(10, levels held) a side -- 20 a
//     publish on any book at least ten deep.
void check_row_arithmetic(const dc::harness::ReplayResult& r) {
    const auto& w = r.window;
    CHECK(w.rows_filled + w.rows_unknown ==
          2ull * depthcharge::kDisplayLevels * r.book.publishes);
    if (w.policy == Policy::TopOfBook && w.validated_depth > 0) {
        CHECK(w.rows_validated == 2ull * w.validated_depth * r.book.publishes);
    }
}

}  // namespace

TEST_CASE("the shipped Kraken depth makes the policy choice a no-op, and the pins say so") {
    // ========================================================================
    // STAGE C'S HEADLINE, AND THE ANSWER TO ITS OWN KNOWN UNKNOWN.
    // ========================================================================
    //
    // The brief asked whether a real Kraken book at depth 25 is sparse enough
    // for the policies to visibly differ, and said a negative answer would be
    // worth more than the policies. It is negative, and it is stronger than
    // "near-identical": at depth 25 into 27 rows **not one level is ever
    // dropped**, so all three policies render every level the book holds and are
    // the same window byte for byte. Nothing about the shipped configuration can
    // distinguish them, at the console or on the panel.
    //
    // So the policy choice only becomes a choice if the firmware subscribes
    // DEEPER than it can draw -- and B2's finding is the other half of that
    // trade, because the CRC32 covers the top 10 a side whatever the depth.
    // That is stage D's decision and the numbers for it are the d100 case below.
    for (const char* name : {"kraken_btcusd_d25_20260816", "kraken_minagbp_d25_20260816",
                             "kraken_minagbp_d25_resync_20260818"}) {
        CAPTURE(name);
        const auto top = replay_with(name, Policy::TopOfBook);
        const auto largest = replay_with(name, Policy::LargestFirst);
        const auto thinned = replay_with(name, Policy::ThinnedTail);

        CHECK(top.window.levels_dropped == 0);
        CHECK(top.window.frames_with_drops == 0);
        for (const auto* r : {&largest, &thinned}) {
            CHECK(r->window.rows_filled == top.window.rows_filled);
            CHECK(r->window.rows_unknown == top.window.rows_unknown);
            CHECK(r->window.levels_dropped == 0);
            CHECK(r->window.rows_validated == top.window.rows_validated);
            CHECK(r->window.worst_tick_span == top.window.worst_tick_span);
        }
        check_row_arithmetic(top);
        check_row_arithmetic(largest);
        check_row_arithmetic(thinned);
    }
}

TEST_CASE("row counts pinned per policy on the committed traces") {
    // Goldens ADD ROWS; none of the existing ones move, and none did -- the
    // default policy is `TopOfBook`, which is exactly what `publish` did before
    // this stage, so every figure pinned since M1 is untouched.
    SUBCASE("Kraken BTC/USD depth 25 — 25 levels into 27 rows") {
        const auto r = replay_with("kraken_btcusd_d25_20260816", Policy::TopOfBook);
        CHECK(r.window.rows_filled == 149694);
        CHECK(r.window.rows_unknown == 11982);      // the two spare rows a side
        CHECK(r.window.levels_dropped == 0);
        CHECK(r.window.rows_validated == 59880);    // 20 a publish: 10 a side
        CHECK(r.window.worst_tick_span == 308);
        CHECK(r.window.final_bid.rows_filled == 25);
        CHECK(r.window.final_bid.levels_offered == 25);
        CHECK(r.window.validated_depth == 10);
        check_row_arithmetic(r);
    }

    SUBCASE("Kraken BTC/USD depth 100 — where the policies finally differ") {
        // 100 levels into 27 rows, so 73 a side are dropped on every publish and
        // the three policies choose differently. The column that matters is the
        // last one: `largest` reaches for size deep in the book, and most of what
        // it reaches for the venue never checksummed.
        struct Expect {
            Policy policy;
            std::uint64_t validated;
            depthcharge::PriceTicks worst_span;
        };
        const Expect expected[] = {
            {Policy::TopOfBook,    104120,  319},
            {Policy::LargestFirst,  33433, 1128},
            {Policy::ThinnedTail,  104120, 1060},
        };
        for (const Expect& e : expected) {
            CAPTURE(std::string(policy_name(e.policy)));
            const auto r = replay_with("kraken_btcusd_d100_20260816", e.policy);
            CHECK(r.window.rows_filled == 281124);     // every row filled, always
            CHECK(r.window.rows_unknown == 0);
            CHECK(r.window.levels_dropped == 760185);
            CHECK(r.window.frames_with_drops == 5206);
            CHECK(r.window.rows_validated == e.validated);
            CHECK(r.window.worst_tick_span == e.worst_span);
            check_row_arithmetic(r);
        }
        // Stated as a comparison as well as three numbers, because the RATIO is
        // the thing stage D reads: at depth 100 the largest-first window shows
        // roughly a third as many checked rows as the other two.
        const auto top = replay_with("kraken_btcusd_d100_20260816", Policy::TopOfBook);
        const auto largest = replay_with("kraken_btcusd_d100_20260816", Policy::LargestFirst);
        CHECK(largest.window.rows_validated * 3 < top.window.rows_validated);
        CHECK(largest.window.worst_tick_span > top.window.worst_tick_span * 3);
    }

    SUBCASE("Anvil depth 101 — the deepest committed book, and no checksum at all") {
        struct Expect { Policy policy; depthcharge::PriceTicks worst_span; };
        const Expect expected[] = {
            {Policy::TopOfBook,     414},
            {Policy::LargestFirst,  873},
            {Policy::ThinnedTail,  1054},
        };
        for (const Expect& e : expected) {
            CAPTURE(std::string(policy_name(e.policy)));
            const auto r = replay_with("anvil_101_baseline", e.policy);
            CHECK(r.window.rows_filled == 66150);
            CHECK(r.window.rows_unknown == 0);
            CHECK(r.window.levels_dropped == 184911);
            CHECK(r.window.frames_with_drops == 1225);
            CHECK(r.window.worst_tick_span == e.worst_span);
            // NOT ONE ROW ON THIS VENUE WAS EVER EXTERNALLY CONFIRMED. Anvil
            // publishes no checksum of any kind, so `validated_depth` is 0 and
            // the honest count is zero rather than "all of them" -- which is the
            // reading a missing field would have invited.
            CHECK(r.window.validated_depth == 0);
            CHECK(r.window.rows_validated == 0);
            check_row_arithmetic(r);
        }
    }
}
