# M5 Stage C — what a green liveness clock is entitled to mean

**Track:** Agentic [A] · **Status:** Not started · **Size:** one evening
**Executor:** Claude Code, **desk only. No board, no flash, no `firmware/` change, no panel judgement.**

B1 found a socket that answers pings and delivers nothing for ever. B2 measured what the age meter
can and cannot see through it, and captured the first trace in this project's history that
calibrates the liveness clock at all. What is left is not a measurement. **Every number this stage
needs is already committed** — C decides, C does not re-measure.

The venue's ping is emitted below the subscription, so it proves the socket and never the feed. The
2026-08-17 ruling armed the grey on a liveness signal because at Anvil and Kraken the same subsystem
emitted the ladder and the heartbeat; here it does not. Decision 1 stands — greying on socket death
alone lost by 141 s and 291 s at M4 stage D's B3. **What changes is what a green clock is entitled to
mean**, and this evening is where that gets written down as behaviour rather than as a strain card.

---

## The scoping ruling, taken before this brief was written

B2's *Owed by stage C* section closed by warning that C could not be briefed until someone settled
which of its items a desk can close — *"or C is written as one evening and discovers halfway through
that half of it cannot be finished at a desk."* That question is settled here, and the answer comes
from M4 twice rather than from judgement:

- **M4 triage, 2026-08-17:** *the uninitialised-book question splits — the engine state is C's, the
  rendering is D's, because the two candidate renderings differ in nothing a host test can assert.*
- **M4 stage D, item A1:** the brief said no host test could check the rewire, and that was *"true of
  the BEHAVIOUR and not of the POLICY"* — so the rule went into `liveness_watchdog.hpp` and was host
  tested, and only the behaviour waited for the bench.

**So: C is an engine-and-policy evening, and every question of the form *what does the panel look
like* is D's, without exception.** That is a narrower C than the inheritance table implies and a
wider D, and it is the only split under which C is one evening. Applied to the eight items:

| # | Item | Verdict |
| --- | --- | --- |
| 1 | The threshold multiplier | **C.** B2's 221 s calibration trace is the first artefact that enters the calibrated branch. The policy is host-assertable today. |
| 2 | `kThresholdCeilingMs`'s changed role | **C**, with 1, and coupled to 3 — see deliverable 2. |
| 3 | The four unbuilt remedies | **Splits.** C picks the mechanism and proves it against the defect fixture; whether the resulting grey *reads* right is D's. |
| 4 | What the panel renders on a silent feed | **D.** Rendering. M4's precedent is exact and this stage does not touch it. |
| 5 | The transport-versus-feed hole | **C** — as a written limit, not a fix. It has none. |
| 6 · 7 · 8 | `age_ms` blind to a feed backlog · the 638.8 s no-reading window · whether ~85 min is defensible | **C**, on B2's own measurements. |

**Item 4 leaving C is the whole reason C fits in an evening.** It is also why deliverable 3 exists:
a rendering decision deferred to D still needs an engine state to render, and publishing that state
is C's.

---

## Two corrections to the inheritance list, found while scoping it

Both are the failure that section's own preamble names — *a list reconstructed by grepping arrives
short* — arriving inside the list written to prevent it. Neither is a measurement error and nothing
is failing.

**(i) There are two live strain cards numbered 26**, and everything in M5 cites the newer one.

- **26 · The liveness watchdog fires exactly once, because only an arrival re-arms it** — opened
  2026-08-20 at M4 stage D, owner M4 stage D's bench day B2, still open in M4's residue.
- **26 · The ping proves the socket and never the feed** — opened 2026-08-25 at M5 stage B1, owner C.

`docs/DESIGN.html` has met this before and recorded the remedy inside card 23: *"Two live cards under
one number in a document whose whole job is to be cited is a defect in the citation, so this one moves
to 23 and the older number keeps its references."* The precedent applies unchanged — **the M5 card
moves and the M4 card keeps 26** — except that here the newer card is the one with inbound references
from §9, ROADMAP, `NOTES-binance.md`, both M5 adapter briefs and DESIGN's own cross-references —
**12 mentions across six files** — so the renumbering has a cost the 22→23 case did not.
**Raise it rather than executing it silently.** Note the pattern while raising it: both
collisions were created by a **stage B1** opening a card, a milestone apart, and nothing checks.

**(ii) Card 28's C-half is missing from the eight-item table.** Card 28 — *the trigger asks for a
re-seed and nothing can adopt one* — resolves **"D for the mechanism and the memory, C for what the
panel shows while a re-seed is in flight."** The table has no row for it. It was opened by B2 on the
same day the table was written, which is exactly how the five-scattered-mentions problem reproduces.
It is deliverable 3 below. **Nine items, not eight.**

---

## Read first

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §6, §4 | Frozen, and this stage adds no `GapReason` and no `FeedEvent::Kind`. |
| `ARCHITECTURE.md` §9, **2026-08-25 (M5 stage A, qualifying the ping ruling)** | Items 1 and 2 in one row, with the reason the multiplier and the venue must not move together. |
| `ARCHITECTURE.md` §9, the two **stage B1** rows | The lying socket, and the correction to decision 1's premise. Deliverables 2 and 4 are downstream of the second. |
| `docs/DESIGN.html` strain **26 (M5)** | The four candidate remedies, their costs, the defect fixture, and **the expiry clause with teeth**. |
| `docs/DESIGN.html` strain **28** | Deliverable 3, and the adoptability measurement that already decided part of it. |
| `docs/briefs/M5-stage-B2-…md` § *Owed by stage C* | The inheritance table, and the two cautions carried with it. |
| `docs/briefs/M4-stage-A2-the-age-meter.md` | The *no reading* shape deliverable 5 reuses, and the blind spot pinned as a test rather than described. |
| `harness/replay/NOTES-binance.md` | Every figure here, **with the desk-box provenance stated beside it.** |

**Depends on:** B2 ✅. **Blocks:** D, M5 close-out.

---

## Deliverables

### 0 · Two ground-truth traces are CRLF in the working tree, and have been since 2026-08-07

`harness/replay/anvil_101_baseline.ndjson` and `anvil_101_reconnect.ndjson` differ from their
committed blobs in **every line and nothing else** — strip the CRs and both are byte-identical to
`HEAD`. They are the two oldest traces in the corpus, committed at M0 (`f103b72`) and checked out
onto this Windows desk *before* `a56ccd6` added `*.ndjson -text` to `.gitattributes`. The rule
protects every trace captured after it and never healed the two that predate it; the other twenty
are clean LF.

Nothing has failed and nothing could: `trace.cpp:148` and `json_scan.hpp:111` both treat `\r` as
whitespace, so the suite is green on either. **The exposure is `git add -A`** — a named guard failure
in `CLAUDE.md` — which would commit CRLF over the project's two oldest ground-truth files and move
two goldens without anyone choosing to.

Restore both to LF in **one commit of its own, first, before anything else in the split**, and
confirm `git hash-object` matches `git rev-parse HEAD:<path>` for each. This moves no golden: it
restores the committed bytes.

### 1 · The threshold: decide the number, and put the policy where a test can reach it

`kThresholdMultiple = 4.0` against a measured ~19.96 s cadence is ~79.9 s, and
`kThresholdCeilingMs = 30,000` clamps it to 30 s — so the self-calibration is **inert at this venue**,
producing exactly `kUncalibratedThresholdMs`. The ceiling was sized against Anvil's 176,000 ms silence
and has never met a venue whose *healthy* cadence is two thirds of it.

Decide: **ceiling rises, multiplier falls, or both become per-venue.** Record which, and why, with the
cost of the other two stated. The §9 row's constraint is binding — *changing the constant and the
venue in one step leaves no way to attribute a regression* — so if the ceiling moves, it moves in a
commit that changes nothing else.

`binance_atomeur_d100ms_liveness_20260826.ndjson` (221 s, 11 pings, 10 intervals against
`kMinSamples`' 8) is the first committed artefact that enters the calibrated branch. **Ship a test
that enters it and asserts the threshold that comes out** — the policy is host-checkable even though
the behaviour is not.

*One thing already established, so this stage does not have to reason about it:* the deferred
median-convention defect (strain 29) **cannot change this decision**. Interpolated gives
4 × 19,963.97 = 79,855.9 ms; nearest rank gives 4 × 19,947.7 = 79,790.8 ms. Both clamp to 30,000 today,
and if the ceiling rises to ~80 s the two conventions differ by **65 ms, or 0.081%**. State it and move
on — it is the argument that the M5 close-out deferral costs this stage nothing.

### 2 · Pick a remedy for the lying socket, and invert the fixture that proves it

Four candidates, none built at B1, all in strain 26 with costs: **(a)** defer the Snapshot to bracket
time — cheapest, no new concept, costs a fraction of a second of grey on a healthy connect and up to
~10 s on a quiet pair; **(b)** the ping does not stamp liveness until the bracket has been satisfied
once — no added grey, and the lie self-terminates at the threshold; **(c)** a *never-started* detector,
a genuinely new question needing a threshold of its own; **(d)** grade the seeded book against the
`@depth20` stream the board already pays 1.33 KiB/s for.

**Deliverables 1 and 2 are coupled and the table does not say so.** Remedy (b)'s cost *is* the
threshold — ~30 s today, ~80 s if the ceiling rises. Choosing (b) and raising the ceiling in the same
evening triples how long the lie renders LIVE. Decide them together, in that knowledge, and say which
constrained which.

Say out loud what the choice does **not** buy: neither (a) nor (b) closes the general hole. A
server-side subscription drop an hour into a session still presents identically. What they convert is
*permanent* into *bounded*, and bounded is where invariant #5 draws its line.

**The fixture clause is binding.** `test_binance_adapter.cpp`'s silent-stream case asserts today's
broken behaviour — `status == Live`, no stale episodes, `seed_bracket_ok == 0`, a populated book. When
a remedy lands that case **must** fail, and the correct response is to **invert the two assertions
marked THE LIE in the commit that makes it pass** — not to delete it and not to relax it. If C ships
without inverting it, the fixture does not lapse: it moves to whichever stage next touches liveness,
with this clause attached.

### 3 · The engine state a re-seed in flight needs — the item the inheritance table missed

Strain 28: the trigger B2 built asks for a re-seed and no layer answers it on a live book.
`reseed_wanted()` is a request nothing can serve; the adapter says so out loud via
`resnapshots_declined` / `resnapshots_adoptable` rather than dropping it silently.

The mechanism and its memory are **D's** — (a) a ~128 KiB deferred buffer, (b) drop-gap-reseed, (c)
merge below the touch. **C owns the state that gets published while a fetch is in flight**, because
D cannot decide what to render without one and the two candidate renderings differ in nothing a host
test can assert. Publish it in existing padding if it fits, as A2 and M4 stage C both did, and pin
`sizeof(DisplaySnapshot)` unmoved on **both** host and xtensa — three documents quote a byte count
derived from it.

The measurement that constrains (b) is already taken and belongs in the record here: adoption is
loss-free whenever the body is not older than the book, and B2 measured that two ways, agreeing on
every capture — **0 of 7 adoptable at `limit=1000` on the liquid pair, 19 of 19 everywhere else**. The
deeper the seed the older it lands: the venue snapshots roughly three quarters of the way through the
round trip and spends the rest shipping ~120 KB.

### 4 · Write the transport-versus-feed hole where the code can see it

Item 5 has no fix and this stage is not to invent one. The ping is emitted below the subscription;
no care with stream names closes it; the venue publishes no subscription-state signal on the
market-data streams at all. **What C owes is that this is stated at the point of use** — beside the
thing that stamps liveness, not only in §9 and a strain card — so the next reader of a green clock
learns what it is entitled to mean without having to find two documents.

### 5 · The three `age_ms` limits, recorded per venue, and item 8 decided

All three measured at B2; none needs re-measuring.

- **Blind to a feed backlog, tracks a socket backlog at 1.00×** — the ping shares the TCP stream. B1's
  hypothesis narrowed rather than confirmed.
- **No reading at all for the first ~638.8 s of every connection** — `kBaselineSamples = 32` at a
  ~20 s cadence, against 16 s at Anvil and 32 s at Kraken. Decide whether the header shows *no
  reading* rather than a number it cannot support; **the shape already exists** — A2 used it for a
  reconnect. C publishes the state; D decides how it looks.
- **`kAgeWindowSamples = 256` is ~85.2 min at this venue**, and the constant's own comment says it is a
  2 KiB memory budget and never a duration. **Handed to C undecided, and C decides it**: is an
  85-minute supremum window defensible at a 20 s cadence, or does the constant become per-venue? Either
  answer is fine; carrying it further is not.

### 6 · Ask the parity question explicitly rather than letting D discover it

**Three of M4's honesty mechanisms are compromised at this venue from one cause**, not three: the grey
threshold has nothing to arm on, the age meter has nothing to measure against, and the meter
additionally has no reading for the first ~11 minutes. Whether M5's definition of done can still claim
parity with M4's is a question this stage answers in writing — yes with these caveats, or no and here
is the reduced claim. B2 flagged it precisely so it would not be discovered at the bench.

### 7 · Writeback

Session log with decisions **and why**; `ROADMAP.md`'s M5 row; `NOTES-binance.md` for anything
measured, provenance stated beside each figure; `ARCHITECTURE.md` §9 for anything with architectural
weight; `docs/DESIGN.html` for every card this stage closes or moves, its milestone strip, and §08.
**Raise the duplicate card 26** rather than renumbering it unasked.

---

## Constraints

- **§6 frozen. §4 does not move.** No new `GapReason`, no new `FeedEvent::Kind`. **Sixth asking**; the five
  before it were all refused. Stop and raise.
- **Desk only.** No `firmware/`, nothing flashed, and **no judgement about what the panel looks like.**
  Every such question is D's by the ruling above; if the work seems to need one, that is the signal
  that the split was drawn wrong — raise it, do not decide it.
- **The ceiling, if it moves, moves alone.** §9, 2026-08-25.
- Pin tables add-rows-only. **No golden moves** — deliverable 0 restores committed bytes and is not an
  exception to this.
- **Per-commit verification in a fresh detached worktree, `CMAKE_HOME_DIRECTORY` confirmed to point at
  the worktree**, loop run **inline** — `powershell -File` dies at `CMakeTestCXXCompiler`. This desk
  uses the **`host-mingw`** presets. Delete stale worktrees when done.
- **Push the stage to `m5/stage-c`. Fast-forward `master` only once the ladder has closed.**
- **Commit only when asked.**

## Known unknowns — resolve and record

Whether the ceiling, the multiplier, or per-venue values is the right shape, and what the other two
would cost. Whether remedy (a), (b), (c) or (d) — and what the threshold decision did to that choice.
Whether the state a re-seed in flight needs fits in existing padding. Whether an 85-minute supremum
window is defensible at a 20 s cadence. Whether M5 can claim parity with M4's definition of done.
**And whether the duplicate card 26 is renumbered now or after the inbound references are counted.**

## Definition of done

- ☐ Both Anvil traces restored to LF in the split's first commit, hashes confirmed against `HEAD`.
- ☐ The threshold decided — ceiling, multiplier or per-venue — with the other two costed, the
      constant moved in a commit of its own if it moves, and **a test that enters the calibrated
      branch and asserts what comes out.**
- ☐ One of the four remedies built, **chosen jointly with the threshold and the coupling recorded**,
      and the general hole stated as still open.
- ☐ `test_binance_adapter.cpp`'s silent-stream case **inverted, not deleted and not relaxed**, in the
      commit that makes the remedy pass.
- ☐ The re-seed-in-flight engine state published, `sizeof(DisplaySnapshot)` pinned unmoved on host
      and xtensa.
- ☐ The transport-versus-feed hole stated at the point of use.
- ☐ Items 6, 7 and 8 recorded per venue; **8 decided.**
- ☐ The parity question answered in writing.
- ☐ The duplicate strain card 26 raised with its reference count.
- ☐ ctest green from a clean configure; no golden moved; session log · ROADMAP · NOTES · §9 · DESIGN;
      split proposed; nothing committed until approved; `m5/stage-c` pushed and `master`
      fast-forwarded only after the ladder closes.

## Out of scope

**Everything about what the panel looks like — every one of them D's**: what a silent feed renders,
what a re-seed in flight renders, whether the chosen remedy's grey reads right, and strain 24's
unvalidated-levels question (*C records the number per policy; D decides knowing it*). The re-seed
**mechanism and its memory** (D). The **median convention** and `trace.cpp`'s second home for it —
**M5 close-out**, and deliverable 1 shows the deferral costs this stage nothing. The bench, the
>24 h soak, the board's audit stream (D). The client ping (M6). The venue toggle (M7). M4's carried
residues — D1, D2, D7's scope trace, and M4 stage D's own card 26. The §1-versus-reality trade-print
question — ROADMAP backlog **D0**, owner's, on no stage.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->

