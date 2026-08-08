// test_streaming_parser.cpp — what is true of the STREAMING parser alone.
//
// Everything the two implementations must agree about lives in shared files that
// both dc_tests and dc_tests_streaming compile: test_replay_goldens.cpp (both
// traces end to end), test_anvil_adapter.cpp (the malformed corpus and the
// seam's postcondition) and test_parser_equivalence.cpp (the JSON grammar).
// This file is compiled into dc_tests_streaming ONLY, and holds the two kinds of
// claim that cannot be shared:
//
//   1. THE ALLOCATION PROOF (invariant #7). The reference allocates by design —
//      that is the debt M1 booked and this stage pays — so the probe can only
//      be pointed at this parser. It is the reason the streaming parser exists,
//      and until it is measured it is only a claim about a file's contents.
//   2. THE DELIBERATE DIVERGENCES. Two inputs get a different answer here than
//      from nlohmann, both of them outside anything Anvil can send, both chosen
//      rather than stumbled into. Asserting them makes them a decision on
//      record instead of a comment: if a later session "fixes" one, this file
//      says what was traded away and why.
#include <doctest/doctest.h>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <depthcharge/anvil/anvil_adapter.hpp>
#include <depthcharge/anvil/anvil_frame.hpp>
#include <depthcharge/book.hpp>
#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/snapshot_channel.hpp>

#include "alloc_probe.hpp"
#include "dc_harness/trace.hpp"

using depthcharge::Book;
using depthcharge::DisplaySnapshot;
using depthcharge::FeedEvent;
using depthcharge::SnapshotChannel;
using depthcharge::anvil::AnvilAdapter;
using depthcharge::anvil::AnvilFrame;
using depthcharge::anvil::kAnvilTicker101;
using depthcharge::anvil::parse_anvil_frame;
using depthcharge::anvil::ParseStatus;

namespace {

// A whole trace held in memory, sliced into verbatim frame views.
//
// The measured window must contain no file I/O and no nlohmann, because both
// allocate freely and would drown the signal. So the trace is read, split and
// sliced up front — every allocation this test can possibly make happens before
// the counter is latched — and the replay itself then runs over a vector of
// string_views into one already-owned buffer, which is exactly the shape the
// firmware net task will have (a fixed reassembly buffer handed to the parser).
struct LoadedTrace {
    std::string text;                      // owns the bytes
    std::vector<std::string_view> frames;  // verbatim wire frames, in order
};

LoadedTrace load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    const std::string context = "trace: " + path;
    INFO(context);
    REQUIRE(in.good());
    std::ostringstream buf;
    buf << in.rdbuf();

    LoadedTrace out;
    out.text = buf.str();
    std::string_view rest(out.text);
    while (!rest.empty()) {
        const std::size_t nl = rest.find('\n');
        const std::string_view line = (nl == std::string_view::npos)
                                          ? rest
                                          : std::string_view(rest.data(), nl);
        rest = (nl == std::string_view::npos)
                   ? std::string_view{}
                   : std::string_view(rest.data() + nl + 1, rest.size() - nl - 1);
        // Line 1 is capture metadata and carries no "frame" key, so it slices to
        // empty and drops out here along with any trailing blank line.
        const std::string_view frame = dc::harness::slice_frame_json(line);
        if (!frame.empty()) { out.frames.push_back(frame); }
    }
    return out;
}

std::string baseline_path() { return std::string(DC_REPLAY_DIR) + "/anvil_101_baseline.ndjson"; }
std::string reconnect_path() { return std::string(DC_REPLAY_DIR) + "/anvil_101_reconnect.ndjson"; }

// The full feed path, run without a single doctest macro inside it: the probe
// counts global operator new, and CHECK() allocates.
struct ProbeResult {
    std::size_t frames = 0;
    std::size_t events = 0;
    std::size_t snapshots = 0;
    std::size_t allocations_after_first_snapshot = 0;
    std::size_t publishes = 0;
    bool first_snapshot_seen = false;
};

ProbeResult run_feed_path(const LoadedTrace& trace, AnvilAdapter& adapter, Book& book,
                          DisplaySnapshot& staging, SnapshotChannel& channel,
                          DisplaySnapshot& received) {
    ProbeResult r;
    std::size_t baseline_allocations = 0;

    for (const std::string_view frame : trace.frames) {
        adapter.on_frame(frame, [&](const FeedEvent& ev) {
            ++r.events;
            if (ev.kind == FeedEvent::Kind::Snapshot) { ++r.snapshots; }
            book.apply(ev);
            book.publish(staging);
            channel.publish(staging);
            if (channel.consume(received)) { ++r.publishes; }
        });
        ++r.frames;

        // Latch the counter the moment the first snapshot has been folded in and
        // published — the exact boundary invariant #7 names ("after connect +
        // first snapshot"). Everything after this point must move it by zero.
        if (!r.first_snapshot_seen && r.snapshots > 0) {
            r.first_snapshot_seen = true;
            baseline_allocations = dc::testing::allocation_count();
        }
    }

    r.allocations_after_first_snapshot =
        dc::testing::allocation_count() - baseline_allocations;
    return r;
}

}  // namespace

// --- invariant #7 -----------------------------------------------------------

TEST_CASE("the streaming feed path allocates nothing after the first snapshot") {
    // One subcase per trace so a failure names which one, and so the reconnect
    // trace's resync path is measured too. Counts are pinned rather than bounded:
    // a zero-allocation result over a run that quietly stopped early would be a
    // vacuous pass, and these are the same per-kind totals the M0 trace goldens
    // hold. (No Gap events appear here — a transport gap is synthesised by the
    // replay driver from the rx_ns hole, and this loop feeds frames directly.)
    std::string path;
    std::size_t want_frames = 0;
    std::size_t want_events = 0;
    SUBCASE("baseline trace") {
        path = baseline_path();
        want_frames = 1406;
        want_events = 1225;   // 1 snapshot + 1088 book + 136 trade
    }
    SUBCASE("reconnect trace") {
        path = reconnect_path();
        want_frames = 1288;
        want_events = 1116;   // 1 snapshot + 1012 book + 103 trade
    }

    const LoadedTrace trace = load(path);
    REQUIRE(trace.frames.size() == want_frames);

    // Everything that can allocate is constructed before the window. The channel
    // is three DisplaySnapshots and the adapter carries the 8 KiB AnvilFrame, so
    // both are deliberately built here rather than inside the loop.
    AnvilAdapter adapter(kAnvilTicker101);
    Book book(kAnvilTicker101);
    DisplaySnapshot staging{};
    DisplaySnapshot received{};
    SnapshotChannel channel;

    const ProbeResult r =
        run_feed_path(trace, adapter, book, staging, channel, received);

    CHECK(r.frames == want_frames);
    CHECK(r.first_snapshot_seen);
    CHECK(r.events == want_events);
    CHECK(r.publishes == r.events);   // the reader keeps up, so nothing is skipped

    // The headline. Not "small": zero.
    CHECK(r.allocations_after_first_snapshot == 0);

    // ...and the adapter agrees the run was clean, so the zero above is a
    // measurement of the real decode path and not of an early bail-out.
    CHECK(adapter.stats().parse_errors == 0);
    CHECK(adapter.stats().price_errors == 0);
    CHECK(adapter.stats().frames_in == trace.frames.size());
}

TEST_CASE("malformed frames are dropped without allocating and without throwing") {
    // M1's rule, now measured on the target parser: a frame that will not decode
    // is counted and dropped. Anvil republishes the whole book every ~80 ms, so
    // one lost frame self-heals — but only if losing it costs nothing.
    const char* const bad[] = {
        "this is not json",
        "[1,2,3]",
        R"({"type":"bo)",
        R"({"seq":1,"ticker":101})",
        R"({"type":"book","ticker":101})",
        R"({"type":"book","ticker":101,"bids":[{"price":"9.99725","qty":9}],"asks":[]})",
        R"({"type":"book","ticker":107,"bids":[],"asks":[]})",
        R"({"type":"book","ticker":101,"bids":[{"price":1.0,"qty":1}],"asks":[]})",
        R"({"type":"trade","ticker":101,"price":"1.0","qty":-1,"aggr":"B"})",
    };

    AnvilAdapter adapter(kAnvilTicker101);
    std::size_t events = 0;
    const auto sink = [&](const FeedEvent&) { ++events; };

    // Warm the adapter with one good frame first, so the window measures steady
    // state rather than first use.
    adapter.on_frame(R"({"type":"book","ticker":101,"bids":[],"asks":[]})", sink);

    const std::size_t before = dc::testing::allocation_count();
    for (int pass = 0; pass < 50; ++pass) {
        for (const char* json : bad) { adapter.on_frame(json, sink); }
    }
    const std::size_t after = dc::testing::allocation_count();

    CHECK(after == before);
    CHECK(events == 1);                        // only the warm-up frame
    CHECK(adapter.stats().transport_gaps == 0);  // dropped, never a Gap

    // Exactly one frame in the list is a scale disagreement and exactly one is
    // for another ticker; the rest are malformed. Derived from the list rather
    // than written out, so adding a case never leaves a stale count behind.
    constexpr std::size_t kPasses = 50;
    constexpr std::size_t kMalformed = std::size(bad) - 2;
    CHECK(adapter.stats().price_errors == kPasses);
    CHECK(adapter.stats().other_ticker == kPasses);
    CHECK(adapter.stats().parse_errors == kPasses * kMalformed);
}

TEST_CASE("a deep book costs no heap either, however often it is re-parsed") {
    // The level arrays are the one place a parser could be tempted to grow
    // storage. 256 levels a side, parsed 200 times into the same reused frame.
    std::string json = R"({"type":"book","seq":1,"ticker":101,"bids":[)";
    for (std::size_t i = 0; i < depthcharge::kMaxSnapshotLevels; ++i) {
        if (i > 0) { json += ','; }
        json += R"({"price":"10.0","qty":7,"orders":1})";
    }
    json += R"(],"asks":[)";
    for (std::size_t i = 0; i < depthcharge::kMaxSnapshotLevels; ++i) {
        if (i > 0) { json += ','; }
        json += R"({"price":"10.1","qty":8,"orders":1})";
    }
    json += R"(]})";

    AnvilFrame frame{};
    REQUIRE(parse_anvil_frame(json, kAnvilTicker101, frame) == ParseStatus::Ok);

    const std::size_t before = dc::testing::allocation_count();
    ParseStatus last = ParseStatus::NotJson;
    for (int i = 0; i < 200; ++i) {
        last = parse_anvil_frame(json, kAnvilTicker101, frame);
    }
    const std::size_t after = dc::testing::allocation_count();

    CHECK(last == ParseStatus::Ok);
    CHECK(after == before);
    CHECK(frame.bid_count == depthcharge::kMaxSnapshotLevels);
    CHECK(frame.ask_count == depthcharge::kMaxSnapshotLevels);
}

// --- the deliberate divergences ---------------------------------------------

namespace {

// A frame whose "x" member is `depth` nested empty arrays.
std::string nested_frame(int depth) {
    std::string json = R"({"type":"book","ticker":101,"bids":[],"asks":[],"x":)";
    for (int i = 0; i < depth; ++i) { json += '['; }
    for (int i = 0; i < depth; ++i) { json += ']'; }
    json += '}';
    return json;
}

}  // namespace

TEST_CASE("one skipped value may nest 64 containers; the 65th is rejected, by choice") {
    // nlohmann has no depth limit because it heap-allocates its parse stack
    // (a std::vector<bool>), and 200 000 nested arrays parse fine there. This
    // parser holds one bit per open container in a single uint64_t, which is
    // what makes the skipper both iterative and allocation-free — the two
    // properties invariant #7 and a few-KB FreeRTOS task stack actually need.
    //
    // The trade is a cap at 64 containers per skipped value. Anvil's deepest
    // frame is 4 levels, so the cap sits 16x above anything the venue can
    // produce, and a frame that exceeds it is dropped and counted like any other
    // undecodable frame rather than being allowed to consume stack.
    //
    // The boundary is asserted on BOTH sides. A one-sided test ("200 is
    // rejected") would still pass if the cap silently fell to 8.
    AnvilFrame frame{};

    SUBCASE("64 containers deep is accepted, as it is by the reference") {
        CHECK(parse_anvil_frame(nested_frame(64), kAnvilTicker101, frame) == ParseStatus::Ok);
    }

    SUBCASE("65 is where the two part company") {
        CHECK(parse_anvil_frame(nested_frame(65), kAnvilTicker101, frame) ==
              ParseStatus::NotJson);
    }

    SUBCASE("and far beyond it, without recursing") {
        // 20 000 brackets: proof the skipper is iterative. A recursive-descent
        // skipper would not return a status here, it would take the stack with
        // it.
        CHECK(parse_anvil_frame(nested_frame(20000), kAnvilTicker101, frame) ==
              ParseStatus::NotJson);
    }

    SUBCASE("rejecting it still leaves the frame well-formed") {
        REQUIRE(parse_anvil_frame(nested_frame(200), kAnvilTicker101, frame) ==
                ParseStatus::NotJson);
        CHECK(frame.kind == depthcharge::anvil::FrameKind::Unknown);
        CHECK(frame.bid_count == 0);
        CHECK_FALSE(frame.has_ticker);
    }

    SUBCASE("the budget is per skipped value, not per document") {
        // Two sibling members, each 64 deep, and the whole thing sits inside the
        // frame object and a bids array that the grammar walks itself. If the
        // 64 were a document-wide budget this would fail; it is not, and the
        // real rule is therefore narrower than "deeper than 64 is rejected".
        std::string json = R"({"type":"book","ticker":101,"asks":[],"bids":[{"price":"10.0","qty":1}],"x":)";
        for (int i = 0; i < 64; ++i) { json += '['; }
        for (int i = 0; i < 64; ++i) { json += ']'; }
        json += R"(,"y":)";
        for (int i = 0; i < 64; ++i) { json += '['; }
        for (int i = 0; i < 64; ++i) { json += ']'; }
        json += '}';
        REQUIRE(parse_anvil_frame(json, kAnvilTicker101, frame) == ParseStatus::Ok);
        CHECK(frame.bid_count == 1);
    }
}

namespace {

// A book frame with one bid whose price is `zeros` leading zeros then "9.9972".
// The first zero is written as an escape when `escaped`, which is what forces
// the token through the parser's scratch buffer instead of being sliced in
// place. Numerically the price is 9.9972 however many zeros pad it.
std::string padded_price_frame(int zeros, bool escaped) {
    std::string json = "{\"type\":\"book\",\"ticker\":101,\"bids\":[{\"price\":\"";
    json += escaped ? "\\u0030" : "0";
    for (int i = 1; i < zeros; ++i) { json += '0'; }
    json += "9.9972\",\"qty\":9}],\"asks\":[]}";
    return json;
}

}  // namespace

TEST_CASE("an escaped price whose unescaped form passes the scratch is unrecognised") {
    // Unescaped strings are sliced in place at any length — the common path
    // copies nothing at all, which is the whole point of the streaming design.
    // Only a string that actually carries an escape is rebuilt, into a 64-byte
    // buffer, and one that does not fit cannot be compared.
    //
    // For every token except a price that gives the reference's answer anyway:
    // no long string equals "book", "B" or one of our key names. So the single
    // diverging input is a price written with an escape AND padded past 64
    // bytes, which nlohmann decodes and this calls BadPrice. Anvil emits no
    // escapes at all (measured: zero backslashes across 2,694 frames), so the
    // regime is unreachable from the wire.
    AnvilFrame frame{};

    SUBCASE("an escaped price that fits is decoded normally") {
        // "\u0039.9972" -> "9.9972", six characters unescaped.
        CHECK(parse_anvil_frame("{\"type\":\"book\",\"ticker\":101,"
                                "\"bids\":[{\"price\":\"\\u0039.9972\",\"qty\":9}],"
                                "\"asks\":[]}",
                                kAnvilTicker101, frame) == ParseStatus::Ok);
        REQUIRE(frame.bid_count == 1);
        CHECK(frame.bids[0].px == 99972);
    }

    // Both sides of the boundary. "0"*58 + "9.9972" is 64 characters unescaped,
    // which is the last length the scratch holds; 59 zeros is 65 and is where
    // the two implementations part.
    SUBCASE("64 unescaped characters still decode") {
        REQUIRE(parse_anvil_frame(padded_price_frame(58, /*escaped=*/true), kAnvilTicker101,
                                  frame) == ParseStatus::Ok);
        REQUIRE(frame.bid_count == 1);
        CHECK(frame.bids[0].px == 99972);
    }

    SUBCASE("65 is BadPrice rather than the value") {
        CHECK(parse_anvil_frame(padded_price_frame(59, /*escaped=*/true), kAnvilTicker101,
                                frame) == ParseStatus::BadPrice);
    }

    SUBCASE("the same price with no escape is sliced and decoded, at any length") {
        // No escape anywhere, so there is no scratch and no length limit: the
        // token is a view straight into the input buffer.
        REQUIRE(parse_anvil_frame(padded_price_frame(500, /*escaped=*/false),
                                  kAnvilTicker101, frame) == ParseStatus::Ok);
        REQUIRE(frame.bid_count == 1);
        CHECK(frame.bids[0].px == 99972);
    }

    SUBCASE("a long escaped type or aggressor gives the reference's answer anyway") {
        std::string type = "{\"type\":\"\\u0062ook";
        for (int i = 0; i < 200; ++i) { type += 'x'; }
        type += "\",\"ticker\":101,\"bids\":[],\"asks\":[]}";
        // Not "book", so Unknown — and an unknown kind never looks at the ticker.
        REQUIRE(parse_anvil_frame(type, kAnvilTicker101, frame) == ParseStatus::Ok);
        CHECK(frame.kind == depthcharge::anvil::FrameKind::Unknown);

        std::string aggr = "{\"type\":\"trade\",\"ticker\":101,\"price\":\"10\","
                           "\"qty\":1,\"aggr\":\"\\u0042";
        for (int i = 0; i < 200; ++i) { aggr += 'x'; }
        aggr += "\"}";
        CHECK(parse_anvil_frame(aggr, kAnvilTicker101, frame) == ParseStatus::BadShape);
    }
}
