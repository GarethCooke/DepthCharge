// test_kraken_adapter.cpp — the second adapter, end to end (M4 stages B1, B2).
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
// B2: THE MEASUREMENT ABOVE IS NOW ALSO A CODE PATH, AND THE TWO AGREE
// ============================================================================
//
// At B1 the 4,878 figure was desk evidence used to DERIVE the goldens below and
// nothing more — `KrakenAdapter` computed no CRC32 and this file asserted none.
// B2 makes the same comparison inside the shipping adapter, and it reproduces
// the same number: **839 + 1,537 + 2,472 + 30 = 4,878 / 4,878 matched, 0
// failed**, from integer ticks, with no wire text retained anywhere.
//
// That is a second independent implementation agreeing with the first — Python
// over stored decimal TEXT versus C++ over scaled INTEGERS, two languages and
// two representations, which is a sharper independence than the eviction counts
// above because the two routes to the token are different arithmetic rather
// than the same arithmetic written twice.
//
// The fifth slice reads **49 unverifiable, 0 failed**, which is the answer that
// distinguishes an honest adapter from a loud one: it has no opening snapshot,
// so there is no book to compare, and comparing anyway would produce 49
// mismatches that are facts about the file rather than about the wire.
//
// WHAT IS STILL DELIBERATELY NOT HERE: a golden for a CRC MISMATCH. No capture
// contains one and none can be provoked from outside, so the coverage for that
// path is synthetic by necessity — see the healing-path case at the end of this
// file, and ARCHITECTURE §9 (2026-08-18) on why a synthetic case is mandatory
// rather than second-best where an assumption and a venue's behaviour coincide.
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
// M4 stage B2: one connection, two subscriptions, a real mid-stream snapshot.
const char* kResync = "kraken_minagbp_d25_resync_20260818";

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
        // B2: THE VENUE'S OWN VERDICT ON OUR BOOK, on every message.
        CHECK(r.kraken.checksums_matched == 839);
        CHECK(r.kraken.checksums_failed == 0);
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
        CHECK(r.kraken.checksums_matched == 1537);
        CHECK(r.kraken.checksums_failed == 0);
    }
    SUBCASE("BTC/USD depth 100") {
        const ReplayResult r = replay(kD100);
        CHECK(r.decoder == "kraken");
        CHECK(r.frames == 2535);
        CHECK(r.kraken.update_frames == 2471);
        CHECK(r.kraken.levels_evicted == 1156);  // stage 0's figure
        CHECK(r.kraken.levels_removed == 1047);
        CHECK(r.kraken.checksums_seen == 2472);
        CHECK(r.kraken.checksums_matched == 2472);
        CHECK(r.kraken.checksums_failed == 0);
    }
    SUBCASE("MINA/GBP depth 25 — the quiet pair") {
        const ReplayResult r = replay(kQuiet);
        CHECK(r.decoder == "kraken");
        CHECK(r.frames == 93);
        CHECK(r.kraken.update_frames == 29);
        CHECK(r.kraken.heartbeats == 60);
        CHECK(r.kraken.levels_evicted == 19);    // stage 0's figure
        CHECK(r.kraken.price_errors == 0);       // 10^-4, a different scale
        CHECK(r.kraken.checksums_matched == 30);
        CHECK(r.kraken.checksums_failed == 0);
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
        // AND ITS 49 CHECKSUMS ARE UNVERIFIABLE, NOT FAILED. The distinction is
        // the whole reason this slice is in `NOT_A_CHECKSUM_GOLDEN`: with no
        // opening snapshot there is no book to compare, and an adapter that
        // compared anyway would report 49 mismatches, grey the panel, and blame
        // the wire for a fact about the file. 0/49 is the right answer and it is
        // spelled as three counters rather than as one ratio.
        CHECK(r.kraken.checksums_seen == 49);
        CHECK(r.kraken.checksums_unverifiable == 49);
        CHECK(r.kraken.checksums_matched == 0);
        CHECK(r.kraken.checksums_failed == 0);
        CHECK(r.kraken.resyncs_requested == 0);
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
    // permanent state. This is the resync shape, exercised with the transport
    // doing nothing at all.
    //
    // B2 gave the two post-baseline frames their REAL checksums — 298657013 and
    // 1051150574, computed by zlib.crc32 outside this build — so the case now
    // runs the whole path rather than the ladder half of it. The first frame
    // keeps an arbitrary one on purpose: it arrives with no baseline, so it is
    // the `checksums_unverifiable` case, and a value that would fail if it were
    // ever compared is the right value to leave there.
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
    CHECK(adapter.stats().checksums_unverifiable == 1);
    CHECK(adapter.stats().checksums_failed == 0);

    adapter.on_frame(
        R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":62791.0,"qty":2.00000000}],)"
        R"("asks":[{"price":62792.0,"qty":3.00000000}],"checksum":298657013}]})",
        sink);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == FeedEvent::Kind::Snapshot);
    CHECK(adapter.has_baseline());
    CHECK(adapter.stats().checksums_matched == 1);

    adapter.on_frame(
        R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":62790.0,"qty":1.00000000}],"asks":[],"checksum":1051150574}]})",
        sink);
    REQUIRE(events.size() == 2);
    CHECK(events[1].kind == FeedEvent::Kind::Delta);
    CHECK(events[1].px == 627900);
    CHECK(events[1].qty == 100000000);
    CHECK(events[1].side == Side::Bid);
    CHECK(adapter.stats().checksums_matched == 2);
    CHECK(adapter.stats().checksums_failed == 0);
    CHECK_FALSE(adapter.resync_wanted());
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
    //
    // ========================================================================
    // NO CHECKSUM ON THESE FRAMES, AND IT IS NOT LAZINESS (B2).
    // ========================================================================
    //
    // A synthetic frame that carries a checksum is making a claim about the
    // book, and from B2 the adapter checks that claim. There are two ways to
    // satisfy it and only one is available here.
    //
    // The subscription below is DEPTH 2, which is not a depth Kraken offers —
    // it is a synthetic depth chosen because it is the only way to reach the
    // truncation edge in a few lines. Kraken's CRC32 covers its own top TEN
    // levels, so against a depth-2 book there is no checksum the venue could
    // have sent that our two levels would reproduce: the correct value for this
    // fixture does not exist rather than being tedious to compute. Leaving a
    // placeholder would make every frame here a deliberate corruption and turn
    // a truncation test into a healing-path test.
    //
    // So they carry none, `book_msgs_unchecksummed` counts that fact, and the
    // slice goldens pin it at zero — which is what stops "no checksum" from
    // becoming a silent way to opt out of the check.
    KrakenAdapter adapter(kKrakenBtcUsd, 2);   // depth 2, so the edge is reachable
    std::vector<FeedEvent> events;
    auto sink = [&events](const FeedEvent& ev) { events.push_back(ev); };

    adapter.on_frame(
        R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":100.0,"qty":1.00000000},{"price":99.0,"qty":1.00000000}],)"
        R"("asks":[]}]})",
        sink);
    REQUIRE(events.size() == 1);
    CHECK(adapter.bid_count() == 2);
    CHECK(adapter.stats().book_msgs_unchecksummed == 1);

    SUBCASE("a level outside the window is dropped and emits nothing") {
        adapter.on_frame(
            R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
            R"("bids":[{"price":98.0,"qty":5.00000000}],"asks":[]}]})",
            sink);
        CHECK(events.size() == 1);                       // nothing new
        CHECK(adapter.stats().levels_outside_depth == 1);
        CHECK(adapter.bid_count() == 2);
    }

    SUBCASE("a level INSIDE the window inserts and evicts the worst, in that order") {
        adapter.on_frame(
            R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
            R"("bids":[{"price":99.5,"qty":5.00000000}],"asks":[]}]})",
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
            R"("bids":[{"price":50.0,"qty":0.00000000}],"asks":[]}]})",
            sink);
        CHECK(events.size() == 1);
        CHECK(adapter.stats().levels_outside_depth == 1);
    }

    SUBCASE("a level re-sent at the quantity we hold emits nothing") {
        adapter.on_frame(
            R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
            R"("bids":[{"price":100.0,"qty":1.00000000}],"asks":[]}]})",
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
        // The two entries carry DIFFERENT checksums, and only the second one is
        // ours. 3483870407 is the real CRC32 of a book holding just our level;
        // 1 is not the CRC of anything. So an adapter that took the checksum
        // from data[0] would not merely pick the wrong number — it would fail
        // the comparison and grey the panel, which is what the last two
        // assertions here are checking did not happen.
        adapter.on_frame(
            R"({"channel":"book","type":"snapshot","data":[)"
            R"({"symbol":"ETH/USD","bids":[{"price":3000.0,"qty":1.00000000}],)"
            R"("asks":[],"checksum":1},)"
            R"({"symbol":"BTC/USD","bids":[{"price":62807.0,"qty":1.00000000}],)"
            R"("asks":[],"checksum":3483870407}]})",
            sink);
        CHECK(adapter.last_status() == ParseStatus::Ok);
        CHECK(events == 1);
        CHECK(adapter.bid_count() == 1);
        CHECK(adapter.last_checksum() == 3483870407u);
        CHECK(adapter.stats().checksums_matched == 1);
        CHECK_FALSE(adapter.resync_wanted());
    }
    SUBCASE("`data` is iterated, and OUR entry is not the last one either") {
        // The mirror image of the case above, and it is not symmetry for its own
        // sake. B2 made the checksum's PROVENANCE load-bearing — a value taken
        // from the wrong entry now greys the panel rather than merely being an
        // unread number — and a parser that latched the LAST entry's checksum
        // instead of the matched one would pass the case above and fail here.
        // Both orderings are one `if` apart and only one of them was tested.
        adapter.on_frame(
            R"({"channel":"book","type":"snapshot","data":[)"
            R"({"symbol":"BTC/USD","bids":[{"price":62807.0,"qty":1.00000000}],)"
            R"("asks":[],"checksum":3483870407},)"
            R"({"symbol":"ETH/USD","bids":[{"price":3000.0,"qty":1.00000000}],)"
            R"("asks":[],"checksum":1}]})",
            sink);
        CHECK(adapter.last_status() == ParseStatus::Ok);
        CHECK(events == 1);
        CHECK(adapter.bid_count() == 1);
        CHECK(adapter.last_checksum() == 3483870407u);
        CHECK(adapter.stats().checksums_matched == 1);
        CHECK(adapter.stats().checksums_failed == 0);
        CHECK_FALSE(adapter.resync_wanted());
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
    // Checksums omitted: this case is about the gap, and the frames are not
    // making a claim about the book. See the truncation case above for the rule.
    KrakenAdapter adapter(kKrakenBtcUsd, 25);
    std::vector<FeedEvent> events;
    auto sink = [&events](const FeedEvent& ev) { events.push_back(ev); };

    adapter.on_frame(
        R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":100.0,"qty":1.00000000}],"asks":[]}]})",
        sink);
    REQUIRE(adapter.has_baseline());

    adapter.on_transport_gap(GapReason::Disconnect, sink);
    REQUIRE(events.size() == 2);
    CHECK(events[1].kind == FeedEvent::Kind::Gap);
    // Deltas applied across a hole onto a pre-hole ladder are the same "different
    // book that looks plausible" as deltas applied before any baseline.
    CHECK_FALSE(adapter.has_baseline());
    CHECK(adapter.bid_count() == 0);

    // AND IT DOES NOT ASK FOR A RESUBSCRIBE (B2). A reconnect subscribes on its
    // own, so the flag would be a duplicate request; the CRC path is the only
    // one that needs it, because there the socket never blinks. Asserted here
    // rather than only in the CRC test, because the two paths share `drop_book`
    // and the difference between them is exactly this line.
    CHECK_FALSE(adapter.resync_wanted());
    CHECK(adapter.stats().resyncs_requested == 0);

    adapter.on_frame(
        R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
        R"("bids":[{"price":100.0,"qty":2.00000000}],"asks":[]}]})",
        sink);
    CHECK(events.size() == 2);
    CHECK(adapter.stats().deltas_before_baseline == 1);
}

// ---------------------------------------------------------------------------
// DELIVERABLE 7 — the queue-vs-shed premise, RECORDED rather than assumed
// ---------------------------------------------------------------------------

TEST_CASE("age_ms at Kraken rests on a MEASURED premise: this venue queues") {
    // ============================================================================
    // B1 PINNED AN ASSUMPTION HERE AND NAMED B2 AS ITS OWNER. B2 MEASURED IT.
    // THE DEBT IS DISCHARGED, AND THE ANSWER IS THE ONE THE NUMBER NEEDED.
    // ============================================================================
    //
    // **Kraken QUEUES.** Measured 2026-08-18 by `tools/kraken_backpressure_probe.py`
    // — one connection, BTC/USD depth 25, 80 s in three phases — and three
    // independent signals agree:
    //
    //   * **331 / 331 checksums clean** through the throttled phase. A SHED
    //     update leaves a level wrong and the venue's own CRC32 says so within a
    //     message or two; not one failed. Nothing was lost.
    //   * **Lag grew +0.690 s per second**, from the venue's own microsecond
    //     `timestamp` on every book message, reaching 26.0 s. Everything was
    //     late.
    //   * **On release the backlog drained at 64.2 msg/s against a 19.5 msg/s
    //     baseline** and the lag decayed at -1.411 s/s back to where it started.
    //     A server that had shed would have had nothing to catch up with.
    //
    // The arithmetic closes on itself, which is the check that makes it more
    // than three plausible columns: a slope of 0.690 means we drained at 31% of
    // the venue's rate, so the venue was producing 8.63 / 0.31 = ~27.8 msg/s —
    // and the release phase independently implies ~27 msg/s from the volume it
    // had to clear. Two routes to the venue's true rate, neither derived from
    // the other.
    //
    // So the deficit `age_estimator.hpp` computes IS a queuing lag at this venue,
    // and `age_ms` stands. The three-outcome table in the M4 stage B2 brief is
    // answered on row one; rows two (shed) and three (disconnected) did not
    // occur — the socket survived a 26 s backlog without closing.
    //
    // WHAT THE MEASUREMENT DOES NOT SAY, stated so it is not over-read: the
    // lever is TCP backpressure, so what was measured is the whole path's
    // behaviour and not a statement about Kraken's internal send queue. That is
    // the right question for `age_ms` — the panel experiences the path — but it
    // is not a contract, and unlike Anvil's (whose `CrowWsSubscriber::deliver()`
    // is readable source) it could change without notice.
    //
    // THE ASSERTIONS BELOW ARE UNCHANGED AND STILL ASSERT THE SHAPE OF THE
    // CLAIM, NOT THE MEASUREMENT.
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
    // **B2 WAS THE OWNER AND IT CLOSED THIS AS A SIDE EFFECT OF WORK IT WAS
    // ALREADY DOING** — the reasoning below is kept verbatim because it is the
    // design of the experiment that ran. B2 builds the CRC path. A SHED update
    // breaks the checksum — the
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

    // 5. THE DEBT, DISCHARGED. The line B1 left here said this number rested on
    //    an unmeasured venue property. It no longer does, and what replaced it
    //    is a measurement rather than a deletion — the experiment is repeatable
    //    with one command and the tool prints the same verdict logic every time.
    MESSAGE("CLOSED BY M4 STAGE B2 (2026-08-18): Kraken QUEUES. "
            "331/331 checksums clean under a throttled socket while the venue's own "
            "timestamp showed lag growing 0.690 s/s to 26.0 s, and the backlog then "
            "drained at 3.3x the baseline rate on release. The deficit is an age. "
            "Re-run: tools/kraken_backpressure_probe.py --delay-ms 120");
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
        R"({"price":96.0,"qty":1.00000000}],"asks":[]}]})",
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
        R"("bids":[{"price":99.5,"qty":9.00000000}],"asks":[]}]})",
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

// ---------------------------------------------------------------------------
// M4 STAGE B2 — THE HEALING PATH
// ---------------------------------------------------------------------------

namespace {

// The healing-path fixture, spelled as a book rather than as opaque JSON so the
// checksums beside it are readable. Prices are BTC/USD's 10^-1 scale and sizes
// 10^-8, and every expected CRC32 below was computed by `zlib.crc32` OUTSIDE
// this build — the same route every figure in NOTES-kraken.md took. A golden
// derived by asking the code under test what it thought would be a pin on
// nothing (ARCHITECTURE §9, 2026-08-18).
//
//   snapshot / recovery : asks 62791.3@0.16701217, 62794.2@2.0
//                         bids 62791.2@1.38258808, 62791.0@0.5     -> 477240181
//   after update 1      : bid  62791.0 becomes 0.75                -> 208614319
//   after update 2      : ask  62791.3 becomes 0.2                 ->  30466471
//
constexpr const char* kHealSnapshot =
    R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
    R"("bids":[{"price":62791.2,"qty":1.38258808},{"price":62791.0,"qty":0.50000000}],)"
    R"("asks":[{"price":62791.3,"qty":0.16701217},{"price":62794.2,"qty":2.00000000}],)"
    R"("checksum":477240181}]})";

constexpr const char* kHealUpdate1 =
    R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
    R"("bids":[{"price":62791.0,"qty":0.75000000}],"asks":[],"checksum":208614319}]})";

// The same message update 2 would really be, carrying a checksum ONE away from
// the truth. One away is the point: a wildly wrong value would also be caught by
// comparing against almost anything, and this one is caught only by actually
// computing the book's CRC32.
constexpr const char* kHealUpdate2Corrupt =
    R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
    R"("bids":[],"asks":[{"price":62791.3,"qty":0.20000000}],"checksum":30466472}]})";

constexpr const char* kHealUpdate2Honest =
    R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",)"
    R"("bids":[],"asks":[{"price":62791.3,"qty":0.20000000}],"checksum":30466471}]})";

}  // namespace

TEST_CASE("a wrong checksum greys the book, drops it, and asks for a resubscribe") {
    // ========================================================================
    // SYNTHETIC BY NECESSITY, WHICH MAKES IT MANDATORY RATHER THAN SECOND BEST.
    // ========================================================================
    //
    // A genuine venue-side corruption cannot be provoked from outside and was
    // never once observed across 9,932 captured frames (NOTES-kraken.md, known
    // unknowns). So there is no capture of it and there will not be one on
    // demand — a captured trace records a venue BEHAVING, which is no evidence
    // at all about what this client does when the venue does something else
    // (ARCHITECTURE §9, 2026-08-18). The synthetic frame is the instrument here,
    // not a stand-in for one, and it is the same reasoning the FrameReassembler
    // tests are built on.
    //
    // The honest twin `kHealUpdate2Honest` exists for one reason: it is the
    // control. Without it a "mismatch detected" assertion would pass just as
    // well against an adapter that raised Gap on EVERY update, and no captured
    // slice could tell the difference because every real message is honest.
    KrakenAdapter adapter(kKrakenBtcUsd, 25);
    Book book(kKrakenBtcUsd.spec);
    DisplaySnapshot panel{};
    std::vector<FeedEvent> events;
    auto sink = [&](const FeedEvent& ev) {
        events.push_back(ev);
        book.apply(ev);
        book.publish(panel);
    };

    adapter.on_frame(kHealSnapshot, sink);
    adapter.on_frame(kHealUpdate1, sink);
    REQUIRE(adapter.has_baseline());
    REQUIRE(adapter.stats().checksums_matched == 2);
    REQUIRE(panel.live());
    const std::size_t before = events.size();

    SUBCASE("the honest twin passes — so the check is not simply always failing") {
        adapter.on_frame(kHealUpdate2Honest, sink);
        CHECK(adapter.stats().checksums_matched == 3);
        CHECK(adapter.stats().checksums_failed == 0);
        CHECK(adapter.has_baseline());
        CHECK_FALSE(adapter.resync_wanted());
        CHECK(panel.live());
        // One Delta, and no Gap behind it.
        CHECK(events.size() == before + 1);
        CHECK(events.back().kind == FeedEvent::Kind::Delta);
    }

    SUBCASE("one wrong checksum drives the whole path") {
        adapter.on_frame(kHealUpdate2Corrupt, sink);

        // 1. THE COMPARISON. Both operands survive, because "they said X, we
        //    computed Y" is the only useful thing to print about a mismatch.
        CHECK(adapter.stats().checksums_failed == 1);
        CHECK(adapter.stats().checksums_matched == 2);
        CHECK(adapter.last_checksum() == 30466472u);
        CHECK(adapter.last_computed_checksum() == 30466471u);

        // 2. THE EVENT, IN THE EXISTING VOCABULARY. §4 is frozen and named this
        //    reason before any of this code existed; nothing here needed a new
        //    GapReason and nothing here needed a new FeedEvent::Kind.
        REQUIRE(events.size() == before + 2);
        CHECK(events[before].kind == FeedEvent::Kind::Delta);      // it did happen
        CHECK(events.back().kind == FeedEvent::Kind::Gap);
        CHECK(events.back().reason == GapReason::ChecksumFail);
        // Seq stays dense across the Gap — the adapter's one door, unchanged.
        CHECK(events.back().seq == events[before].seq + 1);

        // 3. THE PANEL GREYS, and it says why. Invariant #5 reached through a
        //    route that did not exist before tonight.
        CHECK_FALSE(panel.live());
        CHECK(panel.stale_reason == GapReason::ChecksumFail);
        CHECK(book.status() == depthcharge::FeedStatus::Stale);

        // 4. THE BOOK IS GONE, not merely marked. A ladder kept across a
        //    mismatch is the "different book that looks plausible" B1's
        //    no-baseline-no-deltas rule refuses one level down.
        CHECK_FALSE(adapter.has_baseline());
        CHECK(adapter.bid_count() == 0);
        CHECK(adapter.ask_count() == 0);

        // 5. AND SOMEBODY HAS TO ASK FOR THE SNAPSHOT. Kraken never re-snapshots
        //    on its own and the socket here is perfectly healthy, so without
        //    this latch the panel would stay grey over a live 1 Hz heartbeat for
        //    ever — honest, and permanently wrong.
        CHECK(adapter.resync_wanted());
        CHECK(adapter.stats().resyncs_requested == 1);
    }

    SUBCASE("the updates that follow are unverifiable, not a storm of failures") {
        adapter.on_frame(kHealUpdate2Corrupt, sink);
        const std::size_t at_gap = events.size();

        adapter.on_frame(kHealUpdate1, sink);
        adapter.on_frame(kHealUpdate2Honest, sink);

        // Nothing is emitted and nothing else fails: dropping the baseline makes
        // every following message unverifiable rather than wrong, which is what
        // stops one corruption becoming a resubscribe cadence. The pacing of a
        // retry belongs to the transport anyway (ARCHITECTURE §9, 2026-08-16 pm
        // — a cadence that begins with a teardown is not a latency knob).
        CHECK(events.size() == at_gap);
        CHECK(adapter.stats().checksums_failed == 1);
        CHECK(adapter.stats().resyncs_requested == 1);
        CHECK(adapter.stats().checksums_unverifiable == 2);
        CHECK(adapter.stats().deltas_before_baseline == 2);
    }

    SUBCASE("a fresh snapshot heals it, and the caller clears the request") {
        adapter.on_frame(kHealUpdate2Corrupt, sink);
        REQUIRE_FALSE(panel.live());

        // What the transport would do: unsubscribe, resubscribe, and the venue
        // serves a snapshot. Modelled here as the snapshot alone, because the
        // resubscribe is the caller's half and this file is the adapter's.
        adapter.clear_resync_wanted();
        adapter.on_frame(kHealSnapshot, sink);

        CHECK(adapter.has_baseline());
        CHECK(adapter.stats().checksums_matched == 3);
        CHECK(events.back().kind == FeedEvent::Kind::Snapshot);
        CHECK(panel.live());
        CHECK(panel.bid_count == 2);
        CHECK(panel.ask_count == 2);
        CHECK(panel.best_bid() == 627912);
        CHECK(panel.best_ask() == 627913);
        CHECK_FALSE(adapter.resync_wanted());
    }

    SUBCASE("a snapshot whose OWN checksum is wrong does not baseline the book") {
        // The recovery frame can be corrupt too, and trusting it is the worse
        // failure: a book that believes it has re-baselined will happily amend
        // from there for ever.
        KrakenAdapter fresh(kKrakenBtcUsd, 25);
        std::vector<FeedEvent> got;
        auto tap = [&got](const FeedEvent& ev) { got.push_back(ev); };

        fresh.on_frame(
            R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD",)"
            R"("bids":[{"price":62791.2,"qty":1.38258808},{"price":62791.0,"qty":0.50000000}],)"
            R"("asks":[{"price":62791.3,"qty":0.16701217},{"price":62794.2,"qty":2.00000000}],)"
            R"("checksum":477240182}]})",
            tap);

        REQUIRE(got.size() == 2);
        CHECK(got[0].kind == FeedEvent::Kind::Snapshot);
        CHECK(got[1].kind == FeedEvent::Kind::Gap);
        CHECK(got[1].reason == GapReason::ChecksumFail);
        CHECK_FALSE(fresh.has_baseline());
        CHECK(fresh.resync_wanted());
    }
}

TEST_CASE("the checksum ledger is total: seen == matched + failed + unverifiable") {
    // The three outcomes are disjoint and exhaustive BY CONSTRUCTION, and this
    // is the case that stops a fourth quietly appearing — a message that took
    // none of the three paths would leave the identity short, and nothing else
    // in the suite would notice.
    //
    // Run over all five committed slices plus the synthetic corruption, so it
    // holds for a stream that never fails, one that can never be checked, and
    // one that fails once.
    for (const char* name : {kD10, kD25, kD100, kQuiet, kExtreme}) {
        CAPTURE(name);
        const ReplayResult r = replay(name);
        const auto& k = r.kraken;
        CHECK(k.checksums_seen ==
              k.checksums_matched + k.checksums_failed + k.checksums_unverifiable);
        // And every book message carried one. Zero here is a WIRE FACT — this
        // venue checksums every snapshot and every update — and it is also what
        // stops "carry no checksum" becoming a silent way for a future fixture,
        // or a future wire change, to opt out of the check entirely.
        CHECK(k.book_msgs_unchecksummed == 0);
        CHECK(k.checksums_seen == k.snapshot_frames + k.update_frames);
    }

    KrakenAdapter adapter(kKrakenBtcUsd, 25);
    auto sink = [](const FeedEvent&) {};
    adapter.on_frame(kHealSnapshot, sink);
    adapter.on_frame(kHealUpdate2Corrupt, sink);
    adapter.on_frame(kHealUpdate1, sink);
    const auto& k = adapter.stats();
    CHECK(k.checksums_seen == 3);
    CHECK(k.checksums_matched == 1);
    CHECK(k.checksums_failed == 1);
    CHECK(k.checksums_unverifiable == 1);
    CHECK(k.book_msgs_unchecksummed == 0);
    CHECK(k.checksums_seen ==
          k.checksums_matched + k.checksums_failed + k.checksums_unverifiable);
}

// ---------------------------------------------------------------------------
// M4 STAGE B2 — THE FRAME THAT HAD NEVER BEEN CAPTURED
// ---------------------------------------------------------------------------

TEST_CASE("an unsubscribe ack is not a subscribe ack") {
    // ========================================================================
    // FOUND BY CAPTURING THE RESYNC SLICE, NOT BY READING THE PARSER.
    // ========================================================================
    //
    // `{"method":"unsubscribe","result":{...},"success":true}` is identical in
    // every structural respect to a subscribe ack — no `channel`, no `type`,
    // `method` + `result` + `success` — and until B2 the parser did not read the
    // method name at all: `if (have_method) return SubscribeAck`.
    //
    // NO CAPTURE COULD HAVE CAUGHT IT, and that is the point rather than an
    // excuse. The healing path is the first thing this project has ever built
    // that SENDS an unsubscribe, so before 2026-08-18 no trace at any venue
    // contained one. Same class as B1's truncation defect: the discriminating
    // input did not exist, so the code and every committed file agreed.
    //
    // Benign while the venue says `success:true`. The failure it was one refused
    // unsubscribe away from is not benign at all — `on_ack` would have latched
    // `Refused`, which the firmware turns into `die()`.
    std::vector<FeedEvent> events;
    auto sink = [&events](const FeedEvent& ev) { events.push_back(ev); };

    SUBCASE("a successful unsubscribe leaves the subscription state alone") {
        KrakenAdapter adapter(kKrakenBtcUsd, 25);
        adapter.on_frame(
            R"({"method":"subscribe","result":{"channel":"book","depth":25,)"
            R"("snapshot":true,"symbol":"BTC/USD"},"success":true})",
            sink);
        REQUIRE(adapter.subscribe_state() == KrakenAdapter::SubscribeState::Subscribed);

        adapter.on_frame(
            R"({"method":"unsubscribe","result":{"channel":"book","depth":25,)"
            R"("symbol":"BTC/USD"},"success":true})",
            sink);
        CHECK(adapter.stats().unsubscribe_acks == 1);
        CHECK(adapter.stats().acks == 1);          // NOT counted as a second subscribe
        CHECK_FALSE(adapter.refused());
    }

    SUBCASE("a REFUSED unsubscribe does not latch refused() and kill the board") {
        // The assertion the defect was one wire answer away from failing.
        KrakenAdapter adapter(kKrakenBtcUsd, 25);
        adapter.on_frame(
            R"({"method":"unsubscribe","result":{"channel":"book"},)"
            R"("success":false,"error":"Subscription Not Found"})",
            sink);
        CHECK(adapter.stats().unsubscribe_acks == 1);
        CHECK(adapter.stats().unsubscribe_refused == 1);
        CHECK_FALSE(adapter.refused());
        CHECK(adapter.subscribe_state() == KrakenAdapter::SubscribeState::Unknown);
        // It still drops the book, because we no longer know what we are
        // subscribed to and grey is the conservative direction.
        REQUIRE(events.size() == 1);
        CHECK(events[0].reason == GapReason::Resync);
    }

    SUBCASE("the ack drops the book, so a frozen ladder cannot render live") {
        KrakenAdapter adapter(kKrakenBtcUsd, 25);
        adapter.on_frame(kHealSnapshot, sink);
        REQUIRE(adapter.has_baseline());
        REQUIRE(adapter.bid_count() == 2);

        adapter.on_frame(
            R"({"method":"unsubscribe","result":{"channel":"book","depth":25,)"
            R"("symbol":"BTC/USD"},"success":true})",
            sink);
        REQUIRE(events.size() == 2);
        CHECK(events[1].kind == FeedEvent::Kind::Gap);
        CHECK(events[1].reason == GapReason::Resync);
        CHECK_FALSE(adapter.has_baseline());
        CHECK(adapter.bid_count() == 0);
        // AND IT IS NOT A RESYNC REQUEST. The transport sent the unsubscribe; it
        // is mid-sequence and about to subscribe. A latch asking it to do what it
        // is already doing is the retry storm §9 (2026-08-16 pm) is about.
        CHECK_FALSE(adapter.resync_wanted());
        CHECK(adapter.stats().resyncs_requested == 0);
    }

    SUBCASE("a method this build does not know is tolerated, counted, ignored") {
        // `ping`/`pong` are Kraken v2's other methods and M6 owns them. Filing an
        // unrecognised method as a subscribe ack is precisely how the defect
        // above happened, so the fallback is Unknown rather than the nearest
        // familiar thing.
        KrakenAdapter adapter(kKrakenBtcUsd, 25);
        adapter.on_frame(R"({"method":"pong","time_in":"x","time_out":"y"})", sink);
        CHECK(adapter.stats().unknown_kind == 1);
        CHECK(adapter.stats().acks == 0);
        CHECK(adapter.stats().unsubscribe_acks == 0);
        CHECK(events.empty());
    }
}

// ---------------------------------------------------------------------------
// M4 STAGE B2 — THE RESYNC SLICE, CAPTURED DELIBERATELY
// ---------------------------------------------------------------------------

TEST_CASE("the resync slice: one connection, two subscriptions, two books") {
    // ========================================================================
    // THE ARTEFACT THE TRIAGE HAS OWED SINCE STAGE A, AND WHAT MAKES IT REAL
    // RATHER THAN SYNTHESISED.
    // ========================================================================
    //
    // Captured 2026-08-18 by `capture_kraken.py --resubscribe-after 34`: one
    // connection, one deliberate unsubscribe and re-subscribe sent mid-capture,
    // and a genuine `book/snapshot` from the venue in reply. The CLIENT's half
    // is provoked — that is what makes the case capturable at all, and it is a
    // thing a client really does — and the VENUE's half is untouched. Nothing
    // here is synthetic, which is the distinction ARCHITECTURE §9 (2026-08-18)
    // draws between this and the checksum-mismatch case above.
    //
    // Measured on the wire while capturing, and it is the answer to the open
    // question this stage inherited: **the mid-stream snapshot is byte-shape
    // IDENTICAL to the on-connect one** — same `channel`, same `type`, same
    // `data[0]` keys, no flag, no marker. So a resync is detectable only by
    // POSITION, the venue-free predicate is the only thing that could work, and
    // a Kraken-specific branch would have had nothing to branch on.
    const ReplayResult r = replay(kResync);

    CHECK(r.decoder == "kraken");
    CHECK(r.frames == 99);
    CHECK(r.kraken.snapshot_frames == 2);        // the on-connect one and the resync
    CHECK(r.kraken.update_frames == 17);
    CHECK(r.kraken.heartbeats == 73);
    CHECK(r.kraken.status_frames == 1);
    CHECK(r.kraken.acks == 2);                   // two subscribes, both accepted
    CHECK(r.kraken.unsubscribe_acks == 1);
    CHECK(r.kraken.unsubscribe_refused == 0);
    CHECK(r.kraken.parse_errors == 0);
    CHECK(r.kraken.unknown_kind == 0);
    CHECK(r.kraken.levels_evicted == 3);

    SUBCASE("both books check out against the venue, across the resync") {
        // 19 = 2 snapshots + 17 updates, split across two subscriptions. The
        // second baseline is as good as the first, and nothing is unverifiable:
        // every book message arrived with a book to compare it to.
        CHECK(r.kraken.checksums_seen == 19);
        CHECK(r.kraken.checksums_matched == 19);
        CHECK(r.kraken.checksums_failed == 0);
        CHECK(r.kraken.checksums_unverifiable == 0);
        CHECK(r.kraken.book_msgs_unchecksummed == 0);
        CHECK(r.kraken.resyncs_requested == 0);   // nothing went wrong; we asked
    }

    SUBCASE("the unsubscribe ack greys the panel, and the snapshot clears it") {
        // THE BOOK IS DROPPED BY A FRAME, NOT BY A TIMEOUT — which is the whole
        // reason this slice had to exist. Measured on the busier BTC/USD capture
        // of the same evening: 3,548 ms of no book events between the
        // unsubscribe ack and the re-subscription's snapshot, with the 1 Hz
        // heartbeat running straight through. So no watchdog fires, nothing on
        // the connection looks wrong, and a book left baselined across that
        // window renders live while standing still.
        CHECK(r.book.gaps == 1);
        REQUIRE(r.episodes.size() == 1);
        const auto& ep = r.episodes[0];
        CHECK(ep.reason == GapReason::Resync);
        CHECK(ep.cleared);
        CHECK(ep.gap_events == 1);
        // ZERO SILENCE CAUSED IT. `observed_gap_ms` is the hole that opened the
        // episode, and there was none — a frame's CONTENT did this. That is a
        // different shape of outage from everything committed before tonight and
        // it is worth an assertion rather than a comment.
        CHECK(ep.observed_gap_ms == doctest::Approx(0.0));
        CHECK(ep.stale_ms > 900.0);
        CHECK(ep.stale_ms < 1300.0);
        // ...and it really did clear, from the second snapshot rather than by
        // running out of trace.
        CHECK(ep.cleared_frame > ep.frame_before);
        CHECK(r.final_snapshot.live());
        CHECK(r.final_snapshot.bid_count == 25);
        CHECK(r.final_snapshot.ask_count == 25);
    }

    SUBCASE("the liveness clock never notices, and that is correct") {
        // The heartbeat is a property of the CONNECTION, and the connection did
        // not blink. So the calibrated grey threshold sees nothing here — the
        // panel greyed because the adapter said so, not because a clock expired.
        // The two mechanisms are independent and this trace is the only place
        // that shows it.
        CHECK(r.liveness_arrivals == 73);
        CHECK(r.liveness_median_ms == doctest::Approx(1000.0).epsilon(0.01));
        CHECK(r.worst_age_ms <= 1100);
    }
}
