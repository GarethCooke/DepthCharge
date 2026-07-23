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
// M0 scope: contract TYPES only. No behaviour, no parsing, no book logic — those
// land with the Anvil adapter and phase-1 book in M1.
#pragma once

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

// The one boundary type. A plain aggregate — no methods, no ownership — so it
// stays trivially copyable and allocation-free (invariant #7); the render/feed
// hand-off can memcpy it. Payload field meaning depends on `kind`:
//
//   Snapshot  full replacement of both sides to the venue's stated depth.
//             Depth beyond N is *unknown*, not zero. How the level list is
//             conveyed alongside the event is deliberately not fixed yet
//             (ARCHITECTURE.md §8; decided with the phase-1 book in M1) — no
//             owning container is baked into this struct.
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
};

// The hand-off and the embedded heap budget both depend on FeedEvent being a
// flat, copyable value; lock that at compile time so a future field addition
// that breaks it fails the build rather than the invariant.
static_assert(std::is_trivially_copyable_v<FeedEvent>,
              "FeedEvent must stay trivially copyable (ARCHITECTURE invariant #7)");

}  // namespace depthcharge
