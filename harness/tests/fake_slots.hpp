// harness/tests/fake_slots.hpp — one fake FramePipe, shared by the two suites
// that drive the reassembler.
//
// A FAKE AND NOT A MOCK, and the distinction is the whole value of the file: it
// actually models slot ownership, so a leaked slot, a double release or a
// publish from a slot nobody acquired fails loudly inside the call that did it,
// with a doctest frame pointing at the line. Half the reassembler's contract is
// "the pool comes back whole whatever the stream does", and a mock that only
// recorded calls could not check that at all.
//
// It exists because there were two of these — `FakeSlots` in
// test_frame_reassembler.cpp and `Pipe` in test_ws_frame.cpp — that had drifted
// into the same object with different names and different amounts of
// instrumentation, which the 2026-08-15 review caught. The richer surface won:
// `acquire_failures` and the `arrivals` / `published_at` pair are what the
// arrival-vs-event split is argued from, and a suite that lacks them cannot ask
// the question.
//
// Capacity and slot count are template parameters because the two suites need
// different ones for good reasons. The reassembler suite runs a 64-byte capacity
// so the oversize boundary is reachable in a readable test; the frame suite runs
// the firmware's real 16 KiB, because its cases are about what happens AT the
// production geometry.
#pragma once

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dc::testing {

template <std::size_t kCapacity, std::uint8_t kSlotCount = 2>
struct FakeSlotPool {
    static constexpr std::size_t kCap = kCapacity;
    static constexpr std::uint8_t kSlots = kSlotCount;

    // Heap-backed rather than a member array: at the frame suite's 16 KiB x 2
    // this is 32 KiB, and a Chain on the stack would overflow the default
    // thread stack on some hosts. std::vector is fine here — alloc_probe's
    // zero-allocation window is opened after construction, never around it.
    std::vector<char> storage = std::vector<char>(std::size_t{kSlots} * kCap);
    bool in_flight[kSlots]{};
    std::uint8_t free_count = kSlots;

    std::vector<std::string> published;
    // The arrival half of M3's stall instrument: `arrivals` is every whole
    // message the socket delivered, `published_at` only the ones that survived
    // to the feed task. The two differing is the point — see the note in
    // FrameReassembler::finish().
    std::vector<std::int64_t> arrivals;
    std::vector<std::int64_t> published_at;

    std::uint32_t oversize = 0;
    std::uint32_t abandoned = 0;
    std::uint32_t continuation = 0;
    std::uint32_t control = 0;
    std::uint32_t chunks = 0;
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

    char* buffer(std::uint8_t slot) noexcept {
        return storage.data() + (static_cast<std::size_t>(slot) * kCap);
    }

    void release(std::uint8_t slot) noexcept {
        REQUIRE(in_flight[slot]);   // a double release is a bug, not a no-op
        in_flight[slot] = false;
        ++free_count;
    }

    bool publish(std::uint8_t slot, std::uint32_t len, std::int64_t arrival_us) noexcept {
        REQUIRE(in_flight[slot]);
        published.emplace_back(buffer(slot), len);
        published_at.push_back(arrival_us);
        in_flight[slot] = false;
        ++free_count;
        return true;
    }

    void note_arrival(std::int64_t at_us) noexcept { arrivals.push_back(at_us); }
    void count_oversize() noexcept { ++oversize; }
    void count_abandoned() noexcept { ++abandoned; }
    void count_continuation() noexcept { ++continuation; }
    void count_control() noexcept { ++control; }
    void count_chunk() noexcept { ++chunks; }
};

}  // namespace dc::testing
