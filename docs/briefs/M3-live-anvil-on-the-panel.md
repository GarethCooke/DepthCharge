# M3 — Live Anvil on the panel

**Track:** Mixed [A+B] · **Status:** Stages A–D written; the bench acceptance is all that remains
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

☑ **Stage A:** `SnapshotChannel` is a wait-free double buffer/seqlock; public API unchanged;
  host concurrency test + a committed clean ThreadSanitizer report; goldens unchanged.
  *(Done 2026-08-07. Built as a three-slot mailbox rather than two slots or a seqlock —
  both of those are a data race on `DisplaySnapshot` and could not have produced a clean
  report; measured and recorded in ARCHITECTURE §9 and the session log.)*
☑ **Stage B:** streaming allocation-free `parse_anvil_frame` links on host and passes
  `test_replay_goldens.cpp` **unchanged** on both traces; alloc probe shows zero heap in
  steady state. *(Done 2026-08-08. Also links for the target — `dc_engine_target_check` now
  compiles the TU with xtensa GCC 8.4: 5,861 B `.text`, 0 `.data`/`.bss`, and no undefined
  symbol implying a heap, an exception or a float. Scaling was shared via a new
  `anvil_scaling.hpp` rather than by moving the `AnvilFrame` boundary — see the session log
  and ARCHITECTURE §9.)*
☑ `cmake --workflow --preset host` green from a clean clone, warnings-as-errors clean, with
  A and B merged (this is the agentic-half gate). *(9/9 ctest; verified from a clean
  out-of-tree configure as well as in place.)*
☑ **Stage C:** `firmware/` links `engine/` as-is; connects over TLS WS to Anvil; feed task on
  Core 0; 1000 ms RX watchdog beside socket-close, both → `Gap{Disconnect}`; serial log
  shows connect → snapshot → steady → drop → stale → resync → live.
  *(Done 2026-08-09 on the bench; captures and analysis in
  `hardware/bench-2026-08-09-ws-reconnect.md`. The full sequence is in the 18:15–18:22 runs,
  three stop-the-server outages, `parse_errors=0` throughout. Four decisions departed from the brief
  on measured grounds: a pinned ISRG Root X1 instead of `esp_crt_bundle` and a
  free-heap/low-water/largest-block probe instead of `heap_trace`, both because this IDF vintage
  cannot reach the alternative; four reassembly slots rather than two; and the supervisor driving
  reconnects from the polled socket state rather than from `esp_websocket_client`'s event stream,
  which does not raise the events the obvious design needs. All in ARCHITECTURE §9 and the
  session log. **Caveat carried into D:** the 1000 ms watchdog threshold is honest code against
  an eroded premise — the server has slowed to ~6 msg/s from M0's 15.5 and it now trips on
  healthy data. It needs re-deriving from a fresh capture, not nudging.)*
◐ **Stage D:** render task on Core 1 draws the live ladder on the panel (bids green, asks
  red, spread, prints, last px, sparkline); stale greys the panel.
  *(Written and host-proven 2026-08-10, **not yet flashed**. Expanded work order:
  `docs/briefs/M3-stage-D-the-panel.md`. All six deliverables are in the tree including both
  stretch items; the row budget, the geometry, the boot frame, the heartbeat and the
  allocation-freedom of the render path are `dc_tests` cases, and "a stale panel carries no
  hue" is a `static_assert` rather than a test — the renderer emits `Ink` and cannot name a
  colour. What is untested is everything downstream of `PanelCanvas`: the DMA, the pins, and
  whether it looks right.)*
☐ **The pull-the-Wi-Fi acceptance passes** on the bench: live → grey within ~1 s → clean
  resync. Photo/clip committed in `hardware/`. **— the one thing left in M3.**
☑ `firmware/README.md` documents the PlatformIO build/flash line and the `secrets.h` shape.
  *(Plus the board overrides for the N16R8, the bench acceptance procedure, and a table of what
  the statistics block should read on a healthy run.)*
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

### 2026-08-07 · Opus 5 · Stage A complete — the channel is a real cross-core mailbox

**Done.** `SnapshotChannel`'s internals are now a wait-free SPSC mailbox; public API
byte-identical. New `harness/tests/test_snapshot_channel.cpp` (6 test cases), a shared
`channel_stress.hpp`, a standalone `tsan_workload.cpp`, `harness/tsan.sh`, and a committed
clean report at `harness/tests/tsan_clean.txt`. ctest 5/5 → 6/6 green; doctest 63 → 70 cases,
6,970 assertions. **All four committed-trace outputs are byte-identical** (`dc_ladder` and
`dc_replay` × baseline and reconnect, SHA-256 before and after): the goldens did not move, so
by this brief's own rule the change stayed in scope.

**Three slots, not two — and this is the decision to argue with.** The brief offered "two
slots + an atomic version, or a classic seqlock". Both were rejected in favour of three
slots and one atomic word, with `publish`/`consume` each swapping their slot in with a single
`exchange`. The reason is that both offered designs let the reader copy a slot the writer is
writing and then *discard* the result — the tear happens; the version check only stops it
being drawn. That was measured rather than asserted, because the distinction matters:

- a seqlock of exactly that shape delivered **4.8 M frames with zero tears reaching the
  consumer** (x86, GCC 10, `-O2`). It is not broken on today's compiler.
- ThreadSanitizer flagged it on the **first frame** — `Read of size 8` in `consume` against
  `previous write of size 8` in `publish`.

So the objection is not "a seqlock misbehaves"; it is that the copy *is* a data race, stage
A's DoD asks for a clean TSan report, and the only way to have both would be a suppression
sitting on the single cross-core path — on a compiler generation (xtensa GCC 8.4) nobody here
controls. Three slots make the writer's, reader's and ready slots always three distinct
objects, so there is no race to suppress. Two slots cannot achieve that: the reader holds one,
so a writer alternating across two must eventually land on the one being read. Recorded in
ARCHITECTURE §9 — §2 and §5 still say "seqlock/double buffer", and they should be read as
naming the contract, not the storage.

**Measured cost, on the target toolchain (xtensa GCC 8.4, `-Os`).** `.data` 1,176 → 3,528 B
(+2,352, 0.45% of the S3's 512 KB internal SRAM); `.text` `publish` 30 → 87 B, `consume`
39 → 96 B (+114 total). The exchange lowers to an `S32C1I` CAS retry with `memw` fences —
disassembled, not assumed — and to `xchg` on x86. `std::atomic<uint32_t>::is_always_lock_free`
is `static_assert`ed, so `dc_engine_target_check` now proves the one platform fact the design
rests on rather than the header claiming it.

**Wait-freedom, stated honestly.** `publish` is a fixed-size copy plus one `exchange`: no
loop, no lock, nothing the reader can hold. On the LX7 that exchange is a bounded CAS retry
whose only contender is the single reader's own one-shot exchange — bounded, not formally
wait-free, and the header says so in those words rather than claiming more.

**No `Gap{Overflow}`, and why that is not a gap in the invariant.** Invariant #4 says overflow
"reports it as `Gap{Overflow}`". A latest-value mailbox has no queue to overflow: a slow
reader loses intermediate frames and is handed the newest, which is what a ladder wants, and
`DisplaySnapshot`'s eight-deep trade ring carries the prints across the skip. `Gap{Overflow}`
stays reserved for a bounded queue that actually drops, if one ever exists.

**Mutation-verified, both directions.** Pinning every slot index to 0 (i.e. M1's single slot)
fails the two-thread test at version 12 and the workload at version 46. Letting the writer
rotate correctly while the reader never adopts the slot it was handed fails 3 of 3 test cases
— including the *single-threaded* interleaving one, which is reproducible. A test that only
compared versions would have passed both mutants, which is why `channel_stress.hpp` stamps
every field of the 1,168-byte frame from its own version and names the field that tore.

**Review found one real defect in my own test**, now fixed: the two-thread consumer only
evaluated its 30 s deadline on the *empty-poll* branch, so a mutant that returned `true`
forever with a frozen version would have hung ctest rather than failed it. The deadline is now
sampled every 4,096 iterations regardless of outcome. `dc_tests` and `dc_channel_race` also
gained an explicit 120 s ctest `TIMEOUT`: a blocked writer does not fail an assertion, it
hangs, and the useful report is ctest saying so in a minute rather than in 25.

**Two things the harness had to change.** `alloc_probe`'s counter is now
`std::atomic<std::size_t>` — `std::thread` startup allocates, and a plain counter would have
made every threaded test in the binary UB, which is exactly the defect the channel was rebuilt
to avoid. And `harness/tsan.sh` compiles the workload TU **directly** rather than through
CMake: TSan needs Linux, this desk is Windows/MinGW, and the Linux side (WSL) has no CMake.
Ubuntu 20.04's gcc-9 is also unusable here (`libtsan_preinit.o` missing against libtsan0
10.5.0); the run is on **WSL Debian, g++ 10.2.1**. The same TU builds without the sanitiser in
the host build and runs as the `dc_channel_race` ctest, so it cannot rot between TSan runs.

**Open for Stage C, unchanged and still worth pinning:** `hardware/BRINGUP.md` does not record
which PlatformIO platform M2's first-light sketch used, though this brief says to reuse it.

**Exact next step.** Stage B — the streaming allocation-free `parse_anvil_frame`, linked in
place of `dc_engine_anvil`, passing `test_replay_goldens.cpp` unchanged on both traces, with
the alloc probe extended over it. Stage A and Stage B are independent; nothing here touched
the parser seam.

### 2026-08-08 · Opus 5 · Stage B complete — the firmware parser exists, and the goldens did not move

**Done.** A second `parse_anvil_frame` — hand-rolled, streaming, allocation-free — in
`engine/src/anvil/anvil_frame_streaming.cpp` (958 lines), behind the unchanged seam. New
shared `engine/include/depthcharge/anvil/anvil_scaling.hpp`; the nlohmann TU refactored onto
it (207 lines, down from 237 of logic). New CMake targets `dc_engine_anvil_streaming`,
`dc_tests_streaming`, `dc_replay_streaming`. New tests: `test_parser_equivalence.cpp` (shared
— compiled into **both** binaries), `test_streaming_parser.cpp` (streaming-only),
`doctest_main.cpp`. ctest 6/6 → **9/9**; `dc_tests` 70 → 80 cases / 7,118 assertions;
`dc_tests_streaming` 44 cases / 686 assertions. `dc_replay` and `dc_replay_streaming` produce
**SHA-256-identical stdout** on both committed traces.

**The goldens passed unchanged, which is the whole claim.** `test_replay_goldens.cpp` and
`test_anvil_adapter.cpp` are compiled into both binaries from the same source files — two link
configurations, no fork, no `#ifdef`. `dc_harness` no longer links a parser at all; the symbol
is left unresolved and each executable chooses. That is what made "same source, two link
configs" mechanical rather than aspirational.

**The brief asked for scaling to be lifted into the shared adapter. I did something narrower,
and this is the decision to argue with.** Scaling *was* entangled in the nlohmann TU, as the
brief suspected. But moving the `AnvilFrame` boundary so the frame carries raw decimal tokens
would have been the wrong lift: nlohmann's DOM strings die with the parse, so the reference
would have had to copy ~250 tokens per frame into the frame just to hand one onward, and it
would have rewritten `AnvilFrame`, the adapter and the postcondition test — which are Stage B's
own acceptance gate, and which this milestone's constraints say do not change ("Nothing in
`book.hpp`, `feed_event.hpp`, `display_snapshot.hpp`, the adapter logic, or the
scaling/seq-synthesis path changes"). The brief contains both instructions and they conflict.
What the worry actually asked for — one implementation of invariant #3, byte-identical in both
parsers — is delivered by sharing the *conversion* instead of moving the boundary:
`kind_from_type`, `price_text_to_ticks`, `wire_qty_to_steps` and `aggressor_from_text` now live
once, in a header both TUs include. Recorded in ARCHITECTURE §9; **M4/M5 should copy this split,
not the one the brief described.**

**Scan once, adjudicate at the end — the design decision that made equivalence possible.**
The reference builds a DOM and *then* asks questions in a fixed order (type → seq → ticker →
payload; within a side, bids fully then asks; within a level, both keys present then price then
qty). A left-to-right scanner meets fields in wire order, and the two orders disagree about
which error to report when a frame is wrong in more than one place. That is not cosmetic: the
adapter files `BadPrice`, `OtherTicker` and everything-else into three *different* counters, and
those counters are pinned goldens. So the parser never returns early on a semantic problem — it
scans the whole document recording one outcome slot per field (last occurrence wins, as a
`std::map` DOM does), and `adjudicate()` then replays the reference's decision order over those
slots. A frame with a foreign ticker *and* a bad price reports `OtherTicker` even when the price
came first on the wire, because that is what the reference does.

**Hand-rolled, not a library — and the reference is the specification, accidents included.**
The brief recommended hand-rolled and I agree, but the reason turned out to be sharper than
"small and fixed". Equivalence means reproducing nlohmann 3.11.3's *actual* behaviour, and three
of its rules are accidents nobody designed: duplicate keys resolve **last-wins** (its object is
a `std::map` filled through `operator[]` then assigned over); an integer too large for
`uint64_t` becomes a **float**, so an over-range qty is `BadShape` and not a saturated number,
while one that fits `uint64_t` but not `int64_t` is accepted and *wraps* negative and is then
caught by the `raw < 0` guard; and a **NUL byte ends the document** even mid-buffer, so
`{...}\0junk` parses clean. A parser written to a reasonable reading of JSON would have moved
goldens on malformed input while looking correct. All three are now pinned in
`test_parser_equivalence.cpp`. ARCHITECTURE §9.

**Verified adversarially, because "I read it carefully" is not evidence for a hand-rolled
parser.** Three independent agents, ~2.4 M differential inputs plus 35.5 M sanitised parses:

- **Differential fuzz, 1,421,150 records** through both implementations: real frames from both
  traces, byte-level mutations, exhaustive truncation at every offset, a structured adversarial
  grammar, pure random bytes — across 23 `SymbolSpec` configurations. **5,669 differences, 0
  unexplained.** The decisive corpus is the one engineered so that *no* declared divergence can
  fire (nesting ≤ 20, nothing near 1e308, escaped strings ≤ 40 B) replayed over 24 spec
  configurations: **567,842 records, zero differences.**
- **Exhaustive lexer sweep, ~1,022,000 inputs**: every byte 0–255 raw inside a string in seven
  contexts, all 65,536 two-byte raw sequences, all 65,536 `\uXXXX`, full surrogate high×low
  sweeps, every permutation of the top-level member set, int64/uint64/double boundary sweeps.
  0 diffs outside the three.
- **Memory safety**: ASan+UBSan under WSL Debian g++ 10.2.1, *and* mmap guard pages either side
  of the input (so any read at/past `end_` is an instant SIGSEGV) — 35.5 M parses including
  every frame of both traces truncated at **every byte offset** (17,463,878 parses, ~70 GB).
  Zero faults, with positive controls proving the harness traps.
- **Coverage** 99.81% of lines, 100% of branches executed. The one uncovered line is the
  unreachable defensive `return` after an exhaustive switch.

**Measured on the target toolchain** (xtensa GCC 8.4, `-Os -fno-exceptions -fno-rtti -Werror`):
`.text` **5,861 B**, `.data` 0, `.bss` 0, no `.init_array`. Undefined symbols are exactly
`__ashldi3 __divdi3 __lshrdi3 __moddi3 __udivdi3 memcmp memset strlen` — no `operator new`, no
`__cxa_*`/`_Unwind_*`, nothing from ESP-IDF, and no soft-float helper, so invariants #7, #1
and #3 are link-time facts rather than claims. Zero floating-point instructions in the
disassembly. `dc_engine_target_check` now compiles this TU too, not just the headers.
Stack via `-fstack-usage`: every frame static, largest 272 B, acyclic call graph, worst chain
~600 B — **input-independent**, so a 5,000-bracket frame costs what a flat one does.

**Three deliberate divergences, asserted on both sides of each boundary.** Nesting: one skipped
value may hold 64 containers, not 65 (the container stack is one `uint64_t`, which is what keeps
the skipper iterative and heap-free). An *escaped* price whose unescaped form reaches 65 bytes is
`BadPrice` rather than its value — escaped `type`/`aggr`/keys agree at any length, and unescaped
strings are sliced in place with no limit at all (verified to 500,000 chars). Numbers in the open
band (`DBL_MAX`, 1e309) are finite here and reject the document there. **The review pass corrected
my wording on two of these** — the depth budget is per `skip_value()` call, not per document, and
only a *price* token can diverge on length — so the comment now states the implemented rule, since
that block is what a future editor will treat as the spec. One-sided tests were replaced with
both-sided ones: "200 is rejected" would still pass if the cap silently fell to 8.

**The code review found one real DRY defect and I took it.** `price`/`qty` were read by two
near-identical 14-line blocks — once for a trade's own members, once for a book level's — in a
file whose entire ongoing risk is drifting from a second implementation. Collapsed into
`read_price_value`/`read_qty_value`/`read_integer_value`/`read_string_value`. Because this landed
*after* the verification, it was re-proved rather than assumed: the streaming parser's output over
820,703 corpus records is **byte-identical before and after** (SHA-256 per corpus), and the full
differential against the reference reproduces the same counts (corpus1 5,536 diffs; the two
no-divergence corpora 0 and 0). It also took 378 B off the target `.text`.

**Open, and not a Stage B problem.** `wire_qty_to_steps` does an `int64_t` divide and modulo per
level — ~3,000 software 64-bit divisions a second on the LX7 at Anvil's depth and rate. It is
inherited from M1's shared scaling, identical in both parsers, and `qty_step` is 1 for every venue
we consume. Not touched here (it would be a behaviour change); it is the first place to look if the
Stage C feed task ever needs headroom.

**Still open for Stage C, unchanged:** `hardware/BRINGUP.md` does not record which PlatformIO
platform M2's first-light sketch used, though this brief says to reuse it. Note also that
`dc_engine_target_check` resolves its compiler through the *glob fallback*, not the configured
default path — the unversioned `toolchain-xtensa-esp32s3` directory exists on this desk but
contains only `gcc`, no `g++`.

**Exact next step.** Stage C — stand up `firmware/` (PlatformIO, ESP32-S3), link `engine/`
as-is, TLS WebSocket to `wss://anvil.garethcooke.com/ws?ticker=101`, feed task on Core 0
using this parser, 1000 ms RX watchdog beside the socket-close callback. Resolve the framework
known-unknown first: it is also a language-version decision (espressif32 6.5.0 pins xtensa
GCC 8.4; pioarduino / IDF 5.x give GCC 14–15). Both host stages are done, so the agentic-half
gate is met.

### 2026-08-08 · Opus 5 · Stage C written and building — **not yet flashed**

**Done (CC-side).** `firmware/` exists: PlatformIO + Arduino, links `engine/` as-is, Wi-Fi →
TLS WebSocket → streaming parser → adapter → phase-1 book → `SnapshotChannel`, feed task
pinned to Core 0, serial consumer on Core 1, RX watchdog, reconnect supervisor, heap
instrumentation. `pio run` clean: **RAM 30.7 % (100,656 / 327,680 B), flash 832,201 B**. Host
loop untouched and still green — ctest **9/9**, doctest 80 → **97 cases / 9,101 assertions**.
Nothing on the bench yet; **every runtime claim below is static or measured on the desk, never
observed on the board.**

**The framework known-unknown is closed, with evidence rather than recall.** M2's sketch is not
in the repo and `hardware/BRINGUP.md` never recorded its platform, but the machine does:
`~/.platformio/packages` holds `framework-arduinoespressif32` 3.20014.231204 (Arduino-ESP32
2.0.14) and **no `framework-espidf` at all**, so the bench has only ever built Arduino. Pinned
`espressif32@6.5.0` + `framework = arduino`, which also keeps stage D's HUB75 library on the
stack M2 proved. Both stage-C dependencies are present in that framework and were checked, not
assumed: `libesp_websocket_client.a` for esp32s3 is shipped **and in the default link set**,
and `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`.

**Task model — not a preference, forced by invariant #8.** The brief offered "process in the
callback" vs "a dedicated feed task". The callback design is unbuildable here: the RX watchdog
must fire when data *stops*, so it needs a context awake during silence, and it raises
`Gap{Disconnect}` — which writes the book. A timer would therefore be a *second book writer*.
The watchdog and the frames must be serviced by the same context, i.e. a task blocking with a
timeout, and the timeout **is** the watchdog. Independently: this IDF vintage's
`esp_websocket_client_config_t` has no `task_core_id`, so "feed on Core 0" is only achievable
in a task we create. The callback fills a slot and posts it; the feed task owns the engine.

**TLS: a pinned root, because the bundle is unreachable — and the chain was measured.**
`esp_crt_bundle` is attached via `esp_tls_cfg_t::crt_bundle_attach`, and the IDF 4.4
`esp_websocket_client_config_t` has no such field (it offers `cert_pem` and
`use_global_ca_store`, nothing else). Reaching the bundle means pure ESP-IDF and re-running
first light. So the brief's fallback applies. Measured against the live server:
`anvil.garethcooke.com` ← `Let's Encrypt YE1` ← `ISRG Root YE` ← `ISRG Root X2` ← **`ISRG Root
X1`** — four certificates, ending cross-signed by X1. Pinned X1 (SHA-256 `96:BC:EC:06:…:08:C6`,
expires **2035-06-04**), and verified with a control before committing it:
`openssl verify -CAfile isrg_x1.pem -untrusted <chain> <leaf>` → **OK**, against an unrelated
self-signed CA → `error 20 at depth 3`. The committed PEM was **generated from the verified
file and round-tripped back out of the C++ literal** and compared byte-for-byte, rather than
transcribed — the first draft was hand-typed and that is exactly the thing not to trust.

**Reassembly buffer sized from measurement.** Largest Anvil message across both captures is
**8,726 B** (a `book` frame), mean 6,486. Two static 16 KiB slots — 1.9× the largest ever seen.
Oversize is a *defined, counted drop*, never a realloc. The client's own RX buffer is
deliberately **smaller** (4 KiB) so reassembly runs ~12 times a second rather than being a rare
path that rots.

**The reassembler is host-tested, and that was the highest-value thing in the stage.** It is
the only genuinely new logic here (everything else is engine code or a thin Espressif wrapper)
and a replay trace cannot exercise it — the capture tool's library reassembles before writing
the line. So it was extracted into `firmware/src/frame_reassembler.hpp` with **zero ESP-IDF**,
templated on a slot pool, and `harness/tests/test_frame_reassembler.cpp` runs it in `dc_tests`
against a fake that models ownership: oversize, no-free-slot, socket-death-mid-message,
new-message-mid-message, ping/pong, zero-length frames, every chunk size 1–40, and a 5,000-step
random walk asserting **no sequence of chunks ever leaks a slot**. This is the only `firmware/`
path the host build knows about, and it is a header — no firmware `.cpp` compiles on the host.

**Adversarial review found six real defects; all six are fixed.** Three independent reviewers
(FreeRTOS/ownership, Espressif API, host-vs-firmware semantics) over ~1.6 M tokens; three
further findings were refuted on verification. The two that would have cost a bench evening
each:

1. **The firmware was mute.** Every `ESP_LOGI`/`ESP_LOGW` in `feed_task.cpp`,
   `serial_console.cpp` and `heap_probe.cpp` was compiled out — those TUs include `esp_log.h`
   directly, where `LOG_LOCAL_LEVEL` defaults to `CONFIG_LOG_MAXIMUM_LEVEL`, which the
   precompiled framework ships as **1 (ERROR)**. `CORE_DEBUG_LEVEL` does not fix it: that only
   feeds Arduino's `esp32-hal-log.h`, which those TUs never see. Stage C's entire evidence is
   the serial log, so the board would have come up apparently dead while working perfectly. The
   fix needs all three of `-D LOG_LOCAL_LEVEL=3`, `-D CORE_DEBUG_LEVEL=3` and
   `esp_log_level_set("*", ESP_LOG_INFO)`. **Verified by grepping the built `.elf` for the log
   strings** — absent before, present after — because my first attempt (CORE_DEBUG_LEVEL alone)
   looked right and did nothing.
2. **A clean server-side CLOSE killed the feed permanently.** `esp_websocket_client`'s task
   echoes the close frame, breaks its run loop and `vTaskDelete(NULL)`s; `auto_reconnect` is
   consulted only on the abort/error path, so nothing restarts it. Invariant #5 survives — the
   panel greys — but it greys *forever*, reading at a bench as an intermittent hang hours into
   a healthy run, with `socket_gaps` possibly still 0. Anvil gets redeployed, so this is not
   hypothetical. Added a supervisor in `loop()` (never the callback — the API forbids
   `stop()`/`start()` there) that restarts the client after 20 s disconnected, chosen above the
   client's own 10 s backoff so it backs it up rather than fights it.

3. **A systematic parse failure froze the ladder reading LIVE** — the one output invariant #5
   forbids. The watchdog was armed by *byte arrival*, so a server sending frames the parser
   rejects (or nothing but `summary`) kept feeding it while the book never advanced. Fixed by
   arming on an **event reaching the book**, and the cost of doing so was measured rather than
   argued: over both captures the worst healthy gap is **640.2 ms whichever way you count** —
   any frame, event-producing frames, or book frames only — because the book stream is the
   dense one and 2 Hz summaries never fill a hole it left. The 1.6× margin under 1000 ms
   survives intact. *This makes the firmware deliberately **stricter** than the host replay
   driver, which still gaps on `rx_ns` holes between any frames; aligning the host is a
   candidate for M4 and is not done here because that driver is golden-covered.*
4. **`worst_gap_us` could never record an outage** — it was gated on the same flag the watchdog
   clears, so the one number that quantifies a gap was suppressed by the thing that detects it.
5. **Nothing was published before the first event**, so a connected-but-unproductive feed left
   the consumer with no snapshot at all — at stage D a dark panel, which says nothing where #5
   requires "not trusted". The feed task now publishes the book's initial `Stale{Resync}` once
   at start-up.
6. **The feed task logged.** Arduino routes `ESP_LOGx` to `log_printfv`, which `malloc`s for
   lines over 64 chars and takes the UART mutex with `portMAX_DELAY` — so a log line on Core 0
   would block the feed on a mutex the console holds on Core 1 (invariant #4) through an
   allocation (#7). All logging removed from `feed_task.cpp`; every event it reported is
   already a counter, and the Live↔Stale transitions are printed by the console off the
   published snapshot. Noted in the header so stage D does not reintroduce it.

**Invariant #7 on the target — settled statically, and §9 written from it.** ESP-IDF heap
tracing is **unavailable**: the precompiled framework ships `CONFIG_HEAP_TRACING_OFF=y`, so
there is no trace code to link. More importantly the question it was meant to answer is already
answered — `esp_websocket_client` dispatches every event through `esp_event_post_to`, which
heap-copies the 28-byte payload and frees it on return (visible as `memset → calloc → memcpy →
… → free` in the shipped `libesp_event.a`): ~37 balanced pairs a second at three DATA events
per frame. So #7 on the target reads as **no net allocation and no fragmentation drift**, with
the engine half still holding in the strong form and proven. Recorded in ARCHITECTURE §9.
`heap_probe` samples free bytes, low-water and largest-free-block and is documented as
detecting a *leak or fragmentation* — **not** the churn: an earlier draft claimed the low-water
mark could see it, which is false (since-boot minimum, no reset, and the baseline is taken
after the dip). That claim was corrected rather than shipped.

**Also resolved:** `payload_offset` accumulates across chunks of one frame and the opcode is
repeated with FIN masked off (confirmed by disassembling `libtcp_transport.a`), so the
reassembly contract holds; `cert_len = 0` is correct for a NUL-terminated PEM; `%lld`/`%llu`
are safe (`CONFIG_NEWLIB_NANO_FORMAT` is not set); `xTaskCreate`'s stack argument is **bytes**
on ESP-IDF, not words — the parameter was misnamed and the sizes are now 8 KiB feed / 6 KiB
console.

**Open / for the owner at the bench.** The board config is the untested part: the stock
`esp32-s3-devkitc-1` is the N8 variant, so flash size, `default_16MB.csv` and
`memory_type = qio_opi` are overridden for the N16R8 — if it fails to boot, the two PSRAM lines
are the first thing to bisect (stage C uses no PSRAM), and the answer should be recorded either
way because stage D wants it. `firmware/README.md` has the full acceptance procedure and a
table of what the statistics block should say on a healthy run.

**Exact next step.** Owner: flash, capture the serial log, run the pull-the-Wi-Fi acceptance,
and commit the log to `hardware/`. Then **Stage D** — the render task on Core 1, HUB75 with the
M2 pin map and FM6124 init, replacing `serial_console.cpp`'s body while keeping its shape
(Core 1, `consume()`, redraw only on a new version).

### 2026-08-09 · Opus 5 · first bench run read; slot starvation fixed, rate instrumented

**The board ran.** Owner flashed and captured ~40 s of serial log. Everything the stage was
built to prove worked on the first attempt: Wi-Fi and TLS up in 2.5 s, WS upgrade accepted with
no `Origin`, first snapshot adopted at v2, `parse_errors=0 price_errors=0 unknown=0 trunc=0`
across 190 messages, `wd_gaps=0 sock_gaps=0` (no false watchdog trips), and the boot sequence
showed the honest `STALE (resync)` at v1 before any data — the frame added at review, without
which the panel would have been dark rather than grey. `backseq=1`: M0's global-counter-runs-
backwards finding, reproduced on silicon. Heap over the steady window: `free` delta **0**,
`largest` **+0**, `low` flat — the invariant-#7 reading behaving exactly as written.

**One real defect in the log, and it was nearly read the wrong way round.** `no_slot` climbed
1 → 33 while `published` went 22 → 190: **16% of inbound messages discarded**, linearly, not a
transient. It was initially read as the mailbox's superseded-frame count and therefore healthy.
It is the opposite — `FramePipe::acquire()` failing means a whole WebSocket message never
reached the parser. The mailbox's drops are deliberately *uncounted* (§9, 2026-08-08) but are
recoverable as `published_v − drawn`, which was 15 of 166 (9%) and genuinely is healthy
consumer lag. Two numbers, opposite meanings, and the confusable one now prints as
`superseded=` on the same line so nobody has to guess again.

**Why it mattered more than the wasted bandwidth.** Correctness was never at risk — book frames
are idempotent full replaces and nothing failed to parse. But each dropped message lengthens the
gap between *events*, and that gap is what the RX watchdog measures: `worst_gap` came in at
**721 ms against the 1000 ms threshold**, a 1.39× margin where M1's 640 ms measurement had
assumed 1.6×. Left alone it trends toward a **false STALE**, which is the precise lie
invariant #5 exists to prevent.

**Fix: `kFrameSlots` 2 → 4.** The two-slot reasoning was right about averages and wrong about
the distribution — an ~8 KB message holds a slot across the three DATA events it takes to
reassemble at a 4 KiB RX buffer, while the feed task spends up to 8.5 ms on the previous one,
and Anvil coalesces so three back-to-back is routine. Cost 64 KiB total; RAM 30.7% → **40.7%**.
`kWsRxBufferBytes` stays at 4096 deliberately: one event per message would cut both slot
residency and the `esp_event` allocation rate, but it would also stop the wire exercising chunk
reassembly — and 190 multi-chunk messages with zero parse errors is exactly the evidence worth
keeping.

**Instrumented so the next run measures instead of infers.** The log showed ~6.7 messages/s
attempted against M0's 15.5 — but with no way to distinguish a slower server from frames lost
upstream of us, and those have different fixes. Added `bytes_published`, `largest_message`,
`smallest_message` and `chunks` to `FramePipeStats`, and a `-- rate` line printing per-window
messages/s, attempted/s, loss %, events/s, KiB/s, mean message size and **chunks per message**.
The arithmetic is integer-only and was checked on the host against the real log windows before
shipping (it reproduces 5.09/s in / 6.29/s attempted / 19% lost for B3→B4, and at M0's 15.5
frames/s it returns M0's 13.6 events/s) — the first draft of the KiB/s expression folded decimal
and binary thousands together and was wrong by 2.4%.

**The next hardware run is paired with a fresh capture**, and `firmware/README.md` leads the
acceptance with it: run `tools/capture_anvil.py` from the desk for the whole session, then
`dc_replay` the result and compare its frames/s and max gap against the board's `-- rate` and
`worst_gap`. Both ends of the rate, measured at once. If the host also reads ~6/s the server has
slowed and the watchdog margin needs re-deriving against a fresh measurement; if the host reads
~15/s and the board ~6/s, the loss is between the socket and the reassembler and is ours.

**Also worth recording:** `cont=0` — no server-side WebSocket fragmentation, so that path
remains host-proven only, as expected. But *chunk* reassembly fired on every one of the 190
messages, which is what the deliberately-undersized RX buffer bought. And `worst_frame=8548 µs`
for parse → book → publish of one ~8 KB frame: 10% of one core at 12 frames/s, fine now, worth
watching when stage D puts a render task beside it. The cross-core stats read is unsynchronised
`uint64_t` — the first block showed `in=21` against 20 classified, a benign mid-classify sample,
though on a 32-bit target it could in principle tear rather than merely be stale.

**Exact next step unchanged:** the paired bench run above, then Stage D.

### 2026-08-09 · Opus 5 · reconnect in 2 s, and the supervisor's margin made a compile error

**The change asked for could not be made the way it was specified, and the reason is worth
recording.** The plan was to hoist the client reconnect backoff to a shared `constexpr` and feed
`esp_websocket_client_config_t::reconnect_timeout_ms` from it. **That field does not exist in
this vintage** — checked member by member in the shipped
`framework-arduinoespressif32/tools/sdk/esp32s3/include/esp_websocket_client/include/esp_websocket_client.h`;
it arrives in a later IDF than the one Arduino-ESP32 2.0.14 ships. The library's 10 s
`WEBSOCKET_RECONNECT_TIMEOUT_MS` is compiled into the precompiled archive with no override. This
is the **third** time this vintage has decided a design here, after the certificate bundle
(→ pinned root) and heap tracing (→ `heap_probe`), and the pattern is now explicit enough to
expect: assume nothing about this config struct without reading it.

**So the supervisor took the cadence over instead of configuring it.** `WEBSOCKET_EVENT_ERROR` or
`WEBSOCKET_EVENT_CLOSED` sets one lock-free flag; `supervise()` sees it and does the stop/start
`kReconnectBackoffUs` = **2 s** later, preempting the client's unreachable 10 s wait. The client's
own auto-reconnect stays enabled underneath and simply never wins the race. Net effect on an
error-path outage is a retry every 2 s from each reported failure, rather than one every 10 s.

**Why arming on failure — and not on DISCONNECTED — is the whole safety argument.** A retry timer
that can fire mid-handshake is a feed that never recovers, and `esp_websocket_client` dispatches
`DISCONNECTED` from `abort_connection()`, which is also what our own `stop()` triggers. Arming on
it would mean every restart re-armed the timer that caused it, and the retry 2 s later would land
on the handshake it had just started. `ERROR`/`CLOSED` mean an attempt is definitively over, so
there is nothing in flight to interrupt; an attempt still handshaking has raised neither and
leaves the flag clear. The cadence is "2 s since something went **wrong**", not "2 s since
something happened".

**The stale comment was the actual defect.** `kReviveAfterUs = 20 s` was justified in prose
against "the client's own reconnect backoff (10 s in this vintage)" — a constant that this change
moves, in a comment that would have gone on being believed. Replaced with
`static_assert(kReviveAfterUs > kReconnectBackoffUs + kHandshakeBudgetUs)`, where
`kHandshakeBudgetUs` = 5 s is the first bench run's cold 2.5 s Wi-Fi-plus-TLS bring-up with room
for a slow DNS answer. **Verified by breaking it**: set to 6 s the firmware fails to compile with
exactly that message; at 12 s it builds clean. The margin is now a claim that cannot rot.

**`kReviveAfterUs` retuned 20 s → 12 s**, which the assert permits (12 > 2 + 5) and which halves
the worst case for the outage that raises no event at all — a clean close leaves no task to report
anything, a wedged client leaves no event either, and that path is still measured from when the
socket went down and so is still the one that *can* land mid-handshake.

**`loop()` now polls at 250 ms, not 1000 ms.** The supervisor's shortest deadline is 2 s, and a 1 s
poll put up to 50% jitter on the single number this change exists to move. loopTask is priority 1
and reads a flag; four wake-ups a second is not worth a second of grey panel.

**Also:** every retry logs one line with the outage duration and a consecutive-attempt count, so a
long Anvil outage reads as a visible sequence rather than a silent one — worth knowing because at
a 2 s cadence a ten-minute outage is ~300 task create/destroy pairs of a 6 KiB stack. They are
balanced and same-sized so `heap_4` should reuse one region, and `heap_probe`'s largest-free-block
is already the instrument that would say otherwise. **Unmeasured — check it on the next long
outage** rather than assuming it.

**State of the tree.** Host suite green (9/9, `--preset host-mingw`; note the plain `host` preset
picks `Unix Makefiles` and collides with the existing `build/host` cache — use the mingw pair on
this desk). Firmware builds clean; RAM unchanged at 40.7%, flash 12.7%.

**NOT DONE — the bench re-run is the owner's, and it is the acceptance for this change.** Nothing
here has run on hardware: it needs the board flashed and Anvil actually stopped, and Anvil is a
deployed service this repo does not touch. Record two numbers against the pre-change baseline
(grey within a beat of the stop; LIVE back in **~18 s**):

1. **Time from server stop to grey** — expected *unchanged*. Nothing in this change touches
   detection; it is still `Gap{Disconnect}` from the socket event or the 1000 ms RX watchdog,
   whichever is first.
2. **Time from server return to LIVE** — expected **~2 s + handshake**, so call it 2–5 s. If it
   still reads ~10 s or more, the outage is producing `DISCONNECTED` with neither `ERROR` nor
   `CLOSED` and the flag is never being armed; the `reconnecting after N ms down (attempt #k)`
   line is present exactly when the fast path fired, so its absence is the diagnosis.

Also worth capturing from the same run: whether the stop is seen as `CLOSED` or as `ERROR`, since
the 18 s baseline suggests the error path rather than the clean close the supervisor was
originally built for, and the brief has been assuming the latter.

**Exact next step:** the bench re-run above (pair it with the `tools/capture_anvil.py` run the
previous entry asked for — one board session answers both), then **Stage D**.

### 2026-08-09 · Opus 5 · the bench refuted the fast path; the trigger moved to the socket state

**The previous entry's change did not work, and the log says so in one number: `grey for
18609 ms`.** Two stop-the-server outages at 17:41 and 17:44, both recovered by the 12 s backstop
(`websocket down for 12 s and not recovering — restarting client (#7 / #10)`), and the
`reconnecting after N ms down` line the fast path prints appears **nowhere**. That is proof rather
than inference: the armed path returns early while it is waiting, so the backstop could not have
run if the flag had ever been set.

**Why it never armed.** It armed on `WEBSOCKET_EVENT_ERROR` and `WEBSOCKET_EVENT_CLOSED`, and this
vintage dispatches neither for a socket that dies. A read failure lands in
`esp_websocket_client_abort_connection()`, which raises **`WEBSOCKET_EVENT_DISCONNECTED`** — the
one event the previous entry deliberately excluded, and excluded for a reason that was correct in
itself: `stop()` runs through the same `abort_connection()`, so arming on `DISCONNECTED` would have
each restart re-arm the timer that caused it. The `E ... Error receive data` lines in the log are
the library's own `ESP_LOGE` on the way there, **not** an event; everything it would otherwise say
about reconnecting is an `ESP_LOGI` compiled out of the precompiled archive, which is why the log
looks silent between the error and the backstop.

**The lesson is bigger than the bug: supervise on observed state, not on reported events.** The
trigger is now `supervise()`'s own poll of `esp_websocket_client_is_connected()` — which needs no
theory about the library's internals, and which had been driving the 20 s backstop correctly since
the day it was written. The event handler now drives nothing at all; it reports to the feed task
and logs the event id once per outage, so the next run settles on paper what this one cost an
outage to learn. Recorded in ARCHITECTURE §9 as the general rule.

**One mechanism now, not two.** `kReviveAfterUs` is gone. With a working trigger the retry covers
every case the backstop did — clean close, wedged client, read error — and a second timer firing
the identical stop/start would have been two names for one thing. What replaces it is the pair the
correctness argument actually needs: `disconnected_since_us_` schedules the first attempt a backoff
after the feed dies, and `attempt_started_us_` buys that attempt `kHandshakeBudgetUs` of immunity,
inside which `supervise()` does nothing. Without the second clock a 2 s cadence kills every attempt
a beat before it succeeds, turning a recoverable outage into a permanent one — which is the failure
this file has been guarding against since the supervisor was added, now enforced by a clock we own
rather than by a guess about which event will arrive.

**`kHandshakeBudgetUs` 5 s → 7 s, and it is now pinned to a measurement.** The bench gives
`restart → LIVE` of **6136 ms** (revive at 17:44:52.791, LIVE at 17:44:58.927), so the old 5 s
budget was *under* the only recovery ever observed — it would have preempted the very attempt that
worked. That figure is now `kObservedRecoveryUs` with
`static_assert(kHandshakeBudgetUs >= kObservedRecoveryUs)`, so the budget cannot be tuned back
under the evidence. The user-requested
`static_assert(... "supervisor grace must exceed a full client reconnect or it preempts one")`
survives on `kRetryCycleUs`.

**The 6136 ms covers two things and the split is unknown**, which is the next thing worth knowing:
it is the socket coming up *plus* Anvil's first snapshot, because LIVE is granted on data and never
on a socket (invariant #5). The first bench run's 2.5 s cold Wi-Fi-plus-TLS suggests the socket half
is the smaller one. `supervise()` now prints `socket up N ms into attempt #k` on every recovery, and
the difference between that and the panel's own `grey for N ms` is Anvil's snapshot latency. If the
socket half is ~2.5 s, the remaining ~3.6 s is the server's and no backoff here can touch it.

**Simulated before flashing, because the last change was not.** The timer arithmetic was replayed
on the host against the real outage (scratchpad, not committed — the durable version of this is the
open item below). Expected results for the re-run:

| scenario | grey |
| --- | --- |
| bench outage 2 replayed (server already back, 6136 ms recovery) | **8.5 s** (was 18.6 s) |
| same, if the socket half turns out to be the 2.5 s of the first run | 8.4 s — unchanged, the snapshot dominates |
| redeploy that takes 10 s to come back | 17.5 s, two attempts |
| connect slower than the 7 s budget (9 s) | 11.3 s, recovers — but this is the marginal case the budget exists to keep off |

**So the honest target is ~8.5 s, not the ~2 s the change was asked for.** 2 s was reachable only
against the assumption that reconnecting is instant; it measures 6.1 s, and that term is the floor
regardless of what the backoff is set to. The improvement is 18.6 s → ~8.5 s, and the next lever is
the 6.1 s itself, not the wait in front of it.

**State of the tree.** Firmware builds clean, RAM 40.7%, flash 12.7%. Host suite untouched by this
change and green as of the previous entry.

**Open — and it is the real remedy.** This bug shipped because the scheduling decision only exists
on hardware, in a path a bench run reaches twice an evening. It is pure arithmetic over two
timestamps and has no ESP-IDF in it: lifting it into a `ws_supervisor.hpp` free function beside
`frame_reassembler.hpp`, with a `dc_tests` case per scenario in the table above, would have caught
this on the desk in seconds. `frame_reassembler.hpp` is precedent that this works and is the
highest-value thing the stage produced. **Not done here** — it is a structural change and the
board is mid-loop.

**Exact next step:** re-flash, re-run the stop-the-server test, and record (a) `grey for N ms`,
expected ~8.5 s, (b) the new `socket up N ms into attempt #k` line, which splits the 6.1 s for the
first time, and (c) the `ws down: event N` id, which settles the dispatch question permanently —
`2` confirms `DISCONNECTED` as diagnosed. Then the host-test extraction above, then Stage D.

### 2026-08-09 · Opus 5 · bench confirms the fix, and the same log catches the boot handshake

**It works, and the prediction was exact.** Bench at 18:15–18:17. The socket outage at
18:15:58.488 greyed the panel at 18:15:58.507, `websocket down 2 s — restarting client
(attempt #2)` fired at 18:16:00.947 — **2.44 s**, being the 2 s backoff plus the 250 ms poll
granularity plus the stamp tick — and `worst_gap` moved **783 ms → 8509 ms** across the outage.
The previous entry's simulation predicted **8.5 s** for exactly this scenario. Measured 8509 ms,
against 18609 ms before the change. **2.2× better, and the residual is the reconnect itself, not
the wait in front of it.**

**`ws down: event 2` — the diagnosis confirmed on silicon.** `WEBSOCKET_EVENT_DISCONNECTED`, the
event the first attempt deliberately excluded, is the only one this vintage raises for a dead
socket; it never dispatches `ERROR` (0) or `CLOSED` (4), which is what that version armed on. That
question is now closed with a number from the board rather than an argument from the headers, and
the log line stays because it costs one line per outage and it is the thing that would catch an IDF
bump changing the answer.

**The same log caught a bug the change introduced, in the one place the design forgot.** The first
outage restart is logged `attempt #2`, and `connects=2` before any outage occurred: `attempts_`
only ever incremented in `supervise()`, so it had already restarted the client once, during boot.
The cause is that `attempt_started_us_` was only ever stamped by `supervise()`'s own restarts —
the `esp_websocket_client_start()` in `WsTransport::start()` got **no handshake immunity at all**,
so a client that had simply never connected yet was indistinguishable from one that had dropped,
and the supervisor cut straight through the cold TLS handshake 2 s into boot.

That is precisely the failure the handshake budget exists to prevent, left uncovered on the one
path that is not a retry. **Fixed**: `start()` now stamps `attempt_started_us_` and counts itself
as attempt #1. It recovered every time and cost only a wasted connection per boot, which is why it
read as noise — the counter in the log line is the only reason it was visible at all, and that is
an argument for printing the attempt number rather than a bare message.

**The second event in the log is not a socket outage and should not be read as one.** At
18:16:54.996 the panel greyed and was LIVE again 155 ms later. `wd_gaps` 0 → 1, `sock_gaps`
unchanged at 1, `connects` unchanged at 3: the socket never dropped. That was the **RX watchdog**
on a 1.9 s hole in book events (18:16:53.075 → 18:16:54.996), greying on data stopping and
recovering on data resuming — invariant #5 behaving exactly as specified, and the supervisor
correctly doing nothing, because there was nothing wrong with the transport. Worth noting that
whatever was done to the server at that moment did not disturb the TCP connection at all.

**Still not split: the 6.1 s recovery.** The `socket up N ms into attempt #k` line lands in the
window between the two pasted log blocks (the reconnect completed around 18:16:07), so the socket
half versus Anvil's snapshot latency is still unknown. It is one line in the next capture and it
is the only remaining lever on the 8.5 s: if the socket half is the ~2.5 s the first bench run
measured, the other ~3.6 s is the server's and nothing in this file can touch it.

**Also in this run:** `worst_frame` 8064 → **18171 µs** for one frame's parse → book → publish,
more than double the previous worst and 22% of one core at 12 frames/s. Nothing here changed that
path, and `max=8886 B` is a larger frame than the previous run's 8636 B, but it is worth watching
when Stage D puts a render task beside it. Heap flat across both outages (`free` −16 B, `largest`
never below baseline), so the restart churn this cadence causes is not fragmenting anything at
these outage lengths.

**State of the tree.** Firmware builds clean, RAM 40.7%, flash 12.7%. Host suite unaffected.

**Exact next step:** flash the boot-immunity fix and confirm two things in one capture — the first
`attempt #1` now belongs to the boot connect with no restart during it (`connects=1`, not 2,
before any outage), and the `socket up N ms into attempt #k` line, which finally splits the 6.1 s.
Then the `ws_supervisor.hpp` host-test extraction still open from the previous entry — this
session produced two bugs in the same arithmetic and both would have been caught on the desk by it
— then Stage D.

### 2026-08-09 · Opus 5 · the recovery decomposed; and the RX watchdog is now greying a healthy feed

**Boot immunity confirmed.** `connects=1` and `sock_gaps=0` across the whole pre-outage run, and
the first real outage restarts as `attempt #2` — so the boot connection is attempt #1 and nothing
cut through it. The previous entry's bug is closed on evidence.

**The real socket outage at 18:22:27 recovered in 9451 ms**, against the 18609 ms baseline, and
this time the log carries enough to take it apart. Two independent clocks in the same lines do it:
the restart printed at Arduino-millis `103561`, the socket-up at `110124` — 6563 ms between them —
while the message itself reports only **4018 ms** elapsed since `attempt_started_us_`. The stamp is
taken *after* `esp_websocket_client_stop()` returns, so the missing 2545 ms is the teardown:

| term | ms | whose |
| --- | --- | --- |
| backoff + 250 ms poll granularity | 2445 | ours, tunable |
| `esp_websocket_client_stop()` blocking | ~2545 | the library's, and pure waste on an already-dead socket |
| DNS + TCP + TLS + upgrade | 4018 | the network's |
| socket up → snapshot → LIVE | ~435 | Anvil's |
| **total** | **9451** | matches `grey for 9451 ms` |

**The assumption that framed this whole task was wrong, and in a useful direction.** The working
theory was that Anvil's snapshot latency dominated the tail — it is **435 ms**. The server is not
the slow part. The two big terms are a 4 s TLS connect and a **2.5 s blocking `stop()` on
loopTask**, which was invisible before this run and is worth knowing independently of reconnects:
`loop()` is stalled for it, and at Stage D that is Core 1 with a panel on it.

**Ranked levers on the remaining 9.45 s**, none taken here — the task was the backoff and it is
delivered:

1. **4018 ms connect.** The largest single term. Nothing in this file reaches it.
2. **~2545 ms `stop()`.** Only paid because we stop a client that is already dead. Skipping it
   needs a way to know the task is gone, which this vintage does not expose; the honest options
   are to measure whether `start()` alone succeeds after an abort, or to accept it.
3. **2445 ms backoff.** Cheapest to move and the least worth moving — it is already the smallest
   of the three, and halving it buys ~1.2 s against a 4 s connect.

`kObservedRecoveryUs` stays at the conservative end-to-end 6136 ms rather than being tuned to the
4018 ms it now formally bounds: one sample, and being wrong downward gives a supervisor that kills
connections a beat before they succeed, which is the failure mode that cannot self-correct. Real
margin on the budget today is 7000/4018 = **1.74×**, recorded in the header.

**RAISED, NOT FIXED — the RX watchdog is now greying a healthy feed, and this outranks the
reconnect work.** In the first 30 s of this run, with `sock_gaps=0` and `connects=1` and no
interference of any kind, `wd_gaps` reached **2** and `worst_gap` **1027 ms**. Two of the three
greys the owner read as server restarts were not socket events at all: 18:21:35 (grey 1470 ms) and
18:22:13 (grey 144 ms) both left `sock_gaps=0` and `connects=1` — watchdog trips on a live socket.

`kRxWatchdogMs = 1000` is M1's *measured* threshold, not a tuned one: 640 ms worst healthy silence
across 6,494 frames, a 1.6× margin. That premise no longer holds on the deployed server. This run
reads **5.8–6.6 msg/s** where M0 measured 15.5, which the previous entry already flagged as the
open question — this run answers it: the gaps have grown past the threshold, so the panel now
greys on healthy data. That is invariant #5's lie in the opposite direction, telling the desk the
book is untrusted when it is not, and at roughly one false grey per 15–20 s it will be the
dominant visual defect once Stage D puts it on a panel.

**Not changed here, deliberately.** `ReplayOptions::disconnect_gap_ms` is the identical constant on
the host and the M1 goldens are pinned to it, so moving it moves committed expectations — an
ARCHITECTURE-weight decision and the owner's call, not a session's. What it needs first is the
paired capture the 2026-08-09 entry already asked for: run `tools/capture_anvil.py` from the desk
alongside the board, replay it, and re-derive the worst healthy gap the way M1 did. If the host
agrees at ~6/s the threshold should be re-derived from the new measurement rather than nudged.

**State of the tree.** Firmware builds clean, RAM 40.7%, flash 12.7%. Host suite unaffected.

**Exact next step:** the paired capture above, to re-derive `kRxWatchdogMs` against a current
measurement — this is now the highest-value item in the milestone. Then the `ws_supervisor.hpp`
host-test extraction still open from two entries back. Then Stage D.

### 2026-08-09 · Opus 5 · the watchdog was measured, the premise was wrong, and nothing moved

**The task was to re-derive `kRxWatchdogMs` from Anvil's current cadence. The measurement
refused the premise, so the constant stayed at 1000 ms** — owner consulted mid-session and
agreed. The brief pre-sanctioned a golden move as the one exception to "a moved golden means
stop"; that sanction rested on "the deployed server now runs ~6 msg/s, healthy silences have
stretched", and it does not.

**What Anvil actually does, from a 20-minute desk capture (20,418 frames, one connection):**
**17.02 frames/s, worst inter-frame gap 391 ms.** Faster and steadier than M0's 15.5 /s and
640 ms. Per-kind: `book` 13.44/s, `summary` 2.00/s, `trade` 1.58/s — the 2 Hz timer and the
event-driven tape are exactly where M0 left them. Distribution (nearest-rank, `tools/gap_stats.py`),
all three counting rules agreeing on the worst gap as they did at M0:

| counting rule | n | rate/s | p50 | p90 | p99 | p99.9 | max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| any frame (host driver) | 20,418 | 17.02 | 63 | 78 | 141 | 172 | **391** |
| event-producing | 18,017 | 15.02 | 63 | 79 | 141 | 172 | **391** |
| book-affecting (firmware's rule) | 16,124 | 13.44 | 78 | 79 | 141 | 188 | **391** |

So the brief's own method, applied to the data the brief asked for, moves the threshold
**down** — p99.9 × margin lands near 500 ms, 1.6× over the max lands at 626 ms — which would
have made the false greys worse. That is the whole reason this session's output is a
measurement and not a patch.

**The board's ~6 msg/s is real and is backpressure, not cadence.** `tools/anvil_drain_probe.py`
opens the same socket and sleeps after each message, run against a *simultaneous* unthrottled
capture that stayed flat at 16.9 msg/s throughout — one server, one instant, two drain speeds:

| drain delay | rate | throughput | gaps p50 | gaps max |
| ---: | ---: | ---: | ---: | ---: |
| 0 ms | 16.95 /s | 106.5 KB/s | 63 ms | 156 ms |
| 120 ms | **8.32 /s** | 54.4 KB/s | 125 ms | 125 ms |
| 250 ms | **4.01 /s** | 26.9 KB/s | 250 ms | 266 ms |

**Anvil sheds to a slow consumer, and sheds *evenly*** — 4× fewer messages, gaps unchanged,
because `book` frames are coalesced per socket so a backed-up socket gets the newest book
rather than a queue of stale ones. This reconciles the bench numbers exactly: hold `summary`
at 2.00/s and `trade` at 1.58/s and solve run A's 8.59 msg/s at 42.70 KB/s for `book` → 5.14
book/s, 8.72 msg/s, mean 5,012 B, against the board's reported 8.59 msg/s and 5,148 B. Inside
1.5%. New venue behaviour, not in the vendored protocol, and a note for M4/M5: **a thinned
Anvil stream is not a broken one.**

**The control that settles it.** Throttle the desk to the board's own message rate (8.30 vs
8.59 msg/s) and measure with the board's own book-event rule, four minutes: worst silence
**594 ms**, zero watchdog trips. The board shows **2,461 ms** and five trips in 90 s. Anvil
delivers a thinned stream evenly; the multi-second holes are board-side. **Raising the
threshold would have hidden a real 1–2.5 s freeze of the feed pipeline, reading Live — the
exact output invariant #5 exists to forbid.** The watchdog is not crying wolf; it is the only
instrument that noticed.

**Both costs, recorded as the brief asked** — for the threshold that was kept, not one that
was chosen. False-grey headroom: 1000 − 391 = **609 ms** against Anvil at full rate, and
1000 − 594 = **406 ms** (1.68×) against Anvil at the board's rate, which is the binding one.
Silent half-open detection latency: **1000 ms**, unchanged; every clean drop still surfaces as
a socket error in under 1 s, so the watchdog only ever owned silent half-opens. The 4,468 ms
reconnect gap keeps its full 3,468 ms of grey and the invariant-5 proof is untouched.

**Delivered.** Fresh 90 s slice committed (`anvil_101_baseline_20260809.ndjson`, 1,513 frames,
190 KiB gzipped) *beside* the M0 trace rather than replacing it, since the M1 goldens pin the
M0 one. Two new tools in `tools/`: `gap_stats.py` (distribution + "what would threshold T do
to this trace", validated by reproducing the existing reconnect golden's 4,468/3,468 ms and
frames 382→383 exactly) and `anvil_drain_probe.py`. A new golden pins the finding —
`test_replay_goldens.cpp`, "2026-08 capture: Anvil's cadence still clears the watchdog by 6x"
— so a *real* future cadence change goes red on the desk instead of grey on the panel, which
is the guard the M1 derivation never had. ARCHITECTURE §9 amended; DESIGN.html §05 rewritten,
strain 10 updated (the two "feed has stopped" rules are golden-neutral on captures but
separate 4× on a throttled stream, so M4's alignment now owns a throttled trace too), strain
**12 opened** — the board stalls for seconds and only the panel can see it.

**Measurement caveat, recorded rather than buried.** This capture's `rx_ns` came from a
Windows `time.monotonic_ns()` at the default 15.625 ms timer resolution — the whole capture
holds only 35 distinct gap values, all on that grid. Every figure above is ±16 ms, which is
nothing at 391 vs 1000 ms but does mean p50 = 63 ms is "~60–70 ms, unchanged from M0" and not
a real shift. M0's capture had sub-ms resolution; capture from WSL next time.

**Two ways the comparison could still be wrong**, both stated in NOTES.md: the desk's default
route is wired Ethernet where the board is on Wi-Fi (same gateway, same WAN path), and the
desk capture ran ~1.4 h after the bench run rather than alongside it. Neither produces
*selective* multi-second silence in a TCP stream a sender is filling evenly — loss is
retransmitted, not skipped — but the board can close both alone by printing a distribution
instead of a high-water mark.

**State of the tree.** `cmake --workflow --preset host-mingw` green: 11/11 ctest (two new
replay registrations for the new trace), 99/99 doctest in `dc_tests` (9,126 assertions),
45/45 in `dc_tests_streaming` (704). No firmware change, so no reflash needed and the bench
confirm the brief asked for does not apply — `wd_gaps` will not drop to ~0 until the stall is
found, and that is the point.

**Exact next step (superseded by the next entry): the stall, before Stage D.** Add a bucketed gap histogram plus
arrival-vs-event counters to `FeedTask::Stats` (`worst_gap` is a high-water mark with no
distribution behind it, so the board can say it saw 2,461 ms but not how often, nor whether
the hole was in arrival or in decode), then one bench run. Candidates in the order the
evidence favours them: the 2.5 s blocking `esp_websocket_client_stop()` already measured on
loopTask; `esp_event` dispatch backing up behind three DATA events per book frame at a 4 KiB
RX buffer; feed-task starvation on Core 0, which also hosts the Wi-Fi and lwIP tasks at higher
priority. The `ws_supervisor.hpp` host-test extraction is still open behind it, and the
histogram work touches the same file, so they may merge. Then Stage D.

### 2026-08-09 · Opus 5 · the stall is instrumented; the verdict is one bench run away

**Delivered: the instrument, not the diagnosis, and the split between those two is the honest
state of this entry.** Everything that can be built and proven on the desk is done and green;
the measurement it exists to take needs the board flashed for ten minutes and is the owner's.
Nothing below has run on hardware.

**What the board now prints, every 10 s.** Three lines beside the existing block (illustrative
shape, not measured):

```text
-- arrive : <100:1180 100-250:88 250-500:9 500-1k:1 1-1.5k:0 1.5-2.5k:0 >2.5k:0 | n=1278 worst=612 ms >1s=0 mode=-
-- event  : <100:1100 100-250:96 250-500:12 500-1k:3 1-1.5k:5 1.5-2.5k:2 >2.5k:0 | n=1218 worst=2461 ms >1s=7 mode=1-1.5k
-- a->e   : <1:900 1-5:210 5-25:104 25-100:3 100-500:1 0.5-1k:0 >1k:0 | n=1218 worst=88 ms | qwait=41230 us behind=2/6 msgs_in=1279
```

`arrive` is inter-arrival gaps between whole messages, stamped inside the WebSocket client's
callback; `event` is the identical quantity the RX watchdog greys on, bucketed; `a->e` is one
message's arrival to the book having moved, with `qwait` splitting off the part spent waiting
to be scheduled at all. The 1000 ms bucket edge **is** `kRxWatchdogMs`, so `>1s` is literally a
count of the occasions the panel had grounds to grey — the number `worst_gap` could never give.

**The one design decision worth arguing with: a message dropped for want of a slot is counted
as an arrival anyway.** It is one line of ordering in `FrameReassembler::finish()` and it is
the difference between an instrument and a misleading one. Slot exhaustion is *caused* by the
feed task being slow, so an arrival series recording only the messages the board managed to
keep would print feed-side starvation as a hole in the *network* — the exact misreading the
split exists to prevent. Pinned by a host test ("a message dropped for want of a slot still
counts as an arrival"), because it is invisible on inspection and reads as a bug.

**Both histograms have exactly one writer**, which is what lets them be lock-free 32-bit
counters read from Core 1 with no synchronisation. `arrival_gaps` lives in `FramePipeStats`
and is written only on the WebSocket client's task, which already owns every other counter in
that struct; `event_gaps`, `arrival_to_event`, `worst_queue_wait_us` and `max_ready_backlog`
live in `FeedTask::Stats` and are written only on Core 0. A stale read costs one wrong log
line; nothing branches on them and no test gates on them, as the brief required.

**Host-first, and it earned its keep immediately.** `gap_histogram.hpp` is ESP-IDF-free like
`frame_reassembler.hpp`, so the bucketing is `dc_tests`' problem rather than the bench's — the
failure mode here is not a crash but a plausible distribution with a label one column out of
step, which would send an evening after the wrong candidate and could otherwise only be caught
by measuring the same thing twice. 18 new test cases; `dc_tests` 99 → **117 cases**, 9,126 →
**9,780 assertions**, ctest **11/11**. The first draft of the truncation test asserted a
property `render` never promised — that the last byte of the buffer is untouched, when
`snprintf` is entitled to put the NUL there — and it went red rather than being reasoned
about; it is now a guard-region test that checks the real property.

**Wi-Fi power save: the brief's expectation was right about Arduino and wrong about this
firmware, so the experiment changes shape.** Arduino-ESP32 2.0.14 does default modem sleep on
for the S3 (`_sleepEnabled = WIFI_PS_MIN_MODEM`; only the S2 is exempt) — but
`WsTransport::connect_wifi()` has called `WiFi.setSleep(false)` since the stage C draft, and
calls it *after* `WiFi.mode()`, which the shipped `WiFiGeneric.cpp` says is the order that
makes it take effect (`setSleep` forwards to `esp_wifi_set_ps` only when the mode already has
`WIFI_MODE_STA`, and the Arduino event handler re-applies the cached value on `STA_START`, so
association cannot undo it either). The arms are therefore not "off vs default" but **"off vs
deliberately on"**, which answers the same fork from the other side and needs no assumption
about which call wins. Both are buildable — `pio run -e depthcharge` and `-e depthcharge-ps`
(`-D DC_WIFI_POWER_SAVE=1`) — and the mode is read back out of the driver with
`esp_wifi_get_ps()` rather than echoed from what we asked for, so the log line cannot agree
with the code by construction. **Verified the two arms really are two builds** (the `.bin`
hashes differ) rather than trusting the `${env.build_flags}` interpolation.

**Cost, measured by building the tree with and without the change rather than estimated.**
RAM 133,512 → **133,640 B** (+128, 40.7% → 40.8%); flash 834,033 → **835,793 B** (+1,760,
12.7% → 12.8%). The 128 bytes are exactly what the structs say: 40 in `FramePipeStats` (one
histogram plus `messages_arrived`), 80 in `FeedTask::Stats` (two histograms plus two scalars),
8 for `last_arrival_us_`. `FeedMessage` grew 8 bytes for the arrival stamp, which is 48 bytes
more *heap* in the ready queue, allocated once in `begin()` — not steady state, so invariant
#7 is untouched. Per sample the instrument costs six comparisons and two increments.

**Review found five things and all five are fixed.** A saturating-clamp idiom written twice
(now `clamp_us_to_u32`, shared and tested — what it guards against is a 71-minute gap reported
as a few milliseconds, the one direction a stall instrument must never be wrong in); the ready
queue depth spelled `kFrameSlots + 2` in two files (now `kReadyQueueDepth`, so the console
cannot print a fraction of the wrong denominator); two near-identical histogram print blocks
(now one helper, and both gap lines gained `mode=` as a result, which is the better output
anyway — reading them is a comparison, and a comparison between differently-shaped lines is
one someone gets wrong at 2 a.m.); a claim that `>1s` should equal `wd_gaps`, when it is
`wd_gaps + sock_gaps` because a hole is recorded when the next event lands whatever ended it
(they agree only on the `sock_gaps=0` run this instrument was built for, now stated in both
the header and the README); and `backlog` described as the queue depth when it is sampled
*after* the dequeue and so tops out one below it (renamed `behind`). The random-walk property
test also gained the two arrival invariants: every published message has a matching arrival
(`arrivals` is a superset, never the reverse), and the arrival timeline never runs backwards.

**NOT DONE — the three deliverables that need the board, and they are the point.** No bench
run, so no >1 s bucket count for either power-save arm, no steady-state distribution, and
**no verdict**. Strain 12 is updated to say *instrumented, not diagnosed*, and the candidate
list is deliberately left in its prior order: re-ranking it without data is precisely the
thing this task exists to stop. `firmware/README.md` carries the procedure, the verdict table,
and the warning that `pio run -t upload` with no `-e` uploads **both** environments and the
second one wins.

**What the run must record**, so the next session ranks candidates rather than argues:

1. **Baseline**, `-e depthcharge`, at least 10 minutes, no deliberate disconnects. Confirm
   `power-save requested=OFF driver ps=0` at association, and `sock_gaps=0 connects=1` at the
   end — the second is what makes the distribution steady-state rather than a mixture.
2. **Contrast**, `-e depthcharge-ps`, same duration and conditions, `driver ps=1` confirmed.
3. For each arm: the `>1s` count from **both** `arrive` and `event`, with `n` and the window
   length so they are rates and not raw counts; `mode=`; `qwait`; `behind`; `worst_frame`.
4. **The absence of a >2.5 s sample on a `connects=1` run is itself a result** — it says the
   2,461 ms outlier was the reconnect's blocking `stop()` and the steady population is the
   1–1.5 s one, which is a different bug with a different fix.
5. Commit the log as `hardware/bench-YYYY-MM-DD-feed-stall.md`, paired with a simultaneous
   `tools/capture_anvil.py` run from the desk as the control for "was the wire quiet".

**State of the tree.** `cmake --workflow --preset host-mingw` green: **11/11 ctest**, 117/117
doctest in `dc_tests` (9,780 assertions), 45/45 in `dc_tests_streaming` (704). Both firmware
environments build clean, RAM 40.8%, flash 12.8%. No engine, book, `DisplaySnapshot` or golden
change, as the brief required. DESIGN §05 gained the arrival-vs-event split and its verdict
table; strain 12 is half-closed. No ARCHITECTURE §9 amendment: this is instrumentation behind
existing boundaries, and the only rule in it with any reach — count the arrival before
resolving ownership — is recorded where it is implemented and tested rather than in the
constitution.

**Exact next step: the two bench runs above, then the fix the data chooses** — and the fix is
genuinely not chosen yet, which is the whole reason this session stopped here. The
`ws_supervisor.hpp` host-test extraction is still open behind it, unchanged by this work
(which touched `ws_transport.cpp` only for the arrival stamp and the power-save readback); if
the data indicts the blocking `stop()`, that extraction and the fix become the same task. Then
Stage D.

### 2026-08-10 · Opus 5 · the split could not decide it; the verdict moves outside the data path

**The two runs happened and the instrument built for them came back undecided.** Both arms held
`connects=1` and `sock_gaps=0` across a whole run that still logged **17 book-holes over 11 min**
(power save off, worst 2,461 ms) and **25 over 10.5 min** (power save on, worst 1,893 ms), with
`arrive` and `event` filling their >1 s buckets **together**. Two results fall straight out of
that and are now closed: **modem sleep is not the mechanism** (it makes it worse, not different —
both arms measured, `driver ps` read back out of the driver), and the socket is not involved.
What does *not* fall out is the fork the next brief depends on, and the reason is worth more than
the measurement.

**The old verdict table's first row was an over-claim, and this is the correction.** "Both fill →
transport" rests on `arrive` being the wire. It is not. The stamp is taken in
`WsTransport::on_event`, on `esp_websocket_client`'s own task, so it already sits **downstream of
the Wi-Fi driver, lwIP, the socket read, the TLS record decrypt and the `esp_event` dispatch
hop** — the split's real boundary is the FramePipe queue hop, not the antenna, and an arrival-side
hole is as consistent with a Core 0 that cannot keep up as with a dry socket. The brief asked for
this to be stated precisely before the new signals were read against it; it is now stated in
`frame_pipe.hpp`, `serial_console.cpp`, `firmware/README.md` and DESIGN §05, with the offending
row struck through rather than quietly rewritten.

**The second entanglement is upstream and not ours.** Anvil's per-socket coalescing *sheds* `book`
frames whenever a socket backs up (2026-08-09: a client throttled to a quarter rate receives 4×
fewer messages and is never silent longer than one drain interval). Shedding is therefore a
**symptom** of the board falling behind, not an independent cause, and "Anvil shed it" and "Wi-Fi
delayed it" are indistinguishable from anywhere inside the data path. No amount of instrumenting
that path separates them — which is why this session's output is a different *kind* of signal
rather than a finer version of the same one.

**Delivered: three signals from outside the feed path, and the classification that reads them.**

1. **Per-core idle across exactly the window that greyed the panel** — the verdict. Core 0 far
   below its own healthy baseline means it could not drain the stream (board-bound, firmware's to
   fix); at or above it means it was waiting on data that was not there (link-bound, and no lever
   in this repo reaches it).
2. **Recovery shape** — the inter-arrival of the next four messages plus the wire-`seq` step,
   separating "the frames existed and were delivered late" from "the middle was never sent to us".
3. **rssi through the run**, sampled every 500 ms on loopTask, with the value at each hole
   attached to that hole's record.

`firmware/src/stall_probe.hpp` is the arithmetic and is ESP-IDF-free and host-tested;
`core_idle.*` is the one platform half. Costs, measured by building with and without: RAM 133,640
→ **134,280 B** (+640, 40.8% → 41.0%), flash 835,793 → **840,025 B** (+4,232, 12.8%). Still
allocation-free — `nm -u` on `feed_task.cpp.o` shows no `operator new`, no `malloc`, no
`snprintf`, and no float instruction in the disassembly. The feed path did gain `__divdi3` /
`__udivdi3`: three soft 64-bit divides per event (~40/s) against the ~3,000/s the parser's qty
scaling already does, so it is recorded rather than optimised, with the reason the obvious
cheaper version is wrong written where it would be tried.

**`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS is not set`, which is the fourth time this vintage has
decided a design here** (after `esp_crt_bundle`, `heap_trace_start` and `reconnect_timeout_ms`).
The brief named it directly. It is a compile-time switch that changes the TCB layout and is baked
into the precompiled `libfreertos.a`, so defining it in our own flags would not enable it — it
would make our translation units disagree with the archive about the shape of a task. Read out of
the shipped `sdkconfig`, alongside `CONFIG_FREERTOS_USE_TRACE_FACILITY is not set`.

**So idle is measured directly, and the mechanism was verified in the shipped archives rather than
from the docs.** Disassembling `esp_vApplicationIdleHook` in `qio_opi/libesp_system.a` shows the
loop over eight hook slots, a `moveqz` clearing `can_go_idle` for any hook that returns zero, and
`esp_pm_impl_waiti` called **only** if that flag survives; `prvIdleTask` in `libfreertos.a` calls
the hook every pass. A hook that returns `false` is therefore called in a tight loop for as long as
its core has nothing to run, and differencing the core's own cycle counter across those calls is
idle time in microseconds — no calibration constant, no statistical sampling. The generated hook
is 30 instructions: `rsr.prid`, `rsr.ccount`, hardware `quou`/`remu`, no call, no flash access.

**The perturbation is real and is stated rather than buried.** Returning `false` suppresses
`waiti`, so an idle core spins. Acceptable here — `CONFIG_PM_ENABLE` is not set so there is no
light sleep or frequency scaling to disturb, the idle task is priority 0 so it cannot delay real
work, modem sleep is a radio state and is untouched, and the task watchdog is fed more often
rather than less. It is reversible with `-D DC_IDLE_PROBE=0`, which compiles the probe out and
reports every idle figure as unknown, so a suspicious result can be re-run without the instrument.
**Whether it ships past this characterisation is a stage D decision and is not settled here.**

**The decision to argue with: a failed probe reports `unknown`, never zero.** A probe that could
not register would otherwise report 0 µs of idle, which classifies *every* hole in the run as
board-bound and reads as an unusually decisive result — the single most expensive way this
instrument could be wrong. `idle_valid` rides on every sample and on every record, the console
prints `idle probe NOT RUNNING` as a warning, and the README makes a missing `per-core idle probe
up` line a stop-the-run condition. Host-tested from both sides.

**Two other choices worth recording.** The hole threshold is taken *from* `GapScale`'s >1 s bucket
edge rather than restated, with a `static_assert` tying it to `kRxWatchdogMs` — so `holes n=` and
`event >1s=` are two independent instruments counting the same events, and the bench can check one
against the other. And a message is offered as a recovery sample **before** it is allowed to open
a hole of its own; reversed, every hole would record a first recovery gap equal to itself and every
recovery would classify as a resumed cadence. Both are pinned by tests, because both are invisible
on inspection.

**The engine diff is one read-only accessor**, `AnvilAdapter::last_wire_seq()`, exposing state the
adapter already keeps for `wire_seq_backward`. It exists because Anvil's global seq is unusable for
ordering but its *rate* is a clock: a frame ending a hole with a step of about one is an old frame
that was sitting somewhere, and one with a step of roughly rate × hole is a fresh frame whose
predecessors were never sent to us. That is the shed-versus-delayed discriminator, and the board
prints the step beside the prediction so it is checkable rather than asserted. No behaviour
changed; no golden moved.

**Two real defects, both found by tests rather than by the bench, and neither would have failed
loudly.**

1. The "what a fresh frame would have carried" prediction folded the hole's own second into the
   rate it was predicting from, so a genuinely stale hole looked *less* stale the longer it was —
   backwards, and exactly the kind of thing that gets believed on a log line. The rate is now taken
   as it stood before the sample.
2. **The hole ring handed out a record that was being overwritten.** With eight slots the retained
   window was eight, but one slot always holds the record still gathering its recovery series — so
   the moment a new hole opened, the "oldest retained" index pointed at it and the console could
   print an unclassified record as the oldest finalised one. The log retains seven now. Found by
   the property test added during review (a random walk asserting the tally stays closed: verdict
   buckets sum to the finalised count, at most one hole open at a time, ordinals dense across the
   retained window), which is the argument for having written it.

**The review also collapsed three copies of one idiom.** The truncating snprintf-append existed
in `Histogram::render`, in `render_tally` and in `render_hole` — the same three lines whose failure
mode is not a crash but a log line silently one field short, in the one place a bench session is
reading for a verdict. It is now `append_truncating` in `gap_histogram.hpp`, used by all three and
covered by both files' guard-region tests. Likewise the percentage helper existed twice (32- and
64-bit) and is now one function, and `IdleAccumulator::pass` became `note_pass` to match
`note_event` / `note_message` / `note`.

**State of the tree.** `cmake --workflow --preset host-mingw` green: **11/11 ctest**, `dc_tests`
117 → **144 cases** (16,084 assertions), `dc_tests_streaming` 45/45 unchanged. Both firmware
environments build clean. No engine behaviour change, no golden moved, `kRxWatchdogMs` still
1000 ms, and no board-side lever pulled — applying a fix before the split says "board-bound" is the
trap this milestone has already paid for three times. No ARCHITECTURE §9 amendment: nothing has
been decided yet, and the reasoning that has reach (where the arrival stamp really sits) is
recorded where it is implemented and in DESIGN §05, following the precedent of the previous entry.

**NOT DONE — the run and the verdict, which are the point.** No bench run, so no idle figure, no
classified tally, no verdict, and the candidate list is deliberately left in its prior order.

**Exact next step: one ≥10-minute `-e depthcharge` run, steady state, `connects=1`** (protocol and
the read-in-this-order checklist in `firmware/README.md`). It picks the next brief outright:

- **board-bound** → the lever bundle, each re-benched against this instrument with near-zero
  steady-state >1 s holes as the bar: `esp_websocket_client buffer_size`; the LWIP receive window —
  worth noting that this framework ships `CONFIG_LWIP_TCP_WND_DEFAULT=5744`, **smaller than one
  8.7 KB Anvil book frame**, so a burst cannot be absorbed without TCP-backpressuring Anvil into
  shedding; and WS-task priority/core.
- **link-bound** → out of firmware entirely: 2.4 GHz channel scan, placement, rssi-vs-hole
  correlation. Anvil's incremental-L2 feed stays on the Anvil backlog for M4/M5.

Then the `ws_supervisor.hpp` host-test extraction, still open from three entries back and untouched
by this work. Then Stage D.

### 2026-08-10 · Opus 5 · the connect-time reject burst is instrumented; the fix is deliberately not chosen

**The task was "log the first ~10 rejected payloads on connect, then suppress or skip-not-error
it, and document the burst in the vendored protocol". Only the first of those three is
delivered, and the other two are gated on it rather than deferred out of laziness** — the
three things `parse_errors` could be have three different owners, and picking one before the
payload is read is the trap this milestone has already paid for three times (the watchdog
constant, the reconnect arming, the arrival-stamp over-claim).

**The finding, restated so the numbers travel.** ~1,281 frames rejected in the first ~60 s of
each connect, ~85% of the opening burst, then flat, with a smaller batch per reconnect;
`price=0 ticker=0 unknown=0 trunc=0`, so every one of them is `parse_errors`. Nothing about
the ladder is at risk — Anvil's book frames are idempotent full replaces and the adapter
counts-and-drops by design — but under invariant #5 a transport delivering frames the parser
rejects is *stopped*, so this is the exact shape the event-armed watchdog exists for, and the
board is doing it to itself for a minute on every connect.

**What the desk already says, and it narrows the search before any bench time is spent.** The
committed `anvil_101_baseline_20260809.ndjson` **opens at a connect** — first frame line is the
on-connect `snapshot`, `summary` immediately after — runs 1,513 frames over 90 s, and
`test_replay_goldens.cpp` pins `parse_errors == 0` across it. That covers the same window the
board is failing in, so **Anvil sends nothing at connect that this parser rejects** and the
first suspect moves from the venue to the client. Two corollaries worth having in writing:
`no_slot` cannot be the mechanism (a message dropped for want of a slot never reaches the
parser, so it cannot become a parse error), and neither can `summary` (counted separately, and
zero here).

**The one hypothesis a capture is structurally blind to, which is why this is not closed.**
`tools/capture_anvil.py` writes one line per *message* and the `websockets` library reassembles
WebSocket-level fragmentation before handing it over — so a message Anvil split at the WS layer
is written as one whole line and **cannot appear in a trace at all**. That is not an idle
worry: `frame_reassembler.hpp` already documents itself as publishing such a message
*incomplete*, because this IDF vintage does not surface the FIN bit, and it names the parse
error that follows as the correct failure. Every capture we own is blind to the one candidate
the firmware has already written down. Recorded in `harness/replay/NOTES.md`, with the
consequence: **if it is server fragmentation, no trace will ever reproduce it and the honest
coverage is a synthesised `FrameReassembler` test, not a capture.**

**Delivered: `firmware/src/reject_log.hpp`**, ESP-IDF-free and host-tested like
`frame_reassembler.hpp` / `gap_histogram.hpp` / `stall_probe.hpp`, capturing the first ten
rejected payloads **of each connect** (a once-only budget would have hidden the reconnect
batches, which are the samples that say whether it is the same shape). Every field is there to
separate the three candidates and nothing is there because it was cheap:

| field | what it decides |
| --- | --- |
| `status` | `NotJson` = the bytes, ours · `MissingType` = a frame shape, the venue's · `BadShape` = a payload change, a re-vendor |
| `len` | the **whole** payload. A multiple of the 4,096 B RX buffer names a chunk-boundary truncation |
| `head` | `{"type":"..."` — the frame kind, which is the question the bench could not answer |
| `tail` | ends `}` = whole and rejected on its contents; ends mid-token = cut off |
| `SPLIT@n` | a second `{"type":` inside one buffer: two messages in one slot |

`SPLIT@` is the one that had to be computed rather than eyeballed — a spliced buffer *begins*
like a valid frame and *ends* like a valid frame, so head and tail both look right and the
reject reads as inexplicable. Safe to search for because no Anvil frame contains `{"type":`
anywhere but at byte 0 (no nested object in this protocol has a `type` member), which the test
pins against a `summary` frame full of nested objects.

**The engine diff is one read-only accessor**, `AnvilAdapter::last_status()`, exposing the
`ParseStatus` the adapter already computes on every frame — same shape and same justification
as `last_wire_seq()` last session. It exists because `parse_errors` is a bucket, not a
diagnosis, and diffing three counters across the call to recover one enum is worse code than
one comparison. No behaviour changed and no golden moved. Its coverage is driven off the
**existing** `kBadFrames` corpus, which already declares the expected status per case, so the
two cannot drift — and because that file compiles into both test binaries, the accessor is
proven against both parsers rather than one.

**Two real defects in my own code, both found by tests rather than by reading, and the second
is the argument for having written the property test at all.**

1. The first draft sized the head and tail constants as *buffer* sizes, so `copy_printable`
   reserved a byte for the NUL and the tail held 47 characters ending one byte before the
   payload's end — throwing away exactly the closing `}` that is the whole reason a tail is
   captured. The constants now mean characters and the storage is one larger.
2. **A null payload with a non-zero length faulted.** The tail is copied from
   `payload + (len - want)`, and `copy_printable`'s own null check cannot save that: by the
   time the pointer arrives it has been offset and is no longer null, so it reads an address
   computed from nothing. The head and the header search were safe because neither offsets
   before testing. It cannot come out of the real pipeline (the reassembler never publishes a
   null slot) and it is guarded anyway, because this reads a network buffer and the failure is
   a fault rather than a wrong log line. Found by the degenerate-input case added during
   review, which is precisely the case an example-based test of the happy path never reaches.

Mutation-verified, all four properties that are invisible on inspection: the tail off-by-one,
the second-header search starting at 0 instead of 1 (which makes it return "none" for every
payload, because the payload's own header matches first), the null guard, and the retained
window using the full ring depth instead of depth − 1. Each turns `dc_tests` red.

**The review's one open finding, deferred with the fix sketched rather than dropped.**
`StallProbe` and `RejectLog` now contain the same ~8 lines twice: a monotonic count over a
fixed ring, a retained window of depth − 1, and an `at(index)` that returns null once evicted.
The console likewise drains them with two near-identical twelve-line loops. That is real
duplication and its failure mode is not cosmetic — handing out a record that is being
overwritten is the exact bug the stall probe's property test caught on 2026-08-10, and the
second copy has now inherited the same reasoning by hand. The extraction that fits both is an
append-only ring whose newest slot may be *uncommitted* (`open()` / `commit()` / `at()` /
`oldest_retained()` / `completed()`): the stall probe needs the open slot for the record still
gathering its recovery series, and the reject log commits immediately. **Not done here**
because it means reworking an instrument that landed yesterday, in a tree a second session is
writing to, for a defect that is one bench run from being understood — mixing a refactor into a
diagnostic addition is how the signal from that run gets muddied. It is the right first task
for whoever adds a *third* log of this shape, and at that point it is not optional.

**Deliberately not done, and each for a reason rather than for time.**

- *No fix.* `MissingType` on a whole payload wants skip-not-error and a protocol note;
  `NotJson` at a chunk boundary wants the lever bundle strain 12 already names. Opposite
  changes; one bench line chooses.
- *No `parse_errors` sub-counters in `AnvilAdapter::Stats`.* The per-status tally lives in the
  firmware's reject log, where the question actually arises. A trace's frames all parsed once
  by definition of having been captured, so the host would carry three permanently-zero
  counters and a wider printed report for nothing.
- *No `chunks`-per-message on `FeedMessage`.* It would have made "did this arrive in one DATA
  event or three" certain rather than inferred, but `len` against the 4,096 B buffer already
  answers it, and the cost was a signature change to `FrameReassembler`'s slot contract — the
  most carefully tested file in `firmware/`, for a fact its own test already implies.
- *`docs/vendor/anvil-protocol.md` records the desk control, not the burst.* The file's rule is
  "do not edit the body", so the note is in the header block with the M0 finding; and writing
  the burst down as venue behaviour before knowing it is the venue would be inventing a
  protocol fact. What is recorded is what is measured: the handshake sequence confirmed, the
  capture showing no unparseable frame at connect, and the fragmentation hypothesis named as
  the one thing a capture cannot exclude. The 2026-08-09 per-socket shedding finding was also
  written into that header, where it had been missing.

**Measured, not estimated.** `RejectLog` is **2,168 B** of `.bss` and `RejectRecord` **176 B**,
read out of the object with `nm` after compiling the header standalone with the target
toolchain (xtensa GCC 8.4, `-Os -fno-exceptions -fno-rtti -Werror`) — so it clears the same bar
`dc_engine_target_check` applies to `engine/`. Firmware RAM 134,280 → **136,456 B** (41.0% →
41.6%), which is the 2,168 plus 8 of alignment and therefore all of it; flash 840,025 →
842,889 B, and that figure is **not** separable, because a parallel session's in-flight
`ws_transport` rework is in the same tree. Still allocation-free: fixed storage, and the reject
path is the only path that touches it.

**State of the tree.** `cmake --workflow --preset host-mingw` green: **11/11 ctest**, `dc_tests`
144 → **169 cases** (66,720 assertions), `dc_tests_streaming` 45 → **46**. Fifteen of those
cases are this change (fourteen in `test_reject_log.cpp`, one in `test_anvil_adapter.cpp`); the
rest are the parallel `ws_supervisor` work sharing the same binary, so read the totals with
that in mind. The assertion count is dominated by the 5,000-step random walk, which follows
the reassembler's precedent. Firmware builds clean at RAM 41.6% / flash 12.9%. No engine
behaviour change, no golden moved, no `DisplaySnapshot`/`FeedEvent` change. No ARCHITECTURE §9 amendment:
nothing has been decided, and the reasoning with reach (a capture cannot see WS-level
fragmentation, so a trace cannot cover this class of bug) is recorded in `NOTES.md` and
DESIGN strain 13 where it is implemented — the precedent the last two entries set.

**Caution for whoever picks this up: the working tree is shared.** `ws_transport.{hpp,cpp}`,
`firmware/src/ws_supervisor.hpp` and `docs/briefs/M3-transport-residual-reconnect-fix.md` are
another session's uncommitted work and were edited during this one; the firmware build was
briefly red on `ws_transport.cpp` mid-session through no change of this entry's. Nothing here
touches those files.

**Exact next step: one bench run, and read the first `-- reject` lines of a connect.** They
pick the fix outright — `no-type` on a whole payload ending `}` is the venue's (skip-not-error
plus a protocol note), `not-json` with a 4096-multiple length or a `SPLIT@` offset is ours
(`buffer_size`, the LWIP window, WS-task priority — the same bundle strain 12 names, which is
worth noticing: if both turn out board-bound under burst, they are one fix). The reading table
is in `firmware/README.md`. Behind it, unchanged: the stall verdict run, the `ws_supervisor.hpp`
host-test extraction, then Stage D.

### 2026-08-10 · owner at the desk, written up by Opus 5 · the stall was the mesh; strain 12 closes

**This entry records three things settled at the bench after the previous entry was written,
and it is deliberately the first move of the Stage D sitting** (`docs/briefs/M3-stage-D-the-panel.md`
§0). Two of them close findings this repo still shows as open, and one of them would otherwise
send the next session after a bug that no longer exists. No code changed for this entry.

**1. The steady-state stall is SOLVED, and it was never the watchdog's fault — nor the
firmware's.** Root cause: **Deco mesh mis-association.** The board had attached to a far node at
**−75 dBm**; moving it to the near node put it at **−34 dBm** and the mid-connection stalls
stopped. An 11-minute run on the near node shows **two ~130 ms cadence blips and nothing else** —
against 17 book-holes over 11 minutes on the far node. Band-steering and fast-roaming were
already off on that mesh, so steering was never the mechanism; it was simply a bad link the
board had no reason to leave.

That is the **link-bound** arm of the fork the previous entry set up, and it is the arm with no
lever in this repo. It also retires the whole candidate list the last three entries kept in
priority order — `esp_websocket_client buffer_size`, the 5,744-byte `CONFIG_LWIP_TCP_WND_DEFAULT`,
WS-task priority and core — **none of which should now be pulled**, because none of them was ever
the mechanism and each would have been a change measured against a moving link. Strain 12 is
closed. The instruments that decided it (`gap_histogram.hpp`, `stall_probe.hpp`, `core_idle.*`)
stay: they are what made a two-arm question answerable at all, and Stage D re-reads the same
lines with a panel beside them.

**Therefore the RX watchdog's open finding is closed, and `kRxWatchdogMs` stays at 1000 ms.**
ARCHITECTURE §9's 2026-08-09 entry had already ruled the correct half — Anvil's cadence is
17.02 frames/s with a 391 ms worst gap, the board's multi-second holes were board-side, and
raising the threshold would have hidden a real freeze. The mesh fix names the board-side cause,
so the constant's premise is intact and the honest instrument was right. **Do not re-derive it,
do not nudge it, and do not touch `ReplayOptions::disconnect_gap_ms`** — the goldens are pinned
to it. A grey on a healthy feed from here on is a finding to report, not a constant to change.
Recorded against `hardware/bench-2026-08-09-ws-reconnect.md`'s "Open finding: the RX watchdog
trips on healthy data", which now points here.

**2. The transport residual is decided and shipping.** What remains on a strong, stable
association is occasional socket-drop reconnects — roughly **four per eleven minutes, arriving in
pairs, then minutes of clean running.** The two-handle reconnect fix (`a32ff59`, ARCHITECTURE §9
2026-08-10) takes a drop from **~9.5 s to ~4.7 s of grey**. **That is the whole treatment.** No
further reconnect work is scheduled, and none is wanted: a reconnect grey is invariant #5 telling
the truth, and Stage D's job is to *render* it, not to prevent it. The acceptance run should see
a reconnect grey read ~4.7 s and look identical to any other grey.

**3. Parked — real, low priority, and not to be reopened at Stage D.**

- The **parse-burst reassembler fix**: client-side reassembly truncating large book frames under
  the connect flood (the ~1,281 rejected frames in the first ~60 s of each connect, previous
  entry). The reject log built for it stays in and will print if it recurs.
- The optional **`WIFI_EVENT_STA_DISCONNECTED.reason` logging**. Worth having; not worth an
  evening, and with the mesh cause named it has lost its best customer.

**Exact next step: Stage D**, per `docs/briefs/M3-stage-D-the-panel.md` — the render task on
Core 1, HUB75 with the M2 pin map and FM6124 init, `serial_console.cpp`'s body becoming the
renderer while its three proven properties stay (Core 1, `consume()`-gated redraw, reads nothing
but the `DisplaySnapshot`). The `ws_supervisor.hpp` host-test extraction asked for four entries
back **is done** — it landed with the two-handle fix (`a32ff59`, `harness/tests/test_ws_supervisor.cpp`).

### 2026-08-10 · Opus 5 · Stage D written and host-proven — **not yet flashed**

**Done (CC-side).** The panel exists in the tree. `serial_console.{hpp,cpp}` →
`render_task.{hpp,cpp}` (`git mv`, so the diff reads as a rename), body replaced with the
HUB75 renderer, serial evidence kept intact. New: `ladder_font.hpp`, `ladder_render.hpp`,
`panel.{hpp,cpp}`, and `harness/tests/test_ladder_render.cpp`. `platformio.ini` pins
`mrfaptastic/ESP32 HUB75 LED MATRIX PANEL DMA Display@3.0.15` and adds `-D NO_GFX`. Host loop
green: **11/11 ctest**, `dc_tests` 169 → **188 cases** (66,720 → **558,780 assertions**),
`dc_tests_streaming` 46/46 unchanged. Both firmware environments build clean: RAM 136,456 →
**137,244 B** (41.6% → 41.9%, +788), flash 842,889 → **865,745 B** (12.9% → 13.2%, +22,856).
**Nothing here has run on hardware. Every claim below is static, host-measured, or read out of
the library's source — never observed on a panel.**

**The decision to argue with: the renderer cannot name a colour.** The brief asked for "one
palette selection, taken from `snap.status`, at the top of the draw — and no other code path
picks a colour", and for that to be structural rather than remembered. The obvious
implementation is a rule, and a rule fails the first time someone adds a widget and forgets.
So `ladder_render.hpp` emits `Ink` — a *role* — and `PanelCanvas` holds the `Palette`, chosen
once where it is constructed. Two `static_assert`s do the rest:

```cpp
static_assert(all_grey(kStalePalette), ...);                       // no hue anywhere
static_assert(none_black_except(kStalePalette, Ink::Count), ...);  // grey, never blank
```

A new `Ink` without a stale entry fails the build; a stale entry with a hue fails the build.
`-D NO_GFX` removes Adafruit GFX's `print()`/`setTextColor()`, which closes the last route to a
pixel that did not come through an `Ink`. **There is no path from a stale snapshot to a
coloured pixel that this build accepts**, which is a stronger statement than any test, and it
is why ARCHITECTURE §9 has an entry rather than this log alone: M7's board mode and the
backlog's web mirror inherit the obligation.

**The row budget is derived, asserted, and it fits exactly.** 5 header + 1 rule + 27 asks + 1
spread + 27 bids + 1 rule + 2 strip = **64**. `kLevels` falls *out* of the leftover rows and is
clamped to `kDisplayLevels`, so a header that ever needs a second line costs levels on the panel
and the build stays green — which is what turns "draw fewer levels, never change
`kDisplayLevels`" from an instruction into a compile-time consequence. It cannot overrun:
`kStripRows` would go negative and the assert fires.

**The font is ours, and that is the reason the budget is checkable at all.** Adafruit GFX's
built-in font is 5×7 in a 6×8 cell — ten characters across 64 px and eight rows for one line —
and its custom fonts position the cursor on the *baseline*, so the rows a header occupies depend
on the glyphs in it. Either way the budget could only be verified by looking at the panel.
`ladder_font.hpp` is 41 glyphs of five bytes (**205 B** of `.rodata`), 3×5 in a 4×5 cell, which
gives **sixteen** characters across the header and is what makes 27 levels a side fit with two
rows to spare. `ctest` now answers the question a bench used to.

**The `i2s_pins` trap the brief flagged: confirmed present in the vendored 3.0.15, and made
unreachable.** The struct really is
`r1, g1, b1, r2, g2, b2, a, b, c, d, e, lat, oe, clk`, so a brace list ends `12, 13, 11`. Rather
than write that and comment it, every pin is assigned **by field name**, so a future version can
reorder them and nothing here has to notice. Worth knowing separately: the library's own S3
defaults are *different* from BRINGUP's map (A=18, B=8, C=3, D=42, E=−1, LAT=40, OE=2, CLK=41),
so an omitted pin is a silently wrong panel rather than a build error.

**Framebuffer fit: a calculation, not a retry loop, and the reason is in the library's source.**
Cost is `32 × 64 × depth × 2 B × buffers`, so **65,536 B** at depth 8 double buffered, in
internal DMA-capable RAM. `Panel::begin()` measures the free DMA-capable heap, holds back
`kReserveInternalBytes` (96 KiB), and picks the deepest rung that fits — 8→3 double buffered
**before** considering single buffering, per the brief. It calls `begin()` **once**, because
when a row buffer fails `setupDMA()` returns false leaving everything it already took attached,
above its own `// TODO: should we release all previous rowBitStructs here???` — a second
`begin()` appends *more* rows to the same vector, so a failed attempt costs most of a
framebuffer permanently and the rung beneath it then fails for the wrong reason. Sixth time this
vintage has decided a design.

**The boot order is the other half of that, and it is the one judgement call.** The panel and
the network want the same pool, so whoever allocates first wins. Wi-Fi associates → `Panel::begin()`
sizes against what is left → the WebSocket client starts. A panel that drops a colour bit is
cosmetic; a TLS handshake that cannot allocate is a dead object. `begin()` is never fatal, and it
runs *before* the Wi-Fi halt, so a board with a bad `secrets.h` shows an honest grey `RESYNC`
frame rather than a dark panel. **The reserve is deliberately conservative and is the one number
here with no measurement behind it** — the board prints free-before, free-after, our prediction,
the measured delta and every rung's cost, so the next session can lower it against a figure.

**Strain 7 closed, and closing it found a real defect in the closing.** DESIGN.html queued
"M3 stage D — extend the alloc probe across the render side too". Done: 200 frames of
`LadderView::draw` — every depth, both statuses, a trade print every frame — inside the probe's
window, counter must not move. **Then the mutation check failed to fail.** A `new int` in
`draw()` passed; a `std::string(64,'x')` passed. Both were *silently deleted by GCC's allocation
DCE* while visibly executing (a side effect in the same block failed a different test case,
which is the only reason they were caught at all). Only an allocation whose **pointer** escapes
through a volatile store survives the optimiser — that mutant moves the counter by exactly 200.
A positive control of that shape is now committed beside the assertion, because an allocation
counter that is not wired reports zero for every possible renderer, and this one would have
shipped reading green over a renderer built out of `std::string`.

**Strain 15 opened, and it is the honest cost of double buffering here.**
`Bus_Parallel16::flip_dma_output_buffer()` on the S3 re-points the descriptor chain and returns;
the wait for the previous buffer to drain is in the source and **commented out**
(`gdma_lcd_parallel16.cpp:444`). So the flip is asynchronous and nothing stops the render task
writing into a buffer the DMA has not finished with. What makes it safe is arithmetic in two
files and stated in neither: ~100 Hz scan against a 33 ms period. Raising the draw rate, lowering
`min_refresh_rate`, or landing on a shallower colour depth narrows it, and what comes back is
tearing that reads as a ribbon-cable fault. The guard is cheap (compare
`calculated_refresh_rate` against `kFramePeriodMs` at boot) and is deliberately not written here.

**Both stretch items shipped.** Trade-print flash — three drawn frames of `Ink::Flash` at the
touched level, keyed on the trade ring's own event seq so "new print" is a comparison and not a
heuristic, and a boot with a populated ring does not flash. Sparkline — a 62-sample ring in
`LadderView`, two rows, integer-scaled against the window's own min/max. **Both are render-side
sampled state and neither is a `DisplaySnapshot` field**, which is the decision the brief said
must be recorded whether or not the code was written. `last_px` advancing while Stale needs no
special case: the grey wash covers it.

**Measured on the target toolchain**, not estimated: `LadderView` **536 B**, palettes **72 B**,
font **205 B**; `render_task.cpp.o` has **zero floating-point instructions** and no
`operator new` (the `operator delete` in its undefined list is the driver's never-called virtual
destructor). `ladder_render.hpp` also compiles clean standalone under xtensa GCC 8.4 at
`-Os -fno-exceptions -fno-rtti`, the same bar `dc_engine_target_check` applies to `engine/`.
The render path does add `__divdi3`/`__udivdi3` — one 64-bit divide per drawn level, ~700/s at
Anvil's publish rate against the ~3,000/s the parser's qty scaling already does. Recorded rather
than optimised, following `wire_qty_to_steps`' precedent.

**The review found a real bug in my own arithmetic, and a property test found it rather than a
reader.** `bar_length` was the obvious `kPanelWidth * qty / max_qty` in `int64`, which is
**undefined behaviour** once `qty` passes `INT64_MAX / 64` (~1.4e17). No venue we consume quotes
anything near that — but `Qty` is the full 64-bit type by deliberate design (§4: "crypto tick
sizes and price ranges vary wildly"), the adapter's only quantity guard is `raw < 0`, and a
renderer is a bad place to discover it. The fix shifts both operands down together until the
product is safe; both stay ≥ 1 and the precision lost is far under one pixel of a 64-pixel bar.
It was found by the 3,000-step random walk added during review — the example-based cases all
used quantities a human would think to write down.

**Three DRY findings from the same pass, all taken.** `draw_asks`/`draw_bids` were the same
fourteen lines differing in array, direction and ink pair — now one `draw_side` over a
designated-initialised `SidePlan`, so the ask/bid asymmetry exists in one place (this is the
console ladder's `level_row` lesson, one level up). `render_task.cpp` carried its own
`PriceText` — the same eight lines as `ladder_render.hpp`'s `TextField`, same buffer, same
`format_scaled`, same `'?'` fallback — two implementations of the display edge's one rule in a
firmware whose panel and whose log must agree about what a price is; the copy is gone. And the
paint-a-band-flat idiom appeared twice, now `fill_rows`.

**Deliberately not done.** No `ESP_LOGx` went into `dc_feed`. No `engine/` change of any kind —
not one line, as the brief required. No golden moved. `kRxWatchdogMs` untouched. The idle probe
still ships (the brief left that a stage D decision): it costs an idle core its `waiti`, and with
the stall now diagnosed it is no longer load-bearing — but it is the instrument that would catch
a regression once a panel is beside the feed, and turning it off is `-D DC_IDLE_PROBE=0` on a run
that looks suspicious. Revisit at M4 when the render side has a baseline.

**State of the tree.** `cmake --workflow --preset host-mingw` green: 11/11 ctest, 188/188 in
`dc_tests` (558,780 assertions), 46/46 in `dc_tests_streaming`. Both firmware environments build
clean at RAM 41.9% / flash 13.2%. `docs/DESIGN.html` updated: status strip, §01's HUB75 box and
caption, a new §07 "what the panel costs", strain 7 closed, strain 12 closed, strain 15 opened,
the §09 queue and the footer figures. ARCHITECTURE §9 has two new entries.

**NOT DONE — the acceptance, which is the point.** No bench run, so no pull-the-Wi-Fi result, no
soak, no photo, and **ROADMAP M3 stays `Next`**. Nothing on the panel has been seen by anyone.

**Exact next step: the owner flashes and runs the stage D acceptance in
`firmware/README.md`.** In reading order:

1. The three `panel-hw` boot lines. Record the colour depth, whether it is double buffered, and
   the three allocation figures — ours, the measured heap delta, and the library's own
   `Allocating N bytes`. They should agree to within allocator overhead; if they do not, this
   arithmetic has drifted from the library's. `SINGLE-BUFFERED` is a finding, not a pass.
2. First light: an honest grey `101 RESYNC` frame within a second of Wi-Fi, **not black**.
3. **Pull the Wi-Fi.** Colour drains within ~1 s, geometry stays, header reads `101 DISCONNECT`.
   No torn frame, no frozen intermediate, no flash of a coloured stale book. **This is the M3
   DoD line.** Restore; expect `grey for ~4700 ms` and a reconnect grey indistinguishable from
   any other.
4. Ten minutes on the **near** mesh node — the first time the 2026-08-10 mesh fix is checked on a
   panel rather than in a log. `wd_gaps` 0, `sock_gaps` low, colour held throughout.
5. The `-- panel` line: `drew … /s` should track the `-- rate` line's events/s, and `worst paint`
   should be a small fraction of 33,000 µs and should **not** move with book depth.
6. Feed side unregressed (`arrive`/`event`/`a→e`, Core 0 ~90% idle, `a→e` ≤ 22 ms) and heap flat.
7. Photo/clip to `hardware/` — it also unblocks **MP stage 2**, so shoot it like it will be
   published.

Then tick Stage D here and in `docs/briefs/M3-stage-D-the-panel.md`, and only then tick **M3** in
`ROADMAP.md` and mark **M4 Next**.

### 2026-08-11 · Opus 5 + owner at the bench · THE OBJECT CAME ALIVE, and the panel taught us five things

**The ladder is on the panel.** Live Anvil order book at 64×64: bids stacking green below the
spread, asks red above it, the amber spread row between them, the header naming the instrument
and the last price, the tape strip along the bottom. The same `engine/` that runs under `ctest`
on the desk, drawing copper and light. Photographed twice during the session.

**Invariant #5 was watched behaving, repeatedly and unprompted.** The honest grey `RESYNC` frame
appears before Wi-Fi finishes associating — never a black screen — and the panel goes coloured on
the first snapshot. Over the evening it greyed on watchdog trips and socket drops dozens of times
and came back clean every time, with `grey for N ms` matching the log. **The formal
pull-the-Wi-Fi acceptance run and its photo are still owed** (see the DoD), but the behaviour it
tests has now been seen many times over.

Five findings, in the order the bench produced them. Four are recorded here because they are
*measurements*, and one belongs to hardware.

**1. The framebuffer sizing was wrong by 44%, and the board said so in one line.**
`predicted=65536 measured=94468`. The model counted the BCM framebuffer and ignored 24,576 B of
DMA descriptors and ~4,356 B of allocator bookkeeping. It only worked because the reserve
happened to exceed the error — `choose_depth()` said "fits in 88,332" for something needing
94,468. `panel_budget.hpp` now replays the library's own integer arithmetic (refresh loop,
transition-bit search, descriptor count) and `test_panel_budget.cpp` pins it against the numbers
the board printed. Confirmed the next boot: predicted 78,080 against 77,896 measured, 0.24%.

**2. Colour depth 8 → 6, and it cost nothing visible.** The library's own log said why: at depth
8 it cannot reach 60 Hz without collapsing the bottom three colour bits ("Percieved colour depth
to the eye may be reduced"), while at depth 6 it clears 60 Hz with every bit correctly weighted.
Depth 8 was paying 16 KiB for colour the panel was never showing. That mattered — steady free
internal heap was **13,816 bytes with a 4,084 byte largest block**, against ~124 KB in stage C
with no panel. Now 30,172 / 12,276.

**3. Pure hues, because six bits quantise a tint into a different colour.** The sides were tinted
toward white to mark the touch; at six bits the small off-channel components survive while the
main channel is already saturated, and the verdict was immediate — "red is now more pink". Each
side is one channel now, best-of-book distinguished by intensity. **Then the reverse mistake:**
dimming the bars to reduce coupling made things *worse*, because under BCM a channel at 255 is
DC-on for the whole frame while an intermediate value toggles bit-planes several times per frame.
**The aggressor is edges, not amps.** Best-of-book stays pinned at the rail precisely because
that is the quiet state.

**4. The 3×5 font was never going to be readable, and the row budget absorbed the fix by
itself.** Two bench verdicts, one of them after brightness went 160 → 224 specifically to rule
brightness out. 4×6 is 78% more lit area; it costs one header row and therefore `kLevels`
27 → 26, exactly the trade the stage D brief pre-authorised ("draw fewer levels; never change
`kDisplayLevels`"). **Changing `kGlyphHeight` moved the header, both rules, both ladders, the
spread row and the tape strip with no coordinate edited**, and the "budget sums to exactly 64
rows" assert held throughout. That derivation was built for this moment and it paid.

**5. Wi-Fi: the framework gives up permanently on AUTH_FAIL, and nothing was watching.** Blocking
the board at the AP left the panel grey for the rest of the run — 145 s and counting, unblocking
changed nothing. `WiFiGeneric.cpp:1177`'s reconnectable-reason list contains
`WIFI_REASON_802_1X_AUTH_FAILED` and **not** `WIFI_REASON_AUTH_FAIL` (202), so after Arduino's one
retry is refused, `WiFi.begin()` is never called again. `connect_wifi()` being a one-shot at boot
was a latent hole with no owner. `WifiSupervisor` now rejoins on its own clock, host-tested beside
the socket policy — same lesson as the reconnect rework, one layer down: **supervise on observed
state, not on the library's policy.** Confirmed on the board (`rejoining (#15)` → LIVE). A second
pass halved the recovery again by noticing that a *refused* attempt is not an *in-flight* one:
the AP refuses in 60 ms, so refused retries come back in 1 s instead of 5.

**THE ONE THAT IS HARDWARE, AND IT IS WRITTEN INTO `hardware/BRINGUP.md` RATHER THAN HERE.** The
panel shows ghost pixels beside the header glyphs. `clkphase = false` clears them completely —
and collapses inbound Wi-Fi from ~6 msg/s to ~2 msg/s, reproduced consistently both ways, with
`sock_gaps=0`, 0% lost and both cores 96% idle. It cannot be firmware: `clkphase` reaches only a
GPIO-matrix inversion on the CLK pin, with no change to DMA, data rate or CPU load. Inverting CLK
aligns its edge with the thirteen data lines beside it, so ~14 outputs switch simultaneously down
a bare ribbon with no ground plane, a hand's width from the DevKit's 2.4 GHz antenna. **The panel
samples its own data better and jams its own radio.** The feed wins; `clkphase` stays default.
This is now an M6 carrier-board *requirement* with evidence, and it makes the roadmap's existing
ground plane, short leads and `2× 74HCT245` buffers load-bearing rather than nice-to-have.

**And a fact nobody had noticed in three stages: this panel has never run at the clock the config
asks for.** The S3 backend *buckets* `i2sspeed` rather than using it — anything ≤ 10 MHz lands on
divider 16, i.e. 160/16 = **10 MHz** — while the library's refresh loop still computes from the
nominal 8 MHz. So every `refresh=105 Hz` this firmware has ever printed is ~25% low; the real
figure was ~131 Hz. The board had been printing both halves on adjacent lines (`Clock divider is
16`) the whole time. It also means the obvious way to slow the clock (lower `i2sspeed`) would have
changed nothing and read as a null result. `S3_LCD_DIV_NUM=24` is the real knob and is shipped as
a partial mitigation (6.67 MHz, ~87 Hz actual): the header's left number went stable, the right
one did not. **DESIGN strain 15's "one scan inside one render period" margin was computed from
that wrong number and was right by luck, not analysis.**

**Deliberately stopped here.** The remaining ghosting is cosmetic, understood, and has a hardware
fix scheduled. Stage D's definition of done does not mention the header, and this milestone's log
already records three evenings lost to chasing a fix before the data chose it.

**State of the tree.** `cmake --workflow --preset host-mingw` green: **11/11 ctest**, `dc_tests`
169 → **200 cases**. Both firmware environments build clean, RAM 41.9%, flash 13.2%. No `engine/`
behaviour change, no golden moved, `kRxWatchdogMs` still 1000 ms. Eight commits on
`m3/stage-d-the-panel`.

**The acceptance was started the same morning and is most of the way there** —
`hardware/bench-2026-08-11-stage-d-soak.md`. The feed-side and heap line is **met and ticked**:
`a→e` worst 8 ms against the ≤22 ms bar, both cores exactly at their healthy idle baselines (the
render task costs nothing measurable), `parse=0` across 589 frames, pipe all zeros, and heap
`free (+0) largest (+0)` across four consecutive blocks. The reconnect measured **3,975 ms**
against the ~4,700 ms the transport-residual work predicted. The RX watchdog was caught firing
**979 ms into a 1,605 ms silence** — the 1,000 ms deadline, to the log's resolution.

**Exact next step, and it is now three specific things rather than "the acceptance".**

1. **Capture with `pio device monitor -f log2file`.** The monitor buffer truncated both attempts
   at ~2.5 minutes, so the ten-minute cumulative totals do not exist. This is the whole reason
   two DoD lines are still open.
2. **Re-run the pull-the-Wi-Fi test with that capture running.** The stopwatch figures (~3 s to
   grey, ~9 s to recover) both decompose correctly — ~1 s of watchdog plus ~2 s of data still in
   flight, and ~4.5 s of station re-association plus the measured 3.6 s socket bring-up — but
   the DoD's number is silence-to-grey off the `-- hole` line, not wall-clock from the switch.
3. **Commit a photo/clip to `hardware/`.** Shot twice during the session, neither committed.
   Unblocks MP stage 2, so shoot it like it will be published.

**And one question that outranks all three: three `rst:0x1 (POWERON)` resets in the first 35 s
of the soak.** Owner power-cycling, or the board browning out? `POWERON` is a genuine power
event — not a panic, not a task watchdog — and brightness 224 plus panel inrush on a
USB-powered DevKit is a plausible mechanism. If it is the board, it is a stability question and
M3 should not be ticked over it.

Then tick Stage D here and in `docs/briefs/M3-stage-D-the-panel.md`, and only then tick **M3** in
`ROADMAP.md` and mark **M4 Next**.
