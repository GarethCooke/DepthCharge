// engine/src/anvil/anvil_frame_nlohmann.cpp — the M1 (host) implementation of
// parse_anvil_frame, backed by nlohmann/json.
//
// THIS TU IS HOST-ONLY. It is the single file the M3 firmware replaces with a
// streaming, allocation-free parser (see anvil_frame.hpp for why the seam is
// here). nlohmann allocates per parse, which invariant #7 forbids on the feed
// path of the target; on the desk it buys a rigorously correct reference
// decoder for free, and the goldens it green-lights are what the replacement
// will have to reproduce byte for byte.
//
// Two rules this file follows that the replacement must also follow:
//   * No exceptions escape (the declaration is noexcept): parsing is done with
//     allow_exceptions = false and every field is type-checked before it is read.
//   * No floating point ever touches a price. nlohmann is asked for the price
//     *string*; depthcharge::parse_scaled converts it exactly (invariant #3).
//     Never json.get<double>() on a price — that is the bug this whole design
//     exists to make impossible.
#include "depthcharge/anvil/anvil_frame.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#include "depthcharge/decimal.hpp"

#include <nlohmann/json.hpp>

namespace depthcharge::anvil {
namespace {

using nlohmann::json;

// The only way this file reads a JSON string. is_string() already guarantees
// get_ptr() is non-null, so the three hand-written null checks it replaces were
// unreachable branches — untestable, and therefore permanently uncovered.
const std::string* as_string(const json& node) noexcept {
    return node.is_string() ? node.get_ptr<const std::string*>() : nullptr;
}

FrameKind kind_from_type(std::string_view type) noexcept {
    if (type == "book") { return FrameKind::Book; }
    if (type == "snapshot") { return FrameKind::Snapshot; }
    if (type == "trade") { return FrameKind::Trade; }
    if (type == "summary") { return FrameKind::Summary; }
    return FrameKind::Unknown;  // incl. the reserved "error" frame (protocol §3.4)
}

// Anvil prices are JSON strings ("9.9972", "10"). Anything else — a JSON number,
// a null — is a shape error, not something to coerce.
bool price_to_ticks(const json& node, const SymbolSpec& spec, PriceTicks& out,
                    ParseStatus& status) noexcept {
    const std::string* s = as_string(node);
    if (s == nullptr) {
        status = ParseStatus::BadShape;
        return false;
    }
    const DecimalParse p = parse_scaled(*s, spec.price_decimals);
    if (!p.ok()) {
        // TooManyDecimals means the declared scale is wrong for this venue —
        // the loud failure ARCHITECTURE §4 asks for, instead of a silent round.
        status = (p.error == DecimalError::BadScale) ? ParseStatus::BadShape
                                                     : ParseStatus::BadPrice;
        return false;
    }
    out = p.value;
    return true;
}

// Wire quantities are whole units; SymbolSpec::qty_step says how many wire units
// make one Qty step (Anvil: 1, so this is an identity with an exactness check).
// The spec itself is validated once per frame by the caller, not once per level.
bool qty_to_steps(const json& node, const SymbolSpec& spec, Qty& out,
                  ParseStatus& status) noexcept {
    if (!node.is_number_integer()) {
        status = ParseStatus::BadShape;
        return false;
    }
    const std::int64_t raw = node.get<std::int64_t>();
    // A negative resting size or fill size is not a quantity, and the adapter is
    // the quarantine boundary (invariant #2): reject it here rather than let a
    // nonsense level reach the book and get drawn.
    if (raw < 0) {
        status = ParseStatus::BadShape;
        return false;
    }
    if (raw % spec.qty_step != 0) {
        status = ParseStatus::BadShape;
        return false;
    }
    out = raw / spec.qty_step;
    return true;
}

// Decode one side of a book/snapshot frame. Levels arrive best-first and are
// kept in that order; anything past kMaxSnapshotLevels is dropped off the deep
// end (legal — depth beyond N is unknown, not zero) and flagged.
bool parse_side(const json& arr, const SymbolSpec& spec, BookLevel* dst,
                std::uint32_t& count, bool& truncated, ParseStatus& status) noexcept {
    if (!arr.is_array()) {
        status = ParseStatus::BadShape;
        return false;
    }
    count = 0;
    for (const json& lvl : arr) {
        if (!lvl.is_object()) {
            status = ParseStatus::BadShape;
            return false;
        }
        const auto px_it = lvl.find("price");
        const auto qty_it = lvl.find("qty");
        if (px_it == lvl.end() || qty_it == lvl.end()) {
            status = ParseStatus::BadShape;
            return false;
        }
        BookLevel level{};
        if (!price_to_ticks(*px_it, spec, level.px, status)) { return false; }
        if (!qty_to_steps(*qty_it, spec, level.qty, status)) { return false; }

        if (count < kMaxSnapshotLevels) {
            dst[count++] = level;
        } else {
            truncated = true;
        }
    }
    return true;
}

// The body. Returns a status and may leave `out` half-written; the wrapper
// below is what guarantees the postcondition, so nothing in here has to
// remember to.
ParseStatus parse_into(std::string_view text, const SymbolSpec& spec,
                       AnvilFrame& out) noexcept {
    // A spec the adapter cannot decode against is a shape error like any other,
    // and checking it here costs one compare per frame instead of one per level
    // (~250 at Anvil's live depth). The declared constants assert it at compile
    // time; this catches one built at runtime from trace metadata.
    if (!spec.valid()) { return ParseStatus::BadShape; }

    // Iterator pair (not the string_view overload) so this compiles against any
    // nlohmann 3.x, and allow_exceptions=false so nothing escapes a noexcept fn.
    const json doc = json::parse(text.begin(), text.end(), /*cb=*/nullptr,
                                 /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) { return ParseStatus::NotJson; }

    const std::string* type = nullptr;
    if (const auto type_it = doc.find("type"); type_it != doc.end()) {
        type = as_string(*type_it);
    }
    if (type == nullptr) { return ParseStatus::MissingType; }

    const FrameKind kind = kind_from_type(*type);

    // Decoded for diagnostics only; never used for ordering (ARCHITECTURE §4).
    if (const auto seq_it = doc.find("seq");
        seq_it != doc.end() && seq_it->is_number_integer()) {
        out.wire_seq = seq_it->get<std::int64_t>();
        out.has_wire_seq = true;
    }

    // `summary` is cross-ticker and carries no "ticker" field; every other kind
    // names the ticker it belongs to, and one that is not ours is dropped before
    // any payload work (a socket subscribes to one ticker, so this is a
    // belt-and-braces check, not an expected path).
    if (kind != FrameKind::Summary && kind != FrameKind::Unknown) {
        const auto tk_it = doc.find("ticker");
        if (tk_it == doc.end() || !tk_it->is_number_integer()) { return ParseStatus::BadShape; }
        const std::int64_t ticker = tk_it->get<std::int64_t>();
        if (ticker < 0) { return ParseStatus::BadShape; }
        out.ticker = static_cast<std::uint32_t>(ticker);
        out.has_ticker = true;
        if (out.ticker != spec.id) { return ParseStatus::OtherTicker; }
    }

    ParseStatus status = ParseStatus::Ok;
    switch (kind) {
        case FrameKind::Snapshot:
        case FrameKind::Book: {
            const auto bids_it = doc.find("bids");
            const auto asks_it = doc.find("asks");
            if (bids_it == doc.end() || asks_it == doc.end()) { return ParseStatus::BadShape; }
            if (!parse_side(*bids_it, spec, out.bids, out.bid_count, out.levels_truncated,
                            status) ||
                !parse_side(*asks_it, spec, out.asks, out.ask_count, out.levels_truncated,
                            status)) {
                return status;
            }
            break;
        }
        case FrameKind::Trade: {
            const auto px_it = doc.find("price");
            const auto qty_it = doc.find("qty");
            const auto aggr_it = doc.find("aggr");
            if (px_it == doc.end() || qty_it == doc.end() || aggr_it == doc.end()) {
                return ParseStatus::BadShape;
            }
            if (!price_to_ticks(*px_it, spec, out.trade_px, status) ||
                !qty_to_steps(*qty_it, spec, out.trade_qty, status)) {
                return status;
            }
            const std::string* aggr = as_string(*aggr_it);
            if (aggr == nullptr) { return ParseStatus::BadShape; }
            // "B"/"S" are the engine's AggrSide (protocol §1): the aggressor is
            // the incoming order, so B => a buy hit the offer.
            if (*aggr == "B") {
                out.aggressor = Side::Bid;
            } else if (*aggr == "S") {
                out.aggressor = Side::Ask;
            } else {
                return ParseStatus::BadShape;
            }
            break;
        }
        case FrameKind::Summary:
        case FrameKind::Unknown:
            break;  // no payload this milestone cares about
    }

    out.kind = kind;
    return ParseStatus::Ok;
}

}  // namespace

ParseStatus parse_anvil_frame(std::string_view text, const SymbolSpec& spec,
                              AnvilFrame& out) noexcept {
    // The declared postcondition (anvil_frame.hpp) in two lines, instead of a
    // hand-written `out.reset()` before each of ten failing returns — three of
    // which did not have one and were correct only because of the reset here.
    // The M3 streaming parser has to reproduce this contract; giving it one
    // place to honour rather than ten is the point.
    out.reset();
    const ParseStatus st = parse_into(text, spec, out);
    if (st != ParseStatus::Ok) { out.reset(); }
    return st;
}

}  // namespace depthcharge::anvil
