# M5 Stage D-A4 — audit against master, 2026-09-05

**Verdict: the brief needs rewriting before it is worked.** Its premise is spent, one of its two
open candidates is now excluded by an assertion that exists rather than merely being expensive, and
five of its figures or referents have moved. Nothing in it was wrong on 2026-08-30; it was written
before D-A3 closed, before D-C's first run was read, and before stage E landed.

**Written:** 2026-09-05, from the desk seat, against the tree. **Nothing was edited** — the brief is
unchanged and no repo file was touched by this audit.

---

## Provenance, and the caveat that qualified everything below

Read from the working tree on `iguana-republic` at `C:\Development\Projects\DepthCharge`:
`ARCHITECTURE.md`, `ROADMAP.md`, `docs/DESIGN.html` (strain cards 28 and 29),
`docs/briefs/M5-stage-D-{A1,A2,A3,B,C}` and `M5-stage-{D,E}`, `firmware/src/venue_budget.hpp`,
`firmware/src/ladder_render.hpp`, `firmware/src/feed_task.cpp`, `firmware/platformio.ini`,
`engine/include/depthcharge/{display_snapshot,snapshot_channel}.hpp`,
`harness/tests/test_ladder_render.cpp`, and `.git`'s refs and reflogs.

**The branch position.** `HEAD` is `m5/stage-e-publish-boundary` at `c664f20`; `master` is `ba835db`,
one docs commit behind (`c664f20` is *"docs: D-C's status and the log of its first run"*).
**And the first pass's push claim was wrong, from exactly the mistake this audit is about.** It
read `packed-refs`, found `origin/master fbd6d80`, and concluded *"none of this has been pushed"*.
The packed value is stale — a loose `.git/refs/remotes/origin/master` overrides it. `git rev-parse
origin/master` is **`ba835db`** and `git log origin/master..master` is **empty**, so master is fully
pushed; `origin/m5/stage-e-publish-boundary` is at **`c664f20`**, so the branch is published too. A
ref read out of a file rather than asked of git is the same species of error as a figure read out of
a brief rather than from source, and it survived into a document whose subject is that error.

**And the same shape once more, in §1's hazard.** *"It is being chased in `firmware/` by another
session"* was carried over from D-C's brief and never checked. On 2026-09-06 there was no branch, no
brief and nobody on it; it is now backlog **D10**. D-A4 is not gated on it.

**The limit on the method is discharged (2026-09-06).** The first pass read the tree by staging
files, because the Linux workspace on the box would not start, and could not say whether the working
tree was clean. It has since been checked. `git status --porcelain=v1` reports **199 files modified,
and every one of them is line endings alone** — 109,184 insertions against 109,184 deletions, and
`git diff --ignore-cr-at-eol` is **empty**. The Windows checkout is CRLF, as `.gitattributes` says in
its own comment, and the shell that ran the check is a Linux VM whose git has no `core.autocrlf`, so
the whole tree reads as modified through it. **The working tree's content is byte-identical to
`HEAD`.** Refs are exactly as expected: branch `m5/stage-e-publish-boundary`, `HEAD` `c664f20`,
`master` `ba835db`, and `git diff master HEAD` touches one file —
`docs/briefs/M5-stage-D-C-the-soak.md`, +119/−2.

**No figure moved.** Every figure below was re-read from `git show master:<path>` and matches:
`kVenueInternalBudgetBytes == 111'624` (`venue_budget.hpp:165`), `kAnvilAdapterBytesAtMeasurement`
8,400 (:133), the linker's +59,660 (:64), the header's 44,596 B (:18), the stale `71'308` (:192);
`static_assert(sizeof(DisplaySnapshot) == 1168)` (`display_snapshot.hpp:192`) with `InFlight` still
unreachable (:62); 51,188 B steady state at 3.06× and 53,236 B at 3.18× over 16,717 B; 5,562 seed
fetches at one per 22.4 s, `live=1` 28.5%, grey 71.4%; 7.19 publishes/s, 146.35 events/s, 93.8%,
27.81 h; ROADMAP's *"Owed: the M5 close-out list, and D-A4."* and D6's 1,168→1,176 / 3,528→3,552; the
thirteen-character header (`test_ladder_render.cpp:206`) and the width assertion (:522).

**And what is branch-only is narrower than "the D-C status row".** `c664f20` adds to the D-C brief
its `Status:` line, its 2026-09-03 session log, the six task-watchdog aborts at PC `0x4201c9f8` and
the *"the crash lands before any re-run"* next step. **§2(i) is not among them** — the fetch-duration
disagreement, *no source in the tree produces 5.6 s*, is on master at
`docs/briefs/M5-stage-D-C-the-soak.md:182`. The run's headline figures — 34.56 h, seven boots,
9.84 h continuous — are **also on master**, in the ROADMAP M5 row. Every finding in this audit is
therefore master's.

---

## 1 · The premise moved, and this is the headline

| what the brief says | what master says |
| --- | --- |
| *"it is meant to be spent while the soak is running"* | **D-C has run.** 2026-08-30/09-01, 34.56 h of wall clock in **seven boots**, longest continuous **9.84 h** — and it did not meet its own §1. Stage E is **done 2026-09-05** with a second soak of its own: 32.25 h in two boots, **27.81 h continuous**. |
| *"It therefore blocks nothing that is waiting"* | ROADMAP closes the M5 row with **"Owed: the M5 close-out list, and D-A4."** A4 is the last stage in M5. |
| *"Does NOT depend on D-C, and does not block it"* | True and now moot. D-C's second run is gated on a firmware crash, not on A4. |
| §2: *"**Do not read the soak.**"* | **Inverted.** Two soak records are committed — `hardware/bench-2026-08-30-D-C-soak.md` and `hardware/bench-2026-09-04-E-soak.md` — and both bear directly on A4's known unknowns. |
| *"The soak is the only remaining item in M5 whose duration this project cannot compress"* | Weaker than it was: stage E's soak ran **one socket for 27.59 h with no close of any kind**, refuting D-C §1's premise that Binance closes at 24 h by policy. |

**A hazard the brief cannot know about.** D-C's six restarts are the task watchdog on a starved
`IDLE (CPU 0)` with `dc_feed` on core 0, then `abort()` at the identical PC `0x4201c9f8`. D-C's exact
next step is *"the crash lands before any re-run"*, and it is being chased in `firmware/` by another
session. **A4 touches `firmware/`.**

## 2 · Figures that moved

**(a) does not fit where the brief puts it, and that is the sharpest finding.**
`firmware/src/venue_budget.hpp` now pins `kVenueInternalBudgetBytes == 111'624` (241,720 + 8,400 −
106,496 − 32,000) and asserts `internal_resident_bytes<venue::Adapter>()` against it. Binance's
adapter is **~68,060 B** (`kAnvilAdapterBytesAtMeasurement` 8,400 plus the linker's +59,660 delta),
leaving **~43.6 KiB** under the bound — the file's own header states the margin as **44,596 B**,
and the two should be reconciled, but either is a third of 128 KiB. D-C check 1 then measured the
largest free internal block at steady state at **51,188 B**, so a contiguous 128 KiB *internal*
allocation is out at runtime as well as at compile time.

> **So (a) is no longer "expensive". It is "PSRAM or nothing".** `buf_lvl_` is the precedent and
> ARCHITECTURE §5's *"the window lives in internal SRAM, the tail in PSRAM"* is the stated test. The
> live choice is **(a) in PSRAM** or **(c)** — which is a different question from the one the brief asks.

**"the adapter's ~96 KiB of fixed state" is the wrong quantity for this comparison.** The figure is
still in master as B1's, but **32,768 B of it — `buf_lvl_` — went to PSRAM at D-A1**, so internal
residency is ~66 KiB. Worse, there is a *second* ~96 KiB in the same subsystem: the PSRAM REST body
buffer (`feed_task.cpp:251`, *"the 96 KiB buffer is free the moment this returns"*). Setting
"~96 KiB" beside "~128 KiB" in one sentence is the conflation strain 28 has already recorded once,
as ~120 KB versus ~128 KiB.

**The fetch round trip is a live disagreement and the brief picks a side silently.** The brief states
**3,143–4,485 ms** (D-A2's log). ROADMAP, DESIGN card 28 and **D-B's decision 2 — the decision A4
must implement** — all say **3.1–5.6 s**. D-C §2 lists this as one of four numbers that disagree with
themselves and records that **no source in the tree produces 5.6 s**. Unresolved either way.

**Known unknown 1 is answerable now.** The brief warns against pricing (a) against D-A1's footprint
*"which two stages have since moved"* — correct, and the replacement exists: D-C check 1 supersedes
D-A1's `17,396 / 679 B` with **51,188 B steady state at 3.18× and 3.06×** over the 16,717 B
threshold, and confirms `kReserveInternalBytes` at **104 KiB, not moved**.

**Known unknown 3 has measurements now.** The D-C run took **5,562 seed fetches, one per 22.4 s**,
with `live=1` on 28.5% of samples and grey 71.4% of uptime. And since stage E the panel sees
**7.19 publishes/s** against 146.35 events/s and draws **93.8%** of published frames — which is
exactly the arithmetic *"visible on a panel for 3–5 s"* needs.

## 3 · Referents that moved

**The §2 verification constraint is stale by one day.** D-A3 corrected that §9 row on 2026-08-30:
*"ctest verifies the engine half only"* is too strong. The host suite compiles a `firmware/src`
header **if and only if some host test includes it** — sixteen do — and compiles **no
`firmware/src/*.cpp` and no `platformio.ini` ever**. The test is mechanical: *does a host test
include this file?* D-A3 also added **`dc_tests_binance`** (its first run found a `-Werror=comment`
in `binance_root_ca.hpp`) and closed at **ctest 52/52, six arms build**, so the DoD's
*"ctest green **and** `pio run -e depthcharge-binance` green"* names less than the ladder now is.

**Deliverable 2's stamp site moved.** Stage E made a publish **one per venue message** rather than
one per level. The field is stamped in `FeedTask::publish_current()`, between `book_.publish` and
`channel_.publish` — the position card 28 describes as *"one line after `Book::publish`"*, which now
means once per message. `InFlight` is still genuinely unreachable (`display_snapshot.hpp:62`), so the
deliverable stands; only its site and cadence need naming.

**The fetch side is already built.** `SeedAction::Issue` → `seed_.request(...)` runs on the board
today, 5,562 times in the D-C run. What is missing on a *live* book is the adoption and the state
advance — deliverable 1 is narrower than *"build the mechanism"* reads.

**§3b's defects were fixed 2026-08-27**, and §3b's own status note says *"the proposed owner below is
superseded"*. Listing them in out-of-scope-to-the-close-out is stale.

**Strain 29's tripwire now points at A4 specifically.** *"If any stage before the close-out needs to
quote or re-pin a Binance cadence figure, this closes first"* — A4 is now the only stage before the
close-out, and its known unknown 2 rests on a cadence-derived figure (639 s = 32 × 19,964 ms).
Out-of-scoping it is a decision now, not a deferral.

**One mis-attribution.** *"D-A2 left it deliberately unreachable so that an open card would be
visible on the panel"* — the deferral is D-A2's (its own out-of-scope names it), but the reasoning is
stage C's (card 28, `display_snapshot.hpp:62`).

## 4 · Verified current — do not touch these

- **`64,046 B` is the corrected figure, not the stale one.** Corrected 2026-08-27 from ~120 KB;
  eleven byte-exact BTCUSDT bodies across four traces. The residual close-out item is propagating it
  into `M5-stage-C`'s two remaining quotes, which is what the brief's out-of-scope line means.
- **`sizeof` pins hold.** `static_assert(sizeof(DisplaySnapshot) == 1168)` is in the tree, and
  ROADMAP **D6** still prices the price-axis window at +8/+24 bytes (1,168 → 1,176, 3,528 → 3,552).
- **Adoptability unchanged.** ARCHITECTURE says so in as many words: *0 of 7 adoptable at
  `limit=1000` on the liquid pair, 19 of 19 everywhere else*.
- **Three-quarters through the round trip** — card 28's positions are median ~0.76, range 0.21–0.86.
- **~11 minutes / 639 s** at the 19,964 ms ping cadence, 32 baseline intervals.
- **D-B ✅ `05e05d6`** is a real master commit (fast-forward merge, 2026-08-30).
- **Every source referent exists**: `Ink::Symbol`, `draw_header`, `reason_text`, the standing
  **VALUE > AGE > SYMBOL** priority, the `depthcharge-binance` arm, and `venue_budget.hpp` as the
  place a footprint is asserted.
- **The marker's constraints are dimensionally coherent.** The header is thirteen characters
  (`test_ladder_render.cpp:206`), the value slot is four (`"9999"`), so eight fits — and the
  assertion to copy is line 522: `text_width("9999") + kGlyphAdvance + text_width(reason_text(r)) <= kPanelWidth`.
- **Invariants #5 and #7 are numbered correctly**, D-C does have **six** named checks, and strain 28's
  D-half is *"the mechanism and the memory alone"*, exactly as the brief's DoD assumes.

## 5 · One defect found in master while auditing, not A4's

`firmware/src/venue_budget.hpp` closes its assertion note with *"What is NOT on the list is editing
`71'308`"* while the pinned constant is **`111'624`**. `71'308` was the D-A1 value; the sentence
written to stop anyone editing the number now names a number that is not there. **Close-out.**
