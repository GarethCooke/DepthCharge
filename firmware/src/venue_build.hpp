// firmware/src/venue_build.hpp — ONE VENUE PER BUILD, chosen at compile time.
//
// M4 stage D item A3, implementing the ARCHITECTURE §9 row of 2026-08-17:
//
//     One venue per build until M7. Compile-time constant, default stays Anvil.
//     The runtime toggle is M7's encoder, and M7 depends on M4 and M6.
//
// AND THE ROW'S REASON IS WORTH REPEATING HERE, because "just make it a runtime
// flag" is the obvious suggestion and it is wrong on this part: a runtime venue
// switch is not one switch. It is two adapters live in one binary (8.4 KiB and
// 9.6 KiB of decoded-frame buffer, both resident), two `SymbolSpec`s, two
// declared thresholds, two book depths and a ladder whose height changes under
// it — on a device where invariant #7 forbids the allocation that would make any
// of that comfortable. A build flag costs a rebuild and nothing else.
//
// THE SHAPE IS THE ONE `DC_WS_PING` ESTABLISHED, three times over now
// (`DC_WS_PING`, `DC_WIFI_POWER_SAVE`, `DC_IDLE_PROBE`): an `#ifndef` default
// in a firmware header so a bare compile is the shipping configuration, a named
// PlatformIO environment supplying the `-D`, and a second host link
// configuration in `CMakeLists.txt` so the other arm's branch is reachable from
// ctest instead of being proved only by a flash. `dc_tests_kraken` is that
// configuration.
//
// WHAT IS AND IS NOT VENUE-SHAPED. Everything below is a type, a constant or a
// one-line accessor; there is no venue logic here, because venue logic lives in
// `engine/` where the host suite can reach it. The firmware's own venue
// knowledge is exactly this: which adapter type, which endpoint, which counter
// counts the liveness signal, and whether the transport has a subscription to
// manage at all. That list is short on purpose — anything longer would be
// venue behaviour leaking out of `engine/` past invariant #2.
#pragma once

#define DC_VENUE_ANVIL  1
#define DC_VENUE_KRAKEN 2

#ifndef DC_VENUE
#define DC_VENUE DC_VENUE_ANVIL
#endif

#if DC_VENUE != DC_VENUE_ANVIL && DC_VENUE != DC_VENUE_KRAKEN
#error "DC_VENUE must be DC_VENUE_ANVIL or DC_VENUE_KRAKEN"
#endif

// THE PREDICATE THE PREPROCESSOR BRANCHES ON, DEFINED ONCE.
//
// Review found the five sites that actually decide behaviour — the resync
// hand-off, `last_wire_seq`, `maybe_subscribe`, the send-path bound and the
// adapter log lines — all spelling the question as `#if DC_VENUE ==
// DC_VENUE_KRAKEN`, while `kHasSubscription` below, whose comment claims to be
// "what the transport branches on", governed nothing but a test. A third venue
// with a subscription would set the constant, pass the test, and connect to a
// socket nobody ever subscribes on.
//
// A macro rather than the `constexpr bool` because the branches must be
// PREPROCESSED: `if constexpr` outside a template still instantiates the
// discarded arm, so an Anvil build would have to compile
// `adapter_.resync_wanted()` against an adapter that has no such member. The
// `static_assert` at the bottom of this file ties the two spellings together so
// they cannot separate.
#if DC_VENUE == DC_VENUE_KRAKEN
#define DC_VENUE_HAS_SUBSCRIPTION 1
#else
#define DC_VENUE_HAS_SUBSCRIPTION 0
#endif

#include <cstdint>
#include <string_view>

#include <depthcharge/symbol.hpp>

#if DC_VENUE == DC_VENUE_KRAKEN
#include <depthcharge/kraken/kraken_adapter.hpp>
#include <depthcharge/kraken/kraken_checksum.hpp>
#include "kraken_endpoint.hpp"
#include "kraken_root_ca.hpp"
#else
#include <depthcharge/anvil/anvil_adapter.hpp>
#include "anvil_endpoint.hpp"
#include "anvil_root_ca.hpp"
#endif

namespace depthcharge::fw::venue {

#if DC_VENUE == DC_VENUE_KRAKEN

using Adapter = kraken::KrakenAdapter;
using ParseStatus = kraken::ParseStatus;

inline constexpr std::string_view kName = "kraken";

// The endpoint, aliased rather than re-spelled: two spellings of one authority
// is how the diag client and the firmware drifted at A7.
inline constexpr const char* kHost = kKrakenHost;
inline constexpr const char* kPath = kKrakenPath;
inline constexpr std::uint16_t kPort = kKrakenPort;
inline constexpr const char* kPortText = kKrakenPortText;
inline constexpr const char* kRootCaPem = kKrakenRootCaPem;
// esp-tls wants a length, and the PEM form counts the NUL. `sizeof` cannot
// travel through the `const char*` alias above, so the count is taken here
// where the array itself is still in scope.
inline constexpr std::size_t kRootCaPemBytes = sizeof(kKrakenRootCaPem);

// A SUBSCRIPTION THE TRANSPORT MUST MANAGE. Anvil's socket is the subscription;
// Kraken's needs a frame after the upgrade, and a second pair of frames to heal
// a checksum failure. `kHasSubscription` is what the transport branches on, so
// the Anvil build compiles none of that path.
inline constexpr bool kHasSubscription = true;
inline constexpr const char* kSubscribeText = kKrakenSubscribeText;
inline constexpr const char* kUnsubscribeText = kKrakenUnsubscribeText;

// The scale the ladder is drawn at, and the depth asked for.
inline constexpr SymbolSpec kSymbol = kraken::kKrakenMinaGbp.spec;
inline constexpr std::int32_t kSubscribeDepth = kraken::kKrakenSubscribeDepth;

// HOW MANY LEVELS A SIDE THE VENUE'S OWN CHECKSUM CONFIRMS — 0 where there is
// no checksum at all. It reaches `Book` as its `validated_depth`, which is what
// makes `window::WindowStats::rows_validated` a real number instead of the zero
// an unfilled field looks like. A4's "rows within the checksum's reach" is that
// number; there is no assertion anywhere that would catch it being left at the
// default, so it is passed here rather than at the construction site.
inline constexpr std::uint32_t kValidatedDepth = kraken::kChecksumLevels;

// The frame kind that carries a CLOCK rather than a market event. Everything
// else on this wire is event-driven and therefore says nothing about elapsed
// time. Differenced across a parse rather than instrumented inside the adapter,
// because `engine/` is shared with the host replay and is not modified from here.
inline std::uint64_t liveness_count(const Adapter& a) noexcept { return a.stats().heartbeats; }
inline constexpr std::string_view kLivenessSignal = "heartbeat";

// WHETHER THE VENUE'S WIRE CARRIES A SEQ WORTH JOINING ON. ARCHITECTURE §4:
// "Kraken has no seq — its adapter synthesises one." A synthesised counter looks
// exactly like a join key and correlates with nothing outside this board, so the
// instruments that print one are told not to.
inline constexpr bool kHasWireSeq = false;

// WHAT A FRAME FROM THIS VENUE STARTS WITH, for the reject log's spliced-second-
// frame scan. Review found that scan hard-coding Anvil's `{"type":`, which
// appears in no Kraken v2 frame — so `SPLIT@nnnnn`, the field that diagnosed the
// entire 2026-08-13 corruption, was a permanent no-op on this build while still
// printing as though it had looked.
//
// Two prefixes, because Kraken opens book messages with `channel` and acks with
// `method`, and the splice that matters can be either.
inline constexpr std::string_view kFrameHeadPrefix = "{\"channel\":";
inline constexpr std::string_view kFrameHeadPrefixAlt = "{\"method\":";

// The board's own construction of the adapter. Returned as a prvalue and used to
// initialise a member directly, so C++17's guaranteed elision means no copy and
// no move — which matters, because this object owns ~9 KiB of decoded-frame
// buffer and B1's review already found one use-after-move in it.
inline Adapter make_adapter() noexcept { return Adapter{kraken::kKrakenMinaGbp, kSubscribeDepth}; }

// TWO COUNTERS THAT EXIST AT ONE VENUE ONLY, read through a common name so
// the soak line needs no `#if` of its own. A venue with no checksum reports
// zero failures because it has no checksum to fail — a different statement
// from "the checksum passed", and the soak line says which one it means in
// words on the build where it applies.
inline std::uint64_t checksum_failures(const Adapter::Stats& s) noexcept {
    return s.checksums_failed;
}
inline std::uint64_t resyncs_requested(const Adapter::Stats& s) noexcept {
    return s.resyncs_requested;
}

inline const char* parse_status_name(ParseStatus st) noexcept {
    switch (st) {
        case ParseStatus::Ok:          return "ok";
        case ParseStatus::NotJson:     return "not-json";
        case ParseStatus::BadShape:    return "bad-shape";
        case ParseStatus::BadPrice:    return "bad-price";
        case ParseStatus::BadQty:      return "bad-qty";
        case ParseStatus::OtherSymbol: return "other-symbol";
    }
    return "?";
}

// The tally in `reject_log.hpp` is indexed by this, so it must cover every
// value. `note()` bounds-checks the slot, so the failure is not a write past the
// array — it is quieter than that: the new status is counted in `total_` and in
// no column, and `render_tally` never prints one for it, so the one line a bench
// greps silently stops adding up.
//
// KNOWN LIMIT, recorded rather than left to be discovered: this anchors on the
// LAST enumerator, so it catches a status INSERTED (which shifts it) and not one
// APPENDED (which does not). Closing it properly wants a trailing `Count` member
// on the engine enums, which would make every exhaustive `switch` over them warn
// — a wider blast radius than this stage should take.
inline constexpr std::size_t kParseStatusCount = 6;
static_assert(static_cast<std::size_t>(ParseStatus::OtherSymbol) + 1 == kParseStatusCount,
              "kraken::ParseStatus gained a value; widen the reject tally to match");

#else  // DC_VENUE_ANVIL

using Adapter = anvil::AnvilAdapter;
using ParseStatus = anvil::ParseStatus;

inline constexpr std::string_view kName = "anvil";

inline constexpr const char* kHost = kAnvilHost;
inline constexpr const char* kPath = kAnvilPath;
inline constexpr std::uint16_t kPort = kAnvilPort;
inline constexpr const char* kPortText = kAnvilPortText;
inline constexpr const char* kRootCaPem = kAnvilRootCaPem;
inline constexpr std::size_t kRootCaPemBytes = sizeof(kAnvilRootCaPem);

// Anvil's depth is in the query string and the socket IS the subscription, so
// there is nothing for the transport to send and nothing to re-send. The empty
// strings are never read; `kHasSubscription` is what guards them.
inline constexpr bool kHasSubscription = false;
inline constexpr const char* kSubscribeText = "";
inline constexpr const char* kUnsubscribeText = "";

inline constexpr SymbolSpec kSymbol = anvil::kAnvilTicker101;
inline constexpr std::int32_t kSubscribeDepth = 27;   // spelled in kAnvilPath's `&depth=27`

// ZERO, AND IT IS A STATEMENT RATHER THAN AN UNSET FIELD. Anvil's protocol
// publishes no checksum of any kind, so no rendered row on this build was ever
// externally confirmed — which is what the serial line has to say, in those
// words, rather than printing a percentage of nothing (DESIGN strain 24).
inline constexpr std::uint32_t kValidatedDepth = 0;

inline std::uint64_t liveness_count(const Adapter& a) noexcept { return a.stats().summary_ignored; }
inline constexpr std::string_view kLivenessSignal = "summary";

// Anvil's wire `seq` is a global engine counter — useless for ordering, which is
// exactly what makes it a JOIN KEY: the same broadcast carries the same value on
// every socket, so a serial log and a desk capture can be joined offline.
inline constexpr bool kHasWireSeq = true;

// Every frame Anvil has ever sent starts `{"type":`. The alternate is the same,
// so the two-prefix scan degenerates to one.
inline constexpr std::string_view kFrameHeadPrefix = "{\"type\":";
inline constexpr std::string_view kFrameHeadPrefixAlt = "{\"type\":";

inline Adapter make_adapter() noexcept { return Adapter{kSymbol}; }

inline std::uint64_t checksum_failures(const Adapter::Stats&) noexcept { return 0; }
inline std::uint64_t resyncs_requested(const Adapter::Stats&) noexcept { return 0; }

inline const char* parse_status_name(ParseStatus st) noexcept {
    switch (st) {
        case ParseStatus::Ok:          return "ok";
        case ParseStatus::NotJson:     return "not-json";
        case ParseStatus::MissingType: return "no-type";
        case ParseStatus::BadShape:    return "bad-shape";
        case ParseStatus::BadPrice:    return "bad-price";
        case ParseStatus::OtherTicker: return "other-ticker";
    }
    return "?";
}

inline constexpr std::size_t kParseStatusCount = 6;
static_assert(static_cast<std::size_t>(ParseStatus::OtherTicker) + 1 == kParseStatusCount,
              "anvil::ParseStatus gained a value; widen the reject tally to match");

#endif

// True where the adapter can ask the transport to re-subscribe after a checksum
// failure (M4 stage D item A2). Kraken never re-snapshots unasked, so without
// this path a CRC failure greys the panel for ever over a healthy socket;
// Anvil's adapter has no such latch because it has no checksum to fail.
inline constexpr bool kCanRequestResync = kHasSubscription;

static_assert(kSymbol.valid(), "the selected venue's SymbolSpec must be decodable");

// The macro and the constant are one fact, and this is what stops them
// separating — which is exactly what had happened before review.
static_assert(kHasSubscription == (DC_VENUE_HAS_SUBSCRIPTION != 0),
              "DC_VENUE_HAS_SUBSCRIPTION and kHasSubscription must agree");

}  // namespace depthcharge::fw::venue
