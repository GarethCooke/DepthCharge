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
// At M5 stage D-A1 the Binance build cleared the double-buffer floor by
// **3,628 bytes**, and at that margin one more field in the adapter removed the
// panel. **D-A2 widened it to 44,596 B** by moving `FramePipe`'s 64 KiB out of
// `.bss`, and the assertion is worth MORE rather than less for that: the margin
// is no longer the thing holding the panel up, so a regression would now be
// silent for a long time before it bit. It also now guards a second input — the
// reserve rose 80 -> 104 KiB in the same stage to cover a concurrent TLS
// session, and a future rise spends this margin without touching `engine/` at
// all.
//
// The failure it prevents is unchanged: `Panel::begin()` measures the free heap,
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
//     depthcharge            81,032 B      —                —
//     depthcharge-kraken     89,360 B      +8,328           +8,344   (16 B out)
//     depthcharge-binance   140,688 B      +59,656          +59,660  ( 4 B out)
//
// The residuals are pointer-width and alignment noise. **The absolute figures
// fell by 65,528 B across all three at D-A2** when `FramePipe`'s slabs left
// `.bss`; the DELTAS are what this derivation uses and they did not move, which
// is the point of expressing it as a difference. So
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
// at `Panel::begin()`, and **it is the only input here that is not compiler or
// linker output — so it is the one that goes stale, and it did, within one
// stage.**
//
//     2026-08-24, D-A1 build   176,828  177,040  177,048  177,236   spread 408
//     2026-08-28, D-A2 build   241,720                              <-- in use
//
// D-A2 moved it by 64,892 B, because `FramePipe`'s 65,536 B of slabs left
// `.bss` for the heap. **The old reading was not wrong; it was measured on a
// build that no longer exists** — which is precisely the failure mode
// ARCHITECTURE §9's 2026-08-27 row names, arriving one stage after that row was
// written, and it is why this constant is re-measured rather than adjusted.
//
// **Only one reading this time, and that is a real weakness of the figure.**
// D-A1 had four and could take the worst; this has one, so it carries no spread
// of its own and the 408 B from the earlier set is the only guide to how much
// it might move. It errs the right way — a HIGH reading makes the budget
// generous and the assertion lax, so the risk is a missed regression rather
// than a false alarm — but if this is ever tightened, take four readings first.
//
// **Note what this number is NOT.** The D-A1 brief carries 179,300, a single
// sample from 2026-08-20 taken on a third build again. If a bench evening reads
// a materially different `dma-internal free=` on the Anvil build, this constant
// is what has to be re-derived.
#pragma once

#include <cstddef>

#include "panel.hpp"
#include "venue_build.hpp"

namespace depthcharge::fw {

// The Anvil board's free internal DMA-capable heap at `Panel::begin()`.
//
// **MEASURED 2026-08-28 on the D-A2 build**, which is the only honest way to
// carry it: this is the one number in this file that is neither compiler nor
// linker output, and D-A2 moved it by returning `FramePipe`'s 65,536 B to the
// heap. The projection said 176,828 + 65,536 = 242,364 and the board read
// **241,720** — 644 B low, which is inside the run-to-run spread the four
// 2026-08-24 readings already showed (408 B). The projection was close enough
// to trust the model and not close enough to substitute for the reading, which
// is why the reading is what is written down.
inline constexpr std::size_t kAnvilFreeInternalAtPanelInit = 241'720;

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

// 241,720 + 8,400 - 106,496 - 32,000 = 111,624. Pinned so that a
// change to the reserve or to the panel ladder cannot move this bound silently:
// both are legitimate things to change, and both must be seen to change it.
// It was 71,308 at D-A1; D-A2 moved two of the three inputs in opposite
// directions — FramePipe returned 65,536 B of internal SRAM and the reserve took
// 24 KiB of it back to cover a second TLS session — and the pin is what makes
// that visible rather than a quiet net figure.
static_assert(kVenueInternalBudgetBytes == 111'624,
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
