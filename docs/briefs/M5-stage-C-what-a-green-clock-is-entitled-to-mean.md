# M5 Stage C — what a green liveness clock is entitled to mean

**Track:** Agentic [A] · **Status:** DONE 2026-08-26 (split proposed, nothing committed) · **Size:** one evening
**Executor:** Claude Code, **desk only. No board, no flash, no `firmware/` change, no panel judgement.**

B1 found a socket that answers pings and delivers nothing for ever. B2 measured what the age meter
can and cannot see through it, and captured the first trace in this project's history that
calibrates the liveness clock at all. What is left is not a measurement. **Every number this stage
needs is already committed** — C decides, C does not re-measure.

The venue's ping is emitted below the subscription, so it proves the socket and never the feed. The
2026-08-17 ruling armed the grey on a liveness signal because at Anvil and Kraken the same subsystem
emitted the ladder and the heartbeat; here it does not. Decision 1 stands — greying on socket death
alone lost by 141 s and 291 s at M4 stage D's B3. **What changes is what a green clock is entitled to
mean**, and this evening is where that gets written down as behaviour rather than as a strain card.

---

## The scoping ruling, taken before this brief was written

B2's *Owed by stage C* section closed by warning that C could not be briefed until someone settled
which of its items a desk can close — *"or C is written as one evening and discovers halfway through
that half of it cannot be finished at a desk."* That question is settled here, and the answer comes
from M4 twice rather than from judgement:

- **M4 triage, 2026-08-17:** *the uninitialised-book question splits — the engine state is C's, the
  rendering is D's, because the two candidate renderings differ in nothing a host test can assert.*
- **M4 stage D, item A1:** the brief said no host test could check the rewire, and that was *"true of
  the BEHAVIOUR and not of the POLICY"* — so the rule went into `liveness_watchdog.hpp` and was host
  tested, and only the behaviour waited for the bench.

**So: C is an engine-and-policy evening, and every question of the form *what does the panel look
like* is D's, without exception.** That is a narrower C than the inheritance table implies and a
wider D, and it is the only split under which C is one evening. Applied to the eight items:

| # | Item | Verdict |
| --- | --- | --- |
| 1 | The threshold multiplier | **C.** B2's 221 s calibration trace is the first artefact that enters the calibrated branch. The policy is host-assertable today. |
| 2 | `kThresholdCeilingMs`'s changed role | **C**, with 1, and coupled to 3 — see deliverable 2. |
| 3 | The four unbuilt remedies | **Splits.** C picks the mechanism and proves it against the defect fixture; whether the resulting grey *reads* right is D's. |
| 4 | What the panel renders on a silent feed | **D.** Rendering. M4's precedent is exact and this stage does not touch it. |
| 5 | The transport-versus-feed hole | **C** — as a written limit, not a fix. It has none. |
| 6 · 7 · 8 | `age_ms` blind to a feed backlog · the 638.8 s no-reading window · whether ~85 min is defensible | **C**, on B2's own measurements. |

**Item 4 leaving C is the whole reason C fits in an evening.** It is also why deliverable 3 exists:
a rendering decision deferred to D still needs an engine state to render, and publishing that state
is C's.

---

## Two corrections to the inheritance list, found while scoping it

Both are the failure that section's own preamble names — *a list reconstructed by grepping arrives
short* — arriving inside the list written to prevent it. Neither is a measurement error and nothing
is failing.

**(i) There are two live strain cards numbered 26**, and everything in M5 cites the newer one.

- **26 · The liveness watchdog fires exactly once, because only an arrival re-arms it** — opened
  2026-08-20 at M4 stage D, owner M4 stage D's bench day B2, still open in M4's residue.
- **26 · The ping proves the socket and never the feed** — opened 2026-08-25 at M5 stage B1, owner C.

`docs/DESIGN.html` has met this before and recorded the remedy inside card 23: *"Two live cards under
one number in a document whose whole job is to be cited is a defect in the citation, so this one moves
to 23 and the older number keeps its references."* The precedent applies unchanged — **the M5 card
moves and the M4 card keeps 26** — except that here the newer card is the one with inbound references
from §9, ROADMAP, `NOTES-binance.md`, both M5 adapter briefs and DESIGN's own cross-references —
**12 mentions across six files** — so the renumbering has a cost the 22→23 case did not.
**Raise it rather than executing it silently.** Note the pattern while raising it: both
collisions were created by a **stage B1** opening a card, a milestone apart, and nothing checks.

*Decided at the owner's approval of this stage, 2026-08-26, and the paragraph above is left standing because it is the argument that produced the decision rather than an error in it: **the M4 card moves to 30 and the M5 card keeps 26**. The count was checked rather than estimated and came out at 15 references across 8 files, 14 of them the M5 card's — so card 23's precedent is followed by its reason (fewest broken citations) rather than by its letter (newer moves), which at 22→23 could not tell the two apart. DESIGN card 30 carries it.*

**(ii) Card 28's C-half is missing from the eight-item table.** Card 28 — *the trigger asks for a
re-seed and nothing can adopt one* — resolves **"D for the mechanism and the memory, C for what the
panel shows while a re-seed is in flight."** The table has no row for it. It was opened by B2 on the
same day the table was written, which is exactly how the five-scattered-mentions problem reproduces.
It is deliverable 3 below. **Nine items, not eight.**

---

## Read first

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §6, §4 | Frozen, and this stage adds no `GapReason` and no `FeedEvent::Kind`. |
| `ARCHITECTURE.md` §9, **2026-08-25 (M5 stage A, qualifying the ping ruling)** | Items 1 and 2 in one row, with the reason the multiplier and the venue must not move together. |
| `ARCHITECTURE.md` §9, the two **stage B1** rows | The lying socket, and the correction to decision 1's premise. Deliverables 2 and 4 are downstream of the second. |
| `docs/DESIGN.html` strain **26 (M5)** | The four candidate remedies, their costs, the defect fixture, and **the expiry clause with teeth**. |
| `docs/DESIGN.html` strain **28** | Deliverable 3, and the adoptability measurement that already decided part of it. |
| `docs/briefs/M5-stage-B2-…md` § *Owed by stage C* | The inheritance table, and the two cautions carried with it. |
| `docs/briefs/M4-stage-A2-the-age-meter.md` | The *no reading* shape deliverable 5 reuses, and the blind spot pinned as a test rather than described. |
| `harness/replay/NOTES-binance.md` | Every figure here, **with the desk-box provenance stated beside it.** |

**Depends on:** B2 ✅. **Blocks:** D, M5 close-out.

---

## Deliverables

### 0 · Two ground-truth traces are CRLF in the working tree, and have been since 2026-08-07

`harness/replay/anvil_101_baseline.ndjson` and `anvil_101_reconnect.ndjson` differ from their
committed blobs in **every line and nothing else** — strip the CRs and both are byte-identical to
`HEAD`. They are the two oldest traces in the corpus, committed at M0 (`f103b72`) and checked out
onto this Windows desk *before* `a56ccd6` added `*.ndjson -text` to `.gitattributes`. The rule
protects every trace captured after it and never healed the two that predate it; the other twenty
are clean LF.

Nothing has failed and nothing could: `trace.cpp:148` and `json_scan.hpp:111` both treat `\r` as
whitespace, so the suite is green on either. **The exposure is `git add -A`** — a named guard failure
in `CLAUDE.md` — which would commit CRLF over the project's two oldest ground-truth files and move
two goldens without anyone choosing to.

Restore both to LF in **one commit of its own, first, before anything else in the split**, and
confirm `git hash-object` matches `git rev-parse HEAD:<path>` for each. This moves no golden: it
restores the committed bytes.

### 1 · The threshold: decide the number, and put the policy where a test can reach it

`kThresholdMultiple = 4.0` against a measured ~19.96 s cadence is ~79.9 s, and
`kThresholdCeilingMs = 30,000` clamps it to 30 s — so the self-calibration is **inert at this venue**,
producing exactly `kUncalibratedThresholdMs`. The ceiling was sized against Anvil's 176,000 ms silence
and has never met a venue whose *healthy* cadence is two thirds of it.

Decide: **ceiling rises, multiplier falls, or both become per-venue.** Record which, and why, with the
cost of the other two stated. The §9 row's constraint is binding — *changing the constant and the
venue in one step leaves no way to attribute a regression* — so if the ceiling moves, it moves in a
commit that changes nothing else.

`binance_atomeur_d100ms_liveness_20260826.ndjson` (221 s, 11 pings, 10 intervals against
`kMinSamples`' 8) is the first committed artefact that enters the calibrated branch. **Ship a test
that enters it and asserts the threshold that comes out** — the policy is host-checkable even though
the behaviour is not.

*One thing already established, so this stage does not have to reason about it:* the deferred
median-convention defect (strain 29) **cannot change this decision**. Interpolated gives
4 × 19,963.97 = 79,855.9 ms; nearest rank gives 4 × 19,947.7 = 79,790.8 ms. Both clamp to 30,000 today,
and if the ceiling rises to ~80 s the two conventions differ by **65 ms, or 0.081%**. State it and move
on — it is the argument that the M5 close-out deferral costs this stage nothing.

### 2 · Pick a remedy for the lying socket, and invert the fixture that proves it

Four candidates, none built at B1, all in strain 26 with costs: **(a)** defer the Snapshot to bracket
time — cheapest, no new concept, costs a fraction of a second of grey on a healthy connect and up to
~10 s on a quiet pair; **(b)** the ping does not stamp liveness until the bracket has been satisfied
once — no added grey, and the lie self-terminates at the threshold; **(c)** a *never-started* detector,
a genuinely new question needing a threshold of its own; **(d)** grade the seeded book against the
`@depth20` stream the board already pays 1.33 KiB/s for.

**Deliverables 1 and 2 are coupled and the table does not say so.** Remedy (b)'s cost *is* the
threshold — ~30 s today, ~80 s if the ceiling rises. Choosing (b) and raising the ceiling in the same
evening triples how long the lie renders LIVE. Decide them together, in that knowledge, and say which
constrained which.

Say out loud what the choice does **not** buy: neither (a) nor (b) closes the general hole. A
server-side subscription drop an hour into a session still presents identically. What they convert is
*permanent* into *bounded*, and bounded is where invariant #5 draws its line.

**The fixture clause is binding.** `test_binance_adapter.cpp`'s silent-stream case asserts today's
broken behaviour — `status == Live`, no stale episodes, `seed_bracket_ok == 0`, a populated book. When
a remedy lands that case **must** fail, and the correct response is to **invert the two assertions
marked THE LIE in the commit that makes it pass** — not to delete it and not to relax it. If C ships
without inverting it, the fixture does not lapse: it moves to whichever stage next touches liveness,
with this clause attached.

### 3 · The engine state a re-seed in flight needs — the item the inheritance table missed

Strain 28: the trigger B2 built asks for a re-seed and no layer answers it on a live book.
`reseed_wanted()` is a request nothing can serve; the adapter says so out loud via
`resnapshots_declined` / `resnapshots_adoptable` rather than dropping it silently.

The mechanism and its memory are **D's** — (a) a ~128 KiB deferred buffer, (b) drop-gap-reseed, (c)
merge below the touch. **C owns the state that gets published while a fetch is in flight**, because
D cannot decide what to render without one and the two candidate renderings differ in nothing a host
test can assert. Publish it in existing padding if it fits, as A2 and M4 stage C both did, and pin
`sizeof(DisplaySnapshot)` unmoved on **both** host and xtensa — three documents quote a byte count
derived from it.

The measurement that constrains (b) is already taken and belongs in the record here: adoption is
loss-free whenever the body is not older than the book, and B2 measured that two ways, agreeing on
every capture — **0 of 7 adoptable at `limit=1000` on the liquid pair, 19 of 19 everywhere else**. The
deeper the seed the older it lands: the venue snapshots roughly three quarters of the way through the
round trip and spends the rest shipping ~120 KB.

> **CORRECTED 2026-08-27 (M5 stage D, scoping): ~120 KB is 64,046 B** — quoted here from
> `NOTES-binance.md`, which carries the correction, the eleven-body measurement and where the figure
> came from. Left standing per ARCHITECTURE §9's own rule. The adoptability measurement this
> paragraph exists for is unaffected.

### 4 · Write the transport-versus-feed hole where the code can see it

Item 5 has no fix and this stage is not to invent one. The ping is emitted below the subscription;
no care with stream names closes it; the venue publishes no subscription-state signal on the
market-data streams at all. **What C owes is that this is stated at the point of use** — beside the
thing that stamps liveness, not only in §9 and a strain card — so the next reader of a green clock
learns what it is entitled to mean without having to find two documents.

### 5 · The three `age_ms` limits, recorded per venue, and item 8 decided

All three measured at B2; none needs re-measuring.

- **Blind to a feed backlog, tracks a socket backlog at 1.00×** — the ping shares the TCP stream. B1's
  hypothesis narrowed rather than confirmed.
- **No reading at all for the first ~638.8 s of every connection** — `kBaselineSamples = 32` at a
  ~20 s cadence, against 16 s at Anvil and 32 s at Kraken. Decide whether the header shows *no
  reading* rather than a number it cannot support; **the shape already exists** — A2 used it for a
  reconnect. C publishes the state; D decides how it looks.
- **`kAgeWindowSamples = 256` is ~85.2 min at this venue**, and the constant's own comment says it is a
  2 KiB memory budget and never a duration. **Handed to C undecided, and C decides it**: is an
  85-minute supremum window defensible at a 20 s cadence, or does the constant become per-venue? Either
  answer is fine; carrying it further is not.

### 6 · Ask the parity question explicitly rather than letting D discover it

**Three of M4's honesty mechanisms are compromised at this venue from one cause**, not three: the grey
threshold has nothing to arm on, the age meter has nothing to measure against, and the meter
additionally has no reading for the first ~11 minutes. Whether M5's definition of done can still claim
parity with M4's is a question this stage answers in writing — yes with these caveats, or no and here
is the reduced claim. B2 flagged it precisely so it would not be discovered at the bench.

### 7 · Writeback

Session log with decisions **and why**; `ROADMAP.md`'s M5 row; `NOTES-binance.md` for anything
measured, provenance stated beside each figure; `ARCHITECTURE.md` §9 for anything with architectural
weight; `docs/DESIGN.html` for every card this stage closes or moves, its milestone strip, and §08.
**Raise the duplicate card 26** rather than renumbering it unasked.

---

## Constraints

- **§6 frozen. §4 does not move.** No new `GapReason`, no new `FeedEvent::Kind`. **Sixth asking**; the five
  before it were all refused. Stop and raise.
- **Desk only.** No `firmware/`, nothing flashed, and **no judgement about what the panel looks like.**
  Every such question is D's by the ruling above; if the work seems to need one, that is the signal
  that the split was drawn wrong — raise it, do not decide it.
- **The ceiling, if it moves, moves alone.** §9, 2026-08-25.
- Pin tables add-rows-only. **No golden moves** — deliverable 0 restores committed bytes and is not an
  exception to this.
- **Per-commit verification in a fresh detached worktree, `CMAKE_HOME_DIRECTORY` confirmed to point at
  the worktree**, loop run **inline** — `powershell -File` dies at `CMakeTestCXXCompiler`. This desk
  uses the **`host-mingw`** presets. Delete stale worktrees when done.
- **Push the stage to `m5/stage-c`. Fast-forward `master` only once the ladder has closed.**
- **Commit only when asked.**

## Known unknowns — resolve and record

Whether the ceiling, the multiplier, or per-venue values is the right shape, and what the other two
would cost. Whether remedy (a), (b), (c) or (d) — and what the threshold decision did to that choice.
Whether the state a re-seed in flight needs fits in existing padding. Whether an 85-minute supremum
window is defensible at a 20 s cadence. Whether M5 can claim parity with M4's definition of done.
**And whether the duplicate card 26 is renumbered now or after the inbound references are counted.**

## Definition of done

- ☑ Both Anvil traces restored to LF, hashes confirmed against `HEAD` (`762cb1e2…`, `37fae0f7…`).
      **Not "in the split's first commit", because that commit is empty by construction** — the
      committed blobs were always LF, so the repair has no diff. What is in the split's first
      code commit instead is the **guard**: no committed trace may contain a CR byte. See the
      session log's *deviations*.
- ☑ The threshold decided — ceiling, multiplier or per-venue — with the other two costed, the
      constant moved in a commit of its own if it moves, and **a test that enters the calibrated
      branch and asserts what comes out.**
- ☑ One of the four remedies built, **chosen jointly with the threshold and the coupling recorded**,
      and the general hole stated as still open.
- ☑ `test_binance_adapter.cpp`'s silent-stream case **inverted, not deleted and not relaxed**, in the
      commit that makes the remedy pass.
- ☑ The re-seed-in-flight engine state published, `sizeof(DisplaySnapshot)` pinned unmoved on host
      and xtensa.
- ☑ The transport-versus-feed hole stated at the point of use.
- ☑ Items 6, 7 and 8 recorded per venue; **8 decided.**
- ☑ The parity question answered in writing.
- ☑ The duplicate strain card 26 raised with its reference count.
- ☑ ctest green (50/50, `host-mingw`); no golden moved and no pin moved; session log · ROADMAP ·
      NOTES · §9 · DESIGN all written; **split proposed and nothing committed**, as the last
      constraint requires.
- ☐ **NOT DONE, AND IT CANNOT BE UNTIL THE SPLIT IS APPROVED:** the commits created, each verified
      in a fresh detached worktree with `CMAKE_HOME_DIRECTORY` confirmed, `m5/stage-c` pushed, and
      `master` fast-forwarded only after the ladder closes. Left as an open box rather than folded
      into the one above, because a tick covering both would be claiming a verification that has
      not run.

## Out of scope

**Everything about what the panel looks like — every one of them D's**: what a silent feed renders,
what a re-seed in flight renders, whether the chosen remedy's grey reads right, and strain 24's
unvalidated-levels question (*C records the number per policy; D decides knowing it*). The re-seed
**mechanism and its memory** (D). The **median convention** and `trace.cpp`'s second home for it —
**M5 close-out**, and deliverable 1 shows the deferral costs this stage nothing. The bench, the
>24 h soak, the board's audit stream (D). The client ping (M6). The venue toggle (M7). M4's carried
residues — D1, D2, D7's scope trace, and M4 stage D's own card 26 (**renumbered to 30 by this stage**). The §1-versus-reality trade-print
question — ROADMAP backlog **D0**, owner's, on no stage.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->

### 2026-08-26 · Claude Opus 5 (1M context) · the threshold, the remedy and the state a re-seed needs

**Green.** 50/50 ctest on `host-mingw`, 407 doctest cases / 3,610,456 assertions in `dc_tests`.
**No golden moved and no pin moved:** every Anvil and Kraken `dc_replay` report is byte-identical
before and after, and the eleven Binance reports differ in exactly one line each — the GREY line
whose number this stage decided. `dc_binance_oracle` returns B1's and B2's figures unchanged
(235/235, 235/235, 88/88, 88/88). **Nothing committed** — the split is proposed below and awaits
approval, per the brief's last constraint.

**Done, with the why.**

1. **The threshold: both the multiplier and the ceiling become per-venue** (deliverable 1).
   `LivenessPolicy{multiple, floor_ms, ceiling_ms}` is a constructor parameter on `LivenessClock`,
   **defaulted to the shipping constants**, selected per venue from `venue.hpp` the way `Book`
   already takes `validated_depth`. Binance is `{2.0, 2,000, 60,000}` and the threshold is
   **39,927.94 ms — calibrated, and strictly inside its own bounds for the first time.**
   *Why not the other two:* a global ceiling raised to clear 79,855.9 also raises
   `kUncalibratedThresholdMs`, which **is** the ceiling, at Anvil and Kraken — one constant, three
   venues moved, to fix one. A global multiplier cannot reach a live calibration here at all: it
   must fall to **≤ 1.503** to come under today's 30,000, which is **below Anvil's measured 1.937×
   worst healthy multiple**, and anything above that leaves Binance clamped while moving Kraken's
   pinned 4,000 ms. *Why 2.0 is derived rather than chosen:* `liveness_clock.hpp` already states the
   rule that produced Anvil's 4.0 — worst healthy as a multiple of the venue's own median, times ~2
   of margin — and this signal measures **1.005×** over B2's ten intervals, so the same rule gives
   2.0 at 1.99× margin. *Why the ceiling is 60,000:* it must clear 39,927.94 or the clamp is the
   threshold again, and it admits a cadence 50% slower than measured before it binds.
   **The cost is stated rather than left to be found:** the uncalibrated default IS the ceiling, so
   Binance's pre-calibration threshold goes **30 s → 60 s**, for the 159.7 s `kMinSamples` takes at
   this cadence. Decoupling the two is the successor and was deliberately not taken — it would be a
   fourth number with no measurement behind it, in a stage whose whole argument is that the
   multiplier was derived.
   B2's *"...and what it calibrates to is INERT"* case went red exactly as its author intended and
   was **rewritten deliberately**, keeping what it used to assert, which is the DoD's *test that
   enters the calibrated branch and asserts what comes out*.

2. **Remedy (a) is built, and (b) was rejected on a finding** (deliverable 2).
   `adopt_seed()` no longer emits; `replay_buffer()` and `check_continuity()` publish the
   `Snapshot` at the instant a diff satisfies `U ≤ lastUpdateId + 1 ≤ u`, and if neither ever does,
   nothing is ever published. *Why (a):* it needs no new concept, no `GapReason`, no
   `FeedEvent::Kind` and no firmware, and the between-state — an uninitialised book — has been
   explicit since M4 stage C and grey since M4 stage D. *Why not (b), and this is the finding worth
   more than the choice:* strain 26 records (b) as self-terminating at the threshold, and **it does
   not, on the board.** `LivenessWatchdog::expired()` is `armed_ && now >= deadline_ns()` and
   `armed_` is set only by the first `on_liveness`, so withholding liveness on a lying socket
   leaves the watchdog **never armed** — the lie becomes permanent *and* invisible to the very
   instrument (b) relies on. Making it work needs a never-armed-since-connect deadline, which is (c).
   **The coupling ran one way and is recorded as such:** (b)'s cost *is* the threshold, so raising
   the ceiling would have tripled the lie's LIVE window — an argument against (b), not against the
   ceiling. (a) has no threshold dependency at all, so **the threshold decision constrained the
   remedy and the remedy constrained nothing.**
   The silent-stream fixture's two assertions marked THE LIE are **inverted, not deleted and not
   relaxed, in the same commit**. Its file keeps the `DEFECT` name deliberately: it is a capture of a
   real defective *condition*, which is as real after the remedy as before.

3. **The re-seed-in-flight state is published** (deliverable 3). `DisplaySnapshot::reseed`,
   `{None, Wanted, InFlight}`, stamped feed-side one line after `Book::publish` exactly as `age_ms`
   is. It lands in the **four pad bytes the struct already carried** between `SymbolSpec` and
   8-aligned `seq`; `sizeof(DisplaySnapshot) == 1168` and `sizeof(SnapshotChannel) == 3528` verified
   on the host **and on xtensa-esp32s3 GCC 8.4.0 — the version `platformio.ini` pins — with a
   negative control asserting 1169 to prove the check discriminates.** *Why an enum with a state
   nothing produces:* `InFlight` being unreachable in this build **is** strain 28, and publishing it
   puts the open card where a bench sitting can see it rather than on a report line. *Why Binance
   only:* Kraken's `resync_wanted()` is a re-subscribe, already served, and settled at M4.

4. **The transport-versus-feed hole is stated at the point of use** (deliverable 4) — beside
   `on_liveness` in `liveness_clock.hpp`, with the three venues' emission points in a table.
   **One point of use is deliberately not covered:** `LivenessWatchdog::on_liveness` is in
   `firmware/`, which this stage may not touch. **The same sentence belongs there and it is D's.**

5. **The three `age_ms` limits recorded per venue, and item 8 decided** (deliverable 5).
   `kAgeWindowSamples = 256` **stays and does not become per-venue**: the window bounds the
   estimator's *reach*, and the sup binds only on a backlog older than the window itself, so wider
   can only describe an older backlog and never overstate a newer one — at a 20 s cadence wider is
   the safe direction, and shrinking it per venue would buy back 2 KiB and cost exactly that. The
   constant that bites is `kBaselineSamples`, and that one cannot shrink either, because 32 is what
   a measurement set. The *no reading* state D needs **already existed** — `has_age`, M4 stage A2 —
   so C confirmed it and pinned the per-venue duration as a behaviour (false at 221 s, with an Anvil
   control that reads) rather than adding a second flag.

6. **Parity: NO**, with the reduced claim written out (deliverable 6) — `NOTES-binance.md` §6.
   Both of M4's honesty mechanisms are narrower here from one cause, and B2 asked for this to be
   answered in writing precisely so it would not be discovered at the bench.

**Two deviations from the brief, both deliberate, both flagged rather than quietly taken.**

- **Deliverable 0's commit is empty as specified, so what ships is a guard rather than the repair.**
  The two traces' committed blobs were always LF; restoring the working copy produces no diff.
  `git status` reported the tree clean throughout the nineteen days because the index's cached stat
  data was written when the file *was* CRLF — and for the same reason `git checkout --` alone does
  not repair it, the file has to be deleted first. A repair with no commit is one the next stale
  clone undoes in silence, which is exactly what `CLAUDE.md` says is not a check. So
  `test_replay_goldens.cpp` now asserts **no committed trace contains a CR byte**, positively scoped
  to the directory that enumerates itself, with a count assertion against a vacuous pass, and
  mutation-verified with one injected CR. Hashes confirmed as the brief asked: `762cb1e2…` and
  `37fae0f7…`, both equal to `HEAD`.
- **Two pre-existing DESIGN drifts corrected while in there.** `DisplaySnapshot::initialised` has
  been on the struct since M4 stage C and was never drawn in §03's class diagram. A member missing
  from a diagram is §09's own first trigger, and leaving it because it is not this stage's is how a
  design doc drifts. Added with `reseed`.

**Raised, not executed — both as the brief instructed.**

- **The duplicate strain card 26.** Counted rather than asserted: **15 textual references across 8
  files at `HEAD`; 14 mean the M5 card and one means the M4 card — and both live in `ROADMAP.md`,
  two rows of the same table apart.** This stage adds six more to the M5 card in four further files,
  so it is ~20 across 12 and grows every stage. Card 23's precedent says the newer card moves and
  *"the older number keeps its references"*; **here the reference weight is inverted, so the
  precedent's letter contradicts its reason.** Recommendation: move the **M4** card to 30 and let
  the M5 card keep 26 — one reference to edit instead of twenty. And the pattern, noted while
  raising it: **both collisions were created by a stage B1 opening a card, a milestone apart, and
  nothing checks.**
- **Strain 29's tripwire is written against the wrong event.** It fires *"if any stage needs to
  quote or re-pin a Binance cadence figure"*, and C quotes one — 19,963.97 ms, six times — so read
  literally the convention change must land inside this split, which the same clause forbids two
  sentences earlier. The hazard was never *quoting a cadence figure*; it was **quoting one from the
  wrong home**. C quotes `lower_median`'s answer, re-pins nothing, and asserts that the two
  conventions differ by **10.8 ms (0.027%)** here and land strictly inside the same bounds. Read as
  not firing; the wording is handed back to the M5 close-out.

**The `code-review` skill was run against the diff before the split was proposed, and it found two
defects in this stage's own new code. Both are fixed, both with a red-before-green, and both are
recorded here rather than only fixed — they are the same family this log keeps naming.**

1. **One event, two answers, depending on which of two bracket sites saw it.** A seed the feed never
   confirmed reaches `drop_book` down two paths — `replay_buffer`, when a buffered event survives the
   snapshot, and `check_continuity`, when none does and the first diff plays the survivor's role. The
   new `seeds_unconfirmed` counter is computed in `drop_book` from `bracket_checked_`, and
   `check_continuity` set that flag **before** testing the bracket, so a failure on that path arrived
   with the flag already true and **counted nothing**, while the identical failure through
   `replay_buffer` counted one. The pre-existing assignment was harmless for three milestones
   precisely because nothing read the flag on the way out; adding a counter that reads it is what
   made the ordering load-bearing. Fixed by moving the assignment into the success branch — behaviour
   is otherwise unchanged, because `drop_book` clears the flag anyway. **Covered by a new case, and
   the case was run against the pre-fix code: 0 == 1, red.** The case exists as its own test rather
   than a line in the neighbouring one because the property being asserted is *the two sites agree*,
   and a property about two paths cannot be asserted by exercising one.

2. **The report printed a multiplier it had looked up separately from the threshold it printed
   beside it.** `trace_report.cpp` rendered `"N ms (Kx the observed median)"` with N from the clock
   and K from the venue table — two homes for one number, in the report a reader checks the watchdog
   against, which is the drift §9 keeps catching in other people's code. It could not disagree today
   because both read the same row, and it would have disagreed the moment anything constructed a
   clock with a policy the table did not hold. `TraceStats` now carries `liveness_multiple`, taken
   from `liveness.policy().multiple` — the clock's own — and the report reads only that. **Every
   `dc_replay` report is byte-identical across the change**, which is the point: the fix is to the
   provenance, not to the number.

*(The review also collapsed four copies of the calibration trace's replay setup onto one helper. That
one is tidying and is noted only because it briefly broke the build — three cases were still reading
`spec` and `path` locals the shared setup had declared, which is exactly the hazard of extracting a
helper from setup that other assertions reach into.)*

**Nothing about the panel was decided.** Every question of the form *what does this look like* —
the silent feed, the re-seed in flight, whether (a)'s grey reads right, strain 24's unvalidated
rows — is D's under this brief's own scoping ruling, and none was touched.

**Exact next step.** Get the split below approved. Then create the commits, verify each in a fresh
detached worktree with `CMAKE_HOME_DIRECTORY` confirmed to point at the worktree and the loop run
**inline** (`powershell -File` dies at `CMakeTestCXXCompiler`), push to **`m5/stage-c`**, and
fast-forward `master` **only after the ladder has closed**. Note for the executor: **the
`LivenessPolicy` commit and the Binance-values commit are one file's edits split in two, so the
ladder must build the `LivenessPolicy` one with all three `venue.hpp` rows still `{}`** — the
intermediate state is real and testable, and it is what makes the value change attributable.

*Corrected at the owner's approval, and the correction is the record.* This paragraph originally
read *"commits 2 and 3 … build commit 2"*, and the table below has the pair at **3 and 4** with **3**
as the one that must build with the rows `{}`. Off by one in both places, because `docs: the stage C
brief` was added as commit 1 *after* the paragraph was written and the prose was not re-read. **It is
the species B2 recorded one stage earlier** — *"the headline count did not survive its own table"* —
arriving in the same document family, in the sentence written to prevent the confusion, and nothing
could have caught it: no test reads a split table, and prose and rows are checkable against each
other only by someone doing it. **The fix is not to re-read more carefully.** Commit ordinals are
now gone from this paragraph: it names the commits by their message, which cannot go stale when one
is inserted. See the `CLAUDE.md` proposal in the approval-response log below.

#### The proposed split — 8 commits, nothing committed yet

| # | message | what is in it |
| --- | --- | --- |
| 1 | `docs: the stage C brief` | the work order, untracked until now |
| 2 | `harness: no committed trace may contain a CR byte` | the guard, plus the working-copy LF restore (which has no diff). **Red before this commit only via the injected-CR control, which is recorded rather than committed** |
| 3 | `engine: the liveness clamp becomes a policy, with the shipping values as its defaults` | `LivenessPolicy`, the `LivenessClock` ctor, the `VenueTraits` field with **all three rows `{}`**, and the plumbing through `Replay`, `trace.cpp` and `trace_report.cpp` — including `TraceStats::liveness_multiple`, so the report's "Kx" comes from the clock that produced the threshold beside it rather than from a second lookup. **No number moves anywhere** |
| 4 | `harness: Binance's liveness clamp is 2.0 x median, ceiling 60 s` | the one row's two values and its note; the inertness case rewritten to assert what comes out. **The only commit in which a threshold moves, and it moves for one venue** |
| 5 | `engine: the Binance seed is not published until a diff brackets it` | remedy (a), `seeds_unconfirmed`, `seed_confirmed()`, the `bracket_checked_` ordering fix with the case that makes the two bracket sites agree, the three unit cases that seeded without a bracket, `test_trace_records`' fourth record, **and the silent-stream fixture inverted** — one commit, because its expiry clause says *in the commit that makes it pass* |
| 6 | `engine: publish whether a re-seed is outstanding` | `ReseedState`, the `DisplaySnapshot` field, `stamp_reseed`, the `dc_ladder` line, the transition test |
| 7 | `engine: what a green liveness clock is entitled to mean, at the point of use` | deliverable 4's paragraph, `kAgeWindowSamples`' decision, the no-reading pin with its Anvil control |
| 8 | `docs: stage C — the threshold, the remedy, and what could not be closed` | this log, ROADMAP, NOTES-binance §C, ARCHITECTURE §9 (five rows), DESIGN (cards 26 and 28, §03, §09, the strip) |

### 2026-08-26 · Claude Opus 5 (1M context) · the approval response — one fix, two records, one move

**Green.** 50/50 ctest on `host-mingw` from a wiped build tree. **No new behaviour**, as the
approval's first constraint requires: everything below is prose, records, and one card renumbered.
No threshold and no pin moved.

**§0 · The off-by-one is fixed, and the fix is not "re-read more carefully".** The approval is
right on both counts: the pair is **3 and 4**, and the commit that must build with all three
`venue.hpp` rows still `{}` is **3**. The mechanism was exactly as diagnosed — `docs: the stage C
brief` was added as commit 1 after the paragraph was written. **The paragraph no longer contains a
commit ordinal at all:** it names the two commits by their messages, which cannot go stale when one
is inserted. The correction is recorded in place beside it, with its species named, rather than
silently applied.

**§0's second half · the `CLAUDE.md` line — proposed, with the remedy changed and the diagnosis
accepted.** The class is real and the home is right: tooling, not architecture, so `CLAUDE.md`
beside the sentinel and `powershell -File` notes rather than a §9 row. **But the proposed wording
ends in *"when the commit count changes, re-read the prose"*, and that is the species the paragraph
three lines above it already refuses** — *a check that depends on the right person reading the right
document at the right moment is not a check*. It would be the sentinel guard's own failure, written
into the file that names it. So the proposal keeps the diagnosis and replaces the remedy with one
that removes the second statement instead of asking someone to compare the two:

> **A split's prose must name a commit by its message, never by its ordinal.** An ordinal restates
> a fact the table already owns, and it goes stale silently the moment a commit is inserted — which
> is how M5 stage C's *exact next step* came to instruct an executor to build the wrong commit with
> the wrong rows. A message is the same string the table holds, so there is nothing left to drift.
> Same family as B2's *"the headline count did not survive its own table"*: **the fix for a fact
> stated twice is to state it once, not to check it twice.** Tooling rather than architecture, so
> no §9 row.

`CLAUDE.md` is **not edited** — the approval says the owner's call, and this is the wording offered.
It is already applied to this brief's own prose, so the proposal is a rule with an instance rather
than a suggestion.

**§0's third half · the `bracket_checked_` defect is referenced, not re-filed.** Added to §9's
*three ways a green suite is wrong* row as **the third instance of clause (1)**, the coincidence
class. What it contributes beyond a tally is the trigger: the ordering was harmless for three
milestones because nothing read the flag on the way out, and it became load-bearing the moment
`drop_book` started counting off it — **so the hazard is not the ordering, it is the new reader.**

**§1 · The multiplier's bound is recorded in both places, and k is not touched.** The approval's
catch is exact and this stage had it in a table without drawing the conclusion: read the *binding
case* column rather than the ratio, and Anvil's 4.0 clears **one missed tick** by 2.06× while
Binance's 2.0 clears **jitter** by 1.99× and a **missed ping by 1.000×** — one missed ping is
2 × 19,963.97 = 39,927.94 ms, the threshold is 39,927.94 ms, and `expired()` is `armed_ && now >=
deadline_ns()`. **A single dropped ping greys the panel.** Ten intervals spanning 111 ms cannot
contain a missed-tick event, and the same Anvil signal read 1.094× over 62 idle frames against
1.937× over 1,191 — which is what a short window costs. Recorded verbatim in the NOTES §C multiplier
table and on the §9 per-venue-policy row, with the falsifier and the k ≤ 3.005 headroom.
**k stays at 2.0**: moving it on desk evidence would change the constant and the venue in one step,
which is the trap the §9 row this stage wrote exists to name. It is D's, on the soak.

**§2 · Card 26 → 30 executed, and *Owed by stage D* written.** The M4 card moves and carries the
reasoning; its one inbound citation — `ROADMAP.md`'s M4 row — is repointed, and the M5 card's
paragraph changes from a recommendation to a record. The follows-the-reason-not-the-letter sentence
is on card 30, with the observation the approval added and this stage had missed: **the two cards do
not merely collide, they interact** — remedy (b)'s rejection rests on `armed_` having exactly one
setter, which *is* card 30, so (b) is blocked by an open M4 card rather than wrong in principle.
That is item 6 of *Owed by stage D*.

**Nothing else moved.** `git diff` over `harness/replay/*.ndjson` is empty, every Anvil and Kraken
`dc_replay` report is byte-identical, and the eleven Binance reports still differ only in the GREY
line this stage decided.

---

### Owed by stage D

Written at stage C's close, and written **because C had already paid for its absence**: §1 above is
a bound on this stage's own multiplier that was visible in this stage's own table and stated to
nobody until the owner asked for it. B2 opened *Owed by stage C* for exactly that reason — *a list
that has to be reconstructed by grepping is a list that arrives short* — and C inherited the
diagnosis without the practice. Same two-column shape.

| # | What D owns | Where the evidence is |
| --- | --- | --- |
| 1 | **The four panel questions C deliberately did not take**: what a silent feed renders, what a re-seed in flight renders, whether remedy (a)'s grey *reads* right, and strain 24's unvalidated-levels question. **Do not re-argue whether they are D's** — this brief's *scoping ruling* settled it from M4 twice (the triage's engine-state-vs-rendering split, and stage D item A1's *true of the BEHAVIOUR and not of the POLICY*), and item 4 leaving C is the only reason C fitted in an evening. | This brief, § *The scoping ruling*; `M4-triage-of-the-twelve.md`; `M4-stage-D-the-bench.md` item A1. C records the number per policy; D decides knowing it. |
| 2 | **The re-seed mechanism and its memory** — strain 28's D-half. Three candidates, priced: **(a)** a ~128 KiB deferred buffer (no gap, no grey, and it more than doubles the adapter's ~96 KiB of fixed state); **(b)** drop-gap-reseed (free in memory, greys the panel for the length of a fetch on a book that was still correct); **(c)** merge below the touch (cheapest, least proven, nothing in the corpus exercises it). **The engine state to render during a fetch already exists** — `DisplaySnapshot::reseed`, and D advances it to `InFlight`. | DESIGN strain 28; §9 2026-08-26. B2's adoptability measurement is unchanged and decides between them: **0 of 7 adoptable at `limit=1000` on the liquid pair, 19 of 19 everywhere else** — the deeper the seed the older it lands, because the venue snapshots ~¾ of the way through the round trip and spends the rest shipping ~120 KB. |
| 3 | **The multiplier's bound, and it is the soak's FIRST named check.** `k = 2.0` clears this signal's jitter by 1.99× and a **dropped ping by 1.000×**. **Falsifier: record the ping-interval distribution across the soak; any interval reaching 2 × median on a healthy socket raises k.** If it fires, **k rises alone** — the 60,000 ms ceiling already admits k ≤ 3.005 (3.0 → 59,891.91 ms), so the remedy is one value in one row, with no ceiling change and no second attribution problem. | §9 2026-08-26 (the per-venue-policy row); `NOTES-binance.md` M5 stage C addendum §1. The comparison that sizes it: the same Anvil signal read **1.094× over 62 idle frames and 1.937× over 1,191 intervals**. |
| 4 | **The uncalibrated-default window.** `kUncalibratedThresholdMs` **is** the ceiling, so Binance's pre-calibration threshold is **60 s** where it used to be 30 s, for the **159.7 s** `kMinSamples = 8` takes at this cadence — on *every* connection, and the board reconnects. Decoupling the two was deliberately not done at C: it would be a fourth number with no measurement behind it, in a stage whose argument is that the multiplier was derived. **State whether that window matters on the board.** That is a soak observation and not a desk one. | §9 2026-08-26; `NOTES-binance.md` §C.1's *cost, stated*. M4 stage D's B3 is the precedent for what a reconnect-heavy night looks like: 2 half-open outages in 25.39 h, and 21 reconnects in 86 minutes at M3. |
| 5 | **Parity: NO, and D is where the reduced claim is TESTED rather than asserted.** *The panel greys within the calibrated liveness threshold of the **socket** falling silent — 39.9 s — and refuses to colour a ladder the feed has never confirmed. It does not detect a subscription that stops server-side while the socket stays up. `age_ms` is a lag estimate for a socket backlog only, and reads nothing for the first ~11 minutes of every connection.* Both halves are narrower than M4's from **one cause**. | `NOTES-binance.md` §C.6; §9 2026-08-26. The three per-venue `age_ms` limits are B2's and need no re-measuring: **tracks a socket backlog 1.00×, BLIND to a feed backlog, 638.8 s with no reading, 85.2 min supremum window.** |
| 6 | **M4's card 30 (opened as 26) is a LIVE DEPENDENCY, not a residue.** Remedy (b) — *the ping does not stamp liveness until the bracket is satisfied* — is **not wrong in principle; it is blocked by that card being open.** `LivenessWatchdog::expired()` is `armed_ && now >= deadline_ns()` and `armed_` has exactly one setter, so withholding liveness leaves the watchdog never armed and the lie permanent. **If `armed_` ever gains a second setter, (b) becomes available and its trade against (a) reopens.** Written down here so the next stage does not re-derive the rejection from scratch. | DESIGN cards 26 and 30; §9 2026-08-26 (the remedy row); `binance_adapter.hpp`'s *WHY (a) AND NOT (b), (c) OR (d)*. |

> **CORRECTED 2026-08-27 (M5 stage D, scoping): item 2's closing "~120 KB" is 64,046 B.** Left
> standing per ARCHITECTURE §9's own rule; the correction, the eleven-body measurement and the
> figure's provenance are in `NOTES-binance.md` § *A re-snapshot on a live book*. **Item 2's
> *other* number is right and is a different quantity** — the ~128 KiB deferred buffer is the
> 15 s diff hold, not a body size. The two sit in one row here, one in each column, and in
> `NOTES-binance.md` in one section either side of the adoptability tables; that co-location is
> the likeliest account of how the wrong one came to be written.
> Nothing else in the row moves: the three candidates and the 0-of-7 / 19-of-19 measurement that
> decides between them are unaffected.

Two cautions carried with the list, both inherited from B2's and still true:

- **Every fetch-latency and cadence figure D will read is a desk-box figure**, and **M4 stage D's
  bench day B3** measured DNS at 14,000 ms on the board. The provenance is stated beside each number
  in `NOTES-binance.md`; keep it stated.
- **Three of M4's honesty mechanisms are compromised at this venue from one cause.** C bounded the
  connect case and stated the rest; **none of it has been seen on a panel.**
