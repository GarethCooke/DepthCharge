// dc_harness/trace_report.cpp — see trace_report.hpp for why this is one place.
#include "dc_harness/trace_report.hpp"

#include <string>

#include <depthcharge/liveness_clock.hpp>
#include "dc_harness/venue.hpp"

namespace dc::harness {

using depthcharge::kThresholdMultiple;

void print_trace_findings(std::FILE* out, const TraceStats& s, const char* indent) {
    const VenueTraits& t = venue_traits(s.meta.venue);
    const double anvil_ms = venue_traits(Venue::Anvil).legacy_book_threshold_ms;
    const std::string_view clock = s.meta.clock_name();

    std::fprintf(out, "%svenue       : %.*s%s   rx_ns clock: %.*s\n", indent,
                 static_cast<int>(t.name.size()), t.name.data(),
                 s.meta.venue_present ? "" : " (inferred: no venue tag)",
                 static_cast<int>(clock.size()), clock.data());
    std::fprintf(out, "%srecords     : %zu  (%zu received, %zu sent, %zu with no string `type`)\n",
                 indent, s.frame_count, s.received_count(), s.tx_count, s.untyped_records);
    std::fprintf(out, "%sspan        : %.1f s  (%.1f /s)   median gap %.1f ms   max gap %.1f ms\n",
                 indent, s.span_seconds(), s.frames_per_second(), s.median_gap_ms,
                 s.max_gap_ms);
    // Count only. The longest interval between book events used to be printed
    // here AS WELL, under a second name — one quantity, two labels, four lines
    // apart, in the report a reader checks a watchdog against. It belongs with
    // the distribution it is part of, below.
    std::fprintf(out, "%sbook        : %zu event(s)\n", indent, s.book_events);
    // THE PARENTHETICAL USED TO SAY "(=> trace spans a reconnect)" AND THAT WAS
    // AN ANVIL-SHAPED INFERENCE (M5 stage A). It holds at the two venues whose
    // only re-baseline arrives on reconnect; it is false at a venue that
    // re-seeds its book from REST on a four-second schedule, where a mid-stream
    // snapshot is the design rather than an incident. The COUNT is unchanged and
    // still pinned — only the sentence drawn from it moves, to the thing the
    // number actually says at all three venues.
    std::fprintf(out, "%ssnapshots   : %zu   resync %zu %s\n", indent, s.snapshot_count,
                 s.mid_stream_snapshots,
                 s.mid_stream_snapshots > 0
                     ? "(a snapshot re-baselined a book that already had events)"
                     : "(no mid-stream re-baseline)");
    // The two forms that are not frames, printed only where they exist. A line
    // of zeroes on every Anvil and Kraken report would be noise about a thing
    // those venues do not have.
    if (s.rest_records > 0 || s.control_records > 0) {
        std::fprintf(out,
                     "%snot frames  : %zu REST fetch(es), %zu control frame(s), %zu "
                     "answered\n",
                     indent, s.rest_records, s.control_records, s.control_replied);
    }

    // TWO DISTRIBUTIONS, AND THEY MUST NOT BE CONFUSABLE (2026-08-17 ruling).
    // The first decides whether the panel greys. The second decides nothing at
    // all — no threshold on it can be correct, because MINA/GBP went 25,843 ms
    // without a book event on a provably healthy socket. The labels say which is
    // which because reading one as the other is the mistake the ruling exists to
    // prevent.
    //
    // THE SECOND ROW WAS CALLED "BOOK AGE" UNTIL M4 STAGE A2, AND IT WAS THE
    // WRONG NAME — for the same reason, one level down. `age_ms` is the book's
    // estimated QUEUING LAG and is explicitly NOT time since the book last
    // changed: a book that has not changed is not old, and at MINA/GBP the
    // displayed book was exactly correct throughout the 25,843 ms this row
    // measures. Two quantities that differ by orders of magnitude were sharing a
    // name in the report a reader checks the watchdog against. This row is
    // market information; the age is computed from the LIVENESS row above and
    // lives on `DisplaySnapshot` (depthcharge/age_estimator.hpp).
    std::fprintf(out, "%sLIVENESS    : %zu x %.*s   median %.1f ms   worst %.1f ms\n",
                 indent, s.liveness_events,
                 static_cast<int>(t.liveness_signal.size()), t.liveness_signal.data(),
                 s.median_liveness_gap_ms, s.max_liveness_gap_ms);
    std::fprintf(out,
                 "%s  -> GREY     : %zu firing(s) at a self-calibrated %.0f ms "
                 "(%.1fx the observed median)\n",
                 indent, s.liveness_firings, s.liveness_threshold_ms, kThresholdMultiple);
    std::fprintf(out, "%s  signal     : %.*s\n", indent,
                 static_cast<int>(t.liveness_note.size()), t.liveness_note.data());
    std::fprintf(out,
                 "%sBOOK SILENCE: worst %.1f ms with no book event — market information, "
                 "not the book's age, and never a grey signal\n",
                 indent, s.max_book_gap_ms);

    // The two withdrawn constants, kept visible only so the ruling reads as a
    // delta. Labelled WITHDRAWN so nobody quotes them as policy.
    //
    // A VENUE MAY HAVE NEVER DECLARED ONE, and then the column says so instead
    // of printing a sentinel. Binance was added after the ruling that withdrew
    // the other two; `@ -1 ms` would read as a threshold, which is exactly what
    // this block exists to stop anyone doing with these numbers.
    if (t.has_legacy_threshold()) {
        std::fprintf(out,
                     "%s  withdrawn  : record-arrival %zu @ %.0f ms / %zu @ %.0f ms Anvil;"
                     "  book-event %zu / %zu\n",
                     indent, s.watchdog_firings_legacy, t.legacy_book_threshold_ms,
                     s.watchdog_firings_at_anvil_threshold, anvil_ms,
                     s.book_watchdog_firings_legacy,
                     s.book_watchdog_firings_at_anvil_threshold);
    } else {
        std::fprintf(out,
                     "%s  withdrawn  : this venue never declared one;  record-arrival "
                     "%zu @ %.0f ms Anvil;  book-event %zu\n",
                     indent, s.watchdog_firings_at_anvil_threshold, anvil_ms,
                     s.book_watchdog_firings_at_anvil_threshold);
    }

    std::fprintf(out, "%skinds       :", indent);
    for (const auto& [kind, n] : s.kind_counts) {
        std::fprintf(out, "  %s=%zu", kind.c_str(), n);
    }
    std::fprintf(out, "\n");
}

}  // namespace dc::harness
