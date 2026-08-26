// dc_replay — DepthCharge replay-trace validator.
//
// Reads a DepthCharge capture NDJSON trace, validates its line structure, and
// prints per-kind record counts plus cadence and seq observations. No book, no
// adapter logic here — this is the ground-truth reader the later milestones
// build on.
//
//   usage: dc_replay <trace.ndjson>
//
// Exit 0 if the trace is structurally valid; 1 on bad usage or a malformed
// trace (with the offending line number); 2 for a well-formed capture of a
// venue this build does not know, which is a different thing and only one of
// them is a bug.
//
// TWO REPORTS, ONE READER (M4 stage A). The Anvil report below is UNCHANGED,
// deliberately down to the byte: stage A's proof that the venue tag cost Anvil
// nothing is a diff of this program's output over all four committed Anvil
// traces, and a report that grew a line would have made that proof an argument
// instead of a diff. The venue, the clock and the two watchdog clocks are
// printed by dc_taxonomy, which is new and has no baseline to preserve.
#include <cstdio>
#include <exception>
#include <string>

#include "dc_harness/trace.hpp"
#include "dc_harness/trace_report.hpp"
#include "dc_harness/venue.hpp"

namespace {

void print_report(const std::string& path, const dc::harness::TraceStats& s) {
    const auto& m = s.meta;
    std::printf("dc_replay — DepthCharge M0 trace validator\n\n");
    std::printf("trace     : %s\n", path.c_str());
    std::printf("metadata  : ok  (ticker=%lld  tool_version=%s  mode=%s  cycles=%lld)\n",
                static_cast<long long>(m.ticker), m.tool_version.c_str(),
                m.capture_mode.empty() ? "-" : m.capture_mode.c_str(),
                static_cast<long long>(m.cycles));
    std::printf("            captured_at=%s\n", m.captured_at.c_str());
    std::printf("            url=%s\n", m.url.c_str());
    std::printf("frames    : %zu over %.1f s  (%.1f /s)\n",
                s.frame_count, s.span_seconds(), s.frames_per_second());

    std::printf("kinds     :");
    for (const auto& [kind, n] : s.kind_counts) {
        std::printf("  %s=%zu", kind.c_str(), n);
    }
    std::printf("\n");

    const double span = s.span_seconds();
    std::printf("per-kind  :");
    for (const auto& [kind, n] : s.kind_counts) {
        const double rate = span > 0.0 ? static_cast<double>(n) / span : 0.0;
        std::printf("  %s=%.1f/s", kind.c_str(), rate);
    }
    std::printf("\n");

    std::printf("cadence   : median gap %.1f ms   max gap %.1f ms\n",
                s.median_gap_ms, s.max_gap_ms);
    std::printf("seq       : %zu with seq   min=%lld  max=%lld   "
                "backward_steps=%zu   monotonic=%s\n",
                s.seq_frames, static_cast<long long>(s.seq_min),
                static_cast<long long>(s.seq_max), s.seq_backward_steps,
                s.seq_monotonic ? "YES" : "NO");
    std::printf("snapshots : %zu   mid-stream=%zu %s\n",
                s.snapshot_count, s.mid_stream_snapshots,
                s.mid_stream_snapshots > 0 ? "(=> trace spans a reconnect)"
                                           : "(no reconnect in trace)");
    std::printf("\nOK\n");
}

// The report for a venue that identifies its instrument by SYMBOL — Kraken
// since M4 stage A, Binance since M5 stage A.
//
// A separate function from the Anvil one rather than flags threaded through it,
// because the two shapes do not answer the same questions: there is no seq here
// to be non-monotonic about, and the figures that matter are the record taxonomy
// and the two clocks. The findings themselves come from the shared renderer —
// this function owns only the metadata header and the adapter line, which are
// the parts that genuinely differ.
//
// ONE FUNCTION FOR BOTH VENUES, AND THE THIRD VENUE IS WHY. It was written a
// second time at M5 stage A and the copy differed in three lines: the header's
// `depth` field, the adapter sentence, and a "not frames" line that DUPLICATED
// one the shared renderer already prints — two spellings of one figure in one
// report, which is precisely what happens when a printer is copied. `depth` is
// venue-optional already (-1 means the tool recorded none), so the header needed
// no fork at all, and the adapter note is a parameter.
void print_symbol_venue_report(const std::string& path, const dc::harness::TraceStats& s,
                               const char* adapter_note) {
    const auto& m = s.meta;
    // `depth` is optional at both venues; -1 is "the tool did not record one",
    // and printing that number would read as a subscription depth of minus one.
    // Binance records none — its subscription is named by `streams` in the
    // header — so it prints "-" for the same reason a Kraken capture that
    // predates the field does.
    char depth[32] = "-";
    if (m.depth >= 0) { std::snprintf(depth, sizeof depth, "%lld", static_cast<long long>(m.depth)); }

    std::printf("dc_replay — DepthCharge trace validator\n\n");
    std::printf("trace     : %s\n", path.c_str());
    std::printf("metadata  : ok  (symbol=%s  depth=%s  tool_version=%s  mode=%s  cycles=%lld)\n",
                m.symbol.c_str(), depth, m.tool_version.c_str(),
                m.capture_mode.empty() ? "-" : m.capture_mode.c_str(),
                static_cast<long long>(m.cycles));
    std::printf("            captured_at=%s\n", m.captured_at.c_str());
    std::printf("            url=%s\n", m.url.c_str());
    // The record taxonomy, the two clocks, and — where the venue has any — the
    // count of records that are not frames. All of it from one renderer, so
    // dc_taxonomy reports the same figures (trace_report.hpp).
    dc::harness::print_trace_findings(stdout, s, "");
    std::printf("adapter     : %s\n", adapter_note);
    std::printf("\nOK\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: dc_replay <trace.ndjson>\n");
        return 1;
    }
    const std::string path = argv[1];
    try {
        const dc::harness::TraceStats stats = dc::harness::read_trace(path);
        // No `default:`, so a fourth venue is a -Wswitch error here rather than
        // a trace this program reads and says nothing about (M5 stage A,
        // deliverable 3).
        switch (stats.meta.venue) {
            case dc::harness::Venue::Anvil:
                print_report(path, stats);
                break;
            // CORRECTED AT M5 STAGE A. This line read "none — stage A's Kraken
            // decoder is a CLASSIFIER; 0 FeedEvents by design", and it has been
            // false since M4 stage B1 put a real adapter behind that decoder.
            // It went unnoticed because it is trivially true OF THIS PROGRAM —
            // `dc_replay` runs `read_trace`, which drives no adapter at any
            // venue — so the sentence was right about the count and wrong about
            // the reason, which is the worse half to be wrong about.
            case dc::harness::Venue::Kraken:
                print_symbol_venue_report(
                    path, stats,
                    "not driven here — dc_replay reads the envelope and names records; "
                    "KrakenTraceDecoder has been an ADAPTER since M4 stage B1 and "
                    "dc_ladder is what runs it");
                break;
            // CORRECTED AT B1's FOLLOW-UP, and it is the identical stale claim
            // this switch already carries a note about for Kraken. The line read
            // "M5 stage A's Binance decoder is a CLASSIFIER; 0 FeedEvents by
            // design (B1 is the adapter)" and B1 made it false the moment it
            // landed — unnoticed for exactly the same reason as Kraken's, that
            // it is trivially true OF THIS PROGRAM. Twice in one milestone is
            // enough to say the general form out loud: a report line describing
            // a stage rather than a behaviour goes stale on the stage that
            // supersedes it, and nothing anywhere fails.
            case dc::harness::Venue::Binance:
                print_symbol_venue_report(
                    path, stats,
                    "not driven here — dc_replay reads the envelope and names records; "
                    "BinanceTraceDecoder has been an ADAPTER since M5 stage B1 and "
                    "dc_ladder is what runs it");
                break;
        }
    } catch (const dc::harness::UnknownVenueError& e) {
        // Not a malformed trace. Separated so a caller can tell "this file is
        // broken" from "this build cannot read this file" — see trace.hpp.
        std::fprintf(stderr, "dc_replay: %s: %s\n", path.c_str(), e.what());
        return 2;
    } catch (const dc::harness::TraceError& e) {
        std::fprintf(stderr, "dc_replay: %s: %s\n", path.c_str(), e.what());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "dc_replay: %s\n", e.what());
        return 1;
    }
    return 0;
}
