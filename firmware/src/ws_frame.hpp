// firmware/src/ws_frame.hpp — the WebSocket frame layer, ours, and on the desk.
//
// WHY THIS FILE EXISTS AT ALL.
//
// Until 2026-08-13 this layer was `esp_websocket_client`'s, and the bench
// convicted it: 17 of 17 captured socket deaths carried a stale `errno=119`
// (EINPROGRESS — the failing read set no socket error at all), half arrived via
// the library's *clean-close* path, and 85 co-timed parse rejects carried the
// `SPLIT@1` signature — exactly one stray byte of the previous message glued
// ahead of clean JSON, post-decryption, which is an off-by-one in its
// payload-offset accounting caught red-handed. TLS's record MAC guarantees the
// bytes on the wire were intact, so the mangling happened inside that library's
// buffer path. Meanwhile a minimal owned client of exactly this shape held one
// socket for 3.7 h / 534 MB / zero deaths pinned to the worst mesh node in the
// house (`hardware/bench-2026-08-13-wifi-drop-diagnosis.md`; the prototype is
// `firmware/diag/link_autopsy.cpp`).
//
// So the framing is ours now. And because it is ours, it is testable: this
// header contains no ESP-IDF, no FreeRTOS and no Arduino, exactly like
// frame_reassembler.hpp, ws_supervisor.hpp and gap_histogram.hpp before it. The
// bytes come in as a plain span and everything goes out through a template
// parameter, so `harness/tests/test_ws_frame.cpp` drives the identical code the
// board runs — including the case that convicted the library, which is a
// regression test here rather than a hope.
//
// WHERE THE SEAM IS.
//
//   esp_tls read -> WsFrameParser -> WsChunk -> FrameReassembler -> FramePipe
//
// `WsChunk` is deliberately unchanged from what esp_websocket_client's DATA
// event supplied: `payload_offset` (where this piece sits in the frame) and
// `payload_len` (how big the whole frame is). That is what let FrameReassembler
// keep its shape — one new field and one new completion rule (both below), not
// a rewrite — and kept its existing host tests green while gaining
// fragmentation cases. The difference is not the shape of the contract, it is
// that those two numbers are now computed here, by code the host suite can
// interrogate byte by byte.
//
// One field is new: `WsChunk::fin`. The old vintage did not surface the FIN bit,
// so the reassembler could not know where a *server-fragmented* message ended
// and published each fragment on its own — a documented limitation with a
// counter attached. We read FIN off the wire ourselves, so the limitation's
// stated cause is gone and it goes with it.
//
// WHAT THIS LAYER DOES NOT DO.
//
// It does not buffer data payloads: a chunk points straight into the caller's
// read buffer and is valid only for the duration of the `on_chunk` call — the
// same borrowed-span rule `LevelSpan` already carries across the engine
// boundary (ARCHITECTURE §4). It does not allocate, ever (invariant #7), and
// the only storage it owns is 125 bytes for control payloads, which RFC 6455
// §5.5 caps at exactly that. It does not send: pongs are the transport's to
// write, because writing is a platform call and this header has none.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "frame_reassembler.hpp"

namespace depthcharge::fw {

// RFC 6455 §5.5: "All control frames MUST have a payload length of 125 bytes or
// less and MUST NOT be fragmented." Both halves are enforced below, which is
// what bounds the one buffer this class owns.
inline constexpr std::uint32_t kMaxControlPayload = 125;

// The largest data frame this parser will accept before it declares the byte
// stream lost.
//
// It is not a capacity — FrameReassembler already drops anything past
// kFrameCapacity (16 KiB) as a counted `oversize` and keeps the socket, which is
// the right answer for a frame that is merely too big. This is the *sync*
// threshold, and the distinction matters. The largest message Anvil has ever
// sent across 2,694 captured frames is 8,726 bytes; a length field claiming
// more than 1 MiB is not a deep book, it is this parser having lost its place in
// the stream, and there is nothing to do about that but reconnect. Failing here
// is therefore not conservatism — a desynced parser cannot resynchronise by
// reading further, so continuing would be the actively worse choice.
//
// It also keeps every uint32 in WsChunk exact: payload_offset + data_len can
// never exceed payload_len, which can never exceed this.
inline constexpr std::uint64_t kMaxFramePayload = 1024u * 1024u;

// RFC 6455 §7.1.5: a close frame with no payload means "no status code was
// present", and 1005 is the code an application is told to use for it. It is
// never sent on the wire. link_autopsy.cpp reports the same number for the same
// case, so two instruments agree about an empty close.
inline constexpr std::uint16_t kCloseNoStatus = 1005;

// Every way the byte stream can stop being a WebSocket stream. Each one means
// the same thing operationally — the parser has lost sync and the socket must
// go — so the enum exists to put a name on the *bench line*, not to branch on.
// A death with `ws=bad-opcode` beside it is a different investigation from one
// with `ws=masked-frame`, and the whole point of owning this layer is that the
// label exists at all.
enum class WsProtocolError : std::uint8_t {
    None = 0,
    ReservedBits,            // RSV1/2/3 set; no extension was negotiated
    MaskedFrame,             // server->client frames must not be masked (§5.1)
    BadOpcode,               // a reserved opcode: 0x3-0x7, 0xB-0xF
    FragmentedControl,       // control frame with FIN clear (§5.5)
    ControlTooLong,          // control payload > 125 (§5.5)
    LengthTooLarge,          // past kMaxFramePayload — see above
    UnexpectedContinuation,  // continuation with no message open (§5.4)
    InterleavedFrame,        // a new data frame inside a fragmented message (§5.4)
};

inline const char* ws_error_name(WsProtocolError e) noexcept {
    switch (e) {
        case WsProtocolError::None: return "none";
        case WsProtocolError::ReservedBits: return "rsv-bits";
        case WsProtocolError::MaskedFrame: return "masked-frame";
        case WsProtocolError::BadOpcode: return "bad-opcode";
        case WsProtocolError::FragmentedControl: return "fragmented-control";
        case WsProtocolError::ControlTooLong: return "control-too-long";
        case WsProtocolError::LengthTooLarge: return "length-too-large";
        case WsProtocolError::UnexpectedContinuation: return "stray-continuation";
        case WsProtocolError::InterleavedFrame: return "interleaved-frame";
    }
    return "?";
}

inline constexpr bool ws_is_control(std::uint8_t opcode) noexcept { return (opcode & 0x08u) != 0; }

// `Handler` must provide:
//     void on_chunk(const WsChunk&)      a piece of a data frame's payload,
//                                        borrowed for the duration of the call.
//                                        FrameReassembler satisfies this as-is.
//     void on_ping(const std::uint8_t* payload, std::uint32_t len)
//                                        a COMPLETE ping payload (<= 125 bytes,
//                                        possibly zero) — the transport owes a
//                                        pong carrying exactly these bytes.
//     void on_pong()                     a pong arrived; nothing is owed.
//     void on_close(std::uint16_t code, const char* reason, std::uint32_t reason_len)
//                                        the server said goodbye and, usually,
//                                        why. `reason` is NOT NUL-terminated and
//                                        does not outlive the call.
//     void on_protocol_error(WsProtocolError)
//                                        the stream is no longer a WebSocket
//                                        stream. Kill the socket; feeding more
//                                        bytes does nothing (see feed()).
template <typename Handler>
class WsFrameParser {
public:
    explicit WsFrameParser(Handler& handler) noexcept : handler_(handler) {}

    // Feed one socket read. `arrival_us` is stamped by the caller from the
    // monotonic clock as the bytes land, and rides out on every chunk this read
    // produces — same rule, and same reason, as WsChunk::arrival_us has always
    // had: a clock is a platform call and this header has none, so the host test
    // drives a synthetic timeline instead of a real one.
    //
    // Returns nothing on purpose. A caller that wants to know whether the stream
    // survived asks `failed()`, which is a state and not an event — after a
    // protocol error every subsequent byte is ignored, so a transport that
    // notices one read late has still lost nothing.
    void feed(const std::uint8_t* data, std::uint32_t len, std::int64_t arrival_us) noexcept {
        if (data == nullptr) { return; }
        std::uint32_t i = 0;
        while (i < len && err_ == WsProtocolError::None) {
            switch (state_) {
                case State::Op: {
                    read_op(data[i]);
                    ++i;
                    break;
                }
                case State::Len: {
                    const std::uint8_t b = data[i];
                    ++i;
                    // `data + i` is where this frame's payload starts if the
                    // header ends here — one past the end is legal to form and
                    // is exactly the pointer a zero-length frame's empty chunk
                    // wants.
                    read_len(b, data + i, arrival_us);
                    break;
                }
                case State::Ext: {
                    const std::uint8_t b = data[i];
                    ++i;
                    read_ext(b, data + i, arrival_us);
                    break;
                }
                case State::Payload:
                    read_payload(data, len, i, arrival_us);
                    break;
            }
        }
    }

    // A new socket: forget everything, including a latched protocol error and
    // the frame counts. The partial frame we were holding died with the
    // connection, and so did the reason the stream was rejected.
    //
    // The counters go too, and that is not a detail: they are read by the death
    // autopsy, where every other number on the line — lifetime, bytes, close
    // code — is about THIS socket. A cumulative frame count printed beside a
    // per-socket byte count is a line that reads as one thing and means another.
    void reset() noexcept {
        state_ = State::Op;
        err_ = WsProtocolError::None;
        opcode_ = kOpText;
        fin_ = true;
        remaining_ = 0;
        ext_need_ = 0;
        ext_acc_ = 0;
        payload_len_ = 0;
        delivered_ = 0;
        ctrl_len_ = 0;
        message_open_ = false;
        data_frames_ = 0;
        control_frames_ = 0;
    }

    WsProtocolError error() const noexcept { return err_; }
    bool failed() const noexcept { return err_ != WsProtocolError::None; }

    // Diagnostics for the death autopsy: what this socket managed before it
    // ended. Frame counts separate "the feed stopped" from "the feed was never
    // there". Bytes are deliberately NOT counted here — the transport counts
    // what it read, and the same quantity kept in two places is the same
    // quantity waiting to disagree.
    std::uint32_t data_frames() const noexcept { return data_frames_; }
    std::uint32_t control_frames() const noexcept { return control_frames_; }
    // True between the first fragment of a server-fragmented message and its
    // FIN. Exposed for the tests and for a bench line: Anvil has never sent one.
    bool message_open() const noexcept { return message_open_; }

private:
    enum class State : std::uint8_t {
        Op,       // first header byte: FIN, RSV, opcode
        Len,      // second header byte: MASK, 7-bit length
        Ext,      // 16- or 64-bit extended length, big-endian
        Payload,  // body, streamed
    };

    void fail(WsProtocolError e) noexcept {
        err_ = e;
        handler_.on_protocol_error(e);
    }

    void read_op(std::uint8_t b) noexcept {
        // RSV1/2/3. We negotiate no extensions in the upgrade, so any of them set
        // means either a server we do not understand or bytes we have misplaced.
        if ((b & 0x70u) != 0) {
            fail(WsProtocolError::ReservedBits);
            return;
        }
        fin_ = (b & 0x80u) != 0;
        opcode_ = static_cast<std::uint8_t>(b & 0x0Fu);
        switch (opcode_) {
            case kOpContinuation:
            case kOpText:
            case kOpBinary:
            case kOpClose:
            case kOpPing:
            case kOpPong:
                break;
            default:
                fail(WsProtocolError::BadOpcode);
                return;
        }
        if (ws_is_control(opcode_) && !fin_) {
            fail(WsProtocolError::FragmentedControl);
            return;
        }
        state_ = State::Len;
    }

    void read_len(std::uint8_t b, const std::uint8_t* payload_at, std::int64_t arrival_us) noexcept {
        // A masked server->client frame is a protocol violation (§5.1) and, more
        // usefully here, is what a lost byte looks like: the high bit of an
        // arbitrary payload byte is set half the time, so this check catches
        // desync fast instead of letting it become a 4 GB length field.
        if ((b & 0x80u) != 0) {
            fail(WsProtocolError::MaskedFrame);
            return;
        }
        const std::uint8_t len7 = static_cast<std::uint8_t>(b & 0x7Fu);
        if (len7 < 126) {
            remaining_ = len7;
            begin_payload(payload_at, arrival_us);
            return;
        }
        // Non-minimal encodings (a 16-bit form carrying 5, say) are accepted.
        // §5.2 says a sender must use the shortest form, but rejecting one would
        // add a novel way for this firmware to drop a socket in exchange for
        // catching nothing the length ceiling does not already catch.
        ext_need_ = (len7 == 126) ? 2u : 8u;
        ext_acc_ = 0;
        state_ = State::Ext;
    }

    void read_ext(std::uint8_t b, const std::uint8_t* payload_at, std::int64_t arrival_us) noexcept {
        ext_acc_ = (ext_acc_ << 8) | static_cast<std::uint64_t>(b);
        if (--ext_need_ == 0) {
            remaining_ = ext_acc_;
            begin_payload(payload_at, arrival_us);
        }
    }

    // The header is complete: validate the length against what this frame kind
    // is allowed to be, settle the fragmentation bookkeeping, and either enter
    // the payload or finish the frame where it stands.
    void begin_payload(const std::uint8_t* payload_at, std::int64_t arrival_us) noexcept {
        if (ws_is_control(opcode_)) {
            if (remaining_ > kMaxControlPayload) {
                fail(WsProtocolError::ControlTooLong);
                return;
            }
            ctrl_len_ = 0;
        } else {
            if (remaining_ > kMaxFramePayload) {
                fail(WsProtocolError::LengthTooLarge);
                return;
            }
            // §5.4's two ordering rules, and they are worth enforcing rather
            // than absorbing: a continuation with nothing open, or a fresh data
            // frame inside an open message, both mean the frame boundaries we
            // are reading are not the ones the server wrote. Absorbing either
            // would hand FrameReassembler a message assembled out of two
            // different ones — which is precisely the failure this milestone
            // exists to remove, re-implemented by us.
            if (opcode_ == kOpContinuation) {
                if (!message_open_) {
                    fail(WsProtocolError::UnexpectedContinuation);
                    return;
                }
            } else if (message_open_) {
                fail(WsProtocolError::InterleavedFrame);
                return;
            }
            payload_len_ = static_cast<std::uint32_t>(remaining_);
            delivered_ = 0;
        }

        if (remaining_ > 0) {
            state_ = State::Payload;
            return;
        }
        // A frame with no body. For a data frame that still means one chunk, so
        // that the reassembler sees the message begin and end exactly as it
        // would have under the old client (its "a zero-length frame publishes
        // nothing and leaks nothing" test pins the behaviour); for a control
        // frame it is the common spelling of a ping, which must still be ponged.
        if (!ws_is_control(opcode_)) { emit(payload_at, 0, arrival_us); }
        end_frame();
    }

    void read_payload(const std::uint8_t* data, std::uint32_t len, std::uint32_t& i,
                      std::int64_t arrival_us) noexcept {
        const std::uint32_t avail = len - i;
        // remaining_ is 64-bit; the min with a uint32 makes `take` exact.
        const std::uint32_t take =
            (remaining_ < static_cast<std::uint64_t>(avail)) ? static_cast<std::uint32_t>(remaining_)
                                                             : avail;
        if (ws_is_control(opcode_)) {
            // Bounded by the header check: remaining_ <= 125 and ctrl_len_ only
            // ever accumulates this frame's own payload.
            std::memcpy(ctrl_ + ctrl_len_, data + i, take);
            ctrl_len_ += take;
        } else {
            emit(data + i, take, arrival_us);
        }
        i += take;
        remaining_ -= take;
        if (remaining_ == 0) { end_frame(); }
    }

    void emit(const std::uint8_t* at, std::uint32_t len, std::int64_t arrival_us) noexcept {
        WsChunk c;
        c.op_code = opcode_;
        c.payload_offset = delivered_;
        c.payload_len = payload_len_;
        c.data = reinterpret_cast<const char*>(at);
        c.data_len = len;
        c.arrival_us = arrival_us;
        c.fin = fin_;
        handler_.on_chunk(c);
        delivered_ += len;
    }

    void end_frame() noexcept {
        switch (opcode_) {
            case kOpPing:
                ++control_frames_;
                handler_.on_ping(ctrl_, ctrl_len_);
                break;
            case kOpPong:
                ++control_frames_;
                handler_.on_pong();
                break;
            case kOpClose: {
                ++control_frames_;
                // §5.5.1: an empty payload is "no status"; two bytes are the
                // code; anything after them is UTF-8 reason text. A one-byte
                // payload is illegal and is reported as no-status rather than as
                // its own error class, because our response to a close is
                // identical either way — say what came, then reconnect.
                std::uint16_t code = kCloseNoStatus;
                const char* reason = reinterpret_cast<const char*>(ctrl_);
                std::uint32_t reason_len = 0;
                if (ctrl_len_ >= 2) {
                    code = static_cast<std::uint16_t>((static_cast<std::uint16_t>(ctrl_[0]) << 8) |
                                                      ctrl_[1]);
                    reason = reinterpret_cast<const char*>(ctrl_ + 2);
                    reason_len = ctrl_len_ - 2u;
                }
                handler_.on_close(code, reason, reason_len);
                break;
            }
            default:
                ++data_frames_;
                // The message stays open across every fragment until one arrives
                // with FIN set. This is the bit the old vintage never showed us.
                message_open_ = !fin_;
                break;
        }
        ctrl_len_ = 0;
        state_ = State::Op;
    }

    Handler& handler_;

    State state_ = State::Op;
    WsProtocolError err_ = WsProtocolError::None;

    bool fin_ = true;
    std::uint8_t opcode_ = kOpText;
    std::uint64_t remaining_ = 0;   // payload bytes still to come in this frame
    std::uint8_t ext_need_ = 0;     // extended-length bytes still to read
    std::uint64_t ext_acc_ = 0;
    std::uint32_t payload_len_ = 0;  // this frame's total payload (data frames)
    std::uint32_t delivered_ = 0;    // of it, how much has been handed on

    // The only storage this class owns, and RFC 6455 §5.5 is what sizes it.
    std::uint8_t ctrl_[kMaxControlPayload] = {};
    std::uint32_t ctrl_len_ = 0;

    bool message_open_ = false;

    std::uint32_t data_frames_ = 0;
    std::uint32_t control_frames_ = 0;
};

}  // namespace depthcharge::fw
