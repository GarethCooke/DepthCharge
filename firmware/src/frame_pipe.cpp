// firmware/src/frame_pipe.cpp — see frame_pipe.hpp for why this exists.
#include "frame_pipe.hpp"

namespace depthcharge::fw {

bool FramePipe::begin() noexcept {
    free_q_ = xQueueCreate(kFrameSlots, sizeof(std::uint8_t));
    // Deep enough that a Connected/Disconnected can always be posted even with
    // every slot in flight: a status event that failed to enqueue would be an
    // outage the book never hears about, which is the one failure invariant #5
    // does not tolerate.
    ready_q_ = xQueueCreate(kFrameSlots + 2, sizeof(FeedMessage));
    if (free_q_ == nullptr || ready_q_ == nullptr) { return false; }

    for (std::uint8_t i = 0; i < kFrameSlots; ++i) {
        (void)xQueueSend(free_q_, &i, 0);
    }
    return true;
}

bool FramePipe::acquire(std::uint8_t& slot) noexcept {
    // Zero timeout throughout: this runs in the WebSocket client's callback, and
    // a callback that blocks stalls the socket it is reading from.
    if (xQueueReceive(free_q_, &slot, 0) != pdTRUE) {
        ++stats_.no_slot;
        return false;
    }
    return true;
}

bool FramePipe::publish(std::uint8_t slot, std::uint32_t len) noexcept {
    FeedMessage msg;
    msg.kind = FeedMessage::Kind::Frame;
    msg.slot = slot;
    msg.len = len;
    if (xQueueSend(ready_q_, &msg, 0) != pdTRUE) {
        ++stats_.queue_full;
        release(slot);
        return false;
    }
    ++stats_.frames_published;
    return true;
}

void FramePipe::release(std::uint8_t slot) noexcept {
    (void)xQueueSend(free_q_, &slot, 0);
}

bool FramePipe::post_status(FeedMessage::Kind kind) noexcept {
    FeedMessage msg;
    msg.kind = kind;
    if (xQueueSend(ready_q_, &msg, 0) != pdTRUE) {
        ++stats_.queue_full;
        return false;
    }
    return true;
}

bool FramePipe::receive(FeedMessage& out, TickType_t ticks) noexcept {
    return xQueueReceive(ready_q_, &out, ticks) == pdTRUE;
}

void FramePipe::recycle(std::uint8_t slot) noexcept {
    (void)xQueueSend(free_q_, &slot, 0);
}

}  // namespace depthcharge::fw
