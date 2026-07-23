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
   snapshots + per-fill trades, every frame carrying a monotonic `seq`. Easiest venue; also
   the only one we control end-to-end, which makes it the test rig.
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

struct FeedEvent {
    enum class Kind : uint8_t { Snapshot, Delta, Trade, Gap };
    Kind kind; Seq seq;
    // Snapshot: full replacement of both sides to the venue's stated depth
    // Delta:    one level, absolute quantity (qty == 0 ⇒ level removed)
    // Trade:    px, qty, aggressor side
    // Gap:      reason; book state must be treated as unknown until next Snapshot
};
```

Contract semantics:

- **Scaling.** Prices and quantities are integers scaled by per-symbol `tick_size` /
  `qty_step` supplied in venue symbol metadata. `int64_t` because crypto tick sizes and
  price ranges vary wildly across symbols and venues; exact integer equality is the point.
- **Snapshot replaces; Delta amends.** A `Snapshot` discards all prior levels for the
  symbol. Depth beyond the venue's stated N is *unknown*, not zero.
- **Seq is the adapter's problem.** Each adapter normalises its venue's native scheme
  (Anvil's frame `seq`; Binance `U`/`u` bracketing; Kraken has no seq — its adapter
  synthesises one and converts a CRC failure into `Gap{ChecksumFail}`) into a single
  monotonic `Seq`. The engine's only rule: a discontinuity it is told about via `Gap`
  makes the book stale until the next `Snapshot`.
- **Gap is data, not an error.** Disconnects, ring overflow, checksum failures, and seq
  gaps all arrive as `Gap` events and drive the rendered stale state.

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
4. **The feed task is never blocked by the render task.** Hand-off is wait-free on the
   writer side; overflow drops frames *and reports it* as `Gap{Overflow}`.
   *Why:* bounded per-event cost independent of consumer speed — same rule as Anvil's
   egress; on a microcontroller the render stall is the common case, not the rare one.
5. **Stale is a first-class rendered state.** Any `Gap`, disconnect, or resync greys the
   panel until a fresh `Snapshot`. A frozen ladder that looks live is the one unacceptable
   output. *Why:* the entire honesty of the object depends on it.
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
  allowlist question is handled firmware-side first (send a nominated `Origin` header);
  it becomes an Anvil config change only if that proves insufficient.
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
