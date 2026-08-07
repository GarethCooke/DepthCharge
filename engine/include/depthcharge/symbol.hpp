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

    // Wire quantity units per Qty step. Anvil quotes whole units, so 1.
    Qty qty_step = 1;

    // A spec the adapter cannot decode against: a non-positive qty_step would
    // divide by zero or invert every size, and a scale outside parse_scaled's
    // range rejects every price. Deliberately a query rather than a constructor
    // precondition — SymbolSpec stays an aggregate (so it stays usable as an
    // `inline constexpr` constant and keeps DisplaySnapshot trivially
    // copyable), and the declared constants assert this at compile time while
    // the adapter checks a runtime-supplied one once per frame.
    constexpr bool valid() const noexcept {
        return qty_step > 0 && price_decimals >= 0 && price_decimals <= kMaxDecimals;
    }
};

}  // namespace depthcharge
