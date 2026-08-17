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

// Kraken's report. A separate function rather than flags threaded through the
// Anvil one, because the two venues do not answer the same questions: there is
// no seq here to be non-monotonic about, and the figures that matter are the
// record taxonomy and the two clocks. The findings themselves are printed by
// the shared renderer — this function owns only the metadata header, which is
// the part that genuinely differs.
void print_kraken_report(const std::string& path, const dc::harness::TraceStats& s) {
    const auto& m = s.meta;
    // `depth` is optional even at Kraken; -1 is "the tool did not record one",
    // and printing that number would read as a subscription depth of minus one.
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
    dc::harness::print_trace_findings(stdout, s, "");
    std::printf("adapter    : none — stage A's Kraken decoder is a CLASSIFIER; "
                "0 FeedEvents by design\n");
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
        switch (stats.meta.venue) {
            case dc::harness::Venue::Anvil:  print_report(path, stats); break;
            case dc::harness::Venue::Kraken: print_kraken_report(path, stats); break;
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
