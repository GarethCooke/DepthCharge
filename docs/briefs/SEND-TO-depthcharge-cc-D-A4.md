# SEND TO DEPTHCHARGE CC — start D-A4

**The brief is the work order, it was rewritten against master on 2026-09-05 and audited, and this
note does not restate it.** Read
`docs/briefs/M5-stage-D-A4-the-reseed-mechanism-and-the-marker.md` and work from that. Build on
**`25510ec`** on `master`; branch `m5/stage-d-a4`.

`docs/briefs/M5-stage-D-A4-audit-2026-09-05.md` is the record of *why* five of the brief's figures
moved and what the previous version said. Read it if the brief surprises you; skip it otherwise. It
is not a second work order.

This note adds four things and nothing else.

## 1 · Nothing gates you, and the brief used to say otherwise

The old header told you D-C's task-watchdog aborts were *"being chased in `firmware/` by another
session"* and to rebase onto that fix. **That was inherited from D-C's brief and was never true.**
Checked 2026-09-06: no branch, no brief, nobody on it. It is now backlog **D10**, and stage E
re-ran without it and cleared D-C §1 at **27.81 h continuous**. Start when you like.

If you touch the crash at all, `0x4201c9f8` is `task_wdt_isr` at `task_wdt.c:176` — the watchdog's
own abort site, identical across all six aborts because that is where the watchdog aborts. It
identifies nothing. `c0` at **93% busy** is the symptom. **But it is D10's, not yours.**

## 2 · One thing in §4 is the owner's, and you must not decide it quietly

**The ~11 minute age no-reading window** — 639 s, 32 baseline intervals at the 19,964 ms ping
cadence. The brief is explicit: *"If it needs a rendering decision it is the owner's; this stage is
where such a decision would be implemented rather than taken."*

So: if you can satisfy the DoD by **leaving it alone and recording that** — which strain 29's
tripwire now requires you to do explicitly rather than by omission — do that and say so. **If you
conclude it needs a rendering decision, stop and ask.** D-B took all four rendering decisions and
this stage takes none.

The candidate choice — **(a)-in-PSRAM or (c)** — is yours, with the reason recorded. (b) is closed
by ruling and reopening it means reopening D-B decision 2 first, which is the owner's.

## 3 · Verification, because this stage spans both tracks

Per §9's 2026-08-30 D-B row as corrected by D-A3 the same day, and CLAUDE.md's commit-discipline
section. The test is mechanical, per commit: **does a host test include this file?**

- **ctest** (`cmake --workflow --preset host-mingw` at this desk) covers `engine/`, `harness/`,
  `tools/` and any `firmware/src` header a host test includes — sixteen are reached that way.
- **`pio run -e depthcharge-binance` in the same worktree**, with `secrets.h` copied in first,
  covers everything else. The host suite compiles **no `firmware/src/*.cpp` and no
  `platformio.ini`, ever.**
- A commit spanning both needs both. **A track you cannot run means the commit is unverified, not
  green** — say so rather than reporting a pass you did not take.
- `venue_build.hpp`'s Binance arm was compiled by nothing at all until D-A3 added a fourth test
  target, which found a `-Werror` defect on its first run. If a header you touch has no host test
  reaching it, adding one is usually cheaper than leaning on `pio`.

## 4 · The standing norms

**Nothing is committed until the owner approves the split.** Propose it with the review done and
the tree clean; per-commit verification runs as part of *executing* an approved split, not before
it. **Push the stage to `m5/stage-d-a4`. `master` is not fast-forwarded until the ladder closes.**

Run the owner's `code-review` skill against the diff before proposing a non-trivial change.

---

**Two housekeeping facts as of 2026-09-06.** `master` is at `25510ec` locally and **not pushed** —
`origin/master` is `222bdc9`. And the M5 row's *"Owed"* line still reads *"the M5 close-out list, and
D-A4."*; D-A4 is the last stage before the close-out, which is why strain 29's tripwire lands on you.
