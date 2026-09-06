# M5 close-out — the list, and the rule this milestone earned

**Track:** Agentic [desk] · **Status:** Not started · **Size:** one desk evening, possibly two
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

- ☐ The median convention is fixed, with **the pin movement in its own commit** and every moved
      value written down before the run that printed it.
- ☐ 64,046 B propagated into `M5-stage-C`'s two quotes.
- ☐ D-C check 4's and check 2's margin disagreements resolved, **recorded where the losing figure
      was quoted**, with a ruling on whether the two numbers were ever the same quantity.
- ☐ `soak_report.py` knows the `-- reseed :` line, segments per boot, and its grammars are enrolled
      — or each exception is named with a reason.
- ☐ `sizeof(SnapshotChannel) == 3528` pinned beside its sibling.
- ☐ `venue_budget.hpp`'s host-compilability **decided** — done here, or handed on with an owner.
- ☐ Strain 29's tripwire rewritten to fire on the hazard, without invalidating D-A4's answer.
- ☐ `CLAUDE.md`'s prose-versus-ordinal line; D-A4 §6's stale D-C gate corrected in place.
- ☐ The self-review rule in `CLAUDE.md`, and a §9 row if it is architecture.
- ☐ Backlog **D8** triaged: close-out or stays backlog, with the reason.
- ☐ Per-commit verification, **naming the track for each commit** and asking both questions per file
      — does a host test compile this, does the firmware compile it. Session log · ROADMAP; push to
      `m5/closeout`; split proposed; **nothing committed until approved.**

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
