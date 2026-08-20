// dc_harness/trace.hpp — replay-trace reader + validator for the host harness.
//
// Reads a DepthCharge capture NDJSON trace (see tools/capture_anvil.py,
// tools/capture_kraken.py and the M0 brief) and returns structural statistics:
// per-kind record counts, cadence from rx_ns, and seq observations. This is
// harness-only code: it uses nlohmann/json (heavyweight, allocates freely) which
// is fine on the desk but must never touch the firmware hot path
// (ARCHITECTURE.md §7, invariant #7). It lives outside engine/ for that reason.
//
// Trace format (one JSON object per line):
//   line 1  metadata : {captured_at, url, tool_version, venue?, ticker?|symbol?, clock?, ...}
//   line 2+ record   : {"rx_ns": <int>, "dir"?: "tx", "frame": { ... }}
//
// TWO VENUES, ONE READER (M4 stage A, ARCHITECTURE §9 2026-08-17). The metadata
// carries a `venue` tag; an ABSENT tag reads as `anvil`, which is what makes the
// tag additive and the four committed Anvil traces byte-identical. The tag
// selects a row in venue.hpp, and the two rules that used to be universal
// because Anvil satisfied them — a required integer `ticker`, and a string
// `type` on every frame — are now read off that row. Everything venue-specific
// past the envelope belongs to a decoder (trace_decoder.hpp), not here: this
// file validates that a line is a capture record and hands on the VERBATIM
// frame text. It does not know what a heartbeat is.
#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <depthcharge/liveness_clock.hpp>
#include "dc_harness/venue.hpp"

namespace dc::harness {

// Disambiguates TraceReader's "path" and "trace text" constructors, both of
// which take a string.
struct InMemoryTag {};
inline constexpr InMemoryTag in_memory{};

// Metadata parsed from line 1. The *_present flags track which fields the file
// actually carried; `complete()` asks whether it carried the ones THIS VENUE
// requires, which is the check TraceReader enforces and the tests assert.
struct TraceMeta {
    std::string captured_at;
    std::string url;
    std::string tool_version;
    std::string capture_mode;   // "baseline" | "reconnect" | "" if absent
    std::int64_t ticker = -1;
    std::int64_t cycles = 1;

    // --- M4 stage A additions, all optional on the wire --------------------
    // The venue tag and the venue it resolved to. An absent tag is Anvil, so
    // `venue` is always meaningful and `venue_present` says whether it was
    // declared or inherited from the rule.
    Venue venue = Venue::Anvil;
    std::string venue_name;     // as written in the file; "" if absent
    bool venue_present = false;

    std::string symbol;         // Kraken's instrument id, e.g. "BTC/USD"
    bool symbol_present = false;
    std::int64_t depth = -1;    // subscribed book depth, when the tool recorded one

    // WHICH CLOCK STAMPED rx_ns — surfaced, never assumed.
    //
    // capture_kraken.py uses perf_counter_ns and says so; capture_anvil.py used
    // monotonic_ns and, before this stage, said nothing. On this box those two
    // differ by four orders of magnitude in resolution (15.0 ms vs 0.0002 ms,
    // measured at stage 0), so a gap distribution from one is not comparable
    // with a gap distribution from the other and a caller that silently compares
    // them is measuring the clock.
    //
    // A trace that does not declare a clock reads back as UNDECLARED, not as
    // monotonic_ns. The inference would be sound today — one tool wrote every
    // undeclared trace in the repo — and it is exactly the kind of sound
    // inference that stops being true without anyone noticing. Both capture
    // tools now declare it, so "undeclared" means "captured before 2026-08-17".
    std::string clock;          // "" when the file did not say
    bool clock_present = false;
    std::string_view clock_name() const {
        return clock_present ? std::string_view(clock) : std::string_view("undeclared");
    }

    bool captured_at_present = false;
    bool url_present = false;
    bool ticker_present = false;
    bool tool_version_present = false;

    // The venue-conditional metadata contract. captured_at / url / tool_version
    // are required of every venue; the instrument identifier is not portable.
    bool complete() const {
        const VenueTraits& t = venue_traits(venue);
        return captured_at_present && url_present && tool_version_present &&
               (!t.requires_ticker || ticker_present) &&
               (!t.requires_symbol || symbol_present);
    }
};

// Structural statistics over the whole trace.
struct TraceStats {
    TraceMeta meta;

    std::size_t frame_count = 0;
    // Records this side SENT, already included in frame_count. Anvil traces
    // have none; a Kraken trace has its subscribe. Every rate and gap figure
    // below is computed over RECEIVED records only — our own upload is not the
    // venue's traffic, and the interval between the subscribe and the first
    // reply is not an inter-message gap. Same rule as tools/tracefile.py.
    std::size_t tx_count = 0;
    std::size_t received_count() const { return frame_count - tx_count; }

    // Records whose frame object carries no string `type`. Zero at any venue
    // whose traits say frames carry one, because the reader rejects those
    // traces. It is 61 of 1,599 in the committed Kraken depth-25 slice, and
    // that number is the whole reason M4 stage A exists.
    std::size_t untyped_records = 0;

    // std::less<> so count() can look up a string_view without materialising a
    // std::string for every probe. Keyed by the DECODER's name for the record's
    // kind, which at Anvil is the wire `type` verbatim and at Kraken is the
    // channel/type pair (trace_decoder.hpp).
    std::map<std::string, std::size_t, std::less<>> kind_counts;

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

    // Number of snapshot records. `mid_stream_snapshots` counts those that are
    // not the first record of the stream: a snapshot arriving mid-trace is a
    // resync, so mid_stream_snapshots >= 1 evidences a reconnect (a windowed
    // reconnect trace need not contain the original on-connect snapshot).
    // Which kind counts as a snapshot is the venue's business, not this file's.
    std::size_t snapshot_count = 0;
    std::size_t mid_stream_snapshots = 0;

    // ---- THE LIVENESS CLOCK: what greys the panel (2026-08-17 ruling) ------
    // Arrivals of the venue's declared liveness signal, and what the
    // self-calibrating threshold does to them. `liveness_firings` is the count
    // that matters: on a healthy trace it must be ZERO, and on a trace with a
    // real outage it must be the number of outages.
    std::size_t liveness_events = 0;
    double median_liveness_gap_ms = 0.0;
    double max_liveness_gap_ms = 0.0;
    double liveness_threshold_ms = 0.0;   // as calibrated by the trace's own signal
    std::size_t liveness_firings = 0;

    // ---- THE AGE CLOCK: what the header will print as a number -------------
    // Records that reach the BOOK. Since the ruling this is age material and
    // nothing here is a threshold: MINA/GBP's healthy 25,843 ms is the proof
    // that no threshold on it can be correct.
    std::size_t book_events = 0;
    double max_book_gap_ms = 0.0;

    // ---- FIXED HISTORICAL REFERENCES, pinned and deliberately unmoving -----
    // Record-arrival and book-event gaps counted against the WITHDRAWN
    // per-venue constants (Anvil 1,000 ms / Kraken 15,000 ms) and against
    // Anvil's 1,000 ms at both venues. **No policy reads these.** They are kept
    // because `dc_taxonomy` pinned them, and a pin whose definition moves is a
    // pin that measures nothing — holding them still is what makes the ruling's
    // effect legible as a delta rather than as a table that changed shape.
    std::size_t watchdog_firings_legacy = 0;
    std::size_t watchdog_firings_at_anvil_threshold = 0;
    std::size_t book_watchdog_firings_legacy = 0;
    std::size_t book_watchdog_firings_at_anvil_threshold = 0;

    double span_seconds() const {
        return static_cast<double>(last_rx_ns - first_rx_ns) / 1e9;
    }
    double frames_per_second() const {
        double s = span_seconds();
        return s > 0.0 ? static_cast<double>(received_count()) / s : 0.0;
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

// A well-formed DepthCharge capture of a venue THIS BUILD does not know.
//
// Its own type because it is a different failure from every other TraceError
// and only the others are bugs: "this file is not a capture" and "this file is
// a capture I cannot read" want different reactions from a caller, and before
// M4 stage A they were the same message — a Kraken trace failed as "metadata
// line missing a required field", which reads as a corrupt file. Named in the
// stage-A brief's known unknowns; this is the answer.
struct UnknownVenueError : TraceError {
    std::string venue_name;

    UnknownVenueError(std::string src, std::size_t ln, std::string venue)
        : TraceError(std::move(src), ln,
                     "capture declares venue \"" + venue +
                         "\", which this build does not know how to read "
                         "(the file itself looks well-formed)"),
          venue_name(std::move(venue)) {}
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
    std::size_t index = 0;      // 1-based record ordinal (metadata line excluded)
    std::size_t line_no = 0;    // 1-based line in the file
    std::int64_t rx_ns = 0;     // capture-tool monotonic clock; see TraceMeta::clock
    std::string_view frame_json;  // borrowed; valid until the next next() call

    // The frame's wire "type", and its wire seq when it carries one. Both are
    // decoded here because the reader already holds the parsed line: without
    // them read_trace() would have to re-parse frame_json for its per-kind and
    // seq statistics, which measured at roughly double the cost of reading the
    // trace at all.
    //
    // `type` is EMPTY on a venue whose frames do not carry one. It is not a
    // substitute for asking the decoder what kind of record this is.
    std::string_view type;      // borrowed; valid until the next next() call
    std::int64_t seq = 0;
    bool has_seq = false;

    // True for a record this side SENT rather than received (`"dir": "tx"`, set
    // by capture_kraken.py for the subscribe). Surfaced because our own upload
    // is not the venue's traffic: counting it as one inflates a record tally by
    // one and puts a bogus interval at the head of every gap distribution. The
    // Python reader has skipped these since stage 0; the C++ one could not see
    // them at all.
    bool is_tx = false;
};

class TraceReader {
public:
    explicit TraceReader(const std::string& path);            // from disk
    TraceReader(std::string_view text, InMemoryTag, std::string name = "<text>");

    const TraceMeta& meta() const noexcept { return meta_; }
    Venue venue() const noexcept { return meta_.venue; }
    const std::string& name() const noexcept { return name_; }
    std::size_t frames_read() const noexcept { return frame_index_; }

    // Advance to the next record. Returns false at end of trace. Throws
    // TraceError with the line number on a malformed line.
    //
    // Structural rules, which are the harness's single definition of a valid
    // trace: the line is a JSON object carrying an integer rx_ns and an object
    // `frame`; the frame carries a string `type` IF THE VENUE'S FRAMES DO
    // (venue.hpp); and rx_ns never decreases, because it comes from a monotonic
    // clock in the capture tool and a decrease is a corrupt trace rather than a
    // market phenomenon.
    //
    // The `type` rule going venue-conditional is the only relaxation M4 stage A
    // makes to the 2026-08-07 rule, and it is a relaxation for Kraken only:
    // Anvil traces are held to exactly the rule they were held to before, so a
    // frame that lost its `type` still fails on the venue where that means the
    // capture is broken.
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
