// depthcharge/anvil/anvil_scaling.hpp — the Anvil wire semantics both parsers share.
//
// M3 Stage B links a SECOND implementation of parse_anvil_frame (streaming,
// allocation-free, target-bound) beside the M1 nlohmann reference. The two must
// agree byte for byte, and the place where a rewrite silently disagrees is not
// the JSON scanning — a mis-scan fails loudly — it is the *conversion*: a price
// that goes through a double, a qty that rounds, an aggressor letter mapped the
// other way round. Any of those moves a golden by one tick and looks like a
// market difference.
//
// So the conversions are not duplicated. They live here, `inline` in a header
// both TUs include, and each parser's only job is to hand over the bytes:
//
//   wire text ──► [parser: find the token] ──► [this header: convert] ──► AnvilFrame
//     (JSON)         nlohmann OR streaming        one implementation
//
// The M3 brief asked for scaling to be lifted out of the nlohmann TU into
// "shared adapter code". This is that lift, done by sharing the conversion
// rather than by moving the AnvilFrame boundary: the decoded frame still carries
// PriceTicks, exactly as anvil_frame.hpp has described it since M1, because
// moving the boundary would mean the frame carrying raw decimal tokens — which
// the nlohmann path cannot do (its DOM strings die with the parse, so it would
// have to copy every token into the frame) and which would rewrite the very
// adapter and postcondition test that are Stage B's acceptance gate. The risk
// the brief named is invariant #3 drift between two conversion paths; one
// conversion path removes it outright. See the M3 session log.
//
// Invariants: #1 (freestanding-safe standard headers only, host + xtensa),
// #3 (integer ticks; parse_scaled never touches a float), #7 (no allocation,
// no exceptions — everything here is noexcept and returns by out-parameter).
#pragma once

#include <cstdint>
#include <string_view>

#include "depthcharge/anvil/anvil_frame.hpp"
#include "depthcharge/decimal.hpp"
#include "depthcharge/feed_event.hpp"
#include "depthcharge/symbol.hpp"

namespace depthcharge::anvil {

// Wire "type" string -> FrameKind. `snapshot` and `book` stay distinct here so
// the adapter can count them separately; it maps both to a full-replace
// Snapshot event (anvil_adapter.hpp).
inline FrameKind kind_from_type(std::string_view type) noexcept {
    if (type == "book") { return FrameKind::Book; }
    if (type == "snapshot") { return FrameKind::Snapshot; }
    if (type == "trade") { return FrameKind::Trade; }
    if (type == "summary") { return FrameKind::Summary; }
    return FrameKind::Unknown;  // incl. the reserved "error" frame (protocol §3.4)
}

// A price token, already stripped of its JSON quotes and unescaped, converted to
// integer ticks at the declared scale.
//
// The only error mapping either parser is allowed to use: BadScale means the
// SymbolSpec itself is undecodable (a shape problem with our configuration, and
// unreachable once the caller has checked spec.valid()); everything else —
// empty, malformed, too many decimals, out of int64 range — is a *price* the
// declared scale cannot hold exactly, which ARCHITECTURE §4 requires be reported
// loudly instead of rounded.
inline bool price_text_to_ticks(std::string_view text, const SymbolSpec& spec,
                                PriceTicks& out, ParseStatus& status) noexcept {
    const DecimalParse p = parse_scaled(text, static_cast<int>(spec.price_decimals));
    if (!p.ok()) {
        status = (p.error == DecimalError::BadScale) ? ParseStatus::BadShape
                                                     : ParseStatus::BadPrice;
        return false;
    }
    out = p.value;
    return true;
}

// Anvil quotes quantities as whole units in a bare JSON integer, so this scales
// that integer up to the declared step: `out = raw x 10^qty_decimals`, which is
// the identity at Anvil's declared 0 and is what makes the field mean the same
// thing at both venues (symbol.hpp).
//
// A negative resting size or fill size is not a quantity: the adapter is the
// quarantine boundary (invariant #2), so it is rejected here rather than allowed
// to reach the book and be drawn. The spec is validated once per frame by the
// caller, not once per level (~250 of those at Anvil's live depth).
//
// THE OVERFLOW GUARD IS NOT DEFENSIVE PADDING. The predecessor of this function
// DIVIDED by an integer step, so it could not overflow and did not check; this
// one multiplies, and at a hypothetical qty_decimals of 8 an Anvil qty of 10^11
// would wrap int64 silently and draw a negative bar. Rejecting is the same
// answer this file gives every other wire value the declared scale cannot hold.
inline bool wire_qty_to_steps(std::int64_t raw, const SymbolSpec& spec, Qty& out,
                              ParseStatus& status) noexcept {
    if (raw < 0) {
        status = ParseStatus::BadShape;
        return false;
    }
    std::int64_t scaled = raw;
    for (std::int32_t k = 0; k < spec.qty_decimals; ++k) {
        if (scaled > INT64_MAX / 10) {
            status = ParseStatus::BadShape;
            return false;
        }
        scaled *= 10;
    }
    out = scaled;
    return true;
}

// "B"/"S" are the engine's AggrSide (protocol §1): the aggressor is the incoming
// order, so B => a buy hit the offer. Anything else is a shape error, not a
// guess.
inline bool aggressor_from_text(std::string_view text, Side& out) noexcept {
    if (text == "B") {
        out = Side::Bid;
        return true;
    }
    if (text == "S") {
        out = Side::Ask;
        return true;
    }
    return false;
}

}  // namespace depthcharge::anvil
