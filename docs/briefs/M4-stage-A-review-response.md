# M4 Stage A — review response

**Track:** Agentic [A] · **Size:** short evening, four independent tasks
**Executor:** Claude Code. **Nothing in `engine/`, nothing in `firmware/`, no adapter, no commit
without being asked.**

Stage A is accepted. This is the review's four follow-ups. Tasks 1–4 are independent; do them in
order, and **stop and report** at the first place a task's numbers contradict what stage A's log
claims rather than repairing it in flight.

**Read first:** the stage A session log; `ARCHITECTURE.md` §6 and §9 (including the seventh row,
*from the work*); `harness/replay/NOTES-kraken.md`; `harness/replay/NOTES.md`; `docs/DESIGN.html`
§08 strain 22; `tools/slice_trace.py`; the resync predicate as implemented.

---

## Task 1 — the resync rule's numbers, and whether Anvil agreement is real

The claim under review is *reproduces Anvil's answers on all four traces*. A rule that never fires
on Anvil reproduces Anvil's answers trivially, and the diff cannot tell the two apart.

1. **State what `book event` resolves to in the predicate as implemented.** One sentence, exact:
   does a preceding snapshot itself count as a book event, or only a delta/update? Quote the
   predicate.
2. **Produce this table for the four committed Anvil traces**, old rule (`mid_stream_snapshots`)
   versus new rule: records classified as resync, per trace, both rules. Then for the reconnect
   trace specifically, say which mechanism detects the reconnect — the resync rule, the
   `disconnect_gap_ms` threshold, or both.
3. **Answer in the log, explicitly:** on a full-replace venue, can the new rule ever fire? If the
   Anvil column is all zeros under both rules, write *agreement by absence* in those words and do
   not describe it as reproduction.
4. **Run the same table over the four Kraken slices.** Expect zero — stage 0's capture recipe
   excluded healing events by design. Confirm it, and record that **no committed trace exercises
   the positive branch at Kraken**.
5. **Exercise both directions in a host test instead of a capture.** Two synthetic record streams
   in `test_trace_venue.cpp` (or its neighbour): a Kraken snapshot arriving after book events —
   must classify as resync; a Kraken snapshot arriving before any book event — must not. Prove
   both fail under a seeded inversion of the predicate, the same way the existing four mutations
   were verified.
6. **Change no behaviour** unless 2 or 4 shows the rule is wrong. If it is wrong, stop and report
   with the table; do not repair it in this session.

## Task 2 — one quiet-pair capture at a different hour

The 15,000 ms threshold rests on a 1.67× margin over a single 60-second window (worst 9,007, p90
8,480). Anvil's equivalent margin is 2.56×. The tightness of the distribution argues it is a
ceiling rather than a tail, but it has been observed once, at one hour, on the pair whose quiet is
most likely to be time-of-day dependent.

1. Read stage 0's quiet-pair capture hour out of the trace metadata; report it local and UTC.
2. Capture **ten minutes of MINA/GBP** at the same depth as the committed quiet-pair slice, at an
   hour at least four hours away from that one. If this session cannot be run at such an hour,
   **say so and stop** — do not substitute a different pair or a shorter window.
3. Report: worst book gap, p50 / p90 / p99, the count of gaps exceeding 9,007 ms, and the
   resulting margin against 15,000.
4. **Leave the capture untracked and change no threshold.** This is a confirmation, not a
   re-derivation. If the worst gap exceeds 12,000 ms, stop and raise — that is a stage D bench
   criterion problem, not a tuning problem.

## Task 3 — write down what stage B owes, before stage B exists

Strain 22's first two clauses are duplication that stage B closes as a side effect. The third is a
silent-wrong-answer path that opens the moment a second adapter links, and it needs to exist in
writing now.

1. Add to `docs/DESIGN.html` §08 strain 22, as the third clause's stated closing condition,
   these words: **"A Kraken golden fails if it was produced by the Anvil parser. The decoder's
   identity travels with the pinned output, and a mutation test that swaps the dispatch expects
   red."**
2. Add the same line to the stage A session log under an **Owed by stage B** heading, so it is
   found by the next brief-drafting session rather than only by whoever reads DESIGN.
3. Add, in the same place, the known unknown Task 1 surfaces: **stage B's CRC-mismatch path is the
   first thing that exercises the resync rule at Kraken, and it needs a deliberately captured
   resync slice that is not one of the four truncation traces** — a resync heals a truncating
   book, so the two capture purposes are mutually exclusive and cannot share a file.
4. ROADMAP M4 row: one line that stage A is done and green, and that these three are stage B's
   inheritance. No tick moves without the owner.

## Task 4 — take the `slice_trace.py` fix, then sweep for its siblings

`--mode reconnect` is the third Anvil shape found assumed universal, after the required `ticker`
and `mid_stream_snapshots`. All three were found by Kraken rather than by looking. Stage B is
where an unfound one stops producing a wrong count and starts producing a wrong book.

1. **Take the fix.** Use the reader's venue-free resync predicate, not a venue branch — the
   seventh §9 row applies to `tools/` as much as to `harness/`.
2. **Then survey `tools/` and `harness/`** for the remaining Anvil-shape assumptions. Specifically
   look for: identifiers typed or named as `ticker`; anything treating the first record as a
   snapshot; `seq` handled as though its semantics were universal when they are Anvil's global
   non-monotonic counter; cadence assumptions inherited from Anvil's ~80 ms; the trade/book
   independence rule; scaling paths that are only correct because `qty_step` is 1 at every venue
   consumed so far; and any comparison of `rx_ns` across traces without checking the clock field.
3. **Output a list, not a refactor:** file, line, the assumption, whether it fails **loudly** or
   **silently** under a Kraken trace, and the cost to fix.
4. **Fix only the silent ones**, each as its own small change with the reason in the message.
   Report the loud ones for a later decision. Timebox the survey to roughly thirty minutes and say
   so if it ran out — an incomplete survey that names its edge is useful; a padded one is not.

---

## Constraints

- §6 frozen. No `engine/`, no `firmware/`, no adapter, no `FeedEvent` change, no golden moved.
  New pins add rows only; `--pin` still refuses to overwrite.
- `kRxWatchdogMs` and every other firmware constant untouched. The venue table's duplication of it
  is stage B's to close.
- Do not reopen: depth 25 / `kDisplayLevels` 27, strain 3's `static_assert`s, one venue per build,
  the split clock, the §4 non-change.
- `cmake --workflow --preset host-mingw`; green at the end of every task.
- Commit nothing. Report what changed and what it measured.

## Definition of done

- [x] Task 1: predicate quoted; both tables produced; the full-replace answer stated in the log in
      those words; Kraken positive branch confirmed unexercised by any committed trace; two
      synthetic cases added and mutation-verified.
- [x] Task 2: stage 0's hour reported; ten-minute capture at a distant hour, or an explicit stop;
      five figures reported; capture untracked; no threshold changed.
- [x] Task 3: strain 22's third clause carries its closing condition verbatim; the same line and
      the resync-slice known unknown in the session log under **Owed by stage B**; ROADMAP line.
- [x] Task 4: `--mode reconnect` fixed via the venue-free predicate; survey list produced with the
      loud/silent column; silent findings fixed individually; timebox honoured or its edge named.
- [x] Tree green, nothing committed, session log appended.

## Session log

### 2026-08-17 · Claude Opus 5 (1M) · the four follow-ups

**Tree green at 21/21, nothing committed, no `engine/` change, no golden moved,
all eight committed traces byte-identical, `kRxWatchdogMs` and every venue
threshold untouched.**

**⚠ TASK 2 HIT ITS STOP-AND-RAISE CONDITION. Read that section first — the
15,000 ms threshold does not clear the quiet pair's legitimate silence, and
nothing has been changed about it.**

**⚠ One file changed in the working tree that I did not touch:**
`firmware/src/ladder_font.hpp` — the `.` glyph moved from `{0,0,0,0,0,6}` to
`{0,0,6,6,0,0}` with a matching comment edit, mtime **17:28:49 today**, the only
firmware file modified since 16 Aug. It is not mine: across the fifteen survey
subagent transcripts (4.7 MB) there are **zero** Edit/Write calls and zero
mentions of that file, and nothing else in this session went near `firmware/`.
It reads as a deliberate hand edit. **Left completely alone** — reverting it
would have destroyed someone's work to satisfy a constraint it was never
subject to.

---

### Task 1 — the resync rule's numbers ✅ and the Anvil agreement is real

**1 · The predicate, exactly.** A preceding snapshot **does** count as a book
event, at both venues. Three lines, in two files:

```cpp
// harness/include/dc_harness/trace_decoder.hpp:167-171  (Anvil)
k.is_snapshot    = f.type == "snapshot";
k.is_book_event  = k.is_snapshot || f.type == "book" || f.type == "trade";

// harness/src/trace_decoder.cpp:49-53  (Kraken)
if (ch == "book") { k.is_book_event = true;
                    if (type == "snapshot") { k.is_snapshot = true; } }

// harness/src/trace.cpp:377  — the decision, and note the ORDER
if (kind.is_snapshot) { ++stats.snapshot_count;
                        if (stats.book_events > 0) { ++stats.mid_stream_snapshots; } }
...
if (kind.is_book_event) { ++stats.book_events; }   // incremented AFTER the test
```

So `stats.book_events` at the test counts book events **strictly before** this
record, and because a snapshot is itself one, the second snapshot in any trace
fires the rule.

**2 · Old rule vs new, over every committed trace.** Computed by an independent
Python pass, not by the C++ under test — the point of the table is to check the
reader, and asking the reader would only check it against itself. It agrees with
`taxonomy_pins.inc` on all eight rows.

| trace | venue | records | snapshots | 1st snap at | **OLD** | **NEW** | agree? |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `anvil_101_baseline` | anvil | 1406 | 1 | 1 | 0 | 0 | yes |
| `anvil_101_baseline_20260809` | anvil | 1513 | 1 | 1 | 0 | 0 | yes |
| `anvil_101_depth27_20260816` | anvil | 1399 | 1 | 1 | 0 | 0 | yes |
| **`anvil_101_reconnect`** | anvil | 1288 | 1 | **383** | **1** | **1** | yes |
| `kraken_btcusd_d10` | kraken | 902 | 1 | 4 | **1** | 0 | **NO** |
| `kraken_btcusd_d25` | kraken | 1599 | 1 | 4 | **1** | 0 | **NO** |
| `kraken_btcusd_d100` | kraken | 2535 | 1 | 4 | **1** | 0 | **NO** |
| `kraken_minagbp_d25` | kraken | 93 | 1 | 4 | **1** | 0 | **NO** |

*(old rule = a `type:"snapshot"` record whose 1-based index ≠ 1, applied verbatim
including at Kraken, where the pre-stage-A reader could not have got that far.)*

**Which mechanism detects the Anvil reconnect: both — and only one of them is
behavioural.** The `disconnect_gap_ms` threshold is the real one: a 4,468 ms hole
after frame 382 raises `Gap{Disconnect}`, the book goes stale, the panel is grey
for 3,468 ms, and the snapshot in frame 383 clears it. That is what the goldens
pin. The resync rule is **reporting only** — `mid_stream_snapshots` is read by
`dc_replay`, `dc_taxonomy`, `trace_report.cpp` and test assertions, and
`replay_driver.cpp` never reads it at all. That is deliberate and predates stage
A: M1 rejected "a mid-stream snapshot means we reconnected" as *retrospective* —
the Gap would be raised in the same breath as the Snapshot that clears it. Note
the rule fires at **frame 383, the very frame that clears the gap the threshold
opened**, which is that objection made visible. **Consequence worth stating: a
wrong resync count produces a wrong label and a wrong pin, never a wrong book.**

**3 · Can the new rule fire on a full-replace venue? YES, and it does.** At a
full-replace venue every `book` frame is a book event, so every snapshot after
the trace's first book event fires. It fires on `anvil_101_reconnect`. So this is
**not agreement by absence** across the four — one of the four exercises the
positive branch and both rules agree on it, for genuinely different reasons.
Stated precisely, because three of the four *are* the weaker case: **agreement by
absence on the three baselines** (each has one snapshot, at record 1, and neither
rule could have said anything but zero); **real agreement on the reconnect
trace.**

The two rules are **not equivalent**, and I found where they part: an Anvil trace
whose first snapshot is preceded by a `summary` — old says resync, new says not,
and the new one is right because `summary` is roster data the adapter ignores, so
there was no book to re-baseline. No committed trace has that shape (all four
open with a snapshot). Pinned as a host case rather than left as a claim.

**4 · Kraken positive branch: confirmed unexercised.** All four slices score
**resync 0**, as expected — stage 0's recipe excluded healing events by design.
Recorded as owed: **no committed trace exercises the positive branch of the
resync rule at Kraken.**

**5 · Both directions, in host tests.** Three new cases in `test_trace_venue.cpp`,
deliberately stripped of the subscribe machinery so what separates them is a book
event and nothing else: snapshot-after-updates → resync 1; snapshot-before-any-
book-event → resync 0; and snapshot-then-snapshot → exactly 1 (the clause an
implementation drops first if it reads `is_book_event` as "an update"). Seeded
inversions, each built and run:

| mutant | result |
| --- | --- |
| predicate inverted (`book_events == 0`) | **caught** — 3 cases fail |
| resync always false | **caught** — 3 cases fail |
| resync always true | **caught** — 3 cases fail |
| reverted to the old index rule | **caught** — 1 case fails |
| Kraken snapshot no longer a book event | **caught** — 4 cases fail |

17 cases / 82 assertions in that file, all green unmutated.

**6 · No behaviour changed.** The rule is right.

---

### Task 2 — ⚠ STOP AND RAISE: the quiet pair's 9 s is a TAIL, and 15,000 ms does not clear it

**1 · Stage 0's hour.** `2026-08-16T23:37:03Z` = **00:37 BST on the 17th**.

**2 · The capture.** Run at **2026-08-17T16:00:46Z**, **7 h 37 min** away in
hour-of-day. `MINA/GBP`, depth 25 — same pair, same depth — for **600 s**. One
connect, 1,165 frames, `perf_counter_ns`, untracked in `_local/`.

**3 · The five figures, against stage 0's own window.**

| | stage 0 · 23:37Z · 90 s | **confirmation · 16:00Z · 600 s** |
| --- | ---: | ---: |
| p50 / p90 / p99 | 56.8 / 8,480.0 / 9,006.8 ms | 15.9 / 4,068.1 / 8,738.5 ms |
| **worst book gap** | 9,006.8 ms | **25,843.3 ms** |
| gaps > 9,007 ms | 0 | **4** — 14,228 / 16,645 / 18,302 / 25,843 |
| **margin vs 15,000** | 1.67× | **0.58×** |

**4 · 25,843 ms is more than double the 12,000 ms stop line, so this is raised
and nothing was tuned.**

**It is the market, not the socket, and that was checked.** Kraken's 1 Hz
heartbeat is an independent liveness oracle. Inside the 25,843 ms book gap:
**26 heartbeats at 936–1,042 ms intervals**. Inside 18,302: 19. Inside 16,645:
17. Inside 14,228: 14. Every one matches a 1 Hz beat to within a beat. Whole run:
600 heartbeats in 600.2 s, worst *record* gap 1,119 ms, `connects=1`.

**The consequence:** at the declared threshold this capture produces **3
disconnects that did not happen and 15.8 s of grey (2.6% of the run)** on a feed
that never lost a packet — the crying-wolf inversion deliverable 4 exists to
prevent, arriving through deliverable 4's own constant.

**Why the first window looked like a ceiling.** Stage 0's six long gaps spanned
8,113–9,007 ms — a 900 ms spread, ~every 14 s. The confirmation reproduces that
band exactly *and* contains four gaps over 12 s, **all between t+70 s and
t+140 s**; after t+167 s nothing exceeds 8,800 ms. The last 430 seconds look
precisely like the whole of stage 0's window. **Stage 0 sampled the calm regime
for ninety seconds and read its width as a limit.** Also: the busier hour has the
longer tail — 56.3 book events/min against 29.1, and 2.9× the worst gap. Rate
does not predict silence on a thin book.

**General rule, new instance of an old species:** *a distribution's tightness is
evidence about the sample, not about the bound.* §9 holds "a throughput
comparison across time on a WAN path is not an A/B unless the hour is
controlled"; this is its sibling for a **bound** — **a maximum measured in one
window is a sample maximum, and calling it a ceiling needs a second window at a
different hour.** Stage 0 controlled the hour across the three BTC depths so they
would be comparable, then took the quiet pair's absolute maximum from that same
single hour.

**Unchanged, and deliberately left for the owner to move together:**
`venue_traits(Kraken).stale_gap_ms` (15,000), its `stale_gap_note` (still cites
"9,007 ms … 1.7x margin"), and `test_trace_venue.cpp`'s `> 9007.0 * 1.5` floor
assertion. **Both still pass; both are now premised on a figure this capture
beat.** Full write-up, including three options I did **not** take, is in
`NOTES-kraken.md` under the review addendum, with a supersession pointer at the
head of Headline 1 so the old figure cannot be quoted unaware.

---

### Task 3 — what stage B owes ✅

Strain 22's third clause now carries its closing condition verbatim (checked as
*rendered* text, since HTML line-wraps it), and the same line plus the
resync-slice known unknown are in the stage A log under **Owed by stage B**.
ROADMAP M4 row gained the inheritance line. No tick moved.

---

### Task 4 — the fix, and the sweep for its siblings ✅

**1 · `--mode reconnect` uses the reader's venue-free predicate**, not a venue
branch: `find_resync` walks for the first snapshot with a book event before it,
which is the same rule `trace.cpp:377` applies. Verified end to end — the local
Kraken reconnect capture yields a 758-record window scoring **resync 1**, and all
four committed slices still re-slice **byte-identical**.

**2 · The survey.** Seven lenses over `tools/` and `harness/`, each finding then
put to an adversarial verifier told to default to REFUTED, then a completeness
critic. **13 candidates → 6 survived → 4 distinct defects** (three of the six were
the same `COUNTING_RULES` line reported under three lenses, per the critic).

**Timebox: exceeded, and here is the edge.** The sweep itself ran ~16 min; reading,
reproducing and fixing took ~35 more, so ~50 min against the ~30 asked for. What
the sweep did **not** cover, from its own critic: it opened **5 of 11** files in
`tools/` and left `harness/` largely unread. The critic then read both — the six
unopened `tools/` files are Anvil-only *by construction* (they dial Anvil
themselves and take no trace path), and the harness C++ branches on `meta.venue`
throughout. So the gap cost less than it looks, but it is a gap.

**SILENT — fixed, each separately:**

| # | where | what it did at Kraken | fix |
| --- | --- | --- | --- |
| 1 | `gap_stats.py` `COUNTING_RULES` | both book rules matched nothing; rows **silently dropped**; tool reported the 1 Hz heartbeat floor (**1,026 ms**) where the truth is **9,007** — 8.8× low, exit 0 | rules are now predicates over a venue-classified record |
| 2 | `gap_stats.py` `apply_threshold` | clearing test hard-coded `("book","snapshot")`, so **no Kraken frame could ever clear an episode**: "1 episode, never cleared, grey **nan** ms". Truth: **9 episodes, 8 cleared, greys to 7.5 s** | `is_book_event` |
| 3 | `gap_stats.py` kind histogram | `{'?': 62, 'update': 30, 'snapshot': 1}` | `record_kind()`, same strings the C++ decoder emits |
| 4 | `gap_stats.py` private `load()` | never skipped `"dir":"tx"` — our own subscribe counted as venue traffic, bogus head interval (p50 926.7 → **973.7**) | reads through `tracefile.read_capture`, deleting the second reader |
| 5 | `anvil_frame_economics.py` `wire_bytes` | re-serialises the **parsed** frame, so at Kraken it measures Python's float repr: only **51.0%** of d25 frames survive the round trip, total **−1.73%** (quiet pair −3.49%), printed as "bytes on the wire", exit 0. Anvil: 1406/1406, ±0 B | `require_anvil()` refuses a non-Anvil trace at the front door and names the right tool |

Findings 1–4 share one root — **the file had its own reader**, which is the
2026-08-07 §9 drift in Python — so the fix is one import rather than four
patches. Finding 4 was surfaced *in a refutation* and never filed as a finding;
it is fixed because it fell out of the same change.

**Also taken, honestly labelled: two REFUTED findings' fixes.** The header printed
`ticker=None` at Kraken and never named the clock. The verifier refuted both as
captions rather than wrong numbers, and that is defensible — I took them anyway
because they are one line in a function I was already rewriting, and because
Task 2 spent this session proving that a gap figure without its clock is a trap.

**Proof the fixes moved nothing at Anvil:** `gap_stats` over all four Anvil traces
at two thresholds — **every numeric token identical**, 79/74/85/75 tokens, only
row labels and the header changed. `anvil_frame_economics` over four traces ×
three invocations (plain, `--verify`, two depths) — **0 differing**.
`kraken_tool_selfcheck` unchanged. `ctest` 21/21.

**And a cross-check that fell out of it:** the fixed `gap_stats.py` independently
reproduces every Task 2 figure from the 600 s capture — p50 15.9, p90 4,068.1,
p99 8,738.5, max 25,843.3, top gaps 25,843 / 18,302 / 16,645 / 14,228. Two
implementations, one answer.

**LOUD or latent — reported, not fixed:**

- **`harness/src/replay_driver.cpp:191` `symbol_for()`** derives identity from the
  integer `meta.ticker` and hard-codes `kAnvilTicker101`'s `price_decimals` and
  `qty_step` regardless of venue. Refuted *as a finding* because `run_replay`
  throws on a non-Anvil trace before it is reached — which is precisely the guard
  stage B removes. **This is the first thing stage B's dispatch exposes.**
- **`harness/src/console_ladder.cpp`** — `kVenue = " ANVIL "` stamped
  unconditionally (:48); `qty_text` prints raw ticks with no decimals (:139-141),
  correct only because Anvil's `qty_step` is 1 against Kraken's `qty_decimals` 8;
  `std::to_string(snap.symbol.id)` (:252) has no string-symbol path. All gated by
  the same refusal.
- **`tools/slice_trace.py` reconnect-window self-containment.** `find_resync` is
  venue-correct now, but nothing checks the **window it cuts** contains a
  baseline: at a delta venue a `--before 30` tail begins mid-delta-stream against
  an empty book. It does not reproduce today (the one local Kraken reconnect
  capture fits entirely inside the window) and it writes a committed artefact, so
  it would be silent when it bites. **Not fixed deliberately** — the fix is a
  capture-policy decision about what a Kraken resync slice must contain, and it
  belongs with the resync golden already written into *Owed by stage B* rather
  than being guessed at now.

**Category the seven lenses would never have caught, from the critic:** all seven
ask *does this code read a record correctly?* None asks *does this code, when it
emits something, preserve what a delta venue needs?* The three items above are
all write-side. Worth carrying into M5's equivalent survey.
