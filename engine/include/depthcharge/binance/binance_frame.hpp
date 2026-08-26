// depthcharge/binance/binance_frame.hpp — Binance spot's wire frame, decoded.
//
// Adapter-INTERNAL vocabulary, exactly as anvil_frame.hpp and kraken_frame.hpp
// are. BinanceFrame never crosses the adapter -> engine boundary; only FeedEvent
// does (invariant #2).
//
//     ParseStatus parse_binance_frame(json, source, cfg, out);
//
// ONE PARSER, AND IT IS NOT A THIRD SCANNER (M5 stage B1, DESIGN strain 25).
// The card's stated trigger to extract shared JSON primitives was a THIRD venue.
// The trigger arrived and fired at nothing: stage 0 measured this venue's
// grammar as a STRICT SUBSET of what the existing scanners already handle, so
// there was no third scanner to write. What this file does instead is reuse
// Kraken's, which was lifted into `depthcharge/json_scan.hpp` unchanged — and
// the reuse needed NO venue flag, which was the brief's stop-and-raise
// condition. See that header for the re-measured subset figures.
//
// WHY KRAKEN'S SCANNER AND NOT ANVIL'S. Anvil's is deliberately bug-compatible
// with nlohmann 3.11.3's float handling, because M1 shipped an allocating
// reference parser and M3 had to prove a rewrite equivalent to it — accidents
// included. That specification is a liability here and buys nothing: this venue
// sends no float tokens at all. Kraken's carries no such history and hands back
// verbatim token bytes, which is exactly what integer scaling needs.
//
// WHAT THE WIRE LOOKS LIKE, and the three shapes this parser answers for:
//
//   diff       {"e":"depthUpdate","E":<ms>,"s":"BTCUSDT","U":<id>,"u":<id>,
//               "b":[["78732.14000000","3.91282000"],...],"a":[...]}
//   partial    {"lastUpdateId":<id>,"bids":[[..]],"asks":[[..]]}   (@depth20)
//   REST body  {"lastUpdateId":<id>,"bids":[[..]],"asks":[[..]]}   (/api/v3/depth)
//
// A LEVEL IS A TWO-ELEMENT ARRAY OF STRINGS, not an object of numbers. That is
// the one structural difference from Kraken worth naming: `["78732.14000000",
// "3.91282000"]`. The strings are unescaped in all 188,372 committed entries, so
// `scan_string` slices them in place and `parse_scaled` converts the bytes —
// no float, no allocation, no copy.
//
// **THE PARTIAL AND THE REST BODY ARE THE SAME SHAPE AND DIFFERENT THINGS.**
// Both carry `lastUpdateId`, `bids` and `asks` and nothing distinguishes them in
// the payload. Only the RECORD that carried them says which, which is why this
// parser takes a `FrameSource` rather than sniffing: a `@depth20` partial fully
// determines the top 20 and re-baselines nothing, while a REST body is a
// complete book to its `limit` and is the only thing at this venue that does.
// The trace reader draws the same distinction with `RecordForm` (M5 stage A),
// and tools/tracefile.py with `rebaselines(venue, frame, kind)`. Three places,
// one rule: the record says, the payload does not.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "depthcharge/feed_event.hpp"
#include "depthcharge/symbol.hpp"

namespace depthcharge::binance {

// The REST seed depth. `limit=1000` is the 2026-08-25 recommendation and it is
// a measurement rather than a preference: at `limit=100` a correct client scores
// 82.4% over 90 s, because 100 levels is a $16 price window on BTCUSDT and the
// pair walked $29.85 in that time. 1,000 covers ~$240.
inline constexpr std::int64_t kBinanceRestLimit = 1000;

// HOW DEEP THE ADAPTER'S BOOK AND ITS STAGING BUFFER MUST BE, measured rather
// than chosen — and the measurement says the honest answer is "hold what the
// seed gave you", because the requirement is a property of the market and not
// of the venue.
//
// `tools/binance_oracle.py --window-sweep` bounds a correct client's maintained
// book at N levels a side and grades it against the venue's own top-20 stream.
// Over the two deep-seed witnesses:
//
//     window     deepseed          deepseed2
//        100     21/235 matched    235/235  GREEN
//        200     95/235            235/235
//        256    202/235  (33 fail) 235/235
//        500    235/235  GREEN     235/235
//      1,000    235/235  GREEN     235/235
//
// **The two witnesses disagree by a factor of five**, on the same pair, the same
// day, an hour apart. A book bounded at `kMaxSnapshotLevels` (256) is clean on
// one and fails 33 of 235 graded ticks on the other — which is the *bounded
// window cannot refill* mutant occurring by accident rather than by design. So
// 256 is not a defensible bound, 500 is only a 2.4% margin over a number that
// moved 5x between two adjacent windows, and the only size that is not a bet is
// the one that holds everything the seed delivered.
//
// THE COST IS REAL AND IS STATED RATHER THAN BURIED: `BookLevel` is 16 bytes, so
// a 1,024-level ladder is 32 KiB and the staging frame another 32 KiB — 64 KiB
// of fixed, never-allocated state, against ~144 KiB the whole Anvil image uses
// today. Fixed-size, so invariant #7 holds; large, so **where it lives on the
// board is D's decision and not this stage's**.
//
// It also has to hold the largest single DIFF, which is not obviously smaller:
// measured maximum levels in one `depthUpdate`'s `b` or `a` across the seven
// committed slices is **537**, on the 1000 ms tick where a second of changes
// coalesces into one message. 1,024 covers both that and the 1,000-level seed.
inline constexpr std::uint32_t kBinanceMaxFrameLevels = 1024;
static_assert(kBinanceMaxFrameLevels >= static_cast<std::uint32_t>(kBinanceRestLimit),
              "the staging buffer must hold a full REST seed, or the seed is truncated "
              "on arrival and the book starts life shallower than the venue offered");

// The record kinds this build knows.
enum class FrameKind : std::uint8_t {
    Unknown,
    DepthUpdate,   // the diff stream: `e` == "depthUpdate", bracketed by U/u
    PartialDepth,  // @depth20 — a top-N replace that re-baselines NOTHING
    RestSnapshot,  // /api/v3/depth — the only thing here that re-baselines
    Ack,           // {"result":null,"id":N} — a subscribe reply on a /ws socket
};

enum class ParseStatus : std::uint8_t {
    Ok = 0,
    NotJson,         // not a JSON object at all / malformed past recovery
    BadShape,        // a known kind whose payload is the wrong shape
    BadPrice,        // a price the configured scale cannot hold EXACTLY
    BadQty,          // a quantity the configured scale cannot hold EXACTLY
    OtherSymbol,     // a diff for an instrument this adapter is not following
    // MORE LEVELS IN ONE MESSAGE THAN THE STAGING BUFFER HOLDS. Its own status
    // because its consequence is different in kind from the others: a truncated
    // SNAPSHOT is a shallower book, but a truncated DIFF is a book that has
    // silently missed amendments and looks perfectly healthy. The adapter turns
    // this into `Gap{Overflow}` — which §4 already defines as a venue reassembly
    // buffer overflowing, "first produced at a delta venue (M4/M5)". This is
    // that venue and this is that overflow; no new GapReason was needed.
    TooManyLevels,
};

// Which RECORD carried this payload. Not a hint and not a default: a `@depth20`
// partial and a `/api/v3/depth` body are byte-shape identical and mean different
// things, and only the record knows which. See the header note.
enum class FrameSource : std::uint8_t {
    WsFrame,   // arrived over the WebSocket, whatever stream
    RestBody,  // a fetch this client chose to make
};

// What the parser needs: the scale to convert at, and the wire symbol to match a
// diff's `s` against.
//
// THE SCALE IS A CONSTANT AND `tickSize` IS A VALIDATOR, which inverts what the
// same field meant at Kraken. Stage 0 measured every price and quantity on this
// venue at EXACTLY 8 decimals — 202,012 of 202,012 entries then, re-measured at
// B1 as 188,372 of 188,372 across the committed slices — on a 2 dp symbol and a
// 3 dp symbol alike. So the wire precision is a venue-wide constant and has
// nothing to do with the instrument's tick size; deriving the scale from
// `PRICE_FILTER.tickSize` would be wrong by 10^6 on BTCUSDT.
//
// `tick_ticks` and `step_ticks` are therefore not scales. They are the venue's
// declared tick size and lot step EXPRESSED AT THE 8-DECIMAL SCALE, and the
// adapter checks that every value is a whole multiple of them. A violation is a
// reported error, never a silent rounding (ARCHITECTURE §4). Zero disables the
// check for a symbol whose filters this build has not been told.
struct SymbolConfig {
    SymbolSpec spec{};
    std::string_view wire_symbol{};  // e.g. "BTCUSDT" — upper case, as `s` sends it
    PriceTicks tick_ticks = 0;       // PRICE_FILTER.tickSize at 8 decimals
    Qty step_ticks = 0;              // LOT_SIZE.stepSize at 8 decimals
};

// Decoded frame. Fixed capacity, no allocation, and LARGE — 32 KiB of level
// arrays. It is an adapter member and must not be a local: a BinanceFrame on the
// stack overflows a FreeRTOS task stack and comes close on the host's default.
struct BinanceFrame {
    FrameKind kind = FrameKind::Unknown;

    // Levels EXACTLY AS SENT and in the venue's own order. A REST body and a
    // partial arrive best-first; a diff arrives in no order at all. The parser
    // does not sort, because sorting is the ladder's job and doing it here would
    // hide that fact from the adapter — the same rule Kraken's parser follows.
    std::uint32_t bid_count = 0;
    std::uint32_t ask_count = 0;
    BookLevel bids[kBinanceMaxFrameLevels]{};
    BookLevel asks[kBinanceMaxFrameLevels]{};

    // ---- the diff stream's bracketing -------------------------------------
    // `U` and `u`: the first and final update ids this message covers. SPOT
    // CARRIES NO `pu` (the futures stream's previous-final-id), so continuity is
    // one-sided — `U == prev_u + 1` — and it is the only ordering signal there
    // is at this venue.
    std::int64_t first_update_id = 0;  // U
    std::int64_t final_update_id = 0;  // u
    bool has_update_range = false;

    // ---- the two book-bearing shapes --------------------------------------
    // `lastUpdateId`, carried by a partial and by a REST body and by nothing
    // else. On a REST body it is what the diff stream is bracketed against.
    std::int64_t last_update_id = 0;
    bool has_last_update_id = false;

    // Whether the message named an instrument and whether it was ours. A diff
    // carries `s`; a partial and a REST body carry NO symbol at all, so they are
    // identified by the stream that delivered them and `symbol_present` is false
    // for them. Distinguishing "not ours" from "did not say" is what stops a
    // partial being filed as another instrument's.
    bool symbol_present = false;
    bool symbol_matched = false;

    void reset() noexcept {
        kind = FrameKind::Unknown;
        bid_count = 0;
        ask_count = 0;
        first_update_id = 0;
        final_update_id = 0;
        has_update_range = false;
        last_update_id = 0;
        has_last_update_id = false;
        symbol_present = false;
        symbol_matched = false;
        // The level arrays are intentionally not cleared: only [0, *_count) is
        // ever read, and clearing 32 KiB per frame is pure waste. Same rule as
        // AnvilFrame::reset and KrakenFrame::reset — and it matters more here,
        // because the arrays are four times the size and arrive ten times a
        // second.
    }
};

// Decode one payload.
//
// The implementation must be reentrant, must not allocate, must reset `out` on
// entry, and must leave it well-formed (kind == Unknown, counts and flags
// cleared) on any non-Ok status — the same postcondition anvil_frame.hpp states
// and kraken_frame.hpp inherits, for the same reason: the adapter files
// different failures into different counters and those counters are goldens.
//
// THE POSTCONDITION IS WHY THIS STAGES 32 KiB RATHER THAN APPLYING AS IT SCANS.
// A parser that wrote levels straight into the adapter's ladder would need no
// staging buffer at all and would halve this venue's memory — and it could not
// offer the guarantee above, because a message that fails halfway would already
// have amended the book. "A caller that ignores the status cannot read half a
// book out of it" is not available to a streaming apply, and half a book that
// looks whole is the failure this project keeps writing rules about.
//
// `json` is a complete payload: the caller owns the bytes and they stay valid
// for the call. It may contain embedded NUL bytes and need not be
// NUL-terminated; an implementation that reads past `json.size()` is reading a
// network buffer it does not own.
//
// The COMBINED-STREAM ENVELOPE is unwrapped here and never stripped from any
// byte count: `/stream?streams=a/b` delivers `{"stream":...,"data":{...}}` and
// `/ws/<stream>` delivers the bare payload. Whether the wrapper is worth its
// bytes is a measured question (M5 stage 0) and a reader that discarded it would
// have destroyed the evidence.
ParseStatus parse_binance_frame(std::string_view json, FrameSource source,
                                const SymbolConfig& cfg, BinanceFrame& out) noexcept;

}  // namespace depthcharge::binance
