# SEND TO DEPTHCHARGE CC — B1 approved, four records before you commit

The eight-commit split is approved as proposed. Commit 3 stands — the brief's `engine/` constraint
was self-contradictory and your reading is the one under which the deliverable is expressible.
The Part A/B cut is accepted and extended below.

Four records, then commit.

## 1 · One more §9 row — the coincidence class

Deliverable 0's row covers a check that cannot go red. Tonight found the other half twice, and it
is a different failure:

> **A check that can go red still proves nothing if every available input makes the wrong answer
> coincide with the right one.** `adopt_snapshot` truncated nothing and five slices were green,
> because Kraken serves exactly the depth requested and our cap coincided with its cap on every
> committed file. The use-after-move passed direct construction and failed only through
> `run_replay`, because the test built the object differently from the driver. In both cases the
> test and the world agreed by accident.
>
> **Captured traces cannot cover the coincidence class**, because a trace records a venue
> behaving. A synthetic frame is not a weaker substitute for a golden here — it is the only
> instrument that reaches the case. Where an adapter's assumption and a venue's behaviour could
> coincide, the synthetic case is mandatory and the golden is decoration.

Name both defects and both live routes to the first one — a capture with no depth in metadata, and
`ack_depth_mismatch`, which exists because Anvil rounds depth up.

## 2 · Item 8 moves to D's opening, not to a B1b evening

Record the reassignment with the reasoning, so it does not read later as something that was
dropped:

- Rewiring the RX watchdog onto the calibrated threshold changes **when the panel greys**, and the
  behaviour it replaces was established over M3's 23.6-hour soak. The desk can compile it and
  cannot show it correct.
- A partial lift is worse than none: clocks in `engine/` while firmware still includes
  `staleness.hpp` is a third copy of the thing deletion exists to remove.
- D is already the bench stage and its soak is the same soak. Item 8 becomes **D's first act**,
  before any panel rendering is judged, and the M4 evening count drops from five to four.

## 3 · The divergence gets an owner and an expiry

Across B2 and C, host and target compute the book's age by different code, invisible to every host
test. It is **latent, not active** — nothing on the target reads Kraken until D. Record it in
`DESIGN.html` §08 as a live residue with:

- the owner: D's first act;
- the expiry: the moment the target runs a Kraken build, whichever stage that turns out to be;
- the tripwire: if anything before D proposes flashing a Kraken build, this closes first.

## 4 · The commit-verification contradiction, resolved as standing practice

*Verify each commit in a detached worktree* and *commit nothing* cannot both hold, and you were
right to say so rather than fake it. The resolution, for this and every future stage:

> Per-commit verification runs **as part of executing an approved split**, not before it. Create
> the commits, verify each in a detached worktree with `CMAKE_HOME_DIRECTORY` confirmed to point at
> the worktree, and amend the split if any commit is red. Nothing is pushed until every commit has
> been shown green in isolation.

So: **create the eight commits, verify each, report the ladder of results, push nothing.**

---

## Constraints

- No new code. These are records plus the split you already proposed.
- Commit 3 carries the `test_replay_goldens.cpp` counter rename or the build breaks; commit 4
  defines the library that nothing links until 5 — both already noted, both fine.
- `cmake --workflow --preset host-mingw` green at every commit, from the worktree.
- **Push nothing.**

## Definition of done

- [ ] Coincidence-class §9 row written, both defects and both live routes named.
- [ ] Item 8 recorded as D's first act with its reasoning; ROADMAP and the triage brief updated to
      four remaining evenings.
- [ ] Host/target age divergence recorded in DESIGN §08 with owner, expiry and tripwire.
- [ ] Verification practice recorded wherever the commit discipline lives.
- [ ] Eight commits created, each verified green in isolation, ladder of results reported, nothing
      pushed.
