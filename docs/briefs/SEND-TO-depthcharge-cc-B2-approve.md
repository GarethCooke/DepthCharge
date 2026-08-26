# SEND TO DEPTHCHARGE CC — B2 approved, one fix and three records before you commit

The seven-commit split is approved as proposed, in that order, with commit 2 carrying commit 1's
SHA. The `min_bid_levels` → seeded-coverage correction is accepted and is the evening's headline:
an instrument that only ever grows cannot detect the failure class it was built to watch, and
reading a flat 100/100 through a book going wrong is the same shape as `U`/`u` scoring 890/890
clean at 82.4%. Keeping both counters one line apart is right.

`trace.cpp` recorded rather than fixed is the right call. **The reason you gave is not the
load-bearing one** — see §1.

One fix, three records, then commit.

## 0 · The `SHA-PENDING` check cannot pass — fix it before you run the loop

`grep -rn SHA-PENDING` returning nothing is impossible: this stage's own brief carries the literal
token at lines 384–385, in the instruction telling commit 2 to substitute it. **The danger is not
the spurious match — it is the wave-through**, which is indistinguishable from waving through a
real leftover. Scope it:

```
git grep -n SHA-PENDING -- . ':!docs/briefs/M5-stage-B2*'
```

The general form is worth recording in `CLAUDE.md` beside the `powershell -File` note, since it is
tooling rather than architecture and therefore takes no §9 row:

> **A sentinel-token check cannot live in a file that quotes its sentinel.** Scope the search to
> the files that can carry the token, or the guard reports a hit for ever and gets waved through.

## 1 · `trace.cpp` — record the right reason, and give it an owner

Two median conventions: `age_estimator.hpp` uses `lower_median`, which `sample_window.hpp` owns;
`trace.cpp:632-638` interpolates. So every Binance cadence figure this project has quoted —
`taxonomy_pins.inc`'s 19,951.7 / 20,011.6 / 20,013.3 and B1's 20,004.8 — is not the cadence the
shipped clock computes, and Anvil and Kraken agree to 0.1 ms, which is why it survived three
milestones. **The coincidence class again, in a third place.**

**Deferring is correct, and not because the sweep is large.** Record this reason instead:

> Fixing the convention **moves `taxonomy_pins.inc`**, and *no existing golden moves* is precisely
> what makes a seven-commit split reviewable. Bundled here, nothing in the diff would distinguish a
> convention change from a defect. **A convention change that moves pins must be its own stage, so
> that the moved pins have nothing else in the diff to hide behind.**

Then give it the owner/expiry/tripwire shape, the same one B1 §3 used for the host/target age
divergence — *"its own scope" is unowned, and commit 2 of this very split exists to close an
unowned clause in §9. Do not open a second one in the session that closes the first.*

- **Owner: M5 close-out.** Not C — C's evening is already the threshold, the ceiling's changed
  role, the panel decisions and the four unbuilt remedies, and a sweep across three NOTES files and
  a dozen briefs is how C runs long. *Owner's call; override this line if you disagree, but do not
  leave it blank.*
- **Expiry:** when `harness/` and `engine/` compute the median by one convention.
- **Tripwire:** if any stage before the close-out needs to quote or re-pin a Binance cadence
  figure, this closes first.
- **Carried in three places**, the shape the DEFECT fixture already uses: the inverting test, the
  pin/taxonomy row, and DESIGN §08 — each stating that **the correct response is to INVERT the
  test, not to delete or relax it**, and that if the close-out ships without inverting it the
  clause moves to whichever stage next touches a cadence figure rather than lapsing.

## 2 · One §9 row — the sample-counted-constant class, and the instance you have not named

Every liveness constant in this project is expressed in **samples**. A sample count is a per-venue
**duration**, and at a cadence 40× slower than Anvil's they all inflate 40×:

| constant | value | Anvil (~0.5 s) | Kraken (~1.0 s) | Binance (19.97 s) | found at |
| --- | ---: | ---: | ---: | ---: | --- |
| `kMinSamples` | 8 | ~4 s | ~8 s | **~160 s** | M5 stage A |
| `kBaselineSamples` (= `kWindowSamples`) | 32 | 16 s | 32 s | **639.0 s** | M5 stage B2 |
| `kAgeWindowSamples` | 256 | ~128 s | ~255 s | **~85 min** | **not yet named** |

Your 639.0 s is exact by construction — the latch fires at `arrivals_.size() > 32`, so 32 × 19.97 —
and the same constant produces the 16 s and 32 s the header already states. **The third falls out
of grepping for the first two**, and its own comment says why the number is what it is:
*"256 × 8 B = 2 KiB, which is the whole reason it is not larger."* **A memory budget, never a time
span.** That is the window the age supremum is taken over, and nobody has sized it at a venue whose
cadence is forty times Anvil's. Hand it to C with the rest of the age question; do not decide it.

Write the **class**, not a third instance:

> **A CONSTANT EXPRESSED IN SAMPLES IS A PER-VENUE DURATION.** Three liveness constants are sized
> in arrivals, all chosen against Anvil's 500 ms broadcast — one for statistical sufficiency, one
> from a measurement, one from a 2 KiB memory budget — and at 19.97 s each becomes a duration
> nobody sized: 160 s to calibrate, 639 s before the age meter reads at all, ~85 min of supremum
> window. Two were found a milestone apart by different stages looking at different things; the
> third was found by grepping for the first two. **The rule: a sample count in a venue-agnostic
> object must state its duration at every venue the object serves, or the next venue rediscovers
> it one constant at a time.**

And one line of this is **yours, not C's**: `age_estimator.hpp:262` reads *"the meter reads `-` for
the first 16 s of an Anvil connection and 32 s of a Kraken one"* — a per-venue cost sentence
written before the venue where it is 639 s existed. That is the stale-line shape B1 named twice,
sitting inside the file this stage's own finding is about. **Propose it as an eighth commit** —
`engine: name the venue the age meter's no-reading window was never sized for` — rather than
folding a comment fix into commit 4, so 4's diff stays the trigger and nothing else.

## 3 · One §9 row — group the three ways a green suite is wrong

`Approx(19969.4).epsilon(0.001)` is **relative**: ±20 ms on 19,969, which spans both candidate
medians. The test could not discriminate the two conventions, so it passed while the figure was
wrong. That is the third member of a family this log already has two names for, and it should be
grouped rather than filed as a third unrelated instance:

1. **the coincidence class** — two implementations agree because no committed input can tell them
   apart (2026-08-18, from B1's review; and again above);
2. **a never-observed frame kind** — a corpus that has never contained the input (M4 stage B2);
3. **a tolerance spanning the candidates** — a bound wide enough to admit both answers pins
   neither.

All three are *green suite, wrong thing*, and all three are invisible to the normal build loop.
Reference the existing rows rather than restating them.

---

## Constraints

- **No new behaviour.** §0 is a check fix; §§1–3 are records plus the split you already proposed.
- The eighth commit, if taken, is **one comment line**; no code path moves.
- **Nothing written for §§1–3 may move a pin.** If drafting §2's row tempts a figure correction in
  `taxonomy_pins.inc`, that is exactly what §1 defers — stop and leave it.
- `test_binance_adapter.cpp` is touched by 3 and 4: partial-file staging, as you proposed.
- `cmake --workflow --preset host-mingw` green at every commit, from a **fresh** detached worktree
  with `CMAKE_HOME_DIRECTORY` confirmed and normalised for separator and case, loop run inline.
- **Push nothing** until every commit has been shown green in isolation and the scoped
  `SHA-PENDING` check is clean.

## Definition of done

- [ ] `SHA-PENDING` check scoped and shown clean; the sentinel rule recorded in `CLAUDE.md`.
- [ ] `trace.cpp` deferral recorded with the **golden-movement** reason, an owner, an expiry and a
      tripwire, carried in three places with the invert-not-delete wording.
- [ ] Sample-counted-constant **class** written as one §9 row, all three instances tabulated with
      their per-venue durations, and `kAgeWindowSamples` handed to C undecided.
- [ ] `age_estimator.hpp:262`'s per-venue cost sentence corrected — eighth commit, one line.
- [ ] Green-suite family grouped in one §9 row, referencing the existing two rows.
- [ ] Seven (or eight) commits created, each verified green in isolation, ladder of results
      reported, **nothing pushed**.
