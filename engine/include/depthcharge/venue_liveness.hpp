// engine/include/depthcharge/venue_liveness.hpp — ONE HOME FOR THE THREE
// LIVENESS POLICIES, because two homes is how the board and the harness came to
// disagree about what a green clock means.
//
// WHY THIS FILE EXISTS AT ALL (M5 stage D-A3, deliverable 2). Stage C derived a
// per-venue liveness policy and put it in `harness/include/dc_harness/venue.hpp`
// — the host-side venue table. `firmware/` cannot include `dc_harness/`, so the
// board never received it: `firmware/src/liveness_watchdog.hpp` default-
// constructed its `LivenessClock` and ran the SHIPPING defaults (multiple 4.0,
// ceiling 30,000 ms) while every document quoted the derived Binance threshold
// of 39,927.94 ms. Both statements were true of different objects, which is the
// worst shape a number can be in.
//
// The fix is not to copy the table into `firmware/`. A copied constant is one
// edit away from disagreeing again, and the disagreement is silent — the board
// greys on a threshold nobody reads and the harness asserts one nobody runs. So
// the policies live HERE, in `engine/`, which both sides already include, and
// the harness table and the firmware build both point at these names.
//
// WHAT A POLICY IS NOT. It is not a threshold. `LivenessClock` derives the
// threshold from the venue's own measured median inter-arrival; a policy carries
// only the MULTIPLE that median is taken against and the bounds that multiple is
// clamped into. That is the 2026-08-26 ruling's whole point: an absolute
// duration in a venue-agnostic object is a per-venue multiple, so the multiple
// is what transfers between venues and the duration is what does not.
#ifndef DEPTHCHARGE_VENUE_LIVENESS_HPP
#define DEPTHCHARGE_VENUE_LIVENESS_HPP

#include <depthcharge/liveness_clock.hpp>

namespace depthcharge::venue_liveness {

// ANVIL — the shipping defaults, and it is a statement rather than an omission.
//
// The `summary` record is emitted by the application that publishes the book, so
// *the feed is alive* and *the socket is alive* coincide here and the 2026-08-17
// ruling never had to separate them. Its worst healthy inter-arrival is 1.937x
// its own median, and 4.0 clears that by 2.07x.
inline constexpr LivenessPolicy kAnvil{};

// KRAKEN — also the defaults, for the same reason: `heartbeat` comes from the
// publisher, and 4.0 clears its worst healthy multiple with the same margin.
inline constexpr LivenessPolicy kKraken{};

// BINANCE — the one venue that is not the default, derived at M5 stage C.
//
// THE MULTIPLE IS 2.0 AND IT IS DERIVED, NOT PREFERRED. `liveness_clock.hpp`'s
// own rule is *the venue's worst HEALTHY inter-arrival expressed as a multiple of
// that venue's median, times ~2 of margin*. This signal measured **1.005x** over
// the calibration capture's ten intervals and 1.01x over stage A's twenty-three
// — the tightest of the three venues, because it is a metronome inside the
// WebSocket layer rather than a publisher sharing a queue with the book. The
// same derivation that gives Anvil 4.0 gives this 2.0. Inheriting Anvil's 4.0
// here would not buy safety; it would buy forty more seconds of frozen ladder
// for margin this signal's jitter does not need.
//
// THE CEILING IS 60,000 ms BECAUSE IT HAS TO CLEAR THE THRESHOLD. 2.0 x the
// measured 19,963.97 ms median is 39,927.94 ms, and a ceiling below that is not
// a ceiling — it is the threshold, which is exactly the defect stage C found:
// one 30,000 ms ceiling is 60x Anvil's median, 30x Kraken's and **1.50x** this
// one's. 60,000 admits a cadence 50% slower than measured before it binds.
//
// WHAT THIS COSTS, STATED: `uncalibrated_ms()` IS the ceiling, so this venue's
// pre-calibration threshold is 60 s where the default is 30 s, for the 159.7 s
// that `kMinSamples = 8` takes at a 20 s cadence — on every connection, and the
// board reconnects. Whether that window matters on the board is D-C's to say.
inline constexpr LivenessPolicy kBinance{/*multiple=*/2.0,
                                         /*floor_ms=*/kThresholdFloorMs,
                                         /*ceiling_ms=*/60000.0};

// --- THE PROOF THAT ROUTING A POLICY MOVES NOTHING AT TWO OF THREE VENUES ----
//
// Stage C's claim was that Anvil and Kraken *"do not move at all, by
// construction"* — and the construction was that `firmware/` passed nothing, so
// there was nothing to get wrong. **Deliverable 2 ends that construction**: the
// firmware now passes a policy at every venue, so "unchanged" stops being
// structural and has to be CHECKED. These assertions are that check, and they
// fail the build rather than a review if either venue is ever quietly given a
// policy of its own.
static_assert(kAnvil.multiple == kThresholdMultiple &&
                  kAnvil.floor_ms == kThresholdFloorMs &&
                  kAnvil.ceiling_ms == kThresholdCeilingMs,
              "Anvil must remain the shipping default; stage C's no-movement claim "
              "is only true while this holds");
static_assert(kKraken.multiple == kThresholdMultiple &&
                  kKraken.floor_ms == kThresholdFloorMs &&
                  kKraken.ceiling_ms == kThresholdCeilingMs,
              "Kraken must remain the shipping default; stage C's no-movement claim "
              "is only true while this holds");

// And the Binance policy's own bound, which is the one that would silently undo
// the derivation: a ceiling at or below the derived threshold reinstates the
// clamp stage C removed, and the panel would grey at the ceiling while every
// document said 39,927.94 ms.
static_assert(kBinance.ceiling_ms > 2.0 * 19963.97,
              "the Binance ceiling must clear its own derived threshold, or the "
              "clamp is the threshold again -- the exact defect M5 stage C fixed");

}  // namespace depthcharge::venue_liveness

#endif  // DEPTHCHARGE_VENUE_LIVENESS_HPP
