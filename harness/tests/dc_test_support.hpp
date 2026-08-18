// dc_test_support.hpp — the builders and text helpers more than one test needs.
//
// These were copy-pasted between test_book.cpp and test_console_ladder.cpp,
// which is the same drift risk as any other duplication: two definitions of
// "a snapshot event" that can disagree about what they build.
#pragma once

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/feed_event.hpp>
#include <depthcharge/symbol.hpp>

namespace dc::testing {

// Ticker 101 at Anvil's declared scale — the subject of every committed trace.
//
// The third field was `qty_step = 1` (a divisor, identity) until M4 stage B1 and
// is now `qty_decimals = 0` (a scale, also identity). Spelled with designators
// because the two mean different things and the literal `1` would have kept
// compiling while quietly asserting one decimal place — the exact silent
// re-interpretation the change was made to avoid at the venue level.
inline constexpr depthcharge::SymbolSpec kSpec{
    /*id=*/101, /*price_decimals=*/4, /*qty_decimals=*/0};

// --- FeedEvent builders -------------------------------------------------------
//
// The Snapshot spans borrow from the caller's vectors, per the lifetime rule in
// feed_event.hpp: the vectors must outlive the apply() call.

inline depthcharge::FeedEvent snapshot_event(
    depthcharge::Seq seq, const std::vector<depthcharge::BookLevel>& bids,
    const std::vector<depthcharge::BookLevel>& asks) {
    depthcharge::FeedEvent ev{};
    ev.kind = depthcharge::FeedEvent::Kind::Snapshot;
    ev.seq = seq;
    ev.bids = {bids.data(), static_cast<std::uint32_t>(bids.size())};
    ev.asks = {asks.data(), static_cast<std::uint32_t>(asks.size())};
    return ev;
}

inline depthcharge::FeedEvent trade_event(depthcharge::Seq seq, depthcharge::PriceTicks px,
                                          depthcharge::Qty qty, depthcharge::Side side) {
    depthcharge::FeedEvent ev{};
    ev.kind = depthcharge::FeedEvent::Kind::Trade;
    ev.seq = seq;
    ev.px = px;
    ev.qty = qty;
    ev.side = side;
    return ev;
}

inline depthcharge::FeedEvent gap_event(depthcharge::Seq seq, depthcharge::GapReason reason) {
    depthcharge::FeedEvent ev{};
    ev.kind = depthcharge::FeedEvent::Kind::Gap;
    ev.seq = seq;
    ev.reason = reason;
    return ev;
}

// --- rendered-text helpers ----------------------------------------------------

// Split a rendered ladder into rows.
//
// render_ladder always ends with a newline, and views::split yields a trailing
// empty element for it that the row-width assertions would read as a zero-width
// row. Only that final empty is dropped: filtering ALL empties would make the
// width check blind to a blank row in the middle of the box, which is exactly
// the kind of renderer defect it exists to catch.
inline std::vector<std::string> lines_of(std::string_view s) {
    std::vector<std::string> out;
    for (const auto part : std::views::split(s, '\n')) {
        out.emplace_back(part.begin(), part.end());
    }
    if (!out.empty() && out.back().empty()) { out.pop_back(); }
    return out;
}

// Display columns, not bytes: the box glyphs are multi-byte UTF-8, and only a
// lead byte starts a new column. The parameter must be unsigned char — on a
// signed char the high-bit test changes meaning.
inline std::size_t columns(std::string_view line) {
    return static_cast<std::size_t>(std::ranges::count_if(
        line, [](unsigned char b) { return (b & 0xC0) != 0x80; }));
}

inline bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

}  // namespace dc::testing
