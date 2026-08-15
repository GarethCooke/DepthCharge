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

### 2026-08-14 · Opus 5 · deliverables 0–4 written and green on the desk; 5 is the bench's

**Done.** The client is ours. `firmware/src/ws_frame.hpp` (frame layer, ESP-IDF-free),
`harness/tests/test_ws_frame.cpp` (18 cases, 8 subcases), `WsTransport` rebuilt on esp-tls with an owned
RX task pinned to Core 0, the death autopsy, the explicit scan-then-join association, and
four build arms that all compile. Host: `cmake --workflow --preset host-mingw` green, 11/11.
Target: `depthcharge`, `depthcharge-espws`, `depthcharge-ping`, `depthcharge-nopp` all build.
**Nothing has been flashed.** Every claim below is a desk claim.

**The SPLIT@1 regression bites, and that was verified rather than asserted.** The suite was
run against a deliberately re-introduced copy of the library's bug — the first chunk of each
frame emitted with its data pointer one byte early — and it reported exactly the bench
signature: `m.find(R"({"type":)", 1)` returning **1** instead of `npos`, on both messages of
the case. Mutation reverted, suite green. A regression test for a bug nobody can reproduce
on demand is worth what it catches, so it was made to catch it once.

**Decisions, with why.**

1. **`WsChunk` gains `fin`, and `FrameReassembler::complete()` requires it.** The reassembler
   published each server fragment separately and let the parser reject the partial JSON — a
   documented limitation whose stated cause was "this IDF vintage does not surface the FIN
   bit". We read FIN off the wire now, so the cause is gone. The field defaults to `true`, so
   every existing construction (the espws path, all 23 existing tests) means exactly what it
   did and **no existing test moved**; two new cases pin the fixed behaviour and the old case
   is kept, relabelled as the espws arm's world. This is the only change to a seam the brief
   said stays, and it is additive.
2. **One esp-tls connection, not two.** Deliverable 2 says the two-handle design "collapses
   to two esp-tls contexts"; it reads either way and one is right. The second handle existed
   for exactly one reason (ARCHITECTURE §9, 2026-08-10): the library's task slept 5 s inside
   every abort and `esp_websocket_client_stop()` blocked for whatever was left of it, so the
   only route to a live socket inside 5 s was to already own one. `esp_tls_conn_destroy()` is
   ours and returns at once. **Flag for the owner: if two contexts were meant literally, say
   so — it is a small change.** What the collapse deletes: ~10 KiB of heap for the spare's
   buffers, its 6 KiB task stack, `disable_auto_reconnect`, the `live_` atomic, the
   retired-handle DATA guard, and the whole of DESIGN strain 14.
3. **`WsSupervisor` and `WifiSupervisor` are untouched**, per the brief. `kClientSelfExitUs`
   and the `kReconnectBackoffUs + kRetryCycleUs > kClientSelfExitUs` assert now describe a
   library the default build does not contain — deliberately left, because the espws control
   still needs them and editing the supervisor would edit the control. They go in the same
   commit the arm does.
4. **The RX task owns the whole socket lifecycle; `supervise()` only decides.**
   `StartAttempt` sets a flag. loopTask no longer blocks on the network at all — the DNS warm
   and the ~4 s `esp_tls_conn_new_sync` are on the RX task. A request that lands *during* a
   slow connect is cleared on success, so it cannot survive to reconnect the next death with
   no backoff and no association gate.
5. **`Sec-WebSocket-Accept` is verified against a compile-time constant.** The key is fixed
   (`ZGVwdGhjaGFyZ2Utd3MwMQ==`), so the accept value is too, so the check costs one string
   compare and brings no SHA1 dependency onto the board. §4.1 wants a random key; what that
   defends against is a canned response from a non-WebSocket intermediary, and this is TLS to
   one pinned host with a pinned root. The prototype skipped the check entirely; this is
   strictly more, at zero runtime cost.
6. **No client ping by default**, with `depthcharge-ping` (60 s) as the other arm — the
   brief's known unknown, converted from an argument into a flashable measurement.
7. **A close frame is deliberately NOT counted in `control`.** The reassembler has always
   treated close as a no-op, and the only reason the espws arm exists is to have its counters
   compared against these. Counter parity between the arms is load-bearing.
8. **The arrival stamp moved one hop upstream for free** — it is now the instruction after
   the read that produced the bytes, on a task we created and pinned, so the `esp_event`
   dispatch hop and the unpinnable library task are out of the measurement. DESIGN §05 says
   so; the "the arrival stamp is not the wire" caveat still stands, it is just closer to it.

**Measured, statically:** owned arm 141,728 B RAM / 872,721 B flash against the espws arm's
137,348 / 871,045 — **+4,380 B RAM, +1,676 B flash**. The RAM is the 4 KiB read buffer moving
out of the library's heap and into our BSS, which is the direction invariant #7 prefers. At
runtime the owned arm should show *more* free heap (no spare handle's buffers, one task stack
instead of two); `heap_probe` on the bench is the check, and the prediction is written down
before the run rather than after.

**Not done, and why.** Deliverable 5 is the bench and owner-driven. ARCHITECTURE §9 is
deliberately unwritten: the brief asks for "the removal decision **with the acceptance
numbers**", and the numbers do not exist yet. DESIGN.html was updated only for what is true
today — §01's diagram box, §05's arrival-stamp paragraph, strain 14 **closed** (the binary
its three `objdump` facts describe is gone from the default build) and strain 18 **opened**:
the framing is ours now and so is every bug in it, with the four things that changed hands
(a parser on the hot path, a fixed upgrade key, no client ping, two task constants that are
still estimates) and the mitigation each one has.

**Exact next step.** Flash `depthcharge` and run the deliverable-0 check first — it is five
minutes and gates nothing else: five consecutive boots, each joining the strongest sibling in
its own boot survey (`wifi: sibling …` lines, then `wifi: joining …`). Then the deliverable-5
soaks. Read the first `socket up: dns N ms, connect+upgrade N ms` line; a failed upgrade now
says `upgrade accept mismatch` or `upgrade refused` with the server's own words, and a
`SPLIT@` in the reject log would now be **ours**. On any death the `socket end #N [label]`
block is the instrument the old client made impossible, and `stack_free` on its third line is
what lowers `kRxTaskStack` on evidence. If the acceptance passes: delete `depthcharge-espws`
and `depthcharge-nopp`, the `DC_OWNED_WS` switch and every `#if` arm behind it,
`kClientWaitTimeoutMs`/`kClientSelfExitUs` and their assert; then write ARCHITECTURE §9 with
the numbers and tick the boxes here and in `ROADMAP.md`.

---

### 2026-08-14 · Claude Fable 5 · bench: clkphase A/B/A on the owned client; lag reasserted

**Done.**

- **The 2026-08-11 clkphase verdict is superseded on this client.** A/B/A at ~10 min per arm
  (same divider 24, RSSI −38…−47, same desk): `true` 6.58 msg/s median (25 min baseline,
  `device-monitor-260814-151322.log`) → `false` 6.78 (`…-154752.log`) → `true` 6.48
  (`…-155929.log`). No collapse in any arm; byte rate 46–48 KB/s at the TCP-window ceiling
  throughout; `connects=1 sock_gaps=0` in all three runs. **`clkphase = false` now ships** —
  the header-ghosting fix is free. `panel.cpp`, `BRINGUP.md` and ARCHITECTURE §9 all updated;
  the old entries left standing per §9's rule.
- **The growing lag is reasserted, from the serial log alone** (`tools/board_log_lag.py` —
  no Anvil pause/resume, no stopwatch): slope **+0.572…+0.589 s/s** (drain f = 0.41–0.43)
  across all three runs, slope-vs-summary-counter cross-check agreeing to three decimals,
  no plateau. Peak age **873 s after 25 min** on a single clean connection — with the
  socket deaths gone, nothing "un-lags" the book by accident (2026-08-13 §9 entry), so
  staleness is now the dominant defect, on schedule.
- Incidental soak evidence for this brief's acceptance: three boots, ~45 min total, zero
  errno-silent deaths, zero `SPLIT@` rejects, `connects=1` per run. Not the 3 h bar; noted.

**Decisions, with why.**

- Ship `clkphase = false`: the only cost it ever had (the 2 msg/s collapse) does not
  reproduce on the owned client, and the benefit (clean header glyphs) is visible at the
  desk. Which 2026-08-11 variable was real is left open deliberately — the separating
  experiment needs the old client this brief is deleting.
- Lag fix direction (owner to choose, recorded here so the options don't get re-derived):
  **(a) the real fix** — raise the TCP receive window past the 9.7 KiB bandwidth-delay
  product (2× = 11,488 B → 131 KiB/s ceiling clears the 110.4 KiB/s wire; §9 2026-08-11
  pre-authorises the framework rebuild as milestone-weight — pioarduino/IDF-from-source or
  lib-builder; budget the extra window RAM against `kReserveInternalBytes`, which may cost
  the panel a colour-depth rung, the designed direction of that trade);
  **(b) the firmware stopgap** — an age-bounded resync: when integrated age exceeds a
  threshold, deliberately drop the socket for a fresh snapshot, turning the 2026-08-13
  "accidental un-lag" into policy. Host-testable in `ws_supervisor.hpp`; costs one honest
  ~4 s grey per cycle; bounds age at threshold + reconnect;
  **(c) the venue wish** (Anvil backlog, not modified from here): the sequenced incremental
  L2 feed §9 already promoted — a delta stream at a tenth the bytes fits inside today's
  window and makes (a) unnecessary.

**Exact next step.** Unchanged from the entry above (deliverable-0 five-boot check, then the
soaks) — plus: the owner picks a lag-fix lane from the three above; (b) is a one-session
brief if chosen as the interim.

---

### 2026-08-14 (late) · Claude Fable 5 · the TCP-window rebuild — built, verified, and the ceiling did not move

The owner chose lane (a) and it was executed the same evening. The result is a
supersession of this repo's own 2026-08-11 root-cause verdict, and it was found only
because the rebuild was measured rather than declared done.

**Done.**

- **`liblwip.a` rebuilt from the exact shipped vintage** (esp-idf tag v4.4.6 =
  `3572900934`, the commit the package's `versions.txt` names; lib-builder's single
  release/v4.4 patch touches only i2s HAL — skipped as irrelevant; same GCC 8.4
  esp-2021r2-patch5 generation; the package's own `tools/sdk/esp32s3/sdkconfig` as the
  base, which IS the config all 98 archives were built from). Built in WSL Ubuntu after
  Docker Desktop proved dead on this machine. Verified before flashing: 564 global
  symbols identical, 87 members, the only member size delta `tcp.c.obj` +4 B, and
  `tcp_alloc`'s packed `rcv_wnd/rcv_ann_wnd` literal pair `0x1670,0x1670` → `0x4350,0x4350`
  (the adjacent `TCP_SND_BUF` word untouched — note: the 2026-08-11 §9 row's
  "`.literal.tcp_alloc+4`" was actually pointing at SND_BUF; the window pair is at +8).
- **A hazard found and mapped: `CONFIG_LWIP_TCP_RECVMBOX_SIZE` must stay 6 on this
  vintage.** Kconfig's own guidance (WND/MSS + 2 = 14) was applied in the first cut and
  the board could not complete one WebSocket upgrade — TCP connect fine, TLS handshake
  fine, HTTP 101 never arrives, while the desk and stock firmware connected in the same
  minute. Four archives from one pipeline flashed A/B: 5744/6 healthy, 17232/6 healthy,
  5744/14 connects then watchdog-greys within 25 s, 17232/14 never completes an upgrade.
  Mechanism formally open (the constant reaches one `xQueueCreate` in netconn alloc, read
  from source); recorded as measured-bad in the package README. **Do not "fix" the mbox
  to match the window without a soak.**
- **The window arm shipped as `[env:depthcharge-wnd]`** — a local package copy at
  `C:\local\framework-arduinoespressif32-wnd17232` (symlink platform_packages override,
  README with full provenance) with ONE change: `CONFIG_LWIP_TCP_WND_DEFAULT` 5744 → 17232.
  Package sdkconfig records edited to match the binary, both directions, both times.
- **The acceptance run refutes the window as the binding constraint: nothing moved.**
  9.5 min on the strong node: 6.78 msg/s median, 44.0 KB/s median (max 49.6), lag slope
  +0.571 s/s, drain 43% — inside the noise of every 5744 baseline, with 17232 verified in
  the flashed binary. The 2026-08-11 arithmetic (5744/87 ms = 65.5 KiB/s) named A ceiling,
  not THE ceiling: the board was running at 44 — *below* the old window cap — both before
  and after.
- **Where the ceiling actually lives, measured with the repo's own diag**
  (`link-autopsy-wnd`, same rebuilt lwIP, driven over serial): raw **tcp-bulk 213 KB/s**
  sustained for 95 s — on the *weak* node at −78 dBm, through mbox=6; diag **anvil-ws
  (TLS+WS, no parser, no book) ~79 KB/s** in catch-up on that same weak node; production
  firmware **44 KB/s**. Stack, radio and window all exonerated; the ~44 KiB/s pin is in
  the production RX path (read → reassemble → parse → apply → publish, serialized).
  Caveat recorded: tcp-bulk's server is UK (~20 ms RTT) and cannot exercise the
  transatlantic BDP, so the 17232 window remains **necessary** for anything past
  65.5 KiB/s from Anvil — it is just not sufficient. (Side observation, parked: the
  diag's tls-bulk to speed.cloudflare.com now dies in <100 ms per attempt on the weak
  node, `connect-fail`; it worked on 2026-08-13. DNS/anycast on the sick node suspected.)

**Decisions, with why.**

- The board is left running `depthcharge-wnd` (17232/6): indistinguishable from stock
  today, required tomorrow, and every additional hour is soak evidence for the swap.
- The lag-fix lane list from the earlier entry is revised: **(a) alone cannot deliver.**
  The path to a live book is now (1) find and break the ~44 KiB/s serialization in the
  RX path — instrument the loop's per-stage budget first, then likely decouple socket
  reads from parse/apply across tasks/cores; (2) keep the 17232 window so the transport
  can use the headroom; (3) the Anvil delta-feed wish stands and would still moot both.
  A DESIGN §08 strain entry for the RX-path ceiling is owed next session, with the
  instrumentation numbers to draw it from — and the same DESIGN pass owes two smaller
  corrections the 2026-08-15 review caught: strain 11 still describes the esp_event
  dispatch path in the present tense (it left with the old client), and the statusbar
  still stamps the 2026-08-11 source hash.

**Exact next step.** Instrument the owned client's RX loop: time each stage (tls read,
reassembler feed, parse, book apply, publish) per message over a 10-minute run, printed
in the stats block. The stage that eats the frame period is the next brief's target. Then
the deliverable-0/soak sequence from the entries above still applies to whichever build
ships.

---

### 2026-08-15 (00:24) · Claude Fable 5 · the instrument's first reading revises the verdict again

`rx_budget.hpp` (host-tested, `-- rx` line beside `arrive`/`event`) went on the board at
00:15 and its first 8.5 minutes rewrote the suspect list:

- **13.89 msg/s median (peak 17.6), 100–116 KiB/s, lag slope +0.117 s/s, drain 88%**,
  age flat at ~60 s with windows at 100–105% drain, `wd_gaps=0 sock_gaps=0 connects=1`,
  same −43 dBm node as every 6.78 msg/s run. The first near-wire-rate run this project
  has ever logged (`device-monitor-260815-001531.log`).
- The rate is **above the stock window's hard 65.5 KiB/s ceiling**, so the wnd17232
  rebuild is load-bearing, not decorative — the evening's "moved nothing" verdict was
  true only of the congested path it was measured on.
- The budget reads **wait 0% / read 99% / feed ~0%**: the loop is io-bound and the
  parse/apply pipeline is effectively free at full rate. The previous entry's
  "serialized RX path" hypothesis is refuted by its own instrument; with identical
  firmware at 44 KiB/s (23:36) and ~100 KiB/s (00:20), the ~44 pin was upstream —
  **UK-evening transatlantic congestion is the lead suspect**, and §9's 2026-08-14 row's
  "the binding limit is in the firmware's own RX path" sentence is now under test.
- Instrument caveat, stated before anyone reads too much into `read 99%`: the `read`
  bucket is time inside `esp_tls_conn_read` that eventually returned bytes — kernel
  wait and decrypt together. Splitting those needs a different probe; at 0% `feed`
  it does not matter yet.

**Next.** A soak through a full day decides it: if ~44 KiB/s returns at UK peak hours
with `wait/read` still dominating and `feed` still ~0%, the ceiling is the path and the
fix conversation moves to (c) — the Anvil delta feed — plus acceptance that the object
runs behind at peak; if it does not return, tonight was the fix landing late. Then write
the §9 correction from the soak's numbers, not from one midnight run.


---

### 2026-08-16 (00:30) · Claude Fable 5 · the day soak: the ceiling was the path, the window is load-bearing, and the age clock had a ceiling of its own

The 23.6-hour soak (`device-monitor-260815-002728.log`, 26 MB, one continuous monitor;
four brownouts at 06:41 / 11:13 / 13:15 ×2 are the owner switching the panel's independent
5 V supply, 1:1 correlated, and are excluded from every figure) — hourly medians:

| hour | msg/s | KiB/s | rx wait/read/feed/other | drain |
| --- | --- | --- | --- | --- |
| 00 | 12.7 | **87** | 0 / 99 / 0 / 0 | 80% |
| 01–02 | 9.3–10.8 | 66–74 | 0 / 99 / 0 / 0 | 60–70% |
| 07–10 | 8.3–8.8 | **56–60** | 0 / 98–99 / 0 / 0 | 50–55% |
| 11–15 | 10.0–11.4 | 70–78 | 0 / 99 / 0 / 0–1 | 65–70% |
| 16–20 | 9.4–10.1 | 65–71 | 0 / 99 / 0 / 0 | 60–65% |
| 21–23 | 10.5–11.0 | 73–76 | 0 / 99 / 0 / 0–1 | 65–70% |

**Findings.**

- **The 44 KiB/s did not come back at any hour, peak included.** The day's floor is 56
  (mid-morning), the ceiling 87 (midnight); the whole day sits above the stock window's
  65.5 KiB/s hard cap for most hours. So: the wnd17232 window is **load-bearing** and stays;
  the previous entry's "serialized RX path" was wrong; the 2026-08-14 evening's 44 was the
  transatlantic path at UK peak on *that* evening. §9 has a 2026-08-16 row correcting the
  2026-08-14 row, with the rule this cost: **a WAN throughput comparison across time is not
  an A/B unless the hour is controlled.**
- **`-- rx` read wait 0 / read 98–99 / feed 0 in every hour.** The socket always had bytes
  when asked; deframe+reassemble+pipe cost nothing; the loop is bound by how fast bytes
  come off the wire into the board. Nothing left to optimise in the RX path.
- **Lag still grows, slower.** Per-connection slopes +0.243, +0.294, +0.323, and **+0.083
  over the final 10.9 h segment** (13:15–00:06) — against +0.57–0.59 on every 5744 run. The
  residual is path bandwidth (60–80% of a 110 KiB/s wire); the sized fix is still Anvil's
  delta feed, and until then the object runs behind at peak and says so.
- **The staleness clock saturated at 4294.9 s** — `SecondsText` and the whole
  `lag_us/window_us/worst_*` chain were uint32 µs (71.6 min), so `age`, `worst`, `run` and
  `over` all printed the ceiling for eleven straight hours; three of four segments hit it,
  and the true peak age is unrecoverable from this log. **Fixed this session:** the chain is
  64-bit end to end, `test_staleness.cpp` pins a 72-minute age printing as itself; host 11/11.
  `board_log_lag.py`'s slopes above were fitted before saturation and stand.

**Exact next step.** Flash the widened clock (it is built), and let the object run. The lag
work now belongs to the Anvil backlog (delta feed) — DepthCharge's side is instrumented,
windowed and honest. Remaining owed items from the 2026-08-15 review are unchanged (DESIGN
strain 11 text, statusbar hash, the fake-slot-pool extraction).


---

### 2026-08-16 (00:15) · Claude Fable 5 · the half-open unknown answered itself, live, on the acceptance flash

Flashing the widened age clock at 00:11: the board came up, pulled **103 KiB/s for twenty
seconds** (`-- rx` 276 reads / 0 waits, `feed 0%`), then at 00:12:24 the server went
silent mid-stream. From that instant `-- rx` reads **wait 99% / 0 reads / 10 waits** every
window — a live TCP socket, every 1 s read timing out clean, no death, no autopsy — and the
RX watchdog greyed the panel within a second (`STALE (disconnect) at v238`). The desk
confirms it is Anvil, not us: `capture_anvil.py` times out on the WebSocket handshake while
plain HTTPS to the same host answers 200 in 0.5 s. Anvil's WS server is wedged or
restarting; the widened clock touched only a printer and is not a suspect.

**This is the "half-open TCP with a held association" known-unknown, and it now has its
evidence.** `WsSupervisor` gates on `socket_connected`, and a silent-but-open socket reads
as connected, so the transport holds this socket **indefinitely** — the panel is honestly
grey, but nothing will ever try a fresh connect while the server keeps the TCP session
alive without speaking. Tonight the socket may eventually get a FIN when Anvil restarts;
on a true half-open (peer host gone, no FIN, no RST) it never would, and the object would
sit grey with a "healthy" transport for the rest of the run. The RX watchdog covers
*honesty*; it does not cover *recovery*.

**Decision, from evidence rather than assumption:** the transport needs a data-silence
recovery — the supervisor should treat "socket up, no bytes for N seconds" as a death and
recycle the socket, with N well above Anvil's measured healthy silence (~600 ms worst;
2026-08-13 fades to 3.9 s) and above the 1 s RX watchdog: **~15 s** is the first number,
long enough that no weather ever trips it, short enough that a wedged server costs one
grey quarter-minute rather than an evening. That subsumes the client-ping question — a
ping's only job was to *provoke* the silence into an error, and the RX loop can simply act
on the silence directly, which is cheaper and needs no server cooperation. Host-testable in
`ws_supervisor.hpp` (add a `last_rx_us` to `SupervisorInput`), one constant, one test.
It is not done tonight: it is a policy change to a supervisor that has hours of clean
soak behind it, and it deserves its own edit-and-review with the transport at rest.

**Exact next step.** Add the data-silence recovery to `WsSupervisor` (input `last_rx_us`,
constant `kSilenceRecycleUs = 15 s`, test: silent-socket → StartAttempt after the
threshold, never before it, never on a socket that is receiving). Then re-run a soak.
