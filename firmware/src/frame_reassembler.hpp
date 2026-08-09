// firmware/src/frame_reassembler.hpp — WebSocket chunks -> whole messages.
//
// WHY THIS IS A SEPARATE, ESP-IDF-FREE HEADER.
//
// Everything else in stage C is either engine code already proven on the host
// (parser, adapter, book, channel) or a thin wrapper over an Espressif API. This
// is the exception: a genuinely new state machine, with enough cases to be worth
// getting wrong — a message split across chunks, a message that will not fit, no
// free slot, a socket that dies mid-message, a server that fragments at the
// WebSocket layer. It is also the only part of the feed path that cannot be
// exercised by the replay harness, because a trace file has no chunk boundaries.
//
// The project's rule is host-first, so this file contains no ESP-IDF, no
// FreeRTOS and no Arduino: it takes chunks as plain values and drives a slot
// pool through a template parameter. On the target that parameter is FramePipe;
// in harness/tests/test_frame_reassembler.cpp it is a fake that records what
// happened. Same code, two callers — the same trick the parse seam uses.
//
// A note on what a "chunk" is. esp_websocket_client delivers a WebSocket frame
// in as many WEBSOCKET_EVENT_DATA events as its RX buffer requires, each
// carrying `payload_offset` (where this piece sits in the frame) and
// `payload_len` (how big the whole frame is). Those two fields are the entire
// contract this class is written against.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace depthcharge::fw {

// WebSocket opcodes (RFC 6455 §5.2).
inline constexpr std::uint8_t kOpContinuation = 0x0;
inline constexpr std::uint8_t kOpText = 0x1;
inline constexpr std::uint8_t kOpBinary = 0x2;
inline constexpr std::uint8_t kOpClose = 0x8;
inline constexpr std::uint8_t kOpPing = 0x9;
inline constexpr std::uint8_t kOpPong = 0xA;

// One WEBSOCKET_EVENT_DATA, reduced to the fields that matter.
struct WsChunk {
    std::uint8_t op_code = kOpText;
    std::uint32_t payload_offset = 0;  // offset of this piece within the frame
    std::uint32_t payload_len = 0;     // total size of the frame
    const char* data = nullptr;
    std::uint32_t data_len = 0;
};

// `Slots` must provide:
//     bool  acquire(std::uint8_t&)         non-blocking; false when none free
//     char* buffer(std::uint8_t)           writable bytes of an owned slot
//     void  release(std::uint8_t)          give a slot back unpublished
//     bool  publish(std::uint8_t, uint32)  hand a complete message on
//     void  count_oversize() / count_abandoned()
//     void  count_continuation() / count_control()
// plus a static/consteval capacity, passed in as `capacity` so the test can use
// a small one and the target the real 16 KiB.
template <typename Slots>
class FrameReassembler {
public:
    FrameReassembler(Slots& slots, std::size_t capacity) noexcept
        : slots_(slots), capacity_(capacity) {}

    // Feed one chunk. Never blocks, never allocates; publishes through `Slots`
    // when a message completes.
    void on_chunk(const WsChunk& c) noexcept {
        if (c.op_code == kOpClose) {
            // The disconnect that follows does the real work; nothing to
            // reassemble here.
            return;
        }
        if (c.op_code != kOpText && c.op_code != kOpBinary &&
            c.op_code != kOpContinuation) {
            // Ping/pong. Deliberately not treated as data: a live socket with a
            // dead feed is exactly what the RX watchdog must still grey for.
            slots_.count_control();
            return;
        }

        // A WebSocket-level continuation frame — a message the *server* split,
        // as opposed to one this client received in pieces — restarts
        // payload_offset at zero, and this IDF vintage does not surface the FIN
        // bit, so there is no way to know where such a message truly ends.
        //
        // Be precise about what happens then, because it is a limitation and
        // not a solution: the continuation's bytes are appended to whatever is
        // already in the slot, and the message is published at that fragment's
        // end anyway, because `complete()` can only see the fragment. The parser
        // then rejects the incomplete JSON and the adapter counts a parse error.
        // That is the correct failure — a dropped frame Anvil re-sends in ~80 ms
        // — rather than the firmware inventing a message boundary it cannot
        // know. Anvil has never sent one (all 2,694 captured frames are whole
        // text frames); the counter exists so that if it ever starts, the bench
        // log says so instead of the ladder merely misbehaving.
        const bool server_fragment = (c.op_code == kOpContinuation && c.payload_offset == 0);
        if (server_fragment) { slots_.count_continuation(); }

        // Counted whether or not this message ends up stored, so `chunks /
        // messages` reports how many DATA events the client really needed per
        // frame. That number is the direct evidence that multi-chunk reassembly
        // is being exercised on the wire rather than only in the host test: at a
        // 4 KiB RX buffer an ~8 KB Anvil frame should read close to 3.
        slots_.count_chunk();

        const bool starts_message = (c.op_code != kOpContinuation) && (c.payload_offset == 0);
        if (starts_message) { begin_message(); }

        // For a chunked single frame `payload_offset` walks the message; for an
        // appended server fragment it restarts, so write wherever we got to.
        const std::uint32_t at = (c.op_code == kOpContinuation) ? fill_ : c.payload_offset;
        write(at, c.data, c.data_len);

        if (complete(c)) { finish(); }
    }

    // The socket went away: whatever partial message we held is gone with it.
    void reset() noexcept {
        drop_held();
        dropping_ = false;
        fill_ = 0;
    }

    // Exposed for tests and for asserting the invariant that a completed message
    // always leaves us holding nothing.
    bool holding() const noexcept { return holding_; }
    std::uint32_t fill() const noexcept { return fill_; }

private:
    void begin_message() noexcept {
        if (holding_) {
            // A new message began while a partial one was still open. Should not
            // happen on a well-behaved stream; counted, not assumed away.
            slots_.count_abandoned();
            drop_held();
        }
        dropping_ = false;
        fill_ = 0;
        holding_ = slots_.acquire(slot_);
        // No free slot: drop this whole message rather than block a callback
        // that is reading the socket. Anvil re-sends the book in ~80 ms.
        dropping_ = !holding_;
    }

    void write(std::uint32_t at, const char* data, std::uint32_t len) noexcept {
        if (!holding_ || dropping_) { return; }
        if (static_cast<std::size_t>(at) + len > capacity_) {
            // A defined drop, counted — never a reallocation, and never a
            // truncated frame handed to the parser as if it were whole.
            slots_.count_oversize();
            drop_held();
            dropping_ = true;
            return;
        }
        if (len != 0 && data != nullptr) {
            std::memcpy(slots_.buffer(slot_) + at, data, len);
            fill_ = at + len;
        }
    }

    static bool complete(const WsChunk& c) noexcept {
        // `payload_len` is the total for this WebSocket frame, so the last piece
        // of it satisfies this. A zero-length frame is complete immediately and
        // is filtered out by the fill_ check in finish().
        return static_cast<std::size_t>(c.payload_offset) + c.data_len >= c.payload_len;
    }

    void finish() noexcept {
        if (holding_ && !dropping_ && fill_ > 0) {
            (void)slots_.publish(slot_, fill_);
            holding_ = false;
        } else {
            drop_held();
        }
        dropping_ = false;
        fill_ = 0;
    }

    void drop_held() noexcept {
        if (holding_) {
            slots_.release(slot_);
            holding_ = false;
        }
    }

    Slots& slots_;
    std::size_t capacity_;

    bool holding_ = false;   // we own slot_ and are filling it
    bool dropping_ = false;  // this message is being discarded; run to its end
    std::uint8_t slot_ = 0;
    std::uint32_t fill_ = 0;
};

}  // namespace depthcharge::fw
