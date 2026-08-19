// depthcharge/window.hpp — which levels of the book earn one of the panel's rows.
//
// M4 stage C. The book holds up to 256 levels a side; the panel has 27 rows a
// side (`kDisplayLevels`). When the book is deeper than the panel, something has
// to choose — and this file is that choice, made explicit, selectable at compile
// time, and instrumented so stage D can judge it at desk distance rather than
// from an argument in a document.
//
// **C DOES NOT DECIDE WHICH POLICY IS RIGHT.** A host test can prove a window is
// well-formed, contiguous in rank, and never renders a level the book does not
// hold. It cannot prove a window is worth looking at.
//
// ============================================================================
// WHAT THE MEASUREMENT DID TO THE CANDIDATE SET, AND WHY IT IS RANK AND NOT PRICE
// ============================================================================
//
// The stage was proposed with three candidates, of which two were price-axis
// policies: a fixed tick band (one row per tick around the mid) and a
// density-adaptive band. **Measured on every committed trace before any of this
// was written, a real book is far too sparse in price for either to render
// anything** (`harness/replay/NOTES.md`, stage C addendum):
//
//     book                     gap p50   contiguous   one side spans
//     Kraken BTC/USD depth 25    5 tk       19.5%      182 ticks / 25 levels
//     Kraken MINA/GBP depth 25  10 tk       13.3%    6,613 ticks / 25 levels
//     Anvil 101 depth 27         7 tk       12.6%      252 ticks / 30 levels
//
// Only one adjacent level pair in five is one tick from its neighbour. A 27-row
// panel used as a 27-TICK price axis would show about four of BTC/USD's
// twenty-five levels, and on the quiet pair it would show **one** — the touch —
// with twenty-six blank rows. Widening a row to span several ticks fixes the
// coverage and costs the thing the policy existed for: a row would then be an
// AGGREGATE of levels rather than a level, and its width would be a constant
// chosen from the market's own span, which ARCHITECTURE §9 already has a row
// about.
//
// So the axis is RANK, and the three policies differ in which levels earn a row.
// Every one of them renders levels the book actually holds, which is what keeps
// `DisplaySnapshot`'s existing contract true and its `sizeof` unmoved.
//
// **A PRICE-AXIS WINDOW IS NOT REFUTED — IT IS BLOCKED, AND THE BLOCK IS PRICED
// RATHER THAN LEFT TO BE REDISCOVERED.** It needs to say *row 7 is not a level*
// with the unknown rows INTERLEAVED, and `bid_count` expresses only trailing
// ones. Two `uint32_t` present-masks carry it exactly (27 rows fit one, five
// bits spare) for **+8 bytes on DisplaySnapshot and +24 in the three-slot
// mailbox**, plus the three documents that quote the size. Written down in full
// at ROADMAP **D6** so that whoever wants it later argues against a number
// instead of measuring it again; ARCHITECTURE §9 (2026-08-19) has the decision
// and DESIGN §08 strain 24 has what it would buy.
//
// ============================================================================
// THE INVARIANTS, WHICH HOLD FOR EVERY POLICY AND ARE TESTED ONCE
// ============================================================================
//
//   1. A rendered row is a level the book holds. Never zero, never
//      interpolated, never synthesised to fill a gap.
//   2. Rows are monotonic in price, best-first: bids descending, asks ascending
//      — the order `feed_event.hpp` and `DisplaySnapshot` already require.
//   3. **Row 0 is the book's best level whenever the side is non-empty.** This
//      one is not decoration and it is not obvious: `DisplaySnapshot::best_bid`,
//      `best_ask` and `spread_ticks` all read row 0, and a policy free to drop a
//      thin touch in favour of a fat level behind it would make all three lie
//      while every other invariant still held.
//   4. Rows beyond the ones filled are UNKNOWN, not empty — `bid_count` says
//      where the knowledge stops (stage 0's decision: depth beyond N is unknown,
//      not zero).
//   5. Allocation-free, no history, a pure function of the book. A policy that
//      smoothed across frames would be a policy with state, and state is where
//      the single-writer invariant goes to die.
//
// Invariants #1-#3 are why a selection is expressed as INDICES into the book's
// own side and then copied, rather than as levels built up somewhere: an index
// cannot name a level the book does not hold, so #1 is structural rather than
// checked.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "depthcharge/display_snapshot.hpp"
#include "depthcharge/feed_event.hpp"

namespace depthcharge::window {

// The panel's rows are the cap in every real call; the parameter exists so the
// property tests can drive a policy at a cap the panel never uses, which is
// where the off-by-ones live.
inline constexpr std::uint32_t kMaxRows = static_cast<std::uint32_t>(kDisplayLevels);

enum class Policy : std::uint8_t {
    // The best `cap` levels, in book order. The status quo, and the identity
    // whenever the book is no deeper than the panel.
    TopOfBook,
    // The touch, plus the largest levels behind it, rendered in price order.
    // Shows the walls; drops the dust. The touch is forced in by invariant #3,
    // which is also what stops it from ever reporting a spread the book does
    // not have.
    LargestFirst,
    // The best half of the rows individually, then the tail sampled at an even
    // stride. Detail where the trading is, a sense of the depth behind it, and
    // no constant that is a market property — the stride is derived from the
    // book's own depth every frame.
    ThinnedTail,
};

// THE ONE THE FIRMWARE BUILDS WITH. A compile-time constant in the same style as
// the venue's subscribed depth (kraken_adapter.hpp), because which window the
// panel draws is a firmware decision and stage D is the stage that takes it.
// The harness selects at runtime instead, so a golden for each policy is a test
// rather than a rebuild.
inline constexpr Policy kWindowPolicy = Policy::TopOfBook;

// The policy's name, in one place. Used by the console report and by the
// goldens, so a report can never name a policy the run did not use.
constexpr std::string_view policy_name(Policy p) noexcept {
    switch (p) {
        case Policy::TopOfBook:    return "top";
        case Policy::LargestFirst: return "largest";
        case Policy::ThinnedTail:  return "thinned";
    }
    return "?";
}

// The reverse, for a command line. Returns false for anything else rather than
// falling back to a default: a typo that silently selected the status quo would
// make two runs that differ only in a misspelling produce identical output.
constexpr bool policy_from_name(std::string_view name, Policy& out) noexcept {
    for (const Policy p : {Policy::TopOfBook, Policy::LargestFirst, Policy::ThinnedTail}) {
        if (policy_name(p) == name) { out = p; return true; }
    }
    return false;
}

// What one side's selection did. REPORTED, NOT PUBLISHED: `DisplaySnapshot`'s
// size is pinned and quoted in three documents, and none of this is needed to
// draw a ladder — it is needed to judge one, which happens at a console.
struct WindowStats {
    std::uint32_t rows_filled = 0;
    std::uint32_t rows_unknown = 0;      // cap - rows_filled
    std::uint32_t levels_offered = 0;    // what the book held
    std::uint32_t levels_dropped = 0;    // held, and given no row
    // Best row to worst row inclusive, in ticks. A one-row window spans 1; an
    // empty one spans 0. This is the number that says how much of the price axis
    // the panel is actually showing, and it is the reason the price-axis
    // policies were measured out of the candidate set rather than argued out.
    PriceTicks tick_span = 0;
    // How many rendered rows show a level the VENUE ever confirmed. B2 measured
    // that Kraken's CRC32 covers the top 10 levels a side, so a window's
    // position decides whether what the panel shows was ever checked. C records
    // it; C does not solve it (stage C brief §4).
    std::uint32_t rows_validated = 0;
};

namespace detail {

// Insertion of `idx` into a fixed array kept sorted by (qty desc, rank asc).
// O(cap) per candidate, cap <= 27, and no allocation. Ties break toward the
// better price, which is what makes the result a function of the book rather
// than of the loop order.
constexpr void offer(std::uint32_t* best, std::uint32_t& n, std::uint32_t cap,
                     const BookLevel* src, std::uint32_t idx) noexcept {
    if (cap == 0) { return; }
    std::uint32_t at = n;
    while (at > 0) {
        const std::uint32_t prev = best[at - 1];
        const bool better = src[idx].qty > src[prev].qty ||
                            (src[idx].qty == src[prev].qty && idx < prev);
        if (!better) { break; }
        --at;
    }
    if (at >= cap) { return; }
    const std::uint32_t top = n < cap ? n : cap - 1;
    for (std::uint32_t k = top; k > at; --k) { best[k] = best[k - 1]; }
    best[at] = idx;
    if (n < cap) { ++n; }
}

// Sort the chosen indices ascending. The source side is price-ordered, so an
// ascending index order IS price order — which is invariant #2 obtained without
// a second comparator that could disagree with `ladder::better`.
constexpr void sort_indices(std::uint32_t* idx, std::uint32_t n) noexcept {
    for (std::uint32_t i = 1; i < n; ++i) {
        const std::uint32_t v = idx[i];
        std::uint32_t j = i;
        while (j > 0 && idx[j - 1] > v) { idx[j] = idx[j - 1]; --j; }
        idx[j] = v;
    }
}

// TopOfBook: the first `cap` indices.
constexpr std::uint32_t choose_top(std::uint32_t count, std::uint32_t cap,
                                   std::uint32_t* out) noexcept {
    const std::uint32_t n = count < cap ? count : cap;
    for (std::uint32_t i = 0; i < n; ++i) { out[i] = i; }
    return n;
}

// LargestFirst: index 0 always, then the largest of the rest, then price order.
constexpr std::uint32_t choose_largest(const BookLevel* src, std::uint32_t count,
                                       std::uint32_t cap, std::uint32_t* out) noexcept {
    if (count == 0 || cap == 0) { return 0; }
    if (count <= cap) { return choose_top(count, cap, out); }

    // Slot 0 is reserved for the touch (invariant #3), so the competition is for
    // cap - 1 places among levels 1..count.
    std::uint32_t n = 0;
    const std::uint32_t room = cap - 1;
    for (std::uint32_t i = 1; i < count; ++i) { offer(out + 1, n, room, src, i); }
    sort_indices(out + 1, n);
    out[0] = 0;
    return n + 1;
}

// ThinnedTail: the first `cap / 2` levels, then the remainder sampled evenly.
//
// The stride is `(count - head) / (cap - head)` expressed as a running
// multiplication rather than a division, so it is exact and strictly increasing
// without a rounding rule: with count > cap the tail is longer than the rows
// left for it, so consecutive indices differ by at least one.
constexpr std::uint32_t choose_thinned(std::uint32_t count, std::uint32_t cap,
                                       std::uint32_t* out) noexcept {
    if (count <= cap) { return choose_top(count, cap, out); }
    if (cap == 0) { return 0; }   // `rows` below would be 0, and it is a divisor
    const std::uint32_t head = cap / 2;
    for (std::uint32_t i = 0; i < head; ++i) { out[i] = i; }
    const std::uint32_t rows = cap - head;      // > 0, since cap > head
    const std::uint32_t tail = count - head;    // > rows, since count > cap
    for (std::uint32_t k = 0; k < rows; ++k) {
        out[head + k] = head + static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(k) * tail) / rows);
    }
    return cap;
}

}  // namespace detail

// Fill `dst[0, cap)` from one best-first side of the book, and say what happened.
//
// `validated_depth` is how many of the book's BEST levels an external check has
// confirmed — Kraken's CRC32 covers 10, Anvil has no checksum and passes 0. It
// is a parameter rather than a constant because it is a venue fact and this
// header is venue-free (invariant #2): the moment a checksum rule lives in
// `engine/`'s shared code, the book knows about Kraken.
// `cap` is CLAMPED to `kMaxRows` rather than honoured: `dst` is a
// `DisplaySnapshot` side in every real call and writing past 27 rows would run
// off it. A caller asking for more gets 27 and the stats say so — `rows_filled`
// plus `rows_unknown` is the clamped figure, not the one that was asked for.
inline WindowStats select(Policy policy, const BookLevel* src, std::uint32_t count,
                          BookLevel* dst, std::uint32_t cap,
                          std::uint32_t validated_depth = 0) noexcept {
    std::uint32_t idx[kMaxRows]{};
    const std::uint32_t room = cap < kMaxRows ? cap : kMaxRows;

    std::uint32_t n = 0;
    switch (policy) {
        case Policy::TopOfBook:    n = detail::choose_top(count, room, idx); break;
        case Policy::LargestFirst: n = detail::choose_largest(src, count, room, idx); break;
        case Policy::ThinnedTail:  n = detail::choose_thinned(count, room, idx); break;
    }

    WindowStats st;
    st.rows_filled = n;
    st.rows_unknown = room - n;
    st.levels_offered = count;
    st.levels_dropped = count - n;
    for (std::uint32_t i = 0; i < n; ++i) {
        dst[i] = src[idx[i]];
        if (idx[i] < validated_depth) { ++st.rows_validated; }
    }
    if (n > 0) {
        const PriceTicks a = dst[0].px;
        const PriceTicks b = dst[n - 1].px;
        st.tick_span = (a < b ? b - a : a - b) + 1;
    }
    return st;
}

}  // namespace depthcharge::window
