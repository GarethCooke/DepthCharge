// firmware/src/kraken_endpoint.hpp — where the board connects at Kraken, and
// what it says when it gets there.
//
// The counterpart of anvil_endpoint.hpp, and it has one job that file does not:
// **Anvil's depth is in the URL and Kraken's is in a frame the client sends
// after the upgrade.** So this header carries not only host/path/port but the
// exact bytes of a subscribe and an unsubscribe — the first time this project
// has spelled either in C++ rather than in `tools/capture_kraken.py`.
//
// THE BYTES ARE PINNED AGAINST THE COMMITTED CORPUS, not merely written out.
// Every slice in `harness/replay/` records the subscribe it was captured with
// in its metadata header, and `test_kraken_endpoint.cpp` compares this constant
// against that recorded text byte for byte. Without that, a hand-written C++
// spelling that differs in key order or separators would still be ACCEPTED by
// Kraken and would silently stop being the frame the corpus was captured with —
// the goldens would keep passing while the board asked for something else. This
// is the same failure anvil_endpoint.hpp's own header records: `&depth=27` was
// added to the shipping path and not to the diag client's copy, nothing went
// red, and the instrument silently measured a different stream.
//
// TWO NUMBERS THAT MUST AGREE, AND NOW CANNOT DRIFT. The `depth` inside the
// subscribe JSON and the depth handed to `KrakenAdapter`'s constructor are the
// same fact spelled twice; the adapter counts a disagreement in
// `ack_depth_mismatch` after the event, and its own depth silently wins — a
// wrong top of book. The `static_assert`s below read the number back out of the
// JSON and compare it to `kraken::kKrakenSubscribeDepth`, so the two are checked
// by the compiler rather than by whoever edits one of them.
//
// THE PAIR IS MINA/GBP, AND THE CHOICE IS THE BENCH CRITERION'S. M4's
// definition of done requires the panel to hold colour through 26 s of
// legitimate book silence, and MINA/GBP is the pair that produces such silences
// — 25,843 ms measured, the number the whole staleness ruling rests on. BTC/USD
// would never exercise it. The other declared pair is one constant away
// (`kraken::kKrakenBtcUsd`) if a bench evening wants a liquid book instead.
#pragma once

#include <cstdint>
#include <string_view>

#include <depthcharge/kraken/kraken_adapter.hpp>

#include "ws_frame.hpp"

namespace depthcharge::fw {

// Same three facts and one derived spelling as anvil_endpoint.hpp: the
// authority, the request target, and the port in both the forms the two APIs
// want it in, held together by a static_assert.
inline constexpr char kKrakenHost[] = "ws.kraken.com";
inline constexpr char kKrakenPath[] = "/v2";
inline constexpr std::uint16_t kKrakenPort = 443;
inline constexpr char kKrakenPortText[] = "443";
static_assert(kKrakenPort == 443 && kKrakenPortText[0] == '4' && kKrakenPortText[1] == '4' &&
                  kKrakenPortText[2] == '3' && kKrakenPortText[3] == '\0',
              "kKrakenPort and kKrakenPortText must state the same port");

// The subscribed pair. `kraken::symbol_config_for` is what turns this into a
// scale, and it REFUSES an undeclared pair rather than guessing one — a wrong
// scale does not fail, it draws a wrong ladder.
inline constexpr std::string_view kKrakenWireSymbol = kraken::kKrakenMinaGbp.wire_symbol;

// The two frames, exactly as `tools/capture_kraken.py` writes them: compact
// separators, this key order. The unsubscribe is the same shape minus
// `snapshot`, and it is sent BEFORE a re-subscribe rather than instead of one —
// Kraken's answer to a second subscribe on a channel it already holds is
// undocumented and was never measured, while the unsubscribe/subscribe pair is
// the sequence the committed resync slice was captured with.
inline constexpr char kKrakenSubscribeText[] =
    "{\"method\":\"subscribe\",\"params\":{\"channel\":\"book\",\"symbol\":[\"MINA/GBP\"],"
    "\"depth\":25,\"snapshot\":true}}";
inline constexpr char kKrakenUnsubscribeText[] =
    "{\"method\":\"unsubscribe\",\"params\":{\"channel\":\"book\",\"symbol\":[\"MINA/GBP\"],"
    "\"depth\":25}}";

// --- the compiler's half of "these two numbers are one fact" -----------------

// Read `"depth":<n>` back out of a subscribe frame. Returns -1 if the key is
// absent, which fails the assertions below rather than defaulting to something
// plausible.
constexpr std::int32_t depth_in(std::string_view json) noexcept {
    constexpr std::string_view key = "\"depth\":";
    const std::size_t at = json.find(key);
    if (at == std::string_view::npos) { return -1; }
    std::size_t i = at + key.size();
    std::int32_t n = 0;
    bool any = false;
    for (; i < json.size() && json[i] >= '0' && json[i] <= '9'; ++i) {
        n = n * 10 + (json[i] - '0');
        any = true;
    }
    return any ? n : -1;
}

constexpr bool mentions(std::string_view json, std::string_view symbol) noexcept {
    return json.find(symbol) != std::string_view::npos;
}

static_assert(depth_in(kKrakenSubscribeText) == kraken::kKrakenSubscribeDepth,
              "the subscribe frame and the adapter must ask for the same depth");
static_assert(depth_in(kKrakenUnsubscribeText) == kraken::kKrakenSubscribeDepth,
              "the unsubscribe must name the subscription it is cancelling");
static_assert(mentions(kKrakenSubscribeText, kKrakenWireSymbol),
              "the subscribe frame and the configured scale must name the same pair");
static_assert(mentions(kKrakenUnsubscribeText, kKrakenWireSymbol),
              "the unsubscribe frame and the configured scale must name the same pair");

// Both frames must fit the short-header send path. This is not a protocol
// requirement — a text frame may be any length — it is a bound on
// `build_masked_text`, which writes a two-byte header into a fixed stack buffer
// rather than allocating (invariant #7).
//
// ASSERTED AGAINST `kMaxShortPayload` DIRECTLY, and review is why. There used to
// be a local `kKrakenMaxSubscribeBytes = 125` here and a second
// `kWsMaxClientTextBytes = 125` on the send path, asserted against each other —
// two spellings of one fact, which is the shape this project keeps paying for.
// Worse, they conflated two DIFFERENT facts: the size of the stack buffer, and
// the largest length the header form can encode. Raise both to 160 to fit a
// second symbol and everything still compiles, while `0x80 | (138 & 0x7F)`
// announces a ten-byte payload and 144 bytes go out. One number, owned by the
// thing that depends on it.
static_assert(sizeof(kKrakenSubscribeText) - 1 <= kMaxShortPayload,
              "the subscribe frame no longer fits the short-header send path");
static_assert(sizeof(kKrakenUnsubscribeText) - 1 <= kMaxShortPayload,
              "the unsubscribe frame no longer fits the short-header send path");

}  // namespace depthcharge::fw
