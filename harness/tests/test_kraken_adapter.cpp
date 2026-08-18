// test_kraken_adapter.cpp — the second adapter, end to end (M4 stage B1).
//
// Invariant #6 says new adapter behaviour ships with a trace and a golden. This
// file is that, over all five committed Kraken slices, plus the four properties
// the slices cannot demonstrate on their own.
//
// ============================================================================
// WHERE THE EXPECTED NUMBERS COME FROM, AND WHY THEY ARE NOT SELF-CONFIRMING
// ============================================================================
//
// A golden derived by running the code under test and writing down what it said
// pins behaviour and proves nothing about correctness. The book-state figures
// below are derived instead from an INDEPENDENT reconstruction — a Python pass
// over the same files, holding the book as integers, truncating to the
// subscribed depth, and checked against the venue's own CRC32 on every message:
// **4,878 / 4,878 checksums reproduced** on the four slices that open with a
// snapshot. A book that reproduces the exchange's checksum on every message is
// the exchange's book, which is a stronger statement than any comparison against
// this code could be.
//
// The eviction counts have a second, older witness: stage 0's
// `tools/kraken_frame_economics.py` measured levels-evicted per slice before any
// of this existed and recorded them in NOTES-kraken.md as 275 / 585 / 1,156 / 19.
// The adapter reproduces all four exactly. Two implementations in two languages,
// written for different purposes, agreeing on a number neither was tuned to —
// which is the independence the 2026-08-17 close-out row says to check for
// before calling agreement corroboration. These two do not share a parent.
//
// ============================================================================
// WHAT IS DELIBERATELY NOT HERE
// ============================================================================
//
// CRC VERIFICATION. B1 parses and carries the checksum; B2 verifies it. The
// measurement above is evidence used to DERIVE goldens on the desk, not a code
// path that ships — `KrakenAdapter` computes no CRC32 and this file asserts none.
// The distinction matters because B2's deliverable is a check, and a check that
// was quietly already running would be nothing to add.
#include <doctest/doctest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <depthcharge/book.hpp>
#include <depthcharge/decimal.hpp>
#include <depthcharge/display_snapshot.hpp>
#include <depthcharge/kraken/kraken_adapter.hpp>

#include "alloc_probe.hpp"
#include "dc_harness/console_ladder.hpp"
#include "dc_harness/replay_driver.hpp"
#include "dc_harness/trace.hpp"

using depthcharge::Book;
using depthcharge::DisplaySnapshot;
using depthcharge::FeedEvent;
using depthcharge::GapReason;
using depthcharge::PriceTicks;
using depthcharge::Qty;
using depthcharge::Side;
using depthcharge::kraken::KrakenAdapter;
using depthcharge::kraken::ParseStatus;
using depthcharge::kraken::SymbolConfig;
using depthcharge::kraken::kKrakenBtcUsd;
using depthcharge::kraken::kKrakenMinaGbp;
using dc::harness::ReplayOptions;
using dc::harness::ReplayResult;
using dc::harness::run_replay_file;

namespace {

std::string trace_path(std::string_view name) {
    return std::string(DC_REPLAY_DIR) + "/" + std::string(name) + ".ndjson";
}

const char* kD10 = "kraken_btcusd_d10_20260816";
const char* kD25 = "kraken_btcusd_d25_20260816";
const char* kD100 = "kraken_btcusd_d100_20260816";
const char* kQuiet = "kraken_minagbp_d25_20260816";
const char* kExtreme = "kraken_minagbp_d25_20260817";

ReplayResult replay(std::string_view name) {
    dc::harness::TraceReader reader(trace_path(name));
    return dc::harness::run_replay(reader, dc::harness::symbol_for(reader.meta()),
                                   ReplayOptions{});
}

}  // namespace

// ---------------------------------------------------------------------------
// DELIVERABLE 6 — all five slices replay through the REAL adapter
// ---------------------------------------------------------------------------

TEST_CASE("every committed Kraken slice replays through the adapter") {
    // Counts first, and `decoder` before any of them: a page of plausible
    // numbers produced by the wrong parser is the failure this pin exists for
    // (ARCHITECTURE §9, 2026-08-18).
    SUBCASE("BTC/USD depth 10") {
        const ReplayResult r = replay(kD10);
        CHECK(r.decoder == "kraken");
        CHECK(r.frames == 902);
        CHECK(r.kraken.snapshot_frames == 1);
        CHECK(r.kraken.update_frames == 838);
        CHECK(r.kraken.heartbeats == 60);
        CHECK(r.kraken.status_frames == 1);
        CHECK(r.kraken.acks == 1);
        CHECK(r.kraken.parse_errors == 0);
        CHECK(r.kraken.price_errors == 0);   // 10^-1 holds every BTC/USD price
        CHECK(r.kraken.qty_errors == 0);     // 10^-8 holds every BTC/USD size
        CHECK(r.kraken.unknown_kind == 0);
        CHECK(r.kraken.other_symbol == 0);
        // NOTES-kraken.md's stage-0 figure for this slice, measured by a
        // different tool in a different language: 275.
        CHECK(r.kraken.levels_evicted == 275);
        CHECK(r.kraken.levels_removed == 265);
        CHECK(r.kraken.checksums_seen == 839);
        CHECK(r.kraken.deltas_before_baseline == 0);
    }
    SUBCASE("BTC/USD depth 25") {
        const ReplayResult r = replay(kD25);
        CHECK(r.decoder == "kraken");
        CHECK(r.frames == 1599);
        CHECK(r.kraken.snapshot_frames == 1);
        CHECK(r.kraken.update_frames == 1536);
        CHECK(r.kraken.heartbeats == 59);    // the window edge, not a thin feed
        CHECK(r.kraken.levels_evicted == 585);   // stage 0's figure
        CHECK(r.kraken.levels_removed == 591);
        CHECK(r.kraken.checksums_seen == 1537);
        CHECK(r.kraken.price_errors == 0);
        CHECK(r.kraken.qty_errors == 0);
    }
    SUBCASE("BTC/USD depth 100") {
        const ReplayResult r = replay(kD100);
        CHECK(r.decoder == "kraken");
        CHECK(r.frames == 2535);
        CHECK(r.kraken.update_frames == 2471);
        CHECK(r.kraken.levels_evicted == 1156);  // stage 0's figure
        CHECK(r.kraken.levels_removed == 1047);
        CHECK(r.kraken.checksums_seen == 2472);
    }
    SUBCASE("MINA/GBP depth 25 — the quiet pair") {
        const ReplayResult r = replay(kQuiet);
        CHECK(r.decoder == "kraken");
        CHECK(r.frames == 93);
        CHECK(r.kraken.update_frames == 29);
        CHECK(r.kraken.heartbeats == 60);
        CHECK(r.kraken.levels_evicted == 19);    // stage 0's figure
        CHECK(r.kraken.price_errors == 0);       // 10^-4, a different scale
    }
    SUBCASE("MINA/GBP depth 25 — the extreme slice, mid-stream") {
        const ReplayResult r = replay(kExtreme);
        CHECK(r.decoder == "kraken");
        CHECK(r.frames == 144);
        CHECK(r.kraken.heartbeats == 95);
        CHECK(r.kraken.update_frames == 49);
        // No subscribe, no ack, no snapshot anywhere in the file.
        CHECK(r.kraken.acks == 0);
        CHECK(r.kraken.status_frames == 0);
        CHECK(r.kraken.snapshot_frames == 0);
    }
}

// ---------------------------------------------------------------------------
// DELIVERABLE 3 — the ladder IS the venue's book
// ---------------------------------------------------------------------------

TEST_CASE("the truncated ladder matches the CRC-validated reconstruction") {
    // Every figure below is read off the independent Python pass described at
    // the head of this file, whose book reproduced the venue's CRC32 on every
    // message of every slice it is quoted from. If the truncation rule stops
    // running, these move; stage 0 measured that a non-truncating client is
    // wrong in 1,077 of 1,537 messages at depth 25.
    SUBCASE("BTC/USD depth 25 — 25 levels a side, exactly") {
        const ReplayResult r = replay(kD25);
        const DisplaySnapshot& s = r.final_snapshot;
        CHECK(s.bid_count == 25);
        CHECK(s.ask_count == 25);
        CHECK(s.best_bid() == 627912);
        CHECK(s.bids[0].qty == 138258808);
        CHECK(s.best_ask() == 627913);
        CHECK(s.asks[0].qty == 16701217);
        CHECK(s.bids[1].px == 627910);
        CHECK(s.asks[1].px == 627942);
        CHECK(s.bids[24].px == 627779);   // the worst level truncation kept
        CHECK(s.asks[24].px == 628098);
        CHECK(s.spread_ticks() == 1);
    }
    SUBCASE("BTC/USD depth 10 — the same touch, a shallower ladder") {
        const ReplayResult r = replay(kD10);
        const DisplaySnapshot& s = r.final_snapshot;
        CHECK(s.bid_count == 10);
        CHECK(s.ask_count == 10);
        // Two independent captures of the same pair over the same window agree
        // on the touch — which is worth one assertion, because it is the check
        // that the depth parameter changes how DEEP the book is and nothing
        // else about it.
        CHECK(s.best_bid() == 627912);
        CHECK(s.best_ask() == 627913);
        CHECK(s.bids[9].px == 627859);
        CHECK(s.asks[9].px == 628034);
    }
    SUBCASE("BTC/USD depth 100 — the book holds 100, the panel sees 27") {
        const ReplayResult r = replay(kD100);
        // The ADAPTER's ladder holds all 100 (that is the subscription), and
        // DisplaySnapshot carries the top kDisplayLevels. Two different
        // truncations for two different reasons, and only the first is the
        // venue's rule.
        CHECK(r.kraken.checksums_seen == 2472);
        const DisplaySnapshot& s = r.final_snapshot;
        CHECK(s.bid_count == depthcharge::kDisplayLevels);
        CHECK(s.ask_count == depthcharge::kDisplayLevels);
        CHECK(s.best_bid() == 627912);
        CHECK(s.best_ask() == 627913);
    }
    SUBCASE("MINA/GBP — a 4-decimal price scale and a very wide book") {
        const ReplayResult r = replay(kQuiet);
        const DisplaySnapshot& s = r.final_snapshot;
        CHECK(s.bid_count == 25);
        CHECK(s.ask_count == 25);
        CHECK(s.best_bid() == 286);      // 0.0286 GBP at 10^-4
        CHECK(s.best_ask() == 288);
        CHECK(s.bids[0].qty == 7419819125726);   // 74,198.19125726 MINA at 10^-8
        CHECK(s.bids[24].px == 40);      // 0.0040 — the book really is this wide
        CHECK(s.asks[24].px == 6900);
    }
}

TEST_CASE("depth 25 against 27 display rows: the two spare rows are UNKNOWN, not zero") {
    // Stage 0's decision, arriving as a mechanical property. The panel has 27
    // rows a side and the subscription fills 25, so two rows have no data — and
    // "no data" must not render as a level with a size of zero, which is a
    // statement the venue never made (§4: depth beyond N is UNKNOWN, not zero).
    //
    // The mechanism is `bid_count`, not the contents: publish() zero-fills the
    // tail so a shallower book cannot leave the previous frame's rows behind it,
    // and every renderer draws `min(levels, count)` rows. Asserted BOTH ways
    // here, because the zero-fill on its own would be indistinguishable from
    // "two empty levels" to a renderer that trusted the array over the count.
    const ReplayResult r = replay(kD25);
    const DisplaySnapshot& s = r.final_snapshot;

    REQUIRE(depthcharge::kDisplayLevels == 27);
    CHECK(s.bid_count == 25);
    CHECK(s.ask_count == 25);
    CHECK(s.bids[25].px == 0);
    CHECK(s.bids[25].qty == 0);
    CHECK(s.bids[26].qty == 0);
    CHECK(s.asks[25].qty == 0);
    CHECK(s.asks[26].qty == 0);

    // And the console renderer draws 25, not 27 — the row that would carry a
    // zero size is absent, rather than present and empty.
    dc::harness::LadderStyle style{};
    style.color = false;
    style.unicode = false;
    style.levels = depthcharge::kDisplayLevels;
    style.venue = "KRAKEN";
    style.symbol = "BTC/USD";
    const std::string drawn = dc::harness::render_ladder(s, style);
    std::size_t bid_rows = 0;
    std::size_t ask_rows = 0;
    std::istringstream in(drawn);
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("| B ", 0) == 0) { ++bid_rows; }
        if (line.rfind("| A ", 0) == 0) { ++ask_rows; }
    }
    CHECK(bid_rows == 25);
    CHECK(ask_rows == 25);
}

// ---------------------------------------------------------------------------
// DELIVERABLE 2 — the ack, and the absence of one
// ---------------------------------------------------------------------------

TEST_CASE("the extreme slice enters a stream already subscribed") {
    // The trap named in the triage as item 10. This file begins MID-STREAM: its
    // first record is a heartbeat, and it contains no `status`, no subscribe and
    // no ack. An adapter that required an ack would emit nothing here — and this
    // is the only artefact in the repository carrying the 25,843 ms healthy book
    // silence the 2026-08-17 staleness ruling rests on.
    const ReplayResult r = replay(kExtreme);

    CHECK(r.kraken.acks == 0);
    CHECK(r.kraken.parse_errors == 0);
    CHECK(r.kraken.unknown_kind == 0);

    // The records were READ — the heartbeats stamped the liveness clock, which
    // is what the ruling needs from this trace and is entirely independent of
    // whether a book was ever built.
    CHECK(r.kraken.heartbeats == 95);
    CHECK(r.liveness_arrivals == 95);
    CHECK(r.threshold_ms > 0.0);

    // ...and no book was built, deliberately. 49 updates arrived with no
    // snapshot to amend, and applying them to an empty ladder reproduces 0 of 49
    // of the venue's checksums — a different book that merely looks plausible.
    // So they are counted and dropped, and the panel stays honestly grey.
    CHECK(r.kraken.update_frames == 49);
    CHECK(r.kraken.deltas_before_baseline == 82);
    CHECK(r.events == 0);
    CHECK(r.book.snapshots_adopted == 0);
    CHECK_FALSE(r.final_snapshot.live());
    CHECK(r.final_snapshot.stale_reason == GapReason::Resync);
    CHECK(r.final_snapshot.bid_count == 0);

    // The checksums were still carried across, which is what makes B2's resync
    // work an addition rather than a rewrite.
    CHECK(r.kraken.checksums_seen == 49);
}

TEST_CASE("a snapshot after a mid-stream start baselines the book and deltas flow") {
    // The other half of the absence case: entering unsubscribed must not be a
    // permanent state. This is the resync shape B2 builds on, exercised here
    // with the transport doing nothing at all.
    KrakenAdapter adapter(kKrakenBtcUsd, 25);
    std::vector<FeedEvent> events;
    auto sink = [&events](const FeedEvent& ev) { events.push_back(ev); };

    adapter.on_frame(
        R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":62790.0,"qty":1.00000000}],"asks":[],"checksum":7}]})",
        sink);
    CHECK(events.empty());
    CHECK(adapter.stats().deltas_before_baseline == 1);
    CHECK_FALSE(adapter.has_baseline());

    adapter.on_frame(
        R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":62791.0,"qty":2.00000000}],)"
        R"("asks":[{"price":62792.0,"qty":3.00000000}],"checksum":8}]})",
        sink);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == FeedEvent::Kind::Snapshot);
    CHECK(adapter.has_baseline());

    adapter.on_frame(
        R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":62790.0,"qty":1.00000000}],"asks":[],"checksum":9}]})",
        sink);
    REQUIRE(events.size() == 2);
    CHECK(events[1].kind == FeedEvent::Kind::Delta);
    CHECK(events[1].px == 627900);
    CHECK(events[1].qty == 100000000);
    CHECK(events[1].side == Side::Bid);
}

// ---------------------------------------------------------------------------
// DELIVERABLE 1 — the round trip, exactly, over every committed slice
// ---------------------------------------------------------------------------

namespace {

// Slice every `"price":` and `"qty":` token out of a capture's VERBATIM frame
// text. Deliberately a scanner over the raw bytes rather than a JSON parse: the
// property under test is that the wire's own decimal spelling survives, and a
// parse would hand back a re-serialised number, which is the exact substitution
// that fails 2,786 of 2,786 checksums at this venue.
std::size_t for_each_level_token(std::string_view line,
                                 void (*fn)(std::string_view, bool, void*), void* ctx) {
    std::size_t n = 0;
    std::size_t at = 0;
    while (true) {
        const std::size_t p = line.find("\"price\":", at);
        const std::size_t q = line.find("\"qty\":", at);
        const bool is_price = p < q;
        const std::size_t hit = is_price ? p : q;
        if (hit == std::string_view::npos) { break; }
        const std::size_t start = hit + (is_price ? 8 : 6);
        std::size_t end = start;
        while (end < line.size() && (line[end] == '-' || line[end] == '.' ||
                                     (line[end] >= '0' && line[end] <= '9'))) {
            ++end;
        }
        fn(line.substr(start, end - start), is_price, ctx);
        ++n;
        at = end;
    }
    return n;
}

struct RoundTrip {
    const SymbolConfig* cfg = nullptr;
    std::size_t checked = 0;
    std::size_t mismatched = 0;
    std::string first_bad;
};

void check_token(std::string_view tok, bool is_price, void* ctx) {
    auto* rt = static_cast<RoundTrip*>(ctx);
    const int decimals =
        is_price ? rt->cfg->spec.price_decimals : rt->cfg->spec.qty_decimals;

    // string -> ticks, with no float in the path.
    const depthcharge::DecimalParse p = depthcharge::parse_scaled(tok, decimals);
    // ...and back again, integer-only.
    char buf[depthcharge::kMaxFormattedChars] = {};
    const std::size_t n =
        p.ok() ? depthcharge::format_scaled(p.value, decimals, buf, sizeof buf) : 0;

    ++rt->checked;
    if (!p.ok() || n == 0 || std::string_view(buf, n) != tok) {
        ++rt->mismatched;
        if (rt->first_bad.empty()) {
            rt->first_bad = std::string(tok) + " -> " + std::string(buf, n);
        }
    }
}

}  // namespace

TEST_CASE("every price and qty in every slice round-trips string -> ticks -> string") {
    // INVARIANT #3 AS A MEASUREMENT RATHER THAN A RULE. At this venue the CRC32
    // is computed over the decimal text as sent, and stage 0 measured 0 of 2,786
    // checksums surviving a float round-trip — `0.50930100` becomes `0.509301`,
    // and `0.00005100` becomes `5.1e-05`, which the checksum has no spelling for.
    //
    // So the property that matters is not "we avoid doubles", it is that the
    // integer form is LOSSLESS: the text the venue sent is exactly what the
    // scaled integer prints back. That is what lets B2 checksum a book held as
    // integers with no wire text retained anywhere, and it is why this is a
    // property test over all 8,172 level entries rather than a handful of cases.
    struct Slice { const char* name; const SymbolConfig* cfg; std::size_t tokens; };
    const Slice slices[] = {
        {kD10, &kKrakenBtcUsd, 2552},
        {kD25, &kKrakenBtcUsd, 4916},
        {kD100, &kKrakenBtcUsd, 8498},
        {kQuiet, &kKrakenMinaGbp, 214},
        {kExtreme, &kKrakenMinaGbp, 164},
    };

    std::size_t total = 0;
    for (const Slice& s : slices) {
        CAPTURE(s.name);
        std::ifstream in(trace_path(s.name));
        REQUIRE(in.good());
        RoundTrip rt;
        rt.cfg = s.cfg;
        for (std::string line; std::getline(in, line);) {
            for_each_level_token(line, check_token, &rt);
        }
        CAPTURE(rt.first_bad);
        CHECK(rt.mismatched == 0);
        // The token count is pinned too. A scanner that silently found nothing
        // would report zero mismatches, which is the vacuous green this
        // project's own §9 row of 2026-08-18 was written about.
        CHECK(rt.checked == s.tokens);
        total += rt.checked;
    }
    CHECK(total == 16344);   // 8,172 levels x (one price + one qty)
}

// ---------------------------------------------------------------------------
// DELIVERABLE 5 — allocation-free steady state
// ---------------------------------------------------------------------------

TEST_CASE("the Kraken feed path allocates nothing after the first snapshot") {
    // Invariant #7, and the one the brief named as most likely to be broken
    // silently, because a JSON library breaks it for you. There is no library
    // here — kraken_frame_streaming.cpp holds every byte of working state in a
    // stack frame or in the caller's KrakenFrame — so this is the assertion that
    // the claim is structural rather than aspirational.
    //
    // Everything that can allocate is built BEFORE the window: doctest, the
    // trace reader and the std::strings all allocate freely and would drown the
    // signal.
    std::ifstream in(trace_path(kD25));
    REQUIRE(in.good());
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) { lines.push_back(line); }
    REQUIRE(lines.size() == 1600);

    std::vector<std::string_view> frames;
    frames.reserve(lines.size());
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string_view f = dc::harness::slice_frame_json(lines[i]);
        if (!f.empty() && lines[i].find("\"dir\": \"tx\"") == std::string::npos) {
            frames.push_back(f);
        }
    }
    REQUIRE(frames.size() == 1598);

    KrakenAdapter adapter(kKrakenBtcUsd, 25);
    Book book(kKrakenBtcUsd.spec);
    DisplaySnapshot staging{};

    std::size_t events = 0;
    std::size_t snapshots = 0;
    std::size_t baseline = 0;
    bool latched = false;

    for (const std::string_view frame : frames) {
        adapter.on_frame(frame, [&](const FeedEvent& ev) {
            ++events;
            if (ev.kind == FeedEvent::Kind::Snapshot) { ++snapshots; }
            book.apply(ev);
            book.publish(staging);
        });
        if (!latched && snapshots > 0) {
            latched = true;
            baseline = dc::testing::allocation_count();
        }
    }
    const std::size_t allocations = dc::testing::allocation_count() - baseline;

    // Pinned counts, not bounds: zero allocations over a run that quietly
    // stopped after one frame is a vacuous pass.
    CHECK(latched);
    CHECK(snapshots == 1);
    CHECK(events == 2994);
    CHECK(allocations == 0);
}

// ---------------------------------------------------------------------------
// The cases the committed slices cannot exercise
// ---------------------------------------------------------------------------

TEST_CASE("truncation: a level worse than the worst held is refused, not stored") {
    // `levels_outside_depth` reads zero on all five committed slices, so without
    // this case it would be an unexercised counter presented as coverage — the
    // condition ARCHITECTURE §9 (2026-08-18) forbids. Provoked synthetically for
    // the same reason and in the same shape as the FrameReassembler tests.
    KrakenAdapter adapter(kKrakenBtcUsd, 2);   // depth 2, so the edge is reachable
    std::vector<FeedEvent> events;
    auto sink = [&events](const FeedEvent& ev) { events.push_back(ev); };

    adapter.on_frame(
        R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":100.0,"qty":1.00000000},{"price":99.0,"qty":1.00000000}],)"
        R"("asks":[],"checksum":1}]})",
        sink);
    REQUIRE(events.size() == 1);
    CHECK(adapter.bid_count() == 2);

    SUBCASE("a level outside the window is dropped and emits nothing") {
        adapter.on_frame(
            R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
            R"("bids":[{"price":98.0,"qty":5.00000000}],"asks":[],"checksum":2}]})",
            sink);
        CHECK(events.size() == 1);                       // nothing new
        CHECK(adapter.stats().levels_outside_depth == 1);
        CHECK(adapter.bid_count() == 2);
    }

    SUBCASE("a level INSIDE the window inserts and evicts the worst, in that order") {
        adapter.on_frame(
            R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
            R"("bids":[{"price":99.5,"qty":5.00000000}],"asks":[],"checksum":2}]})",
            sink);
        REQUIRE(events.size() == 3);
        CHECK(events[1].kind == FeedEvent::Kind::Delta);
        CHECK(events[1].px == 995);
        CHECK(events[1].qty == 500000000);
        // THE EVICTION. Kraken sends no removal for it — that is the whole
        // truncation rule — so if the adapter does not say it, the engine's book
        // keeps a level the venue has stopped mentioning.
        CHECK(events[2].kind == FeedEvent::Kind::Delta);
        CHECK(events[2].px == 990);
        CHECK(events[2].qty == 0);
        CHECK(adapter.stats().levels_evicted == 1);
        CHECK(adapter.bid_count() == 2);
    }

    SUBCASE("a removal for a level never held is counted, not emitted") {
        adapter.on_frame(
            R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
            R"("bids":[{"price":50.0,"qty":0.00000000}],"asks":[],"checksum":2}]})",
            sink);
        CHECK(events.size() == 1);
        CHECK(adapter.stats().levels_outside_depth == 1);
    }

    SUBCASE("a level re-sent at the quantity we hold emits nothing") {
        adapter.on_frame(
            R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
            R"("bids":[{"price":100.0,"qty":1.00000000}],"asks":[],"checksum":2}]})",
            sink);
        CHECK(events.size() == 1);
        CHECK(adapter.stats().levels_unchanged == 1);
    }
}

TEST_CASE("a price or size the declared scale cannot hold is reported, never rounded") {
    // ARCHITECTURE §4's declare-and-verify rule, at the venue that publishes its
    // own metadata. A wrong scale does not fail — it draws — so the only safe
    // answer is a loud one.
    KrakenAdapter adapter(kKrakenBtcUsd, 25);   // price 10^-1, qty 10^-8
    std::size_t events = 0;
    auto sink = [&events](const FeedEvent&) { ++events; };

    SUBCASE("a price with more precision than the scale") {
        adapter.on_frame(
            R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
            R"("bids":[{"price":62807.05,"qty":1.00000000}],"asks":[],"checksum":1}]})",
            sink);
        CHECK(adapter.last_status() == ParseStatus::BadPrice);
        CHECK(adapter.stats().price_errors == 1);
        CHECK(events == 0);
    }
    SUBCASE("a size with more precision than the scale") {
        adapter.on_frame(
            R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
            R"("bids":[{"price":62807.0,"qty":1.000000001}],"asks":[],"checksum":1}]})",
            sink);
        CHECK(adapter.last_status() == ParseStatus::BadQty);
        CHECK(adapter.stats().qty_errors == 1);
    }
    SUBCASE("exponent notation is refused rather than silently re-read") {
        // The book channel has never sent one in 9,932 frames; the `instrument`
        // channel does (`1e-08`, `5e-05`). The scanner accepts the SYNTAX so the
        // frame is not corrupt, and parse_scaled refuses the VALUE — which is
        // the honest answer, because the declared scale cannot hold it exactly.
        adapter.on_frame(
            R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
            R"("bids":[{"price":62807.0,"qty":5.1e-05}],"asks":[],"checksum":1}]})",
            sink);
        CHECK(adapter.last_status() == ParseStatus::BadQty);
    }
    SUBCASE("a frame for another pair is neither an error nor an event") {
        adapter.on_frame(
            R"({"channel":"book","type":"update","data":[{"symbol":"ETH/USD",)"
            R"("bids":[{"price":3000.0,"qty":1.00000000}],"asks":[],"checksum":1}]})",
            sink);
        CHECK(adapter.last_status() == ParseStatus::OtherSymbol);
        CHECK(adapter.stats().other_symbol == 1);
        CHECK(events == 0);
    }
    SUBCASE("`data` is iterated, not indexed at [0]") {
        // Nothing in any committed slice would catch an adapter reading data[0]:
        // every message holds exactly one entry. A subscribe may name several
        // symbols on one socket (six measured at stage 0), and the entry that is
        // ours would then not be first.
        adapter.on_frame(
            R"({"channel":"book","type":"snapshot","data":[)"
            R"({"symbol":"ETH/USD","bids":[{"price":3000.0,"qty":1.00000000}],)"
            R"("asks":[],"checksum":1},)"
            R"({"symbol":"BTC/USD","bids":[{"price":62807.0,"qty":1.00000000}],)"
            R"("asks":[],"checksum":2}]})",
            sink);
        CHECK(adapter.last_status() == ParseStatus::Ok);
        CHECK(events == 1);
        CHECK(adapter.bid_count() == 1);
        CHECK(adapter.last_checksum() == 2);
    }
}

TEST_CASE("a malformed frame is dropped without allocating and without throwing") {
    KrakenAdapter adapter(kKrakenBtcUsd, 25);
    std::size_t events = 0;
    auto sink = [&events](const FeedEvent&) { ++events; };

    const char* corpus[] = {
        "",
        "{",
        "not json at all",
        R"({"channel":)",
        R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD","bids":[{)",
        R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":1.0}],"asks":[],"checksum":1}]})",   // a level with no qty
        R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":1.0,"qty":1.0,}],"asks":[]}]})",     // trailing comma
        R"([1,2,3])",
    };

    const std::size_t before = dc::testing::allocation_count();
    for (const char* frame : corpus) { adapter.on_frame(frame, sink); }
    const std::size_t after = dc::testing::allocation_count();

    CHECK(after == before);
    CHECK(events == 0);
    CHECK(adapter.stats().frames_in == 8);
    // The postcondition kraken_frame.hpp states: a non-Ok status leaves the
    // frame empty, so a caller that ignores the status cannot read half a book
    // out of it.
    CHECK(adapter.bid_count() == 0);
    CHECK_FALSE(adapter.has_baseline());
}

TEST_CASE("the status frame's connection_id is never parsed as a number") {
    // 15113583034416118705 exceeds 2^53, so a parser that treated every JSON
    // number as a double would corrupt it silently — the same lesson as the
    // level text one rung up, and the reason the skipper validates syntax
    // without ever converting.
    KrakenAdapter adapter(kKrakenBtcUsd, 25);
    std::size_t events = 0;
    auto sink = [&events](const FeedEvent&) { ++events; };
    adapter.on_frame(
        R"({"channel":"status","type":"update","data":[{"version":"2.0.10",)"
        R"("system":"online","api_version":"v2","connection_id":15113583034416118705}]})",
        sink);
    CHECK(adapter.last_status() == ParseStatus::Ok);
    CHECK(adapter.stats().status_frames == 1);
    CHECK(events == 0);
}

TEST_CASE("a transport gap drops the baseline as well as greying the panel") {
    KrakenAdapter adapter(kKrakenBtcUsd, 25);
    std::vector<FeedEvent> events;
    auto sink = [&events](const FeedEvent& ev) { events.push_back(ev); };

    adapter.on_frame(
        R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":100.0,"qty":1.00000000}],"asks":[],"checksum":1}]})",
        sink);
    REQUIRE(adapter.has_baseline());

    adapter.on_transport_gap(GapReason::Disconnect, sink);
    REQUIRE(events.size() == 2);
    CHECK(events[1].kind == FeedEvent::Kind::Gap);
    // Deltas applied across a hole onto a pre-hole ladder are the same "different
    // book that looks plausible" as deltas applied before any baseline.
    CHECK_FALSE(adapter.has_baseline());
    CHECK(adapter.bid_count() == 0);

    adapter.on_frame(
        R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":100.0,"qty":2.00000000}],"asks":[],"checksum":2}]})",
        sink);
    CHECK(events.size() == 2);
    CHECK(adapter.stats().deltas_before_baseline == 1);
}

// ---------------------------------------------------------------------------
// DELIVERABLE 7 — the queue-vs-shed premise, RECORDED rather than assumed
// ---------------------------------------------------------------------------

TEST_CASE("age_ms runs at Kraken tonight on an UNPROVEN premise — B2 owns closing it") {
    // ============================================================================
    // THIS TEST ASSERTS AN ASSUMPTION, NOT A BEHAVIOUR, AND THAT IS DELIBERATE.
    // ============================================================================
    //
    // `age_estimator.hpp` is venue-free arithmetic: over a window of wall time W
    // the venue's liveness signal was emitted W/interval times, and a deficit
    // against that is the book's queuing lag. Tonight that code runs at Kraken
    // for the first time, and the number it produces appears in the header.
    //
    // **A deficit is an AGE only if the venue QUEUES. If it SHEDS, the identical
    // deficit appears and there is no lag at all** — rate alone cannot tell the
    // two apart. At Anvil the premise is measured and now contractual:
    // `CrowWsSubscriber::deliver()` appends to an unbounded per-connection buffer
    // with no cap, no drop rule and no coalescing, and
    // `tools/anvil_freshness_probe.py` confirmed it end to end on 2026-08-11 —
    // a socket throttled to 25% saw every frame kind thinned by the same fraction
    // and its lag grow linearly to 111 s with no plateau.
    //
    // **At Kraken it is neither measured nor stated.** Nothing in this repository
    // says what Kraken does to a slow consumer, and the five committed slices
    // cannot answer it: every one of them was captured by a client that kept up,
    // and a feed nobody fell behind on produces a zero deficit under BOTH
    // hypotheses. That is why this is a pinned assumption and not a golden — no
    // trace can be the evidence, so the honest artefact is a named debt.
    //
    // Three options were available and two were refused:
    //
    //   * SPECIAL-CASE KRAKEN — suppress the number at this venue. Refused: it
    //     hides a quantity that is probably right, and a suppressed field is not
    //     revisited, whereas a wrong one is.
    //   * CAVEAT IT IN A COMMENT and carry it to M5. Refused: an assumption with
    //     no owner is how an estimate becomes believed (the same reasoning the
    //     triage applied to the client ping, decision (a)).
    //   * PIN IT HERE, NAMING THE STAGE THAT CLOSES IT. Taken.
    //
    // **B2 IS THE OWNER, AND IT CLOSES THIS AS A SIDE EFFECT OF WORK IT IS ALREADY
    // DOING.** B2 builds the CRC path. A SHED update breaks the checksum — the
    // book we hold would be missing a level the venue's book has, and the CRC32
    // covers the top 10 — while a QUEUED update arrives late and checksums
    // clean. So:
    //
    //     induce backpressure on a Kraken socket, then read the CRC.
    //     mismatches => the venue sheds, and `age_ms` at Kraken is meaningless.
    //     clean checksums under a growing deficit => the venue queues, and the
    //     deficit is an age.
    //
    // That converts an open assumption into a measurement rather than a caveat
    // carried to M5, and it needs no new instrument: B2 has to build the CRC
    // check anyway, and the throttled-capture mode already exists in
    // `tools/capture_kraken.py`'s drain support.
    //
    // What this test can honestly assert today is the SHAPE of the claim: that
    // the meter runs, that it reads the noise floor on a healthy feed, and that
    // its baseline is the heartbeat cadence rather than anything venue-specific
    // — so that when B2 measures, there is a recorded before.

    const ReplayResult r = replay(kD25);

    // 1. The meter ran at Kraken, off the heartbeat, with nothing special-cased.
    CHECK(r.liveness_arrivals == 59);
    CHECK(r.final_snapshot.has_age);

    // 2. Its baseline is Kraken's 1 Hz heartbeat, learned from this connection
    //    and hardcoded nowhere. Anvil's is 500 ms; the same code found both.
    CHECK(r.age_baseline_ms == doctest::Approx(1000.0).epsilon(0.01));
    CHECK(r.liveness_median_ms == doctest::Approx(1000.0).epsilon(0.01));

    // 3. On a feed nobody fell behind on it reads the sawtooth noise floor —
    //    one heartbeat period, which is the resolution of the instrument and not
    //    a lag. THIS IS THE READING THAT IS IDENTICAL UNDER BOTH HYPOTHESES,
    //    which is precisely why these slices cannot settle the question.
    CHECK(r.worst_age_ms <= 1100);

    // 4. And the same on the quiet pair, whose 25.8 s book hole moves it not at
    //    all — the ruling's whole point, and independent of queue-vs-shed.
    const ReplayResult q = replay(kExtreme);
    CHECK(q.worst_age_ms <= 1100);
    CHECK(q.liveness_arrivals == 95);

    // 5. THE DEBT, PINNED. If a future session deletes this line, it is deleting
    //    the record that the number above rests on an unmeasured venue property.
    MESSAGE("OWED BY M4 STAGE B2: Kraken's queue-vs-shed behaviour is UNMEASURED. "
            "age_ms is reported at this venue on Anvil's premise. The experiment is "
            "CRC mismatch under induced backpressure: shed updates break the "
            "checksum, queued ones do not.");
}

// ---------------------------------------------------------------------------
// DELIVERABLE 4 — the dispatch must prove it dispatched right
// ---------------------------------------------------------------------------

TEST_CASE("decoder identity travels with the pinned output") {
    // TWO ADAPTERS, ONE READER, AND UNTIL B1 NOTHING TIED A PINNED OUTPUT TO THE
    // CODE THAT PRODUCED IT. Every count in this file would still be a count if
    // `run_replay`'s switch sent Kraken's traces to Anvil's decoder — a
    // different set of numbers, equally plausible, and nobody reading the report
    // would know which parser wrote them.
    //
    // `decoder` is not a label the test supplies. It comes from `Decoder::name()`,
    // which is derived from the decoder's own `kVenue` (trace_decoder.hpp), so
    // it cannot read "kraken" while the dispatch is wrong.
    //
    // ============================================================================
    // THE MUTATION WAS RUN, NOT MERELY DESCRIBED (ARCHITECTURE §9, 2026-08-18)
    // ============================================================================
    //
    // On 2026-08-18 `run_replay`'s `case Venue::Kraken` was edited to construct
    // `Replay<AnvilTraceDecoder>` instead, the tree was rebuilt, and this file
    // was run. Result: RED, and `CHECK(r.decoder == "kraken")` was the FIRST
    // assertion to fail on every one of the five slices — before the counts,
    // which also moved. The mutation was then reverted and the suite is green.
    //
    // That ordering is the reason the field exists rather than relying on the
    // counts alone: the counts say "these numbers are not the numbers", and the
    // identity says which decoder produced them. A future reader who breaks the
    // dispatch gets a cause, not a diff.
    for (const char* name : {kD10, kD25, kD100, kQuiet, kExtreme}) {
        CAPTURE(name);
        const ReplayResult r = replay(name);
        CHECK(r.decoder == "kraken");
        // ...and the OTHER venue's counters are untouched, which is the second
        // half of the same claim: a run cannot half-populate both.
        CHECK(r.adapter.frames_in == 0);
        CHECK(r.adapter.events_out == 0);
        CHECK(r.kraken.frames_in > 0);
    }

    // The Anvil side of the pin, so the identity is not trivially constant.
    dc::harness::TraceReader anvil(std::string(DC_REPLAY_DIR) +
                                   "/anvil_101_feederoff_20260817.ndjson");
    const ReplayResult a =
        dc::harness::run_replay(anvil, dc::harness::symbol_for(anvil.meta()),
                                ReplayOptions{});
    CHECK(a.decoder == "anvil");
    CHECK(a.kraken.frames_in == 0);
    CHECK(a.adapter.frames_in > 0);
}

// ---------------------------------------------------------------------------
// REGRESSION — the snapshot path must truncate too
// ---------------------------------------------------------------------------

TEST_CASE("a snapshot DEEPER than the subscription is truncated, not stored whole") {
    // FOUND AT REVIEW, ON 2026-08-18, AFTER ALL FIVE SLICES WERE GREEN.
    //
    // `adopt_snapshot` seeded every level the frame carried and capped only at
    // the staging buffer's 256 rather than at the subscribed depth. The five
    // committed slices could not show it: Kraken serves exactly the depth
    // requested (10/10, 25/25, 100/100 measured at stage 0), so the frame never
    // carried more than `depth_` and the two caps coincided on every file. The
    // goldens were green and the truncation rule was half-implemented.
    //
    // That is this milestone's recurring failure in its purest form — a green
    // check that could not go red, because the input that would break it does
    // not occur in the corpus. So the regression is synthetic BY NECESSITY, and
    // the two routes that reach it in production are real:
    //
    //   * a capture whose metadata records no `depth` falls back to the
    //     firmware's constant while the file may be a depth-100 slice; and
    //   * `ack_depth_mismatch` exists precisely because a venue may serve a
    //     depth other than the one asked for — Anvil already rounds UP, and
    //     NOTES-kraken.md requires this adapter to "assume neither".
    //
    // The consequence is not cosmetic: a ladder holding levels below the
    // subscribed depth is the non-truncating client stage 0 measured as wrong in
    // 1,077 of 1,537 messages at depth 25.
    KrakenAdapter adapter(kKrakenBtcUsd, 2);
    std::vector<FeedEvent> events;
    auto sink = [&events](const FeedEvent& ev) { events.push_back(ev); };

    adapter.on_frame(
        R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD","bids":[)"
        R"({"price":100.0,"qty":1.00000000},{"price":99.0,"qty":1.00000000},)"
        R"({"price":98.0,"qty":1.00000000},{"price":97.0,"qty":1.00000000},)"
        R"({"price":96.0,"qty":1.00000000}],"asks":[],"checksum":1}]})",
        sink);

    CHECK(adapter.bid_count() == 2);
    CHECK(adapter.stats().levels_deeper_than_subscribed == 3);
    // The Snapshot event carries the TRUNCATED span, so the engine's book is a
    // mirror of the subscription rather than of the frame.
    REQUIRE(events.size() == 1);
    CHECK(events[0].bids.size == 2);
    CHECK(events[0].bids[0].px == 1000);
    CHECK(events[0].bids[1].px == 990);

    // ...and the ladder is still correctly ordered afterwards, which is the part
    // a count alone would not catch: truncating by dropping the wrong end, or by
    // leaving `count` past the real data, both give bid_count() == 2.
    adapter.on_frame(
        R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":99.5,"qty":9.00000000}],"asks":[],"checksum":2}]})",
        sink);
    REQUIRE(events.size() == 3);
    CHECK(events[1].px == 995);            // inserted
    CHECK(events[2].px == 990);            // and 99.0 evicted, not 96.0
    CHECK(events[2].qty == 0);
    CHECK(adapter.bid_count() == 2);
}

TEST_CASE("an ack that serves a different depth than we asked for is counted") {
    // `ack_depth_mismatch` had no test until review: a counter nothing exercises
    // is not coverage (ARCHITECTURE §9, 2026-08-18). It matters because the
    // ADAPTER's depth is what the ladder truncates at, so a silent disagreement
    // with the venue is a book that is a different shape from the one the
    // checksum is computed over.
    KrakenAdapter adapter(kKrakenBtcUsd, 25);
    auto sink = [](const FeedEvent&) {};

    adapter.on_frame(
        R"({"method":"subscribe","result":{"channel":"book","depth":100,)"
        R"("snapshot":true,"symbol":"BTC/USD"},"success":true})",
        sink);
    CHECK(adapter.subscribe_state() == KrakenAdapter::SubscribeState::Subscribed);
    CHECK(adapter.stats().ack_depth_mismatch == 1);

    KrakenAdapter agreed(kKrakenBtcUsd, 25);
    agreed.on_frame(
        R"({"method":"subscribe","result":{"channel":"book","depth":25,)"
        R"("snapshot":true,"symbol":"BTC/USD"},"success":true})",
        sink);
    CHECK(agreed.stats().ack_depth_mismatch == 0);
}

TEST_CASE("a depth deeper than the staging buffer is clamped, and says so") {
    // The 500 and 1000 tiers Kraken offers exceed kMaxSnapshotLevels (256). The
    // compile-time constant is static_asserted against that; a RUNTIME depth
    // read from a capture's metadata cannot be, so it clamps — and `depth()`
    // reports what it actually got, because the alternative is a book that is
    // quietly short with a clean top-10 checksum on every message.
    KrakenAdapter deep(kKrakenBtcUsd, 1000);
    CHECK(deep.depth() == static_cast<std::int32_t>(depthcharge::kMaxSnapshotLevels));

    // A trace that recorded no depth at all falls back to the firmware constant
    // rather than to "unlimited", which is the fallback that would silently
    // disable truncation.
    KrakenAdapter none(kKrakenBtcUsd, 0);
    CHECK(none.depth() == depthcharge::kraken::kKrakenSubscribeDepth);
}
