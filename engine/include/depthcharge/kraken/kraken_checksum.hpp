// depthcharge/kraken/kraken_checksum.hpp — Kraken v2's CRC32, over integers.
//
// M4 stage B2. B1 parsed the venue's checksum and carried it; this is the other
// half of the comparison — OUR number, computed from the adapter's truncated
// ladder, so that `ours == theirs` is a complete test of whether the book we
// hold is the book the venue holds.
//
// ============================================================================
// WHY THIS IS A CHECK AND NOT A PARSER
// ============================================================================
//
// Kraken computes the CRC32 over the DECIMAL TEXT of each number as sent, point
// removed and leading zeros stripped. Read literally that says the adapter must
// retain the wire spelling of every level — a per-level text store on the exact
// path invariant #7 governs.
//
// It does not, and B1 measured why (ARCHITECTURE §9, 2026-08-18). Every price
// and quantity on this venue's book channel carries EXACTLY its declared
// precision — 8,172 level entries across five slices, zero exponent notation —
// and at exactly that precision the checksum token IS the decimal spelling of
// the scaled integer:
//
//     wire "0.00005100", qty_decimals 8  ->  5100 steps
//     text route:    "0.00005100" -> "000005100" -> "5100"
//     integer route: 5100                        -> "5100"      identical
//
// So `checksum_token` below takes an integer and never sees the wire. Verified
// end to end before either was written: 4,878 / 4,878 checksums reproduced from
// integers alone on the four slices that open with a snapshot.
//
// **AND THAT IS AN ASSUMPTION WITH A DETECTOR RATHER THAN A BARE ASSUMPTION.**
// The equivalence holds only while the venue spells every number at its declared
// precision, which is an observation over two hours of one day and not a
// contract statement. A venue that started sending `0.5` where it now sends
// `0.50000000` would tokenise to "5" on the wire and "50000000" here, and the
// PARSE would still succeed — `parse_scaled("0.5", 8)` is a perfectly good
// 50,000,000. The first thing that would notice is this file, one message later,
// as a mismatch. So a CRC failure at this venue means the paragraph above before
// it means a lost message, and NOTES-kraken.md says so beside the figure.
//
// ============================================================================
// WHAT THE CHECKSUM COVERS, AND THEREFORE WHAT IT CANNOT SEE
// ============================================================================
//
// **The top `kChecksumLevels` = 10 levels a side, regardless of the subscribed
// depth.** Confirmed at stage 0 against 8,677 checksums at depths 10, 25 and
// 100, and re-confirmed at B2 by the only test that can distinguish the
// hypotheses — a book edited BELOW level 10, whose checksum does not move.
//
// At the shipped depth of 25 that is 20 of the 50 levels the ladder holds, so
// the panel's 27 rows a side draw 10 validated levels, 15 that are rendered and
// never reached by this check, and 2 that are empty by construction (depth 25
// against `kDisplayLevels` 27). Those 15 are not unvalidated in the sense of
// *suspect*: a delta stream that checksums clean at the top has
// provably lost no message, so an error at level 20 would have to be an error in
// THIS code rather than a wire loss. What the CRC cannot detect is a defect
// whose effect is confined below level 10 — and stage 0 measured one that is
// exactly that shape, the non-truncating book, which is 100% correct on a
// top-10 CRC at depth 100 over 90 seconds and 27% correct at depth 10.
//
// This is a limitation and not a defect, and it is written down here, in
// NOTES-kraken.md and in the adapter, because shipping it unstated would be the
// defect. It is also the reason `kKrakenSubscribeDepth` is 25 rather than 100
// (kraken_adapter.hpp) and the reason the 500/1000 tiers carry a static_assert.
//
// ============================================================================
// THE ALGORITHM, SPELLED OUT BECAUSE A CRC HAS MANY NEARBY WRONG ANSWERS
// ============================================================================
//
// CRC-32/ISO-HDLC — the zlib/PNG/Ethernet one: reflected, polynomial 0xEDB88320
// (the bit-reversed 0x04C11DB7), initial value 0xFFFFFFFF, final XOR
// 0xFFFFFFFF. That is `zlib.crc32` exactly, which is what Kraken's own
// documentation and `tools/kraken_frame_economics.py` both use, and the four
// committed slices are 4,878 independent confirmations that this spelling is
// the right one out of the several that differ only in a reflection.
//
// Bitwise rather than table-driven, and the arithmetic rather than the taste.
// The payload is MEASURED — 200 bytes for a depth-25 BTC/USD-shaped book, pinned
// in test_kraken_checksum.cpp — and the venue's message rate on that slice is
// 26/s, so this feeds ~5 KB/s through 8 iterations a byte. The host runs one
// checksum in 1.9 us; the ESP32-S3 is the number that matters and this desk
// cannot measure it, so what is claimed here is only the input to the estimate
// and not the estimate. A 1 KiB lookup table would buy back a fraction of a
// fraction and cost a table nobody can eyeball.
#pragma once

#include <cstddef>
#include <cstdint>

#include "depthcharge/feed_event.hpp"

namespace depthcharge::kraken {

// The venue's rule: the top ten levels a side enter the checksum, whatever the
// subscription's depth. A constant rather than a parameter because it is the
// venue's number and not ours — the subscribed depth is the one that varies.
inline constexpr std::uint32_t kChecksumLevels = 10;

// Longest token `checksum_token` can produce: the 19 digits of INT64_MAX. No
// sign and no decimal point can appear, so that is the whole derivation.
inline constexpr std::size_t kMaxTokenChars = 19;

// Incremental CRC-32/ISO-HDLC. Incremental so the checksum can be computed
// straight out of the ladder with no assembled payload buffer anywhere —
// invariant #7 satisfied structurally rather than by budgeting for the ~800
// bytes a top-10 payload would otherwise need.
class Crc32 {
public:
    constexpr void update(char c) noexcept {
        state_ ^= static_cast<std::uint8_t>(c);
        for (int bit = 0; bit < 8; ++bit) {
            // `(~(state_ & 1u) + 1u)` is 0xFFFFFFFF when the low bit is set and
            // 0 when it is not — the polynomial applied on a 1 bit and nothing
            // applied on a 0, without a branch in the inner loop.
            state_ = (state_ >> 1) ^ (0xEDB88320u & (~(state_ & 1u) + 1u));
        }
    }

    constexpr void update(const char* data, std::size_t size) noexcept {
        for (std::size_t i = 0; i < size; ++i) { update(data[i]); }
    }

    constexpr std::uint32_t value() const noexcept { return state_ ^ 0xFFFFFFFFu; }

private:
    std::uint32_t state_ = 0xFFFFFFFFu;
};

// Kraken's checksum spelling of one number, from the scaled integer.
//
// Writes at most `cap` chars (no NUL) and returns the length, or 0 if it does
// not fit. The digits are the plain decimal spelling of `value` — which is what
// "point removed, leading zeros stripped" produces at exactly the declared
// precision, and the reason this function needs no `decimals` parameter at all.
//
// AND IT IS NOT `format_scaled(value, 0, ...)`, THOUGH IT WOULD PRODUCE THE SAME
// DIGITS TODAY. `decimal.hpp`'s formatter is the DISPLAY edge; this is a WIRE
// contract. The two agree by coincidence at zero decimals, and the coincidence is
// not a thing either side has promised to keep: a future change to `format_scaled`
// for a ladder-column reason — padding, a separator, a rounding rule — would
// silently move every checksum this venue computes. Sharing them would put a
// panel-formatting decision on the path that decides whether the book is
// trustworthy, which is the same shared-parent hazard `ladder.hpp` was extracted
// to avoid, pointing the other way. Twelve lines of digit loop is the cheaper
// half of that trade, and the 4,878 committed checksums are what would catch a
// drift in either.
//
// NEGATIVES AND ZERO, both unreachable and both defined rather than left to the
// reader. A held level always has px > 0 and qty > 0 — `seed` refuses qty <= 0
// and `apply_level` erases on qty == 0 — so neither case can enter the payload
// from a real book. If one somehow did: a negative writes nothing, so the CRC
// will not match and the mismatch path fires, which is the safe direction; and
// zero writes "0" rather than the empty string the text route would produce,
// because a token that vanishes from the payload is the worse failure of the
// two — it would silently shorten the message rather than change it.
constexpr std::size_t checksum_token(std::int64_t value, char* out,
                                     std::size_t cap) noexcept {
    if (out == nullptr || value < 0) { return 0; }

    char buf[kMaxTokenChars] = {};
    std::size_t n = 0;
    std::uint64_t magnitude = static_cast<std::uint64_t>(value);
    do {
        buf[n++] = static_cast<char>('0' + static_cast<int>(magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0);

    if (n > cap) { return 0; }
    for (std::size_t k = 0; k < n; ++k) { out[k] = buf[n - 1 - k]; }
    return n;
}

// The book's checksum: the top ten asks ASCENDING, then the top ten bids
// DESCENDING, each level contributing its price token followed by its quantity
// token, all concatenated with no separator.
//
// Both arrays are expected BEST-FIRST — asks ascending, bids descending — which
// is `depthcharge::ladder`'s one definition of sorted (ladder.hpp) and the order
// feed_event.hpp already requires of a Snapshot's spans. So the venue's stated
// order and this project's stored order are the same order, and this function
// walks each array forwards. That coincidence is worth naming rather than
// relying on: if the two ever disagreed, this function would need a reversal,
// and the absence of one here would be silent.
//
// A side holding fewer than ten levels contributes what it has, which is what
// the venue does with a book that shallow.
constexpr std::uint32_t book_checksum(const BookLevel* asks, std::uint32_t ask_count,
                                      const BookLevel* bids,
                                      std::uint32_t bid_count) noexcept {
    Crc32 crc;
    char token[kMaxTokenChars] = {};

    const auto feed = [&crc, &token](const BookLevel* side, std::uint32_t count) {
        const std::uint32_t n = count < kChecksumLevels ? count : kChecksumLevels;
        for (std::uint32_t i = 0; i < n; ++i) {
            crc.update(token, checksum_token(side[i].px, token, kMaxTokenChars));
            crc.update(token, checksum_token(side[i].qty, token, kMaxTokenChars));
        }
    };

    feed(asks, ask_count);
    feed(bids, bid_count);
    return crc.value();
}

}  // namespace depthcharge::kraken
