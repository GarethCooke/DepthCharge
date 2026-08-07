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

    if (!meta.complete()) {
        throw TraceError(src, line_no,
                         "metadata line missing a required field "
                         "(need captured_at, url, ticker, tool_version)");
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
        const auto type_it = fr_it->find("type");
        if (type_it == fr_it->end() || !type_it->is_string()) {
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
        type_ = type_it->get<std::string>();
        ++frame_index_;

        out.index = frame_index_;
        out.line_no = line_no_;
        out.rx_ns = rx_ns;
        out.frame_json = verbatim;
        out.type = type_;
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

    std::vector<double> gaps;
    std::int64_t prev_seq = 0;
    bool have_prev_seq = false;

    TraceFrame frame;
    while (reader.next(frame)) {
        const bool is_first_frame = stats.frame_count == 0;
        if (is_first_frame) {
            stats.first_rx_ns = frame.rx_ns;
        } else {
            gaps.push_back(static_cast<double>(frame.rx_ns - stats.last_rx_ns) / 1e6);
        }
        stats.last_rx_ns = frame.rx_ns;

        ++stats.frame_count;
        ++stats.kind_counts[std::string(frame.type)];
        if (frame.type == "snapshot") {
            ++stats.snapshot_count;
            // A snapshot arriving mid-trace is a resync, so mid_stream >= 1
            // evidences a reconnect.
            if (!is_first_frame) { ++stats.mid_stream_snapshots; }
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
