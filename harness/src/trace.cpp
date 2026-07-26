// dc_harness/trace.cpp — implementation of the replay-trace reader.
//
// nlohmann/json is included in exactly this one translation unit so the heavy
// header is compiled once and linked into both dc_replay and dc_tests. Harness
// only — never the firmware path.
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

void parse_meta(const json& j, std::size_t line_no, TraceMeta& meta) {
    if (!j.is_object()) {
        throw TraceError(line_no, "metadata line is not a JSON object");
    }
    if (auto it = j.find("captured_at"); it != j.end() && it->is_string()) {
        meta.captured_at = it->get<std::string>();
        meta.captured_at_present = true;
    }
    if (auto it = j.find("url"); it != j.end() && it->is_string()) {
        meta.url = it->get<std::string>();
        meta.url_present = true;
    }
    if (auto it = j.find("ticker"); it != j.end() && it->is_number_integer()) {
        meta.ticker = it->get<std::int64_t>();
        meta.ticker_present = true;
    }
    if (auto it = j.find("tool_version"); it != j.end() && it->is_string()) {
        meta.tool_version = it->get<std::string>();
        meta.tool_version_present = true;
    }
    if (auto it = j.find("capture_mode"); it != j.end() && it->is_string()) {
        meta.capture_mode = it->get<std::string>();
    }
    if (auto it = j.find("cycles"); it != j.end() && it->is_number_integer()) {
        meta.cycles = it->get<std::int64_t>();
    }
    if (!meta.complete()) {
        throw TraceError(line_no,
                         "metadata line missing a required field "
                         "(need captured_at, url, ticker, tool_version)");
    }
}

// Parse one frame line, updating running stats. `gaps` accumulates inter-frame
// deltas for the median. `prev_seq`/`have_prev_seq` thread seq monotonicity.
void parse_frame(const json& j, std::size_t line_no, TraceStats& stats,
                 std::vector<double>& gaps, std::int64_t& prev_seq,
                 bool& have_prev_seq, bool& have_prev_rx) {
    if (!j.is_object()) {
        throw TraceError(line_no, "frame line is not a JSON object");
    }
    auto rx_it = j.find("rx_ns");
    if (rx_it == j.end() || !rx_it->is_number_integer()) {
        throw TraceError(line_no, "frame line missing integer rx_ns");
    }
    const std::int64_t rx_ns = rx_it->get<std::int64_t>();

    auto fr_it = j.find("frame");
    if (fr_it == j.end() || !fr_it->is_object()) {
        throw TraceError(line_no, "frame line missing object 'frame'");
    }
    const json& frame = *fr_it;
    auto type_it = frame.find("type");
    if (type_it == frame.end() || !type_it->is_string()) {
        throw TraceError(line_no, "frame object missing string 'type'");
    }
    const std::string kind = type_it->get<std::string>();
    const bool is_first_frame = !have_prev_rx;

    // rx_ns comes from a monotonic clock in the capture tool; a decrease is a
    // corrupt trace, not a market phenomenon.
    if (have_prev_rx) {
        if (rx_ns < stats.last_rx_ns) {
            throw TraceError(line_no, "rx_ns went backwards (non-monotonic capture clock)");
        }
        gaps.push_back(static_cast<double>(rx_ns - stats.last_rx_ns) / 1e6);
    } else {
        stats.first_rx_ns = rx_ns;
        have_prev_rx = true;
    }
    stats.last_rx_ns = rx_ns;

    ++stats.frame_count;
    ++stats.kind_counts[kind];
    if (kind == "snapshot") {
        ++stats.snapshot_count;
        if (!is_first_frame) {
            ++stats.mid_stream_snapshots;
        }
    }

    if (auto seq_it = frame.find("seq");
        seq_it != frame.end() && seq_it->is_number_integer()) {
        const std::int64_t seq = seq_it->get<std::int64_t>();
        if (stats.seq_frames == 0) {
            stats.seq_min = stats.seq_max = seq;
        } else {
            stats.seq_min = std::min(stats.seq_min, seq);
            stats.seq_max = std::max(stats.seq_max, seq);
        }
        if (have_prev_seq && seq < prev_seq) {
            ++stats.seq_backward_steps;
            stats.seq_monotonic = false;
        }
        prev_seq = seq;
        have_prev_seq = true;
        ++stats.seq_frames;
    }
}

TraceStats parse_lines(std::istream& in) {
    TraceStats stats;
    std::vector<double> gaps;
    std::int64_t prev_seq = 0;
    bool have_prev_seq = false;
    bool have_prev_rx = false;
    bool have_meta = false;

    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        if (line.empty() ||
            line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;  // tolerate blank lines (e.g. trailing newline)
        }
        json j;
        try {
            j = json::parse(line);
        } catch (const json::parse_error& e) {
            throw TraceError(line_no, std::string("invalid JSON: ") + e.what());
        }
        if (!have_meta) {
            parse_meta(j, line_no, stats.meta);
            have_meta = true;
        } else {
            parse_frame(j, line_no, stats, gaps, prev_seq, have_prev_seq,
                        have_prev_rx);
        }
    }

    if (!have_meta) {
        throw TraceError(0, "empty trace (no metadata line)");
    }

    if (!gaps.empty()) {
        std::vector<double> sorted = gaps;
        std::sort(sorted.begin(), sorted.end());
        stats.max_gap_ms = sorted.back();
        const std::size_t mid = sorted.size() / 2;
        stats.median_gap_ms = (sorted.size() % 2 == 0)
                                  ? (sorted[mid - 1] + sorted[mid]) / 2.0
                                  : sorted[mid];
    }
    return stats;
}

}  // namespace

TraceStats read_trace(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open trace: " + path);
    }
    return parse_lines(in);
}

TraceStats read_trace_text(std::string_view text, const std::string&) {
    std::istringstream in{std::string(text)};
    return parse_lines(in);
}

// --- streaming reader --------------------------------------------------------

namespace {

// [start, end) of the JSON object beginning at `start`, or npos if unbalanced.
std::size_t object_end(std::string_view s, std::size_t start) noexcept {
    std::size_t depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = start; i < s.size(); ++i) {
        const char c = s[i];
        if (in_string) {
            if (escaped) { escaped = false; }
            else if (c == '\\') { escaped = true; }
            else if (c == '"') { in_string = false; }
            continue;
        }
        if (c == '"') { in_string = true; }
        else if (c == '{') { ++depth; }
        else if (c == '}' && --depth == 0) { return i + 1; }
    }
    return std::string_view::npos;
}

std::size_t skip_ws(std::string_view s, std::size_t i) noexcept {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) { ++i; }
    return i;
}

}  // namespace

std::string_view slice_frame_json(std::string_view line) noexcept {
    // Walk the line once, tracking string state so that braces and the literal
    // text "frame" inside a JSON string (an order id, a URL) cannot be mistaken
    // for structure. Only depth-1 keys — a string immediately followed by ':' —
    // are candidates.
    std::size_t depth = 0;
    bool in_string = false;
    bool escaped = false;
    std::size_t str_start = std::string_view::npos;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
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
            }
            continue;
        }
        switch (c) {
            case '"':
                in_string = true;
                str_start = i + 1;
                break;
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

TraceReader::TraceReader(const std::string& path) {
    auto file = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*file) {
        throw std::runtime_error("cannot open trace: " + path);
    }
    owned_ = std::move(file);
    in_ = owned_.get();
    read_meta();
}

TraceReader::TraceReader(std::string_view text, InMemoryTag) {
    owned_ = std::make_unique<std::istringstream>(std::string(text));
    in_ = owned_.get();
    read_meta();
}

void TraceReader::read_meta() {
    while (std::getline(*in_, line_)) {
        ++line_no_;
        if (line_.find_first_not_of(" \t\r\n") == std::string::npos) { continue; }
        json j;
        try {
            j = json::parse(line_);
        } catch (const json::parse_error& e) {
            throw TraceError(line_no_, std::string("invalid JSON: ") + e.what());
        }
        parse_meta(j, line_no_, meta_);
        return;
    }
    throw TraceError(0, "empty trace (no metadata line)");
}

bool TraceReader::next(TraceFrame& out) {
    while (std::getline(*in_, line_)) {
        ++line_no_;
        if (line_.find_first_not_of(" \t\r\n") == std::string::npos) { continue; }

        json j;
        try {
            j = json::parse(line_);
        } catch (const json::parse_error& e) {
            throw TraceError(line_no_, std::string("invalid JSON: ") + e.what());
        }
        if (!j.is_object()) {
            throw TraceError(line_no_, "frame line is not a JSON object");
        }
        const auto rx_it = j.find("rx_ns");
        if (rx_it == j.end() || !rx_it->is_number_integer()) {
            throw TraceError(line_no_, "frame line missing integer rx_ns");
        }
        const auto fr_it = j.find("frame");
        if (fr_it == j.end() || !fr_it->is_object()) {
            throw TraceError(line_no_, "frame line missing object 'frame'");
        }

        const std::string_view verbatim = slice_frame_json(line_);
        if (verbatim.empty()) {
            throw TraceError(line_no_, "could not slice verbatim 'frame' text");
        }

        ++frame_index_;
        out.index = frame_index_;
        out.line_no = line_no_;
        out.rx_ns = rx_it->get<std::int64_t>();
        out.frame_json = verbatim;
        return true;
    }
    return false;
}

}  // namespace dc::harness
