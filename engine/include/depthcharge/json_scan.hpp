// depthcharge/json_scan.hpp — the JSON scanning primitives, once.
//
// M5 stage B1. Extracted VERBATIM from `engine/src/kraken/
// kraken_frame_streaming.cpp`, where they were written at M4 stage B1 and have
// been held still by that venue's goldens ever since. Nothing here is new code;
// the diff that created this file moved lines and changed none of them, which is
// the property that makes the extraction checkable rather than hopeful.
//
// WHY THIS EXISTS NOW AND NOT AT M4, WHICH IS DESIGN STRAIN 25's WHOLE ARGUMENT.
// The card said the trigger to extract was a THIRD venue, because three
// instances distinguish the general primitives from two venues' accidents and
// two do not. Stage 0 then measured Binance's grammar and found it a STRICT
// SUBSET of what both existing scanners already handle, so the third scanner did
// not need building — and the card recorded, uncomfortably, that the trigger had
// arrived and fired at nothing. This file is what that answer looks like when
// it is acted on: the third venue reuses the second's scanner rather than
// getting one of its own, and the primitives below are exactly the ones two
// venues turned out to share.
//
// WHAT "STRICT SUBSET" MEANT, RE-MEASURED AT B1 OVER THE SEVEN COMMITTED SLICES
// rather than inherited from stage 0's summary of a capture window — because a
// grammar claim that was true of one window is not automatically true of the
// venue:
//
//     string escapes                   0
//     exponent characters outside a string   0
//     bare '.' outside a string        0   (a float token would appear here)
//     true / false / null literals     0
//     maximum container nesting        4
//     price / quantity entries   188,372, every one of them exactly 8 decimals,
//                                      zero in exponent notation, widest
//                                      integer part 6 digits
//
// So Binance needs `scan_string` (its prices are STRINGS, unescaped),
// `scan_number` (its ids and timestamps are bare integers) and `skip_value`.
// It needs no float path — which is why Anvil's scanner is the wrong one to
// reuse, since that one is deliberately bug-compatible with nlohmann 3.11.3's
// float handling and carries a specification Binance has no use for. It needs
// nothing this file does not already do, and **no venue flag was added to make
// the reuse work**: the brief's instruction was to stop and raise if one were
// needed, and none was.
//
// THE ESCAPE MACHINERY IS KEPT THOUGH NEITHER VENUE HAS EVER SENT ONE. It was
// written for Kraken, no captured frame in either venue's corpus exercises it,
// and removing it would be removing the only handling of a case JSON permits and
// the wire has simply not shown us yet. `\uXXXX` and surrogate pairs decode
// rather than reject, deliberately: a malformed escape in a field nobody reads
// must not reject the frame.
//
// NOT A PARSER. There is no grammar here and no venue vocabulary — a Scanner
// knows how to consume a JSON token and nothing about what the token means.
// Each venue's frame grammar stays in its own translation unit, which is where
// the two venues genuinely differ and where a shared abstraction would start
// needing flags.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace depthcharge::json {

constexpr bool is_digit(unsigned char c) noexcept { return c >= '0' && c <= '9'; }

// Longest token a scanner ever copies rather than slices. Only an ESCAPED string
// needs a copy, and the only escaped string that could matter is a symbol;
// "BTC/USD" is 7 bytes and no symbol at either venue approaches this.
inline constexpr std::size_t kMaxTokenChars = 64;

// Containers a single skip_value() may nest. Held in one uint64_t as a bit per
// level (0 = object, 1 = array), which is what keeps the skipper heap-free.
// Kraken's deepest frame is 4 levels and Binance's is also 4.
inline constexpr int kMaxSkipDepth = 64;

// A decoded string: a view into the input when it had no escapes (the case in
// every captured frame at both venues), or into `scratch_` when it did.
struct StringToken {
    std::string_view text{};
    bool too_long = false;  // an escaped string past kMaxTokenChars
};

// A number, UNCONVERTED. `text` is the verbatim token, which is the whole point:
// it goes to parse_scaled, not to strtod. Invariant #3 lives or dies here.
struct NumberToken {
    std::string_view text{};
};

// A cursor over one frame's bytes, with the JSON token grammar and nothing else.
//
// USED BY DERIVATION, not by composition, and that is a deliberate ergonomic
// choice rather than an OO one: the venue grammars manipulate the cursor
// directly (`++p_`, `peek()`, `p_ = saved`) dozens of times each, and routing
// every one through an accessor would have made the extraction a rewrite of the
// code it is trying to move unchanged. `p_` and `end_` are therefore protected.
// There is no virtual anything: a derived parser is a concrete type, resolved at
// compile time, on a path invariant #7 governs.
class Scanner {
public:
    explicit Scanner(std::string_view text) noexcept
        : p_(text.data()), end_(text.data() + text.size()) {}

    // ---- input -------------------------------------------------------------

    bool at_end() const noexcept { return p_ == end_; }
    unsigned char peek() const noexcept { return static_cast<unsigned char>(*p_); }

    void skip_ws() noexcept {
        while (p_ != end_) {
            const unsigned char c = static_cast<unsigned char>(*p_);
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++p_; } else { break; }
        }
    }

    // A token is required next; end of input means the document was truncated.
    bool want() noexcept {
        skip_ws();
        return !at_end();
    }

    bool eat(char c) noexcept {
        if (!want() || *p_ != c) { return false; }
        ++p_;
        return true;
    }

    // ---- scalars -----------------------------------------------------------

    // On entry p_ is at the opening quote. `capture` is false for a string whose
    // value nobody reads, which is most of them (timestamps, statuses, stream
    // names): the bytes are validated and discarded.
    bool scan_string(StringToken& tok, bool capture) noexcept {
        if (!want() || *p_ != '"') { return false; }
        ++p_;
        const char* start = p_;
        bool escaped = false;
        scratch_len_ = 0;
        scratch_over_ = false;

        while (true) {
            if (at_end()) { return false; }
            const unsigned char c = peek();
            if (c == '"') {
                ++p_;
                break;
            }
            if (c == '\\') {
                if (!escaped) {
                    // First escape: everything before it is verbatim, so seed
                    // the scratch with it and switch to copying.
                    escaped = true;
                    scratch_len_ = 0;
                    scratch_over_ = false;
                    for (const char* q = start; q != p_; ++q) {
                        push(static_cast<unsigned char>(*q));
                    }
                }
                ++p_;
                if (at_end()) { return false; }
                const unsigned char e = static_cast<unsigned char>(*p_++);
                switch (e) {
                    case '"':  push('"');  break;
                    case '\\': push('\\'); break;
                    case '/':  push('/');  break;
                    case 'b':  push('\b'); break;
                    case 'f':  push('\f'); break;
                    case 'n':  push('\n'); break;
                    case 'r':  push('\r'); break;
                    case 't':  push('\t'); break;
                    case 'u': {
                        std::uint32_t cp = 0;
                        if (!read_hex4(cp)) { return false; }
                        if (cp >= 0xD800u && cp <= 0xDBFFu) {
                            // High surrogate: a low one must follow, or this is
                            // a lone surrogate and becomes U+FFFD.
                            if (end_ - p_ >= 2 && p_[0] == '\\' && p_[1] == 'u') {
                                const char* save = p_;
                                p_ += 2;
                                std::uint32_t lo = 0;
                                if (read_hex4(lo) && lo >= 0xDC00u && lo <= 0xDFFFu) {
                                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                                } else {
                                    p_ = save;
                                    cp = 0xFFFDu;
                                }
                            } else {
                                cp = 0xFFFDu;
                            }
                        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
                            cp = 0xFFFDu;
                        }
                        push_codepoint(cp);
                        break;
                    }
                    default: return false;  // an escape JSON does not define
                }
                continue;
            }
            if (c < 0x20u) { return false; }  // raw control byte in a string
            if (escaped) { push(c); }
            ++p_;
        }

        if (!capture) {
            tok.text = std::string_view{};
            tok.too_long = false;
            return true;
        }
        if (escaped) {
            tok.too_long = scratch_over_;
            tok.text = std::string_view(scratch_, scratch_len_);
        } else {
            tok.too_long = false;
            tok.text = std::string_view(start, static_cast<std::size_t>(p_ - 1 - start));
        }
        return true;
    }

    // Validate a JSON number's syntax and hand back its verbatim bytes. NOTHING
    // IS CONVERTED HERE — not even the ones the caller will later reject.
    //
    // Exponent notation is accepted as SYNTAX and refused as a VALUE: the token
    // is sliced whole, and parse_scaled rejects an 'e' as BadFormat, which the
    // venue parser surfaces as its own BadPrice/BadQty. That is deliberate and
    // it is the honest answer, because a price in exponent notation is a scale
    // disagreement the declared SymbolSpec cannot hold exactly (ARCHITECTURE §4:
    // report, never round). Kraken's book channel has never sent one in 9,932
    // frames and Binance none in 188,372 entries; Kraken's `instrument` channel
    // does (`1e-08`, `5e-05`), which is precisely why the syntax must scan and
    // the value must not silently become something else.
    bool scan_number(NumberToken& tok) noexcept {
        if (!want()) { return false; }
        const char* start = p_;
        if (peek() == '-') { ++p_; }
        if (at_end() || !is_digit(peek())) { return false; }
        if (peek() == '0') {
            ++p_;
        } else {
            while (!at_end() && is_digit(peek())) { ++p_; }
        }
        if (!at_end() && peek() == '.') {
            ++p_;
            if (at_end() || !is_digit(peek())) { return false; }
            while (!at_end() && is_digit(peek())) { ++p_; }
        }
        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            ++p_;
            if (!at_end() && (peek() == '+' || peek() == '-')) { ++p_; }
            if (at_end() || !is_digit(peek())) { return false; }
            while (!at_end() && is_digit(peek())) { ++p_; }
        }
        tok.text = std::string_view(start, static_cast<std::size_t>(p_ - start));
        return true;
    }

    bool scan_literal(const char* lit, std::size_t n) noexcept {
        if (static_cast<std::size_t>(end_ - p_) < n) { return false; }
        if (std::memcmp(p_, lit, n) != 0) { return false; }
        p_ += n;
        return true;
    }

    // true / false / null / string / number — anything that is not a container.
    bool scan_scalar(bool* boolean_out = nullptr) noexcept {
        if (!want()) { return false; }
        switch (peek()) {
            case '"': {
                StringToken t;
                return scan_string(t, /*capture=*/false);
            }
            case 't':
                if (!scan_literal("true", 4)) { return false; }
                if (boolean_out != nullptr) { *boolean_out = true; }
                return true;
            case 'f':
                if (!scan_literal("false", 5)) { return false; }
                if (boolean_out != nullptr) { *boolean_out = false; }
                return true;
            case 'n':
                return scan_literal("null", 4);
            default: {
                NumberToken t;
                return scan_number(t);
            }
        }
    }

    // Skip one value of any shape, iteratively. The container-kind stack is one
    // uint64_t: bit k is 1 if the container at depth k is an array.
    bool skip_value() noexcept {
        if (!want()) { return false; }
        if (peek() != '{' && peek() != '[') { return scan_scalar(); }

        std::uint64_t kinds = 0;
        int depth = 0;
        const auto open = [&](bool is_array) noexcept {
            if (is_array) { kinds |= (std::uint64_t{1} << depth); }
            else { kinds &= ~(std::uint64_t{1} << depth); }
            ++depth;
            ++p_;
        };

        open(peek() == '[');

        while (depth > 0) {
            if (depth > kMaxSkipDepth) { return false; }
            if (!want()) { return false; }
            const bool in_array = (kinds & (std::uint64_t{1} << (depth - 1))) != 0;

            // An empty container, or the end of a populated one.
            if ((in_array && peek() == ']') || (!in_array && peek() == '}')) {
                ++p_;
                --depth;
                if (depth > 0) {
                    if (!want()) { return false; }
                    if (peek() == ',') { ++p_; }
                }
                continue;
            }

            if (!in_array) {
                StringToken key;
                if (!scan_string(key, /*capture=*/false)) { return false; }
                if (!eat(':')) { return false; }
            }

            if (!want()) { return false; }
            if (peek() == '{' || peek() == '[') {
                open(peek() == '[');
                continue;
            }
            if (!scan_scalar()) { return false; }
            if (!want()) { return false; }
            if (peek() == ',') { ++p_; }
        }
        return true;
    }

    // ---- key classification -------------------------------------------------
    //
    // Compared as views against literals rather than hashed or interned: the key
    // set is single digits long at both venues and a memcmp of a 7-byte string
    // is cheaper than any of the alternatives on an LX7.

    static bool key_is(const StringToken& k, std::string_view lit) noexcept {
        return k.text == lit;
    }

    static std::size_t copy_small(char* dst, std::size_t cap, std::string_view src) noexcept {
        const std::size_t n = src.size() < cap ? src.size() : cap;
        for (std::size_t i = 0; i < n; ++i) { dst[i] = src[i]; }
        return n;
    }

protected:
    const char* p_;
    const char* end_;

private:
    void push(unsigned char b) noexcept {
        if (scratch_len_ < kMaxTokenChars) { scratch_[scratch_len_++] = static_cast<char>(b); }
        else { scratch_over_ = true; }
    }

    // \uXXXX -> UTF-8. Surrogate pairs are decoded rather than rejected: a lone
    // surrogate is written through as the replacement character, which is what a
    // symbol comparison wants (it will simply not match) and never a parse
    // failure, because a malformed escape in a field we do not read must not
    // reject the frame.
    bool read_hex4(std::uint32_t& cp) noexcept {
        cp = 0;
        for (int i = 0; i < 4; ++i) {
            if (at_end()) { return false; }
            const unsigned char c = static_cast<unsigned char>(*p_++);
            std::uint32_t v = 0;
            if (is_digit(c)) { v = c - '0'; }
            else if (c >= 'a' && c <= 'f') { v = 10u + (c - 'a'); }
            else if (c >= 'A' && c <= 'F') { v = 10u + (c - 'A'); }
            else { return false; }
            cp = (cp << 4) | v;
        }
        return true;
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

    char scratch_[kMaxTokenChars]{};
    std::size_t scratch_len_ = 0;
    bool scratch_over_ = false;
};

}  // namespace depthcharge::json
