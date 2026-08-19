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
#include <string_view>

// For `kChecksumLevels`. The Kraken row below uses the constant rather than
// restating 10, because two numbers that must agree and are written twice are
// two numbers that will one day disagree — the same rule `ladder.hpp` was
// extracted for.
#include <depthcharge/kraken/kraken_checksum.hpp>

namespace dc::harness {

// The venues this build can read a trace for. Adding one is: a row in
// kVenueTable, a decoder in trace_decoder.hpp, and a taxonomy pin.
//
// ONE VENUE PER BUILD is a *firmware* decision (ARCHITECTURE §9, 2026-08-17);
// the harness reads every venue it knows, because a golden that could only be
// run by rebuilding is not a golden anyone runs.
enum class Venue : std::uint8_t { Anvil, Kraken };

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

    // ---- THE WITHDRAWN CONSTANT, kept only as a historical reference -------
    // The per-venue book-silence threshold this venue declared before the
    // 2026-08-17 ruling: Anvil 1,000 ms, Kraken 15,000 ms. **Nothing decides
    // anything on it.** It survives so that `dc_taxonomy`'s pinned columns keep
    // measuring the same fixed quantity they measured when they were pinned, and
    // so the ruling's effect is legible as a delta against a number that does
    // not move. Read `legacy_note` before quoting it.
    double legacy_book_threshold_ms;
    std::string_view legacy_note;

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
};

constexpr const VenueTraits& venue_traits(Venue v) noexcept {
    return kVenueTable[static_cast<std::size_t>(v)];
}

// Belt and braces: the table is indexed by the enum, so a row inserted in the
// wrong place would silently give Kraken Anvil's threshold.
static_assert(kVenueTable[static_cast<std::size_t>(Venue::Anvil)].venue == Venue::Anvil);
static_assert(kVenueTable[static_cast<std::size_t>(Venue::Kraken)].venue == Venue::Kraken);
static_assert(sizeof(kVenueTable) / sizeof(kVenueTable[0]) ==
                  static_cast<std::size_t>(Venue::Kraken) + 1,
              "every Venue enumerator needs a row in kVenueTable");

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
