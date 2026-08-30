// test_venue_build.cpp — the compile-time venue seam, and the bytes it sends.
//
// M4 stage D item A3. Two jobs, and the second is the one with a scar behind it.
//
// FIRST, IT MAKES THE OTHER ARM REACHABLE FROM CTEST. `venue_build.hpp` is a
// preprocessor fork, and a fork whose second branch is only ever compiled by a
// flash is a branch nothing checks. This file compiles under BOTH arms — into
// `dc_tests` at the default (Anvil) and into `dc_tests_kraken` with
// `-DDC_VENUE=DC_VENUE_KRAKEN` — and asserts the properties that must hold
// whichever branch was taken, plus the venue-specific facts behind `#if`. It is
// the same shape `dc_tests_noping` gave `DC_WS_PING`.
//
// SECOND, IT PINS THE SUBSCRIBE FRAME AGAINST THE COMMITTED CORPUS. Until this
// stage the exact bytes of a Kraken subscribe existed only in
// `tools/capture_kraken.py` and as recorded text inside the traces. A C++
// spelling that differs in key order or separators would still be ACCEPTED by
// Kraken — and the board would then be asking for something the corpus was never
// captured with, while every golden kept passing. That is precisely the A7
// failure `anvil_endpoint.hpp` records: `&depth=27` reached the shipping path
// and not the diag client's copy, nothing went red, and the instrument silently
// measured a different stream.
//
// So the assertion is against the traces themselves, read as text, rather than
// against a second literal in this file — a literal compared to a literal is two
// spellings agreeing with each other and with nothing on the wire.
#include <doctest/doctest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "kraken_endpoint.hpp"
#include "reject_log.hpp"
#include "venue_build.hpp"

namespace venue = depthcharge::fw::venue;

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "cannot open " << path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string first_line(const std::string& text) {
    const std::size_t nl = text.find('\n');
    return nl == std::string::npos ? text : text.substr(0, nl);
}

const std::string kResyncSlice = std::string(DC_REPLAY_DIR) +
                                 "/kraken_minagbp_d25_resync_20260818.ndjson";

}  // namespace

TEST_CASE("the venue seam declares one venue, and every field of it") {
    // Properties that must hold whichever arm compiled. A build whose adapter and
    // whose subscribe frame disagree about depth would draw a wrong top of book
    // and count the disagreement after the fact.
    CHECK(venue::kSymbol.valid());
    CHECK(venue::kPort == 443);
    CHECK(std::string(venue::kPortText) == "443");
    CHECK(std::string(venue::kHost).find('/') == std::string::npos);   // authority, not a URI
    CHECK(std::string(venue::kPath).rfind('/', 0) == 0);               // a request target
    CHECK(std::string(venue::kRootCaPem).rfind("-----BEGIN CERTIFICATE-----", 0) == 0);
    CHECK(venue::kName.size() > 0);
    CHECK(venue::kLivenessSignal.size() > 0);

    // A venue with a subscription to manage must have both frames; one without
    // must have neither. Half of either is the state that greys for ever.
    if (venue::kHasSubscription) {
        CHECK(std::string(venue::kSubscribeText).size() > 0);
        CHECK(std::string(venue::kUnsubscribeText).size() > 0);
    } else {
        CHECK(std::string(venue::kSubscribeText).empty());
        CHECK(std::string(venue::kUnsubscribeText).empty());
    }

    // THE ONE THAT COULD ACTUALLY BREAK. `kCanRequestResync == kHasSubscription`
    // used to be asserted here and was a constant compared with its own
    // definition one line away — review's point being that the constants
    // governed nothing at all while five `#if DC_VENUE == DC_VENUE_KRAKEN` sites
    // decided the real behaviour. The macro is what those sites read now, so
    // what is worth checking is that the macro and the constant agree — and the
    // `static_assert` in venue_build.hpp already does that at compile time, so
    // this is the runtime witness that the header under test was the one linked.
    CHECK(venue::kHasSubscription == (DC_VENUE_HAS_SUBSCRIPTION != 0));
}

TEST_CASE("the reject tally is the selected venue's, and every column has a name") {
    // A3 made `reject_log.hpp` compile against the SELECTED venue's ParseStatus,
    // and the CMake comment claimed dc_tests_kraken covered it. Review found
    // that it did not — the target's only source did not include the header — so
    // a Kraken `parse_status_name` that returned "?" for a value, or a
    // `kParseStatusCount` off by one, went green everywhere. Including it here
    // makes the claim true on both arms.
    using depthcharge::fw::kFirstRejectStatus;
    using depthcharge::fw::kParseStatusCount;
    using depthcharge::fw::parse_status_name;
    using depthcharge::fw::RejectLog;

    CHECK(kParseStatusCount == venue::kParseStatusCount);
    CHECK(kFirstRejectStatus == 1);

    // Every column the tally will print has a real name — "?" is the fallback a
    // forgotten `case` produces, and it renders as a column nobody can read.
    for (std::size_t i = 0; i < kParseStatusCount; ++i) {
        const char* name = parse_status_name(static_cast<venue::ParseStatus>(i));
        CAPTURE(i);
        REQUIRE(name != nullptr);
        CHECK(std::string(name) != "?");
        CHECK(std::string(name).size() > 0);
    }

    // And the names are distinct, so two columns cannot report as one.
    for (std::size_t i = 0; i < kParseStatusCount; ++i) {
        for (std::size_t j = i + 1; j < kParseStatusCount; ++j) {
            CAPTURE(i);
            CAPTURE(j);
            CHECK(std::string(parse_status_name(static_cast<venue::ParseStatus>(i))) !=
                  std::string(parse_status_name(static_cast<venue::ParseStatus>(j))));
        }
    }

    // The tally counts into the column it is told to, on this venue's enum.
    RejectLog log;
    log.note_connect(0);
    const auto first = static_cast<venue::ParseStatus>(kFirstRejectStatus);
    log.note(first, "{", 1, 1000);
    CHECK(log.total() == 1);
    CHECK(log.count(first) == 1);

    char line[256];
    log.render_tally(line, sizeof line);
    CHECK(std::string(line).find(parse_status_name(first)) != std::string::npos);
}

TEST_CASE("the spliced-second-frame scan looks for THIS venue's frame opening") {
    // Review found it hard-coded to Anvil's `{"type":`, which appears in no
    // Kraken v2 frame — so `SPLIT@nnnnn`, the field that diagnosed the entire
    // 2026-08-13 corruption, was a permanent no-op on the Kraken build while
    // still printing as though it had looked.
    //
    // THE FRAMES BELOW ARE LITERALS, NOT `venue::kFrameHeadPrefix`. The first
    // version of this case built its inputs out of the constant it was testing,
    // so a mutation that set the Kraken prefix back to Anvil's ALSO changed the
    // expectation and the case stayed green — the mutation run caught it. An
    // assertion whose expected value follows the thing under test proves
    // nothing, which is the species this project has a rule about.
    using depthcharge::fw::find_second_frame_header;

    const auto scan = [](const std::string& s) {
        return find_second_frame_header(s.data(), static_cast<std::uint32_t>(s.size()));
    };

#if DC_VENUE == DC_VENUE_KRAKEN
    // Real Kraken v2 shapes, spelled out.
    const std::string book = R"({"channel":"book","type":"update","data":[]})";
    const std::string beat = R"({"channel":"heartbeat"})";
    const std::string ack = R"({"method":"subscribe","success":true})";

    CHECK(scan(book) == 0);                       // one frame is not a splice
    CHECK(scan(book + beat) == book.size());      // channel + channel
    CHECK(scan(book + ack) == book.size());       // channel + method
    CHECK(scan(ack + book) == ack.size());        // and the other way round

    // Anvil's opening must NOT be what this build looks for.
    const std::string anvil = R"({"type":"book","ticker":101})";
    CHECK(scan(book + anvil) == 0);
#elif DC_VENUE == DC_VENUE_ANVIL
    const std::string book = R"({"type":"book","ticker":101,"seq":1})";
    const std::string summary = R"({"type":"summary","ticker":101})";

    CHECK(scan(book) == 0);
    CHECK(scan(book + summary) == book.size());

    // Kraken's opening must NOT be what this build looks for.
    const std::string kraken = R"({"channel":"heartbeat"})";
    CHECK(scan(book + kraken) == 0);
#else
    // Binance, added with `dc_tests_binance` at M5 stage D-A3. Its prefixes are
    // `{"e":` for a raw stream frame and `{"stream":` for a combined one, and
    // neither of the other two venues' openings may match.
    const std::string diff = R"({"e":"depthUpdate","E":1,"s":"BTCUSDT","U":1,"u":2})";
    const std::string combined = R"({"stream":"btcusdt@depth@100ms","data":{}})";

    CHECK(scan(diff) == 0);
    CHECK(scan(diff + diff) == diff.size());
    CHECK(scan(diff + combined) == diff.size());

    const std::string anvil = R"({"type":"book","ticker":101})";
    const std::string kraken = R"({"channel":"heartbeat"})";
    CHECK(scan(diff + anvil) == 0);
    CHECK(scan(diff + kraken) == 0);
#endif
}

#if DC_VENUE == DC_VENUE_KRAKEN

TEST_CASE("kraken arm: the venue is Kraken, and the checksum reaches ten levels a side") {
    CHECK(venue::kName == "kraken");
    CHECK(venue::kLivenessSignal == "heartbeat");
    CHECK(venue::kHasSubscription);
    CHECK(venue::kSubscribeDepth == 25);
    CHECK(venue::kValidatedDepth == depthcharge::kraken::kChecksumLevels);
    CHECK(venue::kValidatedDepth == 10);
    CHECK(venue::kSymbol.id == depthcharge::kraken::kKrakenMinaGbp.spec.id);
    CHECK(venue::kSymbol.price_decimals == 4);
    CHECK(venue::kSymbol.qty_decimals == 8);
    CHECK(std::string(venue::kHost) == "ws.kraken.com");
    CHECK(std::string(venue::kPath) == "/v2");
}

TEST_CASE("kraken arm: the liveness counter is the heartbeat and nothing else") {
    // The one venue fact the feed task reads on every frame. If this counted
    // book messages the panel would grey through MINA/GBP's healthy silences,
    // which is the failure the whole 2026-08-17 ruling exists to prevent.
    venue::Adapter a = venue::make_adapter();
    CHECK(venue::liveness_count(a, 0) == 0);

    auto sink = [](const depthcharge::FeedEvent&) {};
    a.on_frame(R"({"channel":"heartbeat"})", sink);
    CHECK(venue::liveness_count(a, 0) == 1);

    a.on_frame(R"({"channel":"status","type":"update","data":[{"system":"online"}]})", sink);
    CHECK(venue::liveness_count(a, 0) == 1);       // a status frame is not a clock

    // And the server-ping argument is inert here too (M5 stage D-A3): this
    // venue's clock is an application frame, so a socket-level ping must not
    // move it.
    CHECK(venue::liveness_count(a, 1'000'000) == 1);
}

TEST_CASE("kraken arm: the adapter is built at the depth the subscribe asks for") {
    venue::Adapter a = venue::make_adapter();
    CHECK(a.depth() == static_cast<std::uint32_t>(venue::kSubscribeDepth));
    CHECK(a.wire_symbol() == depthcharge::fw::kKrakenWireSymbol);
}

#elif DC_VENUE == DC_VENUE_ANVIL

// ANVIL BY NAME RATHER THAN BY `#else`, since M5 stage D-A3 added a third
// venue target. `#else` meant "not Kraken", which was Anvil while there were
// two arms and became "Anvil or Binance" the moment `dc_tests_binance` existed --
// so every Anvil fact below was asserted against a Binance build and failed. A
// two-branch conditional that names only one of its branches is a trap that
// springs on whoever adds the third.

TEST_CASE("anvil arm: the venue is Anvil, and NO rendered row is externally confirmed") {
    CHECK(venue::kName == "anvil");
    CHECK(venue::kLivenessSignal == "summary");
    CHECK_FALSE(venue::kHasSubscription);
    // Zero as a STATEMENT, not an unset field: this protocol publishes no
    // checksum of any kind, so the serial line has to say so in words rather
    // than print a percentage of nothing (DESIGN strain 24).
    CHECK(venue::kValidatedDepth == 0);
    CHECK(venue::kSymbol.id == 101);
    CHECK(std::string(venue::kHost) == "anvil.garethcooke.com");
    CHECK(std::string(venue::kPath).find("depth=27") != std::string::npos);
}

TEST_CASE("anvil arm: the liveness counter is the 2 Hz summary and nothing else") {
    venue::Adapter a = venue::make_adapter();
    CHECK(venue::liveness_count(a, 0) == 0);

    auto sink = [](const depthcharge::FeedEvent&) {};
    a.on_frame(R"({"type":"summary","ticker":101,"seq":1})", sink);
    CHECK(venue::liveness_count(a, 0) == 1);
}

TEST_CASE("anvil arm: the server-ping argument is INERT here, and that is the signature's whole point") {
    // M5 stage D-A3 gave `liveness_count` one signature across three venues so
    // that `feed_task.cpp` need not know which venue it was compiled for. The
    // cost of that is a parameter two of the three venues ignore, and the risk
    // is that somebody later "tidies" it into a venue that should not read it:
    // at Anvil a WebSocket ping is a control frame from the SOCKET, not the
    // publisher, and counting it as liveness would be a market-event-dressed-
    // as-a-clock of exactly the kind this file's comments forbid.
    venue::Adapter a = venue::make_adapter();
    CHECK(venue::liveness_count(a, 0) == 0);
    CHECK(venue::liveness_count(a, 1'000'000) == 0);   // pings do not move it here

    auto sink = [](const depthcharge::FeedEvent&) {};
    a.on_frame(R"({"type":"summary","ticker":101,"seq":1})", sink);
    CHECK(venue::liveness_count(a, 0) == 1);
    CHECK(venue::liveness_count(a, 1'000'000) == 1);   // still only the summary
}

#endif

#if DC_VENUE == DC_VENUE_BINANCE

// =====================================================================
// M5 STAGE D-A3, DELIVERABLE 1 — THE PING IS THE CLOCK
// =====================================================================

TEST_CASE("binance arm: the liveness counter IS the server ping, and nothing on the adapter") {
    // This venue emits neither a `summary` nor a `heartbeat`. Its clock is a
    // WebSocket PING every ~20 s, answered on the RX task, which never becomes a
    // message and so never reaches the adapter. Until D-A3 this function
    // returned a constant 0 and the watchdog was never armed at all.
    venue::Adapter a = venue::make_adapter();

    CHECK(venue::liveness_count(a, 0) == 0);
    CHECK(venue::liveness_count(a, 1) == 1);
    CHECK(venue::liveness_count(a, 17) == 17);

    // It is the ping and ONLY the ping: no amount of market data moves it. This
    // is the assertion that stops a later edit reaching for `frames_in` or
    // `diff_frames`, either of which would compile, would stamp the watchdog,
    // and would be a market event dressed as a clock.
    auto sink = [](const depthcharge::FeedEvent&) {};
    a.on_frame(R"({"e":"depthUpdate","E":1,"s":"BTCUSDT","U":1,"u":2,"b":[],"a":[]})", sink);
    CHECK(venue::liveness_count(a, 0) == 0);
    CHECK(venue::liveness_count(a, 5) == 5);
}

TEST_CASE("binance arm: the venue says its signal is the ping, and carries stage C's policy") {
    // The label reaches the `-- age` serial line, so a stale one misdescribes
    // every soak log. It said "server-ping (NOT WIRED — see venue_build.hpp)"
    // until the wire existed.
    CHECK(venue::kLivenessSignal == "server-ping");
    CHECK(venue::kLivenessSignal.find("NOT WIRED") == std::string_view::npos);

    // And deliverable 2: the board carries the derived policy rather than the
    // shipping defaults it default-constructed until this stage.
    CHECK(venue::kLivenessPolicy.multiple == doctest::Approx(2.0));
    CHECK(venue::kLivenessPolicy.ceiling_ms == doctest::Approx(60'000.0));
    CHECK(venue::kLivenessPolicy.uncalibrated_ms() == doctest::Approx(60'000.0));
}

#endif

// --- the corpus pin, which runs under both arms -----------------------------
//
// kraken_endpoint.hpp is includable regardless of DC_VENUE, so these run on the
// default build too. That is deliberate: the pin is most useful on the build
// that is NOT currently asking the question, because that is the build nobody
// is watching when the constant drifts.

TEST_CASE("the subscribe frame is the one the corpus was captured with, byte for byte") {
    const std::string meta_line = first_line(read_file(kResyncSlice));
    const nlohmann::json meta = nlohmann::json::parse(meta_line);

    REQUIRE(meta.contains("subscribe"));
    CHECK(meta["subscribe"].get<std::string>() ==
          std::string(depthcharge::fw::kKrakenSubscribeText));

    // And the two other things the header claims about it, checked against the
    // same line rather than against the header's own static_asserts.
    CHECK(meta["depth"].get<int>() == depthcharge::kraken::kKrakenSubscribeDepth);
    CHECK(meta["symbol"].get<std::string>() == std::string(depthcharge::fw::kKrakenWireSymbol));
    CHECK(meta["url"].get<std::string>() ==
          "wss://" + std::string(depthcharge::fw::kKrakenHost) +
              std::string(depthcharge::fw::kKrakenPath));
}

TEST_CASE("the unsubscribe frame is the only one this project has ever sent") {
    // It exists in exactly one place on the wire: the deliberate mid-stream
    // resubscribe that produced the resync slice. That single `tx` record is the
    // entire evidence base for the frame the healing path will send.
    const std::string text = read_file(kResyncSlice);

    std::size_t found = 0;
    std::size_t at = 0;
    const std::string want = std::string("\"frame\": ") + depthcharge::fw::kKrakenUnsubscribeText;
    while ((at = text.find(want, at)) != std::string::npos) { ++found; at += want.size(); }
    CHECK(found == 1);

    // The subscribe appears twice in the same slice — the opening one and the
    // one that healed it — which is what makes this trace the resync trace.
    found = 0;
    at = 0;
    const std::string want_sub = std::string("\"frame\": ") + depthcharge::fw::kKrakenSubscribeText;
    while ((at = text.find(want_sub, at)) != std::string::npos) { ++found; at += want_sub.size(); }
    CHECK(found == 2);
}
