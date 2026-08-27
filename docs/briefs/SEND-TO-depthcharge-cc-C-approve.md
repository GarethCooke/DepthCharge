# SEND TO DEPTHCHARGE CC — C approved, one fix and two records before you commit

The eight-commit split is approved as proposed, in that order. Both deviations are signed off.
**Remedy (b)'s rejection is the evening's best finding after the threshold itself** — a remedy the
strain card asserted *"self-terminates at the threshold"*, refuted on the mechanism rather than on a
preference, is worth more than a remedy built. Note where the refutation came from: `armed_` is
**M4's** card 26, the older of the two cards sharing that number. The duplicate is not only a
citation defect; the two cards interact.

**Deviation 1 is a correction to the brief, not a deviation from it.** The brief said *"restore both
to LF in one commit of its own"*, which specified a commit that cannot exist — the blobs were
always LF, so the repair has no diff. You were right to notice and right about the replacement: the
guard is the artefact, positively scoped, directory-enumerated, with `checked >= 22` as a lower
bound so an empty sweep cannot wave through. That is the sentinel-scope rule applied rather than
cited.

**Deviation 2 is the process working.** Record the first defect's species while it is in front of
you: `check_continuity` setting `bracket_checked_` before testing made two paths disagree about one
event, and only one input could tell them apart. **That is the coincidence class, third instance** —
§9's *three ways a green suite is wrong* row already has the home; add the reference, not a new row.

One fix, two records, then ladder.

## 0 · The split's prose does not survive its own table — fix before you ladder

*Exact next step* says:

> commits **2 and 3** below are one file's edits split in two, so the ladder must build **commit 2**
> with Binance's row still `{}`

The table says commit 2 is the CR-byte guard, commit 3 is `LivenessPolicy` with **all three rows
`{}`**, and commit 4 is Binance's two values. **The pair is 3 and 4, and the commit that must build
with the rows still `{}` is 3.** Off by one in both places, and the likely mechanism is that
`docs: the stage C brief` was added as commit 1 after the paragraph was written.

It matters because that paragraph is the instruction an executor follows to decide what to build,
and following it literally destroys the attribution the split exists to create. It is also the same
species as B2's *"the headline count did not survive its own table"* — **arriving one stage later in
the same document family, in the sentence written to prevent the confusion.** Nothing failed and
nothing could: no test reads a split table, and the prose and the rows are checkable against each
other only by someone doing it.

The fix lands in **commit 8**, which carries the session log the table sits inside — but make the
edit before you create commit 1, so no commit is built from the wrong reading.

**Second instance, so propose rather than assert:** a `CLAUDE.md` line beside the sentinel and
`powershell -File` notes — *a split's prose and its table are two statements of one fact, and
nothing checks them against each other; when the commit count changes, re-read the prose.* B2's
preflight established that the discriminator for §9 is whether the class is **architectural**, and
this is not — it is tooling. It also established that the discriminator is **not** *three
instances*. Owner's call whether two earns a line; decline it cleanly if you disagree.

## 1 · The multiplier is derived from a sample that cannot contain the event it must survive

Accepted as the shipping value. **Not accepted as a closed question**, and the reason is in your own
table.

| venue | binding case | intervals | k | margin over the binding case |
| --- | --- | ---: | ---: | ---: |
| Anvil | **one missed tick**, 968.8 ms | 1,191 | 4.0 | **2.06×** |
| Kraken | jitter, 1.119× | 834 | 4.0 | 3.57× |
| **Binance** | **jitter, 1.005×** | **10** | **2.0** | **1.000×** |

`M4-stage-A-ruling-and-rederivation.md:351` says it outright — *"Anvil is the binding case and its
1.937× is ONE MISSED TICK."* And the **same Anvil signal** read **1.094×** over 62 idle frames
(nominal 500, worst 547) and **1.937×** over 1,191 intervals. The tail is the missed tick, and a
short window cannot contain one. **Ten Binance intervals cannot contain one either**, so the
derivation only ever saw jitter — which is why *"same rule, third venue"* does not transfer: at
Anvil the rule was applied to a dropped signal, here to its absence.

The arithmetic is exact rather than approximate:

- one missed ping = 2 × 19,963.97 = **39,927.94 ms**
- the new threshold = 2.0 × 19,963.97 = **39,927.94 ms**
- `LivenessWatchdog::expired()` is `armed_ && now >= deadline_ns()`

**A single dropped ping greys the panel, with margin 1.000×** — the event Anvil deliberately kept
2.06× of room for.

**Do not change k.** 2.0 is derived, defensible, and D's soak is where a threshold gets tested in
this project — B3's 25.39 h is the precedent. What is missing is the falsifier, and the useful half
is that **the ceiling you already chose accommodates the fix**: k = 3.0 gives 59,891.91 ms and
k ≤ 3.005 fits under 60,000. If the soak finds a dropped ping, **k rises alone** — no ceiling change,
no second attribution problem, one value in one row.

Record, in the NOTES §C multiplier table and on the §9 per-venue-policy row:

> **k = 2.0 tolerates this signal's jitter and not a dropped ping.** It is derived from ten
> intervals spanning 111 ms, a window too short to contain the missed-tick event that was the
> binding case at the one venue observed long enough to show one — where the same signal read
> 1.094× over 62 samples and 1.937× over 1,191. One missed Binance ping equals the threshold
> exactly. **Falsifier: D's soak records the ping-interval distribution; any interval reaching
> 2 × median on a healthy socket raises k.** The 60,000 ms ceiling already admits k ≤ 3.005, so the
> remedy is one value and moves nothing else.

## 2 · There is no *Owed by stage D* section

B2 wrote *Owed by stage C* at its close-out **because its absence had already cost it**, and opened
it with the reason: *a list that has to be reconstructed by grepping is a list that arrives short.*
C inherited the diagnosis and not the practice — and §1 above is the proof, since the multiplier's
evidence base is stated honestly in a table and handed to nobody.

Write it, same two-column shape (*what D owns* · *where the evidence is*), in commit 8. It must
carry at least:

1. **The four panel questions C deliberately did not take** — the silent feed, the re-seed in
   flight, whether (a)'s grey reads right, strain 24's unvalidated rows. Cite this brief's scoping
   ruling as why they are D's, so D does not re-argue it.
2. **The re-seed mechanism and its memory** — strain 28's D-half, the three candidates, and B2's
   adoptability measurement unchanged: **0 of 7 at `limit=1000` on the liquid pair, 19 of 19
   everywhere else.**
3. **§1's multiplier bound, with its falsifier**, as the soak's first named check.
4. **The uncalibrated-default window** — 30 s → 60 s for 159.7 s on every connection, decoupling
   deliberately not done here because it would be a fourth number with no measurement behind it.
   State whether the window matters on the board; that is a soak observation, not a desk one.
5. **Parity: NO**, with the reduced claim — and D is where the reduced claim is tested rather than
   asserted.
6. **M4's card 26 as a live dependency.** Remedy (b) is not wrong in principle; it is **blocked by
   an open M4 card**. If `armed_` ever gains a second setter, (b) becomes available and its
   trade against (a) reopens. Write that down or the next stage re-derives the rejection from
   scratch — which is exactly what this section exists to prevent.

**Card 26 → 30: approved, move the M4 card.** And say in the row that this **follows** card 23's
precedent rather than departing from it: the precedent's reason is fewest broken citations, and at
22→23 *newer moves* and *fewest edits* pointed the same way, so the rule was written from a case
that could not distinguish them. Here they diverge, 14 of 15 references belong to the M5 card, and
the reason governs the letter. Same shape as B2's *two questions had been run together*.

**Strain 29's tripwire: agreed, reads as not firing.** Your diagnosis is right — the hazard was
never *quoting a cadence figure*, it was quoting one from the wrong home — and the 10.8 ms / 0.027%
assertion settles that this stage's decision is convention-independent either way. Wording to the
close-out, as you proposed.

---

## Constraints

- **No new behaviour.** §0 is a prose fix; §§1–2 are records plus the split you already proposed.
- **Nothing here may move a threshold or a pin.** If writing §1's bound tempts you to raise k, stop
  — that is D's, on the soak's evidence, and taking it here would change the constant and the venue
  in one step, which is the trap the §9 row you just wrote exists to name.
- §§1–2 land in **commit 8**. Not a ninth: they are the writeback class commit 8 already carries.
- The brief now holds both the work order and the session log, so **commit 1 and commit 8 stage the
  same file in parts** — partial-file staging, as at B2.
- `cmake --workflow --preset host-mingw` green at every commit, from a **fresh** detached worktree
  with `CMAKE_HOME_DIRECTORY` confirmed and normalised for separator and case, loop run **inline**.
- Push to **`m5/stage-c`**; fast-forward `master` **only after the ladder has closed.**

## Definition of done

- [ ] *Exact next step* corrected to name **commits 3 and 4**, and commit **3** as the one built
      with all three rows `{}`; edit made before commit 1 is created.
- [ ] The prose-versus-table line proposed for `CLAUDE.md`, or declined with a reason.
- [ ] The `bracket_checked_` defect referenced against §9's existing green-suite row — reference,
      not a new row.
- [ ] §1's bound recorded in the NOTES §C multiplier table **and** on the §9 per-venue-policy row,
      with the falsifier and the k ≤ 3.005 headroom stated.
- [ ] **Owed by stage D** written, carrying all six items above.
- [ ] Card 26 → 30 executed, with the follows-the-reason-not-the-letter sentence in the row.
- [ ] Eight commits created, each verified green in isolation from a fresh worktree, ladder of
      results reported, pushed to `m5/stage-c`, `master` fast-forwarded only after.
