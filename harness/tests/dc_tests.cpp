// dc_tests — DepthCharge M0 golden tests (doctest, wired to ctest).
//
// First goldens for the project: the committed replay traces parse, their
// per-kind frame counts match pinned expectations, the metadata line is present
// and complete, and the two headline empirical findings from the M0 capture are
// locked in as executable assertions:
//   * Anvil's wire seq is non-monotonic in a single ticker's received stream
//     (see harness/replay/NOTES.md) — baseline has backward seq steps.
//   * the reconnect trace carries a mid-stream resync snapshot preceded by a
//     multi-second gap (the simulated drop).
//
// Counts are pinned to the committed slices. Regenerating a trace (re-slicing a
// fresh capture) is expected to change them — re-pin here in the same commit.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

#include <depthcharge/feed_event.hpp>  // engine contract — compiled here so its
                                       // guards actually run under ctest/-Werror
#include "dc_harness/trace.hpp"

using namespace dc::harness;

// Contract compile-guards (ARCHITECTURE §4 / invariants #3, #7). Including the
// engine header above forces it through the harness build under warnings-as-
// errors — the only place the host build compiles engine/ at M0 — so its own
// static_assert fires and these lock the boundary vocabulary against drift.
namespace {
using namespace depthcharge;
static_assert(std::is_trivially_copyable_v<FeedEvent>);
static_assert(sizeof(PriceTicks) == 8 && std::is_signed_v<PriceTicks>);
static_assert(sizeof(Qty) == 8 && std::is_signed_v<Qty>);
static_assert(std::is_same_v<Seq, std::uint64_t>);
static_assert(std::is_same_v<std::underlying_type_t<Side>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<FeedEvent::Kind>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<GapReason>, std::uint8_t>);
static_assert(static_cast<int>(Side::Bid) == 0 && static_cast<int>(Side::Ask) == 1);
}  // namespace

namespace {
std::string baseline_path() { return std::string(DC_REPLAY_DIR) + "/anvil_101_baseline.ndjson"; }
std::string reconnect_path() { return std::string(DC_REPLAY_DIR) + "/anvil_101_reconnect.ndjson"; }
}  // namespace

TEST_CASE("baseline trace: metadata, pinned counts, structure") {
    const TraceStats s = read_trace(baseline_path());

    SUBCASE("metadata line present and complete") {
        CHECK(s.meta.complete());
        CHECK(s.meta.ticker == 101);
        CHECK(s.meta.capture_mode == "baseline");
        CHECK(s.meta.tool_version == "0.1.0");
    }

    SUBCASE("per-kind frame counts match pinned expectations") {
        CHECK(s.frame_count == 1406);
        CHECK(s.count("snapshot") == 1);
        CHECK(s.count("book") == 1088);
        CHECK(s.count("trade") == 136);
        CHECK(s.count("summary") == 181);
        // kinds partition the frames — no unexpected 'type' slipped in.
        CHECK(s.count("snapshot") + s.count("book") + s.count("trade") +
                  s.count("summary") ==
              s.frame_count);
    }

    SUBCASE("one on-connect snapshot, no mid-stream resync") {
        CHECK(s.snapshot_count == 1);
        CHECK(s.mid_stream_snapshots == 0);
    }

    SUBCASE("every frame carries a seq, but the seq is non-monotonic") {
        CHECK(s.seq_frames == s.frame_count);
        CHECK(s.seq_monotonic == false);
        CHECK(s.seq_backward_steps == 14);  // the M0 finding, pinned
    }
}

TEST_CASE("reconnect trace: mid-stream resync preceded by a visible gap") {
    const TraceStats s = read_trace(reconnect_path());

    CHECK(s.meta.complete());
    CHECK(s.meta.capture_mode == "reconnect");

    CHECK(s.frame_count == 1288);
    CHECK(s.count("snapshot") == 1);
    CHECK(s.count("book") == 1012);
    CHECK(s.count("trade") == 103);
    CHECK(s.count("summary") == 172);

    // The committed window is centred on the resync, so it holds exactly one
    // snapshot and that snapshot is mid-stream (a resync, not the on-connect one).
    CHECK(s.mid_stream_snapshots == 1);

    // The ~4 s simulated network drop is on record as the largest inter-frame gap.
    CHECK(s.max_gap_ms > 4000.0);
    CHECK(s.max_gap_ms < 6000.0);
}

TEST_CASE("dc_replay smoke: both committed traces validate") {
    CHECK_NOTHROW(read_trace(baseline_path()));
    CHECK_NOTHROW(read_trace(reconnect_path()));
}

TEST_CASE("reader accepts a well-formed synthetic trace") {
    const std::string t =
        R"({"captured_at":"2026-07-23T00:00:00Z","url":"wss://x/ws","ticker":101,"tool_version":"0.1.0","capture_mode":"baseline"})"
        "\n"
        R"({"rx_ns":1000000,"frame":{"type":"snapshot","seq":10,"ticker":101}})"
        "\n"
        R"({"rx_ns":2000000,"frame":{"type":"book","seq":11,"ticker":101}})"
        "\n"
        R"({"rx_ns":3000000,"frame":{"type":"trade","seq":13,"ticker":101}})"
        "\n";
    const TraceStats s = read_trace_text(t);
    CHECK(s.meta.complete());
    CHECK(s.frame_count == 3);
    CHECK(s.count("book") == 1);
    CHECK(s.snapshot_count == 1);
    CHECK(s.mid_stream_snapshots == 0);
    CHECK(s.seq_monotonic == true);
    CHECK(s.seq_backward_steps == 0);
}

TEST_CASE("reader flags a synthetic non-monotonic seq") {
    const std::string t =
        R"({"captured_at":"t","url":"u","ticker":1,"tool_version":"0.1.0"})"
        "\n"
        R"({"rx_ns":1000000,"frame":{"type":"book","seq":20}})"
        "\n"
        R"({"rx_ns":2000000,"frame":{"type":"summary","seq":25}})"
        "\n"
        R"({"rx_ns":3000000,"frame":{"type":"book","seq":22}})"  // seq drops: 25 -> 22
        "\n";
    const TraceStats s = read_trace_text(t);
    CHECK(s.seq_backward_steps == 1);
    CHECK(s.seq_monotonic == false);
    CHECK(s.seq_min == 20);
    CHECK(s.seq_max == 25);
}

TEST_CASE("reader detects a mid-stream snapshot as a reconnect signature") {
    const std::string t =
        R"({"captured_at":"t","url":"u","ticker":1,"tool_version":"0.1.0","capture_mode":"reconnect"})"
        "\n"
        R"({"rx_ns":1000000,"frame":{"type":"book","seq":1}})"
        "\n"
        R"({"rx_ns":5001000000,"frame":{"type":"snapshot","seq":2}})"  // +5s gap then resync
        "\n";
    const TraceStats s = read_trace_text(t);
    CHECK(s.snapshot_count == 1);
    CHECK(s.mid_stream_snapshots == 1);
    CHECK(s.max_gap_ms > 4999.0);
}

TEST_CASE("reader rejects malformed traces with a line number") {
    SUBCASE("empty input") {
        CHECK_THROWS_AS(read_trace_text(""), TraceError);
    }
    SUBCASE("metadata missing a required field") {
        CHECK_THROWS_AS(read_trace_text(R"({"url":"u","ticker":1,"tool_version":"v"})"),
                        TraceError);
    }
    SUBCASE("frame missing rx_ns") {
        const std::string t =
            R"({"captured_at":"t","url":"u","ticker":1,"tool_version":"v"})"
            "\n"
            R"({"frame":{"type":"book"}})"
            "\n";
        CHECK_THROWS_AS(read_trace_text(t), TraceError);
    }
    SUBCASE("frame missing type") {
        const std::string t =
            R"({"captured_at":"t","url":"u","ticker":1,"tool_version":"v"})"
            "\n"
            R"({"rx_ns":1,"frame":{"seq":1}})"
            "\n";
        CHECK_THROWS_AS(read_trace_text(t), TraceError);
    }
    SUBCASE("non-JSON frame line") {
        const std::string t =
            R"({"captured_at":"t","url":"u","ticker":1,"tool_version":"v"})"
            "\n"
            "this is not json\n";
        CHECK_THROWS_AS(read_trace_text(t), TraceError);
    }
    SUBCASE("rx_ns goes backwards") {
        const std::string t =
            R"({"captured_at":"t","url":"u","ticker":1,"tool_version":"v"})"
            "\n"
            R"({"rx_ns":5000,"frame":{"type":"book"}})"
            "\n"
            R"({"rx_ns":4000,"frame":{"type":"book"}})"
            "\n";
        CHECK_THROWS_AS(read_trace_text(t), TraceError);
    }
}

TEST_CASE("FeedEvent is a flat value the feed->render hand-off can memcpy") {
    // Invariant #7: the boundary type carries no ownership and survives a raw
    // byte copy — the mechanism the seqlock/double-buffer hand-off relies on.
    depthcharge::FeedEvent ev{};
    ev.kind = depthcharge::FeedEvent::Kind::Trade;
    ev.seq = 42;
    ev.px = -1234567890123LL;  // integer ticks, may be negative
    ev.qty = 500;
    ev.side = depthcharge::Side::Ask;

    depthcharge::FeedEvent copy{};
    std::memcpy(&copy, &ev, sizeof ev);

    CHECK(copy.kind == depthcharge::FeedEvent::Kind::Trade);
    CHECK(copy.seq == 42u);
    CHECK(copy.px == -1234567890123LL);
    CHECK(copy.qty == 500);
    CHECK(copy.side == depthcharge::Side::Ask);
}
