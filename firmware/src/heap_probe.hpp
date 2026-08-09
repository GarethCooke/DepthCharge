// firmware/src/heap_probe.hpp — invariant #7, measured on the target.
//
// WHAT THE BRIEF ASKED FOR, AND WHY THIS IS NOT IT.
//
// Stage C's deliverable 8 asks for ESP-IDF heap tracing (`heap_trace_start` over
// a many-frame window) as the device-side twin of the host `alloc_probe`. That
// is unavailable here, and not by oversight: the precompiled Arduino framework's
// sdkconfig has
//
//     CONFIG_HEAP_TRACING_OFF=y
//     # CONFIG_HEAP_TRACING_STANDALONE is not set
//
// so the tracing code is compiled out of the shipped libraries — there is no
// heap_trace object to link against. Turning it on means building ESP-IDF from
// source, i.e. abandoning the Arduino framework M2 proved and the M3 brief says
// to reuse, and re-running first light before stage C could even start.
//
// WHAT THIS MEASURES INSTEAD, AND WHY IT IS ENOUGH.
//
// Three numbers, sampled at the boundaries of a steady-state window:
//
//   free            heap_caps_get_free_size          -> NET allocation
//   low water       heap_caps_get_minimum_free_size  -> GROSS churn
//   largest block   heap_caps_get_largest_free_block -> FRAGMENTATION
//
// BE HONEST ABOUT WHAT THESE THREE CAN AND CANNOT SEE. An earlier draft of this
// comment claimed the low-water mark "cannot be fooled" by an allocate-and-free
// transport. It can, and a review caught it: heap_caps_get_minimum_free_size is
// a SINCE-BOOT minimum with no reset entry point, so a steady churn of one
// fixed-size block dips it once, early, and then never moves it again — and
// mark_baseline() is taken 50 frames in, after that dip has already happened.
// What the three numbers actually establish is "nothing is leaking and nothing
// is fragmenting", which is worth measuring and is not the same claim.
//
// AND THE TRANSPORT DOES ALLOCATE — this was knowable statically and is no
// longer an open question for the bench. esp_websocket_client dispatches every
// event through esp_event_post_to, which heap-copies the 28-byte payload and
// frees it when the handler returns (visible as memset/calloc/memcpy/.../free
// in the shipped libesp_event.a). With a 4 KiB RX buffer and ~8.7 KB book
// frames that is three DATA events per frame at ~12.5 frames/s: roughly 37
// balanced calloc/free pairs a second, inside the very window invariant #7
// names.
//
// SO THE READING FOR ARCHITECTURE §9, written from measurement rather than left
// for the bench to settle:
//
//     Invariant #7 covers "the feed->render path". Its ENGINE half holds in the
//     strong form and is proven: alloc_probe counts zero global operator new
//     over both full traces on the host, and the streaming parser's xtensa
//     object has no undefined operator new, so it cannot allocate on the target
//     either. Its TRANSPORT half does not, and cannot without abandoning
//     esp_websocket_client: the event loop allocates once per event by design.
//     On the target the invariant therefore reads as NO NET ALLOCATION AND NO
//     FRAGMENTATION DRIFT — bounded, balanced churn of one fixed-size block is
//     compatible with what the invariant exists to protect (determinism, and a
//     heap still healthy after a week on a desk); an unbounded or growing one
//     is not.
//
// What the bench adds is confirmation that the churn really is balanced over
// hours — free delta 0, largest free block not falling — not the discovery of
// whether it allocates. If the strong form is ever wanted, the lever is
// kWsRxBufferBytes: raising it above the largest frame gives one event per
// frame instead of three, which reduces the rate but does not reach zero.
#pragma once

#include <cstdint>

namespace depthcharge::fw {

struct HeapSample {
    std::uint32_t free_internal = 0;
    std::uint32_t low_water_internal = 0;
    std::uint32_t largest_block_internal = 0;
    std::uint32_t free_total = 0;
};

// Sample the internal (DMA-capable, non-PSRAM) heap — the one that matters,
// since PSRAM is neither used by the feed path nor safe to assume present.
HeapSample sample_heap() noexcept;

// A window: mark the start once steady state is reached (connect + first
// snapshot, exactly the boundary invariant #7 names), then report at intervals.
class HeapProbe {
public:
    void mark_baseline() noexcept;
    bool armed() const noexcept { return armed_; }

    // Logs the delta against the baseline. Call from the console task.
    void report(const char* tag, std::uint32_t frames_since_baseline) noexcept;

private:
    HeapSample baseline_{};
    bool armed_ = false;
};

}  // namespace depthcharge::fw
