# DepthCharge — session guide

Desk-top hardware market-data terminal: ESP32-S3 + 64×64 HUB75 panel rendering live
order books from Anvil (the owner's matching engine), Kraken, and Binance. Portable C++20
engine, host-first development, replay files as ground truth.

## Read before doing anything

1. `ARCHITECTURE.md` — the constitution. §6 invariants are **binding**; do not refactor
   through them. If work seems to require violating one, stop and raise it instead.
2. `ROADMAP.md` — find the milestone marked **Next**.
3. That milestone's brief in `docs/briefs/` — your actual work order, including its
   session log (the previous session's hand-off).
4. `docs/DESIGN.html` — how the code that exists today actually fits together: class and
   sequence diagrams drawn from source, plus §08's list of where the design is under
   strain. Orientation, not law — `ARCHITECTURE.md` still wins on any disagreement.

## Working rules

- Host first: everything proves under `harness/` (ctest) before any firmware work.
- `engine/` stays free of ESP-IDF/FreeRTOS/Arduino includes — it must build on the host.
- Integer ticks everywhere; floating point never touches book data.
- New behaviour ships with replay coverage: a trace (captured or synthesised) plus a
  golden expectation. No green, no merge.
- Build/test loop: `cmake --workflow --preset host` (configure+build+test in one; needs
  CMake ≥3.25). Established in M0 — the plain `cmake --preset host && ctest --preset host`
  from the original note skips the build step, so use the workflow preset (or run
  configure/build/test presets individually while iterating).
- Toolchain: C++20, GCC ≥13 (Windows MinGW-w64 or Ubuntu), warnings-as-errors, doctest.
- Python lives in `tools/` only.
- **A build arm `extends` the environment it is an arm OF; it never copies it.** An arm exists to
  differ from a parent in ONE stated way, and a copied `build_src_filter` or `build_flags` stops
  matching the moment the parent changes — silently, because the copy still compiles. In
  PlatformIO a child's `build_flags` *replaces* the inherited value, so the parent's must be named
  explicitly: `${env:<parent>.build_flags}`. Verify with `pio project config --json-output` that
  every other option is identical and the flag list differs by exactly the intended define — the
  point is a difference the build system enforces, not one a reader has to police. Tooling rather
  than architecture, so no §9 row; the marker such an arm must carry IS a §9 row (2026-08-29).

## Review

Before committing a non-trivial change, run the owner's `code-review` skill against the
diff. Commit messages: imperative, scoped, e.g. `engine: add FeedEvent contract types`.

## Commit discipline — when per-commit verification runs

*Verify each commit in a detached worktree* and *commit nothing* cannot both hold, and a session
told both should say so rather than fake a verification it did not run. The resolution, standing
from 2026-08-18:

> **Per-commit verification runs as part of EXECUTING an approved split, not before it.** Propose
> the split first, with the review done and nothing committed. Once approved: create the commits,
> verify each one in a detached worktree with `CMAKE_HOME_DIRECTORY` **confirmed to point at the
> worktree** before believing a pass, and amend the split if any commit is red. **Nothing is pushed
> until every commit has been shown green in isolation — and *green* means green for what the commit
> TOUCHES.** The host suite proves `engine/`, `harness/`, `tools/`, and **a `firmware/src` header if
> and only if some host test includes it** (`CMakeLists.txt:341` puts `firmware/src` on the test
> targets' include path). It compiles **no `firmware/src/*.cpp` and no `platformio.ini`, ever**.
>
> **THE COUNT OF REACHED HEADERS IS NOT RESTATED HERE, AND THAT IS DELIBERATE.** It has been written
> down wrongly twice — *"sixteen"* stood in this file and in `ARCHITECTURE.md` §9's 2026-08-30 row
> from the day that row was written, and it was already stale then. **The current figure, its
> enumeration, and the list of headers the host does NOT reach live in that §9 row.** Read it there.
> A number copied into a second file is a number that will disagree with the first.
>
> **AND THE TEST RUNS BOTH WAYS, which the one-line form of it did not say and which cost a wrong
> track assignment at M5 stage D-A4.** *Does a host test include this file?* is only half: the
> firmware compiles `engine/` headers too — `venue_build.hpp` includes the selected venue's adapter —
> so an `engine/` commit is not automatically ctest-only. **Ask both, per file: does a host test
> compile it, and does the firmware compile it? Run every track that answers yes.** Where a track
> cannot be run the commit is unverified rather than green.
> Running one track against a commit in the other is not a weak check — it is a **wave-through**, and
> it reports green.

**AND THE MECHANISM, because that last sentence is prose and prose has no reach.** At M5 stage B2
it was breached from OUTSIDE the session that held it — seven commits reached `origin/master`
thirteen minutes after the last of them was created and before their ladder closed — and nothing
failed, because a rule stated in a brief and held by one session is not a check. Same species as
the stale report line B1 named and the sentinel guard two paragraphs up: a check that depends on
the right person reading the right document at the right moment is not a check.

> **Push the stage to its own branch. Fast-forward `master` only once the ladder has closed.**

An unfinished split is then visible and pushable without being *published*, and the act that
publishes it is a separate, deliberate one. **Not a pre-push hook:** `.git/hooks` is unversioned
and absent on a fresh clone, so a hook is exactly the guard-that-depends-on-somebody-remembering
this file warns about in the paragraph above and the stale worktree it warns about in the one
below. A branch is a thing the repository can see. Tooling rather than architecture, so no §9 row.

The worktree confirmation is not ceremony. A per-commit verification that was silently building the
main tree is one of the three failures behind the mutation-verification rule in `ARCHITECTURE.md`
§9 (2026-08-18) — it passed, and it was measuring the wrong tree. Delete stale worktrees when done:
a stale one is precisely where a green build comes from the wrong source.

**A worktree cannot build `firmware/` until `secrets.h` is copied into it — and a green ctest does
not cover firmware at all.** `/firmware/include/secrets.h` is gitignored (`.gitignore:22`); only
`secrets.h.example` is committed. So a detached worktree dies at `src/main.cpp:36: fatal error:
secrets.h: No such file or directory`, which reads as a red commit and is an artefact of the tree —
the exact confusion the ladder exists to prevent, arriving from the other direction. Copy the real
file from the main tree into `<worktree>/firmware/include/` before `pio run`; removing the worktree
removes it again, so nothing leaks. **The second half is the one that bites quietly:**
`cmake --workflow --preset host-mingw` compiles no `firmware/src/*.cpp` and only those headers a host
test includes, so a green suite says *nothing* about the rest — a firmware commit is verified by
`pio run -e <arm>` **in the worktree** as well, and it is worth confirming the artefact afterwards
(`strings`, or the marker the arm exists to carry). **And where a firmware header has no host test
reaching it, the cheapest fix is usually to add one rather than to lean on `pio`:** M5 stage D-A3
found `venue_build.hpp`'s Binance arm compiled by nothing at all and closed it with a fourth test
target, which found a `-Werror` defect on its first run.

The `secrets.h` half is tooling and needs no §9 row. **The coverage half is not tooling and has
one — `ARCHITECTURE.md` §9, 2026-08-30** — because it is about what a verification is entitled to
claim, which is the same family as the mutation-verification rule this section already cites, and
because it had been silently false for twelve days rather than being a fresh discovery.

**A sentinel-token check cannot live in a file that quotes its sentinel — and the scope must be POSITIVE, not a list of exclusions.** A guard like `grep -rn SHA-PENDING` returning nothing is impossible while the instruction that *names* the token is in the tree, and the danger is not the spurious match: it is the **wave-through**, which is indistinguishable from waving through a real leftover.

Scope it to **the file the substitution targets**:

```
git grep -n SHA-PENDING -- ARCHITECTURE.md      # the only file it was ever meant to be in
```

**Not to the files that quote it.** M5 stage B2 measured why, twice over. The unscoped check came back clean only because the instruction naming the token had been rewritten the moment it was carried out — which works and depends on somebody remembering. And the *negative* scope written to replace it, `-- . ':!docs/briefs/<the-stage-brief>*'`, was **already insufficient when it was recorded**: it returned 7 hits, in the approval note that specified the rule and in this file's statement of it. The set of quoters grows every time anyone writes the token down — a brief, a review note, the rule itself — while the set of substitution targets is known when the guard is written. Tooling rather than architecture, so no §9 row.

**Run the verification loop INLINE. `powershell -File <script>` fails at `CMakeTestCXXCompiler`** —
the compiler check dies during configure, for reasons nobody has rooted out, so a loop written as a
script and launched that way reports a red build that has nothing to do with the commit under test.
Issue the commands directly instead. Tooling rather than architecture, so no §9 row; recorded here
because it has cost an hour more than once. (This desk is configured with the **`host-mingw`**
presets — `build/host` holds a *MinGW Makefiles* cache, so `cmake --preset host` fails with a
generator mismatch. Use `--preset host-mingw` for configure and ctest.)

**A split's prose must name a commit by its message, never by its ordinal.** An ordinal restates a
fact the table already owns, and it goes stale silently the moment a commit is inserted — which is
how M5 stage C's *exact next step* came to instruct an executor to build the wrong commit with the
wrong rows. A message is the same string the table holds, so there is nothing left to drift. Same
family as B2's *"the headline count did not survive its own table"*: **the fix for a fact stated
twice is to state it once, not to check it twice.** Note what this rule is NOT: the first draft of
it ended *"when the commit count changes, re-read the prose"*, and that is the species the paragraph
about sentinel guards above already refuses — *a check that depends on the right person reading the
right document at the right moment is not a check*. Proposed at M5 stage C and applied to that
brief's own prose before it was proposed, so it arrived as a rule with an instance rather than a
suggestion; adopted here at the M5 close-out. Tooling rather than architecture, so no §9 row.

**A self-review does not substitute for a failed independent one.** If the review a change is owed
does not run — the fan-out fails, an agent limit is hit, the pass is abandoned — the change waits
for it rather than proceeding on a substitute. Reviewing your own work and recording that as *the
review* is the reassuring-instrument shape in its hardest form, because the instrument's scope is
the reviewer's own attention: it reports coverage and supplies none.

**M5 stage D-A4 is the worked example, and the evidence is what happened when the review did not
run.** Every substantive defect that stage produced was found by something adversarial — the
independent review, the per-commit ladder, or the owner at the split — and none by the author
re-reading its own work. So the sharp form of the lesson is not *"review works"*. On D-A4's first
attempt the fan-out failed, an inline self-review stood in, and that pass found one real defect
**and missed a bracket/continuity conflation the same session had reintroduced at a second site
hours earlier**. Only the re-run independent review caught it. A self-review is not weaker in
proportion to effort; it is blind in exactly the places the author is, which is where the defects
are. **This IS architecture and it has a §9 row (2026-09-06)** — by the 2026-08-18 precedent, what
is wrong is not the command but the belief the command licenses, and "reviewed" is a claim about
coverage in the same way "verified" is.

## Hand-off protocol (end of every session)

1. Append to the brief's **Session log**: date · model · done · decisions **with why** ·
   exact next step.
2. If a Definition-of-done box is now true, tick it; if the milestone completed, tick it
   in `ROADMAP.md` and mark the next one **Next**.
3. Any decision with architectural weight goes to `ARCHITECTURE.md` §9, not just the log.
4. Update `docs/DESIGN.html` if the change touched anything it draws: a class member, a
   call order, a boundary, a strain point in §08, or the milestone status strip. A design
   doc that drifts is worse than none, because it is believed. Its §09 lists the triggers.
5. Leave the tree building and ctest green. A red tree must be the log's first line.

## Boundaries

- Anvil is a separate repo and is **not modified** from here; its wire contract is pinned
  at `docs/vendor/anvil-protocol.md`. Anvil-side wishes go to the ROADMAP backlog.
- Hardware track (KiCad, enclosure, bench bring-up) is owner-driven; sessions may prepare
  checklists and review artefacts but should not generate board files unasked.
