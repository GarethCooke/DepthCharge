// test_kraken_checksum.cpp — the CRC32 itself, before any adapter touches it.
//
// M4 stage B2. `test_kraken_adapter.cpp` proves the checksum agrees with the
// venue on 4,878 captured messages; this file proves the things those 4,878
// messages CANNOT prove, and they are three:
//
//   1. that the CRC is the standard one rather than one of the several nearby
//      variants that differ only in a reflection — agreement on captured data
//      would look identical for a wrong variant only if the wrong variant were
//      also what the venue used, so this is belt-and-braces, but it is the
//      cheap half of the pair and it localises a failure to the arithmetic;
//   2. **what the checksum does not cover.** Every committed slice agrees with
//      a top-10 rule AND with our implementation of one, which is precisely the
//      coincidence class ARCHITECTURE §9 (2026-08-18) says a corpus cannot
//      settle. The discriminating input is a book edited BELOW level 10, and no
//      capture contains one because the venue never sends a message whose only
//      effect is invisible to its own checksum. So it is synthesised here.
//   3. the tokenisation's edges — zero, a negative, and a buffer too small —
//      none of which a real book can produce, and all of which are reachable
//      from a wire change rather than from a bug.
#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <string>

#include <depthcharge/kraken/kraken_checksum.hpp>

using depthcharge::BookLevel;
using depthcharge::kraken::Crc32;
using depthcharge::kraken::book_checksum;
using depthcharge::kraken::checksum_token;
using depthcharge::kraken::kChecksumLevels;
using depthcharge::kraken::kMaxTokenChars;

namespace {

std::uint32_t crc_of(const std::string& s) {
    Crc32 c;
    c.update(s.data(), s.size());
    return c.value();
}

std::string token_of(std::int64_t v) {
    char buf[kMaxTokenChars] = {};
    return std::string(buf, checksum_token(v, buf, kMaxTokenChars));
}

// A ladder deep enough for the top-10 rule to have something to ignore. Prices
// walk away from the touch by one tick a level so every level is distinct, and
// quantities are distinct too — a payload built from repeated values could not
// tell "level 12 was skipped" from "level 12 contributed the same bytes as
// level 11".
void fill(BookLevel* side, std::uint32_t count, std::int64_t first, std::int64_t step) {
    for (std::uint32_t i = 0; i < count; ++i) {
        side[i].px = first + step * static_cast<std::int64_t>(i);
        side[i].qty = 1000 + static_cast<std::int64_t>(i);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. The arithmetic is CRC-32/ISO-HDLC, not a neighbour of it
// ---------------------------------------------------------------------------

TEST_CASE("the CRC is the standard reflected CRC-32, on its published check vector") {
    // The check value every CRC catalogue lists for CRC-32/ISO-HDLC — zlib,
    // PNG, Ethernet, and `zlib.crc32` in tools/kraken_frame_economics.py, which
    // is where every figure in NOTES-kraken.md came from. A variant with the
    // reflection or the final XOR wrong lands nowhere near this number.
    CHECK(crc_of("123456789") == 0xCBF43926u);

    // The empty payload — reachable, and not a curiosity: it is what an empty
    // book produces, and 0 rather than 0xFFFFFFFF is the difference between
    // remembering the final XOR and not.
    CHECK(crc_of("") == 0u);

    // Incremental and one-shot are the same number, which is what lets
    // book_checksum stream straight out of the ladder with no payload buffer.
    Crc32 split;
    split.update("1234", 4);
    split.update("56789", 5);
    CHECK(split.value() == 0xCBF43926u);

    // constexpr, so the compiler evaluates it too — the target build has no
    // test binary, and this is the only assertion that runs there.
    static_assert([] {
        Crc32 c;
        c.update("123456789", 9);
        return c.value();
    }() == 0xCBF43926u);
}

// ---------------------------------------------------------------------------
// 2. The tokenisation, including the values a book cannot hold
// ---------------------------------------------------------------------------

TEST_CASE("a checksum token is the plain decimal spelling of the scaled integer") {
    // The three from NOTES-kraken.md's worked example, at the two scales the
    // committed slices use. Each is quoted with the wire text it corresponds to,
    // because the claim under test is that the two routes AGREE — and the wire
    // text is the half that is not in the code.
    CHECK(token_of(5100) == "5100");        // wire "0.00005100", qty_decimals 8
    CHECK(token_of(627912) == "627912");    // wire "62791.2",    price_decimals 1
    CHECK(token_of(286) == "286");          // wire "0.0286",     price_decimals 4
    CHECK(token_of(138258808) == "138258808");  // wire "1.38258808"

    // No sign, no point, no padding: 19 digits is the longest possible token and
    // kMaxTokenChars is derived from exactly that.
    CHECK(token_of(INT64_MAX) == "9223372036854775807");
    CHECK(token_of(INT64_MAX).size() == kMaxTokenChars);

    SUBCASE("zero spells 0, not the empty string") {
        // UNREACHABLE FROM A REAL BOOK — `seed` refuses qty <= 0 and
        // `apply_level` erases on qty == 0, so no held level can be zero. Pinned
        // anyway because the two available answers are not equally safe: the
        // text route's `"000".lstrip("0")` is the empty string, which would
        // SHORTEN the payload silently, while "0" changes it visibly. The
        // divergence is deliberate and this is where it is recorded.
        CHECK(token_of(0) == "0");
    }

    SUBCASE("a negative writes nothing, so the comparison fails rather than lies") {
        char buf[kMaxTokenChars] = {};
        CHECK(checksum_token(-1, buf, kMaxTokenChars) == 0);
        CHECK(checksum_token(INT64_MIN, buf, kMaxTokenChars) == 0);
    }

    SUBCASE("every power-of-ten boundary, which is where a digit loop goes wrong") {
        // A PROPERTY RATHER THAN A LIST, and it covers the magnitudes the corpus
        // cannot. The 8,172 real level tokens in `test_kraken_adapter.cpp` are
        // the strongest evidence available that the integer route matches the
        // wire — and they are all BTC/USD and MINA/GBP prices and sizes, so they
        // cluster in a handful of decades and never once land on 9 -> 10 or
        // 99 -> 100, which is exactly where a length calculation or a reversal
        // is off by one.
        //
        // The property: the token is the decimal spelling, so its length is the
        // digit count, its first character is never '0' unless the value is
        // zero, and reading it back gives the value.
        std::int64_t v = 1;
        for (int decade = 0; decade < 19; ++decade) {
            for (std::int64_t probe : {v - 1, v, v + 1}) {
                if (probe < 0) { continue; }
                CAPTURE(probe);
                const std::string tok = token_of(probe);
                REQUIRE_FALSE(tok.empty());
                CHECK(tok == std::to_string(probe));
                // No leading zero — except for zero itself, which is the one
                // value whose token legitimately starts with one, and which the
                // decade walk reaches at v-1 on its first pass.
                if (probe == 0) {
                    CHECK(tok == "0");
                } else {
                    CHECK(tok[0] != '0');
                }
            }
            if (v > INT64_MAX / 10) { break; }
            v *= 10;
        }
    }

    SUBCASE("a buffer too small writes nothing rather than a truncated number") {
        char buf[4] = {};
        CHECK(checksum_token(627912, buf, 4) == 0);
        // The boundary, both sides of it.
        char exact[6] = {};
        CHECK(checksum_token(627912, exact, 6) == 6);
        CHECK(checksum_token(6279123, exact, 6) == 0);
    }
}

// ---------------------------------------------------------------------------
// 3. The payload: asks ascending, then bids descending
// ---------------------------------------------------------------------------

TEST_CASE("the book checksum is asks-then-bids over concatenated tokens") {
    // Two levels a side, taken from the real touch of the depth-25 slice so the
    // numbers are recognisable beside test_kraken_adapter.cpp's ladder
    // assertions. The expected value is NOT this code's output: it is
    //     zlib.crc32(b"627913167012176279425627912138258808627910" + b"7")
    // computed independently, which is the same route
    // tools/kraken_frame_economics.py takes to every figure in NOTES-kraken.md.
    BookLevel asks[2] = {{627913, 16701217}, {627942, 5}};
    BookLevel bids[2] = {{627912, 138258808}, {627910, 7}};
    CHECK(book_checksum(asks, 2, bids, 2) == 3719352410u);

    // And the same tokens by hand, which is what makes the line above readable
    // as a rule rather than as a magic number.
    CHECK(crc_of("627913" "16701217" "627942" "5"
                 "627912" "138258808" "627910" "7") == 3719352410u);

    SUBCASE("the two sides are not interchangeable") {
        // Bids-then-asks is the single most likely way to get this wrong, and it
        // would still agree with itself on every self-derived golden.
        CHECK(book_checksum(bids, 2, asks, 2) != 3719352410u);
    }

    SUBCASE("an empty book checksums, and to the empty payload") {
        CHECK(book_checksum(nullptr, 0, nullptr, 0) == 0u);
    }

    SUBCASE("a side shallower than ten contributes what it has") {
        // Not a special case in the code and deliberately not one here either —
        // the assertion is that nothing pads. A padded short side would agree
        // with the venue never, and with itself always.
        CHECK(book_checksum(asks, 1, bids, 0) == crc_of("62791316701217"));
    }
}

// ---------------------------------------------------------------------------
// 4. THE COVERAGE, AND IT IS THE POINT OF THIS FILE
// ---------------------------------------------------------------------------

TEST_CASE("the checksum covers the top ten levels a side and no more") {
    // ========================================================================
    // WHY THIS CANNOT BE A GOLDEN, AND WHY IT IS MANDATORY.
    // ========================================================================
    //
    // Stage 0 confirmed "top 10 regardless of subscribed depth" by reproducing
    // 8,677 captured checksums with a top-10 rule at depths 10, 25 and 100.
    // That is strong evidence and it is evidence of the wrong shape for the
    // question B2 has to answer, which is not *is the rule top-10* but *what
    // does a top-10 rule fail to see*. Every captured message agrees with both
    // readings, because the venue never sends a message whose only effect is
    // below its own checksum window — it has no reason to.
    //
    // So the discriminating input is synthesised: one book, edited twice, once
    // inside the window and once outside it. This is ARCHITECTURE §9's
    // 2026-08-18 rule applied to a constant the adapter shares with the venue,
    // and the constant here is `kChecksumLevels`.
    BookLevel asks[25]{};
    BookLevel bids[25]{};
    fill(asks, 25, 627913, +1);
    fill(bids, 25, 627912, -1);

    const std::uint32_t full = book_checksum(asks, 25, bids, 25);

    SUBCASE("a level deeper than ten is invisible to it") {
        // Level 11 (index 10) is the first one outside the window, and level 25
        // is the last one the ladder holds. Both are drawn on the panel; neither
        // reaches the checksum.
        asks[10].qty = 999999999;
        CHECK(book_checksum(asks, 25, bids, 25) == full);

        bids[24].px = 1;              // an absurd price, and still invisible
        bids[24].qty = 777777777;
        CHECK(book_checksum(asks, 25, bids, 25) == full);

        // Truncating the whole tail away changes nothing either, which is the
        // same statement from the other end: a book of 10 and a book of 25 that
        // agree on their first 10 are indistinguishable to this check.
        CHECK(book_checksum(asks, kChecksumLevels, bids, kChecksumLevels) == full);
    }

    SUBCASE("level ten itself is inside, so the boundary is not off by one") {
        // The paired assertion, and the one that stops the case above from
        // passing on a checksum that covers nothing at all.
        asks[9].qty += 1;
        CHECK(book_checksum(asks, 25, bids, 25) != full);
    }

    SUBCASE("the first level is inside, which a vacuous checksum would also fail") {
        BookLevel one_ask[1] = {{627913, 5}};
        BookLevel one_bid[1] = {{627912, 5}};
        const std::uint32_t before = book_checksum(one_ask, 1, one_bid, 1);
        one_bid[0].qty = 6;
        CHECK(book_checksum(one_ask, 1, one_bid, 1) != before);
    }
}

// ---------------------------------------------------------------------------
// 5. The cost, measured rather than asserted
// ---------------------------------------------------------------------------

TEST_CASE("the checksum payload is small enough that a bitwise CRC is free") {
    // kraken_checksum.hpp chooses a bitwise CRC over a 1 KiB table and justifies
    // it with an arithmetic argument. The DETERMINISTIC half of that argument —
    // how many bytes one checksum actually feeds — is pinned here, because it is
    // the input to the estimate and it is a property of the venue's book shape
    // rather than of this desk.
    //
    // The throughput figure is REPORTED, not asserted: a wall-clock bound in a
    // unit test is a flake on a loaded box, and the number that matters is the
    // ESP32-S3's, which this machine cannot measure. It is recorded in the
    // session log beside the arithmetic it feeds.
    BookLevel asks[25]{};
    BookLevel bids[25]{};
    fill(asks, 25, 627913, +1);       // BTC/USD-shaped: 6-digit price tokens
    fill(bids, 25, 627912, -1);

    std::size_t payload = 0;
    for (std::uint32_t i = 0; i < kChecksumLevels; ++i) {
        payload += token_of(asks[i].px).size() + token_of(asks[i].qty).size();
        payload += token_of(bids[i].px).size() + token_of(bids[i].qty).size();
    }
    // 20 levels x (6-digit price + 4-digit qty). A real BTC/USD book runs longer
    // quantity tokens than this fixture's; the order of magnitude is the claim.
    CHECK(payload == 200);

    constexpr int kReps = 20000;
    const auto t0 = std::chrono::steady_clock::now();
    std::uint32_t sink = 0;
    for (int i = 0; i < kReps; ++i) {
        bids[0].qty = 1000 + i;       // defeat any hoisting of the whole call
        sink ^= book_checksum(asks, 25, bids, 25);
    }
    const auto t1 = std::chrono::steady_clock::now();
    CHECK(sink != 0xDEADBEEFu);       // the result is used, so nothing elides

    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    MESSAGE("book_checksum: " << (us / kReps) << " us/message on this host, "
            << payload << " bytes fed. Kraken's depth-25 BTC/USD rate is "
            << "26 messages/s.");
}
