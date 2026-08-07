// test_decimal.cpp — the integer-ticks foundation (invariant #3).
//
// Every price DepthCharge will ever draw passes through parse_scaled. If it
// rounds, the whole "exact integer equality, deterministic replay" claim goes
// with it — so this file is deliberately paranoid, and most of it runs at
// compile time.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include <depthcharge/decimal.hpp>

using depthcharge::DecimalError;
using depthcharge::format_scaled;
using depthcharge::parse_scaled;

namespace {

// Compile-time contract: these are the shapes Anvil actually puts on the wire
// (docs/vendor/anvil-protocol.md §1 — "shortest decimal, trailing zeros
// trimmed"), pinned where they cannot regress at runtime.
static_assert(parse_scaled("9.9972", 4).value == 99972);
static_assert(parse_scaled("10", 4).value == 100000);
static_assert(parse_scaled("10.0", 4).value == 100000);
static_assert(parse_scaled("0.0001", 4).value == 1);
static_assert(parse_scaled("0", 4).value == 0);
static_assert(parse_scaled("-1.5", 2).value == -150);
static_assert(parse_scaled("10.1302", 4).value == 101302);
static_assert(parse_scaled("42", 0).value == 42);
static_assert(parse_scaled("9.9972", 4).ok());

// The one that matters: more precision than the declared scale is an error,
// never a rounded value (ARCHITECTURE §4 "declare and verify, never guess and
// round").
static_assert(parse_scaled("10.01234", 4).error == DecimalError::TooManyDecimals);
static_assert(parse_scaled("10.00001", 4).error == DecimalError::TooManyDecimals);

std::string fmt(std::int64_t v, int decimals) {
    char buf[depthcharge::kMaxFormattedChars] = {};
    return std::string(buf, format_scaled(v, decimals, buf, sizeof buf));
}

}  // namespace

TEST_CASE("kMaxFormattedChars is the real bound, not a guessed round number") {
    // format_scaled writes into a buffer of exactly this size and returns 0 if
    // the result will not fit, so an under-sized constant would not corrupt
    // memory — it would silently render every price as an empty string. These
    // are the three shapes that actually reach the bound.
    const std::string widest_int = fmt(INT64_MIN, 0);
    const std::string widest_mixed = fmt(INT64_MIN, 18);      // 19 digits + '.' + '-'
    const std::string widest_fraction = fmt(-1, 18);          // "-0." + 18 digits

    CHECK(widest_int == "-9223372036854775808");
    CHECK(widest_mixed == "-9.223372036854775808");
    CHECK(widest_fraction == "-0.000000000000000001");

    for (const std::string& s : {widest_int, widest_mixed, widest_fraction}) {
        CHECK(s.size() <= depthcharge::kMaxFormattedChars);
        CHECK_FALSE(s.empty());   // 0-length means it did not fit
    }
    // ...and the constant is not wastefully large either: something must reach
    // within a character of it, or the derivation has drifted.
    CHECK(std::max({widest_int.size(), widest_mixed.size(), widest_fraction.size()}) + 1 >=
          depthcharge::kMaxFormattedChars);
}

TEST_CASE("parse_scaled: the shapes Anvil emits") {
    CHECK(parse_scaled("9.9972", 4).value == 99972);
    CHECK(parse_scaled("9.985", 4).value == 99850);
    CHECK(parse_scaled("10", 4).value == 100000);
    CHECK(parse_scaled("10.012", 4).value == 100120);
    CHECK(parse_scaled("007", 4).value == 70000);  // leading zeros are harmless
}

TEST_CASE("parse_scaled: refuses everything that is not a plain decimal") {
    CHECK(parse_scaled("", 4).error == DecimalError::Empty);
    CHECK(parse_scaled("abc", 4).error == DecimalError::BadFormat);
    CHECK(parse_scaled("10.", 4).error == DecimalError::BadFormat);
    CHECK(parse_scaled(".5", 4).error == DecimalError::BadFormat);
    CHECK(parse_scaled("-", 4).error == DecimalError::BadFormat);
    CHECK(parse_scaled("+10", 4).error == DecimalError::BadFormat);
    CHECK(parse_scaled(" 10", 4).error == DecimalError::BadFormat);
    CHECK(parse_scaled("10 ", 4).error == DecimalError::BadFormat);
    CHECK(parse_scaled("1e3", 4).error == DecimalError::BadFormat);   // no exponents
    CHECK(parse_scaled("10.5x", 4).error == DecimalError::BadFormat);
    CHECK(parse_scaled("1,000", 4).error == DecimalError::BadFormat);
    CHECK(parse_scaled("nan", 4).error == DecimalError::BadFormat);
    CHECK(parse_scaled("10", -1).error == DecimalError::BadScale);
    CHECK(parse_scaled("10", 19).error == DecimalError::BadScale);
}

TEST_CASE("parse_scaled: overflow is reported, not wrapped") {
    CHECK(parse_scaled("9223372036854775807", 0).value == INT64_MAX);
    CHECK(parse_scaled("9223372036854775808", 0).error == DecimalError::Overflow);
    CHECK(parse_scaled("99999999999999999999", 0).error == DecimalError::Overflow);
    // Scaling up is checked too: this fits as digits but not at 10^4.
    CHECK(parse_scaled("9223372036854775", 4).error == DecimalError::Overflow);
}

TEST_CASE("format_scaled: fixed-point, integer-only, column-aligned") {
    CHECK(fmt(99972, 4) == "9.9972");
    CHECK(fmt(100000, 4) == "10.0000");
    CHECK(fmt(101302, 4) == "10.1302");
    CHECK(fmt(1, 4) == "0.0001");
    CHECK(fmt(0, 4) == "0.0000");
    CHECK(fmt(-1, 4) == "-0.0001");
    CHECK(fmt(-99972, 4) == "-9.9972");
    CHECK(fmt(9999, 4) == "0.9999");
    CHECK(fmt(42, 0) == "42");
    CHECK(fmt(-42, 0) == "-42");
    CHECK(fmt(7, 2) == "0.07");
    CHECK(fmt(INT64_MIN, 0) == "-9223372036854775808");
}

TEST_CASE("format_scaled: reports rather than truncates when the buffer is short") {
    char small[3] = {};
    CHECK(format_scaled(100000, 4, small, sizeof small) == 0);
    CHECK(format_scaled(0, 4, nullptr, 8) == 0);
}

TEST_CASE("parse -> format -> parse is the identity on every wire shape seen") {
    // Sampled across both committed traces (min, max, and the awkward middles).
    const char* prices[] = {"9.8877", "9.9972", "9.985",  "10",     "10.0",
                            "10.012", "10.1302", "9.9903", "10.0035", "0.0001"};
    for (std::string_view p : prices) {
        const auto first = parse_scaled(p, 4);
        REQUIRE(first.ok());
        const std::string round_tripped = fmt(first.value, 4);
        const auto second = parse_scaled(round_tripped, 4);
        REQUIRE(second.ok());
        CHECK(second.value == first.value);
    }
}
