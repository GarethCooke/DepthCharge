// firmware/src/binance_endpoint.hpp — where the board connects at Binance.
//
// The third of these, and the shortest, because this venue asks the client for
// nothing after the upgrade. Anvil puts its depth in the query string; Kraken
// sends a subscribe frame; **Binance names the stream in the URL path**. So
// there is no `kSubscribeText` here and `venue::kHasSubscription` is false —
// this venue sits exactly where Anvil does, and the existing
// `DC_VENUE_HAS_SUBSCRIPTION` branches already cover it with no new arm.
//
// TWO HOSTS, AND THAT IS THE ONE THING THIS FILE HAS THAT THE OTHER TWO DO NOT.
// The diff stream is on `data-stream.binance.vision` and the REST seed D-A2
// will fetch is on `data-api.binance.vision`. Both are the market-data-only
// hosts — `tools/capture_binance.py` records that no key, no signature and no
// account ever touches them — and the whole corpus in `harness/replay/` was
// captured through this pair.
//
// THE PATH IS PINNED AGAINST THE COMMITTED CORPUS, exactly as Kraken's frames
// are, and at this venue that discipline has already caught something. The
// capture `binance_btcusdt_DEFECT_silent_stream_20260826.ndjson` was taken from
//
//     wss://data-stream.binance.vision/ws/btcusdt@depth@100mss
//                                                            ^ misspelt
//
// which returns **HTTP 101, answers pings, and delivers nothing for ever**.
// That is B1's silent-stream defect and it is a committed fixture, so a
// hand-written path that drifts by one character does not fail loudly — it
// reproduces a known defect. The `static_assert`s below read the symbol and the
// stream suffix back out of `kBinancePath` and check them against the adapter's
// configured symbol, so the compiler owns the agreement rather than whoever
// last edited one of the two.
//
// The healthy single-stream spelling is the one two committed captures use:
// `binance_btcusdt_reconnect_20260824` and
// `binance_atomeur_d100ms_liveness_20260826`.
//
// ONE STREAM, NOT TWO. The corpus mostly uses the `/stream?streams=a/b`
// combined form because the harness grades against `@depth20`, the venue's own
// partial-depth stream, which M5 stage 0 established as the ORACLE. The oracle
// is a grading instrument and has no business on the board: it would double the
// inbound byte rate to check something ctest already checks on the desk. The
// board subscribes to the diff stream and nothing else.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <depthcharge/binance/binance_adapter.hpp>
#include <depthcharge/binance/binance_frame.hpp>

namespace depthcharge::fw {

// The stream host, the request target, and the port in both the forms the two
// APIs want it in — the same three facts and one derived spelling as
// anvil_endpoint.hpp and kraken_endpoint.hpp, held together by a static_assert.
inline constexpr char kBinanceHost[] = "data-stream.binance.vision";
inline constexpr char kBinancePath[] = "/ws/btcusdt@depth@100ms";
inline constexpr std::uint16_t kBinancePort = 443;
inline constexpr char kBinancePortText[] = "443";
static_assert(kBinancePort == 443 && kBinancePortText[0] == '4' && kBinancePortText[1] == '4' &&
                  kBinancePortText[2] == '3' && kBinancePortText[3] == '\0',
              "kBinancePort and kBinancePortText must state the same port");

// THE SEED'S HOST, CARRIED HERE AND NOT USED YET. D-A1 builds no REST client —
// that is D-A2 — but the host belongs beside the stream it seeds, and stating
// it here is what let the TLS anchor be measured against BOTH names in one
// sitting rather than discovered to be a second anchor half way through the
// next one. See binance_root_ca.hpp: it is one anchor, and this is why we know.
inline constexpr char kBinanceRestHost[] = "data-api.binance.vision";
inline constexpr char kBinanceRestPathPrefix[] = "/api/v3/depth?symbol=";

// The subscribed pair. `binance::symbol_config_for` turns this into a scale and
// REFUSES an undeclared pair rather than guessing one — a wrong scale does not
// fail, it draws a wrong ladder.
inline constexpr std::string_view kBinanceWireSymbol = binance::kBinanceBtcUsdt.wire_symbol;

// --- the compiler's half of "the path and the symbol are one fact" -----------

// Binance spells stream names in lower case and symbols in upper case, so the
// two cannot be compared directly and a `find` would not be a check. Fold here
// rather than anywhere else: this is the only place in the firmware where the
// same symbol appears in both cases.
constexpr char lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// The stream name is everything after the last '/' — `btcusdt@depth@100ms`.
constexpr std::string_view stream_in(std::string_view path) noexcept {
    const std::size_t at = path.rfind('/');
    return at == std::string_view::npos ? std::string_view{} : path.substr(at + 1);
}

// Does `stream` begin with `symbol` case-insensitively, and is the next
// character an '@'? The '@' matters: without it `btcusdt2@depth` would pass.
constexpr bool names_symbol(std::string_view stream, std::string_view symbol) noexcept {
    if (stream.size() <= symbol.size()) { return false; }
    for (std::size_t i = 0; i < symbol.size(); ++i) {
        if (lower(stream[i]) != lower(symbol[i])) { return false; }
    }
    return stream[symbol.size()] == '@';
}

constexpr bool ends_with(std::string_view s, std::string_view suffix) noexcept {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

static_assert(names_symbol(stream_in(kBinancePath), kBinanceWireSymbol),
              "the stream in kBinancePath and the adapter's configured symbol must be the "
              "same pair — a mismatch subscribes to a book the scale does not describe");

// THE SUFFIX IS ASSERTED SEPARATELY FROM THE SYMBOL, and the defect fixture is
// why: `btcusdt@depth@100mss` names the right symbol and the wrong stream, so a
// symbol check alone passes it. This is the assertion that would have caught
// the capture that returns 101 and never speaks.
static_assert(ends_with(stream_in(kBinancePath), "@depth@100ms"),
              "the board subscribes to the 100 ms DIFF stream; @depth20 is the harness's "
              "grading oracle and @depth@100mss is a committed silent-stream defect");

// The REST path D-A2 will build, checked now for the same reason. `limit` is
// `kBinanceRestLimit`, whose floor is already enforced in the engine:
// `kBinanceReseedCoverLevels < kBinanceRestLimit`, so a seed shallower than the
// re-seed trigger cannot compile.
static_assert(binance::kBinanceRestLimit > 0,
              "the seed depth must be positive; the engine asserts its lower bound");

}  // namespace depthcharge::fw
