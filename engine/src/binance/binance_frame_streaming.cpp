// depthcharge/binance/binance_frame_streaming.cpp — Binance spot's grammar.
//
// ONE IMPLEMENTATION, target-bound, compiled by dc_engine_target_check with the
// xtensa compiler from the day it lands — the same arrangement Kraken's parser
// has and for the same reason (kraken_frame.hpp): there is no allocating
// reference to be equivalent to, and writing one purely to have something to
// agree with would be inventing the obligation.
//
// THE SCANNING IS NOT HERE. Every JSON token this file consumes is consumed by
// `depthcharge::json::Scanner` (depthcharge/json_scan.hpp), which is Kraken's
// scanner lifted unchanged at M5 stage B1. What is here is the GRAMMAR, which is
// where the two venues genuinely differ:
//
//   * a level is `["78732.14000000","3.91282000"]` — a two-element ARRAY of
//     STRINGS, where Kraken's is an OBJECT of bare numbers;
//   * the payload may be wrapped in a combined-stream envelope;
//   * ordering is `U`/`u`, not a checksum.
//
// NO FLOAT AND NO `Decimal` ANYWHERE IN THIS PATH. The strings are sliced in
// place and handed to depthcharge::parse_scaled, which is integer-only. At 8
// decimals `78764.46000000` is 7,876,446,000,000 — 43 bits, comfortably inside
// int64 — and the widest integer part measured across the committed corpus is 6
// digits, so the headroom is not marginal.
#include "depthcharge/binance/binance_frame.hpp"

#include <cstdint>
#include <string_view>

#include "depthcharge/decimal.hpp"
#include "depthcharge/json_scan.hpp"

namespace depthcharge::binance {
namespace {

using depthcharge::json::is_digit;
using depthcharge::json::NumberToken;
using depthcharge::json::StringToken;

class FrameParser : public depthcharge::json::Scanner {
public:
    FrameParser(std::string_view text, FrameSource source, const SymbolConfig& cfg,
                BinanceFrame& out) noexcept
        : depthcharge::json::Scanner(text), source_(source), cfg_(cfg), out_(out) {}

    ParseStatus run() noexcept {
        skip_ws();
        if (at_end() || *p_ != '{') { return ParseStatus::NotJson; }
        return scan_top_object();
    }

private:
    // ---- the frame grammar --------------------------------------------------

    // The top level is either the payload itself or the combined-stream
    // envelope. Both are handled here rather than by a pre-pass, so the wrapper
    // costs one key comparison and never a second scan of the body.
    ParseStatus scan_top_object() noexcept {
        const char* start = p_;
        ++p_;  // '{'
        if (!want()) { return ParseStatus::NotJson; }
        if (peek() == '}') { ++p_; return finish(FrameKind::Unknown); }

        // THE ENVELOPE IS DETECTED FROM ITS FIRST KEY, and the payload is then
        // scanned EXACTLY ONCE. The obvious shape — walk the top level
        // remembering where `data` started, then rewind and parse it — scans a
        // 1,000-level REST body twice, once to skip it and once to read it. On
        // the deep-seed slices that is 32 KiB of level text parsed for nothing,
        // ten times a second, on the path invariant #7 governs.
        //
        // `stream` and `data` are the envelope's only two keys and a Binance
        // payload has neither, so the first key decides. A payload that somehow
        // began with one would be rewound and read as a payload anyway, because
        // the envelope branch below requires a `data` member to produce anything.
        const char* key_at = p_;
        StringToken first;
        if (!scan_string(first, /*capture=*/true)) { return ParseStatus::NotJson; }
        if (!key_is(first, "stream") && !key_is(first, "data")) {
            p_ = start;
            return scan_payload();
        }

        p_ = key_at;
        ParseStatus payload = ParseStatus::Ok;
        bool scanned = false;
        while (true) {
            StringToken key;
            if (!scan_string(key, /*capture=*/true)) { return ParseStatus::NotJson; }
            if (!eat(':')) { return ParseStatus::NotJson; }
            if (!want()) { return ParseStatus::NotJson; }
            if (key_is(key, "data")) {
                // Scanned IN PLACE, which is what makes it one pass. `stream` is
                // validated and discarded by the skip below: the stream NAME is
                // deliberately not read as identity, because `s` carries the
                // instrument on a diff and a partial carries none at all, so
                // believing the name here would put a second, weaker symbol
                // check beside the real one.
                payload = scan_payload();
                if (payload != ParseStatus::Ok) { return payload; }
                scanned = true;
            } else if (!skip_value()) {
                return ParseStatus::NotJson;
            }
            if (!want()) { return ParseStatus::NotJson; }
            if (peek() == ',') { ++p_; continue; }
            if (peek() == '}') { ++p_; break; }
            return ParseStatus::NotJson;
        }
        return scanned ? payload : finish(FrameKind::Unknown);
    }

    // The payload proper: a diff, a partial, or a REST body.
    ParseStatus scan_payload() noexcept {
        if (!want() || peek() != '{') { return ParseStatus::NotJson; }
        ++p_;
        if (!want()) { return ParseStatus::NotJson; }

        bool is_depth_update = false;
        bool has_ack_shape = false;
        if (peek() == '}') { ++p_; return finish(FrameKind::Unknown); }

        while (true) {
            StringToken key;
            if (!scan_string(key, /*capture=*/true)) { return ParseStatus::NotJson; }
            if (!eat(':')) { return ParseStatus::NotJson; }
            if (!want()) { return ParseStatus::NotJson; }

            if (key_is(key, "e")) {
                StringToken v;
                if (!scan_string(v, /*capture=*/true)) { return ParseStatus::NotJson; }
                if (v.text == "depthUpdate") { is_depth_update = true; }
            } else if (key_is(key, "s")) {
                StringToken v;
                if (!scan_string(v, /*capture=*/true)) { return ParseStatus::NotJson; }
                out_.symbol_present = true;
                if (!v.too_long && v.text == cfg_.wire_symbol) { out_.symbol_matched = true; }
            } else if (key_is(key, "U")) {
                if (!scan_id(out_.first_update_id)) { return ParseStatus::NotJson; }
                have_u_ = true;
            } else if (key_is(key, "u")) {
                if (!scan_id(out_.final_update_id)) { return ParseStatus::NotJson; }
                have_final_ = true;
            } else if (key_is(key, "lastUpdateId")) {
                if (!scan_id(out_.last_update_id)) { return ParseStatus::NotJson; }
                out_.has_last_update_id = true;
            } else if (key_is(key, "b") || key_is(key, "bids")) {
                if (!scan_side(out_.bids, out_.bid_count)) { return fail(); }
            } else if (key_is(key, "a") || key_is(key, "asks")) {
                if (!scan_side(out_.asks, out_.ask_count)) { return fail(); }
            } else if (key_is(key, "result") || key_is(key, "id")) {
                has_ack_shape = true;
                if (!skip_value()) { return ParseStatus::NotJson; }
            } else {
                if (!skip_value()) { return ParseStatus::NotJson; }
            }

            if (!want()) { return ParseStatus::NotJson; }
            if (peek() == ',') { ++p_; continue; }
            if (peek() == '}') { ++p_; break; }
            return ParseStatus::NotJson;
        }

        out_.has_update_range = have_u_ && have_final_;

        if (is_depth_update) {
            // A DIFF FOR SOMEONE ELSE'S INSTRUMENT IS NOT AN ERROR, it is a
            // message this adapter is not following — one socket may carry
            // several streams. Distinguished from a malformed frame for the same
            // reason Kraken distinguishes it: only one of the two is a bug.
            if (out_.symbol_present && !out_.symbol_matched) {
                return ParseStatus::OtherSymbol;
            }
            if (!out_.has_update_range) { return ParseStatus::BadShape; }
            return finish(FrameKind::DepthUpdate);
        }
        if (out_.has_last_update_id) {
            // THE RECORD SAYS WHICH, NOT THE PAYLOAD. Identical shapes, and the
            // difference is whether the book is fully known afterwards.
            return finish(source_ == FrameSource::RestBody ? FrameKind::RestSnapshot
                                                           : FrameKind::PartialDepth);
        }
        if (has_ack_shape) { return finish(FrameKind::Ack); }
        return finish(FrameKind::Unknown);
    }

    // An update id. Bare integer on the wire, converted through parse_scaled at
    // zero decimals so no integer conversion in this file is ad hoc — the
    // largest observed is ~9.9e10, which needs 37 bits.
    bool scan_id(std::int64_t& dst) noexcept {
        NumberToken n;
        if (!want()) { return false; }
        if (!is_digit(peek()) && peek() != '-') { return false; }
        if (!scan_number(n)) { return false; }
        const DecimalParse d = parse_scaled(n.text, 0);
        if (!d.ok()) { return false; }
        dst = d.value;
        return true;
    }

    // `[ ["px","qty"], ... ]`
    bool scan_side(BookLevel* dst, std::uint32_t& count) noexcept {
        if (!want() || peek() != '[') { return false; }
        ++p_;
        count = 0;
        if (!want()) { return false; }
        if (peek() == ']') { ++p_; return true; }
        while (true) {
            BookLevel lvl{};
            if (!scan_level(lvl)) { return false; }
            if (count < kBinanceMaxFrameLevels) {
                dst[count++] = lvl;
            } else {
                // NOT a truncation flag to be reported later. A truncated diff
                // is a book that has silently missed amendments, so the message
                // fails outright and the adapter greys the panel — see
                // ParseStatus::TooManyLevels.
                status_ = ParseStatus::TooManyLevels;
                return false;
            }
            if (!want()) { return false; }
            if (peek() == ',') { ++p_; continue; }
            if (peek() == ']') { ++p_; return true; }
            return false;
        }
    }

    // ONE LEVEL: a two-element array of decimal STRINGS. Extra elements are
    // tolerated and skipped — spot sends exactly two, and the futures stream a
    // third that is documented as ignorable, so refusing a third would be
    // refusing a message this build could read perfectly well.
    bool scan_level(BookLevel& lvl) noexcept {
        if (!want() || peek() != '[') { return false; }
        ++p_;
        StringToken px;
        if (!scan_string(px, /*capture=*/true)) { return false; }
        const DecimalParse p = parse_scaled(px.text, cfg_.spec.price_decimals);
        if (!p.ok()) {
            // A price the declared scale cannot hold EXACTLY is reported, never
            // rounded (ARCHITECTURE §4). At 8 decimals against a wire that sends
            // exactly 8, this fires only if the venue's precision changes — and
            // that is the detector: the first thing to notice a precision change
            // is this named status, not a book that drifts.
            status_ = ParseStatus::BadPrice;
            return false;
        }
        lvl.px = p.value;

        if (!eat(',')) { return false; }
        StringToken qty;
        if (!scan_string(qty, /*capture=*/true)) { return false; }
        const DecimalParse q = parse_scaled(qty.text, cfg_.spec.qty_decimals);
        if (!q.ok() || q.value < 0) {
            status_ = ParseStatus::BadQty;
            return false;
        }
        lvl.qty = q.value;

        // The venue's declared tick and lot filters, used as VALIDATORS rather
        // than as scales (binance_frame.hpp). Zero means this build was not told
        // the filter, and an unchecked value is better than a wrong check.
        if (cfg_.tick_ticks > 0 && (lvl.px % cfg_.tick_ticks) != 0) {
            status_ = ParseStatus::BadPrice;
            return false;
        }
        if (cfg_.step_ticks > 0 && lvl.qty != 0 && (lvl.qty % cfg_.step_ticks) != 0) {
            status_ = ParseStatus::BadQty;
            return false;
        }

        while (true) {
            if (!want()) { return false; }
            if (peek() == ']') { ++p_; return true; }
            if (peek() != ',') { return false; }
            ++p_;
            if (!skip_value()) { return false; }
        }
    }

    ParseStatus finish(FrameKind k) noexcept {
        out_.kind = k;
        return ParseStatus::Ok;
    }

    // A level-scan failure carries its own reason when it set one; otherwise the
    // shape was wrong. Mirrors Kraken's `status_` convention exactly.
    ParseStatus fail() noexcept {
        return status_ == ParseStatus::Ok ? ParseStatus::BadShape : status_;
    }

    FrameSource source_;
    const SymbolConfig& cfg_;
    BinanceFrame& out_;
    ParseStatus status_ = ParseStatus::Ok;
    bool have_u_ = false;
    bool have_final_ = false;
};

}  // namespace

ParseStatus parse_binance_frame(std::string_view text, FrameSource source,
                                const SymbolConfig& cfg, BinanceFrame& out) noexcept {
    out.reset();
    if (!cfg.spec.valid()) { return ParseStatus::BadShape; }
    FrameParser parser(text, source, cfg, out);
    const ParseStatus st = parser.run();
    // The postcondition kraken_frame.hpp states and this file inherits: a non-Ok
    // status leaves the frame well-formed and empty, so a caller that ignores
    // the status cannot read half a book out of it.
    if (st != ParseStatus::Ok) { out.reset(); }
    return st;
}

}  // namespace depthcharge::binance
