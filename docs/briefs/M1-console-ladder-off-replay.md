# M1 — Console ladder off replay

**Track:** Agentic · **Status:** ✅ Done (2026-07-26) — in-tree green, hand-off commit pending
**Read first:** `/ARCHITECTURE.md` (constitution — invariants binding), `ROADMAP.md`,
`docs/briefs/M0-trace-and-harness.md` session log, `harness/replay/NOTES.md`.

**Depends on:** the §1/§4/§7 constitution corrections from M0 being applied first (exact
text in the M0 session log). This brief assumes the corrected §4 — Anvil's adapter
synthesises `Seq` from receive order and never emits `Gap{SeqGap}`. If §4 still reads
"Anvil hands us a usable seq" when you open this, stop and apply the correction before
writing adapter code.

## Goal

The engine boundary becomes real and the first pixels (console ones) appear. `FeedEvent`
gains behaviour; an Anvil adapter turns captured frames into events; a phase-1 book adopts
snapshots and keeps a trade ring; a console ladder renders the M0 traces. At the end, the
baseline trace draws a live ladder and the reconnect trace visibly goes stale then
recovers — the first end-to-end proof of invariant 5, all on the host under ctest.

## Deliverables

1. **`DisplaySnapshot` type** — `engine/include/depthcharge/display_snapshot.hpp`: top
   ~27 levels/side, recent-trade ring (≥8), last px, status `{Live | Stale(reason)}`,
   symbol id (per ARCHITECTURE §5). Integer ticks throughout. Engine file decomposition
   is the session's call (§8) — record it.
2. **Anvil adapter** — `engine/` (name/decomposition session's call): consumes the
   verbatim Anvil frame from a trace line's `frame` field and emits `FeedEvent`s.
   - `book`/`snapshot` → `Snapshot`; per-fill → `Trade`.
   - Synthesises a monotonic `Seq` from receive order; **ignores the wire `seq` for
     ordering** and never raises `Gap{SeqGap}` (corrected §4; M0 measured 42 backward
     steps / 5 min).
   - Parses with the harness JSON choice (nlohmann, per M0). Parsing lives *inside* the
     adapter (§3): it takes bytes/string, emits events — so the firmware parser swap at
     M3+ touches only the adapter. Do **not** pick the firmware parser here.
   - No venue JSON, dialect, or seq scheme crosses the adapter→engine boundary (inv. #2).
3. **Phase-1 book** — `engine/`: "adopt latest `Snapshot`" *is* the book, plus the trade
   ring. Nothing more. **Do not build the dense window** — that lands with the first delta
   venue at M4 (§5, "do not gold-plate ahead of the first delta venue").
4. **Console ladder** — `harness/`: renders a `DisplaySnapshot` to the terminal — bids
   green, asks red, spread gap, trade prints, last px, and a **visibly distinct stale
   state**. Aesthetics are the session's call (§8); the one hard requirement is that a
   stale ladder cannot look live (inv. #5).
5. **Replay → engine driver** — extend `dc_replay` or add `dc_ladder`: feeds a trace
   through adapter → book → `DisplaySnapshot` → console. It must translate the reconnect
   trace's transport gap into a `Gap{Disconnect}` so the ladder greys then recovers on the
   next `Snapshot`. How the driver detects the reconnect from an NDJSON stream with no
   explicit disconnect marker is a known unknown below — decide and record; this rule is
   the host preview of the M3 firmware transport contract.
6. **Goldens** — `dc_tests` gains `DisplaySnapshot` expectations:
   - baseline trace → expected top-of-book / spread / trade-ring contents at a few defined
     checkpoints;
   - reconnect trace → a `Stale` state appears at the gap and clears after the next
     `Snapshot`. This assertion *is* the invariant-5 proof; M1 does not merge without it.

## Constraints

All invariants apply. Milestone-relevant:
- **#2** — `FeedEvent` is the only boundary type; adapter mess stays quarantined.
- **#3** — integer ticks; the adapter scales via per-symbol `tick_size`/`qty_step`
  (source is a known unknown below). Floats only at the console-formatting edge.
- **#5** — stale is first-class; the reconnect golden proves it.
- **#6** — no merge without replay coverage; the M0 traces are the coverage.
- **#7** — engine steady state is allocation-free after first snapshot (keeps host benches
  honest for M3). The console renderer may allocate freely — it's the display edge.
- **#8** — one writer per state. Trivially met on a single-threaded host replay; do not
  design the seam in a way that *assumes* single-threading, so M3 can slot in the real
  double buffer.

Deliberately unspecified (session decides, log records): engine file/class decomposition,
console aesthetics, whether the wait-free double buffer is built now or deferred to M3
(host replay is single-threaded — deferring is legitimate, but the `DisplaySnapshot`
producer→renderer *seam* must exist).

## Known unknowns (resolve and record)

- **Where do `tick_size`/`qty_step` for ticker-101 come from** — a frame field, the
  snapshot, or the vendored protocol? The adapter needs them to emit integer ticks. If
  they're absent from what M0 captured, that's a real gap — flag it, don't guess a scale.
- **How the driver marks the reconnect.** The M0 reconnect trace has no explicit
  disconnect marker — only a 4.47 s `rx_ns` gap followed by a fresh snapshot. Pin the rule
  (rx_ns threshold? something else) and note it previews the M3 transport's `Gap` contract.
- **Trade/book ordering is inherent, not a bug.** A `book` frame following a `trade` may
  carry pre-trade state, so the rendered ladder can momentarily lag a print that already
  flashed. At ~80 ms cadence it's invisible. The streams are independent and phase-1 adopts
  the latest — record that this is expected so no later session tries to reconcile them.
- **`summary` frames** carry a `seq` but feed Anvil's 12-ticker board mode (M7), not the
  ladder. Confirm the M1 adapter ignores them and record it.

## Definition of done

☑ `FeedEvent` exercised by tests; `DisplaySnapshot` type defined.
☑ Anvil adapter emits correct events for both M0 traces; synthesises `Seq`; no `Gap{SeqGap}`.
☑ Phase-1 book (adopt-snapshot) + trade ring; no dense window.
☑ Console ladder renders both traces; stale state visibly distinct on the reconnect trace.
☑ Goldens green including the reconnect stale-state assertion.
☑ ctest green from a clean clone (`cmake --workflow --preset host`), warnings-as-errors clean.
☑ Session log filled in; ROADMAP M1 ticked and the next milestone marked.

## Out of scope

Dense-window book (M4); Kraken/Binance adapters; any firmware, HUB75, or Wi-Fi; the real
wait-free double-buffer implementation if the session defers it to M3 (define the seam,
don't build the mechanism); `summary`/board mode (M7); order entry, persistence, web UI.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step. -->

### 2026-07-26 · Opus 5 (1M) · M1 implemented (green in-tree; hand-off commit pending)

**Done.** All six deliverables. `cmake --workflow --preset host` from a clean
`build/` → **5 ctest / 54 doctest cases / 4658 assertions**, all green,
warnings-as-errors clean (GCC 13.3, Ubuntu). New: `dc_ladder`, plus
`dc_ladder_baseline` / `dc_ladder_reconnect` ctest entries that drive both
committed traces end to end.

**Constitution corrections applied first**, as this brief required (the M0
session log flagged them; ARCHITECTURE §9 records all four):
§1 and §4 now state that Anvil's wire `seq` is a global, per-socket
non-monotonic counter and its adapter synthesises `Seq`; §4 gains `LevelSpan`
and the borrowed-snapshot lifetime rule; §4 states the declare-and-verify rule
for venues that publish no tick metadata; §4 states that `Gap{Disconnect}` is
synthesised transport-side.

**Engine decomposition** (§8 — session's call, recorded here):

| File | Owns |
| --- | --- |
| `decimal.hpp` | exact decimal-string ↔ scaled-integer, constexpr, no FP |
| `symbol.hpp` | `SymbolSpec{id, price_decimals, qty_step}` |
| `feed_event.hpp` | +`LevelSpan`, +`kMaxSnapshotLevels`, snapshot spans on `FeedEvent` |
| `display_snapshot.hpp` | `DisplaySnapshot`, `FeedStatus`, `TradePrint`, `kDisplayLevels=27`, `kTradeRingSize=8` |
| `book.hpp` | phase-1 `Book`: adopt-snapshot + trade ring + stale state machine |
| `snapshot_channel.hpp` | the publish seam (M1 single slot; M3 swaps the internals) |
| `anvil/anvil_frame.hpp` | adapter-internal decoded frame + the `parse_anvil_frame` seam |
| `anvil/anvil_adapter.hpp` | frame → `FeedEvent`, seq synthesis, stats (header-only) |
| `src/anvil/anvil_frame_nlohmann.cpp` | the **host** parse implementation |

**Decisions (with why):**

- **Snapshot levels cross the boundary as borrowed `LevelSpan`s**, valid only
  for the sink call. *Why:* the alternatives were an owning container in
  `FeedEvent` (breaks trivial copyability, invariant #7) or a second callback
  carrying the levels (breaks "FeedEvent is the only boundary type",
  invariant #2). Spans keep both. The cost is a lifetime rule — *defer means
  copy* — stated normatively in `feed_event.hpp` and obeyed by the book, which
  copies on adopt. One depth cap, `kMaxSnapshotLevels = 256`, is shared by the
  adapter staging buffer and the book, so there is exactly one truncation point.
- **The JSON parser sits behind a one-function seam, not inside the adapter's
  logic.** `parse_anvil_frame` is declared in `engine/include` and implemented
  in a separate host-only target (`dc_engine_anvil`, the only thing besides the
  harness that links nlohmann). *Why:* the brief says use nlohmann and do not
  pick the firmware parser — but nlohmann allocates per parse, which invariant
  #7 forbids on the target's feed path. Splitting the *decode* from the
  *semantics* makes the M3 swap literally one TU while seq synthesis, side
  mapping, scaling, truncation and stats stay shared, host-tested code. **This
  is the M1 → M3 debt, and it is deliberate and isolated:** the engine's own
  path (book + publish + channel) is already allocation-free and now has a test
  that proves it (`alloc_probe` replaces global `operator new`; the book applies
  1500 events and publishes 500 times with the counter unmoved).
- **`price_decimals = 4` is declared, not discovered — and verified per price.**
  *Why:* the protocol carries no tick metadata anywhere (the brief's first known
  unknown; flagged, not guessed). 4 is the measured maximum across 6,494 captured
  frames — **zero** prices with 5+ decimals — and a finer price now yields
  `BadPrice`, is counted, and drops the frame rather than rounding it. A scale
  that silently rounded would violate invariant #3 invisibly. Evidence table in
  `harness/replay/NOTES.md` (M1 addendum). Anvil backlog item: publish tick size
  in `/api/health` or a symbols endpoint.
- **Disconnect = an rx_ns hole > 1000 ms, timestamped at watchdog expiry.**
  *Why:* the rejected alternative was "a mid-stream snapshot means we
  reconnected" — retrospective, so the Gap would be raised in the same breath as
  the Snapshot that clears it and the panel would never actually be stale (a
  vacuous invariant-5 proof), and Anvil-specific besides. 1000 ms is measured:
  worst healthy inter-frame gap 640 ms across both full 5-minute captures, the
  drop 4,468 ms. Raising the Gap at `prev_rx + 1000 ms` rather than at the next
  frame's arrival is what makes the stale window **3,468 ms** instead of an
  instant — and it is exactly what an RX watchdog does, so M3 inherits the rule
  and the number.
- **A malformed frame is counted and dropped, never turned into a Gap.** *Why:*
  Anvil republishes the whole book every ~80 ms, so one lost frame self-heals
  within a refresh; greying the panel for it would be dishonest in the other
  direction. A systematic failure surfaces as a stalled ladder plus a non-zero
  `parse_errors` in the replay report.
- **A `Delta` reaching the phase-1 book marks it stale** rather than being
  ignored. *Why:* the book cannot amend a level, and silently dropping the
  amendment leaves a level on the panel the venue has just removed — a stale
  ladder that looks live. Delta support lands with the dense window at M4.
- **A Gap keeps the levels and greys them; it does not blank the book.** *Why:*
  the contract says the book is *unknown*, not empty. A blank panel asserts
  something the feed never said.
- **A trade during an outage is recorded but does not clear stale.** Tape is
  tape; only a `Snapshot` re-baselines a book.
- **The double buffer is deferred to M3, the seam is not.** `SnapshotChannel`
  exists and every replay goes through it (publish → consume by copy), so M3
  replaces the internals with a seqlock and no caller changes. What the API
  already forbids is the design mistake: no way to block the writer, no
  reference handed into producer storage, no half-written frame observable.
- **Stale is distinguished in three independent channels** — banner word, bar
  glyph (`█`→`·`), colour — so it survives `--no-color`, a monochrome terminal
  and a colour-blind reader. Asserted without colour in
  `test_console_ladder.cpp`.
- **Goldens were derived from the traces independently (a Python model), not
  recorded from this code's output.** *Why:* a golden captured from the
  implementation only pins today's behaviour. Two of them failed on first run
  and both were real bugs — `format_scaled` emitted `.09999` for values < 1, and
  an ASCII-mode assertion — which is the whole argument for doing it this way.
- **Invariant #1 is now enforced by the build, not remembered.**
  `dc_engine_header_check` generates one TU per `engine/include` header, each
  including only that header. It fails if a header stops being self-contained,
  and — since the host has no ESP-IDF headers to find — it fails the instant one
  leaks into `engine/`. Verified against a deliberate `#include
  <freertos/FreeRTOS.h>`: the build stops.

**Known unknowns — resolved** (details in `harness/replay/NOTES.md` M1
addendum): (1) tick/qty metadata **is absent from the wire**; declared in
`SymbolSpec` and verified per price. (2) Reconnect is marked by the **1000 ms RX
watchdog**, previewing the M3 transport contract. (3) Trade/book ordering lag is
**inherent** — book frames are coalesced, trades are not; do not reconcile them.
(4) `summary` frames are parsed, counted (`summary_ignored`: 181 / 172) and
**ignored**; they feed the M7 board mode.

**Goldens now pinned** (`test_replay_goldens.cpp`): frame/event counts for both
traces; top-of-book at the on-connect snapshot, at frame 700 and at end of
trace; the full 8-entry trade ring; dense monotonic `Seq` 1…1225 with **zero**
Gaps on the healthy trace while the *wire* seq goes backwards 14 times (the M0
finding re-measured through a completely different code path); and for the
reconnect trace — one stale episode, Gap at event 332 after frame 382, cleared
by the snapshot in frame 383, 4,468 ms observed / 3,468 ms grey, exactly one
stale published frame, live either side.

**Not done / carried:** the hand-off commit (owner asks); the owner's
`code-review` skill has not been run against this diff (it is user-triggered);
the firmware JSON parser choice remains open by design.

**Next session — M3 is blocked on M2 (bench).** The next *agentic* work is
either MP stage 1 (portfolio page, executes in `garethcooke-portfolio`) or M3
preparation once the panel is up. When M3 starts: implement
`parse_anvil_frame` a second time against a streaming allocation-free parser in
`firmware/`, link it instead of `dc_engine_anvil`, and require
`test_replay_goldens.cpp` to pass unchanged — that file is the acceptance test
for the parser swap. The net task implements the same 1000 ms RX watchdog beside
the real socket-close callback, and feeds `SnapshotChannel` after its internals
become a wait-free double buffer.
