// depthcharge/symbol.hpp — the per-symbol scaling metadata the adapters need.
//
// ARCHITECTURE §4: prices and quantities cross the boundary as integers scaled
// by a per-symbol tick size / qty step "supplied in venue symbol metadata".
// Anvil supplies none — its protocol has no tick-size field anywhere (M1 known
// unknown; see harness/replay/NOTES.md). So DepthCharge *declares* the scale
// here and the adapter *verifies* every wire price is exactly representable at
// it (depthcharge::parse_scaled returns TooManyDecimals otherwise). Declare and
// verify, never guess and round: a wrong scale must fail loudly, not quietly
// corrupt a ladder.
#pragma once

#include <cstdint>

#include "depthcharge/decimal.hpp"
#include "depthcharge/feed_event.hpp"

namespace depthcharge {

// Enough to identify what is on the panel; not a symbology system.
struct SymbolSpec {
    // Venue-native symbol id (Anvil ticker; later venues map their string pair
    // to a small id at config time so the engine stays integer-only).
    std::uint32_t id = 0;

    // Wire decimal places => tick size 10^-price_decimals. PriceTicks is the
    // wire price scaled by 10^price_decimals.
    std::int32_t price_decimals = 0;

    // Wire decimal places for QUANTITY => qty step 10^-qty_decimals. Qty is the
    // wire quantity scaled by 10^qty_decimals, exactly as PriceTicks is the wire
    // price scaled by 10^price_decimals.
    //
    // THIS FIELD WAS `Qty qty_step = 1` UNTIL M4 STAGE B1, AND THE REPLACEMENT
    // IS NOT A RENAME. The old field meant "wire quantity units per Qty step"
    // and divided: `out = raw / qty_step`. That shape can only express a step
    // COARSER than the wire unit, and it was never exercised, because Anvil is
    // the only venue consumed before now and quotes whole units — so every
    // scaling path in the tree was correct only because this value was 1.
    // Kraken's step is `qty_increment: 1e-08`, which is finer than the wire unit
    // and is not representable in the old field AT ALL: there is no integer n
    // for which dividing by n scales 0.65540712 to an exact integer. A divisor
    // field would have had to become a multiplier, and a multiplier that is
    // always a power of ten is a decimal scale wearing a disguise — so it is
    // declared as one, and the two scales now read the same way and are applied
    // by the same function (depthcharge::parse_scaled).
    //
    // What the old field's exactness check bought is not lost, it moves: the
    // `raw % qty_step != 0` test rejected a wire quantity the declared step
    // could not hold, and `parse_scaled`'s `TooManyDecimals` rejects exactly the
    // same class of disagreement — loudly, never by rounding (ARCHITECTURE §4).
    std::int32_t qty_decimals = 0;

    // A spec the adapter cannot decode against: a scale outside parse_scaled's
    // range rejects every price or quantity. Deliberately a query rather than a
    // constructor precondition — SymbolSpec stays an aggregate (so it stays
    // usable as an `inline constexpr` constant and keeps DisplaySnapshot
    // trivially copyable), and the declared constants assert this at compile
    // time while the adapter checks a runtime-supplied one once per frame.
    constexpr bool valid() const noexcept {
        return price_decimals >= 0 && price_decimals <= kMaxDecimals &&
               qty_decimals >= 0 && qty_decimals <= kMaxDecimals;
    }
};

}  // namespace depthcharge
