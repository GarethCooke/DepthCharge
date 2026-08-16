// test_ws_frame.cpp — the frame layer this project now owns, on the desk.
//
// The whole argument for removing esp_websocket_client is that its framing was
// wrong in a way nobody could see until a bench log caught it: `SPLIT@1`, one
// stray byte of the previous message glued ahead of the next one, 85 times in
// four hours, co-timed with 17 errno-silent socket deaths
// (`hardware/bench-2026-08-13-wifi-drop-diagnosis.md`). Replacing a library
// because it has a bug is only an improvement if the replacement's equivalent
// bug would be caught on the desk in a second, which is what this file is for.
//
// So the shape of the suite is deliberate: almost every case drives the REAL
// chain — WsFrameParser -> FrameReassembler -> a fake slot pool — and asserts on
// the *messages the feed task would receive*, byte for byte, rather than on the
// parser's internal opinion of them. And the property that matters most is
// tested by exhaustion rather than by example: the same byte stream, delivered
// in every read size from one byte upward, must produce identical messages. A
// framing off-by-one cannot survive that, because it can only be right at one
// alignment.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "alloc_probe.hpp"
#include "fake_slots.hpp"
#include "frame_reassembler.hpp"
#include "reject_log.hpp"
#include "ws_frame.hpp"

using depthcharge::fw::FrameReassembler;
using depthcharge::fw::kCloseNoStatus;
using depthcharge::fw::kMaxControlPayload;
using depthcharge::fw::kMaxFramePayload;
using depthcharge::fw::kOpBinary;
using depthcharge::fw::kOpClose;
using depthcharge::fw::kOpContinuation;
using depthcharge::fw::kOpPing;
using depthcharge::fw::kOpPong;
using depthcharge::fw::kOpText;
using depthcharge::fw::WsChunk;
using depthcharge::fw::WsFrameParser;
using depthcharge::fw::WsProtocolError;

namespace {

// The firmware's real numbers: kFrameCapacity is 16 KiB and the socket is read
// in 4 KiB bites. Both are here rather than as bare literals because the tests
// that matter are about what happens AT those boundaries.
constexpr std::size_t kCap = 16 * 1024;
constexpr std::size_t kReadSize = 4096;
constexpr std::uint8_t kSlots = 2;

// Stands in for FramePipe — fake_slots.hpp, the same fake test_frame_reassembler
// .cpp drives at its 64-byte capacity, here at the firmware's real geometry.
// This file used to carry a second, thinner copy of it; the two had drifted, and
// this one had lost `acquire_failures` and the arrival/published-at pair.
using Pipe = dc::testing::FakeSlotPool<kCap, kSlots>;

// The transport's half, recorded. Data chunks go downstream to the real
// reassembler; control frames are captured as the transport would capture them
// (a pong to write, a close code for the autopsy line).
struct Recorder {
    struct Chunk {
        std::uint8_t op = 0;
        std::uint32_t offset = 0;
        std::uint32_t len = 0;
        std::string data;
        bool fin = true;
    };
    struct Close {
        std::uint16_t code = 0;
        std::string reason;
    };

    FrameReassembler<Pipe>* down = nullptr;
    std::vector<Chunk> chunks;
    std::vector<std::string> pings;
    std::vector<Close> closes;
    int pongs = 0;
    std::vector<WsProtocolError> errors;

    void on_chunk(const WsChunk& c) {
        // Copied HERE, inside the call, which is the borrowed-span rule the
        // chunk carries: a test that stashed the pointer would be testing
        // something the contract does not promise.
        chunks.push_back(Chunk{c.op_code, c.payload_offset, c.payload_len,
                               std::string(c.data, c.data_len), c.fin});
        if (down != nullptr) { down->on_chunk(c); }
    }
    // Control frames stop here, exactly as they do in the transport: the
    // reassembler never sees one, because a pong is a write and a close code is
    // an autopsy field, and neither is a message.
    void on_ping(const std::uint8_t* payload, std::uint32_t len) {
        pings.emplace_back(reinterpret_cast<const char*>(payload), len);
    }
    void on_pong() { ++pongs; }
    void on_close(std::uint16_t code, const char* reason, std::uint32_t reason_len) {
        closes.push_back(Close{code, std::string(reason, reason_len)});
    }
    void on_protocol_error(WsProtocolError e) { errors.push_back(e); }
};

struct Chain {
    Pipe pipe;
    FrameReassembler<Pipe> re{pipe, kCap};
    Recorder rec;
    WsFrameParser<Recorder> parser{rec};

    Chain() { rec.down = &re; }

    // Deliver a byte stream in `read`-sized pieces, exactly as the RX task's
    // esp_tls_conn_read loop would.
    void feed(const std::vector<std::uint8_t>& bytes, std::size_t read = kReadSize,
              std::int64_t at_us = 0) {
        for (std::size_t i = 0; i < bytes.size(); i += read) {
            const std::size_t n = std::min(read, bytes.size() - i);
            parser.feed(bytes.data() + i, static_cast<std::uint32_t>(n), at_us);
        }
    }
};

enum class LenForm { Auto, Short, Ext16, Ext64 };

// One server->client frame, on the wire. Unmasked, because a server's frames
// are (§5.1) and because sending one masked is a case of its own below.
std::vector<std::uint8_t> frame(std::uint8_t opcode, const std::string& payload, bool fin = true,
                                LenForm form = LenForm::Auto) {
    std::vector<std::uint8_t> out;
    out.push_back(static_cast<std::uint8_t>((fin ? 0x80u : 0x00u) | opcode));
    const std::uint64_t n = payload.size();
    LenForm f = form;
    if (f == LenForm::Auto) {
        f = (n < 126) ? LenForm::Short : (n <= 0xFFFFu ? LenForm::Ext16 : LenForm::Ext64);
    }
    if (f == LenForm::Short) {
        out.push_back(static_cast<std::uint8_t>(n));
    } else if (f == LenForm::Ext16) {
        out.push_back(126);
        out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>(n & 0xFFu));
    } else {
        out.push_back(127);
        for (int shift = 56; shift >= 0; shift -= 8) {
            out.push_back(static_cast<std::uint8_t>((n >> shift) & 0xFFu));
        }
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

void append(std::vector<std::uint8_t>& into, const std::vector<std::uint8_t>& more) {
    into.insert(into.end(), more.begin(), more.end());
}

// An Anvil-shaped message of an exact size: starts with '{', carries a seq, and
// its filler is unique per seq so a byte borrowed from a neighbouring message is
// visible rather than merely wrong.
std::string message(int seq, std::size_t size) {
    const std::string head = R"({"type":"book","seq":)" + std::to_string(seq) + R"(,"pad":")";
    const std::string tail = R"("})";
    // Below ~32 bytes there is no room for the JSON shape; those cases exist to
    // exercise the 7-bit length form, where the content is not the point.
    if (size <= head.size() + tail.size()) {
        return std::string(size, static_cast<char>('a' + (seq % 26)));
    }
    std::string s = head;
    s.append(size - head.size() - tail.size(), static_cast<char>('a' + (seq % 26)));
    s += tail;
    REQUIRE(s.size() == size);
    return s;
}

// The largest message across the two committed captures, and therefore the one
// the reassembler runs against ~12 times a second on the board.
constexpr std::size_t kBookFrameBytes = 8726;

}  // namespace

// --- the shape of one frame --------------------------------------------------

TEST_CASE("a whole text frame becomes one chunk carrying what the reassembler needs") {
    Chain c;
    const std::string msg = R"({"type":"book","seq":1})";
    c.feed(frame(kOpText, msg));

    REQUIRE(c.rec.chunks.size() == 1);
    CHECK(c.rec.chunks[0].op == kOpText);
    CHECK(c.rec.chunks[0].offset == 0);
    CHECK(c.rec.chunks[0].len == msg.size());
    CHECK(c.rec.chunks[0].data == msg);
    CHECK(c.rec.chunks[0].fin);
    REQUIRE(c.pipe.published.size() == 1);
    CHECK(c.pipe.published[0] == msg);
    CHECK(c.parser.data_frames() == 1);
    CHECK_FALSE(c.parser.failed());
    CHECK(c.pipe.free_count == kSlots);
}

TEST_CASE("all three length forms decode to the same message") {
    // 7-bit, 16-bit and 64-bit lengths are three different header layouts, and
    // the extended ones are big-endian byte strings the parser accumulates one
    // byte at a time. An 8.7 KB book frame uses the 16-bit form on every wire
    // Anvil has ever produced, so the other two are exercised only here.
    for (const std::size_t size : {std::size_t{5}, std::size_t{200}, kBookFrameBytes}) {
        INFO("payload size ", size);
        Chain c;
        const std::string msg = message(1, size);
        c.feed(frame(kOpText, msg));
        REQUIRE(c.pipe.published.size() == 1);
        CHECK(c.pipe.published[0] == msg);
    }

    SUBCASE("the 64-bit form is accepted for a small payload too") {
        // Non-minimal, which §5.2 says a sender should not do. Accepted
        // deliberately: rejecting it would add a way for this firmware to drop a
        // socket in exchange for catching nothing kMaxFramePayload does not.
        Chain c;
        const std::string msg = message(2, 300);
        c.feed(frame(kOpText, msg, true, LenForm::Ext64));
        REQUIRE(c.pipe.published.size() == 1);
        CHECK(c.pipe.published[0] == msg);
    }
}

TEST_CASE("a binary frame is framed like a text one") {
    Chain c;
    const std::string msg = message(3, 64);
    c.feed(frame(kOpBinary, msg));
    REQUIRE(c.pipe.published.size() == 1);
    CHECK(c.pipe.published[0] == msg);
}

TEST_CASE("a zero-length data frame publishes nothing and leaks nothing") {
    Chain c;
    c.feed(frame(kOpText, ""));
    REQUIRE(c.rec.chunks.size() == 1);   // the reassembler still sees it begin and end
    CHECK(c.rec.chunks[0].len == 0);
    CHECK(c.pipe.published.empty());
    CHECK(c.pipe.free_count == kSlots);
    CHECK_FALSE(c.re.holding());
}

// --- the property that would have caught the library ------------------------

TEST_CASE("the 8.7 KB book frame survives one-byte deliveries") {
    // Every header byte and every payload byte in its own read: the worst
    // alignment there is, and the one where an accounting slip cannot hide.
    Chain c;
    const std::string msg = message(7, kBookFrameBytes);
    c.feed(frame(kOpText, msg), 1);

    REQUIRE(c.pipe.published.size() == 1);
    CHECK(c.pipe.published[0] == msg);
    CHECK(c.pipe.chunks == kBookFrameBytes);   // one chunk per byte, all appended in order
    CHECK(c.pipe.free_count == kSlots);
}

TEST_CASE("read size does not change the messages, at any size") {
    // The exhaustive form of the claim. A stream of five frames whose sizes
    // straddle both length-form boundaries, delivered in every read size from 1
    // to 137 and then at the production 4 KiB, must produce the identical five
    // messages every time.
    std::vector<std::string> want;
    std::vector<std::uint8_t> wire;
    const std::size_t sizes[] = {40, 125, 126, 300, 1000};
    int seq = 0;
    for (const std::size_t size : sizes) {
        want.push_back(message(++seq, size));
        append(wire, frame(kOpText, want.back()));
    }

    for (std::size_t read = 1; read <= 137; ++read) {
        INFO("read size ", read);
        Chain c;
        c.feed(wire, read);
        REQUIRE(c.pipe.published.size() == want.size());
        for (std::size_t i = 0; i < want.size(); ++i) { CHECK(c.pipe.published[i] == want[i]); }
        CHECK(c.pipe.free_count == kSlots);
        CHECK_FALSE(c.parser.failed());
    }

    Chain big;
    big.feed(wire, kReadSize);
    REQUIRE(big.pipe.published.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) { CHECK(big.pipe.published[i] == want[i]); }
}

TEST_CASE("SPLIT@1: a frame that ends one byte into a read does not glue that byte on") {
    // THE REGRESSION FOR THE BUG THAT COST THE LIBRARY ITS JOB.
    //
    // The production geometry, reproduced exactly: with a 4,096-byte read and a
    // 4,097-byte first frame, the read that begins the second message opens with
    // the last byte of the first one. esp_websocket_client's payload-offset
    // accounting put that byte at index 0 of the next message's slot, and the
    // adapter logged `not-json len=8703 SPLIT@1 head[.{"type":"book"...` — clean
    // JSON with one stray character in front of it, 85 times in four hours.
    //
    // Two assertions, because "the messages are right" and "no message contains
    // a second header" fail differently: the first catches a byte borrowed from
    // either neighbour, the second is the exact signature the bench reject log
    // scans for, checked here so a future regression is named rather than merely
    // red.
    //
    // The second one calls `reject_log.hpp`'s own `find_second_frame_header`
    // rather than re-spelling `{"type":` in a std::string::find. The 2026-08-15
    // review's point: a regression test for a bench signature is only worth what
    // it catches, and two independent spellings of that signature can drift
    // apart — at which point the desk stops testing the thing the board reports.
    const std::string first = message(1, 4093);   // + 4 bytes of 16-bit header = 4097
    const std::string second = message(2, kBookFrameBytes);
    const std::vector<std::uint8_t> first_frame = frame(kOpText, first);
    REQUIRE(first_frame.size() % kReadSize == 1);   // the geometry IS the test
    std::vector<std::uint8_t> wire = first_frame;
    append(wire, frame(kOpText, second));

    Chain c;
    c.feed(wire, kReadSize);

    REQUIRE(c.pipe.published.size() == 2);
    CHECK(c.pipe.published[0] == first);
    CHECK(c.pipe.published[1] == second);
    for (const std::string& m : c.pipe.published) {
        INFO("message of ", m.size(), " bytes");
        CHECK(m.front() == '{');
        // The reject log's own scanner, on the reject log's own terms: it
        // returns the offset of a second `{"type":` and 0 when there is only
        // one, which is what the board prints as `SPLIT@N`.
        CHECK(depthcharge::fw::find_second_frame_header(
                  m.data(), static_cast<std::uint32_t>(m.size())) == 0u);
    }
}

TEST_CASE("a header split across a read boundary is still one header") {
    // Every place a two-frame stream can be cut in two, including inside the
    // 16-bit length field, between the two header bytes, and one byte either
    // side of the payload's first byte.
    const std::string a = message(1, 300);
    const std::string b = message(2, 300);
    std::vector<std::uint8_t> wire;
    append(wire, frame(kOpText, a));
    append(wire, frame(kOpText, b));

    for (std::size_t cut = 1; cut < wire.size(); ++cut) {
        INFO("cut at ", cut);
        Chain c;
        c.parser.feed(wire.data(), static_cast<std::uint32_t>(cut), 0);
        c.parser.feed(wire.data() + cut, static_cast<std::uint32_t>(wire.size() - cut), 0);
        REQUIRE(c.pipe.published.size() == 2);
        CHECK(c.pipe.published[0] == a);
        CHECK(c.pipe.published[1] == b);
    }
}

// --- fragmentation, which we can now see ------------------------------------

TEST_CASE("a server-fragmented message is published once, whole") {
    // esp_websocket_client never surfaced FIN, so this arrived as two messages
    // and two parse errors (frame_reassembler.hpp's note). Reading the bit
    // ourselves is the entire fix.
    Chain c;
    std::vector<std::uint8_t> wire;
    append(wire, frame(kOpText, R"({"type":"book",)", /*fin=*/false));
    append(wire, frame(kOpContinuation, R"("seq":9,)", /*fin=*/false));
    append(wire, frame(kOpContinuation, R"("ticker":101})", /*fin=*/true));
    c.feed(wire);

    REQUIRE(c.pipe.published.size() == 1);
    CHECK(c.pipe.published[0] == R"({"type":"book","seq":9,"ticker":101})");
    CHECK(c.pipe.continuation == 2);   // still counted: Anvil has never sent one
    CHECK(c.pipe.free_count == kSlots);
    CHECK_FALSE(c.parser.message_open());
}

TEST_CASE("control frames may sit between fragments and disturb nothing") {
    // §5.4 allows exactly this, and it is the case a fragmentation-blind parser
    // gets wrong in the most confusing way — the ping's payload appended to the
    // message it interrupted.
    Chain c;
    std::vector<std::uint8_t> wire;
    append(wire, frame(kOpText, "{\"a\":1,", /*fin=*/false));
    append(wire, frame(kOpPing, "keepalive"));
    append(wire, frame(kOpPong, ""));
    append(wire, frame(kOpContinuation, "\"b\":2}", /*fin=*/true));
    c.feed(wire, 3);   // and split every one of them across reads

    REQUIRE(c.pipe.published.size() == 1);
    CHECK(c.pipe.published[0] == "{\"a\":1,\"b\":2}");
    REQUIRE(c.rec.pings.size() == 1);
    CHECK(c.rec.pings[0] == "keepalive");
    CHECK(c.rec.pongs == 1);
    CHECK(c.parser.control_frames() == 2);
}

TEST_CASE("read size does not change a fragmented message either, at any size") {
    // The 2026-08-15 review's finding on the exhaustive sweep above: it runs
    // only whole text frames, while the continuation path carries its own
    // offset arithmetic in both layers — exactly the read-boundary bug class
    // that convicted the old library. Same claim, fragment-shaped: a message
    // split across three fragments with a ping and a pong wedged between them,
    // delivered at every read size from 1 to 137 and at the production 4 KiB,
    // must publish the identical messages and answer the same ping every time.
    const std::string before = message(1, 500);
    const std::string frag = message(2, 900);
    const std::string after = message(3, 300);
    std::vector<std::uint8_t> wire;
    append(wire, frame(kOpText, before));
    append(wire, frame(kOpText, frag.substr(0, 200), /*fin=*/false));
    append(wire, frame(kOpPing, "mid"));
    append(wire, frame(kOpContinuation, frag.substr(200, 450), /*fin=*/false));
    append(wire, frame(kOpPong, ""));
    append(wire, frame(kOpContinuation, frag.substr(650), /*fin=*/true));
    append(wire, frame(kOpText, after));

    for (std::size_t read = 1; read <= 137; ++read) {
        INFO("read size ", read);
        Chain c;
        c.feed(wire, read);
        REQUIRE(c.pipe.published.size() == 3);
        CHECK(c.pipe.published[0] == before);
        CHECK(c.pipe.published[1] == frag);
        CHECK(c.pipe.published[2] == after);
        REQUIRE(c.rec.pings.size() == 1);
        CHECK(c.rec.pings[0] == "mid");
        CHECK(c.rec.pongs == 1);
        CHECK(c.pipe.free_count == kSlots);
        CHECK_FALSE(c.parser.failed());
        CHECK_FALSE(c.parser.message_open());
    }

    Chain big;
    big.feed(wire, kReadSize);
    REQUIRE(big.pipe.published.size() == 3);
    CHECK(big.pipe.published[1] == frag);
}

// --- control frames ----------------------------------------------------------

TEST_CASE("an empty ping is still delivered, because it still owes a pong") {
    // The common spelling — esp_websocket_client itself sent them. The autopsy
    // prototype's first draft dispatched completion only from the payload state
    // and so never ponged one, which would have made the instrument the cause of
    // the deaths it was built to explain.
    Chain c;
    c.feed(frame(kOpPing, ""));
    REQUIRE(c.rec.pings.size() == 1);
    CHECK(c.rec.pings[0].empty());
}

TEST_CASE("a ping payload split across reads is delivered whole") {
    Chain c;
    const std::string payload(kMaxControlPayload, 'p');   // the RFC's maximum
    c.feed(frame(kOpPing, payload), 7);
    REQUIRE(c.rec.pings.size() == 1);
    CHECK(c.rec.pings[0] == payload);
    CHECK_FALSE(c.parser.failed());
}

TEST_CASE("a close frame's code and reason are captured") {
    SUBCASE("code and reason text") {
        Chain c;
        std::string payload = "\x03\xe8";   // 1000, normal closure
        payload += "going away";
        c.feed(frame(kOpClose, payload));
        REQUIRE(c.rec.closes.size() == 1);
        CHECK(c.rec.closes[0].code == 1000);
        CHECK(c.rec.closes[0].reason == "going away");
    }
    SUBCASE("code with no reason") {
        Chain c;
        c.feed(frame(kOpClose, std::string("\x03\xf3", 2)));   // 1011, internal error
        REQUIRE(c.rec.closes.size() == 1);
        CHECK(c.rec.closes[0].code == 1011);
        CHECK(c.rec.closes[0].reason.empty());
    }
    SUBCASE("no payload at all is 1005, the code that is never sent") {
        Chain c;
        c.feed(frame(kOpClose, ""));
        REQUIRE(c.rec.closes.size() == 1);
        CHECK(c.rec.closes[0].code == kCloseNoStatus);
    }
    SUBCASE("split across reads, including between the two code bytes") {
        Chain c;
        std::string payload = "\x03\xe9";   // 1001, going away
        payload += "bye";
        c.feed(frame(kOpClose, payload), 1);
        REQUIRE(c.rec.closes.size() == 1);
        CHECK(c.rec.closes[0].code == 1001);
        CHECK(c.rec.closes[0].reason == "bye");
    }
    SUBCASE("a one-byte payload is illegal and reads as no-status") {
        // Reported rather than rejected: the response to a close is the same
        // either way, so an error class for it would be a path with no action.
        Chain c;
        c.feed(frame(kOpClose, std::string("\x03", 1)));
        REQUIRE(c.rec.closes.size() == 1);
        CHECK(c.rec.closes[0].code == kCloseNoStatus);
    }
}

TEST_CASE("a close arriving mid-stream does not disturb the message before it") {
    Chain c;
    const std::string msg = message(4, 500);
    std::vector<std::uint8_t> wire;
    append(wire, frame(kOpText, msg));
    append(wire, frame(kOpClose, std::string("\x03\xe8", 2) + "bye"));
    c.feed(wire, 64);

    REQUIRE(c.pipe.published.size() == 1);
    CHECK(c.pipe.published[0] == msg);
    REQUIRE(c.rec.closes.size() == 1);
    CHECK(c.rec.closes[0].code == 1000);
}

// --- losing the stream -------------------------------------------------------

TEST_CASE("every way the stream stops being a WebSocket stream is named") {
    // Each of these means the same thing operationally — reconnect — so what is
    // pinned here is the LABEL, because a bench line that says which one it was
    // is the reason this layer is ours.
    struct Case {
        const char* what;
        std::vector<std::uint8_t> bytes;
        WsProtocolError want;
    };
    std::vector<Case> cases;

    // A masked server frame (§5.1). Also the fastest detector of a lost byte:
    // half of all payload bytes have the top bit set.
    {
        std::vector<std::uint8_t> b = {0x81, 0x85, 0, 0, 0, 0};
        b.insert(b.end(), {'h', 'e', 'l', 'l', 'o'});
        cases.push_back({"masked frame", b, WsProtocolError::MaskedFrame});
    }
    cases.push_back({"reserved bit", {0xC1, 0x02, 'h', 'i'}, WsProtocolError::ReservedBits});
    cases.push_back({"reserved opcode", {0x83, 0x02, 'h', 'i'}, WsProtocolError::BadOpcode});
    cases.push_back({"fragmented control", {0x09, 0x00}, WsProtocolError::FragmentedControl});
    cases.push_back({"control payload > 125", {0x89, 0x7E, 0x00, 0xFF}, WsProtocolError::ControlTooLong});
    // A 64-bit length past the sync ceiling: not a deep book, a lost place.
    cases.push_back({"absurd length",
                     {0x82, 0x7F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00},
                     WsProtocolError::LengthTooLarge});
    cases.push_back({"continuation with nothing open",
                     {0x80, 0x02, 'h', 'i'},
                     WsProtocolError::UnexpectedContinuation});
    {
        std::vector<std::uint8_t> b;
        append(b, frame(kOpText, "abc", /*fin=*/false));
        append(b, frame(kOpText, "def"));   // a second data frame inside the message
        cases.push_back({"interleaved data frame", b, WsProtocolError::InterleavedFrame});
    }

    for (const Case& k : cases) {
        INFO(k.what);
        Chain c;
        c.feed(k.bytes, 1);
        REQUIRE(c.parser.failed());
        CHECK(c.parser.error() == k.want);
        REQUIRE(c.rec.errors.size() == 1);   // reported exactly once
        CHECK(c.rec.errors[0] == k.want);

        // And the stream is over: good frames after a protocol error produce
        // nothing, so a transport that notices one read late has lost nothing.
        c.feed(frame(kOpText, message(9, 100)));
        CHECK(c.rec.errors.size() == 1);
        CHECK(c.pipe.published.empty());
    }
}

TEST_CASE("a frame at the sync ceiling is streamed, not rejected") {
    // The line between "too big for us" and "we have lost our place" is
    // kMaxFramePayload, and only the second kills the socket. A frame that is
    // merely larger than a slot is FrameReassembler's counted drop, and the feed
    // recovers on the next message — Anvil republishes the book every ~80 ms.
    Chain c;
    const std::string huge = message(5, kCap + 1);
    const std::string next = message(6, 200);
    std::vector<std::uint8_t> wire;
    append(wire, frame(kOpText, huge));
    append(wire, frame(kOpText, next));
    c.feed(wire);

    CHECK_FALSE(c.parser.failed());
    CHECK(c.pipe.oversize == 1);
    REQUIRE(c.pipe.published.size() == 1);
    CHECK(c.pipe.published[0] == next);
    CHECK(c.pipe.free_count == kSlots);
    CHECK(kMaxFramePayload > kCap);   // the two thresholds are not the same one
}

TEST_CASE("a new socket starts from nothing, whatever ended the last one") {
    // Both ways a connection can end mid-stream, and the same two calls the
    // transport makes on the next one.
    const std::string msg = message(2, 500);

    SUBCASE("the socket died half way through a frame") {
        Chain c;
        const std::vector<std::uint8_t> partial = frame(kOpText, message(1, 500));
        c.parser.feed(partial.data(), 200, 0);   // 196 payload bytes, then silence
        CHECK(c.pipe.published.empty());
        CHECK(c.re.holding());

        c.parser.reset();
        c.re.reset();
        CHECK(c.pipe.free_count == kSlots);   // the partial message's slot came back

        c.feed(frame(kOpText, msg));
        REQUIRE(c.pipe.published.size() == 1);
        CHECK(c.pipe.published[0] == msg);
    }

    SUBCASE("the stream was rejected and the error is latched") {
        Chain c;
        c.feed({0x81, 0x85, 0, 0, 0, 0, 'h', 'e', 'l', 'l', 'o'});   // masked
        REQUIRE(c.parser.failed());
        c.feed(frame(kOpText, msg));
        CHECK(c.pipe.published.empty());   // nothing is read after a rejection

        c.parser.reset();
        c.re.reset();
        CHECK_FALSE(c.parser.failed());
        CHECK(c.parser.error() == WsProtocolError::None);

        c.feed(frame(kOpText, msg));
        REQUIRE(c.pipe.published.size() == 1);
        CHECK(c.pipe.published[0] == msg);
    }
}

// --- invariant #7 ------------------------------------------------------------

TEST_CASE("the frame layer allocates nothing") {
    // The transport half of invariant #7, proven the same way the engine half is
    // (alloc_probe.hpp): replace global operator new in the test binary and
    // require the count not to move. The handler below stores nothing on the
    // heap for the same reason the real one does not — a chunk is a borrowed
    // span, consumed inside the call.
    struct Counting {
        std::uint32_t chunks = 0, pings = 0, pongs = 0, closes = 0, errors = 0;
        std::uint64_t bytes = 0;
        void on_chunk(const WsChunk& c) {
            ++chunks;
            bytes += c.data_len;
        }
        void on_ping(const std::uint8_t*, std::uint32_t) { ++pings; }
        void on_pong() { ++pongs; }
        void on_close(std::uint16_t, const char*, std::uint32_t) { ++closes; }
        void on_protocol_error(WsProtocolError) { ++errors; }
    };

    std::vector<std::uint8_t> wire;
    for (int i = 1; i <= 20; ++i) {
        append(wire, frame(kOpText, message(i, kBookFrameBytes)));
        if (i % 5 == 0) { append(wire, frame(kOpPing, "ping")); }
    }

    constexpr int kMessages = 20;
    constexpr int kPingsPerPass = kMessages / 5;
    constexpr int kMeasuredPasses = 5;

    Counting sink;
    WsFrameParser<Counting> parser(sink);
    // One warm-up pass, so the window measures steady state rather than first
    // use — and so nothing lazily initialised inside the test harness counts.
    parser.feed(wire.data(), static_cast<std::uint32_t>(wire.size()), 0);

    const std::size_t before = dc::testing::allocation_count();
    for (int pass = 0; pass < kMeasuredPasses; ++pass) {
        for (std::size_t i = 0; i < wire.size(); i += kReadSize) {
            const std::size_t n = std::min(kReadSize, wire.size() - i);
            parser.feed(wire.data() + i, static_cast<std::uint32_t>(n), 0);
        }
    }
    const std::size_t after = dc::testing::allocation_count();

    CHECK(after == before);
    CHECK_FALSE(parser.failed());
    // Pinned rather than bounded: a zero-allocation result over a run that
    // quietly stopped early would be a vacuous pass.
    CHECK(sink.pings == static_cast<std::uint32_t>(kPingsPerPass * (kMeasuredPasses + 1)));
    CHECK(sink.bytes ==
          static_cast<std::uint64_t>(kMeasuredPasses + 1) * kMessages * kBookFrameBytes);
}
