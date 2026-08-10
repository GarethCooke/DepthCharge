// test_reject_log.cpp — the firmware's rejected-payload capture, on the desk.
//
// `firmware/src/reject_log.hpp` is ESP-IDF-free precisely so this file can
// exist. It is the fourth firmware path the host build knows about, after the
// reassembler, the gap histograms and the stall probe, and it is here for the
// same reason all three are: its failure mode is not a crash but a log line that
// misleads — a truncated payload printed as though it were whole, a length that
// counts the captured bytes instead of the message, a head and tail that overlap
// and make a 100-byte frame look like a 150-byte one. Every one of those would
// be read at the bench as evidence about Anvil, and none of them can be seen by
// inspection.
//
// The subject: the 2026-08-10 bench run showed ~1,281 frames rejected in the
// first ~60 s of every connect with `price=0 ticker=0 unknown=0 trunc=0`, and no
// way to say what they were. These tests pin the instrument that answers it.
#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "reject_log.hpp"

using depthcharge::anvil::ParseStatus;
using depthcharge::fw::copy_printable;
using depthcharge::fw::find_second_frame_header;
using depthcharge::fw::kRejectHeadBytes;
using depthcharge::fw::kRejectLogDepth;
using depthcharge::fw::kRejectsPerConnect;
using depthcharge::fw::kRejectTailBytes;
using depthcharge::fw::parse_status_name;
using depthcharge::fw::RejectLog;
using depthcharge::fw::RejectRecord;

namespace {

// A whole Anvil book frame, shaped exactly like the wire, padded to `len` with
// extra levels so a test can make one of any realistic size.
std::string book_frame(std::size_t target_len) {
    std::string s = R"({"type":"book","seq":362011,"ticker":101,"bids":[)";
    while (s.size() + 64 < target_len) {
        s += R"({"price":"10.0352","qty":24,"orders":1},)";
    }
    if (s.back() == ',') { s.pop_back(); }
    s += R"(],"asks":[]})";
    return s;
}

constexpr std::int64_t kConnectUs = 1'000'000;

// Guard-region check as one assertion rather than one per byte: the sweep below
// runs it 600 times, and a per-byte CHECK would bury the suite's assertion count
// under 100,000 of them without saying anything a single answer does not.
bool intact(const char* buf, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i) {
        if (buf[i] != '#') { return false; }
    }
    return true;
}

// The console's own loop, so the tests exercise the sequence the firmware
// actually reads rather than the ring directly.
std::vector<RejectRecord> drain(const RejectLog& log, std::uint32_t& printed) {
    std::vector<RejectRecord> out;
    if (printed < log.oldest_retained()) { printed = log.oldest_retained(); }
    while (printed < log.captured()) {
        const RejectRecord* r = log.at(printed);
        ++printed;
        if (r != nullptr) { out.push_back(*r); }
    }
    return out;
}

}  // namespace

TEST_CASE("a rejected payload is captured with its status, its whole length and its type") {
    RejectLog log;
    log.note_connect(kConnectUs);

    const std::string frame = book_frame(6000);
    log.note(ParseStatus::NotJson, frame.data(), static_cast<std::uint32_t>(frame.size()),
             kConnectUs + 2'431'000);

    REQUIRE(log.captured() == 1);
    const RejectRecord* r = log.at(0);
    REQUIRE(r != nullptr);

    CHECK(r->status == ParseStatus::NotJson);
    // THE WHOLE PAYLOAD, not what was kept. A record that reported 96 here would
    // hide the single most diagnostic fact available — whether the length sits on
    // a 4096-byte chunk boundary.
    CHECK(r->len == frame.size());
    CHECK(r->ordinal == 1);
    CHECK(r->connect == 1);
    CHECK(r->in_connect == 1);
    CHECK(r->since_connect_ms == 2431);

    // The head carries the type, which is the question the bench could not
    // answer from a counter.
    CHECK(std::strncmp(r->head, R"({"type":"book")", 14) == 0);
    CHECK(r->head_n == kRejectHeadBytes);
    CHECK(r->tail_n == kRejectTailBytes);
    // A whole frame ends with its closing brace; a truncated one does not, and
    // that difference is the only thing separating "rejected on its contents"
    // from "cut off". The tail must therefore reach the payload's LAST byte —
    // the first draft was one short and lost exactly this.
    CHECK(r->tail[r->tail_n - 1] == '}');
    CHECK(r->tail[r->tail_n - 1] == frame.back());
    CHECK(r->second_header_at == 0);
}

TEST_CASE("a truncation is visible in the tail, and a splice in the offset") {
    RejectLog log;
    log.note_connect(kConnectUs);

    // What a chunk-boundary truncation looks like: a real frame cut at 4096.
    const std::string whole = book_frame(6000);
    const std::string cut = whole.substr(0, 4096);
    log.note(ParseStatus::NotJson, cut.data(), 4096, kConnectUs + 1000);

    // What two messages in one buffer looks like: the second frame's header
    // lands inside the payload, where no Anvil frame ever puts one.
    const std::string spliced = cut + book_frame(2000);
    log.note(ParseStatus::NotJson, spliced.data(), static_cast<std::uint32_t>(spliced.size()),
             kConnectUs + 2000);

    const RejectRecord* truncated = log.at(0);
    const RejectRecord* split = log.at(1);
    REQUIRE(truncated != nullptr);
    REQUIRE(split != nullptr);

    CHECK(truncated->len == 4096);
    CHECK(truncated->second_header_at == 0);
    CHECK(truncated->tail[truncated->tail_n - 1] != '}');  // stopped mid-token

    CHECK(split->second_header_at == 4096);
    // And this is why the offset is worth computing rather than eyeballing: the
    // spliced payload begins like a valid frame and ends like a valid frame.
    CHECK(std::strncmp(split->head, R"({"type":"book")", 14) == 0);
    CHECK(split->tail[split->tail_n - 1] == '}');
}

TEST_CASE("the second-header search never reports the payload's own header") {
    const std::string one = book_frame(2000);
    CHECK(find_second_frame_header(one.data(), static_cast<std::uint32_t>(one.size())) == 0);

    // A summary frame is full of nested objects and none of them carries a
    // `type`, which is what makes the marker safe to search for.
    const std::string summary =
        R"({"type":"summary","seq":1,"tickers":[{"ticker":101,"restingBuy":1},)"
        R"({"ticker":102,"restingBuy":2}]})";
    CHECK(find_second_frame_header(summary.data(),
                                   static_cast<std::uint32_t>(summary.size())) == 0);

    // Degenerate inputs, which on this path are network buffers and not
    // literals.
    CHECK(find_second_frame_header(nullptr, 100) == 0);
    CHECK(find_second_frame_header(one.data(), 0) == 0);
    CHECK(find_second_frame_header(R"({"type":)", 8) == 0);  // exactly the marker, once

    // Adjacent duplicates: the FIRST repeat is the one reported, because it is
    // the boundary the buffer was spliced at.
    const std::string three = one + one + one;
    CHECK(find_second_frame_header(three.data(), static_cast<std::uint32_t>(three.size())) ==
          one.size());
}

TEST_CASE("head and tail never overlap, so a short payload is not reported twice") {
    RejectLog log;
    log.note_connect(kConnectUs);

    // Shorter than the head buffer: entirely in the head, no tail at all.
    const std::string tiny = R"({"type":"pong"})";
    log.note(ParseStatus::MissingType, tiny.data(), static_cast<std::uint32_t>(tiny.size()),
             kConnectUs);
    const RejectRecord* a = log.at(0);
    REQUIRE(a != nullptr);
    CHECK(a->head_n == tiny.size());
    CHECK(std::string(a->head) == tiny);
    CHECK(a->tail_n == 0);

    // Exactly at the boundary: a payload of exactly the head's capacity is all
    // head and no tail, and one byte longer is the first that leaves a tail.
    const std::string exact(kRejectHeadBytes, 'x');
    log.note(ParseStatus::BadShape, exact.data(), static_cast<std::uint32_t>(exact.size()),
             kConnectUs);
    const RejectRecord* b = log.at(1);
    REQUIRE(b != nullptr);
    CHECK(b->head_n == kRejectHeadBytes);
    CHECK(b->tail_n == 0);

    const std::string one_over(kRejectHeadBytes + 1, 'x');
    log.note(ParseStatus::BadShape, one_over.data(), static_cast<std::uint32_t>(one_over.size()),
             kConnectUs);
    const RejectRecord* b2 = log.at(2);
    REQUIRE(b2 != nullptr);
    CHECK(b2->head_n == kRejectHeadBytes);
    CHECK(b2->tail_n == 1);

    // Long enough for both, and the two together must never claim more bytes
    // than the payload has.
    const std::string mid(kRejectHeadBytes + 10, 'y');
    log.note(ParseStatus::BadShape, mid.data(), static_cast<std::uint32_t>(mid.size()),
             kConnectUs);
    const RejectRecord* c = log.at(3);
    REQUIRE(c != nullptr);
    CHECK(c->head_n + c->tail_n <= mid.size());
    CHECK(c->tail_n == 10);
}

TEST_CASE("every byte is made printable, and the length of the garbage is preserved") {
    char noisy[] = {'{', '"', 'a', '"', '\n', '\t', '\0', '\x7f', '\xff', 'z', '}'};
    RejectLog log;
    log.note_connect(kConnectUs);
    log.note(ParseStatus::NotJson, noisy, sizeof noisy, kConnectUs);

    const RejectRecord* r = log.at(0);
    REQUIRE(r != nullptr);
    // A NUL mid-buffer does NOT end the capture — the parse seam's own contract
    // says a frame may contain embedded NULs, so a log that stopped there would
    // print six bytes of an eleven-byte payload and call it the payload.
    CHECK(r->head_n == sizeof noisy);
    // Five unprintables in the middle become five dots, not one and not none:
    // the run length is part of the evidence about what mangled the buffer.
    CHECK(std::string(r->head) == "{\"a\".....z}");
    CHECK(r->len == sizeof noisy);

    // The helper on its own, at its edges.
    char out[4];
    CHECK(copy_printable(out, sizeof out, "abcdef", 6) == 3);  // capped, NUL-terminated
    CHECK(std::string(out) == "abc");
    CHECK(copy_printable(out, sizeof out, nullptr, 6) == 0);
    CHECK(out[0] == '\0');
    CHECK(copy_printable(out, 0, "abc", 3) == 0);
    CHECK(copy_printable(nullptr, sizeof out, "abc", 3) == 0);
}

TEST_CASE("the capture budget is per connect, and what it turns away is counted") {
    RejectLog log;
    log.note_connect(kConnectUs);

    const std::string frame = book_frame(500);
    const auto reject = [&](std::uint32_t n) {
        for (std::uint32_t i = 0; i < n; ++i) {
            log.note(ParseStatus::NotJson, frame.data(), static_cast<std::uint32_t>(frame.size()),
                     kConnectUs);
        }
    };

    reject(kRejectsPerConnect + 5);
    CHECK(log.total() == kRejectsPerConnect + 5);
    CHECK(log.captured() == kRejectsPerConnect);
    CHECK(log.suppressed() == 5);

    // A reconnect gets its own ten. This is the half of the phenomenon a
    // once-only budget would have hidden: the burst recurs on every connect and
    // is smaller each time, so the reconnect samples are the ones that say
    // whether it is the same shape.
    log.note_connect(kConnectUs + 60'000'000);
    reject(3);
    CHECK(log.captured() == kRejectsPerConnect + 3);
    CHECK(log.suppressed() == 5);
    CHECK(log.total() == kRejectsPerConnect + 8);
    CHECK(log.connects() == 2);

    // Ordinals run across the whole run and the per-connect index restarts, so a
    // bench log stays readable across an outage.
    const RejectRecord* first_of_second = log.at(kRejectsPerConnect);
    REQUIRE(first_of_second != nullptr);
    CHECK(first_of_second->ordinal == kRejectsPerConnect + 1);
    CHECK(first_of_second->connect == 2);
    CHECK(first_of_second->in_connect == 1);
    CHECK(first_of_second->since_connect_ms == 0);
}

TEST_CASE("the tally counts every rejected frame, including the ones with their own counter") {
    RejectLog log;
    log.note_connect(kConnectUs);
    const std::string frame = book_frame(300);
    const auto note = [&](ParseStatus st) {
        log.note(st, frame.data(), static_cast<std::uint32_t>(frame.size()), kConnectUs);
    };

    note(ParseStatus::NotJson);
    note(ParseStatus::NotJson);
    note(ParseStatus::MissingType);
    note(ParseStatus::BadShape);
    note(ParseStatus::BadPrice);
    note(ParseStatus::OtherTicker);

    CHECK(log.count(ParseStatus::NotJson) == 2);
    CHECK(log.count(ParseStatus::MissingType) == 1);
    CHECK(log.count(ParseStatus::BadShape) == 1);
    CHECK(log.count(ParseStatus::BadPrice) == 1);
    CHECK(log.count(ParseStatus::OtherTicker) == 1);
    CHECK(log.count(ParseStatus::Ok) == 0);
    CHECK(log.total() == 6);

    char line[208];
    log.render_tally(line, sizeof line);
    const std::string text(line);
    CHECK(text.find("n=6") == 0);
    CHECK(text.find("not-json=2") != std::string::npos);
    CHECK(text.find("other-ticker=1") != std::string::npos);
    CHECK(text.find("logged=6 suppressed=0 over 1 connects") != std::string::npos);
    // Ok is not a reject and must not take a column in the one line a bench
    // session greps.
    CHECK(text.find("ok=") == std::string::npos);
}

TEST_CASE("a record the reader has not reached yet is retained; an evicted one is reported") {
    RejectLog log;
    log.note_connect(kConnectUs);
    const std::string frame = book_frame(300);

    // Fill past the ring across several connects, since one connect cannot
    // overflow it (that is what the two spare slots buy).
    for (std::uint32_t c = 0; c < 3; ++c) {
        log.note_connect(kConnectUs + c * 1'000'000);
        for (std::uint32_t i = 0; i < kRejectsPerConnect; ++i) {
            log.note(ParseStatus::NotJson, frame.data(), static_cast<std::uint32_t>(frame.size()),
                     kConnectUs);
        }
    }
    CHECK(log.captured() == 3 * kRejectsPerConnect);
    CHECK(log.oldest_retained() == log.captured() - (kRejectLogDepth - 1));

    // Everything below the retained window is null rather than a stale record
    // reported as a fresh one.
    CHECK(log.at(0) == nullptr);
    CHECK(log.at(log.oldest_retained() - 1) == nullptr);
    CHECK(log.at(log.oldest_retained()) != nullptr);
    CHECK(log.at(log.captured() - 1) != nullptr);
    CHECK(log.at(log.captured()) == nullptr);

    // The reader's contract: it skips forward to the retained window and never
    // gets a null in the middle of its loop.
    std::uint32_t printed = 0;
    const auto got = drain(log, printed);
    CHECK(got.size() == kRejectLogDepth - 1);
    CHECK(printed == log.captured());
    CHECK(got.front().ordinal == log.oldest_retained() + 1);
}

TEST_CASE("a whole connect's capture survives the reader's safety margin") {
    // The reason kRejectLogDepth is larger than kRejectsPerConnect: a console
    // that only polls once during a burst must still be able to read all ten of
    // it. At equal sizes the tenth capture would evict the first.
    static_assert(kRejectLogDepth > kRejectsPerConnect,
                  "the ring must retain a whole connect's budget after the reader's margin");

    RejectLog log;
    log.note_connect(kConnectUs);
    const std::string frame = book_frame(300);
    for (std::uint32_t i = 0; i < kRejectsPerConnect; ++i) {
        log.note(ParseStatus::NotJson, frame.data(), static_cast<std::uint32_t>(frame.size()),
                 kConnectUs);
    }

    std::uint32_t printed = 0;
    const auto got = drain(log, printed);
    CHECK(got.size() == kRejectsPerConnect);
    CHECK(got.front().in_connect == 1);
    CHECK(got.back().in_connect == kRejectsPerConnect);
}

TEST_CASE("rendering truncates rather than overruns, and never loses the diagnosis first") {
    RejectLog log;
    log.note_connect(kConnectUs);
    const std::string frame = book_frame(9000);
    log.note(ParseStatus::BadShape, frame.data(), static_cast<std::uint32_t>(frame.size()),
             kConnectUs + 5000);
    const RejectRecord* r = log.at(0);
    REQUIRE(r != nullptr);

    // A guard region either side, so an overrun is a failed assertion rather
    // than a corrupted neighbour nobody notices — the same shape as the gap
    // histogram's render test, but swept across every truncation point rather
    // than one, because this renderer appends five separate fields and each
    // boundary is its own chance to be off by one.
    for (std::size_t cap = 1; cap <= 300; ++cap) {
        CAPTURE(cap);
        char buf[320];
        std::memset(buf, '#', sizeof buf);
        const std::size_t written = RejectLog::render(*r, buf + 8, cap);
        CHECK(written < cap);
        CHECK(buf[8 + written] == '\0');
        CHECK(std::strlen(buf + 8) == written);
        CHECK(intact(buf, 0, 8));                        // before
        CHECK(intact(buf, 8 + cap, sizeof buf));         // after
    }

    // The fields are ordered so that a truncated line still carries the
    // diagnosis: status and length come before the bytes, because a line cut in
    // half must not be one that only says "here is some JSON".
    char line[48];
    RejectLog::render(*r, line, sizeof line);
    const std::string head(line);
    CHECK(head.find("bad-shape") != std::string::npos);
    CHECK(head.find("len=") != std::string::npos);

    char full[288];
    RejectLog::render(*r, full, sizeof full);
    const std::string text(full);
    CHECK(text.find("#1 c1/#1 +5 ms bad-shape len=") == 0);
    CHECK(text.find("len=" + std::to_string(frame.size())) != std::string::npos);
    CHECK(text.find("head[{\"type\":\"book\"") != std::string::npos);
    CHECK(text.find("tail[") != std::string::npos);
    CHECK(text.find("SPLIT@") == std::string::npos);

    CHECK(RejectLog::render(*r, nullptr, sizeof full) == 0);
    CHECK(RejectLog::render(*r, full, 0) == 0);
    CHECK(log.render_tally(nullptr, sizeof full) == 0);
    CHECK(log.render_tally(full, 0) == 0);
}

TEST_CASE("a reject before any connect is still captured, with no clock to hang it on") {
    // The order of Connected against the first frame is the WebSocket client's
    // to choose, not ours, and a record dropped because the status event had not
    // arrived yet would lose exactly the earliest sample of a connect-time burst.
    RejectLog log;
    const std::string frame = book_frame(300);
    log.note(ParseStatus::NotJson, frame.data(), static_cast<std::uint32_t>(frame.size()),
             kConnectUs);

    REQUIRE(log.captured() == 1);
    const RejectRecord* r = log.at(0);
    REQUIRE(r != nullptr);
    CHECK(r->connect == 0);           // no connect seen
    CHECK(r->since_connect_ms == 0);  // reported as zero, never as a bogus interval
    CHECK(r->in_connect == 1);
}

TEST_CASE("a degenerate payload is recorded, not skipped and not read past") {
    // Neither of these can reach the log through the real pipeline — the
    // reassembler filters zero-length messages and never publishes a null slot —
    // but both are one caller mistake away, and the failure of the second is a
    // read off the end of a network buffer rather than a wrong log line.
    RejectLog log;
    log.note_connect(kConnectUs);

    log.note(ParseStatus::NotJson, nullptr, 8000, kConnectUs);
    const RejectRecord* a = log.at(0);
    REQUIRE(a != nullptr);
    CHECK(a->len == 8000);  // the length is the caller's claim and is reported as given
    CHECK(a->head_n == 0);
    CHECK(a->tail_n == 0);
    CHECK(a->head[0] == '\0');
    CHECK(a->second_header_at == 0);

    log.note(ParseStatus::NotJson, "", 0, kConnectUs);
    const RejectRecord* b = log.at(1);
    REQUIRE(b != nullptr);
    CHECK(b->len == 0);
    CHECK(b->head_n == 0);
    CHECK(b->tail_n == 0);

    // Both still render, because a record that crashes the console is worse than
    // one that says little.
    char line[512];
    CHECK(RejectLog::render(*a, line, sizeof line) > 0);
    CHECK(RejectLog::render(*b, line, sizeof line) > 0);
}

TEST_CASE("random walk: the tally stays closed and the retained window stays sane") {
    // The same shape of test the reassembler and the stall probe carry, and for
    // the same reason: the interesting failures here are not in any one call but
    // in a sequence of them — a budget that leaks across connects, a suppressed
    // reject that is also counted as captured, a retained window that outruns the
    // ring. None of those is visible in an example-based test that does the
    // obvious thing twice.
    RejectLog log;
    std::uint32_t printed = 0;
    std::uint32_t drained = 0;
    std::uint32_t seen_ordinal = 0;

    const std::string frame = book_frame(700);
    const ParseStatus statuses[] = {ParseStatus::NotJson,  ParseStatus::MissingType,
                                    ParseStatus::BadShape, ParseStatus::BadPrice,
                                    ParseStatus::OtherTicker};

    // A cheap deterministic walk: reproducible, so a failure is debuggable, and
    // seeded from nothing so it is the same on every machine.
    std::uint32_t rng = 0x1234567u;
    const auto next = [&rng](std::uint32_t n) {
        rng = rng * 1664525u + 1013904223u;
        return (rng >> 16) % n;
    };

    std::int64_t clock_us = kConnectUs;
    for (std::uint32_t step = 0; step < 5000; ++step) {
        clock_us += 1 + next(200000);
        const std::uint32_t roll = next(100);
        if (roll < 5) {
            log.note_connect(clock_us);
        } else if (roll < 90) {
            const std::uint32_t len = 1 + next(static_cast<std::uint32_t>(frame.size()));
            log.note(statuses[next(5)], frame.data(), len, clock_us);
        } else {
            // The console, running at its own pace rather than in lockstep.
            const auto got = drain(log, printed);
            for (const RejectRecord& r : got) {
                CHECK(r.ordinal > seen_ordinal);  // dense and increasing, never replayed
                seen_ordinal = r.ordinal;
                ++drained;
            }
        }

        // Every reject is either kept or turned away, never both and never
        // neither. This is the property that catches a budget leak.
        CHECK(log.total() == log.captured() + log.suppressed());

        // The per-status columns partition the total, so the tally line and the
        // `-- errors` line can be checked against each other at the bench.
        std::uint32_t by_status = 0;
        for (std::size_t i = static_cast<std::size_t>(ParseStatus::NotJson);
             i < depthcharge::fw::kParseStatusCount; ++i) {
            by_status += log.count(static_cast<ParseStatus>(i));
        }
        CHECK(by_status == log.total());
        CHECK(log.count(ParseStatus::Ok) == 0);  // success is never a reject

        // The retained window never outruns the ring, and never runs backwards.
        CHECK(log.captured() - log.oldest_retained() <= kRejectLogDepth - 1);
        CHECK(log.oldest_retained() <= log.captured());

        // Every index inside the window resolves, and every index outside it is
        // null rather than a stale record dressed as a fresh one.
        if (log.captured() > 0) {
            CHECK(log.at(log.oldest_retained()) != nullptr);
            CHECK(log.at(log.captured() - 1) != nullptr);
        }
        CHECK(log.at(log.captured()) == nullptr);
        if (log.oldest_retained() > 0) { CHECK(log.at(log.oldest_retained() - 1) == nullptr); }
    }

    // The walk actually exercised the thing: it overflowed the ring, it turned
    // rejects away, and the reader really read. Without these the assertions
    // above could all hold on an empty log.
    CHECK(log.connects() > 1);
    CHECK(log.suppressed() > 0);
    CHECK(log.captured() > kRejectLogDepth);
    CHECK(drained > 0);

    // Every record the reader ever saw was well formed. Checked here rather than
    // per-step so the count stays readable.
    for (std::uint32_t i = log.oldest_retained(); i < log.captured(); ++i) {
        const RejectRecord* r = log.at(i);
        REQUIRE(r != nullptr);
        CHECK(r->head_n + r->tail_n <= r->len);
        CHECK(std::strlen(r->head) == r->head_n);
        CHECK(std::strlen(r->tail) == r->tail_n);
        CHECK(r->in_connect >= 1);
        CHECK(r->in_connect <= kRejectsPerConnect);
        for (std::uint8_t c = 0; c < r->head_n; ++c) {
            CHECK(r->head[c] >= 0x20);
            CHECK(r->head[c] < 0x7F);
        }
    }
}

TEST_CASE("every ParseStatus has a name, and none of them is the fallback") {
    const ParseStatus all[] = {ParseStatus::Ok,       ParseStatus::NotJson,
                               ParseStatus::MissingType, ParseStatus::BadShape,
                               ParseStatus::BadPrice, ParseStatus::OtherTicker};
    for (ParseStatus st : all) {
        CHECK(std::strcmp(parse_status_name(st), "?") != 0);
        CHECK(std::strlen(parse_status_name(st)) > 0);
    }
}
