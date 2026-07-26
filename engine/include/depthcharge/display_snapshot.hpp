// depthcharge/display_snapshot.hpp — what the book publishes to whatever draws.
//
// ARCHITECTURE §5: "top ~27 levels/side (fits 64 rows with header, spread gap,
// sparkline strip), recent-trade ring (>=8), last px, status {Live | Stale
// (reason)}, symbol id." This is that type, and nothing more — it is the render
// side's entire universe (invariant #8: only the render task reads it, only the
// feed task writes it).
//
// Design constraints it has to satisfy:
//   #7  Flat, fixed-capacity, trivially copyable: no pointers, no containers,
//       no allocation. The M3 double buffer publishes it by copy.
//   #3  Integer ticks throughout. `symbol.price_decimals` exists so the display
//       edge can format them exactly (depthcharge::format_scaled) — it is the
//       scale, not a float.
//   #5  `status` is part of the payload, not a side channel, so it is
//       impossible to render the ladder without also having the honesty bit.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "depthcharge/feed_event.hpp"
#include "depthcharge/symbol.hpp"

namespace depthcharge {

// 64 panel rows minus header, spread row and the sparkline strip leaves ~27 a
// side (ARCHITECTURE §5). The console ladder renders the same budget so the
// host preview is honest about what will fit.
inline constexpr std::size_t kDisplayLevels = 27;

// "recent-trade ring (>=8)". Eight prints at Anvil's ~1.5 trades/s is ~5 s of
// tape, which is what the bottom strip can show.
inline constexpr std::size_t kTradeRingSize = 8;

// Live means: a Snapshot has been adopted and nothing has told us to doubt it.
// Anything else is Stale — there is no third state and no "probably fine"
// (invariant #5).
enum class FeedStatus : std::uint8_t { Live, Stale };

struct TradePrint {
    PriceTicks px{};
    Qty        qty{};
    Seq        seq{};       // the synthesised event seq that carried it
    Side       aggressor{};
};

struct DisplaySnapshot {
    // Bumped by the producer on every publish; the render side uses it to tell
    // "new frame" from "same frame" without comparing contents. This is the
    // version stamp ARCHITECTURE §5 requires of the double buffer.
    std::uint32_t version{};

    SymbolSpec symbol{};

    // Seq of the last event folded into this snapshot.
    Seq seq{};

    // Stale-by-construction: nothing has been adopted yet, so nothing may be
    // drawn as live. `stale_reason` is only meaningful when status == Stale.
    FeedStatus status{FeedStatus::Stale};
    GapReason  stale_reason{GapReason::Resync};

    std::uint8_t bid_count{};
    std::uint8_t ask_count{};
    std::uint8_t trade_count{};

    bool       has_last{};
    PriceTicks last_px{};

    BookLevel  bids[kDisplayLevels]{};    // best-first: descending px
    BookLevel  asks[kDisplayLevels]{};    // best-first: ascending px
    TradePrint trades[kTradeRingSize]{};  // newest first

    constexpr bool live() const noexcept { return status == FeedStatus::Live; }
    constexpr bool has_book() const noexcept { return bid_count > 0 || ask_count > 0; }
    constexpr bool has_top() const noexcept { return bid_count > 0 && ask_count > 0; }

    // Callers must check has_top() first; there is no sentinel price, because a
    // sentinel is exactly the kind of value that ends up rendered as a level.
    constexpr PriceTicks best_bid() const noexcept { return bids[0].px; }
    constexpr PriceTicks best_ask() const noexcept { return asks[0].px; }
    constexpr PriceTicks spread_ticks() const noexcept { return asks[0].px - bids[0].px; }

    // Mid doubled, so the mid of an odd spread stays exact in integers
    // (invariant #3 — the display edge halves it for text if it wants to).
    constexpr PriceTicks mid_ticks_x2() const noexcept { return bids[0].px + asks[0].px; }
};

// The render hand-off is a copy of this object; anything that breaks that is a
// design change, not an implementation detail (invariant #7).
static_assert(std::is_trivially_copyable_v<DisplaySnapshot>,
              "DisplaySnapshot must stay trivially copyable (ARCHITECTURE invariant #7)");

}  // namespace depthcharge
