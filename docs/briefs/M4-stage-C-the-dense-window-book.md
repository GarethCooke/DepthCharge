# M4 Stage C — the dense-window book

**Track:** Agentic [A] · **Status:** Complete 2026-08-19, awaiting split approval · **Size:** one evening, shares with nothing
**Executor:** Claude Code, **desk only. No board, no flash, no firmware, no panel judgement.**

**This stage has no wire in it, and that changes how it is written.** Every stage since 0 was
settled by measurement — price the wire, replay it, verify it, throttle it. There is nothing to
capture here. The dense window is a decision about which levels of a 25-level book occupy 27
rendered rows, and no capture can adjudicate it.

**So C does not decide.** C makes the window correct, instrumented, and *selectable*, and D
decides which one is right at desk distance. A host test can prove a window is well-formed,
contiguous, and never renders a level the book does not hold. It cannot prove a window is worth
looking at.

**Read first**

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §5 | `DisplaySnapshot` is what the window fills. `sizeof` is pinned by `static_assert` and quoted in three documents. |
| `ARCHITECTURE.md` §6 | #4 integer ticks, allocation-free steady state, single writer per state, stale as a first-class rendered state. |
| `ARCHITECTURE.md` §9 | The coincidence-class row and the never-observed row — both apply to a structure with no corpus behind it. |
| B2's session log | The checksum validates 20 of the 50 levels the ladder holds. That is a design input here. |
| Stage 0's decision on `kDisplayLevels` | 27 rows against depth 25; unfilled rows render **unknown, not zero**. |

**Depends on:** B2 ✅. **Blocks:** D.

---

## 1 · What the window is, and what it must never do

The book holds 25 levels a side at integer ticks. The panel holds 27 rows a side. The levels are
not contiguous in price — a book with gaps between levels cannot be rendered by walking rows.

Whatever the policy, these hold for all of them and are tested once, not per policy:

- **A rendered row is either a level the book holds, or explicitly unknown.** Never zero, never
  interpolated, never a level synthesised to fill a gap. This is stage 0's decision and the
  invariant most easily broken by a policy trying to look tidy.
- **Rows are monotonic in price** and the two sides never cross.
- **Allocation-free.** The window is computed into existing storage each frame.
- **Single writer.** The window is built where the snapshot is built, not read back and patched.
- **The window is a pure function of the book and the policy.** No history, no hysteresis in this
  stage — a policy that smooths across frames is a policy with state, and state is where the
  single-writer invariant goes to die. If a candidate policy seems to need it, **stop and raise**.

## 2 · Uninitialised is a state, not an empty book (triage item 11, engine half)

A book that has received nothing and a book whose levels are genuinely empty are different facts,
and only one of them is knowledge.

- The engine carries an explicit **uninitialised** state, distinct from *empty*.
- The window reports it rather than resolving it — 27 rows of unknown is a rendering decision, and
  it is D's.
- B2's healing path makes this reachable in normal operation: between a dropped book and a fresh
  snapshot, the book is uninitialised rather than empty.

## 3 · Several policies, selected at compile time

Three is the number; fewer than three and D has nothing to compare, more and the selection
mechanism costs more than the policies. **Name them, implement them behind one interface, select
with a compile-time constant in the same style as the venue selection.**

Candidates — take these as the starting set and say so in the log if the book's shape argues for a
different third:

1. **Top-N from the touch.** The 27 rows nearest the mid, ignoring price gaps. Simplest, and the
   one that degenerates most obviously on a sparse book.
2. **Fixed tick window.** Rows are ticks, not levels: a contiguous price band around the mid, with
   levels placed into their tick row and empty ticks rendered unknown. Honest about the price
   axis, and the only policy where row position means the same thing frame to frame.
3. **Density-adaptive.** The band widens or narrows so the rendered rows carry a target share of
   depth. Best use of 27 rows, worst stability — and the one most likely to want history, which
   §1 forbids.

For each: what it renders on a dense book, on a sparse book, when the book is one-sided, and when
fewer than 27 levels exist. Record those four answers per policy in `NOTES.md` — that table is
what D reads at the panel, and writing it is most of the design work.

## 4 · Instrumentation, because D cannot judge what it cannot see

The console ladder gains, per frame and per policy: rows filled, rows unknown, the tick span the
window covers, and **how many of the rendered rows fall within the checksum's reach**.

That last one matters and is new. B2 measured that Kraken's CRC validates the top 10 levels a side
— 20 of the 50 the ladder holds. So a window's position determines whether what the panel shows
was ever checked, and a window sitting below level 10 renders nothing the venue ever confirmed.
**C records this per policy; C does not solve it.** It may well be that the right answer is that
the panel shows unvalidated levels and that is fine — but D should decide that knowing the number.

## 5 · Coverage, given there is no corpus

Two §9 rows apply directly and both point the same way: synthetic input is the instrument here,
not a substitute for one.

- Property tests over generated books: sparse, dense, one-sided, single-level, empty,
  uninitialised, 26 levels, 27, 28, and levels separated by gaps larger than the window.
- The invariants in §1 hold for **every** policy under **every** generated book. One test suite,
  parameterised by policy, not three suites.
- Replay the committed traces through each policy and pin the resulting row counts. Goldens add
  rows; none move.
- Mutations run, not written: break monotonicity, fill an unknown row with zero, and let a window
  render a level the book does not hold. Each expects red.

---

## Constraints

- §4 and §5 frozen. If a policy seems to need a new `DisplaySnapshot` field, **stop and raise** —
  `sizeof` is pinned and quoted in three documents.
- §6 frozen. Allocation-free, single writer, no history.
- `engine/` only. No `firmware/`, no `staleness.hpp`, no watchdog — those are D's first act.
- No panel judgement. C does not pick the policy and does not argue for one in the brief; the
  four-answer table is the argument, and D makes it.
- Run the code-review skill before proposing the split. Per-commit verification in a detached
  worktree with `CMAKE_HOME_DIRECTORY` confirmed.
- `cmake --workflow --preset host-mingw`, green. **Commit nothing.**

## Known unknowns — resolve and record

Whether a real Kraken book at depth 25 is sparse enough for the policies to visibly differ — if
the committed traces show all three producing near-identical windows, that is a finding worth more
than the policies, and D's comparison gets cheaper. Whether the tick-window policy's band can be
chosen without a constant that is really a market property in disguise, which is the mistake §9
already has a row about. Whether *uninitialised* needs to be visible in `DisplaySnapshot` or can be
inferred from the window being wholly unknown — the second is cheaper and may be a lie.

## Definition of done

- [x] The §1 invariants hold for every policy under every generated book, proven by property test —
      plus one the brief did not name: **row 0 is always the book's best level**, or `best_bid`,
      `best_ask` and `spread_ticks` all lie.
- [x] Uninitialised is an explicit engine state, distinct from empty, reachable via B2's healing
      path, and reported rather than resolved. **Published in one byte of existing padding**;
      `sizeof(DisplaySnapshot)` verified unmoved at 1,168 on host and on xtensa.
- [x] Three policies behind one interface, compile-time selected on the board and
      runtime-selectable in the harness, no history in any of them. **Two of the three are not the
      brief's** — see the measurement in the log.
- [x] The four-answer table per policy in `NOTES.md` — dense, sparse, one-sided, under-filled.
- [x] Console instrumentation: rows filled, rows unknown, tick span, and rows within the
      checksum's reach — plus `levels_dropped`, which is the number that says whether the policy
      choice is a choice at all.
- [x] Committed traces replayed through each policy with row counts pinned; no golden moved.
- [x] Three mutations run and red.
- [x] Green (32/32); code review run; split proposed; nothing committed.

## Out of scope

Choosing the policy. Any rendering judgement. `staleness.hpp`, `kRxWatchdogMs`, the watchdog
rewiring and its soak, and firmware acting on `resync_wanted()` — all D's first act. The client
ping (M6). Binance (M5). The runtime venue toggle (M7).

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     measured figures / evidence · exact next step. -->

### 2026-08-19 · Claude Opus 5 · the window, and the measurement that reshaped the stage

**Done.** `engine/window.hpp` is new — three row policies behind one interface, compile-time
selected on the board (`kWindowPolicy`) and runtime-selectable in the harness. `Book::publish`
runs one; `Book` carries an explicit uninitialised state; `dc_ladder --window` prints what each
policy did. `harness/tests/test_window.cpp` is new. **32 ctest targets green, 343 doctest cases,
960,852 assertions** — up from 335 / 855,854.

---

**THE FIRST THING I DID WAS MEASURE, AND IT CHANGED THE CANDIDATE SET.**

The brief named three policies, two of which were price axes: a fixed tick band around the mid,
and a density-adaptive band. Before writing any of it, I measured the tick distance between
adjacent levels across every committed trace:

| book | gap p50 | p90 | max | contiguous | one side spans |
| --- | ---: | ---: | ---: | ---: | ---: |
| Kraken BTC/USD d25 | 5 | 18 | 62 | **19.5%** | **182 tk / 25 levels** |
| Kraken MINA/GBP d25 | 10 | 350 | 1,990 | 13.3% | **6,613 tk / 25 levels** |
| Anvil 101 baseline | 6 | 23 | 186 | 10.0% | 1,079 tk / 101 levels |
| Anvil 101 depth 27 | 7 | 22 | 99 | 12.6% | 252 tk / 30 levels |

**Only one adjacent level pair in five is a single tick from its neighbour, on the densest book in
the repository.** A 27-row panel used as a 27-tick price axis covers 27 of BTC/USD's 182 ticks —
about **four of twenty-five levels**, twenty-three rows blank — and 27 of MINA/GBP's 6,613, which
is **the touch and twenty-six blank rows**. Widening a row to span several ticks restores the
coverage and destroys the policy: a row becomes an *aggregate* of levels rather than a level,
which §1 forbids, and its width becomes a constant taken from the market's own span, which is the
mistake §9 already has a row about.

**So the axis is rank, and the brief's own escape clause is what let me do it** — it invited a
different third candidate if the book's shape argued for one. The shape argued for a different
second as well, and that is the deviation to check first:

1. **`top`** — the best 27 levels. The status quo, and the identity whenever the book fits.
2. **`largest`** — the touch, plus the largest levels behind it, in price order. Shows the walls.
3. **`thinned`** — the best 13 individually, then the tail sampled at a stride derived from the
   book's own depth. Detail at the touch, shape behind it, and no constant that is a market
   property in disguise.

**A price-axis window is blocked, not refuted, and I raised it rather than taking it.** It needs
one thing the published snapshot cannot say: *row 7 is not a level*, INTERLEAVED rather than
trailing — `bid_count` expresses only trailing unknowns. Two `uint32_t` present-masks carry it
exactly (27 rows fit one), for **8 bytes on `DisplaySnapshot` and 24 in the three-slot mailbox**,
plus correcting the three documents that quote the size. The brief says stop and raise for a new
field, so that is a decision waiting rather than one I took.

---

**THE HEADLINE, AND IT ANSWERS THE STAGE'S OWN KNOWN UNKNOWN.**

The brief asked whether a real Kraken book at depth 25 is sparse enough for the policies to
visibly differ, and said a negative would be worth more than the policies. **It is negative and
stronger than "near-identical": at depth 25 into 27 rows not one level is ever dropped, so all
three policies are byte-identical on every committed Kraken slice.** `levels_dropped` is 0 in 0
frames, on d25 and on the quiet pair and on the resync slice.

So the choice only becomes a choice if the firmware subscribes **deeper than it can draw** — and
B2 supplies the other half of that trade, because the CRC32 covers the top 10 a side whatever the
depth. Over the committed depth-100 slice, of 281,124 rendered rows:

| policy | rows the venue checksummed | worst tick span |
| --- | ---: | ---: |
| `top` | 104,120 — **37.0%** | 319 tk |
| `largest` | **33,433 — 11.9%** | 1,128 tk |
| `thinned` | 104,120 — **37.0%** | 1,060 tk |

`largest` renders a panel of which **88% was never confirmed by anyone**, because it reaches for
size deep in the book and the checksum does not follow it out. `thinned` matching `top` is
arithmetic rather than luck: its head is the best 13 ranks and the CRC covers the best 10. And
**Anvil's number is 0** — that protocol has no checksum of any kind, so no rendered row there was
ever confirmed, which is exactly the reading a missing field would have turned into "all of them".
Recorded as DESIGN §08 strain 24; C records the number, D decides knowing it.

---

**Uninitialised is a published state, and it cost nothing.** `Book::initialised()` is monotonic —
true at the first Snapshot, never false again, because a Gap makes the book *unknown* rather than
*un-received* and `mark_stale` deliberately keeps the levels for the renderer to grey. That is a
deliberate asymmetry with the Kraken adapter, which *does* drop its ladder on a gap, and B2's
healing path is what made both reachable in one run.

It is **published rather than inferred** because the cheap inference is a lie in a reachable case:
`!live() && !has_book()` reads "uninitialised" for a book that adopted an *empty* snapshot and
then took a Gap. That case is a subcase in the tests. The flag lands in the pad byte between
`has_age` and `last_px`'s 8-byte alignment, and **`sizeof(DisplaySnapshot)` is verified unmoved at
1,168 on host AND on xtensa GCC 8.4** — checked by compiling a template-error probe with the
target compiler, not by assuming. (Incidental finding: the target size is 1,168, not the 1,176
three documents imply; that number is the mailbox slot including its share of channel overhead,
and nothing here moved it.)

**Coverage, given there is no corpus.** One suite parameterised by policy, not three — the
invariants belong to the window, and three suites would be three chances to write a weaker version
of the same check. Every generated book runs through every policy. **The generator is sparse by
default and contiguous only as a special case**, which is the opposite of the obvious way round
and is what the measurement above says a real book looks like; quantities span three orders of
magnitude, or `largest` would be indistinguishable from `top` and the suite would test one policy
twice. Shapes: empty, single, 13/26/27/28/40/100/256 levels, dense, gaps of 5,000 ticks, caps of
0/1/2/3/26/27, plus 200 random books per side.

**One invariant is mine rather than the brief's, and it is not decoration.** *Row 0 is the book's
best level whenever the side is non-empty.* `best_bid`, `best_ask` and `spread_ticks` all read row
0, so a policy free to drop a thin touch in favour of a fat level behind it would make all three
lie while every other invariant still held. `largest` reserves slot 0 for the touch because of it.

**Mutations — three run, three red, all reverted.**

| mutation | result |
| --- | ---: |
| drop the price sort, so rows come out in selection order (breaks monotonicity) | **RED** 3 cases / 4,436 assertions |
| report every row as filled, so unknown rows read as levels | **RED** 11 cases / 20 assertions |
| render a level the book does not hold (one row's qty nudged) | **RED** 2 cases / 31,247 assertions |

**Goldens add rows and none moved**, which is structural rather than lucky: the default policy is
`top`, and `top` is exactly what `publish` did before this stage. The new pins carry two identities
that make them more than a transcription — `rows_filled + rows_unknown == 54 × publishes`, and for
`top`, `rows_validated == 2 × validated_depth × publishes`.

**Found at review, fixed:** `choose_thinned` was safe at `cap == 0` only by accident of a loop
bound that never runs, one edit away from a division by zero — now guarded explicitly; and
`copy_clamped`'s `ZeroFill` half lost its only caller when the window took over the display copy,
leaving a dead template parameter behind a comment describing a use that no longer existed. Also
documented: `select` clamps `cap` to `kMaxRows` rather than honouring it, because `dst` is a
`DisplaySnapshot` side in every real call.

**Recorded rather than silently skipped: §5's tick-indexed dense-window STORAGE is still unbuilt.**
It is a different thing from the render window that shares its name — a contiguous `Qty` array
addressed by `(px − anchor)` plus a cold tail, whose purpose is O(1) lookup — and the brief asked
for the render window. It would optimise an ordered insert over a side of 25 running at ~1,000
16-byte shifts a second. §5's text stands; §9 now says why the code does not match it.

**Verification.** `cmake --workflow --preset host-mingw` green, 32/32. `dc_engine_target_check`
compiles `window.hpp` with xtensa GCC 8.4 under `-Werror -fno-exceptions -fno-rtti`. Code review
run. **Nothing committed.**

**Exact next step: the owner approves or amends the proposed split, then it is executed** — each
commit created and verified in a detached worktree with `CMAKE_HOME_DIRECTORY` confirmed, nothing
pushed until every commit is green in isolation. Then **stage D — the bench**, whose first act is
still item 8 (the watchdog lift), and which now also inherits: the policy choice, the subscribed
depth that makes it a choice at all, strain 24's number, what to draw for *uninitialised*, and
firmware acting on B2's `resync_wanted()`.

**Two things for the owner to overrule if I have read the room wrong.** (1) I replaced two of the
three candidate policies on the measurement above; the brief permitted a different third and I
changed the second as well. (2) I added one byte to `DisplaySnapshot` for `initialised` — it is
free in padding and `sizeof` is verified unmoved on both toolchains, so the brief's stated reason
for stopping (the pinned size) does not fire, but it is a new field in a frozen struct and that is
the owner's call rather than mine.

### 2026-08-19 (later) · owner review · the absence rule, and the price written down

Two directions from the owner, both about making a finding outlive the stage that produced it.

**1 · The 0% on Anvil is not about windows, and the §9 row should say the general thing.**

> *A protocol with no checksum renders as no coverage — and a missing field would have rendered as
> all of them. That's the never-observed row generalised past frame kinds: an absent capability and
> an unread one look identical downstream unless the absence is represented.*

Taken, and written as its own §9 row rather than folded into the window rows:

**An absent capability and an unread one are indistinguishable downstream unless the absence is
represented — so a venue's lack of something gets a value and a reason, never a missing field.**

What makes it a rule rather than a case is that **this project has already arrived at the same
answer at least eight times without naming it**, each argued from scratch: `has_age` (*no reading
yet* and *the book is current* are different statements and exactly one is reassuring), `has_last`,
`initialised` (this stage), `checksums_unverifiable` (B2 — 49 unverifiable, not 49 failed),
`book_msgs_unchecksummed` (B2 — an adapter fed messages carrying no checksum verifies nothing while
every other counter looks healthy), `NOT_A_CHECKSUM_GOLDEN` (stage A — *cannot be graded* is a third
state beside pinned and forgotten), `clock: undeclared` in the trace metadata, and §4's oldest one:
Anvil publishes no tick metadata, so DepthCharge **declares** a scale and verifies rather than
defaulting.

And it is the HARDER half of the never-observed row rather than a restatement. There the absence was
in the corpus, and it closed the moment somebody captured the missing frame kind. Here the absence
is in the venue: **no future trace will ever contain an Anvil checksum**, so it cannot close by
accident.

**Made structural, not just documented.** `venue.hpp` now fails to compile a `kVenueTable` row whose
`validated_depth` carries no note — because 0 is also exactly what an unfilled field looks like, and
the next venue's author should have to write down which of the two their zero means. DESIGN strain
24 keeps the specific instance and now points at the rule.

**2 · Record the price of the blocked price-axis window, and it is not M4's.**

> *Writing that down now means whoever wants it later argues against a number rather than
> rediscovering it.*

Added as **ROADMAP backlog D6**, priced in full rather than described: two `uint32_t` present-masks
carry the interleaved present/absent marker exactly (27 rows fit one, five bits spare), for **+8
bytes on `DisplaySnapshot` (1,168 → 1,176) and +24 in the three-slot mailbox (3,528 → 3,552)**, plus
the three documents that quote the size — `firmware/src/main.cpp`'s static-footprint block,
`snapshot_channel.hpp`'s cost note, and ARCHITECTURE §9's 2026-08-07 row. The item also carries the
alternative that needs no field and why it costs more (a row becomes an aggregate, and its width
becomes a market constant), and the condition that would justify it: a subscribed depth larger than
the panel — **the same condition that makes the rank policies differ at all, so both decisions
belong to whoever owns the depth rather than to whoever owns the window**. `window.hpp` and strain
24 both point at D6 so the number is reachable from where it bites.

**Verification.** `cmake --workflow --preset host-mingw` green, 32/32, 343 doctest cases, 960,852
assertions. One incident worth a line: the incremental build tree corrupted itself mid-session
(`objects.a: section string index out of range`, and a `dc_tests.exe` Windows refused to execute).
It failed LOUDLY rather than passing from stale objects, which is the safe direction, and a
`--fresh` configure cleared it — but it is a reminder that per-commit verification belongs in a
worktree of its own, which is where it will happen. **Nothing committed.**
