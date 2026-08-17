// dc_harness/trace.cpp — implementation of the replay-trace reader.
//
// nlohmann/json is included in exactly this one translation unit so the heavy
// header is compiled once and linked into both dc_replay and dc_tests. Harness
// only — never the firmware path.
//
// There is ONE definition of a valid trace here, in TraceReader::next.
// read_trace() is a statistics pass driven by that same reader, not a second
// parser: when the two were written separately they drifted, and a frame line
// carrying no "type" ended up rejected by one and accepted by the other.
#include "dc_harness/trace.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

// The reader owns the ENVELOPE; the decoder owns the DIALECT. accumulate()
// below is a statistics pass, and naming a record's kind is a dialect question
// the moment there are two venues — so it asks, rather than assuming every
// venue puts its kind in a field called `type`.
#include "dc_harness/trace_decoder.hpp"

namespace dc::harness {
namespace {

using nlohmann::json;

// --- metadata ----------------------------------------------------------------

// Take one optional metadata field, reporting whether it was present. Six
// near-identical find / type-check / assign / set-flag blocks collapse into
// calls to this.
template <typename T, typename Pred>
bool take_field(const json& j, const char* key, T& out, Pred is_wanted) {
    const auto it = j.find(key);
    if (it == j.end() || !is_wanted(*it)) { return false; }
    out = it->get<T>();
    return true;
}

bool is_str(const json& v) { return v.is_string(); }
bool is_int(const json& v) { return v.is_number_integer(); }

void parse_meta(const json& j, const std::string& src, std::size_t line_no, TraceMeta& meta) {
    if (!j.is_object()) {
        throw TraceError(src, line_no, "metadata line is not a JSON object");
    }
    meta.captured_at_present = take_field(j, "captured_at", meta.captured_at, is_str);
    meta.url_present = take_field(j, "url", meta.url, is_str);
    meta.ticker_present = take_field(j, "ticker", meta.ticker, is_int);
    meta.tool_version_present = take_field(j, "tool_version", meta.tool_version, is_str);
    take_field(j, "capture_mode", meta.capture_mode, is_str);
    take_field(j, "cycles", meta.cycles, is_int);

    // The venue tag, and the rule that makes it additive: absent reads as
    // `anvil`, so every trace captured before 2026-08-17 is still valid and
    // still means what it meant.
    meta.venue_present = take_field(j, "venue", meta.venue_name, is_str);
    if (!venue_from_name(meta.venue_name, meta.venue)) {
        // Not "malformed". The file is fine; this build is the limitation.
        throw UnknownVenueError(src, line_no, meta.venue_name);
    }
    meta.symbol_present = take_field(j, "symbol", meta.symbol, is_str);
    take_field(j, "depth", meta.depth, is_int);
    meta.clock_present = take_field(j, "clock", meta.clock, is_str);

    if (!meta.complete()) {
        const VenueTraits& t = venue_traits(meta.venue);
        std::string need = "captured_at, url, tool_version";
        if (t.requires_ticker) { need += ", ticker"; }
        if (t.requires_symbol) { need += ", symbol"; }
        throw TraceError(src, line_no,
                         "metadata line missing a required field for venue \"" +
                             std::string(t.name) + "\" (need " + need + ")");
    }
}

// --- JSON string-state, once ---------------------------------------------------

// The escape/quote handling both scanners below need. Feed it one character at
// a time; it reports whether that character was string *content*, which is the
// only question either scanner asks.
class StringScan {
public:
    // True when `c` was consumed as content inside a string literal. The quotes
    // themselves are reported false, so a caller can see where strings begin and
    // end by watching in_string() across the call.
    bool consume(char c) noexcept {
        if (in_string_) {
            if (escaped_) {
                escaped_ = false;
            } else if (c == '\\') {
                escaped_ = true;
            } else if (c == '"') {
                in_string_ = false;
                return false;
            }
            return true;
        }
        if (c == '"') {
            in_string_ = true;
            return false;
        }
        return false;
    }
    bool in_string() const noexcept { return in_string_; }

private:
    bool in_string_ = false;
    bool escaped_ = false;
};

// [start, end) of the JSON object beginning at `start`, or npos if unbalanced.
std::size_t object_end(std::string_view s, std::size_t start) noexcept {
    StringScan scan;
    std::size_t depth = 0;
    for (std::size_t i = start; i < s.size(); ++i) {
        const char c = s[i];
        if (scan.consume(c) || scan.in_string()) { continue; }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            // Guarded rather than trusting the caller to start at a '{':
            // decrementing an unsigned zero wraps to SIZE_MAX and sends the scan
            // hunting for a close brace that can never balance.
            if (depth == 0) { return std::string_view::npos; }
            if (--depth == 0) { return i + 1; }
        }
    }
    return std::string_view::npos;
}

std::size_t skip_ws(std::string_view s, std::size_t i) noexcept {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) { ++i; }
    return i;
}

bool is_blank(std::string_view s) noexcept {
    return s.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

}  // namespace

std::string_view slice_frame_json(std::string_view line) noexcept {
    // Walk the line once, tracking string state so that braces and the literal
    // text "frame" inside a JSON string (an order id, a URL) cannot be mistaken
    // for structure. Only depth-1 keys — a string immediately followed by ':' —
    // are candidates.
    StringScan scan;
    std::size_t depth = 0;
    std::size_t str_start = std::string_view::npos;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        const bool was_in_string = scan.in_string();
        if (scan.consume(c)) { continue; }

        if (was_in_string) {  // this character closed a string
            const bool is_frame_key =
                depth == 1 && line.substr(str_start, i - str_start) == "frame";
            if (is_frame_key) {
                std::size_t j = skip_ws(line, i + 1);
                if (j < line.size() && line[j] == ':') {  // a key, not a value
                    j = skip_ws(line, j + 1);
                    if (j < line.size() && line[j] == '{') {
                        const std::size_t end = object_end(line, j);
                        if (end != std::string_view::npos) {
                            return line.substr(j, end - j);
                        }
                    }
                    return {};  // "frame" is present but not a JSON object
                }
            }
            str_start = std::string_view::npos;
            continue;
        }
        if (scan.in_string()) {  // this character opened one
            str_start = i + 1;
            continue;
        }
        switch (c) {
            case '{':
            case '[':
                ++depth;
                break;
            case '}':
            case ']':
                if (depth > 0) { --depth; }
                break;
            default:
                break;
        }
    }
    return {};
}

// --- streaming reader --------------------------------------------------------

TraceReader::TraceReader(const std::string& path) : name_(path) {
    auto file = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*file) {
        throw std::runtime_error("cannot open trace: " + path);
    }
    owned_ = std::move(file);
    in_ = owned_.get();
    read_meta();
}

TraceReader::TraceReader(std::string_view text, InMemoryTag, std::string name)
    : name_(std::move(name)) {
    owned_ = std::make_unique<std::istringstream>(std::string(text));
    in_ = owned_.get();
    read_meta();
}

void TraceReader::read_meta() {
    while (std::getline(*in_, line_)) {
        ++line_no_;
        if (is_blank(line_)) { continue; }
        const json j = json::parse(line_, /*cb=*/nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded()) {
            throw TraceError(name_, line_no_, "invalid JSON");
        }
        parse_meta(j, name_, line_no_, meta_);
        return;
    }
    throw TraceError(name_, 0, "empty trace (no metadata line)");
}

bool TraceReader::next(TraceFrame& out) {
    while (std::getline(*in_, line_)) {
        ++line_no_;
        if (is_blank(line_)) { continue; }

        const json j = json::parse(line_, /*cb=*/nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded()) {
            throw TraceError(name_, line_no_, "invalid JSON");
        }
        if (!j.is_object()) {
            throw TraceError(name_, line_no_, "frame line is not a JSON object");
        }
        const auto rx_it = j.find("rx_ns");
        if (rx_it == j.end() || !rx_it->is_number_integer()) {
            throw TraceError(name_, line_no_, "frame line missing integer rx_ns");
        }
        const auto fr_it = j.find("frame");
        if (fr_it == j.end() || !fr_it->is_object()) {
            throw TraceError(name_, line_no_, "frame line missing object 'frame'");
        }
        // Venue-conditional (M4 stage A). Anvil frames all carry a string
        // `type` and are still held to it; Kraken's heartbeat, subscribe ack and
        // subscribe carry none, and rejecting them would have rejected exactly
        // the three frames the stage-0 findings rest on.
        const auto type_it = fr_it->find("type");
        const bool have_type = type_it != fr_it->end() && type_it->is_string();
        if (!have_type && venue_traits(meta_.venue).frames_carry_type) {
            throw TraceError(name_, line_no_, "frame object missing string 'type'");
        }

        const std::int64_t rx_ns = rx_it->get<std::int64_t>();
        // rx_ns comes from a monotonic clock in the capture tool; a decrease is
        // a corrupt trace, not a market phenomenon.
        if (have_rx_ && rx_ns < last_rx_ns_) {
            throw TraceError(name_, line_no_,
                             "rx_ns went backwards (non-monotonic capture clock)");
        }

        const std::string_view verbatim = slice_frame_json(line_);
        if (verbatim.empty()) {
            throw TraceError(name_, line_no_, "could not slice verbatim 'frame' text");
        }

        last_rx_ns_ = rx_ns;
        have_rx_ = true;
        type_ = have_type ? type_it->get<std::string>() : std::string{};
        ++frame_index_;

        // `"dir": "tx"` marks a record this side sent (capture_kraken.py's
        // subscribe). Anything else, including its absence, is a receipt.
        const auto dir_it = j.find("dir");
        const bool is_tx =
            dir_it != j.end() && dir_it->is_string() && dir_it->get<std::string>() == "tx";

        out.index = frame_index_;
        out.line_no = line_no_;
        out.rx_ns = rx_ns;
        out.frame_json = verbatim;
        out.type = type_;
        out.is_tx = is_tx;
        out.seq = 0;
        out.has_seq = false;
        if (const auto seq_it = fr_it->find("seq");
            seq_it != fr_it->end() && seq_it->is_number_integer()) {
            out.seq = seq_it->get<std::int64_t>();
            out.has_seq = true;
        }
        return true;
    }
    return false;
}

// --- whole-trace statistics --------------------------------------------------

namespace {

// Everything read_trace() reports, accumulated over TraceReader's frames. The
// reader has already decoded `type` and `seq`, so nothing here re-parses the
// line — doing so measured at roughly double the cost of reading the trace.
TraceStats accumulate(TraceReader& reader) {
    TraceStats stats;
    stats.meta = reader.meta();
    const Venue venue = reader.venue();
    // The two FIXED historical references. Nothing decides on these; they exist
    // so the pinned columns keep measuring what they measured when pinned.
    const double legacy_gap_ms = venue_traits(venue).legacy_book_threshold_ms;
    const double anvil_gap_ms = venue_traits(Venue::Anvil).legacy_book_threshold_ms;

    // The clock that actually matters since the 2026-08-17 ruling. It is fed
    // ONLY the venue's declared liveness signal and calibrates itself from that
    // signal's own observed median — see liveness_clock.hpp for why the
    // threshold cannot be a constant.
    LivenessClock liveness;
    std::vector<double> liveness_gaps;
    std::int64_t prev_liveness_ns = 0;
    bool have_prev_liveness = false;

    std::vector<double> gaps;
    std::int64_t prev_seq = 0;
    bool have_prev_seq = false;
    RecordClassifier classifier(venue);
    std::int64_t prev_book_rx_ns = 0;
    bool have_prev_book = false;

    TraceFrame frame;
    while (reader.next(frame)) {
        ++stats.frame_count;
        if (frame.type.empty()) { ++stats.untyped_records; }
        if (frame.is_tx) {
            // Counted as a record, excluded from every timing figure: our own
            // upload is not the venue's traffic. Anvil traces contain none, so
            // nothing about their statistics moves.
            ++stats.tx_count;
            ++stats.kind_counts[std::string(classifier.classify(frame).name)];
            continue;
        }

        const bool is_first_frame = stats.received_count() == 1;
        if (is_first_frame) {
            stats.first_rx_ns = frame.rx_ns;
        } else {
            const double gap_ms = static_cast<double>(frame.rx_ns - stats.last_rx_ns) / 1e6;
            gaps.push_back(gap_ms);
            // The M1 replay rule, applied at two thresholds. Counting raw
            // firings rather than the driver's folded episodes: the question
            // deliverable 4 asks is how many disconnects a threshold INVENTS,
            // and folding them would report a run of spurious greys as one.
            if (gap_ms > legacy_gap_ms) { ++stats.watchdog_firings_legacy; }
            if (gap_ms > anvil_gap_ms) { ++stats.watchdog_firings_at_anvil_threshold; }
        }
        stats.last_rx_ns = frame.rx_ns;

        const RecordKind kind = classifier.classify(frame);
        ++stats.kind_counts[std::string(kind.name)];
        if (kind.is_snapshot) {
            ++stats.snapshot_count;
            // THE RESYNC RULE, and it is one rule for every venue: a snapshot
            // with a book event before it in the trace is a resync.
            //
            // It used to be "a snapshot that is not the trace's first record",
            // which is Anvil's shape, not a general one — Kraken's on-connect
            // snapshot arrives third (status, subscribe ack, snapshot) and that
            // rule calls every Kraken capture a reconnect. Two venue-specific
            // repairs were written and both were wrong in different directions
            // ("first snapshot after a subscribe" misses the reconnect
            // capture's second subscription; "not the first acked subscription"
            // misses a WINDOWED reconnect slice, which carries only one ack).
            // Asking what came BEFORE gets all four cases and needs no venue
            // split, because it is the actual question: a snapshot re-baselines
            // a book, and it is a resync exactly when there was a book to
            // re-baseline. It reproduces Anvil's answers on all four committed
            // traces, including the reconnect WINDOW that contains no on-connect
            // snapshot at all.
            if (stats.book_events > 0) { ++stats.mid_stream_snapshots; }
        }

        // THE LIVENESS CLOCK — what greys the panel since the 2026-08-17
        // ruling. The threshold is compared against the median the signal
        // itself has established, so a firing here means this venue's own
        // cadence stopped, not that a market went quiet.
        //
        // The threshold is sampled BEFORE this arrival is folded in, which is
        // the honest order: the question a watchdog asks is whether the gap
        // that just ended was tolerable given what was known while it was open.
        if (kind.is_liveness) {
            ++stats.liveness_events;
            if (have_prev_liveness) {
                const double lg_ms =
                    static_cast<double>(frame.rx_ns - prev_liveness_ns) / 1e6;
                liveness_gaps.push_back(lg_ms);
                if (lg_ms > stats.max_liveness_gap_ms) { stats.max_liveness_gap_ms = lg_ms; }
                if (lg_ms > liveness.threshold_ms()) { ++stats.liveness_firings; }
            }
            liveness.on_liveness(frame.rx_ns);
            prev_liveness_ns = frame.rx_ns;
            have_prev_liveness = true;
        }

        // THE AGE CLOCK. Book events, and no threshold on them — the ruling's
        // first clause. The two legacy counters below are the WITHDRAWN
        // constants, held still so the pinned columns stay comparable.
        if (kind.is_book_event) {
            ++stats.book_events;
            if (have_prev_book) {
                const double bg_ms =
                    static_cast<double>(frame.rx_ns - prev_book_rx_ns) / 1e6;
                if (bg_ms > stats.max_book_gap_ms) { stats.max_book_gap_ms = bg_ms; }
                if (bg_ms > legacy_gap_ms) { ++stats.book_watchdog_firings_legacy; }
                if (bg_ms > anvil_gap_ms) { ++stats.book_watchdog_firings_at_anvil_threshold; }
            }
            prev_book_rx_ns = frame.rx_ns;
            have_prev_book = true;
        }

        if (frame.has_seq) {
            if (stats.seq_frames == 0) {
                stats.seq_min = stats.seq_max = frame.seq;
            } else {
                stats.seq_min = std::min(stats.seq_min, frame.seq);
                stats.seq_max = std::max(stats.seq_max, frame.seq);
            }
            if (have_prev_seq && frame.seq < prev_seq) {
                ++stats.seq_backward_steps;
                stats.seq_monotonic = false;
            }
            prev_seq = frame.seq;
            have_prev_seq = true;
            ++stats.seq_frames;
        }
    }

    stats.liveness_threshold_ms = liveness.threshold_ms();
    if (!liveness_gaps.empty()) {
        std::sort(liveness_gaps.begin(), liveness_gaps.end());
        const std::size_t m = liveness_gaps.size() / 2;
        stats.median_liveness_gap_ms =
            (liveness_gaps.size() % 2 == 0)
                ? (liveness_gaps[m - 1] + liveness_gaps[m]) / 2.0
                : liveness_gaps[m];
    }

    if (!gaps.empty()) {
        // Sorted in place: `gaps` is a local that dies on the next line, so the
        // copy this used to take bought nothing. std::sort rather than
        // nth_element because the max is read off the same ordering, and the
        // block is ~0.01% of read_trace either way.
        std::sort(gaps.begin(), gaps.end());
        stats.max_gap_ms = gaps.back();
        const std::size_t mid = gaps.size() / 2;
        stats.median_gap_ms =
            (gaps.size() % 2 == 0) ? (gaps[mid - 1] + gaps[mid]) / 2.0 : gaps[mid];
    }
    return stats;
}

}  // namespace

TraceStats read_trace(const std::string& path) {
    TraceReader reader(path);
    return accumulate(reader);
}

TraceStats read_trace_text(std::string_view text, const std::string& name) {
    TraceReader reader(text, in_memory, name);
    return accumulate(reader);
}

}  // namespace dc::harness
