// depthcharge/ladder.hpp — one definition of "sorted", and the three operations
// every price ladder in this project performs on it.
//
// M4 stage B1. Two ladders now exist and they must agree exactly:
//
//   * the ADAPTER's, which holds the venue's book truncated to the subscribed
//     depth (kraken/kraken_adapter.hpp), and
//   * the ENGINE's, which holds what the adapter told it (book.hpp).
//
// The engine's is a mirror of the adapter's, built entirely from the Snapshots
// and Deltas the adapter emits — so if the two disagree about which of two
// prices ranks better, the panel draws a book the adapter never had, and no
// counter anywhere would say so. Both were written with their own private
// `better()` before this header existed. They agreed. That is exactly the
// condition under which a second copy of a rule survives long enough to matter
// (the same hazard DESIGN strain 3 names, one level down).
//
// So the MECHANISM is shared and the POLICY is not. Finding a rank, shifting an
// array and dropping a displaced level are identical everywhere and live here;
// what to DO about a displaced level is a venue decision (the adapter emits a
// removal, because Kraken sends none of its own) or an engine one (the book
// counts it, because through a truncating adapter it cannot happen), and those
// stay where they belong.
//
// Invariants: #1 (freestanding-safe standard headers only — host and xtensa),
// #3 (integer ticks; no float in the type), #7 (no allocation, everything
// operates on caller-owned fixed arrays).
#pragma once

#include <cstdint>

#include "depthcharge/feed_event.hpp"

namespace depthcharge::ladder {

// Does `a` rank better than `b` on this side? Bids rank high-to-low, asks
// low-to-high — best-first, which is the order feed_event.hpp requires of a
// Snapshot's spans, the order DisplaySnapshot is drawn in, and the order
// Kraken's CRC32 assumes ("asks ascending then bids descending").
//
// THE ONE DEFINITION. Everything else in this header is arithmetic around it.
constexpr bool better(PriceTicks a, PriceTicks b, Side side) noexcept {
    return side == Side::Bid ? a > b : a < b;
}

// Where `px` belongs in a side that is already sorted: the index of the first
// level that does not rank better than it. If a level at `px` is present this is
// its index, which is what makes one scan answer both "where does it go" and
// "is it already here".
//
// Linear, and deliberately so: a subscribed depth is 10-100, the measured
// traffic is 1.55 levels per message, and a binary search over a 25-element
// array of 16-byte structs is slower on an LX7 than the scan it replaces.
constexpr std::uint32_t rank_of(const BookLevel* side, std::uint32_t count,
                                PriceTicks px, Side s) noexcept {
    std::uint32_t at = 0;
    while (at < count && better(side[at].px, px, s)) { ++at; }
    return at;
}

// Is `at` an existing level at exactly `px`? Reads as a question at the call
// site, where `at < count && side[at].px == px` reads as an index calculation.
constexpr bool holds(const BookLevel* side, std::uint32_t count, std::uint32_t at,
                     PriceTicks px) noexcept {
    return at < count && side[at].px == px;
}

// Remove the level at `at`, closing the gap.
constexpr void erase_at(BookLevel* side, std::uint32_t& count, std::uint32_t at) noexcept {
    for (std::uint32_t k = at; k + 1 < count; ++k) { side[k] = side[k + 1]; }
    if (count > 0) { --count; }
}

// Insert `lvl` at rank `at`, keeping at most `cap` levels.
//
// If the side was already full the WORST level is displaced: it is written to
// `evicted` and true is returned. The caller decides what that means — at a
// truncating venue it is a removal the venue will never send and the adapter
// must synthesise, and in the engine's book it cannot happen and is counted.
//
// `at >= cap` is the caller's to check, not this function's: "this level is
// worse than everything I am subscribed to" is a venue judgement with a counter
// attached, and silently dropping it here would hide it.
constexpr bool insert_at(BookLevel* side, std::uint32_t& count, std::uint32_t at,
                         const BookLevel& lvl, std::uint32_t cap,
                         BookLevel& evicted) noexcept {
    if (cap == 0 || at >= cap) { return false; }
    const bool full = count >= cap;
    if (full) { evicted = side[cap - 1]; }
    const std::uint32_t top = full ? cap - 1 : count;
    for (std::uint32_t k = top; k > at; --k) { side[k] = side[k - 1]; }
    side[at] = lvl;
    if (!full) { ++count; }
    return full;
}

}  // namespace depthcharge::ladder
