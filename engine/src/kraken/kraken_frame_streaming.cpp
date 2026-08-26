// engine/src/kraken/kraken_frame_streaming.cpp — the one implementation of
// parse_kraken_frame: streaming, allocation-free, portable C++20.
//
// The rules it is written to, and every one of them is a rule this project has
// already paid for once:
//
//   * NO HEAP, ANYWHERE (invariant #7). Every byte of working state is this
//     object's stack frame — a few scalars and a 64-byte scratch for the one
//     case that cannot be sliced in place — or the caller's KrakenFrame. The
//     brief's deliverable 5 names this as the invariant a JSON library breaks
//     for you; there is no library here, so the claim is structural rather than
//     audited. `test_kraken_adapter.cpp` proves it with the allocation probe.
//   * NO FLOAT NEAR A PRICE (invariant #3). Number tokens are SLICED, never
//     converted: scan_number finds the token's bytes and validates its syntax,
//     and depthcharge::parse_scaled turns those bytes into integer ticks. There
//     is not a `double` in this file. At this venue that is not hygiene — stage
//     0 measured 0 of 2,786 checksums surviving a float round-trip.
//   * NO RECURSION OVER UNTRUSTED STRUCTURE. skip_value() is an iterative state
//     machine over a 64-bit container-kind stack, so a frame of nothing but
//     nested brackets costs no C++ stack. Same reasoning as the Anvil streaming
//     parser: a recursive skipper blows a 4 KB FreeRTOS task stack.
//   * NO EXCEPTIONS, NO RTTI, NO <span>/<ranges>/<bit>/<concepts>. The target
//     toolchain is xtensa GCC 8.4, and this TU is in DC_ENGINE_TARGET_SOURCES so
//     the host build compiles it with that compiler.
//
// WHY THIS IS A SECOND SCANNER AND NOT A REUSE OF ANVIL'S.
//
// The obvious move is to lift the scanning primitives out of
// anvil_frame_streaming.cpp and share them. It was considered and refused, and
// the reason is specific rather than schedule pressure: that parser's primitives
// are deliberately bug-compatible with nlohmann 3.11.3, because its whole
// contract is that the reference implementation's observable behaviour — quirks
// included — is the specification (anvil_frame.hpp). Its number scanner treats
// an integer too large for uint64 as a float so the frame becomes a shape error;
// its skip budget is 64 containers per skipped VALUE; its escape handling has a
// measured divergence at 64 bytes. None of those are properties Kraken wants,
// and a shared header would have to carry them into a venue that has no
// reference to be equivalent to — or grow a flag per quirk, which is worse.
//
// So the duplication is real and it is named rather than hidden: the two files
// share a grammar and not a line of code, and if a third venue arrives with a
// third set of conventions, THAT is the point at which extracting a scanner
// pays, because three instances distinguish the general primitives from the two
// venues' accidents. Recorded as a strain point in docs/DESIGN.html §08.
#include "depthcharge/json_scan.hpp"
#include "depthcharge/kraken/kraken_frame.hpp"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "depthcharge/decimal.hpp"

namespace depthcharge::kraken {
namespace {

// The venue grammar. The JSON token layer it stands on is
// depthcharge/json_scan.hpp, extracted at M5 stage B1 -- see that header for
// why Binance reuses this scanner rather than getting a third one of its own.
//
// `using` rather than a qualified name at every use site: these three are the
// scanner's vocabulary and were unqualified in this file for a milestone before
// they moved, so importing them is what keeps the grammar bodies below
// byte-identical through the extraction.
using depthcharge::json::is_digit;
using depthcharge::json::NumberToken;
using depthcharge::json::StringToken;

class FrameParser : public depthcharge::json::Scanner {
public:
    FrameParser(std::string_view text, const SymbolConfig& cfg, KrakenFrame& out) noexcept
        : depthcharge::json::Scanner(text), cfg_(cfg), out_(out) {}

    ParseStatus run() noexcept {
        skip_ws();
        if (at_end() || *p_ != '{') { return ParseStatus::NotJson; }
        return scan_top_object();
    }

private:
    // ---- the frame grammar --------------------------------------------------

    ParseStatus scan_top_object() noexcept {
        ++p_;  // '{'
        if (!want()) { return ParseStatus::NotJson; }

        bool have_channel = false;
        bool have_method = false;
        char channel[16] = {};
        std::size_t channel_len = 0;
        char type[16] = {};
        std::size_t type_len = 0;
        // The method NAME, not merely its presence. Until M4 stage B2 this
        // parser filed every `method` frame as a subscribe ack, which was true
        // of every frame in every capture and false of the wire: the healing
        // path sends an UNSUBSCRIBE, and its ack has the identical shape.
        // `unsubscribe` is 11 characters, so 16 holds it with room and a longer
        // method truncates into a name that matches neither literal below and
        // therefore reads as Unknown, which is the right answer for a method
        // this build does not know.
        char method[16] = {};
        std::size_t method_len = 0;

        // `data` is scanned in place when it is met, which requires knowing the
        // channel first. Every captured book message puts `channel` before
        // `data` (measured: the key order is fixed across all 9,932 frames), but
        // a parser that DEPENDED on that would be reading a capture rather than
        // a protocol. So a `data` met before its `channel` is remembered and
        // rescanned once the channel is known — one extra pass over one member,
        // on a path no committed slice takes.
        const char* pending_data = nullptr;

        if (peek() == '}') {
            ++p_;
            return finish(FrameKind::Unknown);
        }

        while (true) {
            StringToken key;
            if (!scan_string(key, /*capture=*/true)) { return ParseStatus::NotJson; }
            if (!eat(':')) { return ParseStatus::NotJson; }
            if (!want()) { return ParseStatus::NotJson; }

            if (key_is(key, "channel")) {
                StringToken v;
                if (!scan_string(v, /*capture=*/true)) { return ParseStatus::NotJson; }
                channel_len = copy_small(channel, sizeof channel, v.text);
                have_channel = true;
            } else if (key_is(key, "type")) {
                StringToken v;
                if (!scan_string(v, /*capture=*/true)) { return ParseStatus::NotJson; }
                type_len = copy_small(type, sizeof type, v.text);
            } else if (key_is(key, "method")) {
                StringToken v;
                if (!scan_string(v, /*capture=*/true)) { return ParseStatus::NotJson; }
                method_len = copy_small(method, sizeof method, v.text);
                have_method = true;
            } else if (key_is(key, "success")) {
                bool b = false;
                if (!scan_scalar(&b)) { return ParseStatus::NotJson; }
                out_.ack_success = b;
            } else if (key_is(key, "error")) {
                if (!skip_value()) { return ParseStatus::NotJson; }
                out_.ack_has_error = true;
            } else if (key_is(key, "result")) {
                if (!scan_result_object()) { return ParseStatus::NotJson; }
            } else if (key_is(key, "data")) {
                if (have_channel && std::string_view(channel, channel_len) == "book") {
                    if (!scan_data_array()) { return status_ == ParseStatus::Ok
                                                         ? ParseStatus::NotJson : status_; }
                } else {
                    pending_data = p_;
                    if (!skip_value()) { return ParseStatus::NotJson; }
                }
            } else {
                if (!skip_value()) { return ParseStatus::NotJson; }
            }

            if (!want()) { return ParseStatus::NotJson; }
            if (peek() == ',') {
                ++p_;
                continue;
            }
            if (peek() == '}') {
                ++p_;
                break;
            }
            return ParseStatus::NotJson;
        }

        const std::string_view ch(channel, channel_len);
        const std::string_view ty(type, type_len);

        if (have_channel && ch == "book") {
            if (pending_data != nullptr) {
                // The out-of-order case described above: re-enter at the
                // remembered position now that the channel is known.
                p_ = pending_data;
                if (!scan_data_array()) {
                    return status_ == ParseStatus::Ok ? ParseStatus::NotJson : status_;
                }
            }
            if (status_ != ParseStatus::Ok) { return status_; }
            if (out_.matched_entries == 0) { return ParseStatus::OtherSymbol; }
            return finish(ty == "snapshot" ? FrameKind::BookSnapshot : FrameKind::BookUpdate);
        }
        if (have_channel && ch == "heartbeat") { return finish(FrameKind::Heartbeat); }
        if (have_channel && ch == "status") { return finish(FrameKind::Status); }
        if (have_method) {
            const std::string_view m(method, method_len);
            if (m == "subscribe") { return finish(FrameKind::SubscribeAck); }
            if (m == "unsubscribe") { return finish(FrameKind::UnsubscribeAck); }
            // A method this build does not know — `ping`/`pong` are the ones
            // Kraken v2 offers and M6 owns. Tolerated, counted, ignored, exactly
            // as an unknown channel is: filing it as a subscribe ack is how the
            // defect above happened.
            return finish(FrameKind::Unknown);
        }
        return finish(FrameKind::Unknown);
    }

    ParseStatus finish(FrameKind k) noexcept {
        out_.kind = k;
        return ParseStatus::Ok;
    }

    // The subscribe ack's `result` object: only `depth` is read. `channel`,
    // `symbol` and `snapshot` are validated and discarded — the adapter checks
    // what it needs from the depth and the success flag.
    bool scan_result_object() noexcept {
        if (!want() || peek() != '{') { return skip_value(); }
        ++p_;
        if (!want()) { return false; }
        if (peek() == '}') { ++p_; return true; }
        while (true) {
            StringToken key;
            if (!scan_string(key, /*capture=*/true)) { return false; }
            if (!eat(':')) { return false; }
            if (key_is(key, "depth")) {
                NumberToken n;
                if (!want()) { return false; }
                if (!is_digit(peek()) && peek() != '-') { return false; }
                if (!scan_number(n)) { return false; }
                const DecimalParse d = parse_scaled(n.text, 0);
                if (d.ok()) { out_.ack_depth = d.value; }
            } else {
                if (!skip_value()) { return false; }
            }
            if (!want()) { return false; }
            if (peek() == ',') { ++p_; continue; }
            if (peek() == '}') { ++p_; return true; }
            return false;
        }
    }

    // `data` : [ {symbol, bids, asks, checksum, timestamp}, ... ]
    //
    // ITERATED, NOT INDEXED AT [0]. Every committed slice holds exactly one
    // entry, so nothing here would catch the mistake — but a subscribe may name
    // several symbols on one socket (six measured), and the entry that is ours
    // would then not be the first.
    bool scan_data_array() noexcept {
        if (!want() || peek() != '[') { return false; }
        ++p_;
        if (!want()) { return false; }
        if (peek() == ']') { ++p_; return true; }
        while (true) {
            ++out_.data_entries;
            if (!scan_data_entry()) { return false; }
            if (!want()) { return false; }
            if (peek() == ',') { ++p_; continue; }
            if (peek() == ']') { ++p_; return true; }
            return false;
        }
    }

    bool scan_data_entry() noexcept {
        if (!want() || peek() != '{') { return false; }

        // Two passes over ONE entry, and the reason is the symbol. Levels can
        // only be decoded once it is known whether this entry is ours, and
        // `symbol` precedes `bids` in every captured frame — but depending on
        // that would again be reading the capture rather than the protocol. So
        // pass one finds the symbol and nothing else; pass two decodes.
        const char* entry_start = p_;
        bool mine = false;
        if (!entry_symbol_matches(mine)) { return false; }
        p_ = entry_start;

        if (!mine) {
            // Not our instrument: validated and discarded whole.
            return skip_value();
        }
        ++out_.matched_entries;

        ++p_;  // '{'
        if (!want()) { return false; }
        if (peek() == '}') { ++p_; return true; }
        while (true) {
            StringToken key;
            if (!scan_string(key, /*capture=*/true)) { return false; }
            if (!eat(':')) { return false; }
            if (key_is(key, "bids")) {
                if (!scan_side(out_.bids, out_.bid_count)) { return false; }
            } else if (key_is(key, "asks")) {
                if (!scan_side(out_.asks, out_.ask_count)) { return false; }
            } else if (key_is(key, "checksum")) {
                NumberToken n;
                if (!scan_number(n)) { return false; }
                const DecimalParse d = parse_scaled(n.text, 0);
                if (!d.ok() || d.value < 0 || d.value > 0xFFFFFFFFll) {
                    status_ = ParseStatus::BadShape;
                    return false;
                }
                out_.checksum = static_cast<std::uint32_t>(d.value);
                out_.has_checksum = true;
            } else {
                if (!skip_value()) { return false; }
            }
            if (!want()) { return false; }
            if (peek() == ',') { ++p_; continue; }
            if (peek() == '}') { ++p_; return true; }
            return false;
        }
    }

    // Pass one: scan the entry for `symbol` alone, leaving p_ wherever it ends.
    bool entry_symbol_matches(bool& mine) noexcept {
        mine = false;
        ++p_;  // '{'
        if (!want()) { return false; }
        if (peek() == '}') { ++p_; return true; }
        while (true) {
            StringToken key;
            if (!scan_string(key, /*capture=*/true)) { return false; }
            if (!eat(':')) { return false; }
            if (key_is(key, "symbol")) {
                StringToken v;
                if (!scan_string(v, /*capture=*/true)) { return false; }
                if (!v.too_long && v.text == cfg_.wire_symbol) { mine = true; }
            } else {
                if (!skip_value()) { return false; }
            }
            if (!want()) { return false; }
            if (peek() == ',') { ++p_; continue; }
            if (peek() == '}') { ++p_; return true; }
            return false;
        }
    }

    // [ {"price":N,"qty":N}, ... ] — in no order at all on an update.
    bool scan_side(BookLevel* dst, std::uint32_t& count) noexcept {
        if (!want() || peek() != '[') { return false; }
        ++p_;
        count = 0;
        if (!want()) { return false; }
        if (peek() == ']') { ++p_; return true; }
        while (true) {
            BookLevel lvl{};
            if (!scan_level(lvl)) { return false; }
            if (count < kMaxSnapshotLevels) {
                dst[count++] = lvl;
            } else {
                out_.levels_truncated = true;
            }
            if (!want()) { return false; }
            if (peek() == ',') { ++p_; continue; }
            if (peek() == ']') { ++p_; return true; }
            return false;
        }
    }

    bool scan_level(BookLevel& lvl) noexcept {
        if (!want() || peek() != '{') { return false; }
        ++p_;
        bool have_px = false;
        bool have_qty = false;
        if (!want()) { return false; }
        if (peek() == '}') { ++p_; status_ = ParseStatus::BadShape; return false; }
        while (true) {
            StringToken key;
            if (!scan_string(key, /*capture=*/true)) { return false; }
            if (!eat(':')) { return false; }
            if (key_is(key, "price")) {
                NumberToken n;
                if (!scan_number(n)) { return false; }
                const DecimalParse d = parse_scaled(n.text, cfg_.spec.price_decimals);
                if (!d.ok()) {
                    // A price the declared scale cannot hold EXACTLY is reported,
                    // never rounded (ARCHITECTURE §4). At this venue it is also
                    // the difference between a book and a guaranteed CRC
                    // mismatch on every subsequent message.
                    status_ = ParseStatus::BadPrice;
                    return false;
                }
                lvl.px = d.value;
                have_px = true;
            } else if (key_is(key, "qty")) {
                NumberToken n;
                if (!scan_number(n)) { return false; }
                const DecimalParse d = parse_scaled(n.text, cfg_.spec.qty_decimals);
                if (!d.ok() || d.value < 0) {
                    status_ = ParseStatus::BadQty;
                    return false;
                }
                lvl.qty = d.value;
                have_qty = true;
            } else {
                if (!skip_value()) { return false; }
            }
            if (!want()) { return false; }
            if (peek() == ',') { ++p_; continue; }
            if (peek() == '}') {
                ++p_;
                if (!have_px || !have_qty) {
                    status_ = ParseStatus::BadShape;
                    return false;
                }
                return true;
            }
            return false;
        }
    }

    const SymbolConfig& cfg_;
    KrakenFrame& out_;
    ParseStatus status_ = ParseStatus::Ok;
};

}  // namespace

ParseStatus parse_kraken_frame(std::string_view text, const SymbolConfig& cfg,
                               KrakenFrame& out) noexcept {
    out.reset();
    if (!cfg.spec.valid()) { return ParseStatus::BadShape; }
    FrameParser parser(text, cfg, out);
    const ParseStatus st = parser.run();
    // The postcondition anvil_frame.hpp states and this file inherits: a
    // non-Ok status leaves the frame well-formed and empty, so a caller that
    // ignores the status cannot read half a book out of it.
    if (st != ParseStatus::Ok) { out.reset(); }
    return st;
}

}  // namespace depthcharge::kraken
