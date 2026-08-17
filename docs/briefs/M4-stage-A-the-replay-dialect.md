# M4 Stage A — the replay dialect

**Track:** Agentic [A] · **Status:** ✅ Done (2026-08-17) · **Size:** one evening
**Executor:** Claude Code, **desk only. No board, no flash, no adapter, no `engine/` change.**

**This is not the Kraken adapter, and it is deliberately in front of it.** Invariant #6 is *no
merge without replay coverage*. `dc_replay` cannot read a Kraken capture today: it rejects the
metadata header on the required `ticker`, and 61 of the depth-25 slice's 1,599 records carry no
string `type`. An adapter written before the reader is an adapter that **cannot be covered**, and
the pressure at that point is to reshape the committed traces to suit the reader. Stage 0
deliberately did not reshape them. **The reader learns the wire; the wire does not learn the
reader.**

Lands as `docs/briefs/M4-stage-A-the-replay-dialect.md`.

**Read first**

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §4 | The boundary contract. `FeedEvent` is the sole adapter-boundary type and does not move this evening. |
| `ARCHITECTURE.md` §6 | Frozen. #1, #2 and #6 are load-bearing here; #6 is the reason this stage exists. |
| `ARCHITECTURE.md` §9 | Deliverable 0 appends to it before anything else is written. |
| `docs/briefs/M4-stage-0-price-the-kraken-wire.md` | The measurements this stage consumes, and the four things it said M4's brief must carry. |
| `harness/replay/NOTES-kraken.md` | The observed wire. Where this brief and the notes disagree, the notes win — they were measured. |
| `harness/replay/NOTES.md` | The Anvil equivalent, and the `rx_ns` clock addendum. |
| `docs/briefs/M0-trace-and-harness.md` | The trace contract as originally written, including why `ticker` was required. |
| `tools/tracefile.py` | The shared Python reader; already carries the two-venue error message. |
| `tools/slice_trace.py`, `tools/capture_kraken.py`, `tools/capture_anvil.py` | The writers. Whatever `venue` means, these three agree on it. |
| `docs/briefs/M1-console-ladder-off-replay.md` | Where the driver's reconnect rule was pinned, and the note that it previews the transport `Gap` contract. Deliverable 4 revisits it. |

**Depends on:** M4 stage 0 ✅ (2026-08-16). Six owner decisions taken 2026-08-17 — deliverable 0.

---

## Deliverable 0 — record the six decisions, before any code

Six decisions were taken from the planning seat on 2026-08-17. They are M4's frame and three of
them are cited by later deliverables in this brief. **Write them into `ARCHITECTURE.md` §9 first,
in the owner's words below, then start work.** A decision that exists only in a chat log has
already been lost once on this project.

1. **A refused subscription is not a new `GapReason`.** `success:false` on the subscribe ack is
   treated as a fatal transport error: `die()`, the supervisor retries, the panel greys as
   `Disconnect`. The constant case is proven impossible at compile time — a `static_assert` pins
   the subscribed depth to Kraken's offered ladder {10, 25, 100, 500, 1000}. **§4 does not move,
   and the decision not to move it is itself the §9 row**, with the counter-argument recorded:
   stage 0 held that grey-forever is honest about the wrong thing, and the answer is that a 64×64
   ladder cannot act on *unknown forever* differently from *unknown now* — the serial log names it.
2. **The clock splits in two.** Book staleness is invariant #5's business and counts **book
   events**; transport liveness is the supervisor's business and counts **bytes**. They coincide at
   Anvil, which is the only reason one constant ever worked. The threshold becomes **declared
   venue metadata** beside tick size and qty step — Anvil 1,000 ms (worst measured 391), Kraken
   ~15,000 ms (worst 9,007 on the quiet pair, p90 8,480) — not a firmware constant. The
   any-data-frame rule is rejected: at Kraken it measures the heartbeat.
3. **The trace metadata contract gains a venue tag** and one reader dispatches to per-venue
   decoders. The replay driver feeds **the venue's own adapter**, not a normalised shape, or the
   goldens stop testing the adapter. This stage.
4. **`age_ms` on `DisplaySnapshot` (§5) is taken now**, rendered numerically in the header, no
   third palette. Two reasons that were not true during M3: stage D's strain-20 criterion is
   unassertable unless *old* and *stopped* are separable states, and Kraken's 1 Hz heartbeat is a
   fixed-rate broadcast — the same denominator Anvil's 2 Hz `summary` gave the estimator — so the
   meter generalises once the expected rate is venue-declared. **Authorised, implemented in stage
   A2, not here.**
5. **M4 runs as four evening-sized stages**: A the replay dialect; B the adapter (parse, ack
   check, CRC32, deltas); C the dense-window book in `engine/`; D the bench. C is the only stage
   that touches `engine/` and does not share an evening with anything.
6. **One venue per build until M7.** Compile-time constant, default stays Anvil. The runtime
   toggle is M7's encoder, and M7 depends on M4 and M6 — not M5.

Then the ROADMAP M4 row gains a line saying stage A is the current stage and why it precedes the
adapter.

## Deliverable 1 — the metadata contract gains `venue`

- **Additive, and the four committed Anvil traces do not move.** Absent `venue` reads as
  `anvil`. If a committed trace changes by one byte, the change is wrong.
- `ticker` stops being universally required and becomes **venue-conditional**: required for
  Anvil, and whatever Kraken's captures actually carry (the pair, presumably — read it from the
  slices, do not assume).
- The rule goes in one place and is stated in prose in `harness/replay/NOTES.md`, because it is
  now a contract two venues and two languages share.
- **Both writers emit it** — `capture_anvil.py` and `capture_kraken.py` — and `slice_trace.py`
  carries it through a slice unchanged.
- **The `rx_ns` clock is surfaced, not assumed.** `capture_kraken.py` records `perf_counter_ns`
  and `capture_anvil.py` deliberately kept `monotonic_ns`; the reader must expose which clock a
  trace used rather than let a caller compare gaps across the two silently. If the field is
  already there, surface it; if it is not, add it and default it honestly.

## Deliverable 2 — the reader dispatches; the driver feeds the venue's adapter

**Seam only. No Kraken adapter this evening.**

- The C++ trace reader under `harness/` learns the venue tag and routes records to a per-venue
  decoder. Anvil's path is unchanged in behaviour and in output.
- **The decoder/sink shape is pinned with `static_assert`s on the signature, not a `concept`** —
  strain 3's decision from stage 0, and this is the first place it applies. If the assertion
  cannot be written without contortion, stop and report rather than reaching for the concept.
- Kraken's decoder this evening is a **classifier, not an adapter**: it reads each record, names
  its kind, counts it, and emits **no `FeedEvent`s**. `FeedEvent` does not move and no adapter is
  written.
- `dc_replay` runs all four committed Kraken slices end to end without error and prints the
  per-kind counts. That is the stage's provable end state.

## Deliverable 3 — the record taxonomy, measured rather than assumed

The 61 untyped records in the depth-25 slice are the whole reason this deliverable exists.

- For each of the four committed slices: total records, and a count per kind, with the untyped
  ones **explained** — subscribe acks, heartbeats, status frames, or something stage 0 did not
  name. Read them out of the files.
- Write the taxonomy into `harness/replay/NOTES-kraken.md` beside the existing observations.
- **New ctest cases pin the counts per slice**, in the style of `kraken_tool_selfcheck`:
  add-rows-only, and a pin that refuses to overwrite an existing one. A count that moves means a
  reader change altered what a committed file means, which is exactly the alarm worth having.

## Deliverable 4 — the driver's disconnect rule, or it lies at Kraken

M1 pinned the replay driver's reconnect detection to an `rx_ns` gap threshold, derived from
Anvil, where the worst healthy gap is 391 ms. **Kraken's quiet pair is legitimately silent for
9,007 ms and its p90 is 8,480 ms**, so the M1 rule applied to a Kraken trace synthesises
disconnects that never happened — and would then be pinned into a golden as truth.

- Make the threshold **venue-declared**, from the same source of truth deliverable 1 establishes.
- This is decision 2 arriving in the harness before it arrives in the firmware, and it should be
  written up that way: the replay driver is the cheapest place to discover the split clock was
  right or wrong.
- **Do not touch `kRxWatchdogMs` or any firmware constant.** The firmware half of decision 2
  lands in stage B with the venue metadata table. This deliverable is the harness half only.

## Deliverable 5 — prove Anvil did not move, the way stage 0 proved it

Stage 0's method, because it is the one that catches this class of change:

- Run the pre- and post-change readers over all four committed Anvil traces and diff the outputs
  byte for byte, normalising nothing that is not a clock or a date.
- `cmake --workflow --preset host` green; the existing goldens **unchanged**, not merely passing.
  A moved golden means stop.

## Deliverable 6 — writeback

Session log here. ROADMAP M4 row gains the stage A line. `docs/DESIGN.html`: the trace metadata
contract is a boundary, so §09's triggers have fired — update the trace-layer picture and open or
extend a strain row for the two-venue reader. DESIGN loses to ARCHITECTURE on any disagreement.

---

## Constraints

- **All of §6, frozen.** Nothing in `engine/`, nothing in `firmware/`, no adapter, no
  `FeedEvent` change, no golden moved. New goldens may be **added**.
- **Do not reshape the committed Kraken traces.** If the reader cannot read one, the reader is
  the thing that changes. Stage 0 left them alone on purpose.
- Python lives in `tools/` only, stdlib only.
- Full captures stay untracked; committed slices stay as committed.
- Host build and ctest green from PowerShell — the Bash sandbox breaks compilers silently on
  this machine.
- **Commit only when asked.** Report what changed and what it measured; Gareth decides.
- Do not reopen: the depth (25, `kDisplayLevels` stays 27), strain 3 (`static_assert`s), the
  watchdog constants, the panel orientation.

## Known unknowns — resolve and record

What the 61 untyped records actually are, and whether the other three slices carry the same kinds
in different proportions. Whether Kraken's captures carry a pair identifier in metadata at all,
or whether it has to be inferred from the subscribe record. Whether the existing reader's error
paths can distinguish "not a DepthCharge capture" from "a capture of a venue this build does not
know" — they are different failures and only one is a bug. Whether the venue-declared gap
threshold belongs in the trace metadata, in the driver's CLI, or in a venue table the harness and
firmware will eventually share — **name the choice and its cost; do not build the shared table
this evening.**

## Definition of done

- [x] Six §9 rows written before any code, in the owner's words.
- [x] `venue` in the metadata contract, additive, absent = anvil; both writers emit it;
      `slice_trace.py` carries it through.
- [x] The four committed Anvil traces are **byte-identical** through the changed readers, proven
      by diff rather than asserted.
- [x] `dc_replay` reads all four committed Kraken slices end to end and prints per-kind counts.
- [x] The record taxonomy written into `NOTES-kraken.md`, with the untyped records explained.
- [x] New ctest cases pin the per-slice counts; add-rows-only; pin mode refuses to overwrite.
- [x] The decoder/sink shape pinned with `static_assert`s.
- [x] The driver's reconnect threshold is venue-declared and a Kraken slice's legitimate silences
      no longer read as disconnects — demonstrated on the quiet pair.
- [x] Host build and ctest green; existing goldens unchanged.
- [x] Session log; ROADMAP M4 line; DESIGN trace-layer update.

## Out of scope

The Kraken adapter, `FeedEvent` emission, CRC32, deltas, the dense-window book, re-anchoring
(stage B and C). `age_ms` and the §5 change (stage A2). The venue metadata *table* the firmware
will consume (stage B). Any firmware, any board, any flash. `kRxWatchdogMs` and every other
watchdog constant. `kDisplayLevels`. Binance (M5). The runtime venue toggle (M7). The M4 brief's
own DoD, which stages A–D roll up into and which is written when stage B's shape is known.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     measured figures / evidence · exact next step. -->

### 2026-08-17 · Claude Opus 5 (1M) · stage A executed end to end

**Done.** All ten DoD boxes. Nothing in `engine/`, nothing in `firmware/`, no adapter,
no `FeedEvent` change, no golden moved, no committed trace touched. No board, no flash.
Host build and ctest green: **21/21**, up from 16 — five new cases
(`dc_replay_kraken_btcusd_{d10,d25,d100}`, `dc_replay_kraken_quiet_pair`,
`trace_taxonomy_selfcheck`) plus 14 new doctest cases / 72 assertions in
`test_trace_venue.cpp`.

**Build note for the next session:** this box's `build/host` is configured with the
MinGW generator, so the loop here is `cmake --workflow --preset host-mingw`. The plain
`host` preset errors on the generator mismatch rather than reconfiguring.

**Deliverable 0 first, before any code**, as instructed. Six rows in `ARCHITECTURE.md`
§9 dated 2026-08-17, in the owner's words, each with its counter-argument kept. A
**seventh** row was added at the end of the session and is marked `(from the work)` —
it records what the implementation forced rather than what was decided in advance, and
it is described under "what the work found" below.

---

#### What shipped

| Deliverable | What landed |
| --- | --- |
| 1 · `venue` in the metadata | `TraceMeta` gains `venue` / `symbol` / `depth` / `clock`, all optional; absent `venue` = `anvil`. `capture_anvil.py` now emits `venue` and `clock`; `capture_kraken.py` already did. `slice_trace.py` carries the header through verbatim. The rule is one table per language — `harness/include/dc_harness/venue.hpp` and `tools/tracefile.py` — stated in prose in `NOTES.md`. |
| 2 · reader dispatches | `TraceReader` owns the envelope only. `trace_decoder.hpp` carries `SinkContract` + `DecoderContract` (`static_assert`s, no `concept`), `AnvilTraceDecoder` (wraps `AnvilAdapter`; the replay driver now holds one) and `KrakenTraceDecoder` (classifier: names, counts, emits nothing). `dc_replay` reads all four Kraken slices; the driver refuses a non-Anvil trace loudly. |
| 3 · the taxonomy | New `dc_taxonomy` with `--selfcheck` / `--pin`, an add-rows-only table in `taxonomy_pins.inc`, and `trace_taxonomy_selfcheck` in ctest over **all eight** committed traces. Taxonomy written into `NOTES-kraken.md`. |
| 4 · venue-declared threshold | `ReplayOptions::disconnect_gap_ms` is now *read from* `venue_traits(Anvil).stale_gap_ms`; `for_venue()` / `for_trace()` supply a venue's. Anvil 1,000 ms, Kraken 15,000 ms. `kRxWatchdogMs` untouched. |
| 5 · Anvil did not move | 12 outputs (`dc_replay`, `dc_replay_streaming`, `dc_ladder` × 4 traces) diffed pre/post: **0 differing**. All 8 committed traces byte-identical by SHA-256. `kraken_tool_selfcheck` unchanged. |
| 6 · writeback | This log, the ROADMAP M4 stage-A line, `docs/DESIGN.html` (§06 prose + class diagram, strain 3 updated, **strain 22 opened**, status strip, §09 changelog). |

---

#### The measurements, because deliverable 4 is a claim about numbers

**The record taxonomy, and the 61 are explained.** Depth-25 slice, 1,599 records:
1,536 `book/update` · 59 `heartbeat` · 1 `book/snapshot` · 1 `status/update` ·
1 `ack:subscribe` · 1 `tx:subscribe`. The 61 untyped are **59 heartbeats + the
subscribe ack + the subscribe this side sent**. The other three slices read 62,
because their window caught 60 heartbeats rather than 59 — the 1 Hz broadcast and the
window edge, nothing else. Every record is accounted for on all four slices.

**The correction that fell out of reading them:** `NOTES-kraken.md` said the untyped
three were the heartbeat, the ack and the `status` frame. `status` carries
`{"channel":"status","type":"update"}` and is typed. The **count** was right and one of
the three **names** was not — recorded there rather than silently fixed.

**Deliverable 4, the numbers.** Watchdog firings the M1 rule produces over the committed
slices, at Anvil's 1,000 ms and at Kraken's declared 15,000 ms:

| slice | worst record gap | worst book gap | record rule @1,000 | book rule @1,000 | either @15,000 |
| --- | ---: | ---: | ---: | ---: | ---: |
| BTC/USD d10 | 1,004.6 ms | 5,057.6 ms | 7 | 13 | **0** |
| BTC/USD d25 | 1,002.9 ms | 4,534.6 ms | 5 | 10 | **0** |
| BTC/USD d100 | 998.1 ms | 1,370.7 ms | **0** | 3 | **0** |
| MINA/GBP d25 | 1,026.2 ms | 9,006.8 ms | **25** | 12 | **0** |

Every firing in the middle two columns is a disconnect that did not happen. **Read the
record-rule column across the rows: 7, 5, 0, 25 — and all four worst record gaps are
within 27 ms of one second.** Whether Anvil's constant trips on a Kraken trace is
heartbeat jitter, not signal; d100 comes in at 998.1 ms and looks clean, and the same
subscription an hour later would not. That is stage 0's "straddles the constant"
sentence arriving as four numbers, and it is the evidence for decision 2's rejection of
the any-data-frame rule in the only form that can be checked.

**Two implementations, one answer.** Every Kraken figure above was produced
independently in Python before the C++ was trusted, over the same files, with the same
kind names — 1,599 / 1,536 / 59, 25 and 12 on the quiet pair, 9,006.8 ms. They agree
exactly. The four Anvil taxonomy rows agree with pins `dc_tests.cpp` has held since M0
(1406 / 1088 / 1 / 136 / 181 and 1288 / 1012 / 1 / 103 / 172), which is an independent
check nobody had to write.

---

#### What the work found — the seventh §9 row

**The resync rule was written wrong twice, in opposite directions, before the right
question was asked.** It is worth the space because the failure mode is this project's
own.

`mid_stream_snapshots` meant "a snapshot that is not the trace's first record", which is
Anvil's shape, not a general one — Kraken's on-connect snapshot arrives *third* (status,
ack, snapshot), so that rule calls **every** Kraken capture a reconnect. Repair one,
"the first snapshot after a subscribe is on-connect", fixes that and breaks the
reconnect capture, whose second subscription's snapshot *is* the resync. Repair two,
"not the first acked subscription", fixes that and breaks a **windowed** reconnect
slice, which carries only one ack. Each repair was venue-shaped, plausible, and wrong
somewhere else; each was caught only by running it against a real artefact
(`_local/kraken_btcusd_d25_reconnect_20260816.full.ndjson`, and then a window cut from
it).

The rule that works needs no venue split at all: **a snapshot is a resync exactly when a
book event preceded it in the trace.** It is the question the concept actually names — a
snapshot re-baselines a book, so it is a resync when there was a book to re-baseline —
and it reproduces Anvil's existing answers on all four committed traces, including the
reconnect *window* that contains no on-connect snapshot. It also removed state from the
Kraken decoder rather than adding any. The same rule now cuts the window in
`slice_trace.py`, so the tool that makes a trace and the reader that counts what is in
it cannot disagree about the same file.

General form, recorded in §9 because this milestone has now paid for it twice: **a rule
written per venue is a rule written twice, and the second writing is where they diverge
— before adding a venue branch, check whether the general question has a venue-free
answer.**

The same row narrows ARCHITECTURE §9's 2026-08-07 "one definition of a valid trace" in
exactly two places (`ticker` and the string `type` become venue-conditional) and states
plainly that everything else in it stands. That row is cited elsewhere as though it were
unconditional, and a future session reading it alone would conclude a Kraken trace is
malformed.

---

#### Decisions taken in-session, with why

1. **The venue-declared threshold lives in a harness table, not in trace metadata and
   not in the CLI** — the known unknown the brief asked to be named. Metadata was
   rejected because a capture would then declare the policy it is judged by, so
   re-capturing could move a golden with no code change, and a trace could assert a
   threshold that hides the silence it was captured to show. The CLI was rejected
   because every caller would carry a copy and a golden's meaning would depend on its
   invocation — the shape §9 already paid for once with two trace readers. **Stated
   cost:** the harness and the firmware now hold 1,000 ms in two files with nothing
   that can tell if they diverge, until stage B merges them. It is the first item on
   strain 22.
2. **`dc_replay`'s Anvil report is byte-identical, so the venue / clock / watchdog
   figures go in `dc_taxonomy` instead.** Adding a line to the Anvil report would have
   made deliverable 5's proof an argument rather than a diff. The new information is one
   command away and `dc_taxonomy` has no baseline to preserve.
3. **An absent `clock` reads as `undeclared`, not as `monotonic_ns`.** The inference is
   sound today — one tool wrote every undeclared trace in the repo — and it is exactly
   the kind of sound inference this project has been bitten by. Both writers declare it
   from today, so `undeclared` means "captured before 2026-08-17".
4. **`noexcept` is not asserted on the sink contract**, though stage 0's write-up
   floated it: the only real sink in the tree pushes onto a `std::vector` and calls a
   `std::function` observer, so the assertion would either reject the live path or turn
   a throw into `std::terminate` on the desk.
5. **`UnknownVenueError` is its own type.** The brief's known unknown — whether the
   reader can tell "not a DepthCharge capture" from "a capture of a venue this build
   does not know" — answered *no*, and is fixed. `dc_replay` and `dc_taxonomy` exit 2
   for the second.
6. **The taxonomy pins cover the four Anvil traces as well as the four Kraken ones.**
   Stage A's other claim is that Anvil did not move; a pin says so on every build, where
   a diff has to be remembered.

**One small thing outside the brief's letter, flagged rather than buried:**
`slice_trace.py --mode reconnect` was venue-blind and would have cut a Kraken window
centred on the connection's own opening snapshot — a file named `reconnect` with no
reconnect in it. Fixed with the same rule the reader uses. It was not in scope; it was
~15 lines, and stage B is the session likely to want a Kraken reconnect slice.

---

#### Mutation-verified, not assumed

The pin table is new, so the stage-0 procedure's step 1 ("prove the existing rows still
pass") was vacuous and was replaced by proving the check **bites**. Four deliberate
breakages, each built and run:

| mutant | caught? |
| --- | --- |
| Kraken `heartbeat` named `?` | yes — 4 traces fail |
| the sent subscribe counted as venue traffic | yes — 4 traces fail |
| Kraken's declared threshold 15,000 → 8,000 ms | yes — 1 trace fails: the quiet pair, whose 9,007 ms book gap crosses 8,000 |
| the resync rule dropped back to the index test | yes — 4 traces fail |

The clean table passes. Separately, `test_trace_venue.cpp` was mutation-checked (absent
venue tag → `kraken`): 4 of its 14 cases fail. Neither suite is vacuous.

---

#### Known unknowns, resolved

- **What the 61 untyped records are** — 59 heartbeats + ack + our own subscribe, and the
  other three slices carry 62 for the window reason above. Measured, pinned.
- **Whether Kraken captures carry a pair identifier in metadata** — yes, `symbol`,
  written by `capture_kraken.py` from `--symbol`. It did not have to be inferred from the
  subscribe record, and `depth` is there too.
- **Whether the reader can distinguish "not a capture" from "a venue I do not know"** —
  it could not; `UnknownVenueError` and exit code 2 now do.
- **Where the venue-declared threshold belongs** — decision 1 above, with its cost.

#### Still open, and named rather than left

- **The venue table duplicates `kRxWatchdogMs`** with nothing checking they agree. Stage
  B, by instruction.
- **Adding a venue is four edits in two languages and only three fail loudly** — the
  Python `VENUES` row is the silent one. M5 finds out whether that costs anything.
- **The driver's loud refusal becomes a silent dispatch at stage B.** Nothing yet checks
  that a Kraken golden was produced by the Kraken adapter. Strain 22 says stage B should
  not ship without one.
- **`data` never held more than one entry** in any captured Kraken message, so "one
  symbol per message" is still an observation and not a property — carried forward from
  stage 0 unchanged, and the classifier does not depend on it.

**Exact next step.** Stage B: the Kraken adapter. `KrakenTraceDecoder::decode` is the
function to fill in — its signature is already the one the contract requires and its call
sites already exist. It brings with it the ack `success` check (fatal, per decision 1,
with the depth whitelist as a `static_assert`), CRC32 over the top 10 from the verbatim
token text, delta application, and the venue metadata *table* the firmware consumes —
which is where `venue.hpp`'s two rows and `kRxWatchdogMs` become one thing. The
truncation goldens are named and waiting: `kraken_btcusd_d10_20260816.ndjson` and
`kraken_btcusd_d25_20260816.ndjson`, and the criterion is the measurement, not the depth.

---

### Owed by stage B

Written down at the owner's review of stage A (2026-08-17), **before stage B
exists**, because both items are things a stage-B session would otherwise have
to rediscover — and one of them is a silent-wrong-answer path rather than a
gap in coverage.

**1 · The dispatch has to prove it dispatched right.** Strain 22's third clause
carries this as its stated closing condition, in these words:

> **A Kraken golden fails if it was produced by the Anvil parser. The decoder's
> identity travels with the pinned output, and a mutation test that swaps the
> dispatch expects red.**

Today the replay driver *refuses* a non-Anvil trace, which is loud and safe
because there is exactly one adapter. The moment stage B links a second, that
refusal becomes a **dispatch**, and a dispatch that picks wrong produces a
golden derived from the wrong parser rather than an exception. Note what the
sentence asks for beyond a passing test: the decoder's identity must be **in the
pinned artefact**, not merely known to the harness at run time — the failure
being guarded against is a golden that looks right. And it names the *mutation*,
not the assertion, because a pin nobody has broken on purpose is a pin nobody
has checked.

**2 · Stage B's CRC-mismatch path is the first thing that exercises the resync
rule at Kraken, and it needs a trace that does not exist yet.** Task 1 of this
review established that **no committed trace exercises the positive branch of
the resync rule at Kraken** — all four slices score resync 0, by design, because
stage 0's capture recipe deliberately excluded healing events. So stage B needs
a **deliberately captured Kraken resync slice, and it cannot be one of the four
truncation traces.**

The two capture purposes are **mutually exclusive and cannot share a file**, and
this is not a preference — it is `ARCHITECTURE.md` §9's 2026-08-16 (stage 0) rule
arriving where it was predicted to: *a trace that contains the system's own
healing events measures recovery, not the defect.* A resync calls `Book.replace`,
which clears every level, so it **heals a non-truncating book outright** — a
truncation golden containing a resync scores 840/840 clean and detects nothing.
The reconnect capture already in `_local/` demonstrates exactly that. Therefore:

| purpose | requirement |
| --- | --- |
| truncation golden | liquid pair, shallowest offered depth, **no resync in the window** — `kraken_btcusd_d10` and `kraken_btcusd_d25`, both already committed and both verified CATCHES |
| resync golden | **at least one resync in the window**, which makes it useless as a truncation golden and must therefore be a separate committed file |

Until that second file exists, the Kraken positive branch is covered only by the
three synthetic host cases added in `test_trace_venue.cpp` at this review
(snapshot-after-updates, snapshot-before-any-book-event, and snapshot-then-
snapshot), which is the right coverage for a *rule* and is not coverage for an
*adapter*. Invariant #6 will want the file.

A usable one can be cut today: `slice_trace.py --mode reconnect` now finds a
Kraken resync correctly (fixed at stage A, verified at this review against
`_local/kraken_btcusd_d25_reconnect_20260816.full.ndjson`, which yields a 758-record
window scoring **resync 1**). It was deliberately **not committed** here, because
committing a golden with no adapter to test against it is committing a file whose
expectations nobody can derive.

#### Added by the 2026-08-17 ruling — constraints on stage A2 and stage B

**4 · `age_ms` is a SLIDING-WINDOW deficit, not a cumulative one.** This is a
constraint on stage A2's code, written before that code exists, because the
obvious implementation is wrong here in a way that reads as healthy.

Anvil **queues and never drops per socket** — no cap, no drop policy, no
per-socket coalescing (`CrowWsSubscriber::deliver()` → the connection's private
`write_buffers_`). So a backlogged socket receives *every* frame, late. A
cumulative expected-versus-received count therefore reads **zero deficit through
a 111-second backlog**: nothing was ever missed, it simply all arrived two
minutes after it was generated. The measurement exists — 111 s of lag over 150 s,
by seq-matching a throttled socket against an unthrottled one
(`tools/anvil_freshness_probe.py`, ARCHITECTURE §9 2026-08-11) — and a cumulative
estimator is blind to all of it.

The window must be sized to catch **stall-then-burst** rather than average it
away: the failure shape is a pause followed by a flood that repays the count, and
any window long enough to contain both reports nothing wrong.

**5 · Freshness needs the client ping; cadence cannot supply it.** Arrival proves
the server was alive **when the frame was generated**, not that the frame is
fresh. The liveness clock this ruling installs is a cadence measurement and is
honest only about generation-time liveness — that limit is in the ruling's own
point 7 and is not a defect to be fixed by tuning it.

What closes the gap is already vendored and already measured: Anvil's Crow
answers a client PING **unconditionally, with no application code**, and appends
the pong to the **same per-connection `write_buffers_` as data**, so a pong
cannot overtake a backlog. A round trip therefore prices this socket's
server-side send queue — which is exactly the property `age_ms` needs and the one
cadence structurally cannot provide. `PingProbe` already exists
(`firmware/src/ws_ping.hpp`, D5) and already prints on a `-- ping` line under
`-- age`; what is owed is *using* the two together rather than reading them
side by side.

**6 · One line for the README's *why this exists*** — not a brief item, and
recorded here so it is not lost: **had the panel existed on 16 August it would
have shown A2b's incident for two minutes fifty-six seconds.**

**7 · Carried forward unchanged**, from the earlier list and still owed:

- the **Kraken-golden-from-the-Anvil-parser** mutation test (item 1 above);
- the **deliberately captured resync slice** that is not one of the four
  truncation traces (item 2 above);
- **stage D's criterion**, now sharpened by the ruling into a sentence with two
  halves that must both hold in one sitting: *the quiet pair holds colour through
  26 s of legitimate book silence, and greys within the liveness threshold of the
  heartbeat stopping.* Note the first half is now a measured 25,843 ms rather
  than the 9,007 ms stage 0 believed;
- **`age_ms` must render to at least minutes.** `staleness.hpp`'s 64-bit fix
  (2026-08-16 D5) is the precedent and the reason: the previous `SecondsText`
  saturated at 71.6 minutes and three of four soak segments hit it.
- the **defensive resubscribe**, named and deliberately not built
  (`NOTES-kraken.md`): its trigger would be book silence, which the ruling
  establishes carries no information, so any version worth having needs a second
  signal distinguishing *quiet* from *dead* — and Kraken publishes none.

**8 · `age_ms`'s DEFINITION, pinned before stage A2 writes a line of it.** Three
candidate meanings were treated as one because at Anvil-with-a-busy-feeder they
coincide. They diverge by orders of magnitude — time since the last book *frame*
(~80 ms at Anvil, always), time since the book last *changed* (unbounded at both
venues), and estimated *queuing lag* (zero until a socket backlogs, then 111 s).
The definition, in the owner's words:

> `age_ms` is the estimated age of the displayed book relative to the venue's
> current state — that is, queuing lag. It is not time since the last frame, and
> it is not time since the book last changed. A book that has not changed is not
> old: at MINA/GBP nobody traded, and the displayed book was exactly correct
> throughout 25,843 ms of silence. Time since the last change is market
> information and may be displayed, but it is not age and must never drive a
> rendered state.

That keeps `age_ms` venue-free and consistent with the ruling: both venues
estimate it from a windowed deficit against the liveness signal, and neither
reads it off the book.

**9 · The coverage gap that goes with it, stated as a gap rather than left
implied.** *Age grows while liveness holds* has an Anvil form — a **backlogged
socket** — and **no committed trace can contain one, because a backlog is a
property of the client's socket rather than of the wire.** Two sockets reading
the same server at the same instant disagree about how old the book is, and
`rx_ns` records only when *this* client was handed the bytes; the lag lives
between the server's clock and ours, and a trace has neither.
`_local/drain-120ms.ndjson` is a throttled capture and still does not hold the
answer — the 111 s figure had to be computed against a second, unthrottled socket
(`tools/anvil_freshness_probe.py`).

**This is a known uncoverable-by-capture case**, which means invariant #6 cannot
be satisfied for it by a golden and stage A2 must say so rather than appear
covered. It is also the direct reason the estimator must be **windowed**: a
cumulative expected-versus-received count reads **zero deficit** through the
whole backlog, because nothing was ever missed — it all arrived, late. Full
statement in `NOTES.md`, "The correction, in the terms the ruling wants it
recorded".

#### Three records the mid-stream slice creates (2026-08-17, stage A close-out)

`kraken_minagbp_d25_20260817.ndjson` begins 65 s into a 600 s capture. That is the
correct shape for it — the window could not be moved without cutting the fourth of
its four >12 s gaps — and it makes three previously abstract things concrete.
**All three are records. None is implemented here.**

**10 · The adapter must tolerate never having seen a subscribe ack.**

> **Absence of a subscribe is not failure of a subscribe, and the replay path must
> be able to enter the stream already subscribed.**

Decision 1 (ARCHITECTURE §9, 2026-08-17) makes `success:false` on the ack fatal:
`die()`, the supervisor retries, the panel greys as `Disconnect`. A mid-stream
trace carries **no subscribe at all**, which is a **third case** — neither success
nor failure — and an adapter whose state machine requires having seen an ack will
reject the extreme slice **at exactly the moment it is most needed**, since that
slice is the golden for the case the staleness ruling exists to serve.

This does **not** reopen decision 1. It is about *initial state*, not about
refusal: a refused subscribe is still fatal, and an absent one is still a legal way
for a replay to begin. The distinction is one line in a state machine and a
guaranteed defect if it is discovered at debugging time instead of design time.

**11 · "Healthy feed, no snapshot yet" now has a trace, and stage B decides what it
renders.**

The extreme slice contains no snapshot, so a book built from it **never
initialises**, while the liveness signal stays healthy from the first record to the
last — 95 heartbeats, median 999.1 ms, zero greys. That combination was
hypothetical when the vocabulary question was declined; **it is now a file**:
`harness/replay/kraken_minagbp_d25_20260817.ndjson`.

The question is what the panel shows in that state, and there are two defensible
answers: **an empty ladder that is honestly empty** (the feed is fine, we simply
have no book yet — which is what the feeder-off Anvil trace already renders, and
which §4's *"depth beyond N is unknown, not zero"* argues for), or **grey** (we
cannot draw a book we have never had, and colour would imply we can).

**Deliberately not settled here.** It is a rendering decision that wants the panel
rather than the console: the two options differ in how they look at desk distance
and not in any property a host test can assert. Stage B or the bench, with the
trace named above.

**12 · Synthesised snapshots for sliced traces — named and REJECTED, so it is not
re-proposed.**

The obvious way to make windowed slices book-gradeable is to replay the capture's
prefix, compute the book state at the window's start, and write that computed
snapshot into the slice. It would make `kraken_frame_economics.py` able to check
every checksum in the extreme slice, and it is wrong:

> **A trace is wire truth. A derived baseline would be the first thing in
> `harness/replay/` that no venue ever sent.**

Every committed trace is bytes a server actually transmitted, spliced in verbatim
and never re-serialised — that property is why the CRC32 work is possible at all
(stage 0 measured 0/2,786 checksums surviving a float round-trip), and why the
capture tools go to the trouble of splicing raw text. A synthesised frame breaks
it for the whole directory, not just for the file carrying one: a reader could no
longer assume any given line came off a socket.

`NOT_A_CHECKSUM_GOLDEN` is the honest answer to the same problem — it says *this
file cannot be graded, and here is why* instead of manufacturing the input that
would let it be. **Written down here because someone who finds that exclusion
table will read it as a gap and propose exactly this fix.**
