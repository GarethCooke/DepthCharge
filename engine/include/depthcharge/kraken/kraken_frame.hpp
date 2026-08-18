// depthcharge/kraken/kraken_frame.hpp — Kraken v2's wire frame, decoded.
//
// Adapter-INTERNAL vocabulary, exactly as anvil_frame.hpp is. KrakenFrame never
// crosses the adapter -> engine boundary; only FeedEvent does (invariant #2).
//
//     ParseStatus parse_kraken_frame(json, cfg, out);
//
// ONE IMPLEMENTATION, NOT TWO, AND THAT IS A DECISION RATHER THAN A SHORTCUT.
// Anvil's seam has two definitions (nlohmann reference + streaming target) for a
// historical reason: M1 shipped the reference and M3 had to prove a rewrite
// equivalent to it, so the reference's observable behaviour — nlohmann 3.11.3's
// accidents included — became the specification. Kraken has no such reference to
// be equivalent to, and inventing one would mean writing an allocating parser in
// order to have something for the allocation-free one to agree with. So there is
// one parser, it is the target-bound one, and `engine/src/kraken/
// kraken_frame_streaming.cpp` is compiled by dc_engine_target_check with the
// xtensa compiler from the day it lands (invariant #1's target half).
//
// WHAT THE PARSER MUST NOT DO, AND WHY IT IS THIS VENUE THAT MAKES IT BINDING.
// Kraken puts prices and quantities on the wire as BARE JSON NUMBERS, and its
// CRC32 is computed over their decimal text as sent. Stage 0 measured 0 of 2,786
// checksums surviving a float round-trip — not degraded, zero — because
// `0.50930100` becomes `0.509301` and `0.00005100` becomes `5.1e-05`, which the
// checksum has no spelling for at all. Invariant #3 is therefore not a stylistic
// rule here: a parser that touched a double would fail every message. This
// parser never converts a number itself. It finds the token's BYTES and hands
// them to depthcharge::parse_scaled, which is integer-only.
//
// AND THE COROLLARY, MEASURED AT B1 AND WORTH MORE THAN THE PROHIBITION:
// the integer form is checksum-EXACT, so no wire text has to be kept. The
// checksum tokenisation is "decimal text, point removed, leading zeros
// stripped", and every price and quantity in all five committed slices carries
// EXACTLY the venue's declared precision (8,172 level entries: BTC/USD 1 price
// decimal, MINA/GBP 4, both 8 qty decimals, zero exponent notation). At exactly
// that precision the token IS the decimal spelling of the scaled integer —
// `0.00005100` at 8 decimals is 5100 steps and tokenises to "5100" either way —
// so a book held as integers reproduces the venue's CRC32 with nothing retained.
// Verified end to end before this file was written: **4,878 / 4,878 checksums on
// the four slices that carry an opening snapshot, computed from integers alone.**
// That is what lets B2 add a CHECK rather than a parse, and it is why the frame
// below carries `checksum` as a number and no text buffer anywhere.
//
// (The fifth slice reads 0/49 by construction: it begins mid-stream with no
// snapshot, so the book being checksummed is not the venue's book. That is the
// right answer and it is also why that slice can never be a CRC golden.)
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "depthcharge/feed_event.hpp"
#include "depthcharge/symbol.hpp"

namespace depthcharge::kraken {

// The record kinds this build knows. Five were seen across 9,932 captured frames
// and no others (harness/replay/NOTES-kraken.md); `Unknown` covers anything a
// future server adds — tolerated, counted, ignored, exactly as at Anvil.
enum class FrameKind : std::uint8_t {
    Unknown,
    Heartbeat,     // {"channel":"heartbeat"} — 23 bytes, 1 Hz, THE liveness signal
    Status,        // {"channel":"status",...} — one per connection, carries a
                   //   connection_id above 2^53 that is deliberately never parsed
    BookSnapshot,  // book/snapshot — a full replace to the subscribed depth
    BookUpdate,    // book/update   — levels amended; qty 0 removes
    SubscribeAck,  // no `channel`, no `type`: method + result + success
};

enum class ParseStatus : std::uint8_t {
    Ok = 0,
    NotJson,        // not a JSON object at all / malformed past recovery
    BadShape,       // a known kind whose payload is the wrong shape
    BadPrice,       // a price the configured scale cannot hold EXACTLY
    BadQty,         // a quantity the configured scale cannot hold EXACTLY
    OtherSymbol,    // well-formed book message, none of whose data entries are ours
};

// What the parser needs to know to decode a book message: the scale to convert
// at, and the wire symbol to match `data[].symbol` against.
//
// The symbol is a STRING here and an integer everywhere past the adapter. That
// asymmetry is ARCHITECTURE §4's ("later venues map their string pair to a small
// id at config time so the engine stays integer-only") and this is the mapping
// point: the last place a venue's symbology exists is the adapter, which is the
// same rule that keeps JSON dialects and checksums out of the book.
struct SymbolConfig {
    SymbolSpec spec{};
    std::string_view wire_symbol{};  // e.g. "BTC/USD" — the v2 name, not `wsname`
};

// Decoded frame. Fixed capacity, no allocation. Sized like AnvilFrame at
// kMaxSnapshotLevels per side so a snapshot at any offered depth up to 256 fits;
// the deepest committed slice is depth 100.
struct KrakenFrame {
    FrameKind kind = FrameKind::Unknown;

    // ---- book messages ----------------------------------------------------
    // Levels EXACTLY AS SENT: a snapshot's are best-first, an update's are in no
    // order at all (measured: one message carrying asks 62807.0, 62818.8,
    // 62803.8 in that order). The parser does not sort them, because sorting is
    // the ladder's job and doing it here would hide that fact from the adapter.
    std::uint32_t bid_count = 0;
    std::uint32_t ask_count = 0;
    BookLevel bids[kMaxSnapshotLevels]{};
    BookLevel asks[kMaxSnapshotLevels]{};
    bool levels_truncated = false;  // deeper than kMaxSnapshotLevels; tail dropped

    // Carried, NOT verified (B1 scope; B2 adds the check). Present on BOTH a
    // snapshot and an update — which settles the brief's open question, since
    // Kraken v1's snapshot did not carry one.
    std::uint32_t checksum = 0;
    bool has_checksum = false;

    // How many `data` entries the message held, and how many were ours. `data`
    // is an ARRAY and one subscribe may name several symbols (six measured on
    // one socket), so the parser iterates it rather than indexing [0] — even
    // though every committed slice holds exactly one entry and nothing here
    // would catch the mistake.
    std::uint32_t data_entries = 0;
    std::uint32_t matched_entries = 0;

    // ---- subscribe ack ----------------------------------------------------
    bool ack_success = false;
    std::int64_t ack_depth = -1;      // result.depth; -1 if the ack carried none
    bool ack_has_error = false;       // an `error` member, e.g. the depth refusal

    void reset() noexcept {
        kind = FrameKind::Unknown;
        bid_count = 0;
        ask_count = 0;
        levels_truncated = false;
        checksum = 0;
        has_checksum = false;
        data_entries = 0;
        matched_entries = 0;
        ack_success = false;
        ack_depth = -1;
        ack_has_error = false;
        // The level arrays are intentionally not cleared: only [0, *_count) is
        // ever read, and clearing 8 KiB per frame is pure waste. Same rule as
        // AnvilFrame::reset.
    }
};

// Decode one verbatim WebSocket text frame.
//
// The implementation must be reentrant, must not allocate, must reset `out` on
// entry, and must leave it well-formed (kind == Unknown, counts and flags
// cleared) on any non-Ok status — the same postcondition anvil_frame.hpp states,
// and asserted the same way, because the adapter files different failures into
// different counters and those counters are goldens.
//
// `json` is a complete frame: the caller owns the bytes and they stay valid for
// the call. It may contain embedded NUL bytes and need not be NUL-terminated; an
// implementation that reads past `json.size()` is reading a network buffer it
// does not own.
ParseStatus parse_kraken_frame(std::string_view json, const SymbolConfig& cfg,
                               KrakenFrame& out) noexcept;

}  // namespace depthcharge::kraken
