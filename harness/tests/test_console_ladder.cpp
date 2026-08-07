// test_console_ladder.cpp — the renderer's one hard requirement.
//
// ARCHITECTURE invariant #5 and the M1 brief agree on the single thing the
// console ladder must not do: a stale ladder must not look live. Colour alone
// cannot carry that (goldens are colourless, terminals are sometimes
// monochrome, readers are sometimes colour-blind), so the difference is
// asserted in the two channels that survive `--no-color`: the banner text and
// the bar glyph.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include <depthcharge/book.hpp>
#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/feed_event.hpp>

#include "dc_harness/console_ladder.hpp"

using depthcharge::Book;
using depthcharge::BookLevel;
using depthcharge::DisplaySnapshot;
using depthcharge::FeedEvent;
using depthcharge::GapReason;
using depthcharge::Side;
using depthcharge::SymbolSpec;
using dc::harness::LadderStyle;
using dc::harness::render_ladder;

namespace {

constexpr SymbolSpec kSpec{101, 4, 1};

DisplaySnapshot make_snapshot(bool with_gap, bool with_trades = true) {
    Book book(kSpec);
    std::vector<BookLevel> bids, asks;
    for (int i = 0; i < 10; ++i) {
        bids.push_back({99972 - i * 3, 20 + i * 7});
        asks.push_back({99979 + i * 3, 15 + i * 5});
    }
    FeedEvent ev{};
    ev.kind = FeedEvent::Kind::Snapshot;
    ev.seq = 1;
    ev.bids = {bids.data(), static_cast<std::uint32_t>(bids.size())};
    ev.asks = {asks.data(), static_cast<std::uint32_t>(asks.size())};
    book.apply(ev);

    if (with_trades) {
        FeedEvent t{};
        t.kind = FeedEvent::Kind::Trade;
        t.seq = 2;
        t.px = 99972;
        t.qty = 9;
        t.side = Side::Bid;
        book.apply(t);
        t.seq = 3;
        t.px = 99979;
        t.qty = 14;
        t.side = Side::Ask;
        book.apply(t);
    }
    if (with_gap) {
        FeedEvent g{};
        g.kind = FeedEvent::Kind::Gap;
        g.seq = 4;
        g.reason = GapReason::Disconnect;
        book.apply(g);
    }

    DisplaySnapshot snap{};
    book.publish(snap);
    return snap;
}

std::vector<std::string> lines_of(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : s) {
        if (ch == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) { out.push_back(cur); }
    return out;
}

// Display columns, not bytes: the box glyphs are multi-byte UTF-8.
std::size_t columns(const std::string& line) {
    std::size_t n = 0;
    for (const unsigned char b : line) {
        if ((b & 0xC0) != 0x80) { ++n; }  // count lead bytes only
    }
    return n;
}

bool contains(const std::string& hay, std::string_view needle) {
    return hay.find(needle) != std::string::npos;
}

LadderStyle plain() {
    LadderStyle s;
    s.color = false;
    return s;
}

}  // namespace

TEST_CASE("a live ladder says LIVE and draws solid depth bars") {
    const std::string out = render_ladder(make_snapshot(/*with_gap=*/false), plain());

    CHECK(contains(out, "LIVE"));
    CHECK_FALSE(contains(out, "STALE"));
    CHECK(contains(out, "█"));         // solid block: live depth
    CHECK_FALSE(contains(out, "!! STALE"));
    CHECK(contains(out, "9.9972"));         // best bid, formatted exactly
    CHECK(contains(out, "spread"));
    CHECK(contains(out, "last "));
    CHECK(contains(out, "tape"));
}

TEST_CASE("a stale ladder cannot be mistaken for a live one — without any colour") {
    const std::string live = render_ladder(make_snapshot(false), plain());
    const std::string stale = render_ladder(make_snapshot(true), plain());

    CHECK(live != stale);

    // Channel 1: the word, in the header and again as a banner line.
    CHECK(contains(stale, "STALE"));
    CHECK(contains(stale, "!! STALE"));
    CHECK(contains(stale, "book unknown"));
    CHECK(contains(stale, "disconnect"));   // the reason, not just the state
    CHECK_FALSE(contains(stale, "LIVE"));

    // Channel 2: the bars are hatched, never solid.
    CHECK_FALSE(contains(stale, "█"));
    CHECK(contains(stale, "·"));

    // The ladder is still legible — the last known book is shown, not blanked.
    CHECK(contains(stale, "9.9972"));
}

TEST_CASE("a book that never arrived renders as stale, not as an empty live ladder") {
    Book book(kSpec);
    DisplaySnapshot snap{};
    book.publish(snap);

    const std::string out = render_ladder(snap, plain());
    CHECK(contains(out, "STALE"));
    CHECK(contains(out, "awaiting snapshot"));
    CHECK(contains(out, "no book"));
    CHECK_FALSE(contains(out, "LIVE"));
}

TEST_CASE("every row is the same width, in both glyph modes and both states") {
    for (const bool unicode : {true, false}) {
        for (const bool stale : {true, false}) {
            LadderStyle style = plain();
            style.unicode = unicode;
            const auto rows = lines_of(render_ladder(make_snapshot(stale), style));
            REQUIRE(rows.size() > 4);
            const std::size_t w = columns(rows.front());
            CHECK(w >= 56);
            for (const auto& row : rows) {
                CHECK(columns(row) == w);
            }
        }
    }
}

// The header and the stale banner grow with the seq digits and the gap reason,
// and both used to overflow the box: a fresh book renders "STALE awaiting
// snapshot" at seq 0 as a 62-column header inside a 57-column frame. The width
// test above missed it because make_snapshot() only ever produces Disconnect at
// a single-digit seq. This sweeps the two axes that actually drive the width.
TEST_CASE("the box contains the header and the banner at every reason and seq width") {
    const GapReason reasons[] = {GapReason::SeqGap, GapReason::ChecksumFail,
                                 GapReason::Disconnect, GapReason::Overflow,
                                 GapReason::Resync};
    const depthcharge::Seq seqs[] = {0, 4, 332, 1225, 999999, 18446744073709551615ULL};

    for (const bool unicode : {true, false}) {
        for (const GapReason reason : reasons) {
            for (const depthcharge::Seq seq : seqs) {
                LadderStyle style = plain();
                style.unicode = unicode;

                Book book(kSpec);
                std::vector<BookLevel> bids, asks;
                for (int i = 0; i < 6; ++i) {
                    bids.push_back({99972 - i * 3, 20 + i * 7});
                    asks.push_back({99979 + i * 3, 15 + i * 5});
                }
                FeedEvent ev{};
                ev.kind = FeedEvent::Kind::Snapshot;
                ev.seq = 1;
                ev.bids = {bids.data(), static_cast<std::uint32_t>(bids.size())};
                ev.asks = {asks.data(), static_cast<std::uint32_t>(asks.size())};
                book.apply(ev);

                FeedEvent gap{};
                gap.kind = FeedEvent::Kind::Gap;
                gap.seq = seq;
                gap.reason = reason;
                book.apply(gap);

                DisplaySnapshot snap{};
                book.publish(snap);

                const std::string label = "unicode=" + std::to_string(unicode) +
                                          " reason=" + std::to_string(static_cast<int>(reason)) +
                                          " seq=" + std::to_string(seq);
                INFO(label);

                const auto rows = lines_of(render_ladder(snap, style));
                REQUIRE(rows.size() > 4);
                const std::size_t w = columns(rows.front());
                for (const auto& row : rows) {
                    CHECK(columns(row) == w);
                }
            }
        }
    }
}

// The state every panel boots into, and the widest of all of them: nothing
// adopted, so the reason is "awaiting snapshot" and there are no level rows to
// pad the box out.
TEST_CASE("the boot ladder — nothing adopted — is square") {
    for (const bool unicode : {true, false}) {
        LadderStyle style = plain();
        style.unicode = unicode;

        Book book(kSpec);
        DisplaySnapshot snap{};
        book.publish(snap);

        const auto rows = lines_of(render_ladder(snap, style));
        REQUIRE(rows.size() >= 5);
        const std::size_t w = columns(rows.front());
        for (const auto& row : rows) {
            CHECK(columns(row) == w);
        }
        CHECK(contains(render_ladder(snap, style), "awaiting snapshot"));
    }
}

TEST_CASE("--ascii emits no non-ASCII bytes and --no-color emits no escapes") {
    LadderStyle style = plain();
    style.unicode = false;
    const std::string out = render_ladder(make_snapshot(true), style);

    for (const unsigned char b : out) {
        CHECK(b < 0x80);
        CHECK(b != 0x1b);
    }
    CHECK(contains(out, "STALE"));
    CHECK(contains(out, ":"));         // hatched ASCII bar: stale
    CHECK_FALSE(contains(out, "#"));   // solid ASCII bar: live only

    const std::string live = render_ladder(make_snapshot(false), style);
    CHECK(contains(live, "#"));
    CHECK_FALSE(contains(live, ":"));
}

TEST_CASE("colour is additive: turning it on changes escapes, not content") {
    LadderStyle colored;
    colored.color = true;
    const std::string with = render_ladder(make_snapshot(false), colored);
    CHECK(with.find('\x1b') != std::string::npos);
    CHECK(contains(with, "9.9972"));
    CHECK(contains(with, "LIVE"));
}

TEST_CASE("the tape shows prints newest-first, and says so when there are none") {
    std::string tape;
    for (const auto& row : lines_of(render_ladder(make_snapshot(false, true), plain()))) {
        if (contains(row, " tape ")) { tape = row; }
    }
    REQUIRE_FALSE(tape.empty());

    // Prints went in 9.9972 (buy) then 9.9979 (sell); the newest leads the tape.
    const std::size_t newest = tape.find("9.9979");
    const std::size_t older = tape.find("9.9972");
    REQUIRE(newest != std::string::npos);
    REQUIRE(older != std::string::npos);
    CHECK(newest < older);
    CHECK(contains(tape, "×14"));  // price × qty (U+00D7; --ascii swaps in 'x')
    CHECK_FALSE(contains(tape, "no prints yet"));

    const std::string without = render_ladder(make_snapshot(false, false), plain());
    CHECK(contains(without, "no prints yet"));
}
