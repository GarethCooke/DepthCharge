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
> until every commit has been shown green in isolation.**

The worktree confirmation is not ceremony. A per-commit verification that was silently building the
main tree is one of the three failures behind the mutation-verification rule in `ARCHITECTURE.md`
§9 (2026-08-18) — it passed, and it was measuring the wrong tree. Delete stale worktrees when done:
a stale one is precisely where a green build comes from the wrong source.

**Run the verification loop INLINE. `powershell -File <script>` fails at `CMakeTestCXXCompiler`** —
the compiler check dies during configure, for reasons nobody has rooted out, so a loop written as a
script and launched that way reports a red build that has nothing to do with the commit under test.
Issue the commands directly instead. Tooling rather than architecture, so no §9 row; recorded here
because it has cost an hour more than once. (This desk is configured with the **`host-mingw`**
presets — `build/host` holds a *MinGW Makefiles* cache, so `cmake --preset host` fails with a
generator mismatch. Use `--preset host-mingw` for configure and ctest.)

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
