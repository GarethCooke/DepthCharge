# M3 — Live Anvil on the panel

**Track:** Mixed [A+B] · **Status:** Not started
**Executor:** Mixed. Stages A–B are agentic, host-only, and provable under `ctest` (Claude
Code, no hardware). Stages C–D are firmware on the bench — the owner flashes, pulls the
Wi-Fi, and reads the panel; Claude Code assists (writes the firmware, reviews) but does not
drive the board.
**Read first:** `/ARCHITECTURE.md` (constitution — §2 the two-core split, §4 the FeedEvent
contract, §5 the display output, §6 invariants **binding**), `ROADMAP.md`,
`docs/briefs/M1-console-ladder-off-replay.md` **session log** (the hand-off — it names this
milestone's first three moves), `hardware/BRINGUP.md` (M2 output — pin map, FM6124 init,
power), and `docs/vendor/anvil-protocol.md` §4 *Reconnect & idempotency* (the transport
contract the net task must obey).

**Depends on:** M1 (done — engine, phase-1 book, `DisplaySnapshot`, the `SnapshotChannel`
seam, and the pinned goldens) **and** M2 (done — one panel lit off the DevKit, verified pin
map, FM6124 init, measured draw, signal integrity). Both gates are green: **M3 is
unblocked**, and it is the first session where firmware may exist.

**Hardware pinned (from `hardware/BRINGUP.md` — wire to this, do not re-derive):**
Muen P3 64×64, 1/32 scan, HUB75E. DevKit **ESP32-S3-WROOM-1 N16R8**. Driver **FM6124**
(`mxconfig.driver = HUB75_I2S_CFG::FM6124;`, fallback `FM6126A`). Pin map
`R1=4 G1=5 B1=6 R2=7 G2=15 B2=16 A=17 B=18 C=8 D=9 E=10 CLK=11 LAT=12 OE=13`, panel GND →
DevKit GND, panel 5 V from the **PSU, not the DevKit**. PSU **5 V/5 A** (Mean Well LRS-50-5
class; full-white draw measured 2.6 A, ~2× headroom). Bare jumpers are bench-proven adequate
for M3; if pixel-shift artifacts appear, re-scope CLK on a 10× probe *before* suspecting
firmware (BRINGUP forward note).

## Goal

The object comes alive. The identical `engine/` from M1 — not a port, the same code — runs
on the ESP32-S3, fed by a real TLS WebSocket to Anvil instead of a replay file, and draws
the live ladder on the panel M2 lit. At the end: bids stack green, asks red, prints flash
white, the last price tracks along the bottom, and when the Wi-Fi is pulled the panel greys
to an honest stale state within a second and snaps cleanly back to live on the next
snapshot. This is invariant 5 proven in copper and light, and the last structural risk in
the project (does the host-first engine actually run unchanged on the target?) retired.

Two host-provable stages (A, B) pay down the two debts M1 deliberately left for M3 — the
single-slot channel and the allocating parser — and can land on any evening with no board
in reach. Two bench stages (C, D) stand the firmware up. Keep each stage a landable evening;
`Anvil` and `FrontierView` prep still outrank this.

### A note on "engine unchanged from M1"

The ROADMAP one-liner says *engine unchanged from M1*. Read it as **behaviourally**
unchanged, not zero-diff. Exactly two engine-touching changes are sanctioned here, both
pre-authorised by M1 and §5, and both are structural swaps behind stable seams — not new
book behaviour:

1. `SnapshotChannel`'s **internals** become a wait-free double buffer (the header already
   says "M3 replaces the internals with a seqlock/double buffer and no caller changes").
2. A second `parse_anvil_frame` implementation is linked (the M1 log's first M3 move).

The proof that neither changed behaviour is the acceptance test for both: **`test_replay_
goldens.cpp` passes unchanged.** If a diff makes a golden move, it is out of scope — stop
and raise it. Nothing in `book.hpp`, `feed_event.hpp`, `display_snapshot.hpp`, the adapter
logic, or the scaling/seq-synthesis path changes.

## Deliverables

Staged; each stage is an evening and lands green on its own. A and B are host-only (no
hardware, pure `ctest`); C and D are firmware on the bench. Dependency order: **A → D**,
**B → C → D** (A and B are independent of each other and can be done in either order).

### Stage A — `SnapshotChannel` becomes the real double buffer *(host, agentic)*

The M1 `SnapshotChannel` is a single slot with a plain `uint32_t` version, "single-threaded
use only, not a synchronisation primitive yet." M3 makes it the real cross-core hand-off.

1. **Wait-free SPSC double buffer / seqlock** in
   `engine/include/depthcharge/snapshot_channel.hpp`. The public API (`publish` / `consume`
   / `published_version` / `consumed_version`) **does not change** — only the internals.
   `publish` stays wait-free on the writer (invariant #4) and never blocks on the reader;
   `consume` copies a complete `DisplaySnapshot` or reports nothing-new, and can **never**
   observe a half-written frame (torn read). Two slots + an atomic version, or a classic
   seqlock over the trivially-copyable `DisplaySnapshot` — session's call; record it.
2. **Portable, not FreeRTOS.** It stays in `engine/`, so it stays host-buildable with **zero
   ESP-IDF/FreeRTOS includes** (invariant #1) — `std::atomic` and standard fences only. The
   same header compiles into the harness and the firmware.
3. **Host concurrency proof** — a new `harness/` test (e.g. `test_snapshot_channel.cpp`,
   wired to `ctest`): one producer thread publishing versioned frames, one consumer thread
   consuming, asserting every consumed frame is internally consistent (no field torn across
   a version boundary) and versions are monotonic non-decreasing. Run it under
   ThreadSanitizer and commit a clean report, the way Anvil's Stage 0 proved its concurrent
   paths (`tests/tsan.sh` there is the pattern). This test **is** the invariant-#4/#8
   coverage for the swap.

### Stage B — the firmware JSON parser, host-proven against the M1 goldens *(host, agentic)*

M1 split *decode* from *semantics*: `parse_anvil_frame` is declared in `engine/include` and
the host implementation (`dc_engine_anvil`, nlohmann) is the **only** thing besides the
harness that allocates. That was the deliberate M1→M3 debt — nlohmann allocates per parse,
which invariant #7 forbids on the target feed path.

1. **A second `parse_anvil_frame`** — a **streaming, allocation-free** parser behind the
   *same* seam. It is portable C++20 (no ESP-IDF), so it links on both host and target; the
   nlohmann implementation stays as the harness reference.
2. **Golden equivalence** — a host test target links the streaming parser **instead of**
   `dc_engine_anvil` and runs `test_replay_goldens.cpp` **unchanged**. Byte-identical
   `FeedEvent`s → identical `DisplaySnapshot`s on both committed traces. That file is the
   acceptance test for the swap (M1 log). This is the invariant-#6 replay coverage for the
   firmware parser — earned on the host, before it ever runs on the board.
3. **Allocation proof** — extend M1's `alloc_probe` (it replaces global `operator new`) to
   the streaming path: parse both full traces, publish every frame, assert the heap counter
   never moves after the first snapshot (invariant #7). Malformed frames are **counted and
   dropped, never turned into a Gap** — M1's rule; Anvil republishes the whole book every
   ~80 ms so one lost frame self-heals.

### Stage C — the net/feed task: TLS WebSocket → engine, on Core 0 *(firmware, bench)*

Stand up `firmware/` (PlatformIO, ESP32-S3) and bring the feed half of the pipeline to life.
Prove it over the serial log **before** the panel exists.

1. **PlatformIO project** in `firmware/` linking `engine/` **as-is** (invariant #1 is what
   makes this a link, not a port). Wi-Fi credentials come from a git-ignored
   `firmware/secrets.h` (or `menuconfig`) — never committed.
2. **TLS WebSocket transport** via `esp_websocket_client` + mbed-TLS to
   `wss://anvil.garethcooke.com/ws?ticker=101` (ticker 101 — the M0/M1 subject). Server cert
   validated against the ESP-IDF certificate bundle (`esp_crt_bundle`). **`Origin`:** M0
   measured the upgrade accepts a client sending **no** `Origin`; default to sending none,
   with a compile-time nominated `Origin: https://anvil.garethcooke.com` fallback ready (the
   ROADMAP line presumed a nominated header — reconcile on the real target; see Known
   unknowns).
3. **Feed task on Core 0.** Received text frame → streaming parser (Stage B) → Anvil adapter
   → phase-1 book → `SnapshotChannel::publish`. Same code path as the harness, different
   source of bytes. Allocation-free after connect + first snapshot (invariant #7).
4. **The 1000 ms RX watchdog, beside the real socket-close callback.** Both raise
   `Gap{Disconnect}`; the watchdog fires at `prev_rx + 1000 ms` (M1's measured number:
   worst healthy inter-frame gap 640 ms, so 1000 ms is safe; firing at the deadline rather
   than at the next frame is what makes the panel *actually* go stale, not vacuously).
   Recovery is **transport-driven, never `seq`-driven** (`anvil-protocol.md` §4): on
   reconnect the fresh `snapshot` is the new baseline; the phase-1 book adopts it and clears
   stale. Track `seq` only as a dedupe/watermark, never for ordering or gap detection —
   Anvil's global `seq` is sparse and non-monotonic on one socket (M0: 42 backward steps /
   5 min).
5. **Bring-up evidence:** a serial log showing connect → first snapshot → steady frames →
   (pull the Wi-Fi) watchdog `Gap{Disconnect}` → reconnect → fresh snapshot → live, with
   the published `DisplaySnapshot` version advancing. This is the feed-side dress rehearsal
   for Stage D's panel.

### Stage D — the render task: `DisplaySnapshot` → HUB75, on Core 1 *(firmware, bench)*

The pixels move to the panel. The M1 console ladder is the visual spec; the panel renders
the same `DisplaySnapshot`, now at 64×64.

1. **Render task on Core 1**, driving the `ESP32-HUB75-MatrixPanel-DMA` library with the
   **M2 pin map and FM6124 init** (both pinned above). `SnapshotChannel::consume` on the
   render side; redraw only when a new version arrives (the API already reports nothing-new
   so the task can idle rather than spin). Library back buffer + flip; target the concept's
   ~30 fps. The feed task (Core 0) and render task (Core 1) meet at the channel **and
   nowhere else** (§2, invariant #8).
2. **The ladder, on 64 rows** — top ~27 levels/side (`kDisplayLevels`), bids green / asks
   red, the spread as the dark gap, trade prints flashing white at the touch, last price,
   and the last-price sparkline strip along the bottom. `status == Stale` greys the whole
   panel — the honesty bit is in the payload, so it is impossible to draw the ladder
   without it (invariant #5). Panel aesthetics within this are the session's call (§8), as
   the console ladder's were.
3. **The acceptance test — pull the Wi-Fi.** Live ladder → pull the Wi-Fi → panel greys
   within ~1 s (the watchdog) → restore → next snapshot → clean live ladder, no torn or
   frozen intermediate. This is invariant 5 in hardware and the same contract the M1
   reconnect golden pinned in software — now proven end to end on the object. Owner-driven
   at the bench; capture a photo/clip in `hardware/` as the M2 first-light photo was.

## Constraints

All invariants apply (they are frozen — if a stage seems to need violating one, **stop and
raise it**, do not refactor through it). Milestone-relevant:

- **#1 — `engine/` stays host-buildable, zero ESP-IDF/FreeRTOS/Arduino.** This is the whole
  bet of M3: the engine links into `firmware/` untouched. The seqlock (Stage A) and the
  streaming parser (Stage B) live in `engine/` and therefore stay portable — `std::atomic`,
  not FreeRTOS primitives. All ESP-IDF, Wi-Fi, TLS, tasks, and the HUB75 library live in
  `firmware/` only (§3 repo layout).
- **#4 — the feed task is never blocked by the render task.** The Stage A channel is
  wait-free on the writer; if the render task stalls (the common case on a microcontroller),
  the feed task drops frames and keeps folding the book. Overflow, if the design can produce
  it, reports `Gap{Overflow}`.
- **#5 — stale is first-class.** The pull-the-Wi-Fi test is the acceptance. A frozen ladder
  that looks live is the one unacceptable output.
- **#6 — no merge without replay coverage.** Stages A and B carry their own host tests
  (concurrency + TSan; the unchanged goldens + alloc probe). Stages C and D are physical and
  cannot be `ctest`-gated — their evidence is the serial log and the panel photo/clip in
  `hardware/`; the *logic* they run was already golden-covered by B on the host.
- **#7 — allocation-free steady state.** After connect + first snapshot the feed→render path
  does no heap allocation on the target. Stage B's alloc probe proves it on the host; the
  streaming parser exists precisely so nlohmann's per-parse allocation never reaches the
  target.
- **#8 — one writer per state.** Only the Core 0 feed task mutates the book; only the Core 1
  render task reads `DisplaySnapshot`. The channel is the single meeting point.

Milestone-specific:

- **One panel, one ticker, one venue.** 64×64 only (no chaining — outside ARCHITECTURE
  scope). Ticker 101, hardcoded. Anvil only (Kraken is M4). Prove the pipeline end to end
  before adding any selection.
- **`engine/` diff is behavioural-zero** — see the note above; `test_replay_goldens.cpp`
  passes unchanged or the change is out of scope.
- **Host loop stays green throughout.** `cmake --workflow --preset host` stays green on
  every commit, including the firmware stages (which add `firmware/` but must not break the
  host build). Firmware builds via PlatformIO; document its build line in
  `firmware/README.md`.

Deliberately unspecified (session decides, log records): double-buffer vs seqlock for the
channel; the streaming JSON parser (hand-rolled vs a freestanding library — must be
allocation-free and pass the goldens); the PlatformIO framework (Arduino-ESP32 component vs
pure ESP-IDF — reuse whatever M2's first-light sketch used); panel aesthetics and the
sparkline's exact form; FreeRTOS task priorities/stack sizes; whether the sparkline history
is a render-side ring (preferred — pure display edge) or, only if that proves inadequate, a
new field on `DisplaySnapshot` (which would be a §4/§5 change to record in §9, not a quiet
addition).

## Known unknowns (resolve and record)

- **`Origin` on the deployed upgrade.** M0 measured *no* `Origin` accepted; the ROADMAP line
  says *nominated* `Origin`. Confirm what the live server wants **from the ESP32**
  specifically — `esp_websocket_client` may inject its own headers. If the default connects,
  keep it and note the ROADMAP line is superseded; if not, enable the nominated
  `https://anvil.garethcooke.com` fallback. Record the answer; the firmware must not carry a
  header the server rejects nor omit one it now requires.
- **TLS trust anchor.** Does `esp_crt_bundle` cover `anvil.garethcooke.com`'s chain (likely
  Let's Encrypt)? If yes, use the bundle for M3 and defer cert-pinning. If the handshake
  fails, pin the specific CA and note why. Record the chain and the choice.
- **PlatformIO framework + the HUB75 library.** Which framework did M2's first-light sketch
  use, and does it give *both* `esp_websocket_client` and `ESP32-HUB75-MatrixPanel-DMA` in
  one build on the S3? Pin it before writing Stage C. (M2 already proved the library's
  64×64 framebuffer fits this exact N16R8 DevKit — that memory risk is retired.)
- **Core-pinning and the DMA peripheral.** Confirm the HUB75 DMA driver (LCD_CAM on the S3)
  and the Wi-Fi/TLS stack coexist without starving each other across the two cores at ~30
  fps and Anvil's ~12 book frames/s. If the render task can't hold cadence, that is a
  priority/placement question, not an engine one — record the tuning.
- **Sparkline source.** M1's `DisplaySnapshot` carries `last_px` and the trade ring but no
  price history. Decide the sparkline is render-side sampled state (default) and record it,
  so no later session tries to reconcile it with the book.

## Definition of done

☐ **Stage A:** `SnapshotChannel` is a wait-free double buffer/seqlock; public API unchanged;
  host concurrency test + a committed clean ThreadSanitizer report; goldens unchanged.
☐ **Stage B:** streaming allocation-free `parse_anvil_frame` links on host and passes
  `test_replay_goldens.cpp` **unchanged** on both traces; alloc probe shows zero heap in
  steady state.
☐ `cmake --workflow --preset host` green from a clean clone, warnings-as-errors clean, with
  A and B merged (this is the agentic-half gate).
☐ **Stage C:** `firmware/` links `engine/` as-is; connects over TLS WS to Anvil; feed task on
  Core 0; 1000 ms RX watchdog beside socket-close, both → `Gap{Disconnect}`; serial log
  shows connect → snapshot → steady → drop → stale → resync → live.
☐ **Stage D:** render task on Core 1 draws the live ladder on the panel (bids green, asks
  red, spread, prints, last px, sparkline); stale greys the panel.
☐ **The pull-the-Wi-Fi acceptance passes** on the bench: live → grey within ~1 s → clean
  resync. Photo/clip committed in `hardware/`.
☐ `firmware/README.md` documents the PlatformIO build/flash line and the `secrets.h` shape.
☐ Session log below filled in (per stage — this milestone spans several sessions); ROADMAP
  M3 ticked and M4 marked **Next** only when the acceptance passes.

## Out of scope

Kraken/Binance and any delta/CRC book work (M4/M5); the dense-window book (M4 — phase-1
"adopt latest snapshot" is still the whole engine here); multi-ticker or venue switching
and the rotary-encoder input (later — one hardcoded ticker for M3); Wi-Fi provisioning UI /
captive portal (a git-ignored `secrets.h` is enough); OTA update; the carrier PCB (M6 —
bare jumpers are M2-proven for M3); enclosure, acrylic, mounting, and the standalone PSU
*purchase* as a project step (M2 sized it; buy when convenient); brightness/ambient-sensor
polish; chaining the second panel / 64×128 (outside ARCHITECTURE scope); any change to book
behaviour or the `FeedEvent`/`DisplaySnapshot` vocabulary (a moved golden means stop).

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     measured figures / serial evidence · exact next step (which stage). -->

### 2026-08-07 · Opus 5 · pre-M3 review remediation (no M3 stage started)

**Done.** Full review of `engine/`, `harness/` and `tools/` (49 findings), then a nine-package
remediation landed on `review/remediation`: `98d4a97` guardrails, `2d3fafe` decimal, `33c87eb`
book, `6c3a19a` adapter+parser, `5fef5cf` console ladder, `93c41bc` trace layer, `d5b8e27`
driver+CLI, `177baec` test signal, `a82c83a` tools, plus this build/docs package. ctest 5/5
green on every commit; 55 → 63 test cases.

**Why before M3, not during.** Stage A swaps `SnapshotChannel`'s internals and Stage B links a
second `parse_anvil_frame`, and this brief's rule is that a moved golden means *stop, out of
scope*. That signal is only clean if the goldens are not simultaneously absorbing refactor
churn, so every `engine/`-touching change landed first. Both committed traces render
byte-identical through the whole remediation — verified per package, not assumed.

**What Stage B inherits.** `parse_anvil_frame`'s postcondition was hand-written before ten
separate returns and *absent from three of them*, correct only by accident of the reset at the
top. It is now one wrapper, and `test_anvil_adapter.cpp` asserts it against the declaration —
19 failure modes plus reentrancy — so the streaming parser inherits a check rather than a
habit. Mutation-verified: breaking the reset fails 30 assertions. The parser is also now the
sole owner of the frame reset; the adapter no longer pre-clears.

**Three defects fixed, all reproduced first.** The ladder drew a 62-column header inside a
57-column box in the state every panel boots into (Stage D takes this renderer as the visual
spec, and on 64 columns that is clipping, not untidiness). `read_trace()` and `TraceReader`
disagreed about what a valid trace is. `dc_ladder --at nonsense` silently replayed all 1406
frames.

**Decisions with why.**

- *Ladder width* — computed in one pass from the widest row the box can contain, **not** by
  buffering rows and measuring. Stage D's renderer cannot hold every row alive (invariant #7),
  and it copies this file.
- *Trace validity* — both rules (`type`, `rx_ns` monotonicity) kept, per the owner: stricter
  fails loudly, and if wrong it shows up immediately. ARCHITECTURE §9.
- *Trailing silence* — off-by-default `end_of_trace_silence_ms`; a file has no "now" and the
  capture tool does not record when it stopped listening, so it is reported, never inferred.
  ARCHITECTURE §9.
- *`std::format` declined* for the report: +90 KiB stripped, +3.4 s on that TU, pulls the whole
  `std::locale` facet suite and allocates 16 times per call. A cast helper is byte-for-byte
  free. That idiom must not reach `engine/` or `firmware/`.

**Measured, and it changes a Stage C assumption.** The target toolchain
`espressif32 6.5.0` pins is **xtensa GCC 8.4**, which rejects `-std=c++20` outright and lacks
`<span>`, `<ranges>`, `<concepts>`, `<bit>`, floating-point `from_chars` and `constexpr`
algorithms. All eight `engine/` headers compile clean on it at `-Os -fno-exceptions -fno-rtti
-Werror`; hot path 1494 → 1541 B after the book consolidation. `dc_engine_target_check` now
enforces this and is mutation-verified — it catches a `constexpr std::reverse_copy` that the
host build accepts. GCC 15.2 for xtensa was also installed and tested: same headers clean,
`+6 B` of flash, and every restriction above lifts.

**Open for Stage C.** The framework known-unknown is also a *language* question. Staying on
6.5.0 keeps M2's Arduino first-light stack and is what Stages A/B need (both host-only;
`<atomic>` is present, so the seqlock is unblocked). Moving to pioarduino / IDF 5.x buys real
C++20 but means re-running first light before Stage C. Note `hardware/BRINGUP.md` does not
record which platform M2's sketch used, though this brief says to reuse it — worth pinning
while the answer is still known.

**Exact next step.** Stage A — `SnapshotChannel` becomes the wait-free double buffer /
seqlock, plus the host concurrency test and a committed clean TSan report. Nothing in this
remediation touched `snapshot_channel.hpp`.
