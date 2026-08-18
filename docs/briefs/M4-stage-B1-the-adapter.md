# M4 Stage B1 — the adapter

**Track:** Agentic [A] · **Status:** **Part A done 2026-08-18; Part B reassigned to D's first act** · **Size:** one evening, with a stated cut point
**Executor:** Claude Code, **desk only. No board, no flash, no CRC, no resync, no dense-window
book, no panel decisions.**

This is the evening the second adapter links. Three things that have been theoretical since stage
0 become real in the same sitting: a golden can be produced by the wrong parser, a decimal string
has to become an integer tick without a float touching it, and `run_replay`'s guard — which has
been making two latent defects look loud — comes off.

**Read first**

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §4 | `FeedEvent` is the sole adapter-boundary type and does not move. Kraken emits `FeedEvent`s or it does not ship. |
| `ARCHITECTURE.md` §6 | #1, #2, #4 (integer ticks), #6, allocation-free steady state, single writer per state. Every one of them is in the blast radius tonight. |
| `ARCHITECTURE.md` §9 | The staleness ruling, the M4 triage rows, the A2 rows. Deliverable 0 appends to it. |
| `docs/briefs/M4-triage-of-the-twelve.md` | B1's scope, and the three decisions closed with reasons. |
| `harness/replay/NOTES-kraken.md` | The wire, the taxonomy, the depth refusal, the options named and not taken. |
| `docs/vendor/` snapshot at `4801ed8` | The Anvil contract, current. |
| The A2 session log | The baseline-latch defect and the burst measurement. B1 inherits the clocks it fixed. |

**Depends on:** stage A ✅, stage A2 ✅. **Blocks:** B2 (CRC, resync), C, D.

---

## Deliverable 0 — one §9 row, before any code

Three checks on this project have now passed for the wrong reason: `ok_never_truncating <
checksummed` reading `0 < 49`, true only because everything failed; fifteen goldens pinning
`legacy_anvil()` so the shipped path had no coverage at all; and a per-commit worktree
verification that was building the main tree. Write the general form:

> **A green check must be shown capable of going red.** Mutation-verification is not a per-case
> habit applied to interesting tests — it is the standing condition for a test being counted as
> coverage. A check that has never been seen to fail is a claim about the world, not evidence
> about the code.

## Part A — the adapter, host-side *(this is the evening)*

### 1 · Parse, into integer ticks, with no float in the path

Kraken sends decimal strings. §6 says integer ticks everywhere. **Convert the decimal string
directly to an integer tick — do not parse to `double` and multiply.** Precedent:
`anvil_frame_economics.py` measured Python's float repr and printed it as wire bytes, a −1.73%
error that looked like data.

- The survey found scaling paths that are correct only because `qty_step` has been 1 at every
  venue consumed so far. **Kraken's is not 1.** Those paths go live tonight; fix them as part of
  this deliverable rather than discovering them at the panel.
- Tick size and qty step come from the venue table, not from constants at the call site.
- A property test: string → ticks → string round-trips for every price and qty in all five
  committed Kraken slices, exactly.

### 2 · Subscribe-ack handling, including the absence case

- `success:false` is a fatal transport error: `die()`, supervisor retries, log names it. Decision
  1 stands, §4 does not move.
- The depth constant is pinned to Kraken's offered ladder {10, 25, 100, 500, 1000} by
  `static_assert`.
- **Absence of a subscribe is not failure of a subscribe** (owed item 10). The extreme slice
  begins mid-stream and carries no subscribe at all. The state machine must be able to enter a
  stream already subscribed, or the trace that proves the staleness ruling becomes unusable at
  exactly the moment it matters.

### 3 · Deltas onto the phase-1 book

- Snapshot replaces; update applies. `qty` of zero removes a level.
- Depth 25 against `kDisplayLevels` 27 — the two unfilled rows render **unknown, not zero**, as
  decided at stage 0.
- **The `checksum` field is parsed and carried, not verified.** Verification is B2. Carrying it
  now means B2 adds a check rather than a parse.

### 4 · The dispatch must prove it dispatched right (owed item 1)

The hole opens tonight, not at B2: two adapters, one reader, and nothing yet ties a pinned output
to the code that produced it.

- Decoder identity travels with the pinned output.
- A mutation swapping the dispatch **expects red**, and is run, not just written.

### 5 · Allocation-free steady state

The invariant most likely to be broken silently tonight is the one a JSON library breaks for you.
Parse in place over the frame buffer: no `std::string`, no dynamic containers, nothing that
allocates per frame. If the chosen approach cannot make that claim, **stop and report** rather
than shipping a host-green adapter that cannot run on the target.

### 6 · Replay coverage (invariant #6)

All five committed Kraken slices replay through the real adapter and emit `FeedEvent`s. The
extreme slice exercises deliverable 2's absence case. Goldens added, not moved; pins add rows only.

### 7 · The queue-vs-shed premise is recorded, not assumed

`age_ms` is venue-free code and runs at Kraken tonight. But a deficit is an age **only if the
venue queues**; Anvil's queueing is measured and now contractual, Kraken's is neither. Do not
special-case Kraken and do not suppress the number — **pin a test that records the assumption
explicitly**, naming B2 as its owner. B2 builds the CRC path, and shed updates break the checksum,
so **CRC mismatch under induced backpressure is the queue-vs-shed experiment**. That converts an
open assumption into a measurement rather than a caveat carried to M5.

**Cut point.** If the evening ends here, it ends well. Part B is a clean second sitting.

## Part B — the firmware lift *(rides if there is room; otherwise B1b)*

### 8 · `staleness.hpp` is deleted, not extended

A2 left host and target computing the book's age by different code, invisible to every host test.
That divergence cannot be closed by testing — only by deletion.

- Lift `age_estimator.hpp` and `liveness_clock.hpp` across.
- Delete `firmware/src/staleness.hpp`, and with it `AgeText`'s duplication of `SecondsText`.
- Delete `kRxWatchdogMs` (owed item 9) — the venue table no longer holds a duration, so this is a
  deletion rather than a reconciliation.

### 9 · The guard comes off

`run_replay`'s refusal has been making two defects look loud. Removing it makes them live in the
same evening, which is why they are here and not in a survey backlog: `symbol_for()`, and
`console_ladder`'s hardcoded `" ANVIL "` and raw-tick qty.

---

## Constraints

- §4 frozen: no new `FeedEvent` variant, no new `GapReason`. If the wire seems to need one, **stop
  and raise** — that is the third time this question would have been asked, and the answer has
  been no twice.
- §6 frozen. `engine/` changes only if the dense-window book is *not* what you are reaching for —
  it is stage C, and B1 uses the phase-1 book as it stands.
- No CRC verification, no resync, no capture, no panel work, no depth change.
- Run the code-review skill against the proposed commit split **before** proposing it. That is now
  standing practice, not a request per stage.
- Per-commit verification happens in a detached worktree, and **confirm `CMAKE_HOME_DIRECTORY`
  points at the worktree** before believing a pass.
- Delete the stale worktree at `C:/tmp/dc-verify` (detached `a32ff59`). A stale worktree is
  precisely where a green build comes from the wrong tree.
- `cmake --workflow --preset host-mingw`, green. **Commit nothing.**

## Known unknowns — resolve and record

**All three resolved 2026-08-18, and none of them was a stop-and-raise.**

**1 · Is `qty: 0` the only removal signal, or does an absent level also imply removal?**
**`qty: 0` is the only signal the VENUE sends, and it is proven rather than observed.** An update
carries only the levels that changed — an absent level means *unchanged*, never *removed* — and
the proof is the checksum: a book maintained on exactly the rule *`qty: 0` removes, everything
else amends, then truncate to the subscribed depth* reproduces the venue's CRC32 on **4,878 of
4,878** messages across the four slices with an opening snapshot. Any additional removal rule
would have to leave that number unchanged, and any missing one would have broken it on the first
message. There is a **second, client-side removal** and it is not the venue's: truncation eviction,
which Kraken deliberately never signals (Headline 2). The adapter emits that as `Delta{qty = 0}`
itself.

**2 · Does any committed slice cross the book, and what does the adapter do?**
**No slice ever crosses, and none even touches** — measured over all 4,927 book messages in the
five files: 0 crossed (`best_bid > best_ask`), 0 with `best_bid == best_ask`. So the question of
what to *do* about one is **unanswered by evidence and deliberately left unanswered by code**: the
adapter applies each side independently and never compares the two, so a crossed book would be
stored and drawn as sent. That is the honest behaviour for B1 — a crossed book is a real market
state during a fast move, not necessarily an error, and inventing a rule (drop? gap? grey?) with
zero observations to calibrate it is how a wrong rule ships. **Recorded as owed by B2**, which is
where the CRC gives an oracle: if a crossed book is our own error the checksum says so immediately,
and if it checksums clean it is the venue's truth and must be drawn.

**3 · Can the phase-1 book hold depth 25 without reshaping?**
**Yes, with room to spare, and stage C did not arrive early.** `kBookCapacity` is
`kMaxSnapshotLevels` = 256 per side, against a subscribed depth of 25 and a display window of 27.
The depth-100 slice replays through the same book unchanged. What the book *did* need was not more
room but the ability to amend a level at all — `apply(Delta)` was a refusal that marked the book
stale. That is now an ordered insert / amend / erase over the same sorted array a `Snapshot`
fills, sharing `engine/ladder.hpp` with the adapter's ladder. **It is explicitly not the dense
window**, which remains stage C and changes the storage, not this face.

One unknown the stage *added*, recorded here because it is the same shape: `kMaxSnapshotLevels`
(256) is smaller than Kraken's deepest offered tiers (500, 1000), and the top-10 CRC32 **cannot
detect** a book silently truncated to 256. Nothing subscribes that deep — the firmware constant is
25 — and it is now a `static_assert` rather than a discovery.

## Definition of done

- [x] §9 row on mutation-verification, before any code.
- [x] Decimal → integer ticks with no float in the path; round-trip property test over all five
      slices; `qty_step ≠ 1` paths fixed.
- [x] Ack handling: `success:false` fatal, depth `static_assert`ed, **absence handled** and proven
      on the extreme slice.
- [x] Deltas applied; zero-qty removal; two unfilled rows render unknown; `checksum` parsed and
      carried unverified.
- [x] Decoder identity in every pinned Kraken output; dispatch-swap mutation run and red.
- [x] Allocation-free steady state claimed and justified, or stopped and reported.
- [x] Five slices replay through the real adapter; goldens added, none moved.
- [x] Queue-vs-shed assumption pinned as a test naming B2 as owner.
- [~] *(Part B)* `staleness.hpp`, `AgeText`, `kRxWatchdogMs` deleted; clocks lifted; **guard removed;
      `symbol_for()` and `console_ladder` fixed** — the guard half is DONE (it had to be, those two
      broke the moment the guard came off). The firmware lift is **reassigned to D's first act**,
      not a B1b evening: it needs the bench, and D's acceptance run is the same soak (owner,
      2026-08-18; reasoning in `M4-triage-of-the-twelve.md` and DESIGN §08 strain 22).
- [x] Stale worktree deleted. Code review run. Split proposed. Nothing committed.

## Out of scope

CRC verification, resync, the resync slice capture, `slice_trace`'s reconnect self-containment
(all B2). The dense-window book and the uninitialised-book engine state (C). Where the age sits on
the panel, and the healthy-feed-no-snapshot rendering (D). The client ping (M6). Binance (M5). The
runtime venue toggle (M7).

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     measured figures / evidence · exact next step. -->

### 2026-08-18 · Claude Opus 4.6 · Part A complete, Part B reassigned to D’s first act

**Done.** The second adapter links. `engine/kraken/` is new — `kraken_frame.hpp`
(the decoded frame), `kraken_frame_streaming.cpp` (one parser, allocation-free,
target-bound) and `kraken_adapter.hpp` (the venue's book, truncated, emitting
`FeedEvent`s). `engine/ladder.hpp` is new and shared. The phase-1 book learned to
apply a `Delta`. `run_replay` dispatches on the venue tag and the stage-A refusal
is gone. All five committed slices replay end to end through `dc_replay`,
`dc_ladder` and the goldens: **29 ctest targets green**, up from 24.

**Decisions, with why.**

1. **The venue's book lives in the ADAPTER, truncated there; the engine's book is
   a mirror.** The deciding argument is B2, not taste: the CRC32 covers the top 10
   of the *truncated* book, so whoever truncates is whoever can verify. Putting
   the depth limit in `engine/` would force the adapter to read the book back —
   inverting the one-way flow invariant #2 protects. A level evicted by
   truncation leaves as an ordinary `Delta{qty=0}`, so **no new `FeedEvent`
   variant and no new `GapReason`** — the third asking, the third no.

2. **`qty_step` → `qty_decimals`, a semantic replacement not a rename.** The old
   field divided, which can only express a step *coarser* than the wire unit.
   Kraken's is `1e-08` — finer — and **not representable in the old field at
   all.** Every scaling path in the tree was correct only because the value had
   always been 1. Spelled with designated initialisers everywhere, because a bare
   `{101, 4, 1}` keeps compiling while silently asserting one decimal place.

3. **Absence of a subscribe is not failure of a subscribe**, and separately, **no
   baseline, no deltas.** The state machine starts at `Unknown` and accepts book
   messages there, or the extreme slice — the only artefact carrying the 25,843 ms
   healthy silence the staleness ruling rests on — would emit nothing. Updates
   before the first snapshot are counted and dropped: measured, that slice's 49
   updates onto an empty ladder reproduce **0 of 49** checksums. A *refused*
   subscribe is the opposite and terminal: `Gap{Disconnect}` + latched `refused()`.

4. **One parser for Kraken, not two.** Anvil's pair exists because M1 shipped an
   allocating reference that M3 had to prove a rewrite equivalent to. Kraken has
   no such reference, and writing an allocating parser to have something to agree
   with would be inventing the obligation.

**Measured, before writing the adapter (all in `NOTES-kraken.md`).**

* Every price and qty in all five slices carries **exactly** its declared
  precision — 8,172 level entries, zero exponent notation on the book channel.
* Therefore **the CRC32 reproduces from integer ticks alone: 4,878 / 4,878** on
  the four slices with an opening snapshot, no wire text retained. That is what
  makes B2 add a *check* rather than a parse, and it is why `KrakenFrame` has no
  text buffer. (The fifth reads 0/49 — correctly; it starts mid-stream.)
* The adapter's eviction counts reproduce stage 0's Python figures exactly:
  **275 / 585 / 1,156 / 19.** Two languages, no shared parent — which is the
  independence test the 2026-08-17 close-out row demands before calling agreement
  corroboration.

**Found at review, after all five slices were green: `adopt_snapshot` truncated
nothing.** It seeded every level the frame carried, capped only at the 256-level
staging buffer rather than at the subscribed depth — the exact non-truncating
defect stage 0 measured as wrong in 1,077 of 1,537 messages. **No committed slice
could catch it**, because Kraken serves exactly the depth requested so the two
caps coincided on every file. Reproduced synthetically (5-level snapshot, depth 2
→ ladder held 5), fixed, regression added. Two live routes reached it: a capture
with no `depth` in its metadata, and `ack_depth_mismatch`, which exists precisely
because Anvil already rounds depth *up*. This is the evening's §9 row arriving
against its own author on the same night.

Also from the review: `better()` and the ordered insert/erase existed **twice**,
once in the adapter and once in the book — a mirror that sorts differently from
its original draws a book the adapter never held, and no counter would say so.
Extracted to `engine/ladder.hpp`: mechanism shared, policy not. And a
use-after-move — the decoder owned a `std::string` symbol the adapter viewed;
`Replay` takes its decoder **by value**, so SSO copied the bytes and left the view
in the moved-from husk. It passed the direct-construction test and failed only
through `run_replay`. Now borrows the venue table's static literals.

**Mutation-verification, run not written** (the evening's §9 row):

* **Dispatch swap** — `case Venue::Kraken` rebuilt to construct
  `Replay<AnvilTraceDecoder>`. **RED**, and `CHECK(r.decoder == "kraken")` was the
  *first* assertion to fail on every one of the five slices, before the counts.
  Reverted; suite green.
* **The depth defect** — observed directly, 5 levels before the fix and 2 after,
  which is the same evidence in the other order.
* Counters that no slice exercises got synthetic coverage rather than being
  presented as covered: `levels_outside_depth`, `levels_unchanged`,
  `ack_depth_mismatch`, `levels_deeper_than_subscribed`, the deep-tier clamp.

**Verification.** `cmake --workflow --preset host-mingw` green, 29/29.
`dc_engine_target_check` compiles `kraken_adapter.hpp`, `kraken_frame.hpp`,
`ladder.hpp` and `kraken_frame_streaming.cpp` with **xtensa GCC 8.4** under
`-Werror -fno-exceptions -fno-rtti` — invariant #1's target half covers every new
file. The firmware links unchanged. Stale worktree `C:/tmp/dc-verify` deleted.
**Nothing committed.**

**Why Part B was not attempted, and where it went.** Its item 9 — the `run_replay` guard,
`symbol_for()` and `console_ladder` — is **done**, because those two defects went live the moment
the guard came off and the triage put them here for that reason. Item 8 is not, and the reason is
not time: deleting `kRxWatchdogMs` and rewiring the RX watchdog onto the calibrated liveness
threshold **changes when the panel greys**, and that behaviour was established over M3's 23.6-hour
soak. The desk can compile it (PlatformIO builds clean here) and cannot show it correct. A *partial*
lift is worse than none — the clocks in `engine/` while the firmware still includes `staleness.hpp`
is a third copy of the thing the deletion exists to remove.

**Reassigned by the owner on 2026-08-18 to D's FIRST ACT rather than to a B1b evening**, and the
argument is better than the one this log originally made: D is already the bench stage and its
acceptance run is the same soak, so a separate evening would mean two bench sessions to answer one
question. It goes before any panel rendering is judged, because a panel judged against a watchdog
that is about to change is a panel judged twice. M4's remaining evenings drop from five to four:
**B2 · C · D**. Recorded in `ROADMAP.md`, `M4-triage-of-the-twelve.md`, and `DESIGN.html` §08
strain 22, which carries the owner, the expiry and the tripwire.

**What the lift is, when D runs it.** Move `sample_window.hpp`, `liveness_clock.hpp` and
`age_estimator.hpp` into `engine/include/depthcharge/` (namespace `depthcharge`; all three already
use only freestanding-safe headers, so `dc_engine_target_check` proves them target-buildable on
arrival). Then delete `firmware/src/staleness.hpp` whole — `StalenessEstimator`, `SecondsText`,
`kSummaryPeriodUs`, `drain_percent` — rewire `feed_task`/`render_task` onto `AgeEstimator` +
`LivenessClock`, delete `kRxWatchdogMs` and its `static_assert`, and delete `test_staleness.cpp`.
`AgeText` survives; `SecondsText` is the duplicate that goes. **Bench the grey transition before
calling it done** — that is the one thing in the lift no host test can assert.

**Exact next step: B2 — the healing path.** CRC32 verification (which this stage proved is a
*check* over integers, not a parse), resync, the deliberately captured resync slice, and
`slice_trace`'s reconnect self-containment. Two things B1 hands it: `last_checksum()` is already
carried on every book message, and the queue-vs-shed experiment is pinned as a test naming B2 as
owner — **CRC mismatch under induced backpressure**, since shed updates break the checksum and
queued ones do not.

---

### 2026-08-18 (later) · owner approval · four records, then the split executed

Split approved as proposed, eight commits, commit 3 upheld — the brief's `engine/` constraint was
self-contradictory and the reading under which "deltas onto the phase-1 book" is expressible is the
intended one. Four records written before committing:

1. **§9 gains the coincidence class** — a check that *can* go red still proves nothing if every
   available input makes the wrong answer coincide with the right one. Both of tonight's
   review defects are that shape, and both live routes to the truncation one are named (a capture
   with no `depth` in metadata; `ack_depth_mismatch`, which exists because Anvil rounds depth up).
   The rule it leaves inverts the usual ranking: **where an adapter's assumption and a venue's
   behaviour could coincide, the synthetic case is mandatory and the golden is decoration.**
2. **Item 8 reassigned to D's first act**, above.
3. **The host/target age divergence gets an owner, an expiry and a tripwire** — DESIGN §08
   strain 22. It is latent rather than active: nothing on the target reads Kraken until D.
4. **The commit-verification contradiction resolved as standing practice** — per-commit
   verification runs as part of *executing* an approved split, not before it. Recorded in
   `CLAUDE.md`.
