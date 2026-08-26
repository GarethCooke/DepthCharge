# M5 Stage B2 — the seed, the walk, and the schedule

**Track:** Agentic [A] · **Status:** Done 2026-08-26 · **Size:** one evening
**Executor:** Claude Code, **desk only. No board, no flash, no `firmware/` change, no panel judgement.**

B1 left a book that grades clean against the venue's own and **a window that cannot be sized by a
constant.** The `--window-sweep` disagreed by **5× between two witnesses of the same pair** — 100
clean on one, 500 needed on the other — so book depth is a property of the *market*, not of the
venue, and no fixed bound is defensible. This evening turns B1's instrument into a trigger, sizes it
against the venue's **measured fetch latency** rather than against a capture-tool artefact, and
discharges the one clause in the constitution that currently has no committed trace behind it.

**Read first**

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §9, **2026-08-25 (M5, owner's ruling)** row 2 | Carries the untracked-evidence clause this stage exists to close, *with its expiry and its named destination.* |
| `ARCHITECTURE.md` §9, the two rows from **stage B1** | The lying socket, and the transport-versus-feed correction to decision 1's premise. Section 5 is downstream of the second. |
| `ARCHITECTURE.md` §4, §6 | Frozen. `Gap{SeqGap}` exists; nothing new is needed here either. |
| `harness/replay/NOTES-binance.md` | The measured wire, and **the REST-lag section headed as this stage's most important input.** |
| `docs/briefs/M5-stage-B1-the-adapter.md` | Deliverable 5's low-water instrument, the two truncation boundaries, and the four open unknowns. |
| `docs/briefs/M4-stage-B2-the-healing-path.md` | The Kraken twin. Its **section 4** settled queue-versus-shed by measurement rather than carrying an assumption forward; section 5 below is the same move at a venue likely to give the opposite answer. |
| `docs/briefs/M4-stage-0-price-the-kraken-wire.md` | The capture recipe, and the paragraph left for whoever met a **scheduled** healing event. That is this stage. |

**Depends on:** B1 ✅. **Blocks:** C, D.

---

## Deliverables

### 1 · Discharge the §9 expiry — the ruling's evidence enters the repository

Decision 2 was signed on `harness/replay/_local/binance_btcusdt_mixed{1,2}_20260825.ndjson`, which
are **untracked**. §9 row 2 says so, names B2 as owner, and says the clause is struck **with the
commit SHA in its place**.

- Slice, commit and pin both, in procedure order: **mutants → `--selfcheck` → `--pin`**, add-rows-only.
- Confirm the mixed-cadence coincidence survives slicing — B1's own `slice_trace` guard exists because
  a window cut *at* a baseline record rather than containing one took the oracle from 884/884 green to
  250/250 red.
- **Cut the windows against the write lag, not the round trip.** A REST record is written when the
  *next message* arrives, so `rx_ns − event_ns` runs to a **median of 439 ms and a maximum of
  42,721 ms** over the 26 records in the corpus — on the quiet pair a record can land 42 s after the
  instant it describes. **Anyone cutting a slice needs the max, not the median** (B1 session log).
  **This is the only deliverable those two figures belong to** — see section 3.
- Then **strike the clause and put the SHA in its place.** Do not leave it annotated; the row's own
  wording is that an annotation is a holding position.
- **While the row is open, fix the second stale clause in it.** The same §9 row still reads *"a
  headroom claim at this venue needs a distribution, not a 90-second sample … and **B2's soak is
  where it comes from**"* — while `M5-the-shape-and-the-two-decisions.md`'s stage table and this
  brief's own *Out of scope* both put the soak at **D**. Either strike that sentence and name D, or
  raise it. **Do not edit the row and leave it standing.** A stage that has the row open and leaves a
  stale ownership clause in it is how the drift survives the one stage placed to catch it.

### 2 · The re-snapshot trigger is an observable, not a timer

**A constant cannot survive a 5× market-dependent spread**, which is what the sweep measured. B1
already built the instrument — deliverable 5's low-water mark of book depth per side, reported and
deliberately not acted on. **This stage makes it the trigger.**

- Re-seed when the low-water mark says the seeded window is exhausting, **not on a wall clock**.
- The trigger is self-scaling by construction: a fast market exhausts the window sooner and re-seeds
  sooner, with no constant claiming to know how fast the market is.
- **State what it costs when it is wrong in each direction** — too eager is wire and IP weight (5 / 25
  / 50 / **250** by `limit` tier, and the venue bans on breach); too late is the 82.4% failure class
  stage 0 measured at `limit=100`.

### 3 · Size the margin against the fetch, and against the right measurement of it

B1 and `NOTES-binance.md` report the REST round trip at a **median of ~995 ms at `limit=100` and
1,481.8 ms at `limit=1000`** — measured out of committed files via `event_ns`, and reproducing stage
0's live 1,003 ms / 1,481 ms from the box. The book is unbracketed for the whole of a fetch, so the
trigger must fire while enough window remains to survive the worst fetch, not the typical one.

- **DO NOT size this against 439 ms / 42,721 ms.** Those are `rx_ns − event_ns` — how late a record
  lands in the *trace file* — and they are section 1's slicing constraint. **The board writes no
  trace file**, so the capture tool's flush schedule cannot be a term in the shipped client's margin.
  The two pairs live one paragraph apart in B1's session log and the wrong one is the more memorable,
  so **write the distinction into `NOTES-binance.md` beside both**. This is the third appearance of
  §9's oldest drift shape — *a record's `rx_ns` is not the instant it describes* — and the first two
  were caught in code rather than in a quotation.
- The sizing is **trigger margin ≥ walk rate × p99 fetch latency**, and both terms are measured
  rather than assumed. Report the arithmetic, not just the answer.
- **The p99 is not computable from the corpus that exists.** There are **26 REST records**; the 99th
  percentile of 26 samples *is* the maximum. At the capture tool's 15 s `snapshot_every_s`, a sample
  that can speak to a p99 at all (n ≈ 300) is **~75 minutes of capture** — a distribution rather than
  a 90-second sample, which is **strain 19's shape arriving in a third place**. **Owner's decision,
  OUTSTANDING as this brief is written, and to be taken before this section is scored:** (i) take the
  long capture, (ii) size on max-of-n with the n stated beside it, or (iii) drop the clause to p95 and
  say what n supports it. **Do not tick this deliverable against a percentile the evidence cannot
  support.**
- Note where the *fetch* tail lives, if it has one — and note that the quiet-pair tail in the write
  lag is **not** it. That tail is the capture loop waiting for the next message; it says nothing
  whatever about how long the venue took to answer.

### 4 · What a scheduled re-snapshot does to every capture this project takes from now on

**This stage creates a problem the other two venues do not have, and it must not discover it later.**
At Binance the re-snapshot **is** the healing event, and once the client re-seeds on its own trigger
the healing event becomes **constant rather than incidental** — so a capture with no healing event in
the window stops existing at this venue. M4 stage 0 left this paragraph for whoever arrived here.

- Say plainly what it does to the capture recipe (*liquid pair, shallowest depth, no healing event in
  the committed window*), and what a Binance guard trace can therefore still be.
- If the answer is that guard traces must now be captured with the trigger disabled, **say so and say
  what that costs in fidelity** — a trace taken with a client behaving differently from the shipped
  one is a real compromise and belongs in the notes, not in a footnote.

### 5 · Does `age_ms` mean anything at this venue? — measure it, do not decide it

Kraken's B2 settled queue-versus-shed by measurement and discharged an assumption rather than
carrying it to M5. **Do the same here, and expect the opposite answer.**

`age_ms` is a sliding-window deficit measured against **liveness arrivals**. At Anvil and Kraken those
come from the subsystem that also emits the book, so a growing gap means a backlog. At Binance the
liveness arrival is a **ping on a fixed ~20 s timer in the WebSocket layer** — worst/median **1.01×**,
metronomic, and structurally indifferent to the book's queue.

- **Throttle a replay and report what the meter reads through a known backlog**, the way
  `kraken_backpressure_probe.py` did.
- The hypothesis to falsify: *`age_ms` reads approximately zero at Binance regardless of how far
  behind the book is.* If it holds, then the transport-versus-feed correction disables **both** of the
  panel's honesty mechanisms at this venue, not one, and that is a third hole from a single cause.
- **Measure and record. C decides what the panel does about it** — including whether the header should
  render *no reading* rather than a number it cannot support, which is the shape M4 stage A2 already
  used for a reconnect.

### 6 · Two captures the later stages cannot proceed without

- **A ~200 s ping-bearing capture.** `kMinSamples = 8` at a 20 s cadence needs ~160 s, and no
  committed slice is that long — so **the calibrated liveness path is unreachable on the host** and C
  would be tuning a mechanism no test can enter. Quiet pair, deliberately not gradeable, just long.
- **`event_ns`-on-REST is ANSWERED — do not re-open it.** B1 closed open unknown 4 in the strong
  form: `event_ns` **does** reach REST records (stage A sets it from `req.recv_ns`), which is exactly
  what lets section 3 read round-trip times out of committed files. Section 3's premise *assumes*
  that answer, so asking for it again is the brief asking for a fact it is already using.
- **The other stamp is `req.sent_ns` — note the spelling — and it is ANSWERED too: present on 27 of
  27 committed REST records**, counted from the planning seat 2026-08-26, plus 8 of 8 in the two
  mixed captures. The round trip is therefore readable out of every REST record the corpus holds and
  **the schedule is replayable**. Nothing owed; the count is recorded here so the next stage does not
  re-ask it.

### 7 · Writeback

Session log; `NOTES-binance.md` for the trigger, the sizing arithmetic, the capture-recipe
consequence and section 5's result; ROADMAP M5 line; `DESIGN.html` for the trigger if it draws the
adapter; and §9 for anything with architectural weight — section 5's answer probably qualifies, and
section 4's consequence certainly does if it changes what a guard trace can be.

---

## Constraints

- **§6 frozen. §4 does not move.** No new `GapReason`, no new `FeedEvent::Kind`. Fifth asking; stop
  and raise.
- **Desk only.** No `firmware/`, nothing flashed. The board's re-seed behaviour is D.
- **Do not decide the liveness threshold, `kThresholdCeilingMs`'s changed role, or what the panel
  renders.** All C's. This stage supplies C with measurements it cannot take for itself.
- Rate limits: 300 connection attempts per 5 minutes per IP; REST weight 5 / 25 / 50 / 250 by tier.
  **Do not loop reconnects, and do not poll `limit=5000`.** A ban costs the evening.
- Pin tables add-rows-only. No existing golden moves; the nine Anvil and Kraken traces byte-identical.
- **Per-commit verification in a fresh detached worktree, `CMAKE_HOME_DIRECTORY` confirmed and
  normalised for separator and case, loop run inline.** `powershell -File` fails at
  `CMakeTestCXXCompiler`, unrooted. A restored file keeps its old timestamp and `make` will not
  rebuild it — `touch` after restoring, or verify by content.
- **Commit only when asked.**

## Known unknowns — resolve and record

Whether the low-water mark is a sufficient trigger on its own or needs a second term. What the p99
fetch latency actually is, as opposed to the max — **26 REST records cannot yield a p99, so this is a
capture question before it is a measurement one** (section 3, third bullet; owner's decision
outstanding). Whether the mixed-cadence
coincidence survives slicing. Whether a Binance guard trace can exist at all once the trigger ships.
Whether `age_ms` is measurable-but-meaningless here or simply unmeasurable.

## Definition of done

- ☑ Both mixed-cadence captures **committed WHOLE** (owner's ruling), and pinned; **§9 row 2's clause struck and replaced
      by the commit SHA**, and the row's *second* stale clause — *"B2's soak is where it comes from"* —
      either corrected to name **D** or raised.
- ☑ The re-snapshot trigger is driven by a **corrected** low-water instrument, **not by a clock**, with both
      failure directions costed.
- ☑ Margin sized as **walk rate × a bounded fetch deadline T** — owner's ruling: `p99` struck
      rather than softened, because the round trip has no tail worth sizing against on this path
      (n=23 at `limit=100`, max/median **1.053**; n=13 at `limit=1000`, **1.058**) and because
      all 36 committed records are **desk-box** figures against a board where B3 measured DNS at
      14,000 ms. `T = 15,000 ms`, worst ≤15 s coverage loss **168 levels**, trigger at **448**;
      the arithmetic reported, the sample size stated beside every figure, and the
      round-trip-versus-write-lag distinction written into `NOTES-binance.md` **beside both
      pairs, in place**. T is recorded as a required transport property with a §9 row.
- ☑ The capture-recipe consequence of a scheduled healing event stated, and what a Binance guard
      trace can still be.
- ☑ **`age_ms` measured through a throttled replay**, the hypothesis confirmed or falsified with
      figures, handed to C undecided.
- ☑ The ~200 s ping-bearing capture taken and committed (221 s, 11 pings); **the calibrated liveness path reachable on
      the host** and shown to be, by a test that enters it.
- ☑ **Answered before the stage opened, recorded rather than deleted:** `req.sent_ns` is on **27/27**
      committed REST records (planning seat, 2026-08-26), so the round trip reads out of every one.
      `event_ns`-on-REST was already answered at B1, open unknown 4. **Nothing owed.**
      *B2 re-counted after committing three more captures — **36/36** — and added a corpus-wide
      assertion in `binance_frame_economics.py --selfcheck` so it cannot be lost quietly. Not a
      pinned column: that would mean editing all eleven existing rows, and add-rows-only is that
      table's first rule.*
- ☑ Green (50/50); no golden moved; session log; ROADMAP; NOTES; §9 where it has weight; split proposed;
      nothing committed.

## Out of scope

The liveness threshold, the ceiling's changed role, the four unbuilt remedies in strain 26, and what
the panel renders in any of these states (**C**). The bench, the >24 h soak, the board's audit stream
(**D**). Re-capturing the seven stage-0 slices. The client ping (M6). The venue toggle (M7). M4's
carried bench residues — D1, D2, D7's scope trace. The §1-versus-reality trade-print question — ROADMAP
backlog **D0**, owner's, on no stage.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     measured figures / evidence · exact next step. -->

### 2026-08-26 · Claude Opus 5 (1M context) · the seed, the walk and the schedule

**Green.** 50/50 ctest, no golden moved, the nine Anvil and Kraken traces byte-identical.
Nothing committed; a split is proposed below.

**Owner's rulings taken before any work, and both changed the deliverable.**

1. **Section 3: drop `p99`, do not soften it.** The corpus cannot support a p99 and does not
   need to — pooled, `limit=100` is n=23 with max/median **1.053** and `limit=1000` is n=13
   with **1.058**, and choosing max over median moves the margin by 87.6 ms = $0.029 against
   a $224.52 window, **0.013%** of it. The finding is *the round trip has no tail worth
   sizing against on this path*, not a percentile. No 75-minute capture: 75 minutes to
   resolve 0.013% is the wrong trade on an evening carrying the §9 expiry. **And the
   addition that changed the shape of the deliverable:** all of those are desk-box figures
   and the margin is for a board where M4 B3 measured DNS at a flat 14,000 ms, so size
   against a **bounded fetch deadline T** — `margin ≥ walk rate × T` — which covers 100%
   of fetches by construction rather than 99% of a sample. B2 specifies T and records it as
   a required transport property; implementing it is C/D's.
2. **Deliverable 1: commit both captures WHOLE.** The size argument was in the wrong
   currency — gzip −9 makes them 246,677 B and 178,652 B, so the choice was worth ~250 KB of
   pack — and the ≤900 KB "convention" is not one, since both ATOMEUR files are already
   byte-identical to their `_local` originals. The principle decided it: slice, and the
   row's original assertion *still* has no committed trace, so you would be amending the
   ruling to fit the artefact. Struck **with the SHA in its place**, not struck-and-amended.

**Done.**

1. **§9's untracked-evidence clause discharged.** `binance_btcusdt_mixed{1,2}_20260825`
   committed whole and pinned in four places (CMake trace list, `taxonomy_pins.inc`,
   `binance_pins.py`, `dc_binance_oracle_main.cpp`). Procedure order held: **mutants →
   `--selfcheck` → `--pin`** — both were mutation-verified (honest GREEN, 3/3 exercised and
   RED, the asserted no-op still a no-op) *before* any figure was written down. They
   reproduce the ruling exactly in two implementations: **90/90 coincidence, 88/88 GREEN**
   from `binance_oracle.py`, and **88 matched / 0 failed** from `dc_binance_oracle` driving
   the real adapter. They also joined `binance_oracle_mutants`, which closes a gap nobody
   had named: **every earlier witness runs `@depth20` at 100 ms and the board runs it at
   1000 ms**, so no mutant had ever been run at the shipped cadence.
   *The coincidence-survives-slicing question does not arise: nothing was cut.* Checked
   anyway that this is not a dodge — `slice_trace.py --mode baseline --window 120
   --require-baseline` accepts both and emits a byte-identical file.
   The row's **second** stale clause is corrected in the same edit: *"B2's soak is where it
   comes from"* now names **D**.
2. **The re-snapshot trigger, and B1's instrument was measuring the wrong quantity.**
   `min_bid_levels` counts levels *held*, which only ever grows — so on the two committed
   captures where the 82.4% failure class actually happens it reads a flat, healthy
   **100/100** while the book goes wrong. Seeded **coverage** (held levels still inside the
   price range the seed described) is what erodes: on `binance_btcusdt_reconnect` the bid
   side goes **100 → 0 in 1.2 s**, faster than one REST round trip. Both counters kept and
   printed one line apart; the retired one is not deleted.
3. **The margin, sized and reported as arithmetic.** `margin ≥ walk rate × T`,
   T = `kBinanceFetchDeadlineMs` = **15,000 ms**; worst coverage lost over any ≤15 s window
   across nine captures = **168 levels**; margin **192** (168 + 14%, because 168 is a max
   over a sample, in the market whose depth requirement moved 5× in an hour); trigger at
   `kBinanceEmitDepth + 192` = **448**. 15 s is not a fresh guess — `capture_binance.py`
   already runs this fetch at 15 s behind `REST_DRAIN_TIMEOUT_S = 20` — and it survives the
   sanity check: 15 s is **2.2%** of `limit=1000`'s ~677 s window and **32%** of
   `limit=100`'s ~47 s, which is an independent argument for `limit=1000` arriving from the
   schedule rather than the depth sweep.
4. **Round trip versus write lag, written beside BOTH pairs in `NOTES-binance.md`** — in
   place, as block markers at each section, not only in the addendum. The write-lag corpus
   max is now **48,969 ms** (the silent-stream fixture, where there was no next message at
   all), which states the mechanism as plainly as it can be stated.
5. **`age_ms` measured, and the hypothesis narrowed rather than confirmed.** New
   `dc_age_probe` injects a backlog it knows exactly and reports what the real
   `AgeEstimator` makes of it. **Socket backlog: TRACKS** (1,797.1 s read against 1,796.8 s
   injected, 1.00×) because the ping is a control frame on the same TCP stream. **Feed
   backlog: BLIND** (0.3 s through the same 1,797 s) — and that mode is *physical at
   Binance and not constructible at Anvil or Kraken*. Pinned in both directions at all three
   venues; the Anvil and Kraken socket columns land at 0.99× and 1.00× of their **window
   ceilings**, independently reproducing `age_estimator.hpp`'s own worked table of 384 s and
   768 s. **Separate finding, and nothing had named it: the meter has no reading at all for
   the first ~639 s** — 32 baseline intervals at a 20 s ping cadence — against 16 s at
   Anvil and 32 s at Kraken.
6. **The calibration capture, and a flag that exists because a threshold cannot answer.**
   `binance_atomeur_d100ms_liveness_20260826` — 221 s, quiet pair, single-stream, one seed,
   **11 pings / 10 intervals** against `kMinSamples`' 8, 20,604 bytes, total IP weight 5.
   First committed trace that lets a host test enter the self-calibrated liveness branch.
   `ReplayResult::liveness_calibrated` was added because at this venue `threshold_ms`
   **cannot** distinguish calibrated from uncalibrated (4 × 19,969 clamps to the same 30,000
   the uncalibrated default holds), so a test asserting on it would be a test that cannot
   fail. The inertness is now asserted in the form that C's change breaks.
7. **`req.sent_ns` — not re-opened, and guarded.** Present on **36 of 36** committed REST
   records with `req.recv_ns`; nothing was owed. Added a corpus-wide assertion in
   `binance_frame_economics.py --selfcheck` rather than a pinned column, because a
   `rest_stamped` figure would mean editing all eleven existing rows and that table's first
   rule is add rows only.

**Decisions, with why.**

1. **The trigger latches; it does not fetch, and it does not rate-limit itself.** Kraken's
   `resync_wanted()` shape. The adapter has no clock and cannot see the IP-weight budget, so
   **T and a re-seed rate bound are recorded as required properties of the transport** and
   the adapter latches once per seed epoch. Too eager costs 50 weight a fetch and the venue
   bans on breach; too late is the 82.4% failure class.
2. **A seed that arrives below its own margin does not arm the trigger.** Re-fetching would
   return the identical shortfall at 50 weight a time. Two causes — the request was too
   shallow, or the venue's book is genuinely that shallow — and the adapter *cannot* tell
   them apart because it does not know what `limit` was asked for. **It does not need to:
   the response is the same either way.** Reported as `seeds_below_margin`; whoever holds
   the request can tell which it is. On BTCUSDT at `limit=100` this fires on arrival, about
   a minute before the book actually goes wrong.
3. **A re-snapshot on a live book is still not adopted, and now it is counted.** Rolling one
   forward needs the diffs it is behind by, and buffering those for a 15 s deadline is
   ~128 KiB against the pre-seed buffer's measured 15 events / 823 levels. Three candidate
   mechanisms, all costing board memory (D) or a rendered state (C); **the board's re-seed
   behaviour is D's**, so this stage supplied the measurement instead — DESIGN strain 28.
4. **Coverage is measured once per frame with `ladder::rank_of`**, not maintained
   incrementally. ≤1,024 integer comparisons at ≤10 frames/s is microseconds on an LX7, and
   an incremental count would be a fourth mutation site that can drift silently. The scan
   uses the shared helper rather than a private loop, because `ladder.hpp` exists so that
   "which of two prices ranks better" has one definition.
5. **Both slicing rules named in §9 rather than only in the two taxonomy rows.** A guard
   trace pins a behaviour and should be cut small; a ruling's evidence pins a decision and
   cannot be cut at all. Without the general rule, committing two whole captures reads as a
   precedent that traces may now be large.

**Measured figures.**

| | |
| --- | --- |
| REST round trip, `limit=100` | n=23, 958.0 / **1,009.4** / 1,063.0 ms — max/median **1.053** |
| REST round trip, `limit=1000` | n=13, 973.0 / **1,503.1** / 1,590.7 ms — max/median **1.058** |
| `req.sent_ns` + `req.recv_ns` | **36 / 36** committed REST records |
| record write lag (`rx_ns − recv_ns`) | median 76.5 ms, **max 48,969 ms** (silent-stream fixture) |
| worst coverage lost, ≤1.5 s / ≤15 s | **154** / **168** levels, over nine captures |
| worst single event | one 100 ms tick, best bid −$15.99, **135 covered levels at once** |
| low-water coverage, `limit=1000` | 811, 834, 771, 888, 922 — trigger at 448, never fired |
| low-water coverage, `limit=100` | **0**, 0, 12, 97 — trigger never armed |
| re-snapshot loss-free to adopt | **0 / 7** at `limit=1000` on BTCUSDT; **19 / 19** elsewhere |
| snapshot instant within the round trip | `limit=100` **1.00**; `limit=1000` **0.21–0.86**, median ~0.76 |
| `age_ms`, socket backlog | **TRACKS** 1.00× (Anvil 0.99×, Kraken 1.00× of their ceilings) |
| `age_ms`, feed backlog | **BLIND** — 0.3 s through 1,797 s |
| age baseline latch | Anvil 16 s, Kraken 32 s, **Binance 639 s** |
| ping cadence, 10 intervals | 19,957.0–20,068.0 ms, median **19,964.0**, worst/median **1.005** |
| gzip −9, the two captures | 1,905,142 → **246,677 B**; 1,426,098 → **178,652 B** |

**Mutation-verified, each applied for real, built, run, reverted, baseline re-confirmed.**

| mutation | result |
| --- | --- |
| `cover()` returns the held count (B1's instrument wearing B2's name) | 2 tests red |
| the coverage trigger inverted | 2 tests red |
| a seed below its own margin arms the trigger anyway (the loop bug) | 1 test red |
| the age deficit taken against elapsed rather than the baseline | all 3 socket columns BLIND, `--check` red |
| a `req.sent_ns` removed from one committed REST record | the new corpus assertion fires |

**Not done, deliberately, and named rather than left to be discovered.**

- **No re-seed adoption mechanism** (strain 28). Trigger, margin, deadline and adoptability
  measurement supplied; the mechanism costs board memory and a rendered state.
- **No committed capture exercises the trigger's firing path**, because every `limit=1000`
  seed stays at 771 or better and every `limit=100` seed is below its margin on arrival.
  That is a fact about the corpus, not a gap in it; the crossing is synthesised in
  `test_binance_adapter.cpp` under §9's 2026-08-18 rule, and three mutants prove the tests
  discriminate.
- **`dc_age_probe` synthesises LENGTH, never cadence.** No capture reaches the 639 s
  baseline, and taking an eleven-minute one to watch a metronome would buy a number the
  measured cadence already gives. The report says how many arrivals were real on the same
  line as the verdict.
- No `firmware/` change, nothing flashed, no panel decision, no §4 or §6 movement, no
  threshold or ceiling touched, no existing golden moved, no committed trace reshaped.

**The split, approved and executed 2026-08-26.** Seven commits. §9's discharge names
`a67f2e1` — the commit that landed the two captures and their pins — because a commit
cannot contain its own hash, so the strike is the commit *after* the evidence rather than the
same one. Each was verified in a **fresh** detached worktree with `CMAKE_HOME_DIRECTORY`
confirmed and normalised for separator and case, with the loop run **inline** (`powershell
-File` dies at `CMakeTestCXXCompiler`, unrooted).

**Exact next step.** Nothing is pushed at the time of writing; the owner has not asked for it. *(Superseded — see the post-commit correction at the end of this log: commits 1–7 were pushed at 18:13:47 from outside this session, thirteen minutes after this line was written. Left standing rather than edited, because a next-step line that was true when written and false an hour later is the exact shape this stage spent its review pass on.)* Then **C**, which now
has: the `age_ms` figures in both backlog modes, a committed trace that enters the calibrated
liveness path, and an inertness asserted in the form its change will break.

**Found at review, and it is the finding with the longest reach.** The owner's `code-review`
pass over this diff turned up that **`dc_age_probe` had written its own interpolated median**
— and `sample_window.hpp` says in as many words that the convention has one home, a lower
median by nearest rank, because an interpolated one invents an interval that never occurred
on the wire. The wrong figure (19,969.4 ms) had already reached five documents and one test,
**and the test passed**, because `Approx(19969.4).epsilon(0.001)` is a *relative* tolerance
spanning 19.97 ms and therefore accepts 19,964.0 too. A tolerance wide enough to accept both
candidate answers is a test that cannot fail — the 2026-08-16 clause arriving as a tolerance
rather than as an oracle.

Chasing it found the older instance: **`harness/src/trace.cpp`'s statistics pass carries a
second copy of the convention and interpolates**, so `dc_replay`'s reported cadence is not
the cadence `LivenessClock` computes. The two agree to 0.1 ms at Anvil and Kraken — flat
cadences, the coincidence class again — and disagree on every Binance capture with an even
interval count. **Every Binance cadence figure this project has quoted is the interpolated
one**, including `taxonomy_pins.inc`'s 19,951.7 / 20,011.6 / 20,013.3 and B1's 20,004.8.

Fixed in this stage's own code, corrected in every figure this stage wrote, and **recorded
rather than fixed for `trace.cpp`**: `median_gap_ms` shares that path and its figures are
quoted across three NOTES files and a dozen briefs, so the convention change is a
documentation sweep with its own scope. Pinned meanwhile by a test that must be **inverted**
when the fix lands. ARCHITECTURE §9, 2026-08-26; `NOTES-binance.md`'s closing section.

### 2026-08-26 · Claude Opus 5 (1M context) · follow-up: one fix and three records

The owner's approval note (`SEND-TO-depthcharge-cc-B2-approve.md`) arrived **after** the seven
commits had been created and while their per-commit verification was running, so its four items
land as commits 8–10 on top rather than folded into the split. Nothing in them is behaviour.

**§0 — the `SHA-PENDING` check, and the honest correction to the note's premise.** The note says
the check cannot pass because the brief carries the token at lines 384–385. **It did when the
split was proposed and it does not now**: executing the split rewrote that paragraph into a
description of what had been done, so the token left the tree with the instruction that named it.
Both the scoped and the unscoped forms return **0**.

That is a *weaker* resolution of the same hazard than scoping, because it worked only because the
instruction happened to be rewritten — so the rule earns its place regardless, and is recorded in
`CLAUDE.md` beside the `powershell -File` note as tooling rather than architecture:

> **A sentinel-token check cannot live in a file that quotes its sentinel.** Scope the search to
> the files that can carry the token, or the guard reports a hit for ever and gets waved through.

The danger named — *the wave-through is indistinguishable from waving through a real leftover* —
is the same shape as the three-way family in §3 below, and it is a fourth way a green check is
wrong: a guard whose expected output is a known false positive.

**§1 — the `trace.cpp` deferral now carries the right reason, and an owner.** The reason recorded
at proposal was sweep size; the load-bearing one is **golden movement**: adopting the shared
convention rewrites the cadence figures quoted inside `taxonomy_pins.inc`, and *no existing golden
moves* is what makes a seven-commit split reviewable — bundled in, nothing in the diff would
distinguish a convention change from a defect. **A convention change that moves pins must be its
own stage, so the moved pins have nothing else to hide behind.** The sweep is the consequence.

**Owner: M5 close-out**, taken as proposed and not left blank — *"its own scope" is unowned, and
commit 2 of this very split exists to close an unowned clause in §9.* **Expiry:** when `harness/`
and `engine/` compute the median by one convention. **Tripwire:** if any stage before the
close-out needs to quote or re-pin a Binance cadence figure, this closes first. Carried in three
places with invert-not-delete wording: `test_binance_adapter.cpp`'s two-conventions case,
`taxonomy_pins.inc`'s Binance comment, and DESIGN strain 29.

One precision worth having on record rather than overclaiming: the convention change moves figures
**quoted inside** `taxonomy_pins.inc`, not a pinned *column*. No median is a pinned column, and
`liveness_firings` is computed from the clock's own threshold rather than from the report's median,
so it would not move either. The reviewability argument stands unchanged — a diff that touches a
pin file in a stage about something else is unreviewable whether the moved text is a column or a
comment — and the taxonomy carrier says so explicitly, so that whoever closes it does not go
looking for a column that was never at risk.

**§2 — the sample-counted-constant class, and the instance nobody had named.** One §9 row, all
three instances tabulated at all three venues, measured rather than asserted:

| constant | value | Anvil | Kraken | Binance | found at |
| --- | ---: | ---: | ---: | ---: | --- |
| `kMinSamples` | 8 | 4.0 s | 8.0 s | **159.7 s** | M5 stage A |
| `kBaselineSamples` = `kWindowSamples` | 32 | 16.0 s | 32.0 s | **638.8 s** | M5 stage B2 |
| `kAgeWindowSamples` | 256 | 128.0 s | 256.0 s | **85.2 min** | **this note** |

The Anvil column is not new arithmetic — 16 s and 128 s are already stated in
`age_estimator.hpp`'s own text, which is what makes the Binance column's absence the finding
rather than the numbers themselves. The third constant's comment says why the number is what it
is: *"256 × 8 B = 2 KiB, which is the whole reason it is not larger"* — **a memory budget, never
a time span** — and it is the window the age supremum is taken over. **Handed to C undecided**,
with the multiplier and the ceiling.

**638.8 s, not the note's 639.0 s.** The difference is the median correction the note itself
endorses: 32 × 19,963.97 ms rather than 32 × 19.97 s. Stated because a figure that moves between
two documents in the same evening is exactly what this stage spent its review pass chasing.

**§3 — the green-suite family, grouped.** One §9 row naming three ways a suite is green about the
wrong thing, referencing the existing two rows rather than restating them: the input cannot
discriminate (coincidence class), the corpus never contained it (never-observed frame kind), or
the bound admits both answers (new — a relative `epsilon` spanning ±20 ms on 19,969 accepts
19,964.0 too). The row also names what defeats each, and the shared property that makes them one
family: **none of the three fails, so each is found by something other than the suite.**

**§2's one line that was mine rather than C's**, taken as an eighth commit so commit 4's diff
stays the trigger and nothing else: `age_estimator.hpp`'s *"the meter reads `-` for the first 16 s
of an Anvil connection and 32 s of a Kraken one"* was a per-venue cost sentence written before the
venue where it is 638.8 s existed — the stale-line shape B1 named twice, sitting inside the file
this stage's own finding is about.

**Commits 8–10, no behaviour in any of them.**

| # | commit | contents |
| --- | --- | --- |
| 8 | `engine: name the venue the age meter's no-reading window was never sized for` | `age_estimator.hpp` comment; no code path moves |
| 9 | `harness: carry the median-convention expiry in three places, with an owner` | the inverting test's wording, `taxonomy_pins.inc`'s comment, DESIGN strain 29, the NOTES reason |
| 10 | `docs: three §9 records — the deferral's real reason, sample counts as durations, and the green-suite family` | `ARCHITECTURE.md`, `CLAUDE.md`, this log |

**Commits 8–11 not pushed** (1–7 were, from outside this session — see the correction below). Per-commit verification as before: fresh detached worktree,
`CMAKE_HOME_DIRECTORY` confirmed and normalised, loop inline.

**Post-commit correction to §0, measured rather than argued.** The scoped form the approval note
gave — `git grep -n SHA-PENDING -- . ':!docs/briefs/M5-stage-B2*'` — **returns 7, not 0**, on the
tree that carries these records. The two files it does not exclude are the approval note that
*specified the rule* (5 hits) and `CLAUDE.md`'s own statement of it (2). So the guard's first
application after being written down was itself a wave-through candidate.

The failure is in the *shape* of the scope rather than in its extent: **the set of files that quote
a sentinel grows every time anyone writes about it — a brief, a review note, the rule itself —
while the set of files the substitution targets is known at the moment the guard is written.** So
the rule recorded in `CLAUDE.md` is the positive form, `git grep -n SHA-PENDING -- ARCHITECTURE.md`,
which is 0 and stays 0. Commit 11.

**And the push, reported rather than left to be noticed.** `git reflog show origin/master` records
`update by push` at **18:13:47** landing commits 1–7 on `origin/master`; commit 7 was created at
18:00:05 and the isolation ladder for those seven did not complete until after that. No `git push`
was issued from this session, there is no hook in `.git/hooks/`, and no push-related local config —
so the push came from outside it. **The ladder subsequently returned all-green for 1–7, so what is
on origin is verified; it was verified after landing rather than before**, which is the ordering the
rule exists to enforce. Commits 8–11 are local only and are not pushed.
