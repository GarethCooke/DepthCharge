# M5 Stage B1 — the adapter, with an oracle already in the room

**Track:** Agentic [A] · **Status:** Done 2026-08-25 · **Size:** one evening
**Executor:** Claude Code, **desk only. No board, no flash, no `firmware/` change, no panel judgement.**

**This is the third adapter, and it is the first one written with an independent answer already
available.** At Kraken, B1 shipped a book that had never been checked and B2 supplied the CRC a week
later — which is why B1's session log carries three questions it deliberately left unanswered.
Binance's oracle exists now: `@depth20` grades a book **884/884, 886/886, 90/90, 90/90** across four
witnesses and goes RED on all three mutants. **So questions Kraken's B1 had to defer, this one must
answer** — and inventing a rule with no calibration is not available as an excuse when the
calibration is sitting in `harness/replay/`.

**Read first**

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §4 | Frozen, and `Gap{SeqGap}` has been written down for this venue since M0. This is the fourth asking. |
| `ARCHITECTURE.md` §6 | #2 (one boundary type), #3/#4 (integer ticks, verified representable), #6, #7 (allocation-free steady state). |
| `ARCHITECTURE.md` §9, the two **2026-08-25 (M5, owner's ruling)** rows | Decision 2 is why the audit stream is in the configuration at all. |
| `harness/replay/NOTES-binance.md` | The measured wire. **Where it and this brief disagree, the notes win.** |
| `docs/briefs/M5-stage-A-the-two-records-that-are-not-frames.md` | `TraceRecord`, `RecordForm`, `event_ns`, and `rest:no-body` — the last of which is a requirement on this stage, not trivia. |
| `docs/briefs/M4-stage-B1-the-adapter.md` | The Kraken twin, and specifically its three *known unknowns* and the one it handed to B2. Two of the three are answerable tonight. |
| `engine/include/depthcharge/kraken/kraken_checksum.hpp` | How a venue's own verification was expressed as a check rather than a parser. The shape, not the algorithm. |
| `docs/DESIGN.html` §08 strain **25** | Answered at stage 0: **no third scanner.** Do not re-open it; do reuse deliberately. |

**Depends on:** stage A ✅. **Blocks:** B2, C, D.

---

## Deliverables

### 1 · No third scanner — reuse, and prove the reuse rather than asserting it

Stage 0 measured Binance's grammar as a **strict subset of both existing scanners**: zero floats,
zero exponents, zero string escapes, maximum nesting 4. Strain 25's extraction trigger arrived and
**did not fire**.

- Reuse whichever existing scanner fits, and say in the header **which**, and **why that one**.
- **Prove the subset claim holds on the committed corpus rather than inheriting it from stage 0's
  summary** — one counted pass over all seven slices, reported as figures. A grammar claim that was
  true of the capture window is not automatically true of the venue.
- If reuse turns out to need a flag per venue quirk, **stop and raise**: that is the outcome strain
  25 says is worse than the duplication, and it changes the answer rather than the code.

### 2 · The scale is a constant, and `tickSize` is a validator

Stage 0's finding inverts what §6's *verified representable* check looked like at Kraken.

- **8 decimals, uniformly, on every symbol** — 202,012 of 202,012 entries, on a 2 dp and a 3 dp
  symbol alike. The scale is a **constant**, not derived from `tickSize`; deriving it from `tickSize`
  would be wrong by **10⁶** on BTCUSDT.
- `PRICE_FILTER.tickSize` and `LOT_SIZE.stepSize` become **validators** — *is this value a whole
  multiple of the venue's tick* — and a violation is a reported error, never a silent rounding (§6).
- No float, no `Decimal`, anywhere in the path. `78564.00000000` is 7,856,400,000,000 — 43 bits, so
  `int64_t` has room and invariant #3 needs no new path.
- **The equivalence is an assumption with a detector, exactly as Kraken's was.** If the venue ever
  sends a number at a different precision, the first thing that notices must be a named check with a
  message, not a book that silently drifts. Say in the header what that detector is.

### 3 · The seed, and a REST record that may carry no body

- Seed from `/api/v3/depth` at **`limit=1000`** (the ruling's recommendation; `kMaxSnapshotLevels`
  256 fails 33 of 884 graded ticks in 90 s, and 1,000 covers ~$240 against a measured $29.85/90 s
  walk on BTCUSDT).
- **Reconcile by id, never by position.** Stage 0 paid for that sentence once: a slice cut *at* the
  baseline record rather than containing it took the oracle from 884/884 green to 250/250 red.
- Implement the venue's documented bracketing: discard buffered events whose `u` ≤ the snapshot's
  `lastUpdateId`; the first surviving event must contain `lastUpdateId` in its `[U, u]`; if the
  snapshot is older than the first buffered `U`, **re-fetch rather than proceed**.
- **`rest:no-body` is a state this adapter must hold, not a file it may reject.** Stage A found that
  a failed fetch is *recorded*, and a reader that refused one refused a trace its own capture loop
  had written. The adapter's answer is the same shape as Kraken's *absence of a subscribe is not
  failure of a subscribe*: a fetch that produced nothing means **the seed has not arrived yet**, not
  that the feed is broken. Buffering continues; the book stays uninitialised; nothing is dropped.

### 4 · `U`/`u` is a transport check, and it is wired to `Gap{SeqGap}` and to nothing else

- Continuity is `U == prev_u + 1`. Spot carries **no `pu`**, so this is one-sided and it is the only
  ordering signal there is.
- A break raises **`Gap{SeqGap}`** — the reason §4 wrote down for this venue at M0, before any of
  this code existed. **Fourth asking of §4 across three venues; if it seems to want a new word,
  stop and raise.**
- **Do not let this number appear anywhere near a claim about book correctness.** Stage 0 measured
  it catching **0 of 3** book mutants while remaining 890/890 clean against a client that was 82.4%
  wrong. It caught the deliberate reconnect's 2,204 missed updates, which is its whole job. Put that
  sentence in the header, because the next reader's instinct will be to treat a clean sequence as a
  clean book.

### 5 · The removals that are normal, and the one that is not

Binance's documented behaviour: `qty: 0` removes a level, and **a removal for a level not in the
local book is normal.** That is not a footnote here — it is constant, because a book seeded at 1,000
levels receives removals for levels outside the seeded window continuously.

- Removing an absent level is a **no-op, counted, never an error and never a `Gap`.**
- **Make the seeded-window edge observable without acting on it.** When the best bid or ask
  approaches the boundary of what the seed contained, that is the 82.4% failure arriving. Count it,
  report it on `dc_ladder`'s line, and **do not invent a rule** — the re-snapshot schedule is B2's
  and needs a measured basis. This is the same discipline stage C used: record per policy, solve
  nowhere.

### 6 · The crossed book — answerable tonight, because the oracle exists

Kraken's B1 measured that no slice crossed, declined to invent a rule with zero observations, and
**recorded it as owed to B2, where the CRC would give an oracle.** Binance's oracle is available on
day one, so the deferral is not.

- Measure it: does any committed slice cross (`best_bid > best_ask`) or touch (`==`)?
- If one does, **ask the oracle** — a crossed book that grades clean is the venue's truth and must be
  drawn; one that grades RED is our error and the checksum-shaped answer applies.
- If none does, say so with the count, and record that the rule remains uncalibrated **for the same
  reason as at Kraken** — but note that the instrument to settle it now exists, so the next
  observation settles it rather than deferring again.

### 7 · The oracle in the loop from the first line

Not a verification step at the end. `@depth20` grading is available before the adapter is written,
so use it as the development instrument:

- Grade every committed slice through the real adapter, reporting
  `seen == matched + failed + unverifiable` with the fourth outcome unable to appear quietly.
- The three mutants stay red and the honest control green — and **`NOT EXERCISABLE` remains a state
  distinct from both**, so a quiet-pair witness cannot pass by having nothing to disagree about.
- **The goldens are the adapter's output, and no existing golden may move.** A moved Anvil or Kraken
  golden means stop.
- Note what the oracle does **not** reach: it validates the top 20 a side while the panel draws 25.
  That is the same gap as Kraken's CRC-10 and it is **smaller** — 20 of 25 against 10 of 25 — and it
  belongs on `dc_ladder`'s report line rather than in a comment.

### 8 · Writeback

Session log; `NOTES-binance.md` wherever the wire disagrees with this brief; ROADMAP M5 line;
`DESIGN.html` for anything it draws — strain 25 gains its closing note (*trigger arrived, did not
fire, and here is the measurement*), and the adapter's shape enters §01 if the diagrams carry it.

---

## Constraints

- **§6 frozen. §4 does not move** — `Gap{SeqGap}` exists; no new `GapReason`, no new `FeedEvent::Kind`.
  Stop and raise if it appears to need one.
- **Desk only.** No `firmware/`, nothing flashed, no panel decision. The board subscribing the audit
  stream is decision 2 implemented in **D**, not here.
- Allocation-free steady state after the first snapshot, claimed and justified or **stopped and
  reported** (§6 #7).
- No existing golden moves. The eight Anvil and Kraken traces replay byte-identical.
- Host build and ctest green (`cmake --workflow --preset host-mingw`, from PowerShell).
- **Per-commit verification in a fresh detached worktree with `CMAKE_HOME_DIRECTORY` confirmed, loop
  run INLINE, and the path comparison normalised for separator and case** — stage A's confirmation
  cried wolf on a backslash-versus-forward-slash mismatch, which is the safe direction and still an
  evening's confusion. `powershell -File` fails at `CMakeTestCXXCompiler` for reasons not yet rooted
  out. Delete stale worktrees when done.
- **A restored file keeps its old timestamp**, so `make` will not rebuild it and a mutation run can be
  scored against a stale binary — stage A hit this. `touch` after restoring, or verify by content.
- **Commit only when asked.** Propose the split; commit nothing until approved.

## Known unknowns — resolve and record

**1 · What does a wrong stream name look like?** Binance names streams in the URL path, so there is no
subscribe message and no ack — which removes Kraken's *refused subscription* problem and may
introduce its mirror: **nothing ever confirms the subscription succeeded.** A misspelled stream
plausibly yields an open socket delivering nothing, and at this venue the depth stream legitimately
goes silent for 10.5 s, so silence carries even less information than it did at Kraken. Find out what
the wire actually does, cheaply, and record it. If a bad stream is indistinguishable from a quiet
market, that is a finding for C and possibly a §9 row.

**2 · Does this venue's ladder render trade prints at all?** §1 describes trade prints flashing white
at the touch. Binance's trades are a **separate stream** (`@trade` / `@aggTrade`) with a wire cost
that **decision 2's numbers do not include** — the ruling priced `@depth` and `@depth20` only. Nobody
has asked this question during M5. **Do not subscribe one; price it and raise it.** If the answer is
that a Binance ladder has no trade prints, that is a third venue-shaped hole in what the panel shows,
beside the liveness one, and it belongs in M5's definition of done with the others.

**3 · Does any committed slice cross or touch?** Deliverable 6.

**4 · Does `event_ns` reach REST records, or only control records?** Stage A stamped control records
with arrival because three pings 20 s apart shared one `rx_ns`. A REST body has the same flush
problem, and **B2 needs round-trip times out of the trace** to size the re-snapshot schedule — stage
0's ~1,003 ms and ~1,481 ms came from live measurement, not from a file. Answer it here; fixing it is
whoever's the answer says.

**5 · Is the seeded-window edge observable at all** without a rule that would need calibration?

## Definition of done

- ☑ Scanner reused, the choice justified in the header, and the subset claim **re-measured** on all
      seven slices rather than inherited.
- ☑ Integer ticks at a constant 8-decimal scale; `tickSize`/`stepSize` used as validators; **no float
      and no `Decimal` in the path**; the precision detector named.
- ☑ Seed at `limit=1000`, reconciled **by id**; the venue's bracketing implemented including the
      re-fetch branch.
- ☑ **`rest:no-body` held as *seed not yet arrived*** — buffering continues, book uninitialised,
      nothing dropped, and a test covers it.
- ☑ `U`/`u` continuity raises `Gap{SeqGap}` and nothing else; the *0 of 3 mutants* sentence is in the
      header.
- ☑ Removal of an absent level is a counted no-op; the seeded-window edge is **reported and not
      acted on**.
- ☑ The crossed-book question **answered with a count**, and settled by the oracle if any slice
      crosses.
- ☑ Every committed slice graded through the real adapter, `seen == matched + failed + unverifiable`
      asserted; three mutants red, control green, `NOT EXERCISABLE` still a distinct state.
- ☑ **No existing golden moved**; the eight Anvil and Kraken traces byte-identical.
- ☑ Allocation-free steady state claimed and justified, or stopped and reported.
- ☑ Known unknowns 1, 2 and 4 answered and recorded — **2 raised to the owner rather than decided.**
- ☑ Green; session log; ROADMAP line; DESIGN strain 25 closed with its measurement; split proposed;
      nothing committed.

## Out of scope

The re-snapshot **schedule** and the walk-rate measurement behind it (**B2**). Slicing, committing
and pinning the two mixed-cadence captures, and the ~200 s ping-bearing capture the calibrated
liveness path needs (**B2's capture run**). The liveness threshold, `kThresholdCeilingMs`'s changed
role, and what the panel renders on a silent feed (**C**). The bench, the >24 h soak, and putting the
audit stream on the board (**D**). Subscribing a trade stream (**raised here, decided by the owner**).
The client ping (M6). The runtime venue toggle (M7). M4's carried bench residues — D1, D2, D7's scope
trace.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     measured figures / evidence · exact next step. -->

### 2026-08-25 · Claude Opus 5 (1M context) · B1 complete, green, nothing committed

**Done.** All eleven definition-of-done boxes. `cmake --workflow --preset host-mingw` green,
**45/45** (was 44 — `binance_adapter_oracle` is new). Nothing committed; split below.

**The headline, because it is the thing this stage was built to be able to say.**
`dc_binance_oracle` drives the **real adapter** over every committed slice and grades its
ladder against the venue's own `@depth20`. **Every matched count equals
`tools/binance_oracle.py`'s for the same file** — 235/235 on both deep-seed witnesses,
140, 29, 8, and 28-matched/32-failed on the one that is red. Two implementations, in two
languages, written against the same wire by different means, agreeing at every opportunity
the venue gives. Pinned per slice in `dc_binance_oracle`'s table.

**Decisions, with why.**

1. **No third scanner (deliverable 1, DESIGN strain 25).** The subset claim was
   re-measured over the seven committed slices rather than inherited: 0 escapes, 0
   exponents outside a string, 0 bare `.`, 0 literals, nesting 4, 188,372 entries all at
   exactly 8 decimals. Kraken's scanner was lifted **verbatim** into
   `engine/include/depthcharge/json_scan.hpp` — 327 lines moved, none changed — and
   Binance reuses it **with no venue flag**, which was the brief's stop-and-raise
   condition. Anvil's was the wrong one: it is bug-compatible with nlohmann 3.11.3's float
   handling, a specification this venue has no use for. The extraction is held still by
   the Kraken goldens, the taxonomy pins, and a suite assertion count **identical to the
   byte before and after: 3,609,744**.
2. **The scale is a venue constant; `tickSize` is a validator (deliverable 2).** 8
   decimals on every symbol, so deriving the scale from BTCUSDT's `tickSize` of 0.01 would
   be wrong by 10⁶. The filters became modulo checks and **were verified before being
   declared** — a validator that rejects valid frames is worse than none — by taking the
   GCD of every price and quantity in the corpus: it *is* the declared filter, on 91,665
   BTCUSDT and 2,521 ATOMEUR entries. The precision detector is named:
   `ParseStatus::BadPrice` from `parse_scaled`, raised on the value that disagreed.
3. **Two truncation boundaries, which is new.** Kraken's storage and emission depths
   coincide; here they cannot. Storage must be 1,024 because `--window-sweep` says the
   requirement is a property of how far the market walked — **the two deep-seed witnesses
   disagree by 5×**, 100 clean on one and 500 needed on the other, and 256 fails 33 graded
   ticks. Emission cannot exceed the engine's `kBookCapacity` of 256 or the Deltas are
   dropped. So the adapter holds 1,024, emits the top 256, and emits a level crossing that
   boundary as the crossing it is.
4. **`Gap{Overflow}` for a diff too large to stage**, and no new `GapReason`: §4 already
   defines Overflow as a venue reassembly buffer, "first produced at a delta venue
   (M4/M5)". This is that venue. Fourth asking of §4 across three venues, fourth time it
   needed no new word.
5. **The pre-seed buffer is real and bounded from measurement.** Buffering is not
   optional: a round trip is ~1.0–1.5 s, so a seed adopted without it is a book missing
   that much. Worst case measured is 15 events / 823 levels / 12.9 KiB; bounds are 64 /
   2,048, and an overflow raises `Gap{Overflow}` rather than passing quietly.
6. **`rest:no-body` is a state, not a failure (deliverable 3).** Same shape as Kraken's
   *absence of a subscribe is not failure of a subscribe*: buffering continues, the book
   stays uninitialised, nothing is dropped, no `Gap` is raised — there is nothing to be
   stale about because there was never a book — and a re-seed is latched. Covered by a
   test that also proves the buffered events survive to be reconciled.
7. **The audit stream is compared against, never applied.** A `@depth20` payload replaces
   the top 20 and re-baselines nothing below it, so adopting one leaves the levels outside
   it at whatever they had — right at the touch and silently wrong underneath, which is
   worse than honestly stale.

**Evidence.**

- **Cost, stated rather than buried**: ~96 KiB of fixed, never-allocated adapter state
  (32 KiB ladder + 32 KiB staging frame + 32 KiB pre-seed buffer) against the ~144 KiB the
  whole Anvil image uses. Invariant #7 holds; **where it lives on the board is D's**.
- **Mutation-verified against the oracle**, by hand, because this project does not ship
  mutation code inside the thing being mutated. Each applied for real, rebuilt with
  timestamps forced forward, run, reverted, baseline re-confirmed: qty-0-stored-not-removed
  → 235 matched becomes **0**; sides-swapped → **0**; book bounded at 256 → **0**.
- **The third mutant's figure is not the one the sweep predicts, and that is recorded
  rather than rounded.** `--window-sweep` reports 202/33 for a 256-level book; this mutant
  reports 0/235, because the sweep truncates *after* each update while the mutant bounds
  `insert_at` and therefore cuts the 1,000-level seed on arrival too. Both red, neither the
  other — which is why the prediction was checked against a run instead of being written
  down as though it had been.
- **No existing golden moved.** Eleven taxonomy rows unchanged; the whole suite green.

**Deliverable 6, answered with a count.** Asked of the venue's *own published books* rather
than of one we maintained: **0 crossed, 0 touched across all seven slices.** Tightest
spread is one tick. The rule stays uncalibrated for the same reason as at Kraken — but
unlike Kraken the instrument now exists, so the next observation settles it.

**Known unknown 4, answered and one stage-A figure corrected.** `event_ns` does reach REST
records (stage A set it from `req.recv_ns`), so B2 can size the re-snapshot schedule from
a committed file: `limit=100` median ~995 ms, `limit=1000` median 1,481.8 ms, reproducing
stage 0's live numbers. **Stage A said a REST record lands "~1–1.5 s after the instant it
describes"; measured over 26 records the median is 439 ms and the max is 42,721 ms** — the
record is written when the *next message* arrives, so on the quiet pair it can be 42
seconds late. Anyone cutting a slice needs the max, not the median.

**Known unknown 1, and it is the finding with teeth — amended 2026-08-26 after the owner
located the mechanism.** Probed on the wire with a control run: a misspelled stream returns
**HTTP/1.1 101**, holds the socket, **answers pings in 0.107 ms**, and delivers **zero
frames**, while the REST seed on a different host **succeeds**. The control took 121 frames
in the same 12 s. Replayed through the real adapter and the real book: `adapter events=1`,
`seed bracket ok=0`, `snapshots_adopted=1`, `0 stale episode(s)`, and a populated
100-level ladder rendered **● LIVE**.

**The mechanism is the emission point, not a missing detector**, and the first write-up of
this was vaguer than the evidence allowed. `adopt_seed()` emits the `Snapshot` at seed time
from the REST body, **before any WebSocket event arrives**, so `Book::adopt` sets
`FeedStatus::Live` off the seed alone while `bracket_checked_` stays false for ever. Every
counter reads healthy because the adapter did exactly what it was told.

**And the general statement, which is a correction to decision 1's premise rather than its
conclusion.** The ping is a **transport** liveness signal that the ruling adopted as a
**feed** liveness signal. At Anvil and Kraken the same subsystem emits the liveness record
and the book, so the two coincide and the ruling never had to distinguish them; at Binance
the ping comes from the WebSocket layer, **below the subscription**, and proves the socket
and never the feed. A misspelled stream is only the cheapest way to stage that — **a
server-side subscription drop on a socket that stays up presents identically**. Decision 1
stands (socket-death-alone lost by 141 s and 291 s at B3); what changes is what a green
liveness clock is entitled to mean.

**Four candidate remedies recorded, none built** (DESIGN strain 26, ARCHITECTURE §9): (a)
defer the `Snapshot` to bracket time, so the lie greys with **no threshold, no detector and
no new `GapReason`** — uninitialised has been explicit since M4 stage C and grey since M4
stage D — costing a fraction of a second on a healthy connect and up to ~10 s on a quiet
pair; (b) the ping does not stamp liveness until the bracket has been satisfied once, so
the panel draws the seed immediately and the lie self-terminates at the threshold; (c)
*seeded but zero diffs since*; (d) the `@depth20` grading the board already pays for.
**Neither (a) nor (b) closes the general hole** — the ping stays transport-only after the
bracket too — and both convert **permanent into bounded**, which is where invariant #5
draws its line. **Owner: C.**

**Known unknown 2 — the brief's framing was wrong, and the correction matters more than the
answer.** The trade-print hole is **already there at Kraken** and M4 shipped with it: Kraken
puts trades on a channel DepthCharge does not subscribe, and Binance is identical. The ring
is **Anvil-only** and has been since M4 without being written down. It is therefore **not an
item on any M5 stage** — it is a §1-versus-reality question for the owner, recorded as
**ROADMAP backlog D0**: whether a terminal whose §1 describes trade prints, on a build where
two of three venues have none, is design or debt.

**Not done, deliberately, and named rather than left to be discovered.**

- **The three mutants are not re-implemented as a C++ `--mutant` mode**, and the
  grader's evidential position should be stated rather than hedged. It agrees with
  `binance_oracle.py` — a **mutation-verified** instrument, green on an honest control and
  red on three breakages in the normal build loop — **on every count across the whole
  corpus, in both directions**, including the failing slice where both report 28 matched
  and 32 failed. **So it has been shown red on a real failure, not merely green on healthy
  data.** What it lacks is a *planted* one. The three mutants were run against it by hand
  (each takes 235 matched to 0); automating them wants a second link configuration, the
  `dc_tests_streaming` shape, and is a stage of its own.
- **`NOT EXERCISABLE` is not implemented in the C++ grader.** The Python oracle has it and
  `binance_oracle_mutants` runs it. The C++ side distinguishes GREEN / RED / VACUOUS, which
  covers the corpus as it stands (the reconnect slice is VACUOUS and says so), but a
  quiet-pair mutant that changes nothing observable would read as green here where the
  Python tool refuses to call it either.
- No `firmware/` change, nothing flashed, no panel decision, no trade stream subscribed, no
  §4 or §6 movement, no committed trace reshaped or re-captured.

**Exact next step.** Owner reviews the proposed split. On approval: create the commits,
verify each in a **fresh** detached worktree with `CMAKE_HOME_DIRECTORY` confirmed and the
comparison normalised for separator and case (stage A's cried wolf on a backslash), run the
loop **inline**, and push nothing until every commit is green in isolation. Then **B2** —
which now has its round-trip numbers from the corpus rather than from a live run, and two
mixed-cadence captures the reader has been able to read since stage A.

### 2026-08-26 · Claude Opus 5 (1M context) · follow-up: the defect fixture

**Done.** The one gap B1 left: the misspelled-stream capture existed nowhere. Re-taken live
(one connect, 50 s, one opening REST seed, no loop), committed as
`harness/replay/binance_btcusdt_DEFECT_silent_stream_20260826.ndjson`, pinned in the
taxonomy, and read end to end by `dc_replay`. **46/46** (was 45). Nothing else touched.

Four records: one `rest`, three `ping`, **zero frames**. The reader handles it, the liveness
clock is fed normally at a 20,004.8 ms median through total silence, and the ladder draws
**● LIVE**.

> **CORRECTED 2026-09-06 (M5 close-out): the clock's median for this fixture is 19,950.6 ms,
> not 20,004.8.** 20,004.8 is `dc_replay`'s report line, which carried its own interpolated
> median until the close-out gave `harness/src/trace.cpp` the shared `lower_median`. This is
> the figure ARCHITECTURE §9's 2026-08-26 row names as *"what B1's session log quotes for the
> silent-stream fixture"*; left standing above per that row's own rule, with the correction
> here. Nothing else in this paragraph moves — the clock was fed normally and never fired
> under either convention.

**Named, excluded and pinned deliberately.** `DEFECT` is in the filename in capitals so
nobody has to read a comment to learn it is not gradeable — `NOT_A_CHECKSUM_GOLDEN`'s
convention. It sits in its own CMake variable rather than `DC_BINANCE_TRACES`, so it reaches
`dc_replay` and the taxonomy but never the oracle, the economics pins or the mutants: there
is no book to grade, and a VACUOUS verdict reads like a pass.

**The expiry clause is carried in three places** — the test, the taxonomy row and strain 26 —
in the shape §9 already uses for the audit-stream ruling's untracked-evidence clause: it pins
a defect and not a contract, it must fail when C lands a remedy, the correct response is to
**invert** it rather than delete or relax it, and if C ships without inverting it the fixture
moves to whichever stage next touches liveness rather than lapsing.

**IT FOUND A REAL DEFECT ON ITS FIRST RUN, AND THE DEFECT WAS MINE.** Stage A moved the
statistics pass to `event_ns` and wrote §9's rule as *only the liveness clock reads it* —
then changed one of the **two** liveness clocks. `replay_driver.cpp:90`, the one that decides
`threshold_ms()` and therefore when the panel greys, was still stamping from `rx_ns`. That is
§9's oldest drift shape (2026-08-07) recurring inside the stage that quoted it, and nothing
caught it because at Anvil and Kraken `event_ns == rx_ns`, so both implementations agreed on
every committed trace that existed — the coincidence class again. Fixed here; it moves
nothing, because the only traces where the two stamps differ are Binance's and no golden
reads the driver's clock on them.

**The age estimator deliberately stays on `rx_ns`**, and the note is larger than the
mechanism: `age_ms` is queuing lag measured against the venue's liveness signal, and at
Binance that signal is a *transport* ping with no relationship to the book's queue — so the
quantity is questionable at this venue whichever stamp feeds it. **C's**, with the
transport-versus-feed row.

**One more stale line corrected**, the identical defect this milestone already found once:
`dc_replay`'s Binance report claimed *"M5 stage A's Binance decoder is a CLASSIFIER; 0
FeedEvents by design"*, which B1 made false the moment it landed and which went unnoticed
because it is trivially true *of that program*. Twice in one milestone is enough to state the
general form: **a report line describing a stage rather than a behaviour goes stale on the
stage that supersedes it, and nothing anywhere fails.**

**Exact next step.** B2's brief, from the planning seat.
