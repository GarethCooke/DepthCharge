# SEND TO DEPTHCHARGE CC — M5 stage 0 reviewed; three fixes and one softening before the split

Stage 0 is accepted. The oracle result is the right one, the `limit=100` catch is the evening's
justification on its own, and the four §9 rows are the right four. Four things, then commit.

## 1 · The 150/151, which the next sentence contradicts

`harness/replay/NOTES-binance.md:20` reports the coincidence as 899/899 on the deep-seed capture
**"and on 150 of 151 (99.3%) in the shorter one"** — and the sentence immediately following says
*"Not 'sometimes', not 'usually' … exact at every tick"*, with the *unverifiable* bucket **"empty in
every clean capture."** One payload did not coincide. Nothing in the file chases it, `99.3` appears
exactly once — in the clause that overrides it — and the §9 row has already carried the stronger
claim into the constitution.

**Find the one.** Print its `lastUpdateId`, the diff events either side of it, and its position in
the file. Then one of two things is true and both are acceptable answers:

- **It is a capture-edge artefact** — the first partial arriving before any diff event, or the tail
  cut mid-tick. Then name it, and the exactness claim survives with a stated boundary condition
  instead of a footnote it contradicts.
- **It is not** — the two streams published from different update boundaries at least once. Then
  the claim is an observation with an unexplained exception; `unverifiable` stays a **live** bucket
  rather than being declared permanently empty; and the §9 row, the ROADMAP line and the notes all
  take the weaker sentence.

**Do not resolve it by dropping the shorter capture from the claim.** An instrument that reports
100% once the disagreeing input has been removed is measuring the input set, not the venue — which
is this project's own §9 material, twice over.

## 2 · The two new `ctest` targets skip silently

`CMakeLists.txt` wraps `binance_tool_selfcheck` and `binance_oracle_mutants` in
`if(DC_BINANCE_TRACES)` with no `else()` — twenty lines above an `else()` that *does* announce the
Python-missing skip. `file(GLOB)` also resolves at configure time, so a fresh clone, a stale cache
or a renamed trace drops both tests and the suite still reports green at a number that will read as
35/35 to whoever quotes it.

The rule this repo already holds, from the Kraken pin table: **an unpinned trace is a failure, not a
skip.** Same class. Either name the five slices explicitly so a missing one fails the configure, or
keep the glob and add an `else()` that reports the skip as loudly as the Python one does. Prefer the
first — the file list is part of what is being pinned.

## 3 · The oracle has exactly one witness

Four of five slices are RED for a real reason, so `binance_oracle_mutants` runs against `deepseed`
alone. The exactness claim, the 884/884 and the whole mutant reach rest on **one capture, one pair,
one day** — and the tool that measures the coincidence is the tool whose correctness that
coincidence is being used to establish.

**Take one more capture: ATOMEUR, `limit=1000`, deep-seeded, same recipe.** One 90-second window
buys three things — a second independent witness for the exactness claim; a gradeable trace on a
*quiet* pair, which is where a boundary mismatch is most likely to show and therefore probably where
item 1's answer lives; and a second slice for `--check`, so the mutant suite stops being
single-sourced. Add it by the add-rows-only procedure, in order: **mutants, then `--selfcheck`, then
`--pin`.**

## 4 · One §9 sentence to soften, and the correction is in your favour

The oracle row calls *blind below rank 20 against a 25-row panel* **"the inverse of Kraken's
CRC-10-under-a-25-row-panel but the same class of gap."** It is not the inverse. It is the same gap
and it is **smaller**: Kraken validates 10 of the 25 rows drawn, Binance validates 20 of 25. Put
both numbers in the row, so a later session reading it does not open an evening to close a hole
already narrower than the one it has lived with since M4.

## Then

Commit split as you propose it — **but the CRLF conversion has to go first.** Every tracked text
file is currently modified with equal insertions and deletions (`ROADMAP.md` alone: 726 lines,
594/594), so no per-commit diff is reviewable and the commit-per-claim discipline cannot be
enforced. `git checkout -- .` on the tracked docs, or a deliberate `.gitattributes` decision if CRLF
is to be the tree's answer. The three commits since M4's close are all LF.
