# M3-transport — own the websocket client

**Track:** Agentic, with a bench acceptance · **Status:** Not started
**Read first:** `ARCHITECTURE.md` §6 + the 2026-08-13 §9 entry, `ROADMAP.md`,
`hardware/bench-2026-08-13-wifi-drop-diagnosis.md` (the evidence this brief stands on),
`firmware/diag/link_autopsy.cpp` (the working prototype), `firmware/src/ws_supervisor.hpp`
(the policy that stays), `firmware/src/frame_reassembler.hpp` (the seam that stays).

## Goal

The transport stops using `esp_websocket_client`. In its place: a minimal WebSocket
client this project owns — esp-tls for the socket and TLS, a hand-rolled upgrade and a
host-tested frame parser — feeding the same `FramePipe` through the same supervision.

Why this earns a milestone-sized change: the 2026-08-13 bench convicted the library layer
with labelled evidence. Seventeen of seventeen captured socket deaths carried a **stale
errno** (119/EINPROGRESS — the failing read set no socket error at all), half arrived via
the library's *clean-close* path, and 85 framing-corruption rejects — the `SPLIT@1`
one-stray-byte signature, an off-by-one in payload-offset accounting — co-timed with the
deaths. Meanwhile the prototype of exactly this brief's design (`link_autopsy.cpp`, board
B) held **one socket for 3.7 hours, 534 MB, zero deaths**, pinned to the *worst* node in
the house, surviving measured 0.5–3.9 s RF fades throughout. The deaths were never the
weather; they are the layer this brief removes. Every death costs ~4.7 s of grey plus —
because Anvil queues — a multi-minute backlog skip, so this is also the largest single
lever on the panel's honesty and freshness.

## Deliverables

0. **First and separable: the association fallback — small, and proven necessary.** The
   two-line scan/sort fix failed its five-boot acceptance on 2026-08-13 21:38–21:40:
   draws were `9A −59 / B3 −86 / 9A −64 / 9A −67 / F9 −73` — two of five on weak siblings
   with `ALL_CHANNEL_SCAN` + `BY_SIGNAL` demonstrably active (bench record, closing
   section). Replace the driver's join with an explicit one in `connect_wifi()`: scan,
   log every same-SSID BSSID with channel and dBm (the `wifi_diag` survey, productized),
   `begin(ssid, pass, channel, bssid)` on the strongest. Keep the scan/sort calls as
   belt-and-braces for the rejoin paths that go through plain `begin()`. Acceptance,
   restated relative because sibling levels move ~20 dB across a day: five consecutive
   boots each join the strongest sibling visible in their own boot scan.
1. **`firmware/src/ws_frame.hpp` — the frame layer, ESP-IDF-free and host-tested.**
   Server→client WS frame state machine (opcode, 7/16/64-bit lengths, control frames,
   close-code capture, ping payload capture), byte-in/callback-out, no allocation
   (invariant #7). Modelled on `frame_reassembler.hpp`'s pattern: logic in a header the
   host suite compiles, platform nowhere in it. Tests: `test_ws_frame.cpp` — split
   headers across reads, interleaved control frames, zero-length frames, the 8.7 KB book
   frame in 1-byte deliveries, close frames with and without reason text. **Include a
   regression for the library's own bug**: a stream constructed so a naive offset
   accounting would emit the `SPLIT@1` stray-byte signature must parse clean.
2. **`WsTransport` platform half rebuilt on esp-tls.** Connect = `esp_tls_conn_new_sync`
   with the pinned root CA (`anvil_root_ca.hpp`, unchanged) + hand-rolled 101 upgrade
   (prototype in `link_autopsy.cpp:open_flow`). An owned RX task — **pinned to Core 0,
   which the library's task never could be** — reads into the existing 4 KiB buffer,
   drives `ws_frame` → `FrameReassembler` → `FramePipe`. `WsSupervisor` and
   `WifiSupervisor` policies are reused as-is; the two-handle design collapses to two
   esp-tls contexts (no 5 s library sleeper to dodge any more — record what that
   simplifies in the session log). Pong replies to server pings (all-zero mask is legal;
   the prototype's is fine). No client pings by default: the RX watchdog already owns
   silence detection, and the 3.7 h prototype soak ran pingless — but see known unknowns.
3. **The death autopsy, native.** Every socket end logs: mbedtls rc (hex), errno,
   `SO_ERROR`, bytes and lifetime, close-frame code/reason if seen — the instrument the
   old client made impossible. One line, on the client task, socket already dead
   (the logging rule from `ws_transport.cpp:on_event` carries over).
4. **The old client stays buildable behind an env** (`depthcharge-espws`) until the bench
   acceptance passes, then dies. Same pattern as `depthcharge-ps`/`-nopp`: both arms, no
   line edited between them.
5. **Bench acceptance, owner-driven, bars from the 2026-08-13 data:**
   - Strong node (≥ −45 dBm): **3 h soak, zero errno-silent deaths, `reject` = 0** —
     against today's baseline of a death every 6–10 min and 85 rejects in 4 h.
   - Weak node (pin or re-roll to a −7x sibling): **1 h soak, socket survives every fade
     the watchdog reports** — greys allowed (weather is real), deaths are not, matching
     board B's record on `…:F9`.
   - Feed-side histograms and heap: no regression against the stage-D soak record;
     `a→e` ≤ 22 ms, heap flat, invariant #7's target reading intact.
6. **Docs writeback.** DESIGN.html: transport class diagram + §08 strains (the client
   layer's strain entry closes; the owned client's own risks open). ARCHITECTURE §9: the
   removal decision with the acceptance numbers. Session log per protocol.

## Constraints

- §6 invariants frozen, all of them. #1: nothing here touches `engine/`. #4: the RX task
  never blocks the feed task — same FramePipe seam, same single-writer discipline (#8).
  #7 on the target: buffers at boot, nothing per-frame; esp-tls allocates per handshake
  like TLS always did here, inside the same boundary the invariant already names.
- `kRxWatchdogMs` stays 1000 ms. The goldens stay. A moved golden means stop.
- The wire contract is pinned (`docs/vendor/anvil-protocol.md`); Anvil is not touched.
- Keep the 4 KiB read size: reassembly must keep running ~12×/s (the "code that must be
  right when a message is split is code that runs constantly" rule from `ws_transport.hpp`).
- The TCP-window throughput ceiling (§9 2026-08-11, 65.5 KiB/s) is **not** this brief's
  problem and this change will not move it — say so in the acceptance readout so nobody
  reads the soak as a throughput claim. If the framework rebuild (pioarduino/IDF 5)
  happens first, this design carries over — esp-tls's API survives the bump; that is part
  of why esp-tls and not another third-party WS library.

## Known unknowns

- Half-open TCP with a held association: the old pingpong watchdog nominally covered it;
  the prototype ran 3.7 h without and the RX watchdog + supervisor covered every real
  case today. Decide with evidence: either a long-interval client ping (60 s+) or a
  documented reliance on the RX watchdog — measure, choose, record.
- RX task stack size and priority against the feed task (the library used 6144 at prio 5;
  the owned task does less — measure high-water and set it honestly).
- Whether `esp_tls_conn_new_sync`'s blocking connect needs the DNS warm kept (it blocks
  loopTask today for the same duration; probably keep, it also feeds the `dns=` split).
- The upgrade's `Sec-WebSocket-Accept` check: the prototype skips verification; the
  production client should verify (one SHA1+base64 against a fixed key — decide whether
  the ROM SHA1 is worth the dependency or the check is computed host-side as a constant).

## Definition of done

- ☐ Association joins the strongest visible sibling on every boot (five-boot check,
  relative bar — deliverable 0).
- ☐ `ws_frame.hpp` host-tested; `SPLIT@1` regression case in the suite; ctest green from
  clean clone.
- ☐ Owned transport streams the live feed on the bench through `WsSupervisor` unchanged.
- ☐ Death-autopsy line on every socket end.
- ☐ Strong-node 3 h soak: zero errno-silent deaths, zero rejects.
- ☐ Weak-node 1 h soak: fades grey, nothing dies.
- ☐ Old client env deleted after acceptance; DESIGN.html + ARCHITECTURE §9 + session log
  updated; ROADMAP updated.

## Out of scope

The TCP window / throughput ceiling (framework-rebuild milestone). Kraken/Binance
adapters. Any `engine/` change. The staleness-`Gap` proposal (stage E owns it). Deco-side
investigation. Multi-ticker.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->
