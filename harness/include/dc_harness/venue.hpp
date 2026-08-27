// dc_harness/venue.hpp — what the harness knows about a venue, in one place.
//
// M4 stage A. Until this file existed the harness knew exactly one venue and
// said so nowhere: `ticker` was required of every trace because Anvil has one,
// every frame had to carry a string `type` because Anvil's do, and the replay
// driver's 1000 ms disconnect threshold was a literal in `ReplayOptions`
// derived from Anvil's 391 ms worst healthy gap. Three Anvil facts wearing the
// clothes of universal rules. A Kraken capture met all three and lost.
//
// So the venue tag in the trace metadata (deliverable 1) selects a row here,
// and everything that used to be an assumption is a field. **This is the source
// of truth deliverable 4 draws the threshold from**, and it is deliberately not
// the trace metadata and not the driver's CLI — see WHERE THE THRESHOLD LIVES
// below, where that choice and its cost are stated.
//
// NOT the venue metadata table the firmware will consume. That is stage B's,
// and it will carry tick size, qty step, subscribed depth and the expected
// broadcast rate as well. This is the harness's half, and stage B lifting it is
// the intended path — which is the whole reason the numbers are here rather
// than spread across three call sites.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

// For `kChecksumLevels`. The Kraken row below uses the constant rather than
// restating 10, because two numbers that must agree and are written twice are
// two numbers that will one day disagree — the same rule `ladder.hpp` was
// extracted for.
#include <depthcharge/kraken/kraken_checksum.hpp>

// For `LivenessPolicy`. The clamp the grey threshold is computed with is a
// venue fact for the same reason `validated_depth` is (M5 stage C).
#include <depthcharge/liveness_clock.hpp>

namespace dc::harness {

// The venues this build can read a trace for. Adding one is: a row in
// kVenueTable, a decoder in trace_decoder.hpp, and a taxonomy pin.
//
// ONE VENUE PER BUILD is a *firmware* decision (ARCHITECTURE §9, 2026-08-17);
// the harness reads every venue it knows, because a golden that could only be
// run by rebuilding is not a golden anyone runs.
enum class Venue : std::uint8_t { Anvil, Kraken, Binance };

// A VENUE THIS CODE HAS NO BRANCH FOR IS A HARD FAILURE, NOT A DEFAULT
// (M5 stage A, deliverable 3).
//
// Every switch on `Venue` in the harness ends here instead of returning
// something. Two things make that load-bearing rather than tidy:
//
//   * with no `default:` label and every enumerator listed, adding a fourth
//     enumerator turns `-Wswitch` into an error at EVERY dispatch site under
//     -Werror — which is the loud failure DESIGN strain 22 says three of the
//     four venue edits already had and the fourth did not;
//   * the runtime path throws instead of handing back a zero-initialised
//     answer. `RecordClassifier::classify` used to `return {}` past its
//     switch, and a `RecordKind{}` is indistinguishable from "this record
//     reaches nothing and proves nothing" — a confident wrong answer, which is
//     precisely the failure mode this stage was asked to close.
//
// It throws rather than aborts because the harness's callers already catch
// TraceError-shaped failures and report the file; a std::abort here would kill
// dc_taxonomy mid-table with no idea which trace did it.
//
// `inline` and not a .cpp: this header is the one thing every venue-aware
// translation unit already includes, and a link-time dependency for a function
// that exists to make a build-time failure louder would be its own small joke.
[[noreturn]] inline void unhandled_venue(Venue v, const char* where) {
    throw std::logic_error(
        std::string("no branch for venue ") + std::to_string(static_cast<int>(v)) +
        " in " + where +
        " -- adding a Venue enumerator means adding a branch at every dispatch "
        "site, a row in kVenueTable, a decoder in trace_decoder.hpp, a VENUES row "
        "in tools/tracefile.py and a branch in each of its predicates. This is the "
        "path that used to return a default (DESIGN strain 22).");
}

// Everything that used to be assumed about "a trace".
struct VenueTraits {
    Venue venue;
    std::string_view name;  // the metadata tag; absent metadata reads as "anvil"

    // ---- the metadata contract, venue-conditional (deliverable 1) ----------
    // Every venue needs captured_at, url and tool_version. What identifies the
    // instrument does not generalise: Anvil has an integer ticker id, Kraken has
    // a string pair, and requiring both of everyone would make one of the two
    // capture tools lie.
    bool requires_ticker;
    bool requires_symbol;

    // ---- the frame contract, venue-conditional ----------------------------
    // ARCHITECTURE §9 (2026-08-07) made "a frame line must carry a string
    // `type`" the one definition of a valid trace. It is an Anvil property:
    // Kraken's heartbeat is `{"channel":"heartbeat"}`, its subscribe ack carries
    // `method`, and 61 of the depth-25 slice's 1,599 records have no `type` at
    // all. The rule is kept in full where it is true, and the decoder names the
    // kind where it is not.
    bool frames_carry_type;

    // ---- THE LIVENESS SIGNAL (2026-08-17 ruling) --------------------------
    // The NAME of the record kind whose arrival proves this feed is alive.
    // A WIRE FACT — not a duration, and deliberately not one.
    //
    // This field replaced a `double stale_gap_ms` on 2026-08-17, and the rename
    // is the point rather than tidying: the old name invited a future reader to
    // put a number in it, and the ruling is that **no threshold on book silence
    // can be correct at any venue.** A quiet market and a silently dead
    // subscription are identical on the wire, so book silence carries no
    // information about whether the displayed book can be trusted — MINA/GBP
    // went 25,843 ms without a book event on a provably healthy socket, and
    // greying that would have asserted the opposite of the truth. The bound on
    // book silence is a market property; a market can be closed.
    //
    // What replaces it is measured, not declared. The threshold is
    // `kThresholdMultiple x` the rolling median of THIS signal's observed
    // inter-arrival (liveness_clock.hpp), because both venues' intervals are
    // values DepthCharge cannot read back: Anvil's is `ANVIL_SUMMARY_HZ`,
    // operator config on a server we do not own, and Kraken's is a protocol
    // constant. A number hardcoded here would be coupled to a value that can
    // change with no client change and no error.
    //
    // The accepted cost, in its general form: a connection-level liveness signal
    // does not prove the BOOK subscription is alive. Anvil's `summary` is global
    // fan-out delivered to every socket whatever it subscribed to; Kraken's
    // heartbeat is connection-level. Neither proves the book. This trades an
    // observed false-grey — three per ten minutes on a healthy Kraken feed — for
    // an unobserved false-colour. Stage B's CRC covers updates that arrive and
    // disagree; it does not cover silence.
    std::string_view liveness_signal;

    // What that signal is, and how it was measured — printed beside the figures
    // so a reader never has to go looking for the evidence behind a policy.
    std::string_view liveness_note;

    // ---- WHAT THIS VENUE'S SIGNAL EARNS IT (M5 stage C) --------------------
    // The multiplier, floor and ceiling `LivenessClock` clamps with. NOT a
    // threshold: the threshold is still calibrated from the signal's own median
    // and is still never a constant in this table — the 2026-08-17 ruling is
    // untouched. What is per-venue is the SHAPE of the clamp, because two of
    // the three numbers are milliseconds and milliseconds do not transfer
    // between a 500 ms broadcast and a 20 s one. `liveness_clock.hpp` carries
    // the rule and the three multiples the single 30,000 ms ceiling works out
    // to; each row below carries its own derivation.
    //
    // **Anvil's and Kraken's rows are `{}` — the shipping defaults, byte for
    // byte.** That is the point rather than laziness: no venue with a shipping
    // threshold has its number moved by this field existing, so a regression
    // after it lands cannot be attributed to it (ARCHITECTURE §9, 2026-08-25).
    depthcharge::LivenessPolicy liveness;

    // ---- THE WITHDRAWN CONSTANT, kept only as a historical reference -------
    // The per-venue book-silence threshold this venue declared before the
    // 2026-08-17 ruling: Anvil 1,000 ms, Kraken 15,000 ms. **Nothing decides
    // anything on it.** It survives so that `dc_taxonomy`'s pinned columns keep
    // measuring the same fixed quantity they measured when they were pinned, and
    // so the ruling's effect is legible as a delta against a number that does
    // not move. Read `legacy_note` before quoting it.
    //
    // WHICH CLOCK THIS NUMBER IS ABOUT — said out loud from M5 stage A, because
    // Binance is the first venue where the two clocks are different quantities
    // rather than one quantity measured twice:
    //
    //   RECORD ARRIVAL   any record from the venue, in `rx_ns` order. THIS
    //                    field, and `TraceStats::watchdog_firings_*`, are about
    //                    that clock and only that clock. Binance's record
    //                    arrival goes silent for 10.5 s legitimately on a quiet
    //                    pair (M5 stage 0), which is why nothing may be armed
    //                    on it.
    //   LIVENESS         arrivals of `liveness_signal` and nothing else. This
    //                    is what greys the panel, its threshold is CALIBRATED
    //                    from the signal's own median (liveness_clock.hpp) and
    //                    is never a constant in this table, and at Binance it
    //                    is stamped from `TraceRecord::event_ns` rather than
    //                    `rx_ns` — see trace.hpp, where a ping's arrival and
    //                    the moment its record was written are hours-of-
    //                    difference apart in the same file.
    //
    // At Anvil and Kraken the two coincided closely enough that nothing ever
    // had to say which was meant. That coincidence is over, and a fourth venue
    // must not inherit the ambiguity.
    //
    // **-1.0 MEANS THIS VENUE NEVER DECLARED ONE.** It is a sentinel and not a
    // number: Binance was added after the ruling that withdrew the other two,
    // so it has no withdrawn constant to hold still. It is not 0, because 0
    // would silently count every record-arrival gap as a firing and produce a
    // pinned column full of confident nonsense.
    double legacy_book_threshold_ms;
    std::string_view legacy_note;

    // Whether `legacy_book_threshold_ms` is a real historical number.
    constexpr bool has_legacy_threshold() const noexcept {
        return legacy_book_threshold_ms >= 0.0;
    }

    // ---- HOW MANY OF THE BOOK'S BEST LEVELS THE VENUE ITSELF CONFIRMS ------
    // M4 stage C. Kraken's CRC32 is computed over the top 10 levels a side
    // whatever the subscribed depth (B2, measured); Anvil publishes no checksum
    // of any kind, so nothing it sends is externally validated and the honest
    // number is 0 rather than "all of them".
    //
    // It is here rather than in `engine/` because it is a venue fact, and the
    // window that consumes it must stay venue-free (invariant #2). What it buys
    // is the one number stage D cannot get from the panel: a window's position
    // decides whether the rows on screen were ever checked, and at depth 100 a
    // 27-row window can be showing 27 levels of which at most 10 were.
    //
    // NOTHING BRANCHES ON IT. It is reported beside the window, in the same
    // spirit as `age_ms`: a number the operator reads, never a rule the code
    // applies.
    std::uint32_t validated_depth;
    std::string_view validated_note;
};

// The table. Both rows are measurements, not preferences.
inline constexpr VenueTraits kVenueTable[] = {
    {Venue::Anvil, "anvil",
     /*requires_ticker=*/true, /*requires_symbol=*/false,
     /*frames_carry_type=*/true,
     /*liveness_signal=*/"summary",
     "cross-ticker roster on a fixed engine-thread deadline that fires on an EMPTY "
     "queue -- 1,191 intervals, median 500.0 ms, worst healthy 968.8 ms (1.94x, one "
     "missed tick). Confirmed idle: anvil_101_feederoff_20260817, feeder never "
     "started, 241 byte-identical frames in 120 s with seq advancing",
     // 4.0 x a 500.0 ms median = 2,000 ms, which is also the floor. The 30,000 ms
     // ceiling is 60x the median and binds only on a feed that has decayed 15x.
     /*liveness=*/{},
     /*legacy_book_threshold_ms=*/1000.0,
     "WITHDRAWN 2026-08-17. Was the book-silence threshold, derived from Anvil's "
     "391 ms worst healthy gap at 2.6x margin (ARCHITECTURE §9, 2026-08-09)",
     /*validated_depth=*/0,
     "no checksum anywhere in the protocol -- M0 looked, and the vendored "
     "PROTOCOL.md has no integrity field of any kind. Every level Anvil sends is "
     "taken on trust, which is survivable only because its book frames are "
     "idempotent full replaces: a corrupted one is corrected 80 ms later"},
    {Venue::Kraken, "kraken",
     /*requires_ticker=*/false, /*requires_symbol=*/true,
     /*frames_carry_type=*/false,
     /*liveness_signal=*/"heartbeat",
     "1 Hz connection-level broadcast -- 834 intervals across two hours of day, "
     "median 1000.3 ms, worst 1,119.0 ms (1.12x). Held 936-1,042 ms cadence right "
     "through the 25,843 ms book hole that proved book silence unusable",
     // 4.0 x a 1000.3 ms median = 4,001 ms. Ceiling 30x the median, never binds.
     /*liveness=*/{},
     /*legacy_book_threshold_ms=*/15000.0,
     "WITHDRAWN 2026-08-17, and it is the constant the ruling was written to "
     "retire: a second quiet-pair window measured a healthy 25,843 ms book "
     "silence, 1.72x this number, so it would have invented 3 disconnects in 10 "
     "minutes on a feed that never lost a packet",
     /*validated_depth=*/depthcharge::kraken::kChecksumLevels,
     "CRC32 over the top 10 levels a side, regardless of the subscribed depth -- "
     "confirmed at stage 0 across 8,677 checksums at depths 10/25/100, and "
     "re-confirmed at B2 by the only test that discriminates: a book edited BELOW "
     "level 10 does not move the checksum"},
    // --- BINANCE (M5 stage A) ----------------------------------------------
    // The first venue whose liveness signal is not a record. It is a WebSocket
    // PING control frame: not JSON, not something the venue "said" in the sense
    // the other two rows mean, and until the `kind` record shape landed it could
    // not appear in a DepthCharge trace at all. `liveness_signal` still names it,
    // because the field's contract is "the NAME of the record kind whose arrival
    // proves this feed is alive" and a control record's kind name IS its opcode
    // (trace_decoder.hpp, binance_classify).
    {Venue::Binance, "binance",
     /*requires_ticker=*/false, /*requires_symbol=*/true,
     /*frames_carry_type=*/false,
     /*liveness_signal=*/"ping",
     "WebSocket PING control frame, answered by the transport -- 23 intervals, "
     "median 19,970 ms (min 19,850, max 20,200; worst/median 1.01x, the tightest "
     "of the three venues). The lone 40.7 s is a deliberate reconnect restarting "
     "the venue's schedule, not a missed ping. THE DEPTH STREAMS PUBLISH NO "
     "LIVENESS RECORD AT ALL: both are change-driven, and record arrival went "
     "silent for 10.5 s legitimately on the quiet pair -- so this venue's grey is "
     "armed on the ping and on nothing else (ARCHITECTURE 9, 2026-08-25). The 4x "
     "margin the other two run WOULD put the threshold near 80 s -- a 20x "
     "regression in grey latency against Kraken's ~4 s, accepted in exchange for "
     "catching the half-open socket that was the plurality case at 4 of 7 losses. "
     "IT DID NOT, AND THIS ROW IS WHERE THAT WAS RECORDED (measured M5 stage A): "
     "liveness_clock.hpp clamped at kThresholdCeilingMs = 30,000 ms, so 4 x 19,970 "
     "= 79,880 capped to 30 s -- the identical number kUncalibratedThresholdMs "
     "already held, so the self-calibration the 2026-08-17 ruling rests on was a "
     "constant wearing a calibration's clothes. **SETTLED AT M5 STAGE C, AND THE "
     "MEASUREMENT SAYS NEITHER 80 s NOR 30 s.** The multiplier is not a "
     "preference: liveness_clock.hpp derives it as the venue's worst HEALTHY "
     "inter-arrival expressed as a multiple of its own median, times ~2 of "
     "margin. Anvil is 1.937x and gets 4.0. This signal is 1.005x over the "
     "calibration capture's ten intervals and 1.01x over stage A's 23 -- the "
     "tightest of the three venues, because it is a metronome inside the "
     "WebSocket layer rather than a publisher sharing a queue with the book -- so "
     "the SAME derivation gives 2.0, and 2.0 x 19,963.97 = 39,928 ms. Inheriting "
     "Anvil's 4.0 here does not buy safety; it buys 40 s more frozen ladder for "
     "margin this signal's jitter does not need, and the thing it is detecting is "
     "narrower than at the other two (see the ping's emission point, DESIGN "
     "strain 26). The ceiling is 60,000 ms: it has to clear 39,928 or the clamp "
     "is the threshold again, and it admits a cadence 50% slower than measured "
     "before it binds",
     /*liveness=*/{.multiple = 2.0, .ceiling_ms = 60000.0},
     /*legacy_book_threshold_ms=*/-1.0,
     "NONE, and -1 is a sentinel rather than a number. This venue was added "
     "2026-08-25, after the ruling that withdrew Anvil's 1,000 ms and Kraken's "
     "15,000 ms, so it has no declared record-arrival constant to hold still and "
     "no pinned column derived from one. `watchdog_firings_at_anvil_threshold` "
     "still measures against Anvil's 1,000 ms and is the informative number here: "
     "it is large by construction, because record arrival is not this venue's "
     "liveness clock",
     /*validated_depth=*/0,
     "no integrity field on any record -- no checksum, no CRC, nothing the venue "
     "signs. What this venue has instead is a SECOND STREAM: `@depth20`'s "
     "`lastUpdateId` coincided with a diff `u` on 899/899, 901/901, 90/90 and "
     "29/29 payloads (M5 stage 0), and 90/90 twice more at the 1000 ms tick that "
     "the 2026-08-25 ruling ships on the board. That is an exact CROSS-STREAM "
     "audit, not a per-record checksum, and nothing in THIS build performs it -- "
     "so the honest count of rendered rows the venue itself confirmed is 0. It "
     "becomes 20 at the stage that wires the audit into the window, which is C"},
};

constexpr const VenueTraits& venue_traits(Venue v) noexcept {
    return kVenueTable[static_cast<std::size_t>(v)];
}

// Belt and braces: the table is indexed by the enum, so a row inserted in the
// wrong place would silently give Kraken Anvil's threshold.
static_assert(kVenueTable[static_cast<std::size_t>(Venue::Anvil)].venue == Venue::Anvil);
static_assert(kVenueTable[static_cast<std::size_t>(Venue::Kraken)].venue == Venue::Kraken);
static_assert(kVenueTable[static_cast<std::size_t>(Venue::Binance)].venue == Venue::Binance);
static_assert(sizeof(kVenueTable) / sizeof(kVenueTable[0]) ==
                  static_cast<std::size_t>(Venue::Binance) + 1,
              "every Venue enumerator needs a row in kVenueTable");

// ...and the general form of the same check, which the three lines above cannot
// express: every row sits at the index of the enumerator it names. The named
// assertions catch a row inserted in the wrong place at the ends; this catches
// one inserted anywhere, and costs nothing.
//
// NEITHER catches a FOURTH enumerator added with no row — the count assertion
// above names `Venue::Binance` and would have to be edited to notice. That gap
// is real and is closed elsewhere rather than here: every switch on `Venue` in
// the harness lists its enumerators with no `default:` and ends in
// `unhandled_venue`, so a new enumerator is a -Wswitch error at each dispatch
// site under -Werror. Adding one is meant to break the build in several places
// at once; this file is one of them and is not the only one.
constexpr bool every_row_sits_at_its_own_index() noexcept {
    std::size_t i = 0;
    for (const VenueTraits& v : kVenueTable) {
        if (static_cast<std::size_t>(v.venue) != i) { return false; }
        ++i;
    }
    return true;
}
static_assert(every_row_sits_at_its_own_index(),
              "a venue row is not at the index of the enumerator it names -- "
              "venue_traits() would hand out the wrong venue's thresholds");

// AN ABSENT CAPABILITY MUST BE REPRESENTED, NOT LEFT AS A ZERO (ARCHITECTURE §9,
// 2026-08-19). `validated_depth` is 0 for Anvil because that protocol has no
// checksum at all, and 0 is also exactly what an unfilled field looks like — so
// the note is mandatory and the compiler is what makes it mandatory. A venue
// added without one would publish "nothing here was ever confirmed" as an
// accident rather than as a finding.
constexpr bool every_row_explains_its_validated_depth() noexcept {
    for (const VenueTraits& v : kVenueTable) {
        if (v.validated_note.empty()) { return false; }
    }
    return true;
}
static_assert(every_row_explains_its_validated_depth(),
              "a venue row must say WHY its validated_depth is what it is -- a bare 0 cannot "
              "be told apart from a field nobody filled in");

// The same rule, applied to the other field that now has a sentinel (M5 stage
// A). `legacy_book_threshold_ms` reads -1 at Binance and means NEVER DECLARED;
// a reader who quotes it as a threshold has been told not to, in the row.
constexpr bool every_row_explains_its_legacy_threshold() noexcept {
    for (const VenueTraits& v : kVenueTable) {
        if (v.legacy_note.empty()) { return false; }
    }
    return true;
}
static_assert(every_row_explains_its_legacy_threshold(),
              "a venue row must say what its legacy_book_threshold_ms was and why it is "
              "withdrawn or absent -- the number decides nothing and must never read as "
              "though it did");

// The metadata tag -> venue rule, in ONE place, in this language. The prose
// statement both languages share is in harness/replay/NOTES.md; the Python half
// is tools/tracefile.py.
//
// An ABSENT tag reads as Anvil. That is what makes the tag additive and what
// keeps the four committed Anvil traces byte-identical: they predate it.
constexpr bool venue_from_name(std::string_view name, Venue& out) noexcept {
    if (name.empty()) {
        out = Venue::Anvil;  // absent tag => the venue that predates the tag
        return true;
    }
    for (const VenueTraits& t : kVenueTable) {
        if (t.name == name) {
            out = t.venue;
            return true;
        }
    }
    return false;  // a DepthCharge capture of a venue this build does not know
}

}  // namespace dc::harness
