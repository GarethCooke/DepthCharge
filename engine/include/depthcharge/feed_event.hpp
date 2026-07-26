// depthcharge/feed_event.hpp — the adapter -> engine boundary contract.
//
// FeedEvent is the ONLY type that crosses the venue-adapter -> book-engine
// boundary (ARCHITECTURE.md invariant #2). This header is, from M0 onward, the
// source of truth for that contract (ARCHITECTURE.md §4); it must stay in sync
// with §4's stated intent. Any change to the vocabulary is an architectural
// decision and belongs in ARCHITECTURE.md §9, not just here.
//
// Binding invariants exercised by this header:
//   #1  No ESP-IDF / FreeRTOS / Arduino includes — <cstdint>/<type_traits>
//       are freestanding-safe standard headers, so this builds on the host and
//       the target unchanged.
//   #3  Integer ticks and steps everywhere; no floating point ever touches book
//       data. Every field below is an integer or an enum.
//
// M1 added the one thing M0 left open (ARCHITECTURE §8): how a Snapshot's level
// list is conveyed. It is *borrowed* — two spans into adapter-owned storage,
// valid only for the duration of the sink call. See LevelSpan below.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace depthcharge {

// Price in integer ticks; quantity in integer venue steps. The per-symbol
// tick_size / qty_step that scale these to human units live in venue symbol
// metadata (see ARCHITECTURE.md §4). int64_t because crypto tick sizes and
// price ranges vary wildly across symbols and venues, and exact integer
// equality — no key drift under replay — is the entire point.
using PriceTicks = std::int64_t;
using Qty        = std::int64_t;

// Adapter-normalised, monotonic per (venue, symbol) stream. Each adapter is
// responsible for producing this from its venue's native scheme; a raw wire
// sequence number is NOT necessarily usable as-is (see the M0 NOTES for Anvil,
// whose global-counter seq is non-monotonic in a single ticker's subsequence).
using Seq = std::uint64_t;

enum class Side : std::uint8_t { Bid, Ask };

// Every discontinuity the engine must treat as "book unknown until next
// Snapshot" arrives as a Gap carrying one of these reasons. Gap is data, not an
// error (ARCHITECTURE.md §4 / invariant #5).
enum class GapReason : std::uint8_t { SeqGap, ChecksumFail, Disconnect, Overflow, Resync };

// One price level: absolute resting quantity at a price. Aggregate across the
// venue's stated depth in a Snapshot; a single amended level in a Delta.
struct BookLevel {
    PriceTicks px{};
    Qty        qty{};
};

// The deepest snapshot any adapter may hand across the boundary, per side. It
// bounds the adapter's staging buffer and the book's storage in one number, so
// there is exactly one place where depth is truncated (the adapter) and the
// book can never silently drop a level the adapter accepted. Anvil's live
// stream runs 83–126 levels/side (M0 NOTES), Kraken tops out at 1000 but we
// only ever render ~27 — deeper than this is thrown away with the truncation
// counted and reported, which is legal: depth beyond N is *unknown*, not zero.
inline constexpr std::size_t kMaxSnapshotLevels = 256;

// A Snapshot's levels, borrowed. Non-owning by design: FeedEvent must stay a
// flat trivially-copyable value (invariant #7) and the only boundary type
// (invariant #2), which rules out both a container member and a second
// "here are the levels" callback.
//
// LIFETIME (normative): the pointed-at levels belong to the adapter and are
// valid only for the duration of the sink call that delivered the event. A
// consumer that defers the event — a queue, a ring, another task — MUST copy
// the levels first. The phase-1 book copies on adopt.
//
// Ordering: best-first. bids descending by px, asks ascending.
struct LevelSpan {
    const BookLevel* data{};
    std::uint32_t    size{};

    constexpr bool empty() const noexcept { return size == 0; }
    constexpr const BookLevel* begin() const noexcept { return data; }
    constexpr const BookLevel* end() const noexcept { return data + size; }
    constexpr const BookLevel& operator[](std::uint32_t i) const noexcept { return data[i]; }
};

// The one boundary type. A plain aggregate — no methods, no ownership — so it
// stays trivially copyable and allocation-free (invariant #7); the render/feed
// hand-off can memcpy it. Payload field meaning depends on `kind`:
//
//   Snapshot  full replacement of both sides to the venue's stated depth,
//             carried in `bids`/`asks` (borrowed — see LevelSpan). Depth beyond
//             N is *unknown*, not zero.
//   Delta     one level: `px`, absolute `qty` (qty == 0 => level removed),
//             `side`.
//   Trade     a fill: `px`, `qty`, `side` = aggressor.
//   Gap       `reason`; book state is unknown until the next Snapshot.
struct FeedEvent {
    enum class Kind : std::uint8_t { Snapshot, Delta, Trade, Gap };

    Kind kind{};
    Seq  seq{};

    PriceTicks px{};      // Delta, Trade
    Qty        qty{};     // Delta, Trade
    Side       side{};    // Delta, Trade (aggressor)
    GapReason  reason{};  // Gap

    LevelSpan  bids{};    // Snapshot (borrowed for the sink call only)
    LevelSpan  asks{};    // Snapshot (borrowed for the sink call only)
};

// The hand-off and the embedded heap budget both depend on FeedEvent being a
// flat, copyable value; lock that at compile time so a future field addition
// that breaks it fails the build rather than the invariant.
static_assert(std::is_trivially_copyable_v<FeedEvent>,
              "FeedEvent must stay trivially copyable (ARCHITECTURE invariant #7)");

}  // namespace depthcharge
