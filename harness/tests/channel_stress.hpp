// channel_stress.hpp — the version-stamped frame the SnapshotChannel tests race.
//
// Shared by the two things that exercise the M3 stage-A channel:
//   test_snapshot_channel.cpp  the ctest proof (doctest, runs everywhere)
//   tsan_workload.cpp          the ThreadSanitizer workload (no doctest, Linux)
// Both must agree on what "a whole frame" means, so the definition lives once.
//
// The idea: make every field of DisplaySnapshot a pure function of its own
// `version`. Then a consumed frame is either entirely one publish or provably
// torn, and no cross-thread bookkeeping is needed to tell which — torn_field()
// re-derives the frame from the version it is carrying and names the first
// field that disagrees. A channel that let two publishes overlap in one slot
// would show up as, say, `bids[].qty` mismatching while `version` and `seq`
// looked fine, which is exactly the failure a version check alone cannot see.
//
// Header-only and dependency-free (no doctest, no harness library) so the TSan
// workload stays a single self-contained translation unit.
#pragma once

#include <cstddef>
#include <cstdint>

#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/feed_event.hpp>
#include <depthcharge/snapshot_channel.hpp>

namespace dc::testing::channel {

// Avalanche mix (Murmur3 finaliser over a salted seed): consecutive versions
// differ in essentially every bit of every field, so a copy that tears anywhere
// lands on a mismatch rather than on two versions that happened to agree.
constexpr std::uint32_t mix(std::uint32_t version, std::uint32_t salt) noexcept {
    std::uint32_t x = version * 2654435761u + salt * 2246822519u + 0x9e3779b9u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    x *= 3266489917u;
    x ^= x >> 16;
    return x;
}

// Two independent mixes, so both halves of a 64-bit field move with the version
// and a half-copied PriceTicks/Qty/Seq cannot survive the check.
constexpr std::uint64_t mix64(std::uint32_t version, std::uint32_t salt) noexcept {
    return (static_cast<std::uint64_t>(mix(version, salt)) << 32) |
           static_cast<std::uint64_t>(mix(version, salt ^ 0x5bf03635u));
}

// Fill every field — including the level and trade slots past the published
// counts, which the book zero-fills but which this test deliberately stamps, so
// the whole 1,168-byte object is under the check and not just its head.
inline void stamp(depthcharge::DisplaySnapshot& out, std::uint32_t version) noexcept {
    using namespace depthcharge;

    out.version = version;
    out.symbol.id = mix(version, 1);
    out.symbol.price_decimals = static_cast<std::int32_t>(mix(version, 2) % 9u);
    out.symbol.qty_step = static_cast<Qty>(1 + mix(version, 3) % 1000u);
    out.seq = mix64(version, 4);

    // Both enums stay inside their declared ranges: a value outside them would
    // make the comparison in torn_field() undefined rather than failing.
    out.status = (mix(version, 5) & 1u) != 0 ? FeedStatus::Live : FeedStatus::Stale;
    out.stale_reason = static_cast<GapReason>(mix(version, 6) % 5u);

    out.bid_count = static_cast<std::uint8_t>(mix(version, 7) % (kDisplayLevels + 1));
    out.ask_count = static_cast<std::uint8_t>(mix(version, 8) % (kDisplayLevels + 1));
    out.trade_count = static_cast<std::uint8_t>(mix(version, 9) % (kTradeRingSize + 1));

    out.has_last = (mix(version, 10) & 1u) != 0;
    out.last_px = static_cast<PriceTicks>(mix64(version, 11));

    for (std::size_t i = 0; i < kDisplayLevels; ++i) {
        const auto salt = static_cast<std::uint32_t>(i);
        out.bids[i].px = static_cast<PriceTicks>(mix64(version, 100u + salt));
        out.bids[i].qty = static_cast<Qty>(mix64(version, 200u + salt));
        out.asks[i].px = static_cast<PriceTicks>(mix64(version, 300u + salt));
        out.asks[i].qty = static_cast<Qty>(mix64(version, 400u + salt));
    }
    for (std::size_t i = 0; i < kTradeRingSize; ++i) {
        const auto salt = static_cast<std::uint32_t>(i);
        out.trades[i].px = static_cast<PriceTicks>(mix64(version, 500u + salt));
        out.trades[i].qty = static_cast<Qty>(mix64(version, 600u + salt));
        out.trades[i].seq = mix64(version, 700u + salt);
        out.trades[i].aggressor = (mix(version, 800u + salt) & 1u) != 0 ? Side::Ask : Side::Bid;
    }
}

// nullptr if `snap` is exactly what stamp() writes for snap.version; otherwise
// the name of the first field that disagrees — i.e. where the frame tore.
inline const char* torn_field(const depthcharge::DisplaySnapshot& snap) noexcept {
    depthcharge::DisplaySnapshot want{};
    stamp(want, snap.version);

    if (snap.symbol.id != want.symbol.id) { return "symbol.id"; }
    if (snap.symbol.price_decimals != want.symbol.price_decimals) {
        return "symbol.price_decimals";
    }
    if (snap.symbol.qty_step != want.symbol.qty_step) { return "symbol.qty_step"; }
    if (snap.seq != want.seq) { return "seq"; }
    if (snap.status != want.status) { return "status"; }
    if (snap.stale_reason != want.stale_reason) { return "stale_reason"; }
    if (snap.bid_count != want.bid_count) { return "bid_count"; }
    if (snap.ask_count != want.ask_count) { return "ask_count"; }
    if (snap.trade_count != want.trade_count) { return "trade_count"; }
    if (snap.has_last != want.has_last) { return "has_last"; }
    if (snap.last_px != want.last_px) { return "last_px"; }

    for (std::size_t i = 0; i < depthcharge::kDisplayLevels; ++i) {
        if (snap.bids[i].px != want.bids[i].px) { return "bids[].px"; }
        if (snap.bids[i].qty != want.bids[i].qty) { return "bids[].qty"; }
        if (snap.asks[i].px != want.asks[i].px) { return "asks[].px"; }
        if (snap.asks[i].qty != want.asks[i].qty) { return "asks[].qty"; }
    }
    for (std::size_t i = 0; i < depthcharge::kTradeRingSize; ++i) {
        if (snap.trades[i].px != want.trades[i].px) { return "trades[].px"; }
        if (snap.trades[i].qty != want.trades[i].qty) { return "trades[].qty"; }
        if (snap.trades[i].seq != want.trades[i].seq) { return "trades[].seq"; }
        if (snap.trades[i].aggressor != want.trades[i].aggressor) {
            return "trades[].aggressor";
        }
    }
    return nullptr;
}

// One publish, stamped. The producer-side counterpart of consume_checked()
// below: both callers go through it so "what a publish of version N looks like"
// has exactly one definition.
inline void publish_stamped(depthcharge::SnapshotChannel& channel,
                            depthcharge::DisplaySnapshot& scratch,
                            std::uint32_t version) noexcept {
    stamp(scratch, version);
    channel.publish(scratch);
}

// What one consumer thread observed. Filled by the consumer, read by whoever
// joined it: no assertion macro is called off the main thread, so the test
// framework never has to be thread-safe for this suite to be sound.
struct ConsumerReport {
    std::uint64_t consumed = 0;          // successful consume() calls
    std::uint64_t empty_polls = 0;       // consume() reporting nothing new
    std::uint32_t first_version = 0;     // first frame delivered
    std::uint32_t last_version = 0;      // most recent frame delivered

    const char* torn_where = nullptr;    // first torn field, if any
    std::uint32_t torn_version = 0;

    bool regressed = false;              // a delivered version went backwards
    std::uint32_t regressed_from = 0;
    std::uint32_t regressed_to = 0;

    bool timed_out = false;              // never saw the producer's last frame
};

// One consume(), checked. Returns false if there was nothing new.
//
// Every rule the channel owes the render side is asserted here in one place:
// the frame is whole, and the version it carries is strictly newer than the last
// one delivered (a successful consume means at least one publish has landed
// since the previous one, and the writer's versions only increase — so equal or
// backwards is a bookkeeping bug, not a benign duplicate).
inline bool consume_checked(depthcharge::SnapshotChannel& channel,
                            depthcharge::DisplaySnapshot& out,
                            ConsumerReport& report) noexcept {
    if (!channel.consume(out)) {
        ++report.empty_polls;
        return false;
    }
    if (const char* where = torn_field(out); where != nullptr && report.torn_where == nullptr) {
        report.torn_where = where;
        report.torn_version = out.version;
    }
    if (report.consumed != 0 && out.version <= report.last_version && !report.regressed) {
        report.regressed = true;
        report.regressed_from = report.last_version;
        report.regressed_to = out.version;
    }
    if (report.consumed == 0) { report.first_version = out.version; }
    report.last_version = out.version;
    ++report.consumed;
    return true;
}

}  // namespace dc::testing::channel
