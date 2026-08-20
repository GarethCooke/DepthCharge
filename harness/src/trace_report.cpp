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
    std::fprintf(out, "%ssnapshots   : %zu   resync %zu %s\n", indent, s.snapshot_count,
                 s.mid_stream_snapshots,
                 s.mid_stream_snapshots > 0 ? "(=> trace spans a reconnect)"
                                            : "(no reconnect in trace)");

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
    std::fprintf(out,
                 "%s  withdrawn  : record-arrival %zu @ %.0f ms / %zu @ %.0f ms Anvil;"
                 "  book-event %zu / %zu\n",
                 indent, s.watchdog_firings_legacy, t.legacy_book_threshold_ms,
                 s.watchdog_firings_at_anvil_threshold, anvil_ms,
                 s.book_watchdog_firings_legacy,
                 s.book_watchdog_firings_at_anvil_threshold);

    std::fprintf(out, "%skinds       :", indent);
    for (const auto& [kind, n] : s.kind_counts) {
        std::fprintf(out, "  %s=%zu", kind.c_str(), n);
    }
    std::fprintf(out, "\n");
}

}  // namespace dc::harness
