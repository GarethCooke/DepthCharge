// test_parser_equivalence.cpp — one table, both implementations of the seam.
//
// M3 stage B links a second parse_anvil_frame (streaming, allocation-free) beside
// the M1 nlohmann reference. The goldens prove the two agree on the 2694 frames
// Anvil actually sent; this file is where they are held to the same answer on the
// JSON grammar those frames never exercise — escapes, surrogate pairs, duplicate
// keys, leading zeros, over-range integers, invalid UTF-8, trailing junk.
//
// It is compiled into BOTH dc_tests and dc_tests_streaming, which is the whole
// point: nothing here is an assertion about the streaming parser, it is an
// assertion about the *declaration* in anvil_frame.hpp, and each implementation
// has to satisfy it. A case that only one of them can pass is a bug in one of
// them or a divergence that belongs in test_streaming_parser.cpp with its reason
// written down — not a #ifdef here.
//
// Where a rule looked arbitrary it was read out of the vendored nlohmann 3.11.3
// source rather than guessed, because the reference decoder's behaviour IS the
// specification for the replacement. The three that most often go wrong in a
// hand-rolled parser, and are therefore pinned hardest below:
//
//   1. DUPLICATE KEYS: the LAST occurrence wins and replaces the earlier value
//      wholesale. nlohmann's object is a std::map filled through operator[]
//      followed by assignment, so a repeat overwrites. A parser that keeps the
//      first — the natural result of insert()/emplace() or of "record it if we
//      haven't seen it" — silently disagrees.
//   2. ERROR PRECEDENCE IS THE REFERENCE'S FIELD ORDER, NOT THE WIRE'S. nlohmann
//      builds the whole document and then asks type -> seq -> ticker -> payload.
//      A left-to-right scanner meets the fields in whatever order the server
//      serialised them, so a frame that is wrong in two places must still report
//      the error the reference would. These land in *different* adapter
//      counters (other_ticker vs price_errors vs parse_errors), so getting it
//      wrong moves a golden.
//   3. A NUL BYTE ENDS THE DOCUMENT. nlohmann's lexer treats '\0' as
//      end-of-input even though it was handed an explicit range, so trailing
//      bytes after a NUL are never inspected.
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <depthcharge/anvil/anvil_adapter.hpp>
#include <depthcharge/anvil/anvil_frame.hpp>

using depthcharge::Side;
using depthcharge::anvil::AnvilFrame;
using depthcharge::anvil::FrameKind;
using depthcharge::anvil::kAnvilTicker101;
using depthcharge::anvil::parse_anvil_frame;
using depthcharge::anvil::ParseStatus;

namespace {

// A well-formed book frame with one bid, used as the base for perturbations.
constexpr const char* kGoodBook =
    R"({"type":"book","seq":7,"ticker":101,)"
    R"("bids":[{"price":"9.9972","qty":9,"orders":1}],"asks":[]})";

ParseStatus parse(std::string_view json, AnvilFrame& frame) {
    return parse_anvil_frame(json, kAnvilTicker101, frame);
}

ParseStatus status_of(std::string_view json) {
    AnvilFrame frame{};
    return parse(json, frame);
}

struct GrammarCase {
    const char* what;
    std::string json;
    ParseStatus expected;
};

// Most grammar cases are "a perfectly good book frame carrying ONE odd thing".
// These two put the odd thing in a slot and leave the rest of the frame alone,
// so a table row shows only what is actually under test — and so a change to the
// surrounding frame happens in one place rather than twenty.
//
// `extra` goes in a member the decoder ignores entirely, which isolates the
// question to "does the JSON scanner accept these bytes"; `seq` goes in a member
// it reads but is never allowed to fail on.
std::string with_extra(std::string_view raw_json_value) {
    std::string s = R"({"type":"book","ticker":101,"bids":[],"asks":[],"x":)";
    s.append(raw_json_value);
    s.append("}");
    return s;
}

std::string with_seq(std::string_view raw_json_value) {
    std::string s = R"({"type":"book","seq":)";
    s.append(raw_json_value);
    s.append(R"(,"ticker":101,"bids":[],"asks":[]})");
    return s;
}

// A JSON string containing one raw byte — the way to ask "is 0x1F legal inside a
// string", which cannot be written as a literal.
std::string quoted_byte(unsigned char b) {
    std::string s = "\"";
    s.push_back(static_cast<char>(b));
    s.append("\"");
    return s;
}

std::string repeated(std::string_view unit, std::size_t n) {
    std::string s;
    s.reserve(unit.size() * n);
    for (std::size_t i = 0; i < n; ++i) { s.append(unit); }
    return s;
}

}  // namespace

// --- JSON syntax: what is a document at all ---------------------------------

TEST_CASE("both parsers agree on JSON syntax, including the corners Anvil never sends") {
    const GrammarCase cases[] = {
        // -- accepted ---------------------------------------------------------
        {"the baseline good frame", kGoodBook, ParseStatus::Ok},
        {"whitespace everywhere a token may be preceded",
         "  {  \"type\" : \"book\" ,  \"ticker\" : 101 ,"
         "  \"bids\" : [ ] ,  \"asks\" : [ ]  }  \t\r\n",
         ParseStatus::Ok},
        {"empty containers as ignored payload",
         R"({"type":"book","ticker":101,"bids":[],"asks":[],"extra":{},"more":[]})",
         ParseStatus::Ok},
        {"deeply structured ignored payload",
         with_extra(R"({"a":[1,2,{"b":null,"c":[true,false]}],"d":"s"})"), ParseStatus::Ok},
        {"a UTF-8 BOM is skipped at offset 0",
         std::string("\xEF\xBB\xBF") + kGoodBook, ParseStatus::Ok},
        {"a NUL ends the document; trailing bytes are never seen",
         std::string(kGoodBook) + std::string("\0garbage{{{", 11), ParseStatus::Ok},
        {"valid UTF-8 in an ignored string",
         with_extra("\"\xE2\x82\xAC\""), ParseStatus::Ok},
        // Written with doubled backslashes rather than a raw literal so the
        // bytes handed to the parser are unmistakably the six characters
        // \ u D 8 3 4 — a JSON escape — and not a UTF-8 G-clef in this source.
        {"a valid escaped surrogate pair in an ignored string",
         with_extra("\"\\uD834\\uDD1E\""), ParseStatus::Ok},
        {"an escaped NUL inside an ignored string is legal",
         with_extra("\"a\\u0000b\""), ParseStatus::Ok},
        {"every simple escape, in an ignored string",
         with_extra(R"("\"\\\/\b\f\n\r\tA")"), ParseStatus::Ok},
        {"raw DEL (0x7F) is not a control character",
         with_extra(quoted_byte(0x7Fu)), ParseStatus::Ok},
        {"nesting the reference and the target both accept",
         with_extra(repeated("[", 40) + repeated("]", 40)), ParseStatus::Ok},

        // -- rejected: structure ---------------------------------------------
        {"empty input", "", ParseStatus::NotJson},
        {"whitespace only", "   \t\n", ParseStatus::NotJson},
        {"trailing content after the object",
         std::string(kGoodBook) + "x", ParseStatus::NotJson},
        {"a second document after the first",
         std::string(kGoodBook) + kGoodBook, ParseStatus::NotJson},
        {"trailing comma in an object", R"({"type":"book",})", ParseStatus::NotJson},
        {"trailing comma in an array", with_extra("[1,]"), ParseStatus::NotJson},
        {"mismatched brackets",
         R"({"type":"book","ticker":101,"bids":[},"asks":[]})", ParseStatus::NotJson},
        {"a comment", std::string(kGoodBook) + " // hi", ParseStatus::NotJson},
        {"vertical tab is not JSON whitespace",
         "\v" + std::string(kGoodBook), ParseStatus::NotJson},
        {"form feed is not JSON whitespace",
         "\f" + std::string(kGoodBook), ParseStatus::NotJson},
        {"an incomplete BOM", std::string("\xEF\xBB") + kGoodBook, ParseStatus::NotJson},
        {"a bare key with no value", R"({"type"})", ParseStatus::NotJson},
        {"an unquoted key", R"({type:"book"})", ParseStatus::NotJson},

        // -- rejected: numbers ------------------------------------------------
        // `seq` is the slot the decoder reads but may never fail on, so a
        // rejection here can only have come from the JSON scanner.
        {"a leading zero", R"({"type":"book","ticker":0101,"bids":[],"asks":[]})",
         ParseStatus::NotJson},
        {"a leading plus", R"({"type":"book","ticker":+1,"bids":[],"asks":[]})",
         ParseStatus::NotJson},
        {"a bare fraction", with_seq(".5"), ParseStatus::NotJson},
        {"a trailing decimal point", with_seq("1."), ParseStatus::NotJson},
        {"an exponent with no digits", with_seq("1e"), ParseStatus::NotJson},
        {"Infinity", with_seq("Infinity"), ParseStatus::NotJson},
        {"NaN", with_seq("NaN"), ParseStatus::NotJson},
        {"a hex literal", R"({"type":"book","ticker":0x65,"bids":[],"asks":[]})",
         ParseStatus::NotJson},
        // A float too large for a double is an out_of_range *parse* error in the
        // reference — the whole document is discarded, not just the field.
        {"a number that overflows a double", with_extra("1e400"), ParseStatus::NotJson},
        {"an integer literal that overflows a double",
         with_extra(repeated("9", 400)), ParseStatus::NotJson},

        // -- rejected: strings -------------------------------------------------
        {"an unterminated string", R"({"type":"boo)", ParseStatus::NotJson},
        {"a forbidden escape", with_extra(R"("\a")"), ParseStatus::NotJson},
        {"a short \\u escape", with_extra(R"("\u00")"), ParseStatus::NotJson},
        {"a non-hex \\u escape", with_extra(R"("\u00G1")"), ParseStatus::NotJson},
        {"a lone high surrogate", with_extra(R"("\uD834")"), ParseStatus::NotJson},
        {"a lone low surrogate", with_extra(R"("\uDD1E")"), ParseStatus::NotJson},
        {"a raw tab inside a string", with_extra(quoted_byte(0x09u)), ParseStatus::NotJson},
        {"a raw 0x1F inside a string", with_extra(quoted_byte(0x1Fu)), ParseStatus::NotJson},
        {"a stray continuation byte",
         with_extra(quoted_byte(0x80u)), ParseStatus::NotJson},
        {"an invalid lead byte", with_extra(quoted_byte(0xFFu)), ParseStatus::NotJson},
        {"an overlong two-byte encoding",
         with_extra("\"\xC0\xAF\""), ParseStatus::NotJson},
        {"a UTF-8-encoded surrogate",
         with_extra("\"\xED\xA0\x80\""), ParseStatus::NotJson},
        {"a truncated multi-byte sequence",
         with_extra("\"\xE2\""), ParseStatus::NotJson},

        // -- rejected: shape, via over-range integers -------------------------
        // Too big for uint64 => the reference stores it as a float, so
        // is_number_integer() is false and the qty is a shape error, not a
        // saturated quantity.
        {"a qty too large for uint64",
         R"({"type":"trade","ticker":101,"price":"10","qty":99999999999999999999,)"
         R"("aggr":"B"})",
         ParseStatus::BadShape},
        // Fits in uint64 but not int64: the reference accepts it as an integer
        // and then get<int64_t>() wraps it negative, which its `raw < 0` guard
        // rejects. Same answer, different route — and a parser that rejected at
        // scan time would agree here but disagree on the ticker case below.
        {"a qty that wraps negative through int64",
         R"({"type":"trade","ticker":101,"price":"10","qty":18446744073709551615,)"
         R"("aggr":"B"})",
         ParseStatus::BadShape},
        {"a ticker that wraps negative through int64",
         R"({"type":"book","ticker":18446744073709551615,"bids":[],"asks":[]})",
         ParseStatus::BadShape},
    };

    for (const GrammarCase& tc : cases) {
        const std::string label = std::string("case: ") + tc.what;
        INFO(label);
        CHECK(status_of(tc.json) == tc.expected);
    }
}

// --- duplicate keys: the last occurrence wins, wholesale --------------------

TEST_CASE("a repeated key takes its value from the last occurrence") {
    AnvilFrame frame{};

    SUBCASE("type") {
        REQUIRE(parse(R"({"type":"book","type":"trade","ticker":101,)"
                      R"("price":"9.9972","qty":9,"aggr":"S"})",
                      frame) == ParseStatus::Ok);
        CHECK(frame.kind == FrameKind::Trade);
        CHECK(frame.trade_px == 99972);
        CHECK(frame.aggressor == Side::Ask);
    }

    SUBCASE("type, where only the last one is a string") {
        CHECK(parse(R"({"type":"book","type":7,"ticker":101,"bids":[],"asks":[]})", frame) ==
              ParseStatus::MissingType);
    }

    SUBCASE("type, where only the first one is a string") {
        REQUIRE(parse(R"({"type":7,"type":"book","ticker":101,"bids":[],"asks":[]})", frame) ==
                ParseStatus::Ok);
        CHECK(frame.kind == FrameKind::Book);
    }

    SUBCASE("ticker: a foreign one replaced by ours") {
        REQUIRE(parse(R"({"type":"book","ticker":107,"ticker":101,"bids":[],"asks":[]})",
                      frame) == ParseStatus::Ok);
        CHECK(frame.ticker == 101);
    }

    SUBCASE("ticker: ours replaced by a foreign one") {
        CHECK(parse(R"({"type":"book","ticker":101,"ticker":107,"bids":[],"asks":[]})",
                    frame) == ParseStatus::OtherTicker);
    }

    SUBCASE("seq: a good one replaced by a non-integer is not recorded at all") {
        REQUIRE(parse(R"({"type":"book","seq":9,"seq":"x","ticker":101,)"
                      R"("bids":[],"asks":[]})",
                      frame) == ParseStatus::Ok);
        CHECK_FALSE(frame.has_wire_seq);
        CHECK(frame.wire_seq == 0);
    }

    SUBCASE("a whole side: the bad one is discarded when a good one follows") {
        REQUIRE(parse(R"({"type":"book","ticker":101,)"
                      R"("bids":[{"price":"nonsense","qty":1}],)"
                      R"("bids":[{"price":"10.0","qty":4}],"asks":[]})",
                      frame) == ParseStatus::Ok);
        REQUIRE(frame.bid_count == 1);
        CHECK(frame.bids[0].px == 100000);
        CHECK(frame.bids[0].qty == 4);
    }

    SUBCASE("a whole side: a good one is not rescued by having been good first") {
        CHECK(parse(R"({"type":"book","ticker":101,)"
                    R"("bids":[{"price":"10.0","qty":4}],)"
                    R"("bids":[{"price":"9.99725","qty":1}],"asks":[]})",
                    frame) == ParseStatus::BadPrice);
    }

    SUBCASE("a level's own price key") {
        REQUIRE(parse(R"({"type":"book","ticker":101,)"
                      R"("bids":[{"price":"10.0","price":"9.9972","qty":4}],"asks":[]})",
                      frame) == ParseStatus::Ok);
        REQUIRE(frame.bid_count == 1);
        CHECK(frame.bids[0].px == 99972);
    }
}

// --- error precedence: the reference's field order, not the wire's ----------

TEST_CASE("when a frame is wrong in two places, the reported error is the reference's") {
    // Each of these puts the payload BEFORE the field the reference checks
    // first, so a parser that reports whichever error it trips over first gets a
    // different answer — and the adapter files it under a different counter.
    SUBCASE("a foreign ticker beats a bad price, even when the price came first") {
        CHECK(status_of(R"({"bids":[{"price":"9.99725","qty":1}],"asks":[],)"
                        R"("type":"book","ticker":107})") == ParseStatus::OtherTicker);
    }

    SUBCASE("a foreign ticker beats a bad trade price") {
        CHECK(status_of(R"({"price":"9.99725","qty":1,"aggr":"B",)"
                        R"("type":"trade","ticker":107})") == ParseStatus::OtherTicker);
    }

    SUBCASE("a missing ticker beats a bad price") {
        CHECK(status_of(R"({"bids":[{"price":"9.99725","qty":1}],"asks":[],)"
                        R"("type":"book"})") == ParseStatus::BadShape);
    }

    SUBCASE("a missing type beats everything") {
        CHECK(status_of(R"({"bids":[{"price":"9.99725","qty":1}],"asks":[],)"
                        R"("ticker":107})") == ParseStatus::MissingType);
    }

    SUBCASE("a syntax error beats every semantic error") {
        CHECK(status_of(R"({"bids":[{"price":"9.99725","qty":1}],"asks":[],)"
                        R"("type":"book","ticker":107},)") == ParseStatus::NotJson);
    }

    SUBCASE("bids beat asks, even when asks came first on the wire") {
        // asks would be BadPrice; bids is BadShape. The reference parses bids
        // first, so BadShape is the answer.
        CHECK(status_of(R"({"asks":[{"price":"9.99725","qty":1}],)"
                        R"("bids":[{"price":1,"qty":1}],)"
                        R"("type":"book","ticker":101})") == ParseStatus::BadShape);
    }

    SUBCASE("a missing aggr beats a bad trade price") {
        // The reference checks that price, qty and aggr are all *present* before
        // decoding any of them.
        CHECK(status_of(R"({"type":"trade","ticker":101,"price":"9.99725","qty":1})") ==
              ParseStatus::BadShape);
    }

    SUBCASE("a bad trade price beats a bad aggr") {
        CHECK(status_of(R"({"type":"trade","ticker":101,"price":"9.99725",)"
                        R"("qty":1,"aggr":"X"})") == ParseStatus::BadPrice);
    }

    SUBCASE("a level's missing key beats a bad price in a later level") {
        CHECK(status_of(R"({"type":"book","ticker":101,)"
                        R"("bids":[{"price":"10.0"},{"price":"9.99725","qty":1}],)"
                        R"("asks":[]})") == ParseStatus::BadShape);
    }
}

// --- kinds that ignore their payload ----------------------------------------

TEST_CASE("summary and unknown frames never look at the ticker or the payload") {
    AnvilFrame frame{};

    SUBCASE("a summary carrying a malformed book is still a summary") {
        REQUIRE(parse(R"({"type":"summary","bids":[{"price":"nope","qty":-1}],)"
                      R"("tickers":[{"ticker":101,"last":"9.9975"}]})",
                      frame) == ParseStatus::Ok);
        CHECK(frame.kind == FrameKind::Summary);
        CHECK(frame.bid_count == 0);
        CHECK_FALSE(frame.levels_truncated);
    }

    SUBCASE("a summary carrying someone else's ticker is not OtherTicker") {
        REQUIRE(parse(R"({"type":"summary","ticker":107,"tickers":[]})", frame) ==
                ParseStatus::Ok);
        CHECK(frame.kind == FrameKind::Summary);
        CHECK_FALSE(frame.has_ticker);   // the reference never reads it for summary
        CHECK(frame.ticker == 0);
    }

    SUBCASE("an unknown kind carrying someone else's ticker is not OtherTicker") {
        REQUIRE(parse(R"({"type":"error","ticker":107,"code":"nope"})", frame) ==
                ParseStatus::Ok);
        CHECK(frame.kind == FrameKind::Unknown);
        CHECK_FALSE(frame.has_ticker);
    }

    SUBCASE("a book frame's stray top-level price and qty are ignored") {
        REQUIRE(parse(R"({"type":"book","ticker":101,"price":"9.99725","qty":-4,)"
                      R"("bids":[],"asks":[]})",
                      frame) == ParseStatus::Ok);
        CHECK(frame.kind == FrameKind::Book);
        CHECK(frame.trade_px == 0);
        CHECK(frame.trade_qty == 0);
    }

    SUBCASE("a trade frame's stray bids are ignored") {
        REQUIRE(parse(R"({"type":"trade","ticker":101,"price":"9.9972","qty":9,)"
                      R"("aggr":"B","bids":[{"price":"nope","qty":1}]})",
                      frame) == ParseStatus::Ok);
        CHECK(frame.kind == FrameKind::Trade);
        CHECK(frame.bid_count == 0);
    }
}

// --- the decoded values themselves ------------------------------------------

TEST_CASE("escaped tokens decode to the same values as their plain spelling") {
    // Anvil has never emitted an escape — measured, zero backslashes across all
    // 2694 committed frames — but the reference unescapes before it compares,
    // and a streaming parser that slices the raw bytes would read "\u0062ook" as
    // an unknown frame kind and silently stop drawing the ladder. Doubled
    // backslashes in ordinary literals, so what reaches the parser is beyond
    // doubt.
    AnvilFrame frame{};

    SUBCASE("an escaped key and an escaped type value") {
        // "\u0074ype" is "type"; "boo\u006b" is "book".
        REQUIRE(parse("{\"\\u0074ype\":\"boo\\u006b\",\"ticker\":101,"
                      "\"bids\":[],\"asks\":[]}",
                      frame) == ParseStatus::Ok);
        CHECK(frame.kind == FrameKind::Book);
    }

    SUBCASE("an escaped price string") {
        // "\u0039.9972" is "9.9972" -> 99972 ticks at the declared 10^-4 scale.
        REQUIRE(parse("{\"type\":\"book\",\"ticker\":101,"
                      "\"bids\":[{\"price\":\"\\u0039.9972\",\"qty\":9}],\"asks\":[]}",
                      frame) == ParseStatus::Ok);
        REQUIRE(frame.bid_count == 1);
        CHECK(frame.bids[0].px == 99972);
    }

    SUBCASE("an escaped level key") {
        REQUIRE(parse("{\"type\":\"book\",\"ticker\":101,"
                      "\"bids\":[{\"\\u0070rice\":\"10.0\",\"qty\":4}],\"asks\":[]}",
                      frame) == ParseStatus::Ok);
        REQUIRE(frame.bid_count == 1);
        CHECK(frame.bids[0].px == 100000);
    }

    SUBCASE("an escaped aggressor letter") {
        REQUIRE(parse("{\"type\":\"trade\",\"ticker\":101,\"price\":\"10\","
                      "\"qty\":1,\"aggr\":\"\\u0053\"}",
                      frame) == ParseStatus::Ok);
        CHECK(frame.aggressor == Side::Ask);
    }
}

TEST_CASE("the server trims trailing zeros, so a price may carry 0 to 4 decimals") {
    // Measured over both committed traces: 372 prices are the bare string "10",
    // 5,819 carry two decimals, 42,945 three and 382,409 four. A decoder that
    // assumed a decimal point, or assumed four places, would mis-scale the first
    // group by a factor of 10,000 — and it is the *bid* side that carries them.
    struct PriceCase { const char* text; depthcharge::PriceTicks ticks; };
    const PriceCase cases[] = {
        {"10", 100000}, {"9.99", 99900}, {"9.985", 99850}, {"9.9972", 99972},
        {"10.1302", 101302}, {"9.8877", 98877},
    };

    for (const PriceCase& tc : cases) {
        const std::string json = std::string(R"({"type":"book","ticker":101,"bids":[{"price":")") +
                                 tc.text + R"(","qty":1}],"asks":[]})";
        INFO(json);
        AnvilFrame frame{};
        REQUIRE(parse(json, frame) == ParseStatus::Ok);
        REQUIRE(frame.bid_count == 1);
        CHECK(frame.bids[0].px == tc.ticks);
    }
}

TEST_CASE("a negative wire seq is recorded; it is diagnostics, not ordering") {
    AnvilFrame frame{};
    REQUIRE(parse(R"({"type":"book","seq":-5,"ticker":101,"bids":[],"asks":[]})", frame) ==
            ParseStatus::Ok);
    CHECK(frame.has_wire_seq);
    CHECK(frame.wire_seq == -5);
}

TEST_CASE("a non-integer seq is ignored rather than treated as an error") {
    // The reference guards with `is_number_integer()` and simply does not record
    // a seq it cannot read. It never fails the frame for it.
    const char* const shapes[] = {
        R"({"type":"book","seq":1.5,"ticker":101,"bids":[],"asks":[]})",
        R"({"type":"book","seq":1e3,"ticker":101,"bids":[],"asks":[]})",
        R"({"type":"book","seq":"9","ticker":101,"bids":[],"asks":[]})",
        R"({"type":"book","seq":null,"ticker":101,"bids":[],"asks":[]})",
        R"({"type":"book","seq":99999999999999999999,"ticker":101,"bids":[],"asks":[]})",
    };
    for (const char* json : shapes) {
        INFO(json);
        AnvilFrame frame{};
        REQUIRE(parse(json, frame) == ParseStatus::Ok);
        CHECK_FALSE(frame.has_wire_seq);
    }
}

TEST_CASE("the two sides have independent depth budgets") {
    // 200 levels a side: under the 256 cap individually, over it combined. The
    // flag must stay false — it means "this frame was deeper than we carry",
    // and the two sides have independent budgets.
    std::string json = R"({"type":"book","ticker":101,"bids":[)";
    for (int i = 0; i < 200; ++i) {
        if (i > 0) { json += ','; }
        json += R"({"price":"10.0","qty":1})";
    }
    json += R"(],"asks":[)";
    for (int i = 0; i < 200; ++i) {
        if (i > 0) { json += ','; }
        json += R"({"price":"10.1","qty":1})";
    }
    json += R"(]})";

    AnvilFrame frame{};
    REQUIRE(parse(json, frame) == ParseStatus::Ok);
    CHECK(frame.bid_count == 200);
    CHECK(frame.ask_count == 200);
    CHECK_FALSE(frame.levels_truncated);
}

TEST_CASE("truncation on the ask side alone still flags the frame") {
    std::string json = R"({"type":"book","ticker":101,"bids":[],"asks":[)";
    const std::size_t deep = depthcharge::kMaxSnapshotLevels + 10;
    for (std::size_t i = 0; i < deep; ++i) {
        if (i > 0) { json += ','; }
        json += R"({"price":"10.0","qty":1})";
    }
    json += R"(]})";

    AnvilFrame frame{};
    REQUIRE(parse(json, frame) == ParseStatus::Ok);
    CHECK(frame.ask_count == depthcharge::kMaxSnapshotLevels);
    CHECK(frame.levels_truncated);
    CHECK(frame.bid_count == 0);
}
