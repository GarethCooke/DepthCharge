// firmware/src/venue_budget.hpp — the venue's internal-SRAM footprint, bounded
// at COMPILE TIME so the panel is not lost at boot to a log line nobody reads.
//
// M5 stage D-A1 deliverable 5. This header exists rather than living in
// `panel.hpp` or `venue_build.hpp` because it is a relationship BETWEEN them
// and neither should own it: the panel does not know what an adapter is, and
// the venue does not know what a framebuffer costs. It has no runtime content
// at all — one constant, one function and one `static_assert` — and it is
// included from `main.cpp` for the same reason `dc_engine_target_check` is a
// build target: an assertion that is never compiled is not an assertion.
//
// ===========================================================================
// WHY A COMPILE-TIME BOUND AT ALL, WHEN THE REAL FIT IS A RUNTIME ONE
// ===========================================================================
//
// The Binance build clears the double-buffer floor by **3,248 bytes** (below).
// At that margin **one more field in the adapter removes the panel**, and the
// way it removes it is the problem: `Panel::begin()` measures the free heap,
// finds no rung fits, prints
//
//     "no colour depth fits in %u B — running WITHOUT a panel."
//
// and carries on. That is the correct runtime behaviour — the feed and the
// serial evidence are worth more than the ladder — but as a REGRESSION SIGNAL
// it is a line in a log on a board that may not be plugged into a monitor. The
// desk should hear about it first.
//
// **The floor itself cannot be a `static_assert`**, and this is not a
// limitation to be worked around: the budget depends on the free internal heap
// at `Panel::begin()`, which depends on Wi-Fi, TLS and the framework's own
// allocations, none of which a compiler can see. What IS knowable at compile
// time is the input this project controls — how much internal SRAM the venue's
// adapter occupies before the heap exists — and that is also the part that
// regresses, because it is the part anybody editing `engine/` can move.
//
// So: assert the input, not the outcome. This is the `sizeof(DisplaySnapshot)
// == 1168` pattern, and it exists for the same reason.
//
// ===========================================================================
// THE DERIVATION, BESIDE THE ASSERTION IT JUSTIFIES
// ===========================================================================
//
// The runtime condition the board must satisfy at `Panel::begin()` is
//
//     free_internal(venue) - kReserveInternalBytes >= panel_cost_bytes(3, true)
//
// `free_internal` is not known here, but its DIFFERENCE between venues is: the
// adapter is a member of `FeedTask g_feed` at namespace scope (`main.cpp`), so
// it is `.bss` — claimed before the heap exists — and every byte of it is a
// byte the heap never sees. Measured off the linker rather than assumed, which
// is what makes this arithmetic legitimate:
//
//     image                 static RAM     delta vs Anvil   sizeof(Adapter) delta
//     depthcharge           146,560 B      —                —
//     depthcharge-kraken    154,888 B      +8,328           +8,344   (16 B out)
//     depthcharge-binance   206,216 B      +59,656          +59,660  ( 4 B out)
//
// The residuals are pointer-width and alignment noise. So
//
//     free_internal(venue) ~= free_internal(anvil) - (A_venue - A_anvil)
//
// and substituting into the condition and solving for A_venue:
//
//     A_venue <= free_internal(anvil) + A_anvil
//                - kReserveInternalBytes - panel_cost_bytes(3, true)
//
// ===========================================================================
// THE MEASURED INPUT, AND WHY IT IS THE WORST READING AND NOT THE MEDIAN
// ===========================================================================
//
// `free_internal(anvil)` is the `dma-internal free=` figure `panel.cpp` prints
// at `Panel::begin()`. The four most recent Anvil runs — 2026-08-24, same
// firmware, same desk — read
//
//     176,828   177,040   177,048   177,236        spread 408 B
//
// **176,828 is used: the worst of the four, not the median.** This is a
// CEILING on a footprint, so the conservative direction is the lowest free
// heap, and the whole point of the constant is to fire before the board does.
// That is a different question from the one stage C's multiplier finding warns
// about — there, sizing a REQUIREMENT to the worst observed left a margin of
// 1.000x; here the worst observed is the input to a bound whose margin is
// stated separately below.
//
// **Note what this number is NOT.** The D-A1 brief carries 179,300, a single
// sample from 2026-08-20; the four readings above are more recent and lower.
// The figure is a board measurement and the only input here that is not from a
// linker or a compiler, so it is the one that can go stale. If a bench evening
// reads a materially different `dma-internal free=` on the Anvil build, this
// constant is what has to be re-derived.
#pragma once

#include <cstddef>

#include "panel.hpp"
#include "venue_build.hpp"

namespace depthcharge::fw {

// The Anvil board's free internal DMA-capable heap at `Panel::begin()`, worst
// of four runs on 2026-08-24. See the header note for provenance and staleness.
inline constexpr std::size_t kAnvilFreeInternalAtPanelInit = 176'828;

// `sizeof(AnvilAdapter)`, the footprint the measurement above was taken with.
// Spelled as a literal rather than read from `anvil::AnvilAdapter` so this
// header does not have to include an adapter the build may not compile — and
// so that changing Anvil's adapter cannot silently move a constant derived
// from a measurement taken before the change.
inline constexpr std::size_t kAnvilAdapterBytesAtMeasurement = 8'400;

// HOW MUCH INTERNAL SRAM AN ADAPTER OCCUPIES BEFORE THE HEAP EXISTS.
//
// `sizeof` is the whole answer, and it is worth saying why rather than leaving
// it to look like a simplification. The adapter is a sub-object of a
// namespace-scope `FeedTask`, so its `.bss` residency is exactly its size; a
// member that lives on the heap — `BinanceAdapter::buf_lvl_` is the only one
// today — contributes its POINTER here and its bytes somewhere this bound does
// not govern, which is precisely the distinction the bound is meant to capture.
//
// The template is not generality for its own sake: it names the quantity, so
// that a future member that is heap-backed reads as deliberately outside this
// figure instead of as an oversight.
template <typename Adapter>
constexpr std::size_t internal_resident_bytes() noexcept {
    return sizeof(Adapter);
}

// The ceiling, derived exactly as the header note sets out.
inline constexpr std::size_t kVenueInternalBudgetBytes =
    kAnvilFreeInternalAtPanelInit + kAnvilAdapterBytesAtMeasurement -
    kReserveInternalBytes -
    static_cast<std::size_t>(panel_cost_bytes(kMinDoubleBufferedDepth, /*double_buffered=*/true));

// 176,828 + 8,400 - 81,920 - 32,000 = 71,308. Pinned so that a change to the
// reserve or to the panel ladder cannot move this bound silently: both are
// legitimate things to change, and both must be seen to change it.
static_assert(kVenueInternalBudgetBytes == 71'308,
              "the venue budget moved — check kReserveInternalBytes and the d3 rung, "
              "and re-read the derivation in this file's header before adjusting");

// ===========================================================================
// THE ASSERTION. READ THIS BEFORE RAISING ANY NUMBER ABOVE.
// ===========================================================================
//
// **Firing this is a DECISION, and it is not a licence to raise the constant.**
// The number above is not a policy: it is what the board measured, minus what
// the feed is owed, minus what a double-buffered panel costs. Raising it does
// not create room — it silently spends the panel's second buffer, and the
// board will report that as a single-buffered ladder that tears on a book
// redrawing 13 times a second.
//
// When it fires, the honest responses are, in order of preference:
//
//   1. Put the new state somewhere else. `buf_lvl_` went to PSRAM at D-A1 for
//      exactly this reason, and ARCHITECTURE §5's "the window lives in internal
//      SRAM, the tail in PSRAM" is the test for whether a member may follow it.
//   2. Re-derive the bound against a fresh board measurement, if and only if
//      the free-internal figure has genuinely moved. Then change
//      `kAnvilFreeInternalAtPanelInit` and say what run it came from.
//   3. Accept a shallower colour depth deliberately, by lowering the rung this
//      is derived against — which is a panel decision and belongs to a bench
//      sitting, not to whoever tripped the assertion.
//
// What is NOT on the list is editing `71'308`.
static_assert(internal_resident_bytes<venue::Adapter>() <= kVenueInternalBudgetBytes,
              "this venue's adapter no longer leaves room for a double-buffered panel: "
              "the board would boot single-buffered or with no panel at all. "
              "See venue_budget.hpp — firing this is a decision, not a number to raise.");

}  // namespace depthcharge::fw
