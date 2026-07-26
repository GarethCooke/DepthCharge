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
};

}  // namespace depthcharge
