// depthcharge/anvil/anvil_frame.hpp — Anvil's wire frame, decoded.
//
// This is adapter-INTERNAL vocabulary. AnvilFrame never crosses the adapter ->
// engine boundary; only FeedEvent does (invariant #2). It exists so that the one
// piece of the adapter that is genuinely environment-specific — turning JSON
// bytes into fields — sits behind a single function declaration:
//
//     ParseStatus parse_anvil_frame(json, spec, out);
//
// There are two definitions, and the linker picks one (CMake: dc_engine_anvil
// vs dc_engine_anvil_streaming):
//
//   engine/src/anvil/anvil_frame_nlohmann.cpp   M1, host only. Allocates per
//       parse, which is fine on the desk and NOT fine on the feed path of a
//       microcontroller (invariant #7).
//   engine/src/anvil/anvil_frame_streaming.cpp  M3 stage B. Hand-rolled,
//       allocation-free, compiles for xtensa; this is what firmware/ links.
//
// The swap touched no other line of the adapter, the book, or the goldens —
// which is the whole point of the seam, and why the M1 brief said "do not pick
// the firmware parser here".
//
// EQUIVALENCE IS PART OF THIS DECLARATION. Two implementations of one function
// are only useful if they are indistinguishable, and the way that is settled
// here is: the nlohmann implementation's observable behaviour is the
// specification, including the parts of it that are accidents of nlohmann 3.11.3
// rather than decisions anyone made (duplicate keys resolve last-wins; an
// integer too large for uint64_t is a *float*, so it is a shape error; a NUL
// byte ends the document). Those are load-bearing, because the adapter files
// BadPrice, OtherTicker and everything-else into three different counters and
// the counters are goldens. The equivalence is executed, not asserted:
// harness/tests/test_parser_equivalence.cpp is compiled into both test binaries
// and holds both implementations to one table. A third implementation would
// have to pass it too.
//
// The decoded frame is already in engine vocabulary: prices are PriceTicks and
// levels are BookLevel. The conversion itself is NOT each parser's business —
// it lives once, in anvil_scaling.hpp, which both include. A parser's job stops
// at finding the token's bytes. Wire-shaped mess — the "type" string, the
// aggressor letter, order ids, the global seq — stops here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "depthcharge/feed_event.hpp"
#include "depthcharge/symbol.hpp"

namespace depthcharge::anvil {

// Anvil wire v1 frame kinds (docs/vendor/anvil-protocol.md §3). `Summary` is
// the cross-ticker roster every socket receives regardless of subscription; it
// feeds the M7 board mode, not the ladder. `Unknown` covers anything a future
// server adds — tolerated, counted, ignored.
enum class FrameKind : std::uint8_t { Unknown, Snapshot, Book, Trade, Summary };

enum class ParseStatus : std::uint8_t {
    Ok = 0,
    NotJson,          // not a JSON object at all
    MissingType,      // no "type" string
    BadShape,         // required field missing or of the wrong JSON type
    BadPrice,         // a price string the configured scale cannot hold exactly
    OtherTicker,      // well-formed, but for a ticker this adapter is not on
};

// Decoded frame. Fixed capacity, no allocation: 2 x 256 x 16 B = 8 KiB, so the
// owner (AnvilAdapter) is constructed once at init and never on a task stack.
struct AnvilFrame {
    FrameKind kind = FrameKind::Unknown;

    // Anvil's global counter. Decoded for diagnostics only — it is NOT used for
    // ordering or gap detection (ARCHITECTURE §4; M0 measured it running
    // backwards 42 times in 5 minutes on one socket).
    std::int64_t wire_seq = 0;
    bool has_wire_seq = false;

    std::uint32_t ticker = 0;
    bool has_ticker = false;

    // Snapshot / Book: best-first, already scaled to ticks.
    std::uint32_t bid_count = 0;
    std::uint32_t ask_count = 0;
    bool levels_truncated = false;  // deeper than kMaxSnapshotLevels; tail dropped
    BookLevel bids[kMaxSnapshotLevels]{};
    BookLevel asks[kMaxSnapshotLevels]{};

    // Trade.
    PriceTicks trade_px = 0;
    Qty trade_qty = 0;
    Side aggressor = Side::Bid;

    void reset() noexcept {
        kind = FrameKind::Unknown;
        wire_seq = 0;
        has_wire_seq = false;
        ticker = 0;
        has_ticker = false;
        bid_count = 0;
        ask_count = 0;
        levels_truncated = false;
        trade_px = 0;
        trade_qty = 0;
        aggressor = Side::Bid;
        // The level arrays are intentionally not cleared: only [0, *_count) is
        // ever read, and clearing 8 KiB per frame at 12 Hz is pure waste.
    }
};

// Decode one verbatim WebSocket text frame. `spec.price_decimals` is the scale
// every price string must fit exactly; a price with more precision than that
// yields BadPrice rather than a rounded level (ARCHITECTURE §4).
//
// Implementations must be reentrant, must not allocate into `out`, must reset
// `out` on entry, and must leave it well-formed (kind == Unknown, all counts
// and flags cleared) on any non-Ok status. The parser is the sole owner of that
// reset — callers do not pre-clear the frame. test_anvil_adapter.cpp asserts
// this against every failure mode, and is compiled into both test binaries, so
// the second implementation inherited the check rather than re-earning it.
//
// `json` is a complete frame: the caller owns the bytes and they stay valid for
// the call. Deliberately (ptr, len) rather than an incremental feed — WebSocket
// reassembly is the transport's job (M3 stage C, a fixed buffer sized from M0's
// ~8 KB frames), not the decoder's. It may contain embedded NUL bytes and need
// not be NUL-terminated; an implementation that reads past `json.size()` is
// reading a network buffer it does not own.
ParseStatus parse_anvil_frame(std::string_view json, const SymbolSpec& spec,
                              AnvilFrame& out) noexcept;

}  // namespace depthcharge::anvil
