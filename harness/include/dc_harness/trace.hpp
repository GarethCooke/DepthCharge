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
#include <istream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace dc::harness {

// Disambiguates TraceReader's "path" and "trace text" constructors, both of
// which take a string.
struct InMemoryTag {};
inline constexpr InMemoryTag in_memory{};

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
    // std::less<> so count() can look up a string_view without materialising a
    // std::string for every probe.
    std::map<std::string, std::size_t, std::less<>> kind_counts;  // keyed by wire "type"

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
    std::size_t count(std::string_view kind) const {
        auto it = kind_counts.find(kind);
        return it == kind_counts.end() ? 0 : it->second;
    }
};

// Thrown on any structural violation; carries the 1-based line number and, when
// the caller named the source, which trace it came from — so a failure across
// several fixtures says which one.
struct TraceError : std::runtime_error {
    std::size_t line_no;
    std::string source;

    TraceError(std::size_t ln, const std::string& msg) : TraceError(std::string{}, ln, msg) {}

    TraceError(std::string src, std::size_t ln, const std::string& msg)
        : std::runtime_error((src.empty() ? std::string{} : src + ": ") + "line " +
                             std::to_string(ln) + ": " + msg),
          line_no(ln),
          source(std::move(src)) {}
};

// Read + validate a trace from disk. Throws TraceError on malformed structure,
// std::runtime_error if the file cannot be opened.
TraceStats read_trace(const std::string& path);

// Same, over in-memory text (for tests that exercise malformed inputs without
// fixture files). `name` is only used in error context.
TraceStats read_trace_text(std::string_view text, const std::string& name = "<text>");

// ---------------------------------------------------------------------------
// Streaming reader (M1): hands frames to the adapter one at a time.
//
// read_trace() above answers "what is in this file"; this answers "replay it".
// The distinction that matters is `frame_json`: it is the VERBATIM wire text,
// sliced out of the capture line, never re-serialised. The adapter under test
// must see exactly the bytes the server sent — key order, spacing and all —
// or the harness would be validating a parser against its own output.
// ---------------------------------------------------------------------------

struct TraceFrame {
    std::size_t index = 0;      // 1-based frame ordinal (metadata line excluded)
    std::size_t line_no = 0;    // 1-based line in the file
    std::int64_t rx_ns = 0;     // capture-tool monotonic clock
    std::string_view frame_json;  // borrowed; valid until the next next() call

    // The frame's wire "type", and its wire seq when it carries one. Both are
    // decoded here because the reader already holds the parsed line: without
    // them read_trace() would have to re-parse frame_json for its per-kind and
    // seq statistics, which measured at roughly double the cost of reading the
    // trace at all.
    std::string_view type;      // borrowed; valid until the next next() call
    std::int64_t seq = 0;
    bool has_seq = false;
};

class TraceReader {
public:
    explicit TraceReader(const std::string& path);            // from disk
    TraceReader(std::string_view text, InMemoryTag, std::string name = "<text>");

    const TraceMeta& meta() const noexcept { return meta_; }
    const std::string& name() const noexcept { return name_; }
    std::size_t frames_read() const noexcept { return frame_index_; }

    // Advance to the next frame. Returns false at end of trace. Throws
    // TraceError with the line number on a malformed line.
    //
    // Structural rules, which are the harness's single definition of a valid
    // trace: the line is a JSON object carrying an integer rx_ns and an object
    // `frame`; the frame carries a string `type`; and rx_ns never decreases,
    // because it comes from a monotonic clock in the capture tool and a
    // decrease is a corrupt trace rather than a market phenomenon.
    bool next(TraceFrame& out);

private:
    void read_meta();

    std::unique_ptr<std::istream> owned_;
    std::istream* in_ = nullptr;
    std::string name_;
    std::string line_;
    std::string type_;          // backs TraceFrame::type across the next() call
    TraceMeta meta_;
    std::size_t line_no_ = 0;
    std::size_t frame_index_ = 0;
    std::int64_t last_rx_ns_ = 0;
    bool have_rx_ = false;
};

// Slice the verbatim value of the top-level "frame" key out of a capture line.
// Returns an empty view if the key is absent or the value is not a JSON object.
// String-aware (a brace inside "makerId" cannot fool it), which is why this is
// a scanner and not a find('{').
std::string_view slice_frame_json(std::string_view line) noexcept;

}  // namespace dc::harness
