// firmware/src/frame_pipe.cpp — see frame_pipe.hpp for why this exists.
#include "frame_pipe.hpp"

#include "esp_timer.h"

namespace depthcharge::fw {

bool FramePipe::begin() noexcept {
    // THE SLABS, ONCE, HERE RATHER THAN IN `.bss` (M5 stage D-A2). See the
    // header for the three problems this move solves and for the latency
    // objection it has to answer. Taken in `begin()` and not in a constructor
    // because this function already returns a failure the caller acts on, so a
    // board with no PSRAM says so and runs without a pipe instead of
    // dereferencing null on the first frame.
    slots_.reset(new (std::nothrow) char[kFrameSlots * kFrameCapacity]);
    if (slots_ == nullptr) { return false; }

    free_q_ = xQueueCreate(kFrameSlots, sizeof(std::uint8_t));
    ready_q_ = xQueueCreate(kReadyQueueDepth, sizeof(FeedMessage));
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
    // The slot's life starts here. Written before the slot is handed to anyone,
    // so no synchronisation is needed — see the member.
    acquired_us_[slot] = esp_timer_get_time();
    return true;
}

void FramePipe::note_arrival(std::int64_t at_us) noexcept {
    ++stats_.messages_arrived;
    if (last_arrival_us_ != 0 && at_us > last_arrival_us_) {
        stats_.arrival_gaps.add(static_cast<std::uint64_t>(at_us - last_arrival_us_));
    }
    last_arrival_us_ = at_us;
}

bool FramePipe::publish(std::uint8_t slot, std::uint32_t len, std::int64_t arrival_us) noexcept {
    FeedMessage msg;
    msg.kind = FeedMessage::Kind::Frame;
    msg.slot = slot;
    msg.len = len;
    msg.arrival_us = arrival_us;
    if (xQueueSend(ready_q_, &msg, 0) != pdTRUE) {
        ++stats_.queue_full;
        release(slot);
        return false;
    }
    ++stats_.frames_published;
    stats_.bytes_published += len;
    if (len > stats_.largest_message) { stats_.largest_message = len; }
    if (stats_.smallest_message == 0 || len < stats_.smallest_message) {
        stats_.smallest_message = len;
    }
    return true;
}

// BOTH ENDINGS, because a slot's occupancy is occupancy whether the message was
// published or thrown away. `release` is the abandoned/oversize path and
// `recycle` the parsed one; measuring only the second would report a pipe that
// looks healthiest exactly when it is dropping most.
void FramePipe::note_residency(std::uint8_t slot) noexcept {
    if (slot >= kFrameSlots) { return; }
    const std::int64_t started = acquired_us_[slot];
    if (started == 0) { return; }          // never acquired; nothing to measure
    const std::int64_t now = esp_timer_get_time();
    if (now > started) {
        stats_.residency.add(static_cast<std::uint64_t>(now - started));
    }
    acquired_us_[slot] = 0;
}

void FramePipe::release(std::uint8_t slot) noexcept {
    note_residency(slot);
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
    note_residency(slot);
    (void)xQueueSend(free_q_, &slot, 0);
}

}  // namespace depthcharge::fw
