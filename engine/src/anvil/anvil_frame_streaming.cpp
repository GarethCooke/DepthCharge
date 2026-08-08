// engine/src/anvil/anvil_frame_streaming.cpp — the M3 (target) implementation of
// parse_anvil_frame: streaming, allocation-free, portable C++20.
//
// This is the second implementation of the seam declared in anvil_frame.hpp. The
// first (anvil_frame_nlohmann.cpp) is the desk-side reference and allocates per
// parse, which invariant #7 forbids on the feed path of a microcontroller. This
// one is what links into firmware/, so it obeys the harder rules:
//
//   * NO HEAP. Every byte of working state is this object's stack frame (a few
//     scalars plus a 64-byte token scratch) or the caller's AnvilFrame. Nothing
//     is copied out of the input buffer except escaped tokens, which Anvil has
//     never emitted (measured: zero backslashes in 2694 captured frames).
//   * NO RECURSION over untrusted structure. The generic value skipper is an
//     iterative state machine with a 64-bit container-kind stack, so a frame of
//     nothing but nested brackets costs no C++ stack. A recursive-descent
//     skipper would blow a 4 KB FreeRTOS task stack on an 8 KB frame.
//   * NO EXCEPTIONS, NO RTTI, NO <span>/<ranges>/<bit>/<concepts>. The target
//     toolchain is xtensa GCC 8.4 (ARCHITECTURE §9, 2026-08-07); dc_engine_
//     target_check compiles this very TU with it.
//   * NO FLOATING POINT ANYWHERE NEAR A PRICE. This file finds the price token's
//     bytes and hands them to anvil_scaling.hpp, which is the same code the
//     nlohmann path calls. That shared header is why a streaming rewrite cannot
//     quietly break invariant #3.
//
// EQUIVALENCE IS THE POINT. The acceptance gate is test_replay_goldens.cpp and
// test_anvil_adapter.cpp passing UNCHANGED when linked against this file instead
// of dc_engine_anvil. So this is not "a JSON parser good enough for Anvil": it
// is a deliberate re-implementation of what nlohmann 3.11.3 accepts and rejects,
// including the parts Anvil never exercises, because the reference decoder's
// answers are the goldens. Every non-obvious rule below is annotated with the
// nlohmann behaviour it is reproducing. The four measured divergences are listed
// at the bottom of this comment block.
//
// SHAPE OF THE ALGORITHM — scan once, adjudicate at the end.
//
// The reference builds a whole DOM and *then* asks it questions in a fixed
// order: type, seq, ticker, then payload. A left-to-right scanner meets the
// fields in wire order instead, and those two orders disagree about which error
// to report when a frame has more than one thing wrong with it — which would
// show up as a frame landing in `price_errors` instead of `other_ticker` in the
// adapter's stats, i.e. a moved golden. So this parser never returns early on a
// semantic problem. It scans the whole document, validating syntax and recording
// one outcome slot per field (last occurrence wins, exactly as a std::map DOM
// does with duplicate keys), and only then replays the reference's decision
// order over those slots. `adjudicate()` below is a line-for-line mirror of
// nlohmann_parse_into(), and that is the property to preserve if this file is
// ever edited.
//
// DELIBERATE DIVERGENCES. Exactly three, all outside anything Anvil can emit,
// none reachable from either committed trace or the malformed-frame corpus. The
// boundaries below were measured against the reference, not estimated, and
// test_streaming_parser.cpp pins each one on both sides — so if you are here
// because you want to widen or close one, that test is what will tell you what
// you changed.
//
//   1. ONE SKIPPED VALUE may nest at most kMaxJsonDepth (64) containers; the
//      65th is NotJson. Note the scope: the budget belongs to a single
//      skip_value() call, not to the document, and the containers the frame's
//      own grammar consumes (the top-level object, a bids array, a level
//      object) are not counted against it. So a frame is only rejected once one
//      *value* nests 65 deep — "the document is deeper than 64" is not the
//      threshold, and the real rule is narrower than that. nlohmann has no
//      limit at all because it heap-allocates its parse stack; we hold ours in
//      one uint64_t, which is what keeps the skipper iterative and heap-free.
//      Anvil's deepest frame is 4 levels.
//   2. An *escaped* PRICE string whose unescaped form reaches kMaxTokenChars
//      (65 bytes and up) is BadPrice rather than its value. Only price can
//      diverge: escaped `type`, `aggr` and object keys get the identical answer
//      either way at any length, because no long string equals "book" or "B" or
//      one of our key names. Unescaped strings are sliced in place and are not
//      subject to the limit at all — verified out to 500,000 characters — so
//      this needs a price written with a \u escape AND padded past 64 bytes,
//      which no JSON serialiser produces.
//   3. Numbers in the open band (DBL_MAX, 1e309) are treated as finite floats
//      where nlohmann rejects the whole document as out_of_range. At or below
//      DBL_MAX, and at or above 1e309, the two agree exactly. The integer test
//      in scan_number() is tight rather than approximate: an accepted token is
//      strictly below 10^309, and a rejected one is at least 10^309, which is
//      always infinite — so it can never reject a value nlohmann finds finite
//      except inside that band.
//
// And one rule that is NOT a divergence but is called out because it is the
// expensive-looking one that was kept rather than dropped: invalid UTF-8 is
// rejected, with nlohmann's exact byte ranges. See scan_utf8_sequence().
#include "depthcharge/anvil/anvil_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "depthcharge/anvil/anvil_scaling.hpp"
#include "depthcharge/decimal.hpp"
#include "depthcharge/feed_event.hpp"
#include "depthcharge/symbol.hpp"

namespace depthcharge::anvil {
namespace {

// Container-kind stack width for the generic skipper. One bit per open bracket,
// held in a single uint64_t, which is what keeps the skipper both iterative and
// allocation-free. Divergence 1 above.
constexpr std::size_t kMaxJsonDepth = 64;

// Scratch for a token that had to be unescaped. Sized well past every token this
// parser compares against: the longest is "snapshot" (8), and the longest price
// parse_scaled can accept without overflowing int64 is 21 characters. Divergence
// 2 above.
constexpr std::size_t kMaxTokenChars = 64;

constexpr bool is_digit(unsigned char c) noexcept { return c >= '0' && c <= '9'; }

// A decoded JSON string. `text` is a view either straight into the input buffer
// (the escape-free case — no copy, which is every frame Anvil has ever sent) or
// into the parser's scratch. `fits` is false when an escaped token was longer
// than the scratch; a caller must then treat it as unrecognised rather than
// compare a truncated prefix.
struct StringToken {
    std::string_view text{};
    bool fits = true;
};

// A decoded JSON number. `is_integer` reproduces nlohmann's is_number_integer():
// true only when the literal has no fraction and no exponent AND fits in
// int64/uint64 — an over-range integer literal becomes a float there, and so
// must be rejected as a shape error rather than saturated here.
struct NumberToken {
    std::int64_t value = 0;
    bool is_integer = false;
};

// Which top-level / level key we just read. Comparing once into an enum keeps
// the dispatch a switch instead of a chain of string compares per member.
enum class TopKey : std::uint8_t { Other, Type, Seq, Ticker, Bids, Asks, Price, Qty, Aggr };
enum class LevelKey : std::uint8_t { Other, Price, Qty };

class FrameParser {
public:
    FrameParser(std::string_view text, const SymbolSpec& spec, AnvilFrame& out) noexcept
        : p_(text.data()), end_(text.data() + text.size()), spec_(spec), out_(out) {}

    ParseStatus run() noexcept {
        // A spec the adapter cannot decode against is a shape error like any
        // other, and checking it here costs one compare per frame instead of one
        // per level (~250 at Anvil's live depth).
        if (!spec_.valid()) { return ParseStatus::BadShape; }

        if (!skip_bom()) { return ParseStatus::NotJson; }
        skip_ws();
        // nlohmann parses the whole document and then asks is_object(); a
        // non-object and a syntax error both come back as NotJson, so refusing
        // anything that does not open with '{' is the same answer, sooner.
        if (at_input_end() || peek() != '{') { return ParseStatus::NotJson; }
        if (!scan_top_object()) { return ParseStatus::NotJson; }
        skip_ws();
        if (!at_input_end()) { return ParseStatus::NotJson; }  // trailing content

        return adjudicate();
    }

private:
    // --- cursor -------------------------------------------------------------

    unsigned char peek() const noexcept { return static_cast<unsigned char>(*p_); }

    // A NUL byte reads as end-of-input outside a string. This is not tidiness:
    // nlohmann's lexer returns end_of_input for '\0' even when handed an
    // explicit [first, last) range, so `{"a":1}\0junk` parses cleanly there. A
    // scanner that ran to `last` and rejected the junk would diverge.
    bool at_input_end() const noexcept { return p_ == end_ || *p_ == '\0'; }

    void skip_ws() noexcept {
        // Exactly nlohmann's set. Vertical tab and form feed are NOT whitespace
        // in JSON and must fail.
        while (p_ != end_) {
            const char c = *p_;
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') { break; }
            ++p_;
        }
    }

    // Skip whitespace and report whether a token actually follows.
    bool want_token() noexcept {
        skip_ws();
        return !at_input_end();
    }

    // A UTF-8 BOM is consumed only at offset 0, and a partial one is an error.
    bool skip_bom() noexcept {
        if (p_ == end_ || static_cast<unsigned char>(*p_) != 0xEFu) { return true; }
        if (end_ - p_ < 3) { return false; }
        if (static_cast<unsigned char>(p_[1]) != 0xBBu ||
            static_cast<unsigned char>(p_[2]) != 0xBFu) {
            return false;
        }
        p_ += 3;
        return true;
    }

    // --- token scanners -----------------------------------------------------
    //
    // Every scanner returns false for a SYNTAX error only. Semantic problems
    // (wrong JSON type for a field we care about, a price the scale cannot hold)
    // are recorded in the slots below and decided in adjudicate(), because
    // nlohmann decides them after the whole document has parsed.

    void push(unsigned char b) noexcept {
        if (scratch_len_ < kMaxTokenChars) {
            scratch_[scratch_len_++] = static_cast<char>(b);
        } else {
            scratch_fits_ = false;
        }
    }

    void push_codepoint(std::uint32_t cp) noexcept {
        if (cp < 0x80u) {
            push(static_cast<unsigned char>(cp));
        } else if (cp < 0x800u) {
            push(static_cast<unsigned char>(0xC0u | (cp >> 6)));
            push(static_cast<unsigned char>(0x80u | (cp & 0x3Fu)));
        } else if (cp < 0x10000u) {
            push(static_cast<unsigned char>(0xE0u | (cp >> 12)));
            push(static_cast<unsigned char>(0x80u | ((cp >> 6) & 0x3Fu)));
            push(static_cast<unsigned char>(0x80u | (cp & 0x3Fu)));
        } else {
            push(static_cast<unsigned char>(0xF0u | (cp >> 18)));
            push(static_cast<unsigned char>(0x80u | ((cp >> 12) & 0x3Fu)));
            push(static_cast<unsigned char>(0x80u | ((cp >> 6) & 0x3Fu)));
            push(static_cast<unsigned char>(0x80u | (cp & 0x3Fu)));
        }
    }

    bool read_hex4(std::uint32_t& cp) noexcept {
        if (end_ - p_ < 4) { return false; }
        cp = 0;
        for (int i = 0; i < 4; ++i) {
            const unsigned char c = static_cast<unsigned char>(p_[i]);
            std::uint32_t nibble = 0;
            if (c >= '0' && c <= '9') {
                nibble = static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                nibble = static_cast<std::uint32_t>(c - 'a') + 10u;
            } else if (c >= 'A' && c <= 'F') {
                nibble = static_cast<std::uint32_t>(c - 'A') + 10u;
            } else {
                return false;
            }
            cp = (cp << 4) | nibble;
        }
        p_ += 4;
        return true;
    }

    // p_ sits on the character after the backslash.
    bool scan_escape(bool capture) noexcept {
        if (p_ == end_) { return false; }
        const char e = *p_;
        switch (e) {
            case '"': case '\\': case '/':
                if (capture) { push(static_cast<unsigned char>(e)); }
                ++p_;
                return true;
            case 'b': if (capture) { push(0x08u); } ++p_; return true;
            case 'f': if (capture) { push(0x0Cu); } ++p_; return true;
            case 'n': if (capture) { push(0x0Au); } ++p_; return true;
            case 'r': if (capture) { push(0x0Du); } ++p_; return true;
            case 't': if (capture) { push(0x09u); } ++p_; return true;
            case 'u': break;
            default:  return false;   // forbidden character after backslash
        }
        ++p_;  // past 'u'

        std::uint32_t cp = 0;
        if (!read_hex4(cp)) { return false; }
        if (cp >= 0xD800u && cp <= 0xDBFFu) {
            // A high surrogate must be followed by a literal \u and a low one.
            if (end_ - p_ < 2 || p_[0] != '\\' || p_[1] != 'u') { return false; }
            p_ += 2;
            std::uint32_t low = 0;
            if (!read_hex4(low)) { return false; }
            if (low < 0xDC00u || low > 0xDFFFu) { return false; }
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            return false;  // lone low surrogate
        }
        if (capture) { push_codepoint(cp); }
        return true;
    }

    // Validate one multi-byte UTF-8 sequence with nlohmann's exact byte ranges:
    // overlong forms, encoded surrogates (ED A0..BF) and anything above U+10FFFF
    // are rejected, not replaced. Kept rather than dropped because the reference
    // rejects such a frame outright and the adapter counts it — dropping the
    // check would make a corrupt frame a *decoded* frame on the target only.
    bool scan_utf8_sequence(bool capture) noexcept {
        const unsigned char b0 = peek();
        std::size_t len = 0;
        unsigned char lo1 = 0x80u;
        unsigned char hi1 = 0xBFu;

        if (b0 >= 0xC2u && b0 <= 0xDFu)      { len = 2; }
        else if (b0 == 0xE0u)                { len = 3; lo1 = 0xA0u; }
        else if (b0 >= 0xE1u && b0 <= 0xECu) { len = 3; }
        else if (b0 == 0xEDu)                { len = 3; hi1 = 0x9Fu; }
        else if (b0 >= 0xEEu && b0 <= 0xEFu) { len = 3; }
        else if (b0 == 0xF0u)                { len = 4; lo1 = 0x90u; }
        else if (b0 >= 0xF1u && b0 <= 0xF3u) { len = 4; }
        else if (b0 == 0xF4u)                { len = 4; hi1 = 0x8Fu; }
        else { return false; }  // 0x80..0xC1 and 0xF5..0xFF are never a lead byte

        if (static_cast<std::size_t>(end_ - p_) < len) { return false; }
        for (std::size_t i = 1; i < len; ++i) {
            const unsigned char c = static_cast<unsigned char>(p_[i]);
            const unsigned char lo = (i == 1) ? lo1 : 0x80u;
            const unsigned char hi = (i == 1) ? hi1 : 0xBFu;
            if (c < lo || c > hi) { return false; }
        }
        if (capture) {
            for (std::size_t i = 0; i < len; ++i) {
                push(static_cast<unsigned char>(p_[i]));
            }
        }
        p_ += len;
        return true;
    }

    // p_ sits on the opening quote. `capture` off means "validate and discard",
    // which is what the generic skipper wants and costs no scratch traffic.
    bool scan_string(StringToken& tok, bool capture) noexcept {
        ++p_;  // opening quote
        const char* const raw_begin = p_;
        bool escaped = false;
        if (capture) {
            scratch_len_ = 0;
            scratch_fits_ = true;
        }

        for (;;) {
            if (p_ == end_) { return false; }  // unterminated
            const unsigned char b = peek();
            if (b == '"') { break; }
            if (b < 0x20u) { return false; }   // raw control character
            if (b == '\\') {
                escaped = true;
                ++p_;
                if (!scan_escape(capture)) { return false; }
                continue;
            }
            if (b < 0x80u) {
                if (capture) { push(b); }
                ++p_;
                continue;
            }
            if (!scan_utf8_sequence(capture)) { return false; }
        }

        const char* const raw_end = p_;
        ++p_;  // closing quote

        if (capture) {
            if (escaped) {
                tok.text = std::string_view(scratch_, scratch_len_);
                tok.fits = scratch_fits_;
            } else {
                // The whole point of the streaming design: the common path is a
                // view straight into the received buffer, no copy at all.
                tok.text = std::string_view(raw_begin,
                                            static_cast<std::size_t>(raw_end - raw_begin));
                tok.fits = true;
            }
        }
        return true;
    }

    bool scan_number(NumberToken& tok) noexcept {
        bool negative = false;
        if (peek() == '-') {
            negative = true;
            ++p_;
            if (p_ == end_) { return false; }
        }

        std::uint64_t magnitude = 0;
        bool magnitude_overflow = false;
        std::size_t int_digits = 0;
        bool any_nonzero = false;

        const unsigned char first = static_cast<unsigned char>(*p_);
        if (first == '0') {
            // A leading zero ends the integer part. nlohmann does the same and
            // then trips over the leftover digit as trailing content, so "01"
            // fails there too — our caller fails it the same way, on the
            // unexpected character where a ',' or '}' was due.
            ++p_;
        } else if (is_digit(first)) {
            while (p_ != end_ && is_digit(static_cast<unsigned char>(*p_))) {
                const unsigned d = static_cast<unsigned>(*p_ - '0');
                if (d != 0) { any_nonzero = true; }
                if (!magnitude_overflow) {
                    constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
                    if (magnitude > (kMax - d) / 10u) {
                        magnitude_overflow = true;
                    } else {
                        magnitude = magnitude * 10u + d;
                    }
                }
                ++int_digits;
                ++p_;
            }
        } else {
            return false;
        }

        bool is_float = false;
        std::size_t frac_leading_zeros = 0;
        if (p_ != end_ && *p_ == '.') {
            ++p_;
            if (p_ == end_ || !is_digit(static_cast<unsigned char>(*p_))) { return false; }
            bool still_leading = true;
            while (p_ != end_ && is_digit(static_cast<unsigned char>(*p_))) {
                if (*p_ == '0' && still_leading) {
                    ++frac_leading_zeros;
                } else {
                    still_leading = false;
                    any_nonzero = true;
                }
                ++p_;
            }
            is_float = true;
        }

        std::int64_t exponent = 0;
        if (p_ != end_ && (*p_ == 'e' || *p_ == 'E')) {
            ++p_;
            bool exp_negative = false;
            if (p_ != end_ && (*p_ == '+' || *p_ == '-')) {
                exp_negative = (*p_ == '-');
                ++p_;
            }
            if (p_ == end_ || !is_digit(static_cast<unsigned char>(*p_))) { return false; }
            std::int64_t e = 0;
            while (p_ != end_ && is_digit(static_cast<unsigned char>(*p_))) {
                if (e < 100000) { e = e * 10 + (*p_ - '0'); }
                ++p_;
            }
            exponent = exp_negative ? -e : e;
            is_float = true;
        }

        if (!is_float && !magnitude_overflow) {
            constexpr std::uint64_t kInt64MinMagnitude = 9223372036854775808ULL;
            if (negative) {
                if (magnitude <= kInt64MinMagnitude) {
                    tok.is_integer = true;
                    tok.value = (magnitude == kInt64MinMagnitude)
                                    ? std::numeric_limits<std::int64_t>::min()
                                    : -static_cast<std::int64_t>(magnitude);
                }
            } else {
                // nlohmann stores this as number_unsigned; is_number_integer() is
                // true, and get<int64_t>() is an unchecked static_cast that wraps
                // past INT64_MAX. The reference decoder then rejects the negative
                // result in its `raw < 0` guard, so reproduce the wrap rather
                // than rejecting here.
                tok.is_integer = true;
                tok.value = static_cast<std::int64_t>(magnitude);
            }
        }

        if (!tok.is_integer && any_nonzero) {
            // nlohmann converts a float token with std::strtod and rejects the
            // WHOLE DOCUMENT (out_of_range 406) if the result is not finite.
            const std::int64_t significand_exp =
                (int_digits > 0) ? static_cast<std::int64_t>(int_digits)
                                 : -static_cast<std::int64_t>(frac_leading_zeros);
            if (significand_exp + exponent > 309) { return false; }  // divergence 3
        }
        return true;
    }

    bool scan_literal(const char* lit, std::size_t n) noexcept {
        if (static_cast<std::size_t>(end_ - p_) < n) { return false; }
        for (std::size_t i = 0; i < n; ++i) {
            if (p_[i] != lit[i]) { return false; }
        }
        p_ += n;
        return true;
    }

    bool scan_scalar() noexcept {
        const unsigned char c = peek();
        if (c == '"') {
            StringToken ignored;
            return scan_string(ignored, /*capture=*/false);
        }
        if (c == '-' || is_digit(c)) {
            NumberToken ignored;
            return scan_number(ignored);
        }
        if (c == 't') { return scan_literal("true", 4); }
        if (c == 'f') { return scan_literal("false", 5); }
        if (c == 'n') { return scan_literal("null", 4); }
        return false;
    }

    // Consume and validate one JSON value of any shape, discarding it.
    //
    // Iterative by design (see the file header): `kinds` holds one bit per open
    // container, 1 for array and 0 for object, so closing brackets are matched
    // without a call stack and without allocating.
    bool skip_value() noexcept {
        std::uint64_t kinds = 0;
        std::size_t depth = 0;
        bool need_value = true;

        for (;;) {
            if (need_value) {
                if (!want_token()) { return false; }
                const unsigned char c = peek();
                if (c == '{' || c == '[') {
                    if (depth >= kMaxJsonDepth) { return false; }  // divergence 1
                    const std::uint64_t bit = std::uint64_t{1} << depth;
                    if (c == '[') { kinds |= bit; } else { kinds &= ~bit; }
                    ++depth;
                    ++p_;
                    if (!want_token()) { return false; }
                    if (peek() == (c == '[' ? ']' : '}')) {
                        ++p_;
                        --depth;
                        need_value = false;
                        continue;
                    }
                    if (c == '{' && !skip_member_key()) { return false; }
                    continue;  // the member/element value is still owed
                }
                if (!scan_scalar()) { return false; }
                need_value = false;
                continue;
            }

            if (depth == 0) { return true; }
            if (!want_token()) { return false; }
            const bool in_array = ((kinds >> (depth - 1)) & 1u) != 0u;
            if (peek() == ',') {
                ++p_;
                if (!in_array && !skip_member_key()) { return false; }
                need_value = true;
                continue;
            }
            if (peek() == (in_array ? ']' : '}')) {
                ++p_;
                --depth;
                continue;
            }
            return false;
        }
    }

    // `"key" :` inside a value we are discarding — validated, never compared.
    bool skip_member_key() noexcept {
        if (!want_token() || peek() != '"') { return false; }
        StringToken ignored;
        if (!scan_string(ignored, /*capture=*/false)) { return false; }
        if (!want_token() || peek() != ':') { return false; }
        ++p_;
        return true;
    }

    // `"key" :` for an object whose members we do care about.
    bool read_member_key(StringToken& key) noexcept {
        if (!want_token() || peek() != '"') { return false; }
        if (!scan_string(key, /*capture=*/true)) { return false; }
        if (!want_token() || peek() != ':') { return false; }
        ++p_;
        return want_token();
    }

    // --- typed field readers -----------------------------------------------
    //
    // A field of ours appears in two places on this wire — `price` and `qty` are
    // both a trade's own members and a book level's — and the rules for reading
    // one are fiddly enough (which JSON type the reference demands, which
    // ParseStatus each way of being wrong maps to, what happens when the token
    // will not fit the scratch) that writing them out twice would be two places
    // to drift. They are read here, once, and the caller only supplies the
    // destination and its error slot.
    //
    // All three consume and validate the value whatever it turns out to be, and
    // return false ONLY on a syntax error. A wrong JSON type is recorded, not
    // returned: the reference had already parsed the whole document before it
    // noticed, so we must too.

    // A value the reference reads with is_string() / get_ptr<std::string>().
    // `is_string` is false when it was some other JSON type; `tok.fits` is a
    // separate question (see divergence 2), so the two are not collapsed —
    // a `type` that is a too-long string is still a string, and must not be
    // reported as a missing one.
    bool read_string_value(StringToken& tok, bool& is_string) noexcept {
        if (peek() != '"') {
            is_string = false;
            return skip_value();
        }
        is_string = true;
        return scan_string(tok, /*capture=*/true);
    }

    // A value the reference reads with is_number_integer(). `is_integer` folds
    // in nlohmann's rule that a fraction, an exponent, or an over-range literal
    // all make it a float and therefore not an integer.
    bool read_integer_value(bool& is_integer, std::int64_t& value) noexcept {
        const unsigned char c = peek();
        if (c != '-' && !is_digit(c)) {
            is_integer = false;
            return skip_value();
        }
        NumberToken num;
        if (!scan_number(num)) { return false; }
        is_integer = num.is_integer;
        value = num.value;
        return true;
    }

    // A price: a JSON string, converted by the shared scaling header. `error` is
    // set to Ok on success, and to the status the reference would have produced
    // otherwise.
    bool read_price_value(PriceTicks& out, ParseStatus& error) noexcept {
        error = ParseStatus::Ok;
        StringToken tok;
        bool is_string = false;
        if (!read_string_value(tok, is_string)) { return false; }
        if (!is_string) {
            error = ParseStatus::BadShape;  // a JSON number is not a price
        } else if (!tok.fits) {
            error = ParseStatus::BadPrice;  // divergence 2
        } else {
            (void)price_text_to_ticks(tok.text, spec_, out, error);
        }
        return true;
    }

    // A quantity: a JSON integer, converted by the shared scaling header.
    bool read_qty_value(Qty& out, ParseStatus& error) noexcept {
        error = ParseStatus::Ok;
        bool is_integer = false;
        std::int64_t value = 0;
        if (!read_integer_value(is_integer, value)) { return false; }
        if (!is_integer) {
            error = ParseStatus::BadShape;
        } else {
            (void)wire_qty_to_steps(value, spec_, out, error);
        }
        return true;
    }

    // --- the frame's own grammar -------------------------------------------

    static TopKey classify_top(const StringToken& key) noexcept {
        if (!key.fits) { return TopKey::Other; }
        const std::string_view k = key.text;
        if (k == "type")   { return TopKey::Type; }
        if (k == "seq")    { return TopKey::Seq; }
        if (k == "ticker") { return TopKey::Ticker; }
        if (k == "bids")   { return TopKey::Bids; }
        if (k == "asks")   { return TopKey::Asks; }
        if (k == "price")  { return TopKey::Price; }
        if (k == "qty")    { return TopKey::Qty; }
        if (k == "aggr")   { return TopKey::Aggr; }
        return TopKey::Other;
    }

    static LevelKey classify_level(const StringToken& key) noexcept {
        if (!key.fits) { return LevelKey::Other; }
        if (key.text == "price") { return LevelKey::Price; }
        if (key.text == "qty")   { return LevelKey::Qty; }
        return LevelKey::Other;
    }

    bool scan_top_object() noexcept {
        ++p_;  // '{'
        if (!want_token()) { return false; }
        if (peek() == '}') {
            ++p_;
            return true;
        }
        for (;;) {
            StringToken key;
            if (!read_member_key(key)) { return false; }
            const TopKey which = classify_top(key);
            // `key.text` may point at the scratch, which the value scan is about
            // to reuse — hence classify first, then scan.
            if (!scan_top_member(which)) { return false; }
            if (!want_token()) { return false; }
            if (peek() == ',') {
                ++p_;
                continue;
            }
            if (peek() == '}') {
                ++p_;
                return true;
            }
            return false;
        }
    }

    // Every branch assigns its slot unconditionally, so a repeated key overwrites
    // the earlier one — which is what a std::map-backed DOM does (nlohmann's
    // key() uses operator[] and then assigns over the existing element, so the
    // LAST occurrence survives). Getting this backwards is the classic
    // hand-rolled-parser divergence.
    bool scan_top_member(TopKey which) noexcept {
        switch (which) {
            case TopKey::Type: {
                StringToken tok;
                bool is_string = false;
                if (!read_string_value(tok, is_string)) { return false; }
                type_is_string_ = is_string;
                // A string too long for the scratch is still a string — so not
                // MissingType — but it cannot equal any kind we know.
                kind_ = (is_string && tok.fits) ? kind_from_type(tok.text) : FrameKind::Unknown;
                return true;
            }
            case TopKey::Seq:
                return read_integer_value(seq_is_integer_, seq_value_);
            case TopKey::Ticker:
                ticker_present_ = true;
                return read_integer_value(ticker_is_integer_, ticker_value_);
            case TopKey::Bids:
                has_bids_ = true;
                return scan_side(out_.bids, bid_count_, bids_truncated_, bids_error_);
            case TopKey::Asks:
                has_asks_ = true;
                return scan_side(out_.asks, ask_count_, asks_truncated_, asks_error_);
            case TopKey::Price:
                has_price_ = true;
                return read_price_value(trade_px_, price_error_);
            case TopKey::Qty:
                has_qty_ = true;
                return read_qty_value(trade_qty_, qty_error_);
            case TopKey::Aggr: {
                has_aggr_ = true;
                aggr_error_ = ParseStatus::Ok;
                StringToken tok;
                bool is_string = false;
                if (!read_string_value(tok, is_string)) { return false; }
                if (!is_string || !tok.fits || !aggressor_from_text(tok.text, aggressor_)) {
                    aggr_error_ = ParseStatus::BadShape;
                }
                return true;
            }
            case TopKey::Other:
                return skip_value();
        }
        return skip_value();  // unreachable; keeps every compiler quiet
    }

    // One side of a book/snapshot frame. Levels arrive best-first and stay in
    // that order; anything past kMaxSnapshotLevels is dropped off the deep end
    // (legal — depth beyond N is unknown, not zero) and flagged.
    //
    // Once a level has failed, later levels are still *validated* (nlohmann had
    // already parsed the whole document before it noticed) but no longer stored,
    // and the first failure is the one reported.
    bool scan_side(BookLevel* dst, std::uint32_t& count, bool& truncated,
                   ParseStatus& error) noexcept {
        count = 0;
        truncated = false;
        error = ParseStatus::Ok;

        if (peek() != '[') {
            error = ParseStatus::BadShape;  // nlohmann: !arr.is_array()
            return skip_value();
        }
        ++p_;
        if (!want_token()) { return false; }
        if (peek() == ']') {
            ++p_;
            return true;  // an empty book is a legal Snapshot, not an error
        }
        for (;;) {
            if (!want_token()) { return false; }
            if (peek() != '{') {
                if (error == ParseStatus::Ok) { error = ParseStatus::BadShape; }
                if (!skip_value()) { return false; }
            } else if (!scan_level(dst, count, truncated, error)) {
                return false;
            }
            if (!want_token()) { return false; }
            if (peek() == ',') {
                ++p_;
                continue;
            }
            if (peek() == ']') {
                ++p_;
                return true;
            }
            return false;
        }
    }

    bool scan_level(BookLevel* dst, std::uint32_t& count, bool& truncated,
                    ParseStatus& error) noexcept {
        bool has_price = false;
        bool has_qty = false;
        PriceTicks px = 0;
        Qty qty = 0;
        ParseStatus price_error = ParseStatus::Ok;
        ParseStatus qty_error = ParseStatus::Ok;

        ++p_;  // '{'
        if (!want_token()) { return false; }
        if (peek() == '}') {
            ++p_;
        } else {
            for (;;) {
                StringToken key;
                if (!read_member_key(key)) { return false; }
                // A level's price and qty are read by exactly the same rules as
                // a trade's — same JSON types, same status mapping — so they go
                // through the same two readers.
                switch (classify_level(key)) {
                    case LevelKey::Price:
                        has_price = true;
                        if (!read_price_value(px, price_error)) { return false; }
                        break;
                    case LevelKey::Qty:
                        has_qty = true;
                        if (!read_qty_value(qty, qty_error)) { return false; }
                        break;
                    case LevelKey::Other:
                        if (!skip_value()) { return false; }
                        break;
                }

                if (!want_token()) { return false; }
                if (peek() == ',') {
                    ++p_;
                    continue;
                }
                if (peek() == '}') {
                    ++p_;
                    break;
                }
                return false;
            }
        }

        if (error != ParseStatus::Ok) { return true; }  // an earlier level already failed

        // nlohmann's order inside a level: both keys must exist, then price, then
        // qty. Reproduced exactly, because the three land in different adapter
        // counters (parse_errors vs price_errors).
        if (!has_price || !has_qty) {
            error = ParseStatus::BadShape;
        } else if (price_error != ParseStatus::Ok) {
            error = price_error;
        } else if (qty_error != ParseStatus::Ok) {
            error = qty_error;
        } else if (count < kMaxSnapshotLevels) {
            dst[count].px = px;
            dst[count].qty = qty;
            ++count;
        } else {
            truncated = true;
        }
        return true;
    }

    // --- the decision ------------------------------------------------------
    //
    // A line-for-line mirror of the reference's parse_into(), run over the slots
    // the scan filled instead of over a DOM. Keep the order.
    ParseStatus adjudicate() noexcept {
        if (!type_is_string_) { return ParseStatus::MissingType; }
        const FrameKind kind = kind_;

        // Decoded for diagnostics only; never used for ordering (ARCHITECTURE §4).
        if (seq_is_integer_) {
            out_.wire_seq = seq_value_;
            out_.has_wire_seq = true;
        }

        // `summary` is cross-ticker and carries no "ticker" field; every other
        // kind names the ticker it belongs to, and one that is not ours is
        // dropped before any payload work.
        if (kind != FrameKind::Summary && kind != FrameKind::Unknown) {
            if (!ticker_present_ || !ticker_is_integer_) { return ParseStatus::BadShape; }
            if (ticker_value_ < 0) { return ParseStatus::BadShape; }
            out_.ticker = static_cast<std::uint32_t>(ticker_value_);
            out_.has_ticker = true;
            if (out_.ticker != spec_.id) { return ParseStatus::OtherTicker; }
        }

        switch (kind) {
            case FrameKind::Snapshot:
            case FrameKind::Book: {
                if (!has_bids_ || !has_asks_) { return ParseStatus::BadShape; }
                if (bids_error_ != ParseStatus::Ok) { return bids_error_; }
                if (asks_error_ != ParseStatus::Ok) { return asks_error_; }
                out_.bid_count = bid_count_;
                out_.ask_count = ask_count_;
                out_.levels_truncated = bids_truncated_ || asks_truncated_;
                break;
            }
            case FrameKind::Trade: {
                if (!has_price_ || !has_qty_ || !has_aggr_) { return ParseStatus::BadShape; }
                if (price_error_ != ParseStatus::Ok) { return price_error_; }
                if (qty_error_ != ParseStatus::Ok) { return qty_error_; }
                if (aggr_error_ != ParseStatus::Ok) { return aggr_error_; }
                out_.trade_px = trade_px_;
                out_.trade_qty = trade_qty_;
                out_.aggressor = aggressor_;
                break;
            }
            case FrameKind::Summary:
            case FrameKind::Unknown:
                // No payload this milestone cares about — and, exactly as in the
                // reference, no payload *error* either: a summary frame carrying
                // a malformed `bids` is still a summary. The scan may have
                // written levels into out_; they are simply not adopted, and
                // reset() left the counts at zero.
                break;
        }

        out_.kind = kind;
        return ParseStatus::Ok;
    }

    // --- state -------------------------------------------------------------

    const char* p_;
    const char* const end_;
    const SymbolSpec& spec_;
    AnvilFrame& out_;

    char scratch_[kMaxTokenChars]{};
    std::size_t scratch_len_ = 0;
    bool scratch_fits_ = true;

    bool type_is_string_ = false;
    FrameKind kind_ = FrameKind::Unknown;

    bool seq_is_integer_ = false;
    std::int64_t seq_value_ = 0;

    bool ticker_present_ = false;
    bool ticker_is_integer_ = false;
    std::int64_t ticker_value_ = 0;

    bool has_bids_ = false;
    bool has_asks_ = false;
    std::uint32_t bid_count_ = 0;
    std::uint32_t ask_count_ = 0;
    bool bids_truncated_ = false;
    bool asks_truncated_ = false;
    ParseStatus bids_error_ = ParseStatus::Ok;
    ParseStatus asks_error_ = ParseStatus::Ok;

    bool has_price_ = false;
    bool has_qty_ = false;
    bool has_aggr_ = false;
    ParseStatus price_error_ = ParseStatus::Ok;
    ParseStatus qty_error_ = ParseStatus::Ok;
    ParseStatus aggr_error_ = ParseStatus::Ok;
    PriceTicks trade_px_ = 0;
    Qty trade_qty_ = 0;
    Side aggressor_ = Side::Bid;
};

}  // namespace

ParseStatus parse_anvil_frame(std::string_view text, const SymbolSpec& spec,
                              AnvilFrame& out) noexcept {
    // The declared postcondition (anvil_frame.hpp), honoured in exactly the same
    // two lines as the reference: the parser owns the reset, on entry and on
    // every non-Ok exit. test_anvil_adapter.cpp asserts it against this
    // implementation too, over all 19 failure modes plus reentrancy.
    out.reset();
    FrameParser parser(text, spec, out);
    const ParseStatus st = parser.run();
    if (st != ParseStatus::Ok) { out.reset(); }
    return st;
}

}  // namespace depthcharge::anvil
