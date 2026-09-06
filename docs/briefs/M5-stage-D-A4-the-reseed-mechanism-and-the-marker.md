# M5 Stage D-A4 — the re-seed mechanism, `InFlight`, and the marker

**Track:** Agentic [desk] · **Status:** Not started · **Size:** one desk evening
**Written:** 2026-08-30 by the desk seat, splitting D-A3 on wall-clock grounds.
**REWRITTEN 2026-09-05 against master** — D-A3's close, D-C's first run and stage E moved this
stage's premise and five of its figures. What changed is listed in § *What moved since 2026-08-30*
rather than silently repaired, because a brief that quietly re-wrote itself would be the thing
ARCHITECTURE §9's 2026-08-30 rows are about.
**Executor:** Claude Code. **No new rendering decisions** — D-B took all four.

**This stage is the last one M5 owes before its close-out.** ROADMAP's M5 row ends *"Owed: the M5
close-out list, and D-A4."* Strain 28 stays open until this lands.

**Read first**

| Source | Why |
| --- | --- |
| `M5-stage-D-A3-…md` **§2** | **The ruling.** The re-seed choice is **(a) or (c)**; (b) is closed. **Do not re-derive it** — and see §1 for what reopening it would cost. |
| `firmware/src/venue_budget.hpp`, **the whole header** | **The bound (a) has to clear, and it is pinned.** Read the derivation before pricing anything, and read the assertion note before touching a constant. |
| `hardware/bench-2026-08-30-D-C-soak.md`, **check 1 and §11** | The current internal-heap picture, superseding D-A1's. **This is now required reading, not forbidden** — see § *What moved*. |
| `M5-stage-D-B-…md`, decision **2** | What `InFlight` draws and the four constraints on the marker. **Implement, do not re-decide.** |
| `docs/DESIGN.html` strain **28** | The card this stage closes; C's half is already done and the card's D-half is *the mechanism and the memory alone*. |
| `ARCHITECTURE.md` §6 invariant **#7**, and §9's **2026-08-30 D-B row including its D-A3 correction** | The allocation rule the mechanism must state itself against, and the verification rule this stage is held to. |

**Depends on:** D-B ✅ (`05e05d6`) — decision 2 is a binding input — and **D-A3 ✅ (2026-08-30,
ctest 52/52, six arms)** for the ruling it records. **Blocks:** strain 28's closure and the M5
close-out.

**Coordinate before starting.** D-C's six task-watchdog aborts at PC `0x4201c9f8` are being chased in
`firmware/` by another session, and D-C's re-run is gated on that fix. This stage touches
`firmware/`. **Rebase onto the fix rather than racing it**, and say in the session log which commit
you built on.

---

## What moved since 2026-08-30

Recorded so that a reader who saw the first version knows which sentences are gone and why.

- **The calendar argument is spent.** This stage was split out to run *concurrently with the 24-hour
  soak*. **D-C has run** — 2026-08-30/09-01, 34.56 h in seven boots, longest continuous 9.84 h, §1
  not met — and **stage E is done**, with a second soak of 32.25 h and 27.81 h continuous. The split
  was right; its reason has expired. This stage is now simply next.
- **"Do not read the soak" is inverted.** Two records are committed and both bear on the questions
  below. D-C's instrument repairs are in; a figure taken from either record now is taken from a read
  capture, not from a run in flight.
- **(a) is no longer merely expensive** — see §1. `venue_budget.hpp` pins a bound it does not clear.
- **The fetch duration is a live disagreement**, not a settled figure. See §1's note.
- **The verification rule is narrower and sharper than the first version stated.** See §2.
- **`DisplaySnapshot`'s publish site moved** at stage E, from once per level to once per venue
  message. See deliverable 2.
- **Two out-of-scope items were wrong**: §3b's three defects were **fixed 2026-08-27** (and §3b's own
  note says its proposed owner is superseded), and strain 29's tripwire now lands on *this* stage
  rather than on some later one.

## 1 · The ruling this stage inherits, and the bound it now has to clear

D-A3 §2 settles the candidate question, and it is restated here only so that a session reading this
brief alone does not reach for stage C's table and find (b) the cheapest row on the page:

> **The re-seed mechanism is (a) or (c). (b) is CLOSED** — not on cost or taste, but because
> stage C priced (b) as *"greys the panel for the length of a fetch on a book that was still
> correct"* and D-B's decision 2 refused exactly that. (b) drops the book, so the panel greys;
> decision 2 says it stays coloured. **Reopening (b) means reopening decision 2, deliberately and
> first**, and that is the owner's to do.

| | candidate | cost | status |
| --- | --- | --- | --- |
| **(a)** | a ~128 KiB deferred buffer | no gap, no grey — **and it does not fit in internal SRAM**, so it means PSRAM | **open, re-priced** |
| **(b)** | drop-gap-reseed | free in memory; greys a correct book for the length of a fetch | **closed by ruling** |
| **(c)** | merge below the touch | cheapest, least proven — **nothing in the corpus exercises it** | **open** |

**THE RE-PRICING, AND IT IS THE MOST IMPORTANT SENTENCE IN THIS BRIEF.** The first version of this
brief said (a) *"more than doubles the adapter's ~96 KiB of fixed state"*, which was a comparison
against the wrong quantity and against a number two stages had moved. The quantity that governs is
the one `venue_budget.hpp` asserts:

| quantity | value | source |
| --- | ---: | --- |
| `kVenueInternalBudgetBytes`, pinned and asserted | **111,624 B** | `venue_budget.hpp` (241,720 + 8,400 − 106,496 − 32,000) |
| Binance adapter's internal residency | **~68,060 B** | 8,400 + the linker's +59,660 delta, same file |
| headroom under the bound | **~43.6 KiB** — the file's header states it as 44,596 B | *reconcile these two while you are in there* |
| largest free internal block, steady state | **51,188 B** | D-C check 1, eight reconnect readings, 3.18× / 3.06× over 16,717 B |

**128 KiB is 131,072 B. It clears neither figure.** So (a) is available only in PSRAM, on
`buf_lvl_`'s precedent from D-A1, and ARCHITECTURE §5's *"the window lives in internal SRAM, the tail
in PSRAM"* is the stated test for whether it may go there. **The live choice is therefore (a) in
PSRAM, or (c)** — and (a)-in-PSRAM must be argued on the frame path's behalf, not just on the
budget's, because that is the objection that kept `bids_`, `asks_` and `frame_` internal.

**Two ~96 KiB figures exist in this subsystem and they are not the same number.** B1's *"~96 KiB
fixed and never allocated"* for the adapter's storage — 32,768 B of which is now PSRAM — and the
**96 KiB PSRAM REST body buffer** (`feed_task.cpp:251`). Card 28 has already recorded one conflation
in this exact area (~120 KB versus ~128 KiB). **Name which one you mean, every time.**

**How often it matters.** B2 measured **0 of 7 adoptable at `limit=1000`** on the liquid pair and
**19 of 19 everywhere else** — the deeper the seed the older it lands, because the venue snapshots
about three-quarters of the way through the round trip (median ~0.76, range 0.21–0.86) and spends the
rest shipping the body, **64,046 B every time**. A re-seed is not rare at the shipped depth, which is
what makes this a mechanism rather than an edge case. **D-C's first run went further: 5,562 seed
fetches, one per 22.4 s**, on a board that was `live=1` for 28.5% of samples and grey for 71.4% of
uptime. That run was not quiet and a quieter one will read differently, but the order of magnitude is
now measured rather than inferred.

**And the fetch duration is unsettled — do not pick a side silently.** D-A2's log says
**3,143–4,485 ms**; D-B's brief, ROADMAP and card 28 all say **3.1–5.6 s**; D-C §2(i) records that
**no source in the tree produces 5.6 s**. If this stage needs the number, resolve it and say how, or
quote the one you used and name it as the one you used.

## 2 · Deliverables

**Deliverable 1 · Build the chosen mechanism**, (a)-in-PSRAM or (c), **with the reason recorded**.
Its memory cost is stated against **invariant #7** the way D-A1 and D-A2 both stated theirs, and
asserted where `venue_budget.hpp` asserts the rest — a footprint argued in a log and not checked by
the build is the thing D-A1's guard exists to prevent. **If (c) is chosen, the trace that exercises
it is part of the deliverable**, not a follow-up: *nothing in the corpus exercises it* is stage C's
own description, and "least proven" stops being acceptable the moment it is also "shipped".

*Scope note, because the first version overstated this.* **The fetch side already exists.**
`SeedAction::Issue` → `seed_.request(...)` runs on the board today and ran 5,562 times in the D-C
capture; `on_rest_body` already reaches the book through `apply_only`. What is missing is **adoption
on a live book** and the state advance. Read `feed_task.cpp`'s seed path before writing anything, and
respect the two rules already written into it: the request is a **level, not an edge**, and it is
**cleared at issue, never at completion**.

**Deliverable 2 · Advance `DisplaySnapshot::reseed` to `InFlight`.** The state exists — stage C put
it in the four pad bytes the struct already carried, verified on host *and* xtensa — and it was left
**deliberately unreachable** at stage C so that an open card would be visible on the panel rather
than only in a report line; D-A2 kept it that way by its own out-of-scope. This is the field that
advances. **The site is `FeedTask::publish_current()`**, between `book_.publish(staging_)` and
`channel_.publish(staging_)`, beside the `age_ms` stamp and for the same two reasons (invariants #1
and #8). **Stage E made that a once-per-venue-message stamp rather than once per level** — read the
comment block above `publish_current()` before adding to it. **`sizeof(DisplaySnapshot)` stays 1,168**
and `sizeof(SnapshotChannel)` stays 3,528; ROADMAP **D6** is not being done here.

**Deliverable 3 · The header marker.** D-B pinned it and handed constraints rather than a choice:

- The marker takes the **symbol's slot**, under the standing priority **VALUE > AGE > SYMBOL**.
- **At most eight characters**, asserted against the real header width in `test_ladder_render.cpp`
  the way every `reason_text` string already is — copy line 522's shape,
  `text_width("9999") + kGlyphAdvance + text_width(marker) <= kPanelWidth`. The header is thirteen
  characters at this cell width (line 206), so four for the value plus eight is the whole line.
- It **yields exactly as the symbol yields today**, so a header too narrow drops the marker rather
  than overlapping the price or the age.
- Drawn in **`Ink::Symbol`** — the slot's ink, not a new one. A new `Ink` costs a stale-palette
  entry and a `static_assert`, for a marker that never appears on a grey panel.

On `InFlight` the **live palette stays selected and every ladder `Ink` is unchanged**; the only
difference from `None` is inside `draw_header`. Grey is not used.

## 3 · Constraints

- **Invariant #7 governs deliverable 1, and the axis is §1's pinned bound** — not a doubling of the
  adapter. Whichever candidate wins, the cost is stated in those terms, and if the `static_assert`
  fires, read its own note first: **firing it is a decision, not a licence to raise the number.**
- **`engine/` stays host-buildable.** The mechanism's engine half must not acquire a transport.
- **Nothing branches on `reseed`** (invariant #5). It is a state the renderer may draw, never a rule
  the engine applies — the same standing constraint `age_ms` carries.
- **Verification, per §9's 2026-08-30 D-B row as corrected by D-A3 the same day.** A commit is
  verified only by a build that **compiles what the commit changed**, and the step must name the
  track it ran. The corrected boundary is mechanical: **the host suite compiles a `firmware/src`
  header if and only if some host test includes it** — sixteen do — and compiles **no
  `firmware/src/*.cpp` and no `platformio.ini` ever**. So for each commit ask *does a host test
  include this file?*, then run the track that answers it: **ctest** (52 at D-A3's close, and
  `dc_tests_binance` is the arm that compiles `DC_VENUE=3`) for `engine/`, `harness/`, `tools/` and
  any covered firmware header; **`pio run -e depthcharge-binance` in the same worktree**, with
  `secrets.h` copied in first, for anything it does not cover. **A commit spanning both needs both,
  and a track that cannot be run means the commit is unverified rather than green.**
- **Read the soak records.** `bench-2026-08-30-D-C-soak.md` for the heap and re-seed picture,
  `bench-2026-09-04-E-soak.md` for the publish cadence. **Do not read D-C's *second* run** — it has
  not happened, and it is gated on the crash fix, not on this stage.

## 4 · Known unknowns — resolve and record

- **(a)-in-PSRAM or (c).** The ruling narrows it to two and does not choose. **Price it against
  §1's table**, not against D-A1's figures and not against the adapter's ~96 KiB. If (a), the PSRAM
  argument has to answer the frame-path objection as well as the budget.
- **The fetch-duration disagreement**, if this stage needs the figure at all. §1's note; D-C §2(i).
- **What the header shows during the ~11 minute age no-reading window** (639 s = 32 baseline
  intervals at the 19,964 ms ping cadence). Raised in D-A3, still unassigned, and D-C flagged it as
  *"D's"*. If it needs a rendering decision it is the owner's; this stage is where such a decision
  would be *implemented* rather than taken. **And strain 29's tripwire now points here**: *"if any
  stage before the close-out needs to quote or re-pin a Binance cadence figure, this closes first."*
  This is the only stage before the close-out. **Leaving the window alone is now a decision to
  record, not a deferral to assume.**
- **Whether `InFlight` is reachable often enough to be seen at the bench.** Better armed than it
  was: D-C's run gives one re-seed fetch per 22.4 s, and since stage E the panel draws **93.8%** of
  **7.19 publishes/s** rather than an arbitrary mid-message sample of 146.35 events/s. "Not rare in a
  capture" and "visible on a panel for 3–5 s" are still different claims, and the second one needs a
  bench reading. **If it cannot be seen, say so** — an unreachable state was the *point* before this
  stage and becomes a defect after it.

## 5 · Definition of done

- ☐ The mechanism is built, its candidate chosen **with the reason recorded**, and its memory stated
      against invariant #7 and asserted in `venue_budget.hpp` — **against the pinned 111,624 B bound,
      with the 43.6 KiB / 44,596 B discrepancy in that file's header reconciled or named.**
- ☐ If (c): a trace that exercises it is committed, with a golden.
- ☐ `DisplaySnapshot::reseed` reaches `InFlight` on the board, stamped in `publish_current()`;
      `sizeof` still **1,168 / 3,528**.
- ☐ The header marker renders per D-B's four constraints, with the width assertion in
      `test_ladder_render.cpp`.
- ☐ Any fetch-duration figure quoted is either resolved or **named as the source it came from**.
- ☐ The ~11 minute window is either addressed or **explicitly left, with strain 29's tripwire
      answered either way.**
- ☐ **Strain 28 closed** — the mechanism and the memory were its D-half.
- ☐ Any decision with architectural weight to `ARCHITECTURE.md` §9; `docs/DESIGN.html` updated where
      the card moves.
- ☐ Per-commit verification per §3's corrected rule, **naming the track for each commit**; session
      log · ROADMAP; **push to `m5/stage-d-a4`, rebased onto the crash fix**; split proposed;
      nothing committed until approved.

## 6 · Out of scope

**The ping wire, the policy routing and the soak instrumentation — D-A3 ✅.** **The soak — D-C**,
whose second run is gated on the firmware crash. Every rendering decision — **D-B**, closed;
decision 2 is an input here and is not reopened by this stage. Reopening candidate (b) — the
owner's, and it means reopening decision 2 first. **The task-watchdog crash** — another session's.
ROADMAP **D6**'s +8/+24 bytes and the price-axis window — not here; `sizeof` does not move.

**To the M5 close-out:** the median convention (card 29) and strain 29's tripwire wording; the
`CLAUDE.md` prose-versus-ordinal line; **propagating the 64,046 B correction into `M5-stage-C`'s two
remaining quotes** (the source, `NOTES-binance.md`, already carries it); and the questions D-C's
reading raised — check 4's 1.060× margin against §4.4's stated 2.29×, check 2's measured 1.024×
clearance against stage C's claimed 1.99×, per-boot segmentation in `tools/soak_report.py`, that
tool's un-enrolled grammars, and backlog **D8**. **Plus one defect found while auditing this brief:**
`venue_budget.hpp`'s assertion note still says *"What is NOT on the list is editing `71'308`"* while
the pinned constant is `111'624`.

**No longer out of scope, because it is done:** §3b's three defects, **fixed 2026-08-27**.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->
