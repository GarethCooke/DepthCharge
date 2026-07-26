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
    if (!node.is_string()) {
        status = ParseStatus::BadShape;
        return false;
    }
    const std::string* s = node.get_ptr<const std::string*>();
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
bool qty_to_steps(const json& node, const SymbolSpec& spec, Qty& out,
                  ParseStatus& status) noexcept {
    if (!node.is_number_integer() || spec.qty_step <= 0) {
        status = ParseStatus::BadShape;
        return false;
    }
    const std::int64_t raw = node.get<std::int64_t>();
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

}  // namespace

ParseStatus parse_anvil_frame(std::string_view text, const SymbolSpec& spec,
                              AnvilFrame& out) noexcept {
    out.reset();

    // Iterator pair (not the string_view overload) so this compiles against any
    // nlohmann 3.x, and allow_exceptions=false so nothing escapes a noexcept fn.
    const json doc = json::parse(text.begin(), text.end(), /*cb=*/nullptr,
                                 /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) { return ParseStatus::NotJson; }

    const auto type_it = doc.find("type");
    if (type_it == doc.end() || !type_it->is_string()) { return ParseStatus::MissingType; }
    const std::string* type = type_it->get_ptr<const std::string*>();
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
        if (out.ticker != spec.id) {
            out.reset();
            return ParseStatus::OtherTicker;
        }
    }

    ParseStatus status = ParseStatus::Ok;
    switch (kind) {
        case FrameKind::Snapshot:
        case FrameKind::Book: {
            const auto bids_it = doc.find("bids");
            const auto asks_it = doc.find("asks");
            if (bids_it == doc.end() || asks_it == doc.end()) {
                out.reset();
                return ParseStatus::BadShape;
            }
            if (!parse_side(*bids_it, spec, out.bids, out.bid_count, out.levels_truncated,
                            status) ||
                !parse_side(*asks_it, spec, out.asks, out.ask_count, out.levels_truncated,
                            status)) {
                out.reset();
                return status;
            }
            break;
        }
        case FrameKind::Trade: {
            const auto px_it = doc.find("price");
            const auto qty_it = doc.find("qty");
            const auto aggr_it = doc.find("aggr");
            if (px_it == doc.end() || qty_it == doc.end() || aggr_it == doc.end() ||
                !aggr_it->is_string()) {
                out.reset();
                return ParseStatus::BadShape;
            }
            if (!price_to_ticks(*px_it, spec, out.trade_px, status) ||
                !qty_to_steps(*qty_it, spec, out.trade_qty, status)) {
                out.reset();
                return status;
            }
            const std::string* aggr = aggr_it->get_ptr<const std::string*>();
            if (aggr == nullptr) {
                out.reset();
                return ParseStatus::BadShape;
            }
            // "B"/"S" are the engine's AggrSide (protocol §1): the aggressor is
            // the incoming order, so B => a buy hit the offer.
            if (*aggr == "B") {
                out.aggressor = Side::Bid;
            } else if (*aggr == "S") {
                out.aggressor = Side::Ask;
            } else {
                out.reset();
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

}  // namespace depthcharge::anvil
