// firmware/src/anvil_endpoint.hpp — where the board connects, in ONE place.
//
// WHY THIS IS ITS OWN HEADER, and it is a bug rather than tidiness that put it
// here. These constants lived in ws_transport.hpp and irmware/diag/
// link_autopsy.cpp kept its own private copy, because the diag TU is a
// deliberately minimal client that must not drag in Arduino.h, esp-tls, the
// frame pipe or the panel. On 2026-08-16 the shipping path gained &depth=27
// (backlog A7) and the copy did not. Nothing failed to build and nothing went
// red: the diag tool would simply have subscribed a ~3x heavier stream than the
// firmware it exists to be compared against, and any autopsy run from it would
// have silently measured the wrong thing — which is the worst failure mode an
// instrument has, and the one this project keeps paying for.
//
// So the endpoint is a header with no dependencies beyond <cstdint>, and both
// clients include it. Same argument, same session, as DC_WS_PING moving to
// ws_ping.hpp: two spellings of one fact is how the two ends drift.
#pragma once

#include <cstdint>

namespace depthcharge::fw {

// THE ENDPOINT, IN THE FEWEST SPELLINGS THAT STILL COMPILE. Ticker 101 on the
// deployed demo — the M0/M1/M3 subject, hardcoded for M3 (the brief's "one
// panel, one ticker, one venue"). Multi-ticker is M7.
//
// There used to be five constants here, because the library parsed a `wss://`
// URI while the DNS warm resolved a bare host and the two could drift. Nothing
// parses a URI any more, so there are three facts and one derived spelling:
//
//   kAnvilHost      the authority — resolved, sent as `Host:`, checked against
//                   the certificate's SAN by esp-tls
//   kAnvilPath      the request target of the upgrade
//   kAnvilPort      the port, as esp_tls_conn_new_sync wants it (an int)
//   kAnvilPortText  the same port, as getaddrinfo wants it (a service string),
//                   held to the line above by the static_assert
//
// The `wss://host/path` form survives only where a human reads it — one log
// line in start(), composed at the printf rather than stored, so there is no
// second copy of the authority to go stale.
inline constexpr char kAnvilHost[] = "anvil.garethcooke.com";
// A7, live on the deployed server 2026-08-16 and the whole of its integration on
// this side. `depth` caps this socket's `snapshot`/`book` frames; absent or 0 is
// every published level, which is what the web client still gets.
//
// 27 IS `kDisplayLevels` — the panel's rendered depth — and asking for exactly
// what is drawn is the point of the parameter. **Anvil serves depth in TIERS**
// (1,2,3,5,8,10,15,20,30,40,50,75,100,150,200,300,500,1000,unlimited), rounding
// UP and never down, so this socket is served **30 a side** and the adapter
// truncates the last three. That is deliberate on Anvil's side: free-form depth
// would have cost the broadcaster one serialisation per distinct depth in use,
// from an unauthenticated query string.
//
// MEASURED HERE, NOT TAKEN ON TRUST — `harness/replay/anvil_101_depth27_20260816
// .ndjson`, 90 s, 1,399 frames, priced by `tools/anvil_frame_economics.py`:
// the `book` frame falls **8,428 -> 2,471 bytes** and the whole stream to
// **27.3%** of the committed 2026-08-09 baseline — 112.6 KiB/s becomes
// **30.8 KiB/s**, against the 23.6 h soak's worst measured hour of 56 KiB/s.
// That is 1.8x headroom where there was 0.5x, and it is the fix for the
// staleness that three sessions attributed to the firmware. Anvil's own
// deployed-server figure (2,476 B mean) agrees to 0.2%.
//
// The tier is not free and the number is worth having: at 30 served against 27
// rendered, ~10% of the book bytes are levels the panel never draws. Anvil sized
// that as "a few percent"; measured, it is 9.7% of `book` and ~9% of the stream.
// Trivially worth paying — but if a later tier ladder ever offers 27 exactly,
// that is where the remaining tenth is.
inline constexpr char kAnvilPath[] = "/ws?ticker=101&depth=27";
inline constexpr std::uint16_t kAnvilPort = 443;
inline constexpr char kAnvilPortText[] = "443";
static_assert(kAnvilPort == 443 && kAnvilPortText[0] == '4' && kAnvilPortText[1] == '4' &&
                  kAnvilPortText[2] == '3' && kAnvilPortText[3] == '\0',
              "kAnvilPort and kAnvilPortText must state the same port");

}  // namespace depthcharge::fw
