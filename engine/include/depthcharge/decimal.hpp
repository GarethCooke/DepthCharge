// depthcharge/decimal.hpp — exact decimal-string <-> scaled-integer conversion.
//
// Venues put prices on the wire as decimal strings ("10.012", "9.9972", "10").
// Invariant #3 says no floating point ever touches book data, so the string is
// converted straight to integer ticks here: parse the digits, scale by a
// declared number of decimal places, refuse anything that would lose precision.
// strtod/atof/std::stod are all deliberately absent.
//
// Everything below is constexpr, noexcept, allocation-free and freestanding-safe
// (invariants #1, #7): the same code runs in the harness and on the ESP32.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace depthcharge {

// Why a parse failed. `TooManyDecimals` is the important one: it means the wire
// carried more precision than the configured SymbolSpec scale, i.e. the scale is
// wrong. That is a loud error rather than a silent round (ARCHITECTURE §4).
enum class DecimalError : std::uint8_t {
    None = 0,
    Empty,             // no text at all
    BadFormat,         // stray characters, no digits, "1.", "-", "1e3", " 1"
    TooManyDecimals,   // more fractional digits than the declared scale
    Overflow,          // does not fit in std::int64_t at this scale
    BadScale,          // decimals outside [0, kMaxDecimals]
};

// Largest decimal scale we accept: 10^18 is the last power of ten inside
// int64_t, so anything beyond it could not represent even 1.0.
inline constexpr int kMaxDecimals = 18;

struct DecimalParse {
    std::int64_t  value = 0;
    DecimalError  error = DecimalError::None;

    constexpr bool ok() const noexcept { return error == DecimalError::None; }
    constexpr explicit operator bool() const noexcept { return ok(); }
};

namespace detail {

constexpr std::int64_t pow10_i64(int n) noexcept {
    std::int64_t v = 1;
    for (int i = 0; i < n; ++i) { v *= 10; }
    return v;
}

// v = v*10 + digit, guarding the multiply and the add. Returns false on overflow.
constexpr bool mul_add_checked(std::int64_t& v, int digit) noexcept {
    constexpr std::int64_t kMax = INT64_MAX;
    if (v > (kMax - digit) / 10) { return false; }
    v = v * 10 + digit;
    return true;
}

constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

}  // namespace detail

// Parse a plain decimal string into an integer scaled by 10^decimals.
//
//   parse_scaled("9.9972", 4)  -> 99972
//   parse_scaled("10",     4)  -> 100000
//   parse_scaled("10.01234", 4) -> TooManyDecimals   (never 100123 by rounding)
//
// Accepted grammar (deliberately narrower than JSON's number): an optional '-',
// one or more digits, optionally '.' followed by one or more digits. No '+', no
// exponent, no whitespace, no thousands separators. Trailing fractional zeros
// beyond the scale are *not* accepted either — "10.00000" at scale 4 is a scale
// disagreement worth reporting, and no venue we consume emits it.
constexpr DecimalParse parse_scaled(std::string_view text, int decimals) noexcept {
    if (decimals < 0 || decimals > kMaxDecimals) { return {0, DecimalError::BadScale}; }
    if (text.empty()) { return {0, DecimalError::Empty}; }

    std::size_t i = 0;
    bool negative = false;
    if (text[i] == '-') {
        negative = true;
        ++i;
    }

    std::int64_t value = 0;
    std::size_t int_digits = 0;
    for (; i < text.size() && detail::is_digit(text[i]); ++i, ++int_digits) {
        if (!detail::mul_add_checked(value, text[i] - '0')) {
            return {0, DecimalError::Overflow};
        }
    }
    if (int_digits == 0) { return {0, DecimalError::BadFormat}; }

    std::size_t frac_digits = 0;
    if (i < text.size() && text[i] == '.') {
        ++i;
        for (; i < text.size() && detail::is_digit(text[i]); ++i, ++frac_digits) {
            if (frac_digits == static_cast<std::size_t>(decimals)) {
                return {0, DecimalError::TooManyDecimals};
            }
            if (!detail::mul_add_checked(value, text[i] - '0')) {
                return {0, DecimalError::Overflow};
            }
        }
        if (frac_digits == 0) { return {0, DecimalError::BadFormat}; }  // "10."
    }
    if (i != text.size()) { return {0, DecimalError::BadFormat}; }  // trailing junk

    // Scale up whatever precision the wire did not spell out.
    for (std::size_t k = frac_digits; k < static_cast<std::size_t>(decimals); ++k) {
        if (!detail::mul_add_checked(value, 0)) { return {0, DecimalError::Overflow}; }
    }

    return {negative ? -value : value, DecimalError::None};
}

// Render a scaled integer back to a fixed-point decimal string — the display
// edge, and still integer-only so the ladder cannot disagree with the book.
// Writes at most `cap` chars (no NUL) and returns the length, or 0 if it does
// not fit. Always emits exactly `decimals` fractional digits ("10.0000"), which
// is what a price ladder wants: the column lines up.
constexpr std::size_t format_scaled(std::int64_t value, int decimals, char* out,
                                    std::size_t cap) noexcept {
    if (out == nullptr || decimals < 0 || decimals > kMaxDecimals) { return 0; }

    char buf[32] = {};
    std::size_t n = 0;
    const bool negative = value < 0;

    // Negate in unsigned space so INT64_MIN does not trap.
    std::uint64_t magnitude =
        negative ? (~static_cast<std::uint64_t>(value) + 1u) : static_cast<std::uint64_t>(value);

    // Digits emitted least-significant first into buf, reversed at the end.
    std::size_t emitted = 0;
    do {
        buf[n++] = static_cast<char>('0' + static_cast<int>(magnitude % 10u));
        magnitude /= 10u;
        ++emitted;
        if (emitted == static_cast<std::size_t>(decimals)) {
            if (decimals > 0) {
                // Digits go in least-significant first and are reversed at the
                // end, so the '.' is pushed before the leading zero that makes
                // an all-fraction value read "0.9999" rather than ".9999".
                buf[n++] = '.';
                if (magnitude == 0) { buf[n++] = '0'; }
            }
        }
    } while (magnitude != 0);

    // A value with fewer digits than `decimals` needs zero padding, then "0.".
    while (emitted < static_cast<std::size_t>(decimals)) {
        buf[n++] = '0';
        ++emitted;
        if (emitted == static_cast<std::size_t>(decimals)) {
            buf[n++] = '.';
            buf[n++] = '0';
        }
    }
    if (negative) { buf[n++] = '-'; }

    if (n > cap) { return 0; }
    for (std::size_t k = 0; k < n; ++k) { out[k] = buf[n - 1 - k]; }
    return n;
}

}  // namespace depthcharge
