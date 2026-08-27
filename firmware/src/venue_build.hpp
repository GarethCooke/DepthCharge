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

#define DC_VENUE_ANVIL   1
#define DC_VENUE_KRAKEN  2
#define DC_VENUE_BINANCE 3

#ifndef DC_VENUE
#define DC_VENUE DC_VENUE_ANVIL
#endif

#if DC_VENUE != DC_VENUE_ANVIL && DC_VENUE != DC_VENUE_KRAKEN && \
    DC_VENUE != DC_VENUE_BINANCE
#error "DC_VENUE must be DC_VENUE_ANVIL, DC_VENUE_KRAKEN or DC_VENUE_BINANCE"
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

// THE SECOND PREDICATE, AND M5 STAGE D-A1 ADDED IT BECAUSE THE THIRD VENUE
// PROVED ONE WAS NOT ENOUGH.
//
// `feed_task.hpp::last_wire_seq()` guarded itself with
// `DC_VENUE_HAS_SUBSCRIPTION`, which was correct for exactly as long as there
// were two venues: Kraken had a subscription AND no wire seq, Anvil had neither
// a subscription NOR a missing seq, so one macro happened to answer both
// questions. **Binance has no subscription and no wire seq**, so it fell into
// the Anvil arm and the build failed on `adapter_.last_wire_seq()` — a member
// only Anvil's adapter has.
//
// It failed LOUDLY, which is the good case and is worth recording as such: the
// same confusion in the transport direction is the one this file's header
// describes, where a third venue *with* a subscription would have compiled
// cleanly and connected to a socket nobody subscribes on. A predicate that is
// two facts wearing one name is a defect whether or not today's venues happen
// to agree, and the fix is to separate them rather than to widen the arm.
//
// A macro for the same reason `DC_VENUE_HAS_SUBSCRIPTION` is one: the branch
// must be PREPROCESSED, because `if constexpr` outside a template still
// instantiates the discarded arm and `adapter_.last_wire_seq()` does not
// compile against two of the three adapters. The `static_assert` at the bottom
// of this file ties it to `kHasWireSeq` so the two spellings cannot separate.
#if DC_VENUE == DC_VENUE_ANVIL
#define DC_VENUE_HAS_WIRE_SEQ 1
#else
#define DC_VENUE_HAS_WIRE_SEQ 0
#endif

#include <cstdint>
#include <string_view>

#include <depthcharge/symbol.hpp>

#if DC_VENUE == DC_VENUE_KRAKEN
#include <depthcharge/kraken/kraken_adapter.hpp>
#include <depthcharge/kraken/kraken_checksum.hpp>
#include "kraken_endpoint.hpp"
#include "kraken_root_ca.hpp"
#elif DC_VENUE == DC_VENUE_BINANCE
#include <depthcharge/binance/binance_adapter.hpp>
#include "binance_endpoint.hpp"
#include "binance_root_ca.hpp"
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

#elif DC_VENUE == DC_VENUE_BINANCE

using Adapter = binance::BinanceAdapter;
using ParseStatus = binance::ParseStatus;

inline constexpr std::string_view kName = "binance";

inline constexpr const char* kHost = kBinanceHost;
inline constexpr const char* kPath = kBinancePath;
inline constexpr std::uint16_t kPort = kBinancePort;
inline constexpr const char* kPortText = kBinancePortText;
inline constexpr const char* kRootCaPem = kBinanceRootCaPem;
inline constexpr std::size_t kRootCaPemBytes = sizeof(kBinanceRootCaPem);

// NO SUBSCRIPTION, AND THIS VENUE SITS EXACTLY WHERE ANVIL DOES. Binance names
// the stream in the URL path (`/ws/btcusdt@depth@100ms`), so the socket IS the
// subscription: there is nothing to send after the upgrade and nothing to
// re-send. The empty strings are never read; `kHasSubscription` is what guards
// them, and `DC_VENUE_HAS_SUBSCRIPTION` above already resolves to 0 for
// anything that is not Kraken, so no branch in the transport changes.
inline constexpr bool kHasSubscription = false;
inline constexpr const char* kSubscribeText = "";
inline constexpr const char* kUnsubscribeText = "";

inline constexpr SymbolSpec kSymbol = binance::kBinanceBtcUsdt.spec;
// Not a subscribed depth at this venue — the diff stream carries whatever
// changed — but the number the SEED asks for, which is the quantity the other
// two venues' `kSubscribeDepth` also names: how deep this build's book is
// baselined. Its lower bound is enforced in the engine
// (`kBinanceReseedCoverLevels < kBinanceRestLimit`).
inline constexpr std::int32_t kSubscribeDepth =
    static_cast<std::int32_t>(binance::kBinanceRestLimit);

// ZERO, AND IT IS A STATEMENT RATHER THAN AN UNSET FIELD — the same statement
// Anvil's makes and for a sharper reason. Binance publishes NO checksum of any
// kind (M5's ROADMAP row: "`U`/`u` bracketing detects lost or misordered
// messages and says nothing about whether the resulting book is correct"), so
// no rendered row on this build was ever externally confirmed. The oracle that
// grades this venue is the `@depth20` stream, and it is a HOST instrument: it
// runs in ctest against the committed corpus and is deliberately not on the
// board (see binance_endpoint.hpp). DESIGN strain 24 is what reads this field.
inline constexpr std::uint32_t kValidatedDepth = 0;

// ===========================================================================
// THE LIVENESS SIGNAL AT THIS VENUE IS THE SERVER'S WEBSOCKET PING, AND THIS
// FUNCTION CANNOT SEE IT. READ THIS BEFORE BELIEVING THE BOARD'S GREY.
// ===========================================================================
//
// At Anvil the clock is a `summary` frame and at Kraken a `heartbeat` frame:
// both are application frames, so both reach `BinanceAdapter`'s counterparts as
// parsed messages and `feed_task.cpp` can difference a counter across the
// parse. **Binance emits neither.** Its clock is a WebSocket PING every ~20 s —
// measured across the committed corpus at a median 19,964.0 ms with a
// worst/median of 1.005, the tightest cadence of the three venues
// (`NOTES-binance.md`, M5 stage C addendum) — and a ping is a CONTROL frame.
// It is answered in `WsTransport::on_ping` on the RX task and never becomes a
// message, so it never passes through `handle_message` and this function is
// never called for it.
//
// So this returns a CONSTANT, and the consequence is stated rather than left to
// be discovered on the bench:
//
//   * `venue::liveness_count(adapter_) != liveness_before` is never true, so
//     `LivenessWatchdog::on_liveness` is never stamped on this build, so
//     `armed_` is never set and `expired()` is permanently false.
//   * The panel is therefore NOT greyed by the liveness watchdog here. It is
//     grey because remedy (a) withholds the Snapshot until a diff brackets the
//     seed, and — once a socket dies — because the transport's own silence
//     recycle and `Gap{Disconnect}` grey it, neither of which routes through
//     the watchdog.
//   * `age_ms` reads nothing, which is the already-recorded behaviour for the
//     first ~11 minutes of every connection at this cadence anyway (the age
//     baseline latches on the 32nd interval = 639 s).
//
// A CONSTANT RATHER THAN A PLAUSIBLE COUNTER, deliberately. `frames_in` or
// `diff_frames` would compile, would stamp the watchdog, and would be a market
// event dressed as a clock — the exact dishonesty the other two venues' comment
// exists to prevent ("the one frame kind on either wire that carries a CLOCK
// rather than a market event"). A quantity this build cannot measure is
// reported as absent, not approximated.
//
// **WHAT IS OWED, AND BY WHOM.** Wiring the ping to the watchdog is what stage
// C's *Owed by stage D* item 5 requires before its reduced parity claim — *"the
// panel greys within the calibrated liveness threshold of the socket falling
// silent — 39.9 s"* — can be TESTED rather than asserted, which is D-C's. It is
// not free and it is not D-A1's: the count lives in `PingProbe` on the RX task
// while the watchdog is stamped on the feed task, so it crosses a task boundary
// that invariant #8 governs, and `liveness_count(const Adapter&)`'s signature
// would have to change for all three venues. Raised here rather than improvised
// under a stage whose acceptance does not reach it.
inline std::uint64_t liveness_count(const Adapter&) noexcept { return 0; }
inline constexpr std::string_view kLivenessSignal = "server-ping (NOT WIRED — see venue_build.hpp)";

// Binance's diff stream carries `U`/`u` — a per-symbol update-id range that IS
// meaningful for ordering, and the adapter brackets on it. But it is not a
// join key in Anvil's sense: it correlates a capture with THIS symbol's stream
// and with nothing else on the wire, and no second observer of the same socket
// exists to join against. False, like Kraken's, so the instruments that print
// one are told not to.
inline constexpr bool kHasWireSeq = false;

// WHAT A FRAME FROM THIS VENUE STARTS WITH, for the reject log's spliced-
// second-frame scan. A raw `/ws/` stream delivers the event object itself, so
// every frame opens with the event-type key `{"e":"depthUpdate"`. The alternate
// covers the `/stream?streams=` combined wrapper (`{"stream":`), which the
// board does not use today but the corpus is full of — a capture replayed
// through this build would otherwise scan for a prefix that never appears,
// which is precisely the permanent no-op review found at Kraken.
inline constexpr std::string_view kFrameHeadPrefix = "{\"e\":";
inline constexpr std::string_view kFrameHeadPrefixAlt = "{\"stream\":";

// The board's own construction of the adapter. Returned as a prvalue and used
// to initialise a member directly, so C++17's guaranteed elision means no copy
// and no move — which matters MORE here than at Kraken: since M5 stage D-A1
// this object owns a heap block (`buf_lvl_`, 32 KiB, PSRAM on the target), so
// it is move-only and a copy would not compile.
inline Adapter make_adapter() noexcept { return Adapter{binance::kBinanceBtcUsdt}; }

// No checksum at this venue, so no failures to report — the same honest zero
// Anvil returns, and the SOAK line says in words which of the two statements
// it means. `resyncs_requested` IS real here and is the re-seed counter: the
// adapter asks for a fresh REST snapshot when seeded coverage exhausts.
inline std::uint64_t checksum_failures(const Adapter::Stats&) noexcept { return 0; }
inline std::uint64_t resyncs_requested(const Adapter::Stats& s) noexcept {
    return s.reseeds_requested;
}

inline const char* parse_status_name(ParseStatus st) noexcept {
    switch (st) {
        case ParseStatus::Ok:            return "ok";
        case ParseStatus::NotJson:       return "not-json";
        case ParseStatus::BadShape:      return "bad-shape";
        case ParseStatus::BadPrice:      return "bad-price";
        case ParseStatus::BadQty:        return "bad-qty";
        case ParseStatus::OtherSymbol:   return "other-symbol";
        case ParseStatus::TooManyLevels: return "too-many-levels";
    }
    return "?";
}

// SEVEN, NOT SIX, AND IT IS THE FIRST VENUE THAT DIFFERS. Anvil and Kraken both
// have exactly six statuses; Binance adds `TooManyLevels`, because at a DIFF
// venue a truncated message is a book that has silently missed amendments
// rather than merely a shallower one (see `binance_frame.hpp`). `reject_log.hpp`
// sizes `by_status_[]` from this constant, so six here would not overflow — it
// would do the quieter thing its own comment warns about: count the new status
// in `total_` and in no column, and never print it, so the one line a bench
// greps silently stops adding up.
inline constexpr std::size_t kParseStatusCount = 7;
static_assert(static_cast<std::size_t>(ParseStatus::TooManyLevels) + 1 == kParseStatusCount,
              "binance::ParseStatus gained a value; widen the reject tally to match");

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

// The same tie for the second predicate. Added with it at M5 stage D-A1: the
// macro and the constant are one fact, and this is what stops them separating.
static_assert(kHasWireSeq == (DC_VENUE_HAS_WIRE_SEQ != 0),
              "DC_VENUE_HAS_WIRE_SEQ and kHasWireSeq must agree");

}  // namespace depthcharge::fw::venue
