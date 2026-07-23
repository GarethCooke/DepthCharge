// dc_replay — DepthCharge M0 replay-trace validator.
//
// Reads an Anvil capture NDJSON trace, validates its line structure, and prints
// per-kind frame counts plus cadence and seq observations. No book, no adapter
// logic yet (M1) — this is the ground-truth reader the later milestones build on.
//
//   usage: dc_replay <trace.ndjson>
//
// Exit 0 if the trace is structurally valid; 1 on bad usage or a malformed trace
// (with the offending line number).
#include <cstdio>
#include <exception>
#include <string>

#include "dc_harness/trace.hpp"

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

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: dc_replay <trace.ndjson>\n");
        return 1;
    }
    const std::string path = argv[1];
    try {
        const dc::harness::TraceStats stats = dc::harness::read_trace(path);
        print_report(path, stats);
    } catch (const dc::harness::TraceError& e) {
        std::fprintf(stderr, "dc_replay: %s: %s\n", path.c_str(), e.what());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "dc_replay: %s\n", e.what());
        return 1;
    }
    return 0;
}
