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
//   line 2+ record   : {"rx_ns": <int>, "dir"?: "tx", "kind"?: <form>, "frame": ...}
//
// THREE RECORD FORMS, AND TWO OF THEM ARE NOT FRAMES (M5 stage A). Until this
// stage every record in every trace was one shape — a verbatim JSON object the
// venue sent over the WebSocket — and `frame` was always an object. Binance
// produces two things that shape cannot hold:
//
//   a REST fetch    {"rx_ns": N, "kind": "rest", "req": {...}, "frame": <body>}
//   a control frame {"rx_ns": N, "kind": "control", "ctl": {...}, "frame": null}
//
// A REST body is not something the venue *said*: it is a fetch this client chose
// to make, and its meaning is inseparable from the request, so the record
// carries `req` as well. A ping payload is not JSON at all — arbitrary bytes,
// carried base64 in `ctl` — so its record carries no frame whatsoever.
//
// AN ABSENT `kind` IS `frame`, which is what every record in every trace
// committed before 2026-08-24 already is. That is the same additive rule the
// `venue` tag was granted at M4 stage A, and it is what keeps the four Anvil
// traces and the six Kraken ones byte-identical through this change: nothing in
// them acquires a key, and no golden moves.
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
    //
    // COUNTED OVER FRAME-BEARING RECORDS ONLY (M5 stage A). A control record has
    // no frame, so "its frame carries no type" is not a statement about it; it
    // is counted in `control_records` instead. Nothing moves at Anvil or Kraken,
    // where no control record exists.
    std::size_t untyped_records = 0;

    // --- the two forms that are not frames (M5 stage A) --------------------
    // Both are included in `frame_count` and in every timing figure, because
    // they ARE records of the session: a REST fetch is 100 KB of the byte
    // budget and a ping is what the panel's grey is armed on. Zero at Anvil and
    // at Kraken.
    std::size_t rest_records = 0;
    std::size_t control_records = 0;
    // Control records that recorded a reply going back out. THE EVIDENCE THE
    // 2026-08-25 RULING RESTS ON: a ping that is not answered ends the session
    // 60 s later, so "the transport answers pings" has to be a number read off
    // a committed file rather than a property of a code path nobody exercised.
    std::size_t control_replied = 0;

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
    // The multiplier THAT CLOCK USED, taken from its own `policy()` rather than
    // looked up a second time (M5 stage C). The report prints the two side by
    // side — "N ms (Kx the observed median)" — and reading K from the venue
    // table while the threshold came from the clock is two homes for one number,
    // which is the drift ARCHITECTURE §9 keeps catching. They cannot disagree if
    // only one of them is ever read.
    double liveness_multiple = 0.0;
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

// WHICH OF THE THREE SHAPES A RECORD IS (M5 stage A, deliverable 1).
//
// The wire spelling is the optional `kind` key; an ABSENT key is `Frame`. The
// enum is called RecordForm and not RecordKind because `RecordKind` is already
// the DECODER's answer about a record (trace_decoder.hpp) and the two are
// different questions: the form is what the READER can see from the envelope
// alone, the kind is what the venue's dialect makes of it. The near-collision is
// the wire's, not this file's — the key was named `kind` at M5 stage 0 and the
// committed slices cannot be renamed.
enum class RecordForm : std::uint8_t { Frame, Rest, Control };

// The wire spelling of a form, which is "" for Frame. These are the SAME
// strings tools/tracefile.py's KIND_KEY scan produces, for the reason every
// other cross-language string here is shared: two readers measuring one
// committed file must agree on what they are counting.
constexpr std::string_view record_form_name(RecordForm f) noexcept {
    switch (f) {
        case RecordForm::Frame:   return {};
        case RecordForm::Rest:    return "rest";
        case RecordForm::Control: return "control";
    }
    // NOT `return {}`, which is Frame's answer. A value outside the enumerators
    // reaching this line would then be reported as the one form that means "an
    // ordinary WebSocket text frame" — a confident wrong answer of exactly the
    // kind `unhandled_venue` exists to stop one level up (venue.hpp). It is
    // unreachable through `TraceReader::next`, which rejects any other spelling
    // by name; this is what it looks like if it ever stops being.
    return "?";
}

// THE FETCH THAT PRODUCED A REST BODY. Carried because a REST record is a
// transcript PLUS A QUESTION: every other line in every other trace is
// something the venue said unprompted, and a body whose URL and `limit` are
// unknown cannot be reconciled against anything — a `limit=100` book and a
// `limit=1000` book are different claims about the same instrument, and at
// BTCUSDT the first is wrong within 90 seconds (NOTES-binance.md).
struct RestRequest {
    std::string method;          // "GET"
    std::string url;             // the exact URL fetched, verbatim
    std::int64_t limit = -1;     // the `limit` query parameter
    std::int64_t weight = -1;    // the venue's declared request weight, -1 if absent
    std::int64_t status = -1;    // HTTP status, -1 if the tool did not record one
    std::string used_weight_1m;  // the venue's used-weight header, verbatim, "" if absent

    // WHY THERE IS NO BODY, when there is none. A failed fetch is recorded
    // rather than dropped, and a record that said only "no body" would be
    // indistinguishable from a reader that lost one. Empty when the fetch
    // succeeded.
    std::string error;

    // WHEN THE FETCH ACTUALLY HAPPENED, which is not the record's rx_ns.
    // The fetch runs on a worker thread so it cannot punch a one-second hole in
    // the gap distribution, and the record is stamped when the main loop writes
    // it — so the record lands ~1-1.5 s AFTER the instant it describes. This is
    // the true span, and `TraceRecord::event_ns` is `recv_ns`.
    std::int64_t sent_ns = 0;
    std::int64_t recv_ns = 0;
};

// A WEBSOCKET CONTROL FRAME. Not JSON, and therefore not a `frame` at all: the
// record carries `"frame": null` and everything it knows is here.
//
// `replied_ns` IS NOT DECORATION. It is the evidence that the pong went back,
// and the 2026-08-25 ruling that arms this venue's grey on the ping rests on
// that path being *exercised* rather than merely present — a client that
// answered nothing would look identical on this side of the socket until the
// venue closed it 60 s later.
struct ControlFrame {
    std::string opcode;        // "ping" | "pong" | "close" — the wire `op`
    std::string payload_b64;   // base64: the payload is arbitrary BYTES, not text
    std::int64_t payload_len = 0;

    // When the control frame ARRIVED — again not the record's rx_ns, and here
    // the difference is not subtle: three pings 20 s apart share one rx_ns in
    // `binance_atomeur_deepseed_20260824.ndjson`, because they were flushed
    // together. A liveness clock stamped from rx_ns would see three arrivals
    // separated by 0 ms and calibrate its threshold off a cadence that never
    // happened. `TraceRecord::event_ns` is this field.
    std::int64_t recv_ns = 0;

    std::int64_t replied_ns = 0;  // the wire `pong_ns`
    bool replied = false;         // whether the tool recorded a reply at all
};

// One record. Named for what it is: after M5 stage A a record need not be a
// frame, and two of the three forms are not. It was `TraceFrame` until this
// stage, and the rename is deliberately not a typedef — a decoder still written
// against the old name must fail to build rather than quietly classify a
// control record as though it had a frame.
struct TraceRecord {
    RecordForm form = RecordForm::Frame;

    std::size_t index = 0;      // 1-based record ordinal (metadata line excluded)
    std::size_t line_no = 0;    // 1-based line in the file
    std::int64_t rx_ns = 0;     // capture-tool monotonic clock; see TraceMeta::clock

    // THE INSTANT THIS RECORD DESCRIBES, which is `rx_ns` for a WS frame and is
    // NOT for the other two forms — see RestRequest::recv_ns and
    // ControlFrame::recv_ns for why, with the file that proves it.
    //
    // `rx_ns` still orders the file, still bounds the span, and is still what
    // every gap distribution and every pinned watchdog column is computed from;
    // nothing about those moves, because at Anvil and Kraken the two are the
    // same number. `event_ns` exists for the one consumer that must have the
    // real instant: the liveness clock.
    std::int64_t event_ns = 0;

    // EMPTY when this record has no frame. That is always true of a Control
    // record — a ping payload is not JSON — and it is SOMETIMES true of a Rest
    // one: `capture_binance.py` records a failed fetch rather than dropping it,
    // because "the snapshot did not arrive" is a fact about the capture window.
    // `rest.status` and `rest.error` say why.
    std::string_view frame_json;  // borrowed; valid until the next next() call
    bool has_frame() const noexcept { return !frame_json.empty(); }

    // The frame's wire "type", and its wire seq when it carries one. Both are
    // decoded here because the reader already holds the parsed line: without
    // them read_trace() would have to re-parse frame_json for its per-kind and
    // seq statistics, which measured at roughly double the cost of reading the
    // trace at all.
    //
    // `type` is EMPTY on a venue whose frames do not carry one, and on every
    // record that is not a frame. It is not a substitute for asking the decoder
    // what kind of record this is.
    std::string_view type;      // borrowed; valid until the next next() call
    std::int64_t seq = 0;
    bool has_seq = false;

    // True for a record this side SENT rather than received (`"dir": "tx"`, set
    // by capture_kraken.py for the subscribe). Surfaced because our own upload
    // is not the venue's traffic: counting it as one inflates a record tally by
    // one and puts a bogus interval at the head of every gap distribution. The
    // Python reader has skipped these since stage 0; the C++ one could not see
    // them at all.
    //
    // A REST record is NOT tx, and the wire agrees: `capture_binance.py` writes
    // no `dir` on one. `dir: tx` means a frame this side put on THIS socket; a
    // REST fetch is a different conversation entirely, which is what `req`
    // exists to describe.
    bool is_tx = false;

    // Exactly the one matching `form` is populated. Held by value rather than as
    // views into the reader, because both are rare (3 to 11 per committed slice)
    // and a lifetime rule bought nothing on a path that is not hot.
    RestRequest rest;
    ControlFrame ctl;
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
    // trace: the line is a JSON object carrying an integer rx_ns and a `frame`;
    // the frame carries a string `type` IF THE VENUE'S FRAMES DO (venue.hpp);
    // and rx_ns never decreases, because it comes from a monotonic clock in the
    // capture tool and a decrease is a corrupt trace rather than a market
    // phenomenon.
    //
    // The `type` rule going venue-conditional is the only relaxation M4 stage A
    // makes to the 2026-08-07 rule, and it is a relaxation for Kraken only:
    // Anvil traces are held to exactly the rule they were held to before, so a
    // frame that lost its `type` still fails on the venue where that means the
    // capture is broken.
    //
    // M5 STAGE A adds the `kind` discriminator and one rule per form. They are
    // stated here because tools/tracefile.py implements the same list and the
    // two are held to it by a shared corpus (harness/tests/record_shapes.json):
    //
    //   absent      `frame` is an object, and the `type` rule above applies.
    //   "rest"      `frame` is an object — the response body. `req` is an object
    //               carrying at least a string `url`, an integer `limit` and an
    //               integer `recv_ns`. The `type` rule does NOT apply: a REST
    //               body is not a frame the venue sent, so holding it to a
    //               frame's contract would be the reader teaching the wire.
    //   "control"   `frame` is present and NULL. `ctl` is an object carrying at
    //               least a string `op` and an integer `recv_ns`.
    //   anything else is refused by name. An unknown kind is a capture written
    //               by a newer tool, and reading it as a plain frame would file
    //               a thing this build does not understand in a counter that
    //               claims to describe the venue's wire.
    bool next(TraceRecord& out);

private:
    void read_meta();

    std::unique_ptr<std::istream> owned_;
    std::istream* in_ = nullptr;
    std::string name_;
    std::string line_;
    std::string type_;          // backs TraceRecord::type across the next() call
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
