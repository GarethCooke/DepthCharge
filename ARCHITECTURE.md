# DepthCharge — Architecture

**Status:** Constitution. This document changes only by explicit decision (record it in §9).
Per-milestone work is specified in disposable briefs under `docs/briefs/`; this file is what
every session reads first and must not violate.

---

## 1. What this is

DepthCharge is a desk-top hardware market-data terminal: an ESP32-S3 driving a 64×64 HUB75
LED matrix behind smoked acrylic, rendering a live limit-order-book ladder — bids stacking
green, asks red, trade prints flashing white at the touch, a last-price sparkline along the
bottom, and an honest grey "stale" state whenever the data cannot be trusted.

It consumes three venues, in deliberate order of difficulty:

1. **Anvil** (`anvil.garethcooke.com`) — the author's own C++20 matching engine. Full top-N
   snapshots + per-fill trades; every frame carries a `seq`, but it is a single *global*
   engine counter, so one socket's received subsequence is sparse **and non-monotonic**
   (measured at M0) — the adapter synthesises its own `Seq` (§4). Easiest venue; also the
   only one we control end-to-end, which makes it the test rig.
2. **Kraken** — real L2 deltas, self-verifying via a CRC32 checksum over the top 10 levels.
3. **Binance** — the graduation exercise: buffered diff stream bracketed against a REST
   snapshot, with sequence-gap recovery. (Its partial-depth streams are an Anvil-shaped
   easy mode and may be used as a stepping stone.)

DepthCharge is the *consumer* side of the wire; Anvil is the *producer* side. The interface
to Anvil is Anvil's versioned `PROTOCOL.md` (vendored snapshot in `docs/vendor/`), and
DepthCharge v1 requires **zero changes to Anvil**.

## 2. System overview

```
 venue ws ──► VenueAdapter ──► FeedEvent ──► BookEngine ──► DisplaySnapshot ──► Renderer
 (TLS)        (per venue)     (the one       (hot window    (SPSC double        (HUB75 DMA
                               boundary       + cold tail)   buffer)             / console)
                               type)
```

Two execution contexts share the same `engine/` code:

- **Harness (desktop)** — `harness/` builds the engine on the host, drives it from recorded
  replay files, checks golden expectations, benches it, and renders to a console ladder.
  This is where correctness lives.
- **Firmware (target)** — `firmware/` (PlatformIO, ESP32-S3) runs the identical engine.
  Core 0: network + adapter + book ("feed task"). Core 1: HUB75 render task. They meet at
  a seqlock/double-buffered `DisplaySnapshot` and nowhere else.

## 3. Repo layout

| Path         | Owns                                                                        |
| ------------ | --------------------------------------------------------------------------- |
| `engine/`    | Portable C++20 library: types, `FeedEvent`, book, adapters' *logic* (frame → events). No I/O, no ESP-IDF, no FreeRTOS. |
| `harness/`   | Host executables: replay runner, golden tests (doctest/ctest), console renderer, bench. |
| `firmware/`  | PlatformIO project: Wi-Fi, TLS WebSocket transport, tasks, HUB75 driver glue. Links `engine/` as-is. |
| `hardware/`  | KiCad carrier board, enclosure CAD + STLs, BOM.                             |
| `tools/`     | Capture scripts and other dev tooling (Python allowed here, nowhere else).  |
| `docs/`      | Milestone briefs (`docs/briefs/`) and vendored protocol snapshots (`docs/vendor/`). This file and `ROADMAP.md` live at the repo root. |

Transport (sockets, TLS) lives *outside* `engine/`; adapters in `engine/` accept received
frames as bytes/strings and emit `FeedEvent`s, so the identical adapter logic runs under a
Python-captured replay file, a host WebSocket client, or `esp_websocket_client`.

## 4. The FeedEvent contract (normative)

`FeedEvent` is the **only** type that crosses the adapter → engine boundary. Until code
exists this section is the source of truth; from M0 onward the header
`engine/include/depthcharge/feed_event.hpp` is, and must stay in sync with this intent.

```cpp
using PriceTicks = int64_t;   // price in integer ticks; per-symbol tick size (see below)
using Qty        = int64_t;   // quantity in integer venue steps; per-symbol qty step
using Seq        = uint64_t;  // adapter-normalised, monotonic per (venue, symbol) stream

enum class Side : uint8_t { Bid, Ask };

enum class GapReason : uint8_t { SeqGap, ChecksumFail, Disconnect, Overflow, Resync };

struct BookLevel { PriceTicks px; Qty qty; };

// Borrowed view of a Snapshot's levels; valid only for the duration of the sink
// call that delivered the event. A consumer that defers copies (M1).
struct LevelSpan { const BookLevel* data; uint32_t size; };

struct FeedEvent {
    enum class Kind : uint8_t { Snapshot, Delta, Trade, Gap };
    Kind kind; Seq seq;
    PriceTicks px; Qty qty; Side side; GapReason reason;
    LevelSpan bids, asks;   // Snapshot only
    // Snapshot: full replacement of both sides to the venue's stated depth,
    //           conveyed as the two spans (≤ kMaxSnapshotLevels each)
    // Delta:    one level, absolute quantity (qty == 0 ⇒ level removed)
    // Trade:    px, qty, aggressor side
    // Gap:      reason; book state must be treated as unknown until next Snapshot
};
```

Contract semantics:

- **Scaling.** Prices and quantities are integers scaled by per-symbol `tick_size` /
  `qty_step` supplied in venue symbol metadata. `int64_t` because crypto tick sizes and
  price ranges vary wildly across symbols and venues; exact integer equality is the point.
  Where a venue publishes no such metadata (Anvil does not), DepthCharge declares it in a
  `SymbolSpec` and the adapter **verifies** every wire price is exactly representable at
  that scale — a mismatch is a reported error, never a silent rounding.
- **Snapshot replaces; Delta amends.** A `Snapshot` discards all prior levels for the
  symbol. Depth beyond the venue's stated N is *unknown*, not zero.
- **Snapshot levels are borrowed, not owned.** `FeedEvent` stays a flat, trivially
  copyable value (invariant #7); a `Snapshot` points at an adapter-owned staging buffer
  that is valid only during the sink call. This is what keeps the boundary allocation-free
  with a single boundary type (invariant #2).
- **Seq is the adapter's problem.** Each adapter normalises its venue's native scheme into
  a single monotonic `Seq`. Anvil's wire `seq` is a *global* counter shared by all tickers
  and frame types, so a single socket's subsequence is non-monotonic (M0 measured 42
  backward steps in 5 minutes) and unusable for ordering: its adapter **synthesises** `Seq`
  from receive order and never raises `Gap{SeqGap}` — safe because Anvil's `snapshot`/`book`
  frames are idempotent full replaces. Binance uses `U`/`u` bracketing; Kraken has no seq —
  its adapter synthesises one and converts a CRC failure into `Gap{ChecksumFail}`. The
  engine's only rule: a discontinuity it is told about via `Gap` makes the book stale until
  the next `Snapshot`.
- **Gap is data, not an error.** Disconnects, ring overflow, checksum failures, and seq
  gaps all arrive as `Gap` events and drive the rendered stale state. No venue is required
  to *send* one: Anvil emits no gap/error frame at all, so `Gap{Disconnect}` is synthesised
  **transport-side** — socket close, or an RX watchdog whose timeout exceeds the venue's
  worst healthy inter-frame gap by a clear margin (host replay reads the same rule off
  `rx_ns`; see the M1 brief).

## 5. Book engine

- **Target design** (full form lands with delta venues, M4+): a **tick-indexed dense
  window** — a contiguous array of `Qty` addressed by `(px − anchor)` — over the hot band
  around mid, re-anchoring by bounded copy when the touch drifts out of the window; a cold
  tail (map) for levels outside it. On target the window lives in internal SRAM, the tail
  in PSRAM; on host it's all plain heap. This is deliberately the consumer-side twin of
  the "windowed dense array" named in Anvil's future work.
- **Phase 1 degenerate form** (Anvil-only, M1–M3): snapshots-only venues need no book
  maintenance at all — "adopt latest snapshot" *is* the engine, plus a trade ring. Build
  that first; do not gold-plate ahead of the first delta venue.
- **Output:** `DisplaySnapshot` — top ~27 levels/side (fits 64 rows with header, spread
  gap, sparkline strip), recent-trade ring (≥8), last px, status `{Live | Stale(reason)}`,
  symbol id. Published by version-stamped double buffer; the render side takes the latest
  complete version and never blocks the writer.

## 6. Invariants (frozen — do not refactor through these)

1. **`engine/` builds on the host with zero ESP-IDF/FreeRTOS/Arduino includes.**
   *Why:* every line of book logic must be exercisable by ctest on the desk; this is the
   seam that made Anvil's demo cheap, inherited deliberately.
2. **`FeedEvent` is the only type crossing adapter → engine.**
   *Why:* venue mess (JSON dialects, seq schemes, checksums) stays quarantined; the book
   is written once against one vocabulary.
3. **Integer ticks and steps everywhere; no floating point ever touches book data.**
   *Why:* exact equality, no key drift, deterministic replay. Anvil's rule, verbatim.
   (Floats may appear only at the display-formatting edge.)
4. **The feed task is never blocked by the render task.** The hand-off is a wait-free,
   single-producer/single-consumer *latest-value* mailbox: publish overwrites in place and
   never waits on the render task. Superseded `DisplaySnapshot`s drop silently — lossless at
   the book level, because every published frame is a *complete* render state, not a delta,
   so the render side skipping v10→v13 loses no book information (only intermediate
   trade-ring / sparkline samples, which are best-effort). `Gap{Overflow}` is therefore a
   *feed-side* signal — a venue reassembly buffer, first produced at a delta venue (M4/M5) —
   never a render-hand-off event.
   *Why:* bounded per-event cost independent of consumer speed — same rule as Anvil's
   egress; on a microcontroller the render stall is the common case, not the rare one.
5. **Stale is a first-class rendered state.** Any `Gap`, disconnect, or resync greys the
   panel until a fresh `Snapshot`. A frozen ladder that looks live is the one unacceptable
   output. **Liveness is defined by events reaching the book, not by bytes arriving:** a
   transport that delivers frames the parser then rejects is *stopped* and must grey (M3
   Stage C — the firmware watchdog arms on a book-event for this reason; the current goldens
   pin a weaker byte-cadence definition, see §9). *Why:* the entire honesty of the object
   depends on it.
6. **No feature merges without replay coverage.** New adapter behaviour or book logic
   ships with a captured or synthesised trace and a golden expectation in `harness/`.
   *Why:* multi-session agentic work converges only when red/green is objective.
7. **Allocation-free steady state.** After connect + first snapshot, the feed→render path
   performs no heap allocation. *Why:* determinism and embedded heap health; also keeps
   host benches honest about target behaviour.
8. **One writer per state.** Only the feed task mutates the book; only the render task
   reads `DisplaySnapshot`. No third participant, no locks around the book itself.

## 7. Decisions already made (with rationale)

- **Separate repo from Anvil.** The boundary is the versioned wire contract, not shared
  code; DepthCharge is `PROTOCOL.md`'s second independent client, which is exactly the
  claim worth being able to make. Build/deploy isolation follows the portfolio-wide
  one-repo-per-deploy pattern.
- **One repo for engine + firmware + hardware.** Firmware, board, and enclosure versions
  travel together (MorayGlow pattern); the host-buildable `engine/` seam keeps this from
  hurting testability.
- **Anvil stays untouched in v1.** Known non-blockers live on Anvil's own backlog: chaos
  flag (deliberate frame drop/delay for deterministic gap testing), L2 incremental feed
  (DepthCharge becomes its test client when built), feeder realism. The WS `Origin`
  allowlist question is closed: M0 confirmed the deployed upgrade accepts a client sending
  no `Origin` header, so neither the capture tool nor the firmware needs one and no Anvil
  config change is required. Two DepthCharge→Anvil couplings are now on record — an Anvil
  change to either surfaces only as a dead panel, not an Anvil-side error:
  - **TLS-chain pin** — DepthCharge firmware pins ISRG Root X1 (and X2); an Anvil CA/chain
    change is a firmware-update event, not a transparent one.
  - **Republish-cadence assumption** — the liveness watchdog assumes Anvil's unconditional
    ~80 ms republish; Anvil's future incremental feed must ship a heartbeat, or DepthCharge's
    liveness inverts into crying-wolf (strain point 10).
  - **TLS-chain pin** — DepthCharge firmware pins ISRG Root X1 (and X2); an Anvil CA/chain
  change is a firmware-update event, not a transparent one.
- **Republish-cadence assumption** — the liveness watchdog assumes Anvil's unconditional
  ~80 ms republish; Anvil's future incremental feed must ship a heartbeat or DepthCharge's
  liveness inverts into crying-wolf (strain point 10).
- **Crypto venues for the real-data leg.** 24/7 markets suit a permanent desk object;
  free unauthenticated L2 depth; Anvil's synthetic flow is irrelevant to DepthCharge's
  correctness because the panel consumes wire semantics, not market truth.
- **Portable C++ first, target second.** Every milestone proves on the host harness
  before it touches the ESP32. Replay files are the ground truth artefacts.
- **doctest + ctest** for the harness (familiarity with the Anvil toolchain); JSON parsing
  choice is *not* fixed here — the harness may use a heavyweight parser, but whatever
  parses on the hot firmware path must respect invariant 7. Decide in-milestone, record
  in the brief.

## 8. Deliberately unspecified / out of scope

Unspecified on purpose (sessions decide, briefs record): file decomposition inside
`engine/`, class internals, console renderer aesthetics, exact capture format beyond
"NDJSON, one received frame per line, with a metadata header line".

Out of scope for v1: order entry of any kind, historical persistence, more than three
venues, any web UI (Anvil already has one), battery power.

## 9. Amendment log

| Date       | Change                    | Why |
| ---------- | ------------------------- | --- |
| 2026-07-23 | Initial constitution.     | —   |
| 2026-07-26 | **§1/§4 seq correction.** Anvil's wire `seq` is a global, per-socket non-monotonic counter; its adapter synthesises `Seq` from receive order and never raises `Gap{SeqGap}`. | M0 measured it on the live server (42 backward steps / 5 min, no reset across reconnect). The old text described a guarantee the venue does not provide; an adapter written to it would have gapped ~9 times a minute on healthy data. Safe because Anvil's book frames are idempotent full replaces. |
| 2026-07-26 | **§4 gains `LevelSpan`;** a `Snapshot` conveys its levels as two borrowed spans into adapter-owned storage, capped at `kMaxSnapshotLevels` (256/side). | §4 left "how the level list is conveyed" open until the phase-1 book (M1). Spans keep `FeedEvent` the *only* boundary type (inv. #2) while staying flat, trivially copyable and allocation-free (inv. #7). Cost: the lifetime rule — defer means copy. |
| 2026-07-26 | **§4 scaling made explicit for venues without tick metadata.** DepthCharge declares `SymbolSpec{price_decimals, qty_step}`; the adapter verifies exact representability. | Anvil's protocol carries no tick size or qty step (M1 known unknown). The choice is either declare-and-verify or silently round — and rounding book prices would break inv. #3 invisibly. |
| 2026-07-26 | **§4: `Gap{Disconnect}` is synthesised transport-side**, via socket close or an RX watchdog. | Anvil never sends a gap frame, so absence of data is the only signal. Pinning the rule in the constitution makes the M1 host replay and the M3 firmware transport provably the same contract. |
| 2026-08-07 | **Invariant #1 gains a target half.** `engine/` must compile for the ESP32-S3, not only for the host, and `dc_engine_target_check` enforces it with the PlatformIO xtensa compiler when one is installed. The language `engine/` may use is the C++20 subset that toolchain accepts — today `espressif32 6.5.0` / xtensa GCC 8.4, which means **no `<span>`, `<ranges>`, `<concepts>`, `<bit>`, no floating-point `from_chars`, and no `constexpr` algorithms**. | The host check proved "engine/ builds on the host" and was silently read as "engine/ builds". It does not follow: a `constexpr std::reverse_copy` compiles on the host GCC 15.2 and is ill-formed on GCC 8.4, so a header could be green on the desk and reject on the board with nothing to say so until M3 tried to link. Measured: all eight headers pass today, and the hot path costs 1541 B of `.text` at `-Os`. Raising the ceiling is a platform decision (pioarduino / IDF 5.x give GCC 14–15 and lift every restriction above) and belongs in a brief, not here — this line records what the ceiling *is*, not that it is permanent. |
| 2026-08-07 | **One definition of a valid trace.** `TraceReader::next` is it; `read_trace()` is a statistics pass over the same reader. A frame line must carry a string `type`, and `rx_ns` must not decrease. | `read_trace()` and `TraceReader` were written separately and had drifted: a frame with no `type` was rejected by one and accepted by the other, and `rx_ns` monotonicity was enforced by one and ignored by the other — with `dc_replay` plus the M0 goldens using the first and `dc_ladder` plus every M1 golden using the second. Two answers to "is this trace valid" in a project whose stated premise is that replay files are ground truth. Both rules kept, so the harness got stricter in the direction that fails loudly; both committed traces satisfy both unchanged. |
| 2026-08-07 | **§4 addendum: trailing silence is reported, never inferred.** A replay treats the end of a trace as the end of the timeline unless the caller supplies `ReplayOptions::end_of_trace_silence_ms`. | The RX watchdog is edge-triggered by the arrival of the next frame, so a capture that ended with 30 s of silence replayed as `Live` to the last frame while the panel's timer would have greyed it — the host replay and the M3 firmware disagreeing about the same rule. A file has no "now" and the capture tool does not record when it stopped listening, so the replay cannot infer the answer; a caller that knows says so. Off by default, so no committed golden moves. |
| 2026-08-07 | **§2/§5: the feed→render hand-off is a three-slot mailbox, not a seqlock or a two-slot double buffer.** `SnapshotChannel` keeps three `DisplaySnapshot` slots and one 32-bit atomic word; `publish` and `consume` each swap their slot into that word with a single `exchange`, so the writer's slot, the reader's slot and the ready slot are always three distinct objects. Both §2 and §5 say "seqlock/double buffer"; read them as naming the *contract* (version-stamped, latest-wins, never blocks the writer), which is unchanged, not the storage. | A seqlock and a two-slot buffer both let the reader copy a slot the writer is writing and then discard the result — the tear happens and the version check only stops it being drawn. Measured both ways at M3 stage A: a seqlock of exactly that shape delivered 4.8 M frames with **zero** tears reaching the consumer (x86, GCC 10, `-O2`), and ThreadSanitizer flagged it on the **first** frame. So the objection is not that a seqlock misbehaves on today's compiler; it is that the copy is a data race, stage A's definition of done asks for a clean TSan report, and buying one with a suppression would put that suppression on the single cross-core path, on a target compiler generation (xtensa GCC 8.4) nobody here controls. Two slots cannot avoid it either: the reader holds one, so a writer alternating across two must eventually land on the one being read. Cost of the third slot, measured on the target toolchain at `-Os`: +2,352 B of `.data` (1,176 → 3,528) and +114 B of `.text` (`publish` 30 → 87, `consume` 39 → 96). Invariant #4 survives intact — `publish` is a fixed copy plus one `exchange`, which GCC lowers to `xchg` on x86 and to a bounded `S32C1I` retry on the LX7. |
| 2026-08-08 | **§6 inv. #4: render hand-off is a silent-drop latest-value mailbox; `Gap{Overflow}` is feed-side only.** Superseded frames drop with no report (lossless — each `DisplaySnapshot` is a complete book state); `Gap{Overflow}` is reserved for a delta-venue reassembly buffer (M4/M5) and is correctly raised nowhere in the Anvil-only phase. | The §5 version-stamped double buffer has no queue to overflow, so the original "drops frames *and reports it* as `Gap{Overflow}`" named a mechanism the chosen hand-off doesn't have — a queue model imported into a mailbox design. Surfaced building the real channel at M3 Stage A (3-slot wait-free `SnapshotChannel`; slot-count rationale + measurements at `ARCHITECTURE.md:219` and the `snapshot_channel.hpp` header). The invariant's guarantee (feed never blocked, bounded per-event cost) is unchanged; only the false drop-reporting clause is corrected. |
| 2026-08-08 | **§7: the parse seam has two implementations, and the nlohmann one is the *specification* — its observable behaviour, accidents included.** A second `parse_anvil_frame` (streaming, allocation-free, target-bound) now links beside the M1 reference. Equivalence is not reviewed, it is executed: `harness/tests/test_parser_equivalence.cpp` compiles into both test binaries and holds both to one table, and `dc_tests_streaming` re-runs the M1 goldens and the malformed corpus against the replacement. Three divergences are deliberate and asserted as such (JSON nesting past 64; an *escaped* token unescaping past 64 bytes; the band between `DBL_MAX` and `1e309`). | §7 said only "whatever parses on the hot firmware path must respect invariant 7 — decide in-milestone". That left "and must it agree with the reference?" unstated, and the answer is not obviously yes: nlohmann 3.11.3 resolves duplicate object keys last-wins because its DOM is a `std::map` filled through `operator[]`; classifies an integer too large for `uint64_t` as a *float*, so an over-range qty is `BadShape` and not a saturated number; and treats a NUL byte as end-of-input mid-buffer. None of that is a decision anyone made, and all of it is load-bearing, because `AnvilAdapter` files `BadPrice`, `OtherTicker` and everything-else into three different counters and those counters are pinned goldens. Writing the second parser to a *reasonable* reading of JSON rather than to the reference would have moved goldens on malformed input while looking correct. |
| 2026-08-08 | **§4: the adapter's decode/semantics line stays at `AnvilFrame`; the *conversions* are shared instead.** `AnvilFrame` continues to carry `PriceTicks`/`Qty`, not raw decimal tokens. `kind_from_type`, `price_text_to_ticks`, `wire_qty_to_steps` and `aggressor_from_text` moved out of the nlohmann TU into `engine/include/depthcharge/anvil/anvil_scaling.hpp`, which both parsers include. | The M3 Stage B brief asked for scaling to be "lifted into the shared adapter", on the correct worry that two conversion paths is where invariant #3 drifts silently — a rewrite fails loudly on JSON structure but quietly on arithmetic. Moving the *boundary* would have done that badly: the nlohmann DOM's strings die with the parse, so it would have had to copy every one of ~250 tokens per frame into the frame to hand a token onward, and it would have rewritten `AnvilFrame`, the adapter, and the postcondition test — which are Stage B's own acceptance gate and which this milestone's constraints say do not change. Sharing the conversion achieves what the worry actually asked for (one implementation of #3, byte-identical in both parsers) at no cost to either. Recorded because the next two adapters (Kraken M4, Binance M5) should copy this split, not the one the brief described. |
| 2026-08-08 | **§6 inv. #7 on the target reads as "no net allocation and no fragmentation drift", not "no `malloc` anywhere in the stack".** The *engine* half keeps the strong form and is proven: `alloc_probe` counts zero global `operator new` across both full traces on the host, and the streaming parser's xtensa object has no undefined `operator new`, so it cannot allocate on the board either. The *transport* half cannot: `esp_websocket_client` dispatches every event through `esp_event_post_to`, which heap-copies the 28-byte payload and frees it when the handler returns. | Stage C is the first time invariant #7 met a library nobody here wrote. The measurement was not deferred to the bench — disassembling the shipped `libesp_event.a` shows `memset → calloc → memcpy → … → free` per post, which at a 4 KiB RX buffer and ~8.7 KB book frames is ~37 balanced pairs a second, squarely inside the "after connect + first snapshot" window the invariant names. Two things follow. First, the honest reading is the one above: what #7 exists to protect is determinism and a heap still healthy after a week on a desk, and bounded balanced churn of one fixed-size block threatens neither, whereas an unbounded or growing one does. Second, ESP-IDF heap tracing is unavailable to check it — the precompiled Arduino framework ships `CONFIG_HEAP_TRACING_OFF=y` — so `firmware/src/heap_probe.*` samples free bytes, the low-water mark and the largest free block instead, and is documented as detecting a *leak or fragmentation*, not the churn. An earlier draft claimed the low-water mark could see churn; it cannot (it is a since-boot minimum with no reset), and that claim was corrected rather than shipped. |
| 2026-08-09 | **The transport supervisor owns the reconnect cadence, and drives it from the socket state — never from the client's event stream.** `WsTransport::supervise()` is no longer only the clean-close rescue: it polls `esp_websocket_client_is_connected()` at 250 ms and restarts the client `kReconnectBackoffUs` (2 s) after the feed dies, then leaves each attempt `kHandshakeBudgetUs` of immunity before the next. `esp_websocket_client`'s own auto-reconnect stays enabled but is not relied on. | Third time the precompiled Arduino-ESP32 2.0.14 vintage has decided a design (after the certificate bundle and heap tracing): its `esp_websocket_client_config_t` has no `reconnect_timeout_ms`, so the library's 10 s `WEBSOCKET_RECONNECT_TIMEOUT_MS` is baked into the archive and unreachable — the only route to a shorter cadence is to preempt it. **The event-driven version of this was written first, shipped, and did not fire once**: it armed on `WEBSOCKET_EVENT_ERROR`/`CLOSED`, and this vintage dispatches neither for a dead socket — a read failure goes through `abort_connection()`, which raises `DISCONNECTED` (the library's `Error receive data` line is its own `ESP_LOGE` en route, not an event). Arming on `DISCONNECTED` instead is the trap, because `stop()` runs through the same `abort_connection()` and every restart would re-arm the timer that caused it. The socket state is the only signal that needs no theory about the library's internals, and the same poll had been driving the 20 s backstop correctly all along. Recorded because the general rule outranks the incident: **on this stack, supervise on observed state, not on reported events** — and an IDF bump that supplies `reconnect_timeout_ms` should set the field and delete the preemption rather than leave two mechanisms racing. |
| 2026-08-09 | **§6 #5: liveness is book-events, not byte arrival; the strict definition is canonical.** A transport delivering frames the parser rejects is *stopped* and greys — bytes arriving is not liveness. The M3 Stage C firmware watchdog arms on a book-event; the host replay driver and the committed goldens pin the weaker byte-cadence definition. | Frames-arrive-but-don't-parse is a frozen ladder reading LIVE — exactly what #5 forbids — and a clean capture cannot contain it, so no existing golden exercises it (DESIGN.html strain point 10). Recording the strict definition here *binds* it: until a synthesised malformed-arrival trace pins it on the host (M4 opener), green under-specifies #5 — do not align the firmware down to the host driver. Measured cost of the strict arm: none (worst healthy gap 640.2 ms either way). |