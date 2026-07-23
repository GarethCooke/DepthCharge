// dc_harness/trace.hpp — replay-trace reader + validator for the host harness.
//
// Reads an Anvil capture NDJSON trace (see tools/capture_anvil.py and the M0
// brief) and returns structural statistics: per-kind frame counts, cadence from
// rx_ns, and seq observations. This is harness-only code: it uses nlohmann/json
// (heavyweight, allocates freely) which is fine on the desk but must never touch
// the firmware hot path (ARCHITECTURE.md §7, invariant #7). It lives outside
// engine/ for exactly that reason.
//
// Trace format (one JSON object per line):
//   line 1  metadata : {captured_at, url, ticker, tool_version, ...}
//   line 2+ frame     : {"rx_ns": <int>, "frame": {"type": <string>, ...}}
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace dc::harness {

// Metadata parsed from line 1. The four *_present flags track the M0-required
// fields; `complete()` is the golden the tests assert.
struct TraceMeta {
    std::string captured_at;
    std::string url;
    std::string tool_version;
    std::string capture_mode;   // "baseline" | "reconnect" | "" if absent
    std::int64_t ticker = -1;
    std::int64_t cycles = 1;

    bool captured_at_present = false;
    bool url_present = false;
    bool ticker_present = false;
    bool tool_version_present = false;

    bool complete() const {
        return captured_at_present && url_present && ticker_present &&
               tool_version_present;
    }
};

// Structural statistics over the whole trace.
struct TraceStats {
    TraceMeta meta;

    std::size_t frame_count = 0;
    std::map<std::string, std::size_t> kind_counts;  // keyed by wire "type"

    std::int64_t first_rx_ns = 0;
    std::int64_t last_rx_ns = 0;
    double median_gap_ms = 0.0;
    double max_gap_ms = 0.0;

    // seq observations. Anvil's wire seq is a global counter, so a single
    // ticker's received subsequence is NOT monotonic; the harness reports this
    // rather than treating it as an error (see M0 NOTES).
    std::size_t seq_frames = 0;
    std::size_t seq_backward_steps = 0;
    std::int64_t seq_min = 0;
    std::int64_t seq_max = 0;
    bool seq_monotonic = true;

    // Number of `snapshot` frames. `mid_stream_snapshots` counts those that are
    // not the first frame of the stream: a snapshot arriving mid-trace is a
    // resync, so mid_stream_snapshots >= 1 evidences a reconnect (a windowed
    // reconnect trace need not contain the original on-connect snapshot).
    std::size_t snapshot_count = 0;
    std::size_t mid_stream_snapshots = 0;

    double span_seconds() const {
        return static_cast<double>(last_rx_ns - first_rx_ns) / 1e9;
    }
    double frames_per_second() const {
        double s = span_seconds();
        return s > 0.0 ? static_cast<double>(frame_count) / s : 0.0;
    }
    std::size_t count(const std::string& kind) const {
        auto it = kind_counts.find(kind);
        return it == kind_counts.end() ? 0 : it->second;
    }
};

// Thrown on any structural violation; carries the 1-based line number.
struct TraceError : std::runtime_error {
    std::size_t line_no;
    TraceError(std::size_t ln, const std::string& msg)
        : std::runtime_error("line " + std::to_string(ln) + ": " + msg),
          line_no(ln) {}
};

// Read + validate a trace from disk. Throws TraceError on malformed structure,
// std::runtime_error if the file cannot be opened.
TraceStats read_trace(const std::string& path);

// Same, over in-memory text (for tests that exercise malformed inputs without
// fixture files). `name` is only used in error context.
TraceStats read_trace_text(std::string_view text, const std::string& name = "<text>");

}  // namespace dc::harness
