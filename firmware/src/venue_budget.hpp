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
// panel. D-A2 widened it, by moving `FramePipe`'s 64 KiB out of `.bss`, and the
// assertion is worth MORE rather than less for that: the margin is no longer
// the thing holding the panel up, so a regression would now be silent for a long
// time before it bit. It also now guards a second input — the reserve rose
// 80 -> 104 KiB in the same stage to cover a concurrent TLS session, and a
// future rise spends this margin without touching `engine/` at all.
//
// ---------------------------------------------------------------------------
// TWO MARGINS LIVED IN THAT PARAGRAPH AND ONLY ONE OF THEM WAS EVER MEASURED
// (corrected M5 stage D-A4, which the brief sent to "reconcile or name" it)
// ---------------------------------------------------------------------------
//
// The sentence above used to end *"D-A2 widened it to 44,596 B"*, and D-A4's
// brief carried a second figure for what reads as the same quantity — *"~43.6
// KiB headroom under the bound"*. Neither was right, and they were not even
// about the same measurement:
//
//   **3,628 B is a BOARD READING and it is sound.** D-A1's acceptance printed
//   `dma-internal free=117548 … reserve=81920 budget=35628`, and 35,628 - 32,000
//   is 3,628 (`M5-stage-D-A1-…md`, reading 2). It is the RUNTIME margin.
//
//   **44,596 B was never measured.** No board reading in the tree produces it.
//   `panel.hpp`'s reserve note forecasts *"the Binance build's budget goes
//   35,628 -> ~76,588"*, and 76,588 - 32,000 = 44,588 — eight bytes away, and
//   itself flagged there as **"STILL A FORECAST, AND SAID SO"**. So the figure
//   was a forecast quoted as an outcome, in a file whose whole subject is a
//   number going stale.
//
//   **43,564 B (the brief's "~43.6 KiB") is the right arithmetic on a stale
//   input.** It is 111,624 - 68,060, and 68,060 is `sizeof(BinanceAdapter)` as
//   D-A1 measured it — superseded by D-A2 in the same stage that produced
//   44,596, when `buf_` (64 x 32 = 2,048 B) followed `buf_lvl_` to the heap.
//
// The two margins ARE the same quantity by construction — that is what the
// derivation below establishes — but only when the free-internal reading, the
// linker delta and the `sizeof` are all of one vintage. Here the reading was
// D-A2's, the delta D-A1's and the `sizeof` D-A1's, so the identity broke and
// nothing noticed, twice, in two documents.
//
// **THE FIX IS NOT A THIRD NUMBER IN A COMMENT.** The headroom is now a
// constant this file computes and the build checks —
// `kVenueInternalHeadroomBytes`, pinned below at the figure the compiler
// actually produces. A margin quoted in prose has now been wrong twice in three
// stages; a margin the linker recomputes on every build cannot be.
//
// Also corrected here: the linker table below is D-A1's and its Binance row is
// **2,048 B stale for the same reason**. Left standing, with this note, because
// it is the evidence for the derivation rather than an input to it — the
// constant it feeds is `kAnvilAdapterBytesAtMeasurement`, which is Anvil's and
// did not move.
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

// `panel_budget.hpp` AND NOT `panel.hpp`, WHICH IS THE WHOLE OF THE M5
// CLOSE-OUT'S CHANGE TO THIS FILE.
//
// This header used to include `panel.hpp` for three names — `kReserveInternalBytes`,
// `kMinDoubleBufferedDepth` and `panel_cost_bytes` — and `panel.hpp` includes
// the HUB75 driver, so **this file was not host-compilable at all**. Its two
// `static_assert`s were therefore checked only by `pio run`, and only for the
// arm that build selects: a commit changing the venue budget went green on all
// 52 ctest tests whatever it said. Recorded at M5 stage D-A4 (ARCHITECTURE §9,
// 2026-08-30) and closed here by moving the three names to `panel_budget.hpp`,
// which was already ESP-IDF-free and already host-tested.
//
// **What that bought, exactly:** `test_venue_build.cpp` includes this header, so
// `dc_tests`, `dc_tests_kraken` and `dc_tests_binance` now compile the budget
// assertion for ALL THREE venues on every host build, where `pio` could only
// ever check one. What it did NOT buy is the headroom pin at the foot of this
// file — that is a target `sizeof` and the host's differs by 8 bytes, so it
// stays behind a target guard and stays a `pio` obligation. Saying which half
// moved is the point: a fix that quietly left the second half uncovered while
// reading as "now host-compilable" would be the same defect one layer up.
#include "panel_budget.hpp"
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
// What is NOT on the list is editing `111'624`. (This line said `71'308` until
// M5 stage D-A4 — the D-A1 value, left behind when D-A2 re-derived the constant
// two paragraphs above it. A note naming the wrong number is worse than none,
// because it reads as a cross-check and is not one.)
static_assert(internal_resident_bytes<venue::Adapter>() <= kVenueInternalBudgetBytes,
              "this venue's adapter no longer leaves room for a double-buffered panel: "
              "the board would boot single-buffered or with no panel at all. "
              "See venue_budget.hpp — firing this is a decision, not a number to raise.");

// ===========================================================================
// AND HOW MUCH IS LEFT — PINNED, BECAUSE PROSE HAS ALREADY GOT THIS WRONG TWICE
// ===========================================================================
//
// M5 stage D-A4. The `static_assert` above is a CEILING: it catches an adapter
// that has spent the whole 45 KiB and says nothing about one that has spent 40.
// The headroom is the number every stage since D-A1 has actually quoted — into
// briefs, into this file's own header, into ROADMAP — and it is the number that
// went stale, because nothing recomputed it.
//
// This does. It is `sizeof` minus two constants, so the compiler produces it on
// every build, and the pin below makes any movement a thing the desk is told
// about rather than a thing a later reader has to re-derive. Same pattern, same
// reason, as `sizeof(DisplaySnapshot) == 1168`.
inline constexpr std::size_t kVenueInternalHeadroomBytes =
    kVenueInternalBudgetBytes - internal_resident_bytes<venue::Adapter>();

// ALL THREE ARMS, BOTH TOOLCHAINS — SIX PINS, AND THE OMISSION OF FIVE OF THEM
// WAS A JUSTIFICATION THIS VERY CHANGE DISCHARGED.
//
// This block read *"BINANCE ONLY, AND THE OMISSION OF THE OTHER TWO IS
// DELIBERATE … M5 stage D-A4 built and verified `-e depthcharge-binance`. It did
// not build the Anvil or Kraken arms, and a pin on an arm this session never
// compiled would be a number nobody has seen the compiler produce."* **That
// reasoning was right and it expired at the M5 close-out**, which built all three
// arms and read all three values out of the compiler by the same technique — an
// incomplete template instantiated on the expression — so there is no longer an
// arm whose figure nobody has seen. Recorded rather than quietly replaced,
// because the rule the old paragraph states is the correct rule and the only
// thing that changed is that the evidence now exists.
//
//     arm       target (xtensa GCC 8.4)      host (MinGW GCC)      difference
//     Anvil            103,224                   103,224                0
//     Kraken            94,896                    94,880              +16 host
//     Binance           45,584                    45,568              +16 host
//
// Anvil's adapter holds no pointer-width member at all, so its two figures are
// the same number and the pin is one constant checked twice. The other two
// differ by 16 bytes each, measured rather than derived; Binance's account is
// spelled out below, and **Kraken's matching 16 is recorded as measured and NOT
// explained**, because attributing it without looking would be the same error
// this file caught in its own prose one paragraph later.
//
// **AND SIX PINS RATHER THAN ONE CLOSES A SILENT SKIP.** With only the Binance
// pair, `#if DC_VENUE == DC_VENUE_BINANCE` was itself a construct that vanishes
// quietly: drop `-D DC_VENUE=3` and `venue_build.hpp` defaults to Anvil, both
// blocks are skipped, and the build is green with no headroom check at all. Every
// arm having a pin means there is no configuration in which the check is absent
// — which is the same argument as `__XTENSA__` below, applied to the other
// conjunct.
//
// **66,040 B on xtensa, measured 2026-09-06** by instantiating an incomplete
// template on `internal_resident_bytes<venue::Adapter>()` under
// `pio run -e depthcharge-binance` and reading the value out of the diagnostic.
// It is NOT the host figure, and **the host figure this file used to quote was
// WRONG — which is the first thing the M5 close-out's host pin caught, on its
// first compile.**
//
// This paragraph read *"`dc_tests` reports 66,048, because two `std::unique_ptr`
// members are 8 bytes there and 4 here"*. The host actually reports **66,056**,
// so the gap is **16 bytes and not 8**, and the two `unique_ptr`s account for
// only half of it. The other half is `SymbolConfig::wire_symbol`, a
// `std::string_view` — 16 bytes on a 64-bit host, 8 on xtensa — carried by value
// in `cfg_`. **Three members holding four pointer-widths, named as two members
// holding two.** The full account is beside the host pin at the foot of this file.
//
// Neither number was ever checked by anything, which is the whole point: a
// figure written into a comment inside the header whose subject is a bound
// nothing compiles is as unverified as the bound. The host pin below is what
// turns the sentence into a check, and it fired the moment it existed.
//
// **AND THAT 16-BYTE DIFFERENCE IS WHY THE TARGET PIN, ALONE IN THIS FILE, IS
// STILL TARGET-ONLY.** The parenthesis here used to continue *"no host test
// reaches this file at all … a commit touching this file is unverified by a
// green ctest"*, and the close-out fixed that half: the header includes
// `panel_budget.hpp` rather than `panel.hpp` and `test_venue_build.cpp` compiles
// it on all three venue arms. The BUDGET assertion above is now a ctest
// obligation. The headroom pin cannot be, because it pins a number the host
// cannot produce — so it is guarded on the target and remains a `pio run`
// obligation, restated here rather than left for a reader to infer from a build
// that silently skipped it, and it now has a host twin that is checked in the
// normal loop.
//
// The stage moved it by **+32 B exactly** — four `std::uint64_t` counters added
// to `BinanceAdapter::Stats`: three for the re-seed mechanism, and
// `diffs_inside_baseline` for the venue-procedure clause review found missing.
// **Both ends of that are measured rather than derived** — 66,008 B at `master`
// and 66,040 B here, each read out of the compiler by instantiating an
// incomplete template on the expression, in a detached worktree for the first.
// This pin is what turned the fourth counter into a decision: it fired, and the
// number below was re-measured rather than adjusted to fit. The mechanism's own
// state is one `bool` and it cost nothing, landing in padding the object already
// carried; the 8 KiB + 512 KiB it runs on is `buf_`/`buf_lvl_`, which have been
// in PSRAM since D-A2. That is the invariant #7 statement in full: **no
// allocation at all, at any point, including construction — only the access
// pattern of an existing block changed.**
// `__XTENSA__` is the target discriminator and it is the compiler's own, not a
// build-system define this project could forget to pass: the host toolchain
// cannot define it and the xtensa one always does. A pin that silently vanished
// because a `-D` went missing would be the wave-through this whole file is about.
#if DC_VENUE == DC_VENUE_BINANCE && defined(__XTENSA__)
static_assert(kVenueInternalHeadroomBytes == 45'584,
              "the Binance adapter's internal footprint moved. That is allowed and it is a "
              "DECISION: re-measure under `pio run -e depthcharge-binance`, say what moved "
              "it and why, and update this pin in the same commit. Do not delete it — the "
              "margin it guards has been quoted wrongly in two documents already.");
#endif

// AND THE HOST FIGURE IS PINNED TOO, RATHER THAN LEFT UNCHECKED BECAUSE IT IS
// "not the real one". It sits 16 bytes below the target's headroom (the adapter
// being 16 bytes larger) for a reason that is known, stated and now measured —
// and the arithmetic is spelled out because the first draft of this comment got
// it wrong and review caught it:
//
//     buf_       std::unique_ptr    ONE pointer-width    8 host / 4 target  = +4
//     buf_lvl_   std::unique_ptr    ONE pointer-width    8 host / 4 target  = +4
//     cfg_.wire_symbol
//                std::string_view   TWO pointer-widths   16 host / 8 target = +8
//                                                                    total  = +16
//
// **Three members, FOUR pointer-widths.** The draft said *"each 8 bytes here and
// 4 there"* of all three, which is 12 and not the 16 the two pins actually
// differ by — a `string_view` is a pointer AND a size. A comment whose own
// arithmetic does not reproduce the constants it sits beside is the thing this
// file is about, arriving in the paragraph written to explain it.
//
// **This pin is not decoration — it is the check that found the wrong figure in
// the paragraph above.** The first version of it was written as 45,576 from that
// paragraph's own arithmetic and failed on the first compile at 45,568. A
// derived quantity with a stated derivation and no check is exactly the shape
// this file's whole subject is about; that it happened INSIDE this file is worth
// the four lines it costs.
//
// If the two ever move apart by anything other than 16, either the adapter
// changed or the account of the difference did, and both are decisions.
#if DC_VENUE == DC_VENUE_BINANCE && !defined(__XTENSA__)
static_assert(kVenueInternalHeadroomBytes == 45'568,
              "the Binance adapter's HOST footprint moved. The target pin above is the "
              "shipping one; this is its host twin, 16 bytes apart because three members "
              "hold four pointer-widths between them (two unique_ptr and one string_view, "
              "which is a pointer AND a size). Re-measure both before adjusting either — "
              "a change to one alone means the 16 is no longer the whole story.");
#endif

}  // namespace depthcharge::fw
