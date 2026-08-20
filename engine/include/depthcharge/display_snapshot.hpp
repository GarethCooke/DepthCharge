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

// The published counts below are uint8_t, so the two capacities must fit one.
// Raising either past 255 would otherwise narrow silently in Book::publish and
// show up as a truncated ladder rather than a build failure.
static_assert(kDisplayLevels <= 255, "bid_count/ask_count are uint8_t");
static_assert(kTradeRingSize <= 255, "trade_count is uint8_t");

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

    // How far behind the venue this book is, in milliseconds — QUEUING LAG, and
    // not time since the last frame or time since the book last changed (M4
    // stage A2; the definition is pinned in depthcharge/age_estimator.hpp and in
    // ARCHITECTURE §5). `has_age` is false until the estimator has calibrated,
    // because "no reading yet" and "the book is current" are different
    // statements and exactly one of them is reassuring.
    //
    // NOT WRITTEN BY THE BOOK. The engine has no clock and is not being given
    // one: `Book::publish` fills every other field, and the feed side stamps
    // these two immediately afterwards from the venue's liveness deficit. Same
    // single writer (invariant #8), one line later.
    //
    // **NOTHING BRANCHES ON IT** (invariant #5): age is rendered as a number,
    // never as grey. Grey is `status`, which counts a different signal for a
    // different reason (the 2026-08-17 ruling).
    //
    // Placed HERE, and beside `has_last` below, for a reason worth keeping: both
    // land in alignment padding this struct already had — the 4-byte hole after
    // `version` and the 2 bytes before `last_px` — so the render hand-off did
    // not grow. See the static_assert at the foot of this file.
    std::uint32_t age_ms{};

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
    bool       has_age{};   // see age_ms above; packs into existing padding

    // HAS THIS BOOK EVER BEEN TOLD ANYTHING (M4 stage C).
    //
    // Not the same question as `has_book()`, and the difference is knowledge:
    // a side that has received nothing and a side the venue says is genuinely
    // empty both read `bid_count == 0`, and only the second is a statement
    // anybody made. Both are reachable in one run through B2's healing path.
    //
    // IT IS PUBLISHED RATHER THAN INFERRED, and the inference it replaces is a
    // lie in a reachable case: `!live() && !has_book()` reads "uninitialised"
    // for a book that adopted an empty snapshot and then took a Gap, which is a
    // book that HAS been told something. The renderer must be able to tell
    // "nothing yet" from "nothing there"; what it draws for each is stage D's.
    //
    // COSTS NOTHING, and that is checked rather than asserted in prose: it lands
    // in the pad byte this struct already had between `has_age` and `last_px`'s
    // 8-byte alignment, exactly as `age_ms` and `has_age` landed in padding at
    // stage A2. The static_assert at the foot of this file is what proves it —
    // if `sizeof` had moved, the three documents quoting it would have had to
    // move with it, and that is a decision rather than an edit.
    bool       initialised{};

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

// AND IT IS A FIXED SIZE, PINNED, because three documents quote a byte count
// derived from it: `firmware/src/main.cpp`'s static-footprint block (staging
// 1,168 B, SnapshotChannel 3,528 B), `snapshot_channel.hpp`'s cost note, and
// ARCHITECTURE §9's 2026-08-07 row, which sized the third mailbox slot on the
// target toolchain at 1,176 -> 3,528 B of `.data`. M4 stage A2 added two fields
// and this number did not move, because both went into padding — that is a
// property worth holding rather than rediscovering, since the next field added
// carelessly costs 3x its own size in the mailbox and silently invalidates three
// numbers nobody will re-measure. If this fires, the growth is fine but it is a
// DECISION: take it, and correct the three places above in the same commit.
static_assert(sizeof(DisplaySnapshot) == 1168,
              "DisplaySnapshot changed size — see the note above before adjusting this");

}  // namespace depthcharge
