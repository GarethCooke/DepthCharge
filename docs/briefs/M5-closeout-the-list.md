# M5 close-out — the list, and the rule this milestone earned

**Track:** Agentic [desk] · **Status:** Desk work done 2026-09-06, split proposed, nothing committed
· **Size:** one desk evening, possibly two
**Written:** 2026-09-06 by the desk seat, from the items D-A4 and D-C accumulated.
**Executor:** Claude Code. **No new rendering decisions, no new mechanism.** Everything here is
already known and already written down somewhere; this stage is where the somewheres agree.

**This does NOT close M5.** M5 closes when this list is done **and** D-C's second run has been read,
because that run now carries D-A4's board box as well as its own eight. ROADMAP's M5 row reads
*"Owed: the M5 close-out list."* — this is that list.

**Read first**

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §9, the five 2026-09-06 rows | The stage that produced most of this list, and the family every item in part 3 belongs to. |
| `docs/briefs/M5-stage-D-A4-…md` §6 | Where the list was accumulated. It is the source, and part 4 corrects it. |
| `docs/briefs/M5-stage-D-C-the-soak.md` §2 | The four numbers that disagree with themselves; parts 2 and 3 are its unfinished half. |
| `CLAUDE.md`, the commit-discipline section | Part 5 edits it. Read what is there before adding to it. |

**Depends on:** nothing. **Blocks:** M5's close, jointly with D-C's second run.

---

## 1 · The median convention, and it must be its own commits

`harness/src/trace.cpp` interpolates where `engine/include/depthcharge/sample_window.hpp` says
**nearest rank**, so every Binance cadence figure this repository quotes is not the one the shipped
clock computes — 19,951.7 against 19,947.7, 20,011.6 against 19,962.8, 20,013.3 against 19,973.9.

B2 deferred this rather than fixing it **for a reason that still holds**: it rewrites figures inside
`taxonomy_pins.inc`, and *a convention change that moves pins must be its own stage so the moved pins
have nothing else in the diff to hide behind.* Honour that here — **the pin movement is its own
commit, containing nothing else**, and every moved value is written down before the run that prints
it, per the 2026-09-04 row's discipline.

## 2 · Numbers that disagree with themselves

Three, all already measured; none needs new work, only propagation and a ruling.

- **64,046 B into `M5-stage-C`'s two remaining quotes.** The correction is from 2026-08-27 and the
  source, `harness/replay/NOTES-binance.md`, already carries it. Pure propagation.
- **D-C check 4's 1.060× margin against §4.4's stated 2.29×.**
- **D-C check 2's measured 1.024× clearance against stage C's claimed 1.99×.**

For the last two, the deliverable is **which figure is right and why**, recorded where the loser was
quoted — not a quiet replacement. If a claimed margin was a projection and the measurement
supersedes it, say that; if the measurement is of a different quantity, say that instead, because
then neither number was ever wrong and the defect is the pair sharing a name.

## 3 · Instruments that do not cover what they are trusted for

The §9 family, four instances added by D-A4 alone. Each of these is a live one.

- **`tools/soak_report.py` does not own the `-- reseed :` line**, so it passes its own pre-flight —
  *non-zero counts on every regex it owns* — while covering nothing of D-A4's reading. Teach it the
  line. Until it knows it, the reading is by hand, which D-C now says in its own deliverable.
- **`tools/soak_report.py` has no per-boot segmentation**, so a multi-boot run's grey percentages
  and watchdog-versus-socket sections mix boots. D-C's first run took seven.
- **That tool's un-enrolled grammars** — the same shape once more.
- **`sizeof(SnapshotChannel) == 3528` is pinned nowhere.** No `static_assert`, no test CHECK. Its
  sibling `sizeof(DisplaySnapshot) == 1168` is guarded by the build; this one is verified by
  inspection only, and the DoD of two stages, the D-A4 audit, stage C and ROADMAP **D6** all quote
  it. Pin it beside its sibling.
- **`firmware/src/venue_budget.hpp` is not host-compilable at all**, because it includes `panel.hpp`
  and that includes the HUB75 driver. Its two `static_assert`s are therefore checked only by
  `pio run`, and only for the arm that build selects — a commit changing the venue budget is green
  on all 52 ctest tests whatever it says. §9 names the cheapest structural fix and says it is not
  free: it needs `kReserveInternalBytes` and `panel_cost_bytes` reachable without `panel.hpp`.
  **Decide here whether the close-out does it or hands it on, and record the decision either way** —
  it has been *recorded, not done* since D-A4 and that is how it will stay unless someone rules.

## 4 · Wording that points at the wrong thing

- **Strain 29's tripwire is written against the wrong event.** It fires on *quoting* a cadence
  figure; the hazard is quoting one **from the wrong home**. Rewrite it to fire on the hazard. It
  has now been evaluated once (D-A4, correctly, did not fire) so the rewrite must not invalidate
  that answer.
- **`CLAUDE.md`'s prose-versus-ordinal line.** Named for the close-out since B2.
- **`M5-stage-D-A4-…md` §6 still says D-C's second run is *"gated on the firmware crash."*** It is
  not — the crash is unowned backlog **D10**, and stage E re-ran without it, clearing D-C §1 at
  27.81 h. Same stale gate this stage already removed from its own header; it survived in the
  out-of-scope paragraph. Correct it where it stands rather than editing it away.

## 5 · The rule M5 earned, and it belongs in `CLAUDE.md`

Every substantive defect D-A4 produced was found by something adversarial: the independent review,
the per-commit ladder, or the owner at the split. None was found by the author re-reading its own
work. The sharp form of that is not *"review works"* — it is what happened when the review did not.
The fan-out failed on its first run, an inline self-review stood in, and that pass found one real
defect **and missed a bracket/continuity conflation the same session had reintroduced at a second
site hours earlier**. Only the re-run independent review caught it.

Add to the commit-discipline section, wording to adjust but not to soften:

> **A self-review does not substitute for a failed independent one.** If the review a change is owed
> does not run — the fan-out fails, an agent limit is hit, the pass is abandoned — the change waits
> for it rather than proceeding on a substitute. Reviewing your own work and recording that as the
> review is the reassuring-instrument shape in its hardest form, because the instrument's scope is
> the reviewer's own attention: it reports coverage and supplies none. M5 stage D-A4 is the worked
> example.

**Whether this is architecture or tooling is a real question and the 2026-08-18 precedent says
architecture** — what is wrong is not the command, it is the belief the command licenses. If you
agree, it earns a §9 row as well as the `CLAUDE.md` line; if you do not, say why in the log.

## 6 · Definition of done

- ☒ The median convention is fixed, with **the pin movement in its own commit** and every moved
      value written down before the run that printed it.
- ☒ 64,046 B propagated into `M5-stage-C`'s two quotes. **Already done, on 2026-08-27, in the
      commit that made the correction** — see the log.
- ☒ D-C check 4's and check 2's margin disagreements resolved, **recorded where the losing figure
      was quoted**, with a ruling on whether the two numbers were ever the same quantity.
- ☒ `soak_report.py` knows the `-- reseed :` line, segments per boot, and its grammars are enrolled
      — or each exception is named with a reason.
- ☒ `sizeof(SnapshotChannel) == 3528` pinned beside its sibling.
- ☒ `venue_budget.hpp`'s host-compilability **decided** — done here.
- ☒ Strain 29's tripwire rewritten to fire on the hazard, without invalidating D-A4's answer.
- ☒ `CLAUDE.md`'s prose-versus-ordinal line; D-A4 §6's stale D-C gate corrected in place.
- ☒ The self-review rule in `CLAUDE.md`, and a §9 row — it **is** architecture.
- ☒ Backlog **D8** triaged: **closed here**, with the reason and with what closing it exposed.
- ☐ Per-commit verification, **naming the track for each commit** and asking both questions per file
      — does a host test compile this, does the firmware compile it. Session log · ROADMAP; push to
      `m5/closeout`; split proposed; **nothing committed until approved.** *(Split proposed and
      tracks named; the ladder runs on approval, which is the standing 2026-08-18 resolution.)*

## 7 · Out of scope

**D-C's second run** — owner-driven, and it carries D-A4's board box. **The task-watchdog crash** —
backlog **D10**, unowned. **D9's >24 h bar**, whose premise stage E refuted; D-C records it and D9
settles it. **D6**'s price-axis window and its +8/+24 bytes. **D11** — closed. And **the pipe's four
slots**, card 28's other half, which D-A4's board reading is now downstream of: if `no_slot` drops
are fixed the coverage trigger gets easier to reach, and if they regress the state goes unreachable
again. That coupling is recorded in D-C; it is not this stage's to fix.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->

### 2026-09-06 · Claude Opus 5 (1M context) · the list, done — and what the review found in it

**Green on both tracks.** ctest **52/52** on `host-mingw`; `pio run` SUCCESS on `depthcharge`,
`depthcharge-kraken` and `depthcharge-binance`, RAM unchanged at 140,088 B on the Binance arm.
**Nothing committed.** Thirteen-commit split proposed, with a track named per commit and both
questions asked per file.

**Done — the list, in the brief's own order.**

**§1 · The median convention.** `harness/src/trace.cpp` calls `depthcharge::lower_median` at both
sites, so `harness/` and `engine/` compute the median by one rule and card 29's stated expiry is
met. **Every moved value was written down before the run that printed it** and all five landed
exactly: 19,951.7 → 19,947.7, 20,011.6 → 19,962.8, 20,013.3 → 19,973.9, 20,004.8 → 19,950.6,
19,969.4 → 19,964.0. The three slices with an **odd** interval count did not move, which is the
falsifiable half of the prediction. **The pin movement is its own commit and holds nothing else.**
`test_binance_adapter.cpp`'s two-conventions case is **INVERTED**, not deleted.

**§2 · The three numbers.** **64,046 B was already propagated**, on 2026-08-27, by `479bd91` — the
commit that made the correction. Verified with `git log -S` rather than assumed, and recorded at
the two places that carried the item forward unchecked. **Check 4 and check 2 are SUPERSEDED, not
corrected, and each pair is the same quantity**: 2.29×/1.060× are both capacity over largest
observed message; 1.99×/1.024× are both `k` over worst healthy multiple. Neither earlier figure was
wrong when written; each came from a population that could not contain the tail it bounded.
Recorded where the loser was quoted. **Neither sizing nor `k` moves.**

**§3 · The instruments.** `soak_report.py` learned the `-- reseed :` line, per-boot segmentation and
gzip; four un-enrolled grammars enrolled and five exceptions named. `sizeof(SnapshotChannel)` pinned
by two mutation-verified `static_assert`s. **`venue_budget.hpp` host-compilable — done here.**
ROADMAP **D8** closed.

**§4 · The wording.** Strain 29's tripwire now fires on the hazard; D-A4's evaluation re-run against
it and unchanged. `CLAUDE.md`'s prose-versus-ordinal line adopted in stage C's own wording. D-A4
§6's stale D-C gate corrected in place.

**§5 · The rule — THE RULING, since the brief asked for one either way.** The self-review rule is
**ARCHITECTURE: `CLAUDE.md` line PLUS a §9 row.** The 2026-08-18 precedent decides it — *what is
wrong is not the command, it is the belief the command licenses* — and *"reviewed"* is a claim about
coverage in exactly the way *"verified"* is, which is why the mutation-verification rule is
architecture too. A rule about what a green result ENTITLES you to conclude belongs where the
entitlements are written down.

**The contrast is the other rule this stage adopted, and it went the other way.**
Prose-versus-ordinal is **TOOLING: `CLAUDE.md` line only, no §9 row**, and the test that separates
them is what the rule constrains. An ordinal in a split's prose is a duplicated fact in a document;
getting it wrong misdirects an executor for one stage and is repaired by deleting the duplicate.
A self-review recorded as *the* review is a claim about coverage that outlives the stage, and every
later reader inherits it. **Two rules adopted in one stage, one architecture and one tooling, is
the sharpest available statement of where that line falls.**

---

**Decisions, with why.**

1. **`venue_budget.hpp`: DO IT HERE.** §9 said the fix *"is not free"*; it was five declarations
   moved into `panel_budget.hpp`, which was already ESP-IDF-free and host-tested. The deciding
   fact is that `pio run` takes **12 seconds** on this desk, so the firmware half of the
   verification was never the obstacle the recorded-not-done clause assumed. **All three venue arms
   now compile the budget assertion under ctest**, mutation-verified: an adapter footprint ×10
   fires on Kraken and Binance and correctly not on Anvil, which still fits.
2. **All six headroom pins, not one.** The old paragraph's reason for omitting Anvil and Kraken —
   *"a pin on an arm this session never compiled"* — was right, and **this change discharged it**,
   so the arms were built and read: Anvil 103,224 on both toolchains, Kraken 94,896/94,880, Binance
   45,584/45,568. Six pins also close a silent skip the review found: with only the Binance pair, a
   lost `-DDC_VENUE=3` would have skipped both blocks and gone green with no check at all.
3. **D8 closed, and fixed by adding `note_window()` rather than by teaching the checker.** The
   terminal block is `publish_message` minus the channel; the channel's omission is explained in the
   code and `note_window`'s was not. The totals were not right without it — they were missing one
   frame's rows.
4. **The margin rulings do NOT move `k` or the frame slot.** Both are readings, the falsifier as
   worded did not fire, and §9 keeps the sizing closed. Whether the falsifier is still the right
   rule at 97.6% of threshold is ROADMAP **D12**.
5. **The review was run as a fan-out of four independent agents, not inline** — applying this
   stage's own new rule to its own commit rather than to the next one. It was worth it: see below.

---

**Found while doing it — five things the list did not contain.**

1. **The Kraken bound that made the median divergence look harmless was itself unmeasured.**
   *"The two agree to 0.1 ms at Anvil and Kraken"* is false: 0.4946 ms on
   `kraken_minagbp_d25_20260817`, 0.1040 on `kraken_btcusd_d25_20260816`, 0.0774 on the resync
   slice. The three that agree *exactly* have an **odd** interval count, where agreement is a
   definition rather than a finding. Population named as *"all four"* when six had been committed
   eight days earlier. **Third margin of the same shape in one milestone**, and the one that decided
   how urgent the other two looked.
2. **`venue_budget.hpp`'s host pin failed on its first compile, on the header's own prose** — the
   host/target gap is **16 bytes, not 8**, because `SymbolConfig::wire_symbol` is a `string_view`:
   three members holding four pointer-widths, named as two holding two.
3. **Closing D8 turned a second identity red at `0 == 20`** — `check_row_arithmetic`'s
   validated-rows equality states its precondition in a comment and enforced it nowhere.
4. **ROADMAP's M5 row carried the pre-review bracket counts** (881/881, 2 of 66) that §9's own
   D-A4 row records as wrong. Corrected to 18 of 18 and 2 of 6 over 24 bodies.
5. **The D-C bench record's median range contradicts its own threshold floor** — 39,830 implies
   19,915, which is B3's, not the 19,961 quoted. The tool's new per-boot reading confirms it.

**And the tool's first cut of boot segmentation was wrong TWICE, which is the honest part.**
Treating any tick decrease as a reboot reported **27 boots on a 7-boot capture**: the ESP log
timestamp is stamped at format time and written by whichever task formatted it, so a `rest:` line
lands 4–46 ms behind the `panel:` line it overtook. The capture caught that. **The fix — a decrease
landing under 60 s — was then wrong the other way**, and review caught *that*: it says nothing about
where the drop came from, so an out-of-order write inside the first minute forges a boundary
silently. **And the obvious guard for that, requiring the drop to come from a clock past the
ceiling, is wrong a third way** — the selfcheck said so at once, because it merges a boot that
resets within 60 s, which is exactly when per-boot reading matters most. The rule tests the SIZE of
the drop instead: 1 s, a 20× margin over the worst observed race. All four shapes are now selfcheck
cases, verbatim from the capture.

---

**WHAT THE INDEPENDENT REVIEW FOUND, recorded because this stage's own new rule is about exactly
this.** Four agents over four dimensions returned twenty-two findings, of which these were real
defects in work that had already passed a green ctest, a green `pio` on three arms, and the author's
own reading:

- the 12-versus-16-byte arithmetic in `venue_budget.hpp`'s prose **and in its `static_assert`
  message**, which is the text a maintainer reads when the pin fires;
- a `test_venue_build.cpp` case titled *not vacuous* in which four of five assertions could not
  fail, and an arm guard whose Kraken and Anvil branches were byte-identical — so a lost
  `-DDC_VENUE=2` would have substituted the Anvil adapter and passed;
- **`soak_report.py`'s CRC-clustering section, which was not boot-segmented and published a
  negative probability and a flipped verdict** on a multi-boot capture — the one section in that
  file ending in a printed conclusion, left behind while four others were segmented;
- the same file's largest-free-block section emitting 4,209 plateau lines and binning a 34.55 h run
  into the last boot's 4.45 h;
- two crashes: `int()` on a mangled `cover=` field, and `statistics.median([])` when every grey
  episode exceeds 10 s — which this board's 71% grey makes the normal case;
- the frame-pipe block printing under the checksum-clustering heading, having never opened one;
- **`-- reseed` occurs zero times in every committed capture**, so the comment justifying the whole
  change asserted something about a capture that was not true;
- `soak_report.py` could not read the committed `.gz` at all, so every write-up citing *"reproduces
  from `soak_report.py`"* cited a command that failed on the only input in the tree;
- four stale or unsourced counts, two of them introduced by this diff.

**None of these would have been found by re-reading.** Two — the `venue_budget` arithmetic and the
`soak_report` verdict — are numbers that would have been quoted forward. The rule §5 adds is not a
hypothesis; it is what this evening measured.

**Exact next step.** **Approve or amend the thirteen-commit split**, then execute it: create the
commits, verify each in a detached worktree with `CMAKE_HOME_DIRECTORY` confirmed to point at the
worktree, copy `secrets.h` in before any `pio run`, and run **every track that answers yes** per
commit. Push to **`m5/closeout`**; fast-forward `master` only once the ladder has closed. **M5 does
not close here** — it closes when D-C's second run has been read, which carries D-A4's board box as
check 7, and `soak_report.py` can now read that line for it.

### 2026-09-07 · Claude Opus 5 (1M context) · the gzip retrospective, scoped

**The finding was retrospective and had not been scoped. It is now.** `tools/soak_report.py` read
its input with a plain `open(path,'rb')` and **all five committed captures are `.gz`**, so it could
not process any of them. Measured rather than inferred: the pre-change tool, checked out from
`HEAD`, was run against all five and **failed 5 of 5** with the identical line —
*"NO SOAK LINES PARSED — the grammar has drifted from the firmware"*, a grammar verdict on a
compression format.

**The distinction the deliverable asked for: every figure holds, and every stated provenance was
unrunnable.** Re-run against the committed artefacts, nothing measured moves.

| capture (committed) | tool at HEAD | tool now | the claims that cite it |
| --- | --- | --- | --- |
| `bench-2026-08-22-kraken-b3-soak.log.gz` | ✗ exit 1 | ✓ 1 boot, 25.39 h, 9,051 SOAK, CRC ratio **8.36×** | **HOLD.** M4 stage D's *"33,481,892 B, 259,195 lines"* and the bench record's *"every figure below is emitted by `soak_report.py` … none is hand-read"* both reproduce exactly. |
| `bench-2026-08-30-D-B-silent-stream-054547.log.gz` | ✗ exit 1 | ✓ 1 boot, 33 SOAK | no numeric reproduction claim; processes cleanly. |
| `bench-2026-08-30-D-B-silent-stream-055239.log.gz` | ✗ exit 1 | ✓ 1 boot, 48 SOAK | as above. |
| `bench-2026-08-30-D-C-soak.log.gz` | ✗ exit 1 | ✓ **7 boots, 34.55 h, 6,183 intervals, `oversize` 1 in B1, `max_held` 4 of 4 ×7, 1,188,879 published** | **HOLD**, and they now reproduce by machine what D-C derived by hand. |
| `bench-2026-09-04-E-soak.log.gz` | ✗ exit 1 | ✓ 2 boots, 32.25 h, 65,658,299 B inflated | **HOLD on provenance — and one figure MOVES.** See below. |

**The sha256 half is repaired rather than merely reachable.** The bench records pin the **inflated**
digest and `git` stores the compressed one, so the tool now prints both. All three pinned digests
reproduce byte-exactly from the committed gzip: `6a9139f6…fa8` (kraken-b3, and M4 stage D),
`d4c4fd12…` (D-C), `eb638db1…` (stage E). **So no artefact is in doubt; what was in doubt was
whether anyone could check.**

**One capture the tool still cannot process, and it is a path rather than a format.** The tool's own
usage line and M4 stage D both name `firmware/logs/FROZEN-kraken-b3-soak-20260822.log`, which is
**not committed** (`git ls-files firmware/logs/` is empty). On a fresh clone that command fails for
want of the file. The figures are unaffected — the committed `.gz` inflates to that exact file,
`6a9139f6…fa8`, byte for byte — so the right repair is to cite the artefact that exists, which the
D-C record now does.

#### And what the retrospective actually found: D-C's check 2 falsifier has FIRED

`bench-2026-09-04-E-soak.log.gz`, boot 2 — the 27.81 h continuous connection — ends at

```
-- signal : server-ping n=4977 max=53163 ms >=2x med=4 | median 19989 ms threshold 39979 ms CALIBRATED
```

**Four intervals reached 2 × the settled median on a socket that never dropped.** 2 × 19,989 =
39,978 ms and the `>=40s` bucket admits nothing below 40,000, so all four clear the bar stage C's
rule sets.

**Stage E recorded the count, doubted it, and handed it here — correctly.** Its §7 wondered whether
the four were *"intervals that legitimately greyed rather than healthy ones that nearly did"*,
because the run also has `wd=4`. **They are disjoint sets, and the firmware guarantees it:**
`LivenessWatchdog::note_fired` sets `armed_ = false` and `on_liveness` adds to the histogram only
`if (armed_)`, so an interval that caused a firing cannot appear in that bucket — the gate's own
comment says that ungated *"the falsifier would read as permanently tripped"*. The tick stamps
settle it beyond argument:

```
>=40s histogram entries   368,417   418,534   659,082   709,183
watchdog firings          187,810   247,946   318,112   769,140
```

Not one coincides. **Two counters both reading 4, read as one set of four events.**

**Nothing greyed, and the multiplier is not why.** All four fell in the connection's first ~12
minutes, while `kMinSamples = 8` had not calibrated and the threshold was clamped at the
**60,000 ms** uncalibrated ceiling — above the 53,163 ms worst. Calibrated at 39,979 ms, an
interval that size greys a healthy socket. **The uncalibrated window D-C's check 5 treats as a cost
is the only thing that stood between this run and a false grey.**

**`k` is not moved here**, on stage E's and D-C's own principle — a stage that did not run the soak
may not move a threshold on it. ROADMAP **D12** is rewritten from *"is the falsifier still the right
rule?"* to *"the rule has fired and prescribes its own remedy"*, with both runs' numbers and the
ceiling arithmetic (`k ≤ 3.005`), for the owner.

**Why this is its own commit and not a line here:** a figure moved. `>=2x med` for the corpus goes
from 0 to 4, and four documents asserted the first.

#### The species, and it is a fourth one

Everything else this stage found was an instrument whose scope did not intersect its subject. This
is the same failure through the other door: **the scope was right and the input was unreachable.**
Stage E did everything correctly — read the number, declined to rule, named this stage as owner —
and the finding still did not arrive, because the close-out read the brief's list rather than the
tree, and the list did not carry it.

**Two of the ten items on that list were not open either.** 64,046 B was propagated on 2026-08-27
by the commit that made the correction, and `venue_budget.hpp`'s fix was five declarations moved
against a §9 clause calling it *"not free"* — a cost never measured, when `pio run` takes 12 s on
this desk. Both were carried forward as open by documents citing each other rather than the tree.

**So the list is the instrument, and it was never re-checked against its subject.** Three entries
wrong out of ten: two closed items carried as open, one open item — the fired falsifier — missing
altogether. That is the same family as everything else in §9, one step further out: not an
instrument that cannot see its subject, but **a list that never re-checked its own entries**. The
cheap guard is the one this project keeps arriving at: a to-do that names the evidence rather than
the conclusion can be re-derived; one that names only the conclusion can only be believed.
