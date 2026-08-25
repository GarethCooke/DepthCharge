# M5 Stage A — the two records that are not frames

**Track:** Agentic [A] · **Status:** Done 2026-08-25 · **Size:** one evening
**Executor:** Claude Code, **desk only. No board, no flash, no adapter, no `firmware/` change.**

**This is M4 stage A again, and the argument for putting it in front of the adapter is stronger this
time.** There the reader could not read a Kraken capture and the fix was a metadata tag. Here the
reader cannot represent two of the three things a Binance session produces: **a REST snapshot body is
not a frame the venue sent, and a ping payload is not JSON at all.** Invariant #6 is *no merge
without replay coverage*; an adapter written before the reader is an adapter that **cannot be
covered**, and the pressure at that moment is to reshape the committed traces to suit the reader.
Stage 0 deliberately did not reshape them. **The reader learns the wire; the wire does not learn the
reader.**

The owner's ruling of 2026-08-25 is what makes this one stage rather than two: the ping that stamps
the liveness clock and the REST body that grades the book both need the same widening, so they are
**one trace-contract change, made once, here.**

**Read first**

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §9, the two **2026-08-25 (M5, owner's ruling)** rows | The decisions this stage implements. Row 1 is the mechanism; row 2 is why the audit stream is in the shipped configuration. |
| `ARCHITECTURE.md` §4 | Frozen, and it does **not** move tonight. See Constraints. |
| `ARCHITECTURE.md` §6 | #1, #2 and #6. #6 is the reason this stage exists at all. |
| `docs/briefs/M5-the-shape-and-the-two-decisions.md` | The stage split and the rulings in full. **Deliverable 0 is already discharged — do not transcribe again.** |
| `docs/briefs/M5-stage-0-price-the-binance-wire.md` | The measurements, and the record-shape proposal this stage implements or overrides. |
| `docs/briefs/M4-stage-A-the-replay-dialect.md` | The precedent: additive tag, absent-means-default, per-venue decoders, and the sentence about reshaping traces. |
| `harness/replay/NOTES-binance.md` | The observed wire. **Where these notes and this brief disagree, the notes win** — they were measured. |
| `harness/include/dc_harness/trace_decoder.hpp` | The contract being widened, including the `static_assert` at line 155. |
| `docs/DESIGN.html` §08 strain **22** | Adding a venue is four edits in two languages and only three fail loudly. Stage 0 says the card understates it; deliverable 3 fixes the shape and the card. |

**Depends on:** M5 stage 0 ✅, both rulings ✅ in §9. **Blocks:** B1, B2, C.

---

## Deliverables

### 1 · The record grows a kind, and it carries two things rather than one

Every trace record today is `{"rx_ns": <ns>, "frame": <verbatim JSON>}`. Both new records break that
shape, and the ruling makes them one change rather than two.

Add a discriminator to the record. **Not a second file, not a parallel stream.** The exact spelling
is the session's call; three properties are not:

- **Absent means `frame`.** All eleven existing committed traces must be byte-identical through this
  change and no golden may move — the same additive discipline that let M4 stage A add `venue`.
- **A REST record carries the request as well as the response.** It is *a fetch this client chose to
  make*, not something the venue said, and a reader that cannot tell which URL and which `limit`
  produced a body cannot reconcile it against anything.
- **A control record carries what `wsclient.on_control` already produces** — `(opcode, payload,
  recv_ns, replied_ns)`. **`replied_ns` is not decoration.** It is the evidence that the pong went
  back, and §9's ping row rests on that path being *exercised* rather than merely present.

`tools/tracefile.py` and `TraceReader` learn the kind **together**, and the two must agree on what
they reject. Strain 22 names the hazard precisely: the Python side is the edit that fails quietly.

### 2 · `classify()` widens — and the `static_assert` widens with it

This is the ruling's mechanism, and it is the deliverable the rest of M5 rests on.

`classify` takes `const TraceFrame&`. It must become answerable about a record that **has no frame**.
Whether that is an overload, a wider parameter, or a `TraceRecord` holding an optional frame is the
session's call. **What is not optional: the `static_assert` at `trace_decoder.hpp:155` moves with the
contract and must still fail the build for a decoder that does not satisfy the new one.** A widening
that leaves the old assertion passing unchanged has widened nothing — it has added a second, weaker
path beside a guard that no longer guards it.

- **Anvil's and Kraken's decoders do not change behaviour.** Their `is_liveness` answers come from
  frame content, and no control record exists in any of their traces. Prove it rather than assert
  it: the eight pre-existing committed traces replay byte-identical and every golden is unmoved.
- **Binance's `is_liveness` is true for a ping arrival and false for every depth record.** That is
  the ruling as written. Do not soften it into *true for anything unsolicited* — the audit stream is
  unsolicited too, and it is change-driven, which is the whole reason this venue has no heartbeat.

### 3 · The venue row, and the six predicates that default to Kraken

- **`venue.hpp` gains Binance.** Note before writing the row that this venue separates two clocks the
  other two conflate: stage 0 measured **record arrival going silent for 10.5 s legitimately** while
  the **ping cadence is 19.97 s median**, and the ruling's ~80 s threshold is armed on the *ping*,
  not on record arrival. Anvil declares 1,000 ms and Kraken 15,000 ms against record arrival. **Say
  in the table which clock each number is about**, or the fourth venue inherits an ambiguity that
  currently costs nothing because the two clocks have never disagreed.
- `tools/tracefile.py` gains its `VENUES` row.
- **Strain 22's real cost, measured at stage 0: six predicates default to Kraken's answer, so a
  missing branch returns a confident wrong answer rather than a refused file.** **Fix the shape, not
  the six.** Make an unmatched venue a hard failure, so a fourth venue fails loudly in all four
  places rather than three. Then update the strain card — it currently describes the milder version.

### 4 · The reader reads the wire it was given

- `dc_replay` reads all seven committed Binance slices end to end.
- **No committed trace is reshaped to suit the reader.** If a slice will not read, the reader is
  wrong. This sentence has earned its place once already at M4.
- The Anvil and Kraken traces are byte-identical through the change and every existing golden is
  unmoved. **A moved golden means stop.**

### 5 · The coverage invariant #6 asks for — and it is why this stage exists

**Until this exists, Binance liveness is a claim rather than a covered behaviour.** A host test in
which a trace containing a ping arrival, replayed, stamps the liveness clock — and an otherwise
identical trace without one does not. Mutation-verify it: a decoder that ignores control records
must turn the test red.

Note the second thing this unlocks, because a §9 expiry depends on it: **the two mixed-cadence
captures in `harness/replay/_local/` become readable here**, which is what makes B2 able to slice,
commit and pin them and discharge the untracked-evidence clause on the ruling's row 2.

### 6 · One check while the reader is new, and the bug is not this stage's

`48ba299` fixed `capture_binance` losing an in-flight REST fetch at the end of every capture. **The
seven committed slices were written by the tool before that fix.** The reader built tonight is the
first thing capable of answering whether any of them lost a trailing snapshot. **Report the answer;
do not re-capture** — re-capture and re-pin are B2's, with the rest of the pin work.

### 7 · Writeback

Session log; `NOTES-binance.md` addendum wherever the wire disagrees with this brief; the ROADMAP M5
line; `DESIGN.html` strain 22 in its stronger reading, plus whatever the trace-dialect picture now
draws. And one small owed item that keeps falling off the end: **write the `powershell -File`
verification hazard into `CLAUDE.md`**, beside the per-commit instruction — one line saying the loop
runs inline and that `powershell -File` fails at `CMakeTestCXXCompiler` for reasons not yet rooted
out. It is tooling rather than architecture, so no §9 row; the point is that the next sweep does not
lose an hour rediscovering it.

---

## Constraints

- **§6 frozen. §4 does not move tonight.** No new `FeedEvent`, no new `GapReason`. This stage changes
  what a *trace record* may be and what a *decoder may be asked to classify* — **not** the adapter
  boundary. If it appears to need a §4 change, **stop and raise**. That has been the answer four
  times and has been right four times.
- **No adapter, no `firmware/`, nothing flashed.** The adapter is B1.
- **Additive or it is wrong:** absent discriminator means `frame`; eleven existing traces byte-identical;
  no golden moved.
- Python in `tools/` only, stdlib only. Pin tables stay add-rows-only.
- Host build and ctest green (`cmake --build --preset host`, then `ctest --preset host`, from
  PowerShell — the Bash sandbox breaks compilers silently on this machine).
- **Per-commit verification in a detached worktree with `CMAKE_HOME_DIRECTORY` confirmed to point at
  the worktree — and run the loop INLINE.** `powershell -File` fails at `CMakeTestCXXCompiler` for
  reasons nobody has rooted out; that is deliverable 7's note and it applies to this evening first.
  Delete stale worktrees when done.
- **Commit only when asked.** Propose the split; commit nothing until it is approved.
- **Do not re-transcribe Deliverable 0.** Both §9 rulings landed 2026-08-25 and appear once each; a
  second pass duplicates rather than updates, which is the failure mode of *do this first*
  instructions.

## Known unknowns — resolve and record

Whether the discriminator is a field on the existing record or a wider record type, and what that
costs the Python reader. Whether `classify` widens by overload or by parameter, and whether one
`static_assert` can still express the whole contract. Whether a control record needs the ping's
payload bytes or only the fact and the timestamps. **Whether `venue.hpp` needs two threshold columns
rather than one** — this is the first venue whose record-arrival clock and liveness clock are
different quantities, and a single column would make them look like the same number measured twice.
Whether any of the seven slices lost a trailing REST snapshot to the pre-`48ba299` bug.

## Definition of done

- ☑ The record kind lands, additive, with absent meaning `frame`; **eleven existing traces
      byte-identical, no golden moved.**
- ☑ REST records carry request and response; control records carry `(opcode, payload, recv_ns,
      replied_ns)`.
- ☑ `tools/tracefile.py` and `TraceReader` agree on what they accept and what they reject, proven by
      a test rather than by inspection.
- ☑ `classify()` widened; **the `static_assert` moved with it and still fails the build for a
      non-conforming decoder** — shown by making one fail on purpose.
- ☑ Anvil and Kraken decoder behaviour unchanged, proven on the eight pre-existing traces.
- ☑ Binance `is_liveness`: true for ping arrival, false for every depth record.
- ☑ `venue.hpp` and `VENUES` rows added, **with each threshold's clock named**.
- ☑ Unmatched venue is a hard failure in all four places; strain 22 updated to the stronger reading.
- ☑ `dc_replay` reads all seven committed Binance slices end to end, none reshaped.
- ☑ **The host liveness test exists and is mutation-verified**: a ping arrival stamps the clock, its
      absence does not, and a decoder ignoring control records turns it red.
- ☑ The pre-`48ba299` trailing-snapshot question answered for all seven slices.
- ☑ `CLAUDE.md` carries the `powershell -File` hazard.
- ☑ Session log; ROADMAP M5 line; DESIGN strain 22 and the trace-dialect picture; green; split
      proposed; nothing committed.

## Out of scope

The adapter — parsing, the `U`/`u` state machine, delta application, the REST seed (**B1**). The seed
window, the walk rate and the re-snapshot schedule, and **slicing, committing and pinning the two
mixed-cadence captures** (**B2** — its §9 expiry says so). What the panel *renders* when the feed is
silent, and the ~80 s threshold's final multiplier (**C**). The bench and the >24 h soak (**D**).
Re-capturing the seven slices. The scanner extraction (strain 25 — **answered at stage 0, not
needed**). The client ping (M6). The runtime venue toggle (M7). M4's carried bench residues — D1, D2,
D7's scope trace.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     measured figures / evidence · exact next step. -->

### 2026-08-25 · Claude Opus 5 (1M context) · stage A complete, green, nothing committed

**Done.** All thirteen definition-of-done boxes. `cmake --build --preset host-mingw` clean,
`ctest --preset host-mingw` **44/44** (was 37 — seven new `dc_replay` Binance tests). Nothing
committed; the split is proposed below.

**Decisions, with why.**

1. **`TraceFrame` became `TraceRecord`, a rename rather than a typedef.** The type now
   describes records that are not frames, so keeping the name would make every
   `f.frame_json` read on a control record a lie. It is also what makes deliverable 2's
   assertion bite: a decoder written against the old contract does not merely fail the
   `static_assert`, it fails to name a type. Six files, mechanical.
2. **The discriminator is `RecordForm { Frame, Rest, Control }` on the record**, not a wider
   parameter and not an overload. `record_form_name()` returns the *wire* spellings (`""`,
   `"rest"`, `"control"`) so the two languages share the strings. Named `RecordForm` and not
   `RecordKind` because that name is already the decoder's *answer* about a record and the
   two are different questions — the near-collision is the wire's, since the key was named
   `kind` at stage 0 and committed slices cannot be renamed.
3. **`TraceRecord::event_ns` — the field the brief did not ask for and the stage could not do
   without.** A side-channel record is stamped when the main loop next flushes, so **three
   pings twenty seconds apart share one `rx_ns`** in `binance_atomeur_deepseed`. The liveness
   clock reads `event_ns` (`rx_ns` for a frame, `req.recv_ns`, `ctl.recv_ns`); *everything
   else* stays on `rx_ns`, because that is what orders the file and what every pinned column
   already measures. ARCHITECTURE §9, row 1 of tonight's two.
4. **`RecordKind::is_snapshot` at Binance is a REST record and nothing else**, which makes it
   deliberately disagree with `tracefile.py`'s `is_snapshot` at exactly one venue. The Python
   twin to mirror is `rebaselines()` — whose own comment said *"NO C++ TWIN, deliberately… if
   a C++ caller ever needs it, the authority to mirror is the adapters' own dispatch"*. A C++
   caller now needs it. Counting `@depth20` partials instead would report **151 snapshots and
   151 resyncs on a fifteen-second slice**. Stated in both files.
5. **Deliverable 3's shape fix, both halves.** C++: every `switch` on `Venue` lists its
   enumerators with no `default:` and ends in `unhandled_venue()`, which throws —
   `RecordClassifier::classify` used to `return {}`, and a `RecordKind{}` is a confident wrong
   answer, not a refusal. Python: the six predicates gained explicit `kraken` branches and a
   raising tail. The selfcheck stages **both** failure modes, including the one that used to
   be silent — a venue given a `VENUES` row and no branch.
6. **`legacy_book_threshold_ms = -1` is a sentinel meaning NEVER DECLARED**, not zero. Binance
   arrived after the ruling that withdrew the other two, so its `_legacy` columns are not
   computed; 0 would have counted every record-arrival gap as a firing. A `static_assert`
   makes the row explain itself, exactly as `validated_note` already did.
7. **`dc_ladder` is deliberately absent from the new tests.** `run_replay` refuses Binance by
   name — the holding position Kraken sat in between M4 stage A and B1 — because a decoder
   that emits nothing looks exactly like a feed that said nothing.

**What the wire corrected in the reader, which is why the reader went first.** The first draft
required a `rest` record's `frame` to be an object; `capture_binance --selfcheck` refused a
trace its own capture loop had just written. `on_rest` **records** a failed fetch rather than
dropping it — *the snapshot did not arrive* is a fact about the capture window. Both readers
now accept object-or-null and name a bodyless one **`rest:no-body`**, on the same reasoning
that makes `ack:subscribe REFUSED` its own bucket at Kraken.

**Evidence.**

- **Seven committed slices read end to end, none reshaped.** Plus both untracked
  mixed-cadence captures in `_local/` — 997 and 999 records, `partialDepth=90` against
  `depthUpdate=899/901`, which is the ruling's 90/90 coincidence figure. **B2 is unblocked.**
- **Eleven pre-existing traces byte-identical; every taxonomy row unchanged** — proved by
  running `--selfcheck` over them *before* pinning anything, per the procedure. Seven new
  rows added; the table was never regenerated.
- **`dc_replay`'s output over those eleven differs in six lines**, diffed against a
  `dd9ce87` build in a detached worktree with `CMAKE_HOME_DIRECTORY` confirmed. All six are
  Kraken, all six are the `snapshots` parenthetical, **no count moved**. The Anvil report is
  byte-identical, as its frozen baseline requires. The parenthetical said *"(=> trace spans a
  reconnect)"*, which is an Anvil-shaped inference and false at a venue that re-seeds from
  REST every four seconds.
- **Mutation-verified, measured rather than predicted**: ignoring control records → 4 cases /
  6 assertions; stamping liveness from `rx_ns` → **exactly 1** case; treating a bodyless REST
  as a snapshot → **exactly 1** case. The contract's `static_assert` was made to fail on
  purpose and prints the widened message. Details in `test_trace_records.cpp`'s header.
- **A shared corpus, `harness/tests/record_shapes.json`**, replayed by `test_trace_records.cpp`
  and by `tracefile.py --selfcheck`. Fifteen cases; *"the two readers agree on what they
  reject"* is now a fixture outside both of them rather than an inspection.

**Two findings handed to C, neither in scope tonight.**

- **The ruling's ~80 s threshold is not what the code produces.** `kThresholdCeilingMs` clamps
  it to **30 s** (4 × 19,970 = 79,880).
- **At this cadence the self-calibration never runs.** `kMinSamples = 8` intervals is ~160 s;
  the longest committed slice is 88 s with 5 pings. All seven replay on the *uncalibrated*
  default — which is also 30,000 ms, so the two coincide, nothing misbehaves and no golden
  moves. What is inert is the mechanism the 2026-08-17 ruling rests on. ARCHITECTURE §9.

**Deliverable 6, answered.** **One of the seven lost a trailing REST snapshot:**
`binance_atomeur_d100ms` — last fetch sent at +71.60 s, next due at +86.60 s, a message
arrived at +87.90 s and is the file's last record, so the fetch fired and never landed.
`binance_atomeur_deepseed` did **not**: its last message is at +18.50 s against a fetch due at
+20.00 s, so none was launched (its file ends with the `finally` drain writing two queued pings
at +46.97 s). The other five are windows ending well before their capture's last record.
**Both ATOMEUR files are the whole capture** — their `_local/` copies are byte-identical —
which is exactly why they are the only two exposed. **It costs nothing**: that slice is the
quiet-pair witness, not an oracle golden. No new reason to re-capture; that stays B2's.

**Review.** The owner's `code-review` skill was run against the diff and produced four fixes,
all applied before this log: a **duplicated "not frames" line** printed twice in one Binance
report in two spellings; `print_kraken_report` / `print_binance_report` merged into one
`print_symbol_venue_report` (the copy is what produced the duplicate); two parallel Python
dicts keyed by the same form strings merged into one table; and `record_form_name`'s trailing
`return {}` — which is *Frame's* answer — made `"?"`, since that was the exact defect being
removed from `RecordClassifier::classify` one file away. It also caught a **pre-existing**
false line: `dc_replay`'s Kraken report claimed *"stage A's Kraken decoder is a CLASSIFIER; 0
FeedEvents by design"*, untrue since M4 B1 and unnoticed because it is trivially true *of this
program* — `dc_replay` drives no adapter at any venue. Corrected; a seventh moved line in the
Kraken report.

**A process finding worth more than it looks.** The first mutation run was scored against a
binary built from the wrong sources: reverting a mutant by moving a copy back restored the
file *with its old timestamp*, `make` judged the object current and never rebuilt it, and the
next mutant's run carried the previous one. It reported 4 red cases where the true answer is
1. **CLAUDE.md's worktree rule is about a green build from the wrong tree; this is the same
failure wearing red.** Every figure was re-taken with timestamps forced forward and the
unmutated baseline re-confirmed at 385/385 between each.

**Not done, deliberately.** No `firmware/` change, no adapter, no §4 or §6 movement, nothing
flashed, no trace re-captured or reshaped. `dc_ladder` still cannot render Binance and says so.

**Exact next step.** Owner reviews the proposed five-commit split. On approval: create the
commits, verify each in a **fresh** detached worktree with `CMAKE_HOME_DIRECTORY` confirmed to
point at it, run the loop **inline**, and push nothing until every commit has been shown green
in isolation. Then **B1 — the Binance adapter**: `decode()`'s signature is the one to fill and
not to reshape, and `Replay`'s `if constexpr` chain is deliberately two-way so that forgetting
the Binance branch fails the build rather than populating neither stats field.
