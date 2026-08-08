// test_frame_reassembler.cpp — the firmware's WebSocket reassembly, on the desk.
//
// This is the only piece of M3 stage C that is new logic rather than engine code
// or a thin wrapper over an Espressif call, and it is also the piece a replay
// trace cannot exercise: a trace file has no chunk boundaries, because the
// capture tool's WebSocket library reassembled them before writing the line.
//
// So it is tested here instead, against the contract esp_websocket_client
// documents — each WEBSOCKET_EVENT_DATA carries `payload_offset` (where this
// piece sits within the frame) and `payload_len` (how big the whole frame is).
// The fake slot pool below records exactly what the real FramePipe would have
// been asked to do, so a failure names the wrong call rather than a wrong pixel.
//
// The cases that matter are the ones the bench cannot easily produce on demand:
// a message that will not fit, both slots busy, a socket dying mid-message, and
// a server that fragments at the WebSocket layer. Getting those wrong shows up
// on the panel as a ladder that stops updating, which is the failure hardest to
// diagnose from across a room.
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "frame_reassembler.hpp"

using depthcharge::fw::FrameReassembler;
using depthcharge::fw::kOpBinary;
using depthcharge::fw::kOpClose;
using depthcharge::fw::kOpContinuation;
using depthcharge::fw::kOpPing;
using depthcharge::fw::kOpPong;
using depthcharge::fw::kOpText;
using depthcharge::fw::WsChunk;

namespace {

// Small on purpose: a 64-byte capacity makes the oversize boundary reachable in
// a readable test, and the reassembler takes the capacity as a constructor
// argument precisely so this is possible.
constexpr std::size_t kCap = 64;
constexpr std::uint8_t kSlots = 2;

// Stands in for FramePipe. Records every published message and every counter,
// and — the point of a fake rather than a mock — actually models slot ownership,
// so a double-release or a leaked slot shows up as a wrong free count.
struct FakeSlots {
    char storage[kSlots][kCap]{};
    bool in_flight[kSlots]{};
    std::uint8_t free_count = kSlots;

    std::vector<std::string> published;
    std::uint32_t oversize = 0;
    std::uint32_t abandoned = 0;
    std::uint32_t continuation = 0;
    std::uint32_t control = 0;
    std::uint32_t acquire_failures = 0;

    bool acquire(std::uint8_t& slot) noexcept {
        for (std::uint8_t i = 0; i < kSlots; ++i) {
            if (!in_flight[i]) {
                in_flight[i] = true;
                --free_count;
                slot = i;
                return true;
            }
        }
        ++acquire_failures;
        return false;
    }
    char* buffer(std::uint8_t slot) noexcept { return storage[slot]; }
    void release(std::uint8_t slot) noexcept {
        REQUIRE(in_flight[slot]);   // a double release is a bug, not a no-op
        in_flight[slot] = false;
        ++free_count;
    }
    bool publish(std::uint8_t slot, std::uint32_t len) noexcept {
        REQUIRE(in_flight[slot]);
        published.emplace_back(storage[slot], len);
        in_flight[slot] = false;
        ++free_count;
        return true;
    }
    void count_oversize() noexcept { ++oversize; }
    void count_abandoned() noexcept { ++abandoned; }
    void count_continuation() noexcept { ++continuation; }
    void count_control() noexcept { ++control; }
};

struct Fixture {
    FakeSlots slots;
    FrameReassembler<FakeSlots> re{slots, kCap};

    // Deliver `text` as a single WebSocket frame split into `chunk` byte pieces,
    // exactly as esp_websocket_client would with that RX buffer size.
    void deliver(const std::string& text, std::size_t chunk, std::uint8_t op = kOpText) {
        const std::uint32_t total = static_cast<std::uint32_t>(text.size());
        if (total == 0) {
            re.on_chunk(WsChunk{op, 0, 0, text.data(), 0});
            return;
        }
        for (std::size_t off = 0; off < text.size(); off += chunk) {
            const std::size_t n = (off + chunk <= text.size()) ? chunk : text.size() - off;
            re.on_chunk(WsChunk{op, static_cast<std::uint32_t>(off), total, text.data() + off,
                                static_cast<std::uint32_t>(n)});
        }
    }
};

}  // namespace

TEST_CASE("a whole message in one chunk is published verbatim") {
    Fixture f;
    f.deliver("{\"type\":\"book\"}", 64);

    REQUIRE(f.slots.published.size() == 1);
    CHECK(f.slots.published[0] == "{\"type\":\"book\"}");
    CHECK(f.slots.free_count == kSlots);   // the slot came back
    CHECK_FALSE(f.re.holding());
}

TEST_CASE("a message split across chunks is reassembled in order") {
    // The normal path on the target: an 8.7 KB book frame through a 4 KiB RX
    // buffer arrives in three pieces.
    Fixture f;
    const std::string msg = "0123456789abcdefghijklmnopqrstuvwxyz";
    f.deliver(msg, 7);

    REQUIRE(f.slots.published.size() == 1);
    CHECK(f.slots.published[0] == msg);
    CHECK(f.slots.free_count == kSlots);
}

TEST_CASE("chunk size does not change the result, at any boundary") {
    // Including the sizes where a chunk lands exactly on the end of the message,
    // which is where an off-by-one in the completion test would hide.
    const std::string msg(37, 'x');
    for (std::size_t chunk = 1; chunk <= 40; ++chunk) {
        INFO("chunk size ", chunk);
        Fixture f;
        f.deliver(msg, chunk);
        REQUIRE(f.slots.published.size() == 1);
        CHECK(f.slots.published[0] == msg);
        CHECK(f.slots.free_count == kSlots);
    }
}

TEST_CASE("back-to-back messages each get their own slot and give it back") {
    Fixture f;
    for (int i = 0; i < 20; ++i) {
        f.deliver(std::string("msg") + static_cast<char>('a' + (i % 26)), 2);
    }
    CHECK(f.slots.published.size() == 20);
    CHECK(f.slots.free_count == kSlots);
    CHECK(f.slots.abandoned == 0);
    CHECK(f.slots.oversize == 0);
}

// --- the cases the bench cannot produce on demand ---------------------------

TEST_CASE("a message larger than the buffer is a counted drop, not a truncation") {
    Fixture f;
    const std::string big(kCap + 1, 'y');
    f.deliver(big, 16);

    CHECK(f.slots.published.empty());   // never a partial frame to the parser
    CHECK(f.slots.oversize == 1);
    CHECK(f.slots.free_count == kSlots);  // and the slot is not leaked
    CHECK_FALSE(f.re.holding());
}

TEST_CASE("exactly the buffer size still fits") {
    Fixture f;
    const std::string exact(kCap, 'z');
    f.deliver(exact, 16);

    REQUIRE(f.slots.published.size() == 1);
    CHECK(f.slots.published[0].size() == kCap);
    CHECK(f.slots.oversize == 0);
}

TEST_CASE("the feed recovers on the very next message after an oversize drop") {
    // The whole justification for a defined drop is that Anvil re-sends the book
    // in ~80 ms. That only holds if the drop leaves no residue.
    Fixture f;
    f.deliver(std::string(kCap + 8, 'y'), 16);
    f.deliver("{\"ok\":1}", 16);

    REQUIRE(f.slots.published.size() == 1);
    CHECK(f.slots.published[0] == "{\"ok\":1}");
    CHECK(f.slots.oversize == 1);
}

TEST_CASE("the reassembler cannot starve itself of slots") {
    // Worth pinning, because it is the reason a slot shortage can only ever come
    // from the feed task being slow and never from the reassembler's own
    // book-keeping: beginning a message releases whatever it was holding BEFORE
    // it acquires, so a stream of never-completed messages cycles one slot
    // forever rather than consuming the pool.
    FakeSlots slots;
    FrameReassembler<FakeSlots> re{slots, kCap};

    for (int i = 0; i < 50; ++i) {
        re.on_chunk(WsChunk{kOpText, 0, 1000, "aa", 2});   // starts, never finishes
    }
    CHECK(slots.abandoned == 49);
    CHECK(slots.free_count == kSlots - 1);   // exactly one held, not fifty
    CHECK(slots.acquire_failures == 0);
}

TEST_CASE("with every slot held elsewhere the message is dropped whole") {
    FakeSlots slots;
    FrameReassembler<FakeSlots> re{slots, kCap};

    // The real shortage: the feed task is behind, so both slots are in flight
    // and the reassembler is holding none of them.
    std::uint8_t a = 0;
    std::uint8_t b = 0;
    REQUIRE(slots.acquire(a));
    REQUIRE(slots.acquire(b));
    CHECK(slots.free_count == 0);

    re.on_chunk(WsChunk{kOpText, 0, 4, "cccc", 4});
    CHECK(slots.acquire_failures == 1);
    CHECK(slots.published.empty());
    CHECK_FALSE(re.holding());

    // ...and the drop leaves no residue: the moment a slot frees up, the next
    // message goes through. This is what makes it a one-refresh cost.
    slots.release(a);
    re.on_chunk(WsChunk{kOpText, 0, 6, "dddddd", 6});
    REQUIRE(slots.published.size() == 1);
    CHECK(slots.published[0] == "dddddd");
}

TEST_CASE("a socket that dies mid-message returns the slot") {
    Fixture f;
    f.re.on_chunk(WsChunk{kOpText, 0, 100, "half", 4});
    CHECK(f.re.holding());
    CHECK(f.slots.free_count == kSlots - 1);

    f.re.reset();   // what WEBSOCKET_EVENT_DISCONNECTED does

    CHECK_FALSE(f.re.holding());
    CHECK(f.slots.free_count == kSlots);
    CHECK(f.slots.published.empty());

    // ...and the next connection starts clean.
    f.deliver("{\"fresh\":1}", 4);
    REQUIRE(f.slots.published.size() == 1);
    CHECK(f.slots.published[0] == "{\"fresh\":1}");
}

TEST_CASE("a new message beginning mid-message abandons the old one and is counted") {
    Fixture f;
    f.re.on_chunk(WsChunk{kOpText, 0, 100, "orphan", 6});
    f.deliver("{\"good\":1}", 32);

    CHECK(f.slots.abandoned == 1);
    REQUIRE(f.slots.published.size() == 1);
    CHECK(f.slots.published[0] == "{\"good\":1}");
    CHECK(f.slots.free_count == kSlots);
}

TEST_CASE("ping and pong are counted, never treated as data, and never disturb a message") {
    Fixture f;
    f.re.on_chunk(WsChunk{kOpText, 0, 8, "12345", 5});   // message in progress
    f.re.on_chunk(WsChunk{kOpPing, 0, 0, nullptr, 0});
    f.re.on_chunk(WsChunk{kOpPong, 0, 0, nullptr, 0});
    f.re.on_chunk(WsChunk{kOpText, 5, 8, "678", 3});

    CHECK(f.slots.control == 2);
    REQUIRE(f.slots.published.size() == 1);
    CHECK(f.slots.published[0] == "12345678");
}

TEST_CASE("a close frame is ignored here; the disconnect path does the work") {
    Fixture f;
    f.re.on_chunk(WsChunk{kOpClose, 0, 2, "\x03\xe8", 2});
    CHECK(f.slots.published.empty());
    CHECK(f.slots.control == 0);   // close is not a control *count*, it is a no-op
    CHECK(f.slots.free_count == kSlots);
}

TEST_CASE("a zero-length frame publishes nothing and leaks nothing") {
    Fixture f;
    f.re.on_chunk(WsChunk{kOpText, 0, 0, "", 0});
    CHECK(f.slots.published.empty());
    CHECK(f.slots.free_count == kSlots);
    CHECK_FALSE(f.re.holding());
}

TEST_CASE("a binary frame is reassembled like a text one") {
    // Anvil sends text, but the opcode is the server's choice and a decoder that
    // silently ignored binary would stall the ladder with no counter moving.
    Fixture f;
    f.deliver("{\"binary\":true}", 4, kOpBinary);
    REQUIRE(f.slots.published.size() == 1);
    CHECK(f.slots.published[0] == "{\"binary\":true}");
}

TEST_CASE("a server-fragmented message is counted so the assumption is observable") {
    // Anvil has never done this — the counter exists so that if it ever starts,
    // the bench log says so rather than the ladder just misbehaving.
    Fixture f;
    f.re.on_chunk(WsChunk{kOpText, 0, 4, "abcd", 4});          // fragment 1, "complete"
    f.re.on_chunk(WsChunk{kOpContinuation, 0, 4, "efgh", 4});  // server continuation

    CHECK(f.slots.continuation == 1);
    // The first fragment published on its own; that is the documented
    // limitation (no FIN bit in this IDF vintage), and the parser rejects the
    // partial JSON rather than the firmware inventing a message boundary.
    CHECK(f.slots.published.size() >= 1);
    CHECK(f.slots.published[0] == "abcd");
    CHECK(f.slots.free_count == kSlots);
}

TEST_CASE("no sequence of chunks ever leaks a slot") {
    // The property that actually matters: whatever the stream does, the pool
    // must come back whole, or the feed dies silently a few messages later.
    // A small deterministic pseudo-random walk over every chunk shape.
    FakeSlots slots;
    FrameReassembler<FakeSlots> re{slots, kCap};

    std::uint32_t rng = 12345;
    const auto next = [&rng](std::uint32_t n) {
        rng = rng * 1103515245u + 12345u;
        return (rng >> 16) % n;
    };

    const char* payload = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < 5000; ++i) {
        switch (next(6)) {
            case 0: re.on_chunk(WsChunk{kOpText, 0, next(90), payload, next(20)}); break;
            case 1: re.on_chunk(WsChunk{kOpText, next(40), next(90), payload, next(20)}); break;
            case 2: re.on_chunk(WsChunk{kOpContinuation, next(40), next(90), payload, next(20)});
                break;
            case 3: re.on_chunk(WsChunk{kOpPing, 0, 0, nullptr, 0}); break;
            case 4: re.reset(); break;
            default: re.on_chunk(WsChunk{kOpText, 0, 0, "", 0}); break;
        }
    }
    re.reset();

    CHECK(slots.free_count == kSlots);
    CHECK_FALSE(re.holding());
}
