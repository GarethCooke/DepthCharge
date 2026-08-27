// test_trace_records.cpp — the two records that are not frames (M5 stage A).
//
// Everything here is new behaviour from this stage, and it exists because of
// invariant #6: until this file, Binance liveness was a claim rather than a
// covered behaviour. The 2026-08-25 ruling says the panel greys on a WebSocket
// PING and on nothing else at that venue; a ping is not JSON, was not
// representable in a trace before M5 stage 0, and was not classifiable before
// tonight. A ruling whose mechanism nothing exercises is a comment.
//
// What is asserted, and why each one is here rather than assumed:
//
//   * the record-form contract, replayed from the SHARED CORPUS that
//     tools/tracefile.py replays too — "both readers agree on what they reject"
//     proven by a fixture outside both of them rather than by inspection;
//   * a ping arrival stamps the liveness clock and an otherwise identical trace
//     without one does not — deliverable 5, and the mutation below;
//   * the clock is stamped from `event_ns`, not `rx_ns`, with the case the
//     committed ATOMEUR deep-seed slice actually contains: three pings 20 s
//     apart sharing ONE rx_ns;
//   * a REST record is the only thing at this venue that re-baselines, and a
//     REST fetch that FAILED re-baselines nothing;
//   * Anvil and Kraken are untouched — no control record exists in any of their
//     traces, and their liveness answers still come from frame content;
//   * an unmatched venue is a hard failure rather than a default.
//
// MUTATION-VERIFIED, 2026-08-25, by applying each mutation for real, rebuilding
// and recording what went red. Measured, not predicted:
//
//   1. `binance_classify`'s control branch sets `is_liveness = false`
//      -> 4 cases, 6 assertions. "a ping arrival stamps the liveness clock"
//         (0 == 2), "the liveness clock is stamped from the arrival", "the
//         decoder answers the driver's question", and the decoder's own
//         `liveness_records()` counter.
//   2. accumulate() stamps the liveness clock from `frame.rx_ns` instead of
//      `frame.event_ns`
//      -> exactly 1 case: "the liveness clock is stamped from the arrival, not
//         from the write", reporting a median and worst of 0 ms against 20,000.
//         The narrowness is the point — this mutant is invisible to every other
//         case in the suite, because at Anvil and Kraken the two stamps are the
//         same number.
//   3. `binance_classify` loses its bodyless-REST early return
//      -> exactly 1 case: "a fetch that returned nothing re-baselines nothing",
//         on all four of its assertions.
//
// A NOTE ON HOW MUTANT 2 WAS VERIFIED, because the first attempt lied. The
// mutation was applied and reverted with a copy whose timestamp came back with
// it, so `make` thought the object was current and never rebuilt it — and the
// next mutant's run was scored against a binary still carrying the previous
// one. It showed 4 red cases where the true answer is 1. **A green build from
// the wrong sources is the failure CLAUDE.md's worktree rule exists for, and it
// has a second face: a red one.** Every figure above was re-taken after forcing
// the timestamps forward, with the unmutated baseline re-confirmed at 385/385
// between each.
//
// NOT MUTATION-VERIFIABLE FROM INSIDE THE SUITE, and named rather than
// pretended: the decoder CONTRACT's static_asserts (trace_decoder.hpp). A
// violation breaks the build that would run the test — DESIGN strain 3 records
// this as a CONFIRMED weakness of option (ii), not a gap in this file. Checked
// by hand instead, by giving `BinanceTraceDecoder` a `classify(const
// std::string&)`, i.e. a decoder written against the pre-widening contract:
//
//   trace_decoder.hpp:188:43: error: static assertion failed: a venue decoder
//   must provide RecordKind classify(const TraceRecord&)
//
// That message is the widened one. The assertion moved with the contract.
#include <doctest/doctest.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "dc_harness/replay_driver.hpp"
#include "dc_harness/trace.hpp"
#include "dc_harness/trace_decoder.hpp"
#include "dc_harness/venue.hpp"

using namespace dc::harness;

namespace {

// A Binance header, cut to the bone: the venue tag, the symbol its traits
// require, and the clock. `capture_binance.py` writes ten more fields and none
// of them is part of the contract under test.
constexpr const char* kBinanceHeader =
    R"({"captured_at":"2026-08-25T00:00:00Z","url":"wss://data-stream.binance.vision/stream","venue":"binance","symbol":"BTCUSDT","tool_version":"0.1.0","clock":"perf_counter_ns"})"
    "\n";

std::string binance_trace(std::initializer_list<const char*> records) {
    std::string t = kBinanceHeader;
    for (const char* r : records) {
        t += r;
        t += "\n";
    }
    return t;
}

// One diff event, ready to be given any rx_ns.
std::string depth_update(long long rx_ns, long long u) {
    return R"({"rx_ns":)" + std::to_string(rx_ns) +
           R"(,"frame":{"e":"depthUpdate","U":)" + std::to_string(u) +
           R"(,"u":)" + std::to_string(u + 1) + R"(,"b":[],"a":[]}})";
}

// A control record. `rx_ns` is when the main loop WROTE it and `recv_ns` is when
// the frame ARRIVED — the whole point of the two being separate fields, and the
// reason every ping helper here takes both.
std::string control(long long rx_ns, const char* op, long long recv_ns,
                    bool replied = true) {
    std::string s = R"({"rx_ns":)" + std::to_string(rx_ns) + R"(,"kind":"control","ctl":{"op":")" +
                    op + R"(","recv_ns":)" + std::to_string(recv_ns);
    if (replied) { s += R"(,"pong_ns":)" + std::to_string(recv_ns + 100000); }
    s += R"(,"payload_len":13,"payload_b64":"MTc4NzYwMjk2ODIyNA=="},"frame":null})";
    return s;
}

std::string rest_record(long long rx_ns, long long recv_ns, long long last_update_id) {
    return R"({"rx_ns":)" + std::to_string(rx_ns) +
           R"(,"kind":"rest","req":{"method":"GET","url":"https://data-api.binance.vision/api/v3/depth?symbol=BTCUSDT&limit=1000","limit":1000,"weight":50,"sent_ns":)" +
           std::to_string(recv_ns - 1000000000) + R"(,"status":200,"recv_ns":)" +
           std::to_string(recv_ns) + R"(},"frame":{"lastUpdateId":)" +
           std::to_string(last_update_id) + R"(,"bids":[],"asks":[]}})";
}

}  // namespace

// ---------------------------------------------------------------------------
// The record-form contract, from the corpus BOTH readers replay
// ---------------------------------------------------------------------------

TEST_CASE("the record-shape corpus: this reader and tools/tracefile.py agree") {
    // ONE CORPUS, TWO READERS. tools/tracefile.py's selfcheck replays this same
    // file through read_capture and asserts the same verdicts, which is what
    // makes "the two agree on what they reject" a test rather than an
    // inspection — neither side can quietly relax a rule, because the case lives
    // outside both of them.
    const std::string path = std::string(DC_TESTS_DIR) + "/record_shapes.json";
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.good(), "cannot open the shared record-shape corpus: " << path);
    std::stringstream buf;
    buf << in.rdbuf();

    const nlohmann::json doc = nlohmann::json::parse(buf.str(), nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    const auto& cases = doc.at("cases");
    // The corpus is meant to carry the cases no committed capture contains. A
    // corpus that has quietly shrunk to two entries still passes every case it
    // holds, which is exactly the failure this guards.
    REQUIRE(cases.size() >= 10);

    for (const auto& c : cases) {
        const std::string name = c.at("name").get<std::string>();
        const std::string line = c.at("line").get<std::string>();
        const bool want_accept = c.at("accept").get<bool>();
        CAPTURE(name);
        CAPTURE(c.at("why").get<std::string>());

        const std::string text = kBinanceHeader + line + "\n";
        TraceReader reader(text, in_memory, name);
        TraceRecord rec;
        bool accepted = false;
        std::string err;
        try {
            accepted = reader.next(rec);
        } catch (const TraceError& e) {
            err = e.what();
        }
        CAPTURE(err);
        CHECK(accepted == want_accept);
        if (accepted && want_accept) {
            CHECK(record_form_name(rec.form) == c.at("form").get<std::string>());
        }
    }
}

TEST_CASE("an absent kind is a frame, which is what keeps every old trace valid") {
    // The additive rule, asserted at the reader rather than argued from the
    // eleven byte-identical files. Both halves matter: an absent key reads as
    // Frame, and a Frame record's event_ns IS its rx_ns — so nothing about the
    // two pre-existing venues' timing can have moved.
    const TraceStats s = read_trace_text(binance_trace({depth_update(1000000, 5).c_str()}));
    CHECK(s.frame_count == 1);
    CHECK(s.rest_records == 0);
    CHECK(s.control_records == 0);
}

TEST_CASE("a control record carries what wsclient.on_control produces") {
    const std::string text = binance_trace({control(9000000000, "ping", 5000000000).c_str()});
    TraceReader reader(text, in_memory);
    TraceRecord rec;
    REQUIRE(reader.next(rec));

    CHECK(rec.form == RecordForm::Control);
    CHECK(rec.has_frame() == false);
    CHECK(rec.frame_json.empty());
    CHECK(rec.ctl.opcode == "ping");
    CHECK(rec.ctl.recv_ns == 5000000000);
    CHECK(rec.ctl.payload_len == 13);
    CHECK(rec.ctl.payload_b64 == "MTc4NzYwMjk2ODIyNA==");
    // `replied_ns` IS NOT DECORATION — it is the evidence that the pong went
    // back, and the 2026-08-25 ruling rests on that path being exercised rather
    // than merely present. A client that answered nothing would look identical
    // on this side of the socket until the venue closed it 60 s later.
    CHECK(rec.ctl.replied);
    CHECK(rec.ctl.replied_ns == 5000100000);
}

TEST_CASE("a control record with no reply says so rather than reading as zero") {
    const std::string text =
        binance_trace({control(9000000000, "close", 5000000000, /*replied=*/false).c_str()});
    TraceReader reader(text, in_memory);
    TraceRecord rec;
    REQUIRE(reader.next(rec));
    CHECK(rec.ctl.opcode == "close");
    CHECK(rec.ctl.replied == false);
    CHECK(rec.ctl.replied_ns == 0);  // and the flag is what tells them apart
}

TEST_CASE("a REST record carries the request as well as the response") {
    // A REST body is not a transcript, it is a transcript plus a question: which
    // URL, and which `limit`. A body whose request is unknown cannot be
    // reconciled against anything, and at BTCUSDT a limit=100 book and a
    // limit=1000 book are different claims that diverge within 90 seconds.
    const std::string text = binance_trace({rest_record(5000000000, 4000000000, 99076734902).c_str()});
    TraceReader reader(text, in_memory);
    TraceRecord rec;
    REQUIRE(reader.next(rec));

    CHECK(rec.form == RecordForm::Rest);
    CHECK(rec.has_frame());
    CHECK(rec.rest.limit == 1000);
    CHECK(rec.rest.weight == 50);
    CHECK(rec.rest.status == 200);
    CHECK(rec.rest.method == "GET");
    CHECK(rec.rest.url.find("limit=1000") != std::string::npos);
    CHECK(rec.rest.recv_ns == 4000000000);
    // The record lands ~1-1.5 s AFTER the instant it describes, because the
    // fetch runs on a worker thread and rx_ns is what orders the file.
    CHECK(rec.rx_ns == 5000000000);
    CHECK(rec.event_ns == 4000000000);
}

// ---------------------------------------------------------------------------
// Deliverable 5 — the liveness coverage invariant #6 asks for
// ---------------------------------------------------------------------------

TEST_CASE("a ping arrival stamps the liveness clock; its absence does not") {
    // THE COVERAGE THIS STAGE EXISTS FOR. Two traces identical but for the
    // control records, scored by the same statistics pass the goldens use.
    const std::string with_ping = binance_trace({
        depth_update(1000000, 10).c_str(),
        control(2000000000, "ping", 1000000000).c_str(),
        depth_update(2100000000, 11).c_str(),
        control(22000000000, "ping", 21000000000).c_str(),
    });
    const std::string without = binance_trace({
        depth_update(1000000, 10).c_str(),
        depth_update(2100000000, 11).c_str(),
    });

    const TraceStats a = read_trace_text(with_ping, "with-ping");
    const TraceStats b = read_trace_text(without, "no-ping");

    CHECK(a.liveness_events == 2);
    CHECK(a.control_records == 2);
    CHECK(a.control_replied == 2);
    CHECK(b.liveness_events == 0);
    CHECK(b.control_records == 0);

    // A ping is NOT a book event, and that is what makes it usable as the
    // liveness signal at all — it proves the socket is alive without pretending
    // the book moved. Both traces see the same two diffs.
    CHECK(a.book_events == 2);
    CHECK(b.book_events == 2);
}

TEST_CASE("the liveness clock is stamped from the arrival, not from the write") {
    // THE CASE THE COMMITTED ATOMEUR DEEP-SEED SLICE ACTUALLY CONTAINS: three
    // pings, twenty seconds apart, sharing ONE rx_ns because the main loop
    // flushed them together. Stamping from rx_ns reads three arrivals 0 ms
    // apart, which would drive the self-calibrating median toward zero and then
    // fire on every real interval — a threshold measuring the capture tool's
    // flush schedule instead of the venue's cadence.
    const long long flush = 60000000000LL;
    const std::string text = binance_trace({
        control(flush, "ping", 20000000000LL).c_str(),
        control(flush, "ping", 40000000000LL).c_str(),
        control(flush, "ping", 60000000000LL).c_str(),
    });
    const TraceStats s = read_trace_text(text, "three-pings-one-flush");

    CHECK(s.liveness_events == 3);
    CHECK(s.median_liveness_gap_ms == doctest::Approx(20000.0));
    CHECK(s.max_liveness_gap_ms == doctest::Approx(20000.0));
    // And rx_ns is untouched by any of it: it still orders the file, and the
    // three records still arrived in the same flush.
    CHECK(s.first_rx_ns == flush);
    CHECK(s.last_rx_ns == flush);
}

TEST_CASE("the decoder answers the driver's question about a control record") {
    // `replay_driver.cpp` asks exactly `decoder.classify(record).is_liveness`.
    // This is that expression, on the record shape only this venue produces.
    const std::string text = binance_trace({
        control(1000, "ping", 900).c_str(),
        control(2000, "pong", 1900).c_str(),
        control(3000, "close", 2900).c_str(),
        depth_update(4000, 12).c_str(),
    });
    TraceReader reader(text, in_memory);
    BinanceTraceDecoder decoder(depthcharge::binance::kBinanceBtcUsdt);
    TraceRecord rec;
    std::vector<std::pair<std::string, bool>> seen;
    while (reader.next(rec)) {
        const RecordKind k = decoder.classify(rec);
        seen.emplace_back(std::string(k.name), k.is_liveness);
    }

    REQUIRE(seen.size() == 4);
    CHECK(seen[0] == std::make_pair(std::string("ping"), true));
    // A PONG IS THIS SIDE TALKING and proves nothing about the server; a CLOSE
    // proves the opposite of liveness. The ruling is the ping and nothing else,
    // and it is deliberately not softened into "anything unsolicited" — the
    // audit stream is unsolicited too.
    CHECK(seen[1] == std::make_pair(std::string("pong"), false));
    CHECK(seen[2] == std::make_pair(std::string("close"), false));
    CHECK(seen[3] == std::make_pair(std::string("depthUpdate"), false));

    // The venue table's declared signal names a kind this decoder produces —
    // the same relationship `summary` and `heartbeat` have to theirs. If these
    // two ever disagree the report prints a LIVENESS row counting a bucket that
    // does not exist.
    CHECK(venue_traits(Venue::Binance).liveness_signal == seen[0].first);
}

TEST_CASE("no depth record is ever liveness at this venue") {
    // Stated as its own case because it is the half of the ruling most likely
    // to be softened later: `@depth20` is unsolicited, arrives ten times a
    // second, and is CHANGE-DRIVEN — which is the entire reason this venue
    // needs a control frame to prove it is alive. Record arrival went silent for
    // 10.5 s legitimately on the quiet pair.
    const std::string text = binance_trace({
        R"({"rx_ns":1000,"frame":{"stream":"btcusdt@depth20@100ms","data":{"lastUpdateId":7,"bids":[],"asks":[]}}})",
        R"({"rx_ns":2000,"frame":{"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate","U":8,"u":9,"b":[],"a":[]}}})",
    });
    const TraceStats s = read_trace_text(text, "no-liveness-in-depth");
    CHECK(s.liveness_events == 0);
    CHECK(s.book_events == 2);
    CHECK(s.count("partialDepth") == 1);
    CHECK(s.count("depthUpdate") == 1);
    // The combined-stream envelope is seen through and never stripped: whether
    // the wrapper is worth its bytes is a measured question, and a reader that
    // discarded it would have destroyed the evidence.
    CHECK(s.snapshot_count == 0);
}

// ---------------------------------------------------------------------------
// What re-baselines, and what does not
// ---------------------------------------------------------------------------

TEST_CASE("a REST record is the only thing at this venue that re-baselines") {
    const std::string text = binance_trace({
        R"({"rx_ns":1000,"frame":{"lastUpdateId":7,"bids":[],"asks":[]}})",
        depth_update(2000, 8).c_str(),
        rest_record(3000, 2900, 99076734902).c_str(),
    });
    const TraceStats s = read_trace_text(text, "rebaseline");
    // A `@depth20` partial fully determines the top 20 and nothing below it, so
    // the book is NOT fully known after one — and the venue publishes ten a
    // second, so counting them would report a hundred and fifty resyncs on a
    // fifteen-second slice. The Python twin of THIS field is `rebaselines()`,
    // not `is_snapshot()`; the divergence is stated in both files.
    CHECK(s.snapshot_count == 1);
    CHECK(s.count("rest") == 1);
    CHECK(s.count("partialDepth") == 1);
    // Three book events: the partial, the diff and the REST body all reach it.
    CHECK(s.book_events == 3);
}

TEST_CASE("a fetch that returned nothing re-baselines nothing") {
    // `capture_binance.py` records a FAILED fetch rather than dropping it: "the
    // snapshot did not arrive" is a fact about the capture window. Filing it as
    // an ordinary fetch would hide the only thing it is evidence of — the same
    // rule, and the same reason, as Kraken's `ack:subscribe REFUSED`.
    const std::string text = binance_trace({
        depth_update(1000, 8).c_str(),
        R"({"rx_ns":2000,"kind":"rest","req":{"method":"GET","url":"https://data-api.binance.vision/api/v3/depth?symbol=BTCUSDT&limit=1000","limit":1000,"status":429,"error":"Too Many Requests","recv_ns":1900},"frame":null})",
    });
    const TraceStats s = read_trace_text(text, "failed-fetch");
    CHECK(s.count("rest:no-body") == 1);
    CHECK(s.count("rest") == 0);
    CHECK(s.snapshot_count == 0);
    CHECK(s.book_events == 1);  // the diff only
    CHECK(s.rest_records == 1);  // it is still a record of the session

    TraceReader reader(text, in_memory);
    TraceRecord rec;
    REQUIRE(reader.next(rec));
    REQUIRE(reader.next(rec));
    CHECK(rec.has_frame() == false);
    CHECK(rec.rest.status == 429);
    // WHY there is no body, not merely that there is none.
    CHECK(rec.rest.error == "Too Many Requests");
}

// ---------------------------------------------------------------------------
// The two venues that must not have moved
// ---------------------------------------------------------------------------

TEST_CASE("Anvil and Kraken still answer liveness from frame content") {
    // The stage's other claim, at the decoder rather than at the corpus: their
    // answers come from a frame's own text, no control record exists in any of
    // their committed traces, and the widening added no path they can reach. The
    // eleven byte-identical files are proved by dc_taxonomy's pin table; this is
    // the unit-level statement of the same thing.
    const std::string anvil =
        R"({"captured_at":"t","url":"u","ticker":101,"tool_version":"0.1.0"})"
        "\n"
        R"({"rx_ns":1,"frame":{"type":"summary"}})"
        "\n"
        R"({"rx_ns":2,"frame":{"type":"book"}})"
        "\n";
    const TraceStats a = read_trace_text(anvil, "anvil");
    CHECK(a.liveness_events == 1);
    CHECK(a.control_records == 0);
    CHECK(a.rest_records == 0);

    const std::string kraken =
        R"({"captured_at":"t","url":"u","venue":"kraken","symbol":"BTC/USD","tool_version":"0.1.0"})"
        "\n"
        R"({"rx_ns":1,"frame":{"channel":"heartbeat"}})"
        "\n"
        R"({"rx_ns":2,"frame":{"channel":"book","type":"update","data":[]}})"
        "\n";
    const TraceStats k = read_trace_text(kraken, "kraken");
    CHECK(k.liveness_events == 1);
    CHECK(k.control_records == 0);
    CHECK(k.rest_records == 0);
}

TEST_CASE("the venue table says which clock each threshold is about") {
    // Binance is the first venue whose record-arrival clock and liveness clock
    // are different quantities rather than one quantity measured twice, and a
    // single unlabelled column would make them look like the same number. The
    // sentinel is what stops -1 reading as a threshold.
    CHECK(venue_traits(Venue::Anvil).has_legacy_threshold());
    CHECK(venue_traits(Venue::Kraken).has_legacy_threshold());
    CHECK_FALSE(venue_traits(Venue::Binance).has_legacy_threshold());
    CHECK(venue_traits(Venue::Binance).legacy_book_threshold_ms < 0.0);
    // And the row must SAY so — a bare sentinel cannot be told apart from a
    // field nobody filled in (ARCHITECTURE §9, 2026-08-19).
    CHECK_FALSE(venue_traits(Venue::Binance).legacy_note.empty());
    CHECK_FALSE(venue_traits(Venue::Binance).validated_note.empty());
    // Nothing in this build validates a Binance level: the venue signs nothing,
    // and the `@depth20` audit is a cross-stream comparison no code here makes.
    CHECK(venue_traits(Venue::Binance).validated_depth == 0);
}

// ---------------------------------------------------------------------------
// Deliverable 3 — the unmatched venue
// ---------------------------------------------------------------------------
// The adapter (M5 stage B1)
// ---------------------------------------------------------------------------

TEST_CASE("the seam stage A froze is the seam B1 filled, not reshaped") {
    // THE STAGE-A CASE THIS REPLACES ASSERTED THE OPPOSITE, and that is the
    // point rather than a regression. It read `CHECK(events.empty())` and
    // `decoder.events_emitted() == 0`, because at stage A the Binance decoder
    // was a CLASSIFIER and "emits nothing" was a claim worth a counter. B1 makes
    // it false on purpose. What must NOT have changed is the shape: the same
    // `decode(record, sink)` the classifier took, the same sink contract, the
    // same `on_transport_gap`. The declaration is unchanged; only the body is.
    //
    // The counters moved with the behaviour — from the decoder to the adapter —
    // which is exactly where Kraken's went at M4 B1.
    //
    // **THE FOURTH RECORD ARRIVED AT M5 STAGE C**, and its absence is what the
    // remedy exposed rather than broke. The seed here names `lastUpdateId = 500`
    // and the buffered diff carries `u = 9`, so the snapshot already contains it
    // and there is no survivor to bracket with — meaning the feed never
    // corroborated this seed at all. Until stage C the Snapshot went out anyway,
    // so a sequence with nothing to corroborate it still produced one; now it
    // does not, and a diff that DOES bracket `lastUpdateId + 1` is what this
    // case has to supply in order to still be about the seam.
    const std::string text = binance_trace({
        depth_update(1000, 8).c_str(),
        control(2000, "ping", 1900).c_str(),
        rest_record(3000, 2900, 500).c_str(),
        depth_update(4000, 501).c_str(),
    });
    TraceReader reader(text, in_memory);
    BinanceTraceDecoder decoder(depthcharge::binance::kBinanceBtcUsdt);
    TraceRecord rec;
    std::vector<depthcharge::FeedEvent> events;
    auto sink = [&events](const depthcharge::FeedEvent& e) { events.push_back(e); };
    while (reader.next(rec)) { decoder.decode(rec, sink); }

    const auto& st = decoder.adapter().stats();
    CHECK(st.diff_frames == 2);
    CHECK(st.rest_snapshots == 1);
    // A CONTROL RECORD REACHES NO ADAPTER AT ALL. It stamps the liveness clock
    // through `classify()` and there is nothing for a book to do with it, so the
    // adapter never sees one — `frames_in` counts the two diffs and the REST body.
    CHECK(st.frames_in == 3);
    CHECK(decoder.adapter().has_baseline());

    // The seed emitted a Snapshot — WHEN THE BRACKETING DIFF ARRIVED, not when
    // the REST body did (M5 stage C). Before the seed, the first diff was
    // BUFFERED rather than dropped, which is the venue's documented procedure
    // and the reason a book seeded 1.4 s late is still correct.
    REQUIRE_FALSE(events.empty());
    CHECK(events[0].kind == depthcharge::FeedEvent::Kind::Snapshot);
    CHECK(st.buffered_events == 1);
    CHECK(st.buffered_dropped_by_seed == 1);
    CHECK(st.seed_bracket_ok == 1);
    CHECK(decoder.adapter().seed_confirmed());
    CHECK(BinanceTraceDecoder::name() == "binance");

    // And the transport gap still reaches the adapter through the frozen
    // signature, which is the other half of "filled, not reshaped".
    const std::size_t before = events.size();
    decoder.on_transport_gap(depthcharge::GapReason::Disconnect, sink);
    REQUIRE(events.size() == before + 1);
    CHECK(events.back().kind == depthcharge::FeedEvent::Kind::Gap);
    CHECK(events.back().reason == depthcharge::GapReason::Disconnect);
    CHECK_FALSE(decoder.adapter().has_baseline());
}
