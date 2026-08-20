// firmware/src/reject_log.hpp — what the frames the parser threw away actually were.
//
// WHY A COUNTER WAS NOT ENOUGH.
//
// The 2026-08-10 bench run came back with `parse_errors` climbing to ~1,281 in
// the first ~60 s of every connect and then going flat, with `price=0 ticker=0
// unknown=0 trunc=0` beside it. That is a great deal of information about HOW
// MANY and none whatever about WHAT, and the three questions it leaves open have
// three different owners:
//
//   * a frame shape this client does not know     -> the venue's, and the fix is
//     to skip-not-error it (or not subscribe to it);
//   * bytes that are not a JSON document at all   -> ours, and the fix is in the
//     reassembler or the WebSocket client under burst load;
//   * a known type with a payload we reject       -> a wire change, and the fix
//     is a re-vendor of the protocol.
//
// A count cannot separate them and neither can any finer count. The payload can,
// so this captures the first `kRejectsPerConnect` of them per connect, with
// enough of each to name it, and the console prints them.
//
// WHAT IS IN A RECORD, AND WHY EACH FIELD IS THERE. Every one of these earns its
// place by discriminating between the candidates above; nothing is here because
// it was cheap.
//
//   status            NotJson / MissingType / BadShape is most of the diagnosis
//                     on its own, and it is thrown away by `parse_errors`.
//   len               THE WHOLE payload length, not what was captured. A length
//                     at an exact multiple of the WebSocket client's RX buffer
//                     (`kWsRxBufferBytes`, 4096) names a chunk-boundary
//                     truncation and indicts the reassembler; a plausible
//                     5-9 KB length says the bytes were all there.
//   head              the first bytes, which carry `{"type":"..."` and therefore
//                     the frame kind — the thing the bench run could not say.
//   tail              the last bytes. A payload that ends `}` is whole and was
//                     rejected on its contents; one that ends mid-token was
//                     cut off. Head alone cannot tell those apart and would be
//                     read as "it looks like a book frame, why did it reject?".
//   second_header_at  the offset of a SECOND `{"type":` inside one payload. Two
//                     frames spliced into one buffer look valid at both ends and
//                     are the single most misreadable failure here, so it is
//                     detected rather than left to be spotted by eye.
//   since_connect_ms  the burst is a connect-time phenomenon; a record with no
//                     clock against the connect cannot confirm that.
//
// NO ESP-IDF, DELIBERATELY — the same rule as frame_reassembler.hpp,
// gap_histogram.hpp and stall_probe.hpp, and for the same reason. This is
// bookkeeping and string surgery over a hostile buffer, its failure mode is a
// log line that misleads rather than a crash, and both halves of that are
// testable on the desk. harness/tests/test_reject_log.cpp is the proof.
//
// SINGLE WRITER. Every mutating call is made from the Core 0 feed task, which is
// the same task that owns the book (invariant #8); the console only reads. Same
// trade as every other counter in this firmware — a stale read costs one log
// line, and nothing may branch on any of it.
//
// NOT IN THE STEADY-STATE PATH'S WAY. `note()` runs only when a frame was
// rejected, and the expensive part of it (the second-header scan, which is
// linear in the payload) runs only then too. On a healthy run it is never
// called at all; on the run this exists for it costs a ~16 KB scan at the reject
// rate, which is well under a millisecond of Core 0 per second. Storage is
// fixed and placed at construction, so invariant #7 is untouched.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "gap_histogram.hpp"  // append_truncating
#include "venue_build.hpp"     // the selected venue's ParseStatus and its names

namespace depthcharge::fw {

// How many rejected payloads to capture per connect.
//
// Ten, because the question is "what IS this frame" and not "how many are
// there" — the tally answers the second. Ten is also enough to see whether a
// connect burst is one repeated shape or a mixture, which is the first thing to
// know and the thing a single sample cannot say.
inline constexpr std::uint32_t kRejectsPerConnect = 10;

// Ring slots. Deliberately two more than the per-connect budget so that all ten
// of a connect are still retained after the reader's one-slot safety margin
// (see oldest_retained()); at ten a burst could evict its own first record
// before the console printed it.
inline constexpr std::size_t kRejectLogDepth = 12;

// That relationship is the whole reason the two constants differ, and it is the
// kind of thing a later "the ring is oversized, trim it" would quietly break —
// the symptom being a bench log one payload short of a connect's burst, with
// nothing to say so except a count that does not add up.
static_assert(kRejectLogDepth - 1 >= kRejectsPerConnect,
              "the retained window must cover a whole connect's capture budget");

// How much of a payload is kept, in CHARACTERS — the storage below is one byte
// larger each for the NUL. Stating it this way is not pedantry: the first draft
// made these the buffer sizes, so the tail held 47 characters and stopped one
// byte short of the payload's end, which threw away the closing `}` that is the
// entire point of capturing a tail. The host test caught it.
//
// 96 head characters clears `{"type":"snapshot","seq":361976,"ticker":101,
// "bids":[{"price":"10.0352","qty":24` — the type, the seq, the ticker and the
// start of the payload — and 48 tail characters is comfortably more than the
// longest token a truncation can land inside. Together they are 144 bytes a
// record against a payload that can be 16 KiB, which is the whole reason this is
// a head/tail pair and not a prefix.
inline constexpr std::size_t kRejectHeadBytes = 96;
inline constexpr std::size_t kRejectTailBytes = 48;

// What render() needs to print a record whole, derived rather than counted by
// hand at the call site. The head and tail dominate it and are right here, so a
// caller's hardcoded buffer would silently start truncating the moment either
// constant moved — and the failure mode of that is a log line one field short in
// the one place a bench session is reading for an answer. 96 covers the fixed
// part: the ordinal, connect, in-connect, elapsed and length fields with their
// labels, the longest status name, the optional `SPLIT@nnnnn`, and the
// `head[]`/`tail[]` brackets.
inline constexpr std::size_t kRejectLineChars = kRejectHeadBytes + kRejectTailBytes + 96;

// THE STATUS TYPE IS THE SELECTED VENUE'S, since M4 stage D item A3. This log
// captures what the parser threw away, and which parser that is, is a build-time
// fact — so the enum, its names and the width of the tally all come from
// `venue_build.hpp` rather than being spelled for Anvil here. The two venues'
// enums happen to have six values each, which is exactly the sort of agreement
// this project has learned to distrust: the count is asserted per venue at its
// own declaration, so a status added to either engine enum fails to compile
// instead of writing past the array on the error path, which is where it would
// be found last.
//
// The first non-Ok value is likewise the venue's, and it is spelled `Ok + 1`
// rather than by name because `NotJson` is Anvil's name for it and there is no
// promise the next venue uses that word. Both current enums pin `Ok = 0` and
// list their failures after it.
using ParseStatus = venue::ParseStatus;
inline constexpr std::size_t kParseStatusCount = venue::kParseStatusCount;
inline constexpr std::size_t kFirstRejectStatus = static_cast<std::size_t>(ParseStatus::Ok) + 1;
static_assert(static_cast<std::size_t>(ParseStatus::Ok) == 0,
              "the tally indexes from Ok = 0 and prints from the value after it");

using venue::parse_status_name;

// Copy `n` bytes of a network buffer into a fixed, NUL-terminated, printable
// destination. Returns how many bytes were taken.
//
// EVERY BYTE IS UNTRUSTED. The payload is whatever came off the socket, so it
// may hold NULs (the parser's own spec says a frame "may contain embedded NUL
// bytes"), newlines that would split one log line into several and make the
// record unreadable in a bench capture, and high bytes that a terminal will
// interpret. Anything outside printable ASCII becomes '.', which keeps the
// LENGTH honest — a run of dots is visibly a run of dots and not a shorter
// string — and that matters, because the shape of the garbage is part of the
// evidence.
inline std::uint8_t copy_printable(char* dst, std::size_t cap, const char* src,
                                   std::size_t n) noexcept {
    if (dst == nullptr || cap == 0) { return 0; }
    dst[0] = '\0';
    if (src == nullptr) { return 0; }
    const std::size_t take = (n < cap - 1) ? n : cap - 1;
    for (std::size_t i = 0; i < take; ++i) {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        dst[i] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.';
    }
    dst[take] = '\0';
    return static_cast<std::uint8_t>(take);
}

// Offset of a SECOND Anvil frame header inside one payload, or 0 if there is
// only the one.
//
// Every frame Anvil has ever sent starts `{"type":` — 2,694 captured frames plus
// both committed traces, no exception — and no Anvil frame contains that
// sequence anywhere but at byte 0, because no nested object in this protocol has
// a `type` member. So a hit at any offset above 0 means two messages ended up in
// one buffer, which is a transport defect and NOT a venue one, and it is the
// failure that most needs naming: such a payload begins like a valid frame and
// ends like a valid frame, so head and tail both look right and the reject reads
// as inexplicable.
//
// THE PREFIX IS THE VENUE'S, AND IT WAS ANVIL'S ONLY UNTIL M4 STAGE D FOUND IT.
// A3 put this file on the Kraken build; review then pointed out that no Kraken
// v2 frame starts `{"type":` at all — they start `{"channel":` or `{"method":` —
// so this scan returned 0 for every payload while the renderer went on printing
// as though it had looked. `SPLIT@nnnnn` is the field that diagnosed the entire
// 2026-08-13 corruption; an instrument that silently cannot see is worse than
// one that is absent, which is the rule this project keeps re-learning.
//
// Two prefixes because Kraken has two frame openings and the splice that matters
// can be either. At Anvil both spell the same thing, so the scan degenerates to
// what it always was.
//
// The search starts at 1 rather than 0 so the payload's own header is never the
// answer.
inline std::uint32_t find_second_frame_header(const char* payload, std::uint32_t len) noexcept {
    const std::string_view a = venue::kFrameHeadPrefix;
    const std::string_view b = venue::kFrameHeadPrefixAlt;
    if (payload == nullptr || a.empty()) { return 0; }
    for (std::uint32_t i = 1; i < len; ++i) {
        if (payload[i] != '{') { continue; }
        const std::size_t left = static_cast<std::size_t>(len - i);
        if (left >= a.size() && std::memcmp(payload + i, a.data(), a.size()) == 0) { return i; }
        if (left >= b.size() && std::memcmp(payload + i, b.data(), b.size()) == 0) { return i; }
    }
    return 0;
}

// One rejected payload, with everything needed to name it without the board.
struct RejectRecord {
    std::uint32_t ordinal = 0;           // 1-based across the whole run
    std::uint32_t connect = 0;           // which connect it belongs to, 1-based
    std::uint32_t in_connect = 0;        // 1-based within that connect
    std::uint32_t since_connect_ms = 0;  // 0 if no connect has been seen yet
    std::uint32_t len = 0;               // the WHOLE payload, not what was kept
    std::uint32_t second_header_at = 0;  // 0 when the payload holds one frame

    ParseStatus status = ParseStatus::Ok;
    std::uint8_t head_n = 0;
    std::uint8_t tail_n = 0;

    char head[kRejectHeadBytes + 1]{};  // +1 for the NUL, so head_n can reach the full count
    char tail[kRejectTailBytes + 1]{};
};

class RejectLog {
public:
    // A socket came up. Resets the per-connect capture budget so every connect
    // gets its own first ten — the burst repeats per connect, and a budget spent
    // once at boot would make every reconnect invisible, which is precisely the
    // half of the phenomenon that is hardest to reproduce deliberately.
    //
    // It does NOT reset the tallies or the record ordinals: those are the run's,
    // and a bench log that renumbered from 1 on every reconnect would be
    // unreadable across an outage.
    void note_connect(std::int64_t at_us) noexcept {
        ++connects_;
        connected_at_us_ = at_us;
        in_connect_ = 0;
    }

    // One frame was rejected. `payload` is the verbatim bytes the parser saw and
    // is borrowed for the duration of this call only — the caller still owns the
    // slot, which is why this must run before the slot is recycled.
    void note(ParseStatus status, const char* payload, std::uint32_t len,
              std::int64_t at_us) noexcept {
        ++total_;
        const std::size_t slot = static_cast<std::size_t>(status);
        if (slot < kParseStatusCount) { ++by_status_[slot]; }

        ++in_connect_;
        if (in_connect_ > kRejectsPerConnect) {
            ++suppressed_;
            return;
        }

        RejectRecord& r = ring_[captured_ % kRejectLogDepth];
        r = RejectRecord{};
        r.ordinal = captured_ + 1;
        r.connect = connects_;
        r.in_connect = in_connect_;
        // Saturating, through the same shared clamp the stall probe's high-water
        // marks use. 32 bits of microseconds is 71 minutes, and a desk object is
        // meant to run for weeks: without this a reject two hours into a connect
        // would report a few milliseconds, which is the one direction an
        // instrument about a *connect-time* burst must never be wrong in.
        r.since_connect_ms =
            (connected_at_us_ != 0 && at_us > connected_at_us_)
                ? clamp_us_to_u32(static_cast<std::uint64_t>(at_us - connected_at_us_)) / kUsPerMs
                : 0u;
        r.len = len;
        r.status = status;
        r.second_header_at = find_second_frame_header(payload, len);

        r.head_n = copy_printable(r.head, sizeof r.head, payload, len);
        // The tail is the bytes the head did not already show, taken from the
        // END of the payload — it must include the very last byte or it cannot
        // answer the question it is here for, which is whether the message ends
        // with a closing brace or in the middle of a token. A short payload
        // reports no tail at all rather than printing its middle twice, which
        // would read as a longer payload than it is.
        //
        // `payload != nullptr` is tested HERE and not left to copy_printable,
        // which does check it: by the time the pointer is handed over it has
        // already been advanced by `len - want`, so it is no longer null and the
        // check inside would pass on an address computed from nothing. The head
        // above and the header search are safe because neither offsets the
        // pointer before testing it. A null with a non-zero length cannot come
        // out of the real pipeline — the reassembler never publishes one — but
        // this reads a network buffer, and the failure is a fault rather than a
        // wrong log line.
        if (payload != nullptr && len > r.head_n) {
            const std::uint32_t remaining = len - r.head_n;
            const std::uint32_t want =
                (remaining < kRejectTailBytes) ? remaining
                                               : static_cast<std::uint32_t>(kRejectTailBytes);
            r.tail_n = copy_printable(r.tail, sizeof r.tail, payload + (len - want), want);
        }

        ++captured_;
    }

    // --- what the console reads ---------------------------------------------

    std::uint32_t total() const noexcept { return total_; }
    std::uint32_t count(ParseStatus st) const noexcept {
        const std::size_t slot = static_cast<std::size_t>(st);
        return (slot < kParseStatusCount) ? by_status_[slot] : 0u;
    }
    std::uint32_t captured() const noexcept { return captured_; }
    std::uint32_t suppressed() const noexcept { return suppressed_; }
    std::uint32_t connects() const noexcept { return connects_; }

    // kRejectLogDepth slots, kRejectLogDepth - 1 retained. The spare slot is the
    // reader's margin: the console runs on the other core with no
    // synchronisation, so the slot the writer is about to reuse must not also be
    // the one it is being invited to read. This is the same rule the stall
    // probe's hole ring learned the hard way, applied here before it could be
    // learned twice.
    std::uint32_t oldest_retained() const noexcept {
        constexpr std::uint32_t kRetained = static_cast<std::uint32_t>(kRejectLogDepth) - 1u;
        return (captured_ > kRetained) ? (captured_ - kRetained) : 0u;
    }

    // `index` is a 0-based position in the captured sequence; null once evicted.
    const RejectRecord* at(std::uint32_t index) const noexcept {
        if (index >= captured_ || index < oldest_retained()) { return nullptr; }
        return &ring_[index % kRejectLogDepth];
    }

    // "n=1281 not-json=1281 no-type=0 bad-shape=0 bad-price=0 other-ticker=0 |
    //  logged=10 suppressed=1271 over 1 connects"
    std::size_t render_tally(char* out, std::size_t cap) const noexcept {
        if (out == nullptr || cap == 0) { return 0; }
        out[0] = '\0';
        std::size_t at = 0;
        int n = std::snprintf(out, cap, "n=%u", static_cast<unsigned>(total_));
        if (append_truncating(n, cap, at)) { return at; }
        // From NotJson, not from Ok: an Ok frame is not a reject and a column of
        // permanent zeroes in the one line a bench session greps is noise.
        for (std::size_t i = kFirstRejectStatus; i < kParseStatusCount; ++i) {
            n = std::snprintf(out + at, cap - at, " %s=%u",
                              parse_status_name(static_cast<ParseStatus>(i)),
                              static_cast<unsigned>(by_status_[i]));
            if (append_truncating(n, cap, at)) { return at; }
        }
        n = std::snprintf(out + at, cap - at, " | logged=%u suppressed=%u over %u connects",
                          static_cast<unsigned>(captured_),
                          static_cast<unsigned>(suppressed_),
                          static_cast<unsigned>(connects_));
        (void)append_truncating(n, cap, at);
        return at;
    }

    // "#3 c1/#3 +2431 ms not-json len=4096 split@4096 head[{"type":"book",...]
    //  tail[...,"qty":24]"
    //
    // Everything the verdict rests on is on the line beside the bytes, so the
    // record can be re-argued from a bench log rather than believed.
    static std::size_t render(const RejectRecord& r, char* out, std::size_t cap) noexcept {
        if (out == nullptr || cap == 0) { return 0; }
        out[0] = '\0';
        std::size_t at = 0;
        int n = std::snprintf(out, cap, "#%u c%u/#%u +%u ms %s len=%u",
                              static_cast<unsigned>(r.ordinal), static_cast<unsigned>(r.connect),
                              static_cast<unsigned>(r.in_connect),
                              static_cast<unsigned>(r.since_connect_ms),
                              parse_status_name(r.status), static_cast<unsigned>(r.len));
        if (append_truncating(n, cap, at)) { return at; }
        if (r.second_header_at != 0) {
            n = std::snprintf(out + at, cap - at, " SPLIT@%u",
                              static_cast<unsigned>(r.second_header_at));
            if (append_truncating(n, cap, at)) { return at; }
        }
        n = std::snprintf(out + at, cap - at, " head[%s]", r.head);
        if (append_truncating(n, cap, at)) { return at; }
        if (r.tail_n != 0) {
            n = std::snprintf(out + at, cap - at, " tail[%s]", r.tail);
            (void)append_truncating(n, cap, at);
        }
        return at;
    }

private:
    RejectRecord ring_[kRejectLogDepth]{};

    std::uint32_t by_status_[kParseStatusCount]{};
    std::uint32_t total_ = 0;
    std::uint32_t captured_ = 0;
    std::uint32_t suppressed_ = 0;
    std::uint32_t connects_ = 0;
    std::uint32_t in_connect_ = 0;
    std::int64_t connected_at_us_ = 0;
};

}  // namespace depthcharge::fw
