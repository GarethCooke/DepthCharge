# M5 Stage D-A4 — the re-seed mechanism, `InFlight`, and the marker

**Track:** Agentic [desk] · **Status:** **Desk work done 2026-09-06 — split proposed, nothing
committed; the board half (flash + bench) is not done** · **Size:** one desk evening
**Written:** 2026-08-30 by the desk seat, splitting D-A3 on wall-clock grounds.
**REWRITTEN 2026-09-05 against master** — D-A3's close, D-C's first run and stage E moved this
stage's premise and five of its figures. What changed is listed in § *What moved since 2026-08-30*
rather than silently repaired, because a brief that quietly re-wrote itself would be the thing
ARCHITECTURE §9's 2026-08-30 rows are about.
**Executor:** Claude Code. **No new rendering decisions** — D-B took all four.

> **THAT SENTENCE IS NO LONGER TRUE, AND IT IS LEFT STANDING RATHER THAN EDITED
> AWAY.** There is a **FIFTH** rendering decision: *the header trims trailing
> zeros from the value*, **taken by the owner on 2026-09-06 at this stage's
> split**, after the stage measured that D-B's marker could not be drawn at a
> venue whose declared scale is eight decimals. It is the owner's decision and
> not this stage's finding-turned-into-a-fix; D-A4 implemented it and did not
> re-decide it. It reopens none of D-B's four — the marker still draws in
> `Ink::Symbol`, in the symbol's slot, under **VALUE > AGE > SYMBOL**; only the
> value's width changes. ARCHITECTURE §9, 2026-09-06.

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

**Nothing gates this stage.** An earlier draft of this line said D-C's task-watchdog aborts were
*"being chased in `firmware/` by another session"* and told you to rebase onto that fix. **It was
inherited from D-C's brief and was not true** — checked 2026-09-06: no branch, no brief, nobody on
it. It is backlog **D10**, and stage E re-ran without it and cleared D-C §1 at 27.81 h continuous.
Start when you like; say in the session log which commit you built on.

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
  not happened.

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

- ☒ The mechanism is built, its candidate chosen **with the reason recorded**, and its memory stated
      against invariant #7 and asserted in `venue_budget.hpp` — **against the pinned 111,624 B bound,
      with the 43.6 KiB / 44,596 B discrepancy in that file's header reconciled or named.**
      *(a); +32 B internal, no allocation at all; headroom now the pinned `kVenueInternalHeadroomBytes`
      = 45,584 B. Both figures in the discrepancy were wrong and neither was the other's quantity — §1
      of the log.*
- ☒ *n/a — (c) was not chosen. Coverage is supplied anyway: a synthesised trace drives the whole
      `None → Wanted → InFlight → None` transition, because **no committed capture fires the coverage
      trigger** (`cover_triggers == 0` on all six BTCUSDT captures with the fetch modelled).*
- ☐ `DisplaySnapshot::reseed` reaches `InFlight` on the board, stamped in `publish_current()`;
      `sizeof` still **1,168 / 3,528**.
      *Stamped in `publish_current()` and reached end-to-end on the host. `sizeof` unmoved, verified on
      host **and** on xtensa (`pio run -e depthcharge-binance` green). **The board half is NOT done: no
      flash, no bench sitting this session**, so "on the board" is unverified and is the first line of
      the next step.*
- ☒ The header marker renders per D-B's four constraints, with the width assertion in
      `test_ladder_render.cpp`.
      *Built to all four. **And it can never be drawn on this build** — the arithmetic is pinned in the
      same file and is now ROADMAP **D11**. §4 of the log.*
- ☒ Any fetch-duration figure quoted is either resolved or **named as the source it came from**.
      *Resolved by measurement, not by elimination: **n = 5,542 successful board fetches** in
      `hardware/bench-2026-08-30-D-C-soak.log.gz` — min 2,693 ms, **lower median (nearest rank)
      3,973 ms**, p90 4,790, p99 5,721 (nearest rank), max 8,123 ms. Neither prior range bounds it.*
- ☒ The ~11 minute window is **explicitly left**, and strain 29's tripwire is answered: it does not
      fire, and §5 of the log says why rather than leaving it to be assumed.
- ☒ **Strain 28 closed** — the mechanism and the memory were its D-half. *`docs/DESIGN.html` card 28.*
- ☒ Any decision with architectural weight to `ARCHITECTURE.md` §9; `docs/DESIGN.html` updated where
      the card moves. *Three new §9 rows, plus a correction appended to the 2026-08-30 D-B/D-A3 row
      (the "sixteen headers" count is 17 direct / 22 transitive, and was wrong when written).*
- ☒ Per-commit verification per §3's corrected rule, **naming the track for each commit**; session
      log · ROADMAP; **push to `m5/stage-d-a4`**; split proposed;
      nothing committed until approved.
      *Split approved 2026-09-06 and executed as eight commits; the ladder's per-commit table is
      §8. `origin/m5/stage-d-a4` = `ec80ec4`; `master` unmoved at `25510ec`.*

## 6 · Out of scope

**The ping wire, the policy routing and the soak instrumentation — D-A3 ✅.** **The soak — D-C**,
whose second run is gated on the firmware crash. Every rendering decision — **D-B**, closed;
decision 2 is an input here and is not reopened by this stage. Reopening candidate (b) — the
owner's, and it means reopening decision 2 first. **The task-watchdog crash** — unowned, backlog **D10**.
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

### 2026-09-06 · Opus 5 · the mechanism, and two things it found on the way

Built on **`25510ec`** (`master`, and `origin/master` had caught up to it by the time this session
started — the send-note's *"not pushed, `origin/master` is `222bdc9`"* was already stale). Branch
`m5/stage-d-a4`. **Nothing committed: the split is proposed below and awaits approval.**

**Host: `cmake --workflow --preset host-mingw` 52/52. Firmware: `pio run` green on all three arms
(`depthcharge`, `depthcharge-kraken`, `depthcharge-binance`). No flash, no bench sitting.**

---

#### 1 · The candidate: (a), and the question the ruling framed as hard was already answered

**(a)-in-PSRAM, and it needs no PSRAM that is not already allocated.** §1 of this brief re-priced (a)
against `venue_budget.hpp`'s pinned bound and concluded *"128 KiB clears neither figure, so (a) is
available only in PSRAM"*. That is true and it is not the constraint, because **the buffer (a) needs
already exists**: M5 stage D-A2 raised `kBinanceBufferEvents`/`kBinanceBufferLevels` from 64/2,048 to
**256/32,768 — 8 KiB + 512 KiB, on the heap and therefore in PSRAM** — and sized them against
`kBinanceFetchDeadlineMs`, *the same 15 s deadline a re-seed fetch runs under*. That is **4× stage C's
~128 KiB estimate**, allocated on every boot, and **idle for the entire life of a seeded book**:
`buffer_diff` has exactly one caller, `on_diff`'s `Unseeded` branch. Pre-seed holding runs only while
`Unseeded` and re-seed holding only while `Seeded`, so the two can never overlap and the array is
shareable.

**Why this is a §9 row and not a footnote.** Stage C priced (a) in the same milestone that D-A2 later
paid the price for its own reasons, and nothing re-priced it. Three documents — stage C's table,
DESIGN card 28, and this brief — carried the figure forward for four stages after it had been bought.
*A cost recorded against one stage's design can be paid by another stage for something else, and
nothing re-prices the first.*

**Cost, measured rather than argued.** `sizeof(BinanceAdapter)` **66,008 → 66,040 B on xtensa**, both
ends read out of the compiler rather than derived: **+32 B, all of it four new counters** — three for
the mechanism, and `diffs_inside_baseline` for the venue-procedure clause review found missing
(§5c). The mechanism's own state is one `bool` and it cost nothing, landing in padding the object
already carried. **There is no allocation at all, at
any point, including construction** — only the access pattern of an existing block changed, which is a
stronger invariant #7 statement than D-A1's or D-A2's.

**The frame-path objection, which §4 required to be answered rather than waved.** The hold *is* on the
per-diff path while a fetch is outstanding — every diff is appended to `buf_lvl_` as well as applied —
and that is more than the pre-seed use ARCHITECTURE §5 sanctioned, so it is argued on its own terms:
a **sequential 16-byte append**, never read until the body lands; **~830 levels/s** worst case over the
corpus (~13 KB/s); **duty-cycled at ~18%** (fetch median 3,973 ms against a re-seed roughly every
22.4 s in the D-C capture); and **already instrumented** — `worst_parse_fetch_us` /
`worst_parse_quiet_us` exist for exactly this comparison, and D-C's final boot reads **59,280 µs
during a fetch against 1,499,017 µs quiet**, i.e. the fetch window is currently the *fast* one by 25×.
**`worst_parse_fetch_us` approaching `worst_parse_quiet_us` is the falsifier**, and D-C's second run is
where it should be read.

**Why not (c).** Its case was that it needs no memory, and (a) now needs none either. It is also
unprovable here: the resulting book is a mixture of two instants that no bracketing statement covers,
**and Binance publishes no checksum**, so nothing on the board or in the harness could detect that the
deep half had gone wrong. (a) is lossless by construction — the book it produces is exactly the book a
client that had seeded at `L` and applied every diff since would hold — and that is a property a host
test can assert. Stage C's own words for (c) are *"cheapest, least proven — nothing in the corpus
exercises it"*, and the first half stopped being true.

**And the mechanism turned out not to be new code.** Once the interval is held, adopting a re-snapshot
is character for character the venue's documented seed procedure, which `replay_buffer` has
implemented since B1. `adopt_reseed` reuses it and differs in exactly one thing: **a re-seed that
cannot be bracketed leaves the live book standing.** The seed path answers that failure with
`drop_book`, which is right when there is no book to lose and is **candidate (b) arriving through the
error door** when there is — so the bracket is tested *before* the ladder is touched, which is the
whole ordering difference between `adopt_reseed` and `adopt_seed`.

#### 2 · Two conditions in `binance_adapter.hpp` contradicted each other

Found while building on `replay_buffer`. The venue's rule has two clauses — the event *following* a
snapshot must satisfy `U <= L + 1 <= u`, every event after it must satisfy `U == prev_u + 1` —
and `replay_buffer` tested the first, then applied the bracketing survivor through the second. So a
body whose `lastUpdateId` fell strictly **inside** a coalesced message passed the bracket, **published
its `Snapshot`, and was dropped one statement later with `Gap{SeqGap}`**: a ladder live for the width
of one event and then grey, which is the output M5 stage C's deferred-`Snapshot` remedy exists to
prevent, reached through a door that remedy did not cover.

**It had never fired, and the reason is the stream variant.** Measured over every committed Binance
capture — every `kind:"rest"` body followed by a bracketing diff, 24 of them:

| stream | bodies | `U == L+1` | `U < L+1` (straddle) |
| --- | ---: | ---: | ---: |
| `@depth@100ms` (**shipped**) | 18 | 18 | **0** |
| `@depth` (1,000 ms) | 6 | 4 | **2** |

*(The first version of this table said 947 / 881 / 66, and **review caught it**: `@depth20`
partial-depth payloads carry a `lastUpdateId` as well, so a scan keyed on that field sweeps in 923
payloads that can never reach a bracket — `on_frame` counts `FrameKind::PartialDepth` and breaks.
Wrong population, 39× the sample, and a straddle rate 10× rarer than the truth. The corrected
figure makes the case stronger, not weaker: **2 of 6**.)*

The two straddles are in `binance_btcusdt_d1000ms_20260824.ndjson`, 12 and 45 ids inside a message.
**So the case is in the corpus** — it had simply never run, because those bodies arrive mid-stream
where every re-snapshot was declined unread. The unit test missed it too: `diff(21, 25, …)` against
`seed_body(20, …)` is the one point where the two conditions agree. Fixed at both bracket sites, with
the straddle and a genuine hole tested at each. **Fixed here rather than deferred because this stage
multiplies the exposure ~40×** — a bracket used to run once per connect and now runs once per re-seed.

#### 3 · The `venue_budget.hpp` discrepancy — both figures were wrong, and they were not the same quantity

- **3,628 B is a real board reading.** D-A1's acceptance printed `free=117548 … reserve=81920
  budget=35628`; 35,628 − 32,000 = 3,628.
- **44,596 B was never measured.** No board reading in the tree produces it. `panel.hpp`'s reserve note
  *forecasts* *"the Binance build's budget goes 35,628 → ~76,588"*, and 76,588 − 32,000 = **44,588** —
  eight bytes away, and flagged *there* as **"STILL A FORECAST, AND SAID SO"**. A forecast was quoted
  as an outcome, in the file whose whole subject is a number going stale.
- **43,564 B (the brief's "~43.6 KiB") is right arithmetic on a stale input**: 111,624 − 68,060, where
  68,060 is D-A1's `sizeof(BinanceAdapter)` — superseded by D-A2 *in the same stage that produced
  44,596*, when `buf_` (64 × 32 = 2,048 B) followed `buf_lvl_` to the heap.

The two margins *are* the same quantity by construction; the identity holds only when the
free-internal reading, the linker delta and the `sizeof` are of one vintage, and here they were D-A2's,
D-A1's and D-A1's. **The fix is not a third number in a comment**: the headroom is now
`kVenueInternalHeadroomBytes`, computed by the compiler and pinned at **45,584 B** — measured on
xtensa, since **no host test reaches this file at all** (it includes `panel.hpp`, which includes the
HUB75 driver, so it is not host-compilable). The pin is guarded to the Binance arm, because this
session built that arm and pinning one it never compiled would be the wave-through the ladder exists to
prevent. The stale `71'308` in the assertion note is corrected to `111'624`.

#### 4 · The marker is built to all four constraints and cannot be drawn — ROADMAP **D11**

*(As measured before the owner's fifth and sixth rendering decisions, both taken at the split later the same day. Left standing as the finding that prompted them; §5d and §5e are what became of it, and **D11 is now closed**.)*

`"RESEED"`, in the symbol's slot, in `Ink::Symbol`, yielding through the same test the symbol yields
through, asserted at ≤ 8 characters against the real header width. Then dropped on every header this
project renders.

`left_limit` is what the value and the age leave. A six-character marker is 29 px and needs 34.
**Binance's `price_decimals` is 8**, and `format_scaled` emits exactly that many fractional digits *by
design*, so a price column lines up — so a live BTCUSDT last price is `"108234.56000000"`: **fifteen
characters, 74 px on a 64 px panel.** `value_x` clamps to 0 and the age, the symbol and the marker are
dropped together. The grey panel does not rescue it either — `"NO LINK"` leaves **21 px** with no age
reading and **6 px** with one, so with a reading neither symbol nor marker fits, and without one the
substitution is a net *loss*: the symbol fitted and the marker does not.

**D-B could not have seen this**, and the reason is worth more than the defect: decision 2 was judged
from `hardware/bench-2026-08-30-D-B-*.jpg`, and **every one of those photographs is of a grey panel**,
where the value slot holds a reason of ≤ 8 characters. `InFlight` is, by that same decision, a
**live-palette** state. That is §9's 2026-08-30 observation-window rule with a palette in place of a
clock.

**Reported, not fixed.** Every way out is a rendering decision — trim trailing zeros at an 8-decimal
venue, re-rank the marker above the age during a fetch, or give it pixels of its own — and D-B took all
four rendering decisions while this stage was told to take none. The arithmetic is pinned in
`test_ladder_render.cpp` and **that case is written to FAIL when the value slot is fixed**. Meanwhile
the board reports the mechanism on a new `-- reseed :` serial line, so a bench can read it even while
the panel says nothing. *Note for whoever takes D11: option (a) also restores the **age**, which has
been undrawable on this build since M4 stage D for the same reason and which nobody had noticed.*

#### 5 · The ~11 minute window: LEFT, and strain 29's tripwire does not fire

**Left alone, and that is a decision recorded rather than a deferral assumed.** Leaving it needs no
rendering decision: the panel already draws `-` through `AgeText::unknown()`, settled at M4 stage D
(A4) and undisturbed by D-B, and `test_binance_adapter.cpp` already pins the 638.8 s as a behaviour.
Nothing in this stage's work touches it.

**The tripwire — *"if any stage before the close-out needs to quote or re-pin a Binance cadence figure,
this closes first"* — does not fire, and this is why rather than an assertion that it does not.** The
mechanism and the marker are cadence-independent: neither derives a constant from a Binance interval.
This stage quotes exactly one median — the **fetch duration**, 3,973 ms — and it is not a cadence, it
does not come from `trace.cpp`'s statistics pass (the code path card 29 is actually about), it re-pins
no `taxonomy_pins.inc` figure, and it was computed by the **lower median at nearest rank** that
`sample_window.hpp` mandates, stated as such at the point of use.

**And addressing the window WOULD have fired it**, which is the useful half of this answer: the window
is 32 baseline intervals at the venue's ping cadence, so any change to it must quote that cadence —
the exact figure card 29 says has been taken from the wrong home in five places. The tripwire made
that cost visible before it was paid, which is what it is for. Card 29 stays open for the close-out.

#### 5b · The self-review, which found a quiet stall

The owner's `code-review` skill was run twice. **The first fan-out failed outright** — all five
reviewers hit a subagent session limit and returned nothing — so this section was written as a
self-review against the same priorities. **The second run completed, and §5c is what it found**;
this section is kept because it is what a self-review was worth on its own, which turns out to be
something but not enough.

**A REST body that does not parse used to strand the hold for ever.** `on_rest_body` has two early
returns — a parse failure, and a well-formed body with no `lastUpdateId` — and neither released the
hold. The consequence is not a lost re-seed, it is a **stall**: `reseed_wanted_` was cleared at
issue, so with the hold still open the schedule sees nothing wanted, `SeedSchedule::due_in_us`
returns −1, and **nothing ever asks again**. The hold would go on collecting every diff until it
overflowed minutes later, and `reseed_holds_overflowed` would attribute that to the buffer. Both
paths now call `on_reseed_abandoned()`, `on_rest_missing()` does the same on a seeded book, and
three malformed bodies plus the no-body case are tested.

**And three DRY repairs, two of which had a failure mode rather than only a smell.**

- `hold_for_reseed` had copied `buffer_diff`'s admission test and its fifteen-line append. The two
  paths write one array that **one** `replay_buffer` arithmetic reads back, so an index written one
  way and read the other is a silently wrong book rather than a build error. Now `hold_has_room()`
  and `append_held_event()`, with **the overflow policy the only thing left at each call site** —
  which is the one thing that genuinely differs.
- The three-way `ReseedState` choice was written out in **both** `FeedTask::publish_current()` and
  `replay_driver.cpp`'s `stamp_reseed`, with nothing but prose holding them together. It is now
  `reseed_state_for(holding, wanted)` in `display_snapshot.hpp` — arithmetic over two booleans, so
  it takes no transport into `engine/` — called from both, and pinned by its own case. **A state
  the two spelled differently would have been a state no golden could pin.**
- `adopt_reseed` had its own copy of `replay_buffer`'s survivor scan and bracket test. Both are now
  `first_survivor_after()` and `brackets()`. A bracket implemented twice is a bracket that can
  disagree with itself, which is precisely the defect §2 above records one level down.

Seven test cases opened with the same four setup lines; that is now `seeded_live_book()`.

#### 5c · What the INDEPENDENT review found, and it was the important one

**Two defects, both in code this session wrote, both reproduced before being believed** — I turned
each finding into a failing test first, watched it fail with the reviewer's stated symptom, then
fixed it. Neither would have been caught by the suite as it stood.

**(i) `adopt_reseed` reintroduced, at a different site, the exact defect §2 above records this
stage fixing.** With no held survivor, nothing from the feed is applied on top of the new baseline —
so the next message is the FIRST event after a snapshot and the venue's rule for it is
`U <= L + 1 <= u`. Setting `bracket_checked_ = true` there put it through the strict
`U == last_u + 1` instead. Measured symptom: body at 1025, the pending message `[1011,1030]` lands,
`seq_breaks = 1`, `Gap` raised, `has_baseline()` false. **A live, correct book greyed one message
after the re-seed "succeeded"** — which is what the whole stage exists to prevent, and it is the
`resnapshots_adoptable` case B2 measured at 13 of 13 on the quiet pair, so it is the *common* path
there rather than an edge.

The fix is not a patched flag: `adopt_reseed` now checks the bracket, installs the ladder, clears
`bracket_checked_` and calls **`replay_buffer`** — the one implementation of the venue's procedure.
Delegating removes the possibility instead of fixing an instance, and it closes the reviewer's third
finding for free (`publish_seed` keeps exactly the two callers its own comment says it may have,
where the hand-rolled version was a third with a weaker precondition).

**The uncomfortable part, and the reason this is worth a §9 row rather than a line here: the fix and
the reintroduction were written in the same session, by the same reader, hours apart.** §2's whole
subject is that a bracket and a continuity check are different conditions — and the new code then
conflated them again. A rule understood is not a rule enforced; what enforces it is that there is
now one function that may decide a ladder has been corroborated.

**(ii) The venue's FIRST procedure clause — *drop what the snapshot already contains* — existed in
this file only for BUFFERED events.** A message arriving after a baseline and lying wholly inside it
(`u <= lastUpdateId`) failed the bracket, because `u <= L` makes `L + 1 <= u` false, and the answer
to a failed bracket is `drop_book`. Reachable whenever the body names an instant the socket has not
reached. **Latent on the initial-seed path since B1**, where it costs a wasted seed over a panel that
is already grey; on this stage's re-seed path it destroys a live book, which is what makes it this
stage's to close. `check_continuity` now returns whether the frame should be applied — the second
half matters, because applying a contained frame would have *rewound* `last_u_` below the body's own
instant, which no `Gap` would have made obvious — and counts it as `diffs_inside_baseline`.

**That fourth counter is what the headroom pin is for.** It moved `sizeof(BinanceAdapter)` by 8 B,
the pin fired, and the number was **re-measured (45,584 B) rather than adjusted to fit** — which is
the behaviour the pin was added for, exercised on its first real occasion, in the same stage.

Two further findings were taken as read: the ledger must not count a re-seed that ended in a `Gap` as
`adopted` (guarded), and the no-survivor publish path (closed by the delegation above).

#### 5d · The owner's FIFTH rendering decision, taken at the split and implemented here

**Not a defect fix and not this stage's to take** — recorded as what it is. On 2026-09-06, after
§4 measured that D-B's marker could not be drawn at a venue whose declared scale is eight
decimals, the owner decided: **the header trims trailing zeros from the value.** D-A4 implemented
it and did not re-decide it. It reopens none of D-B's four — `Ink::Symbol`, the symbol's slot,
VALUE > AGE > SYMBOL all stand; only the value's width changes. The brief's *"No new rendering
decisions"* header is left standing with the fifth noted beside it. ARCHITECTURE §9, 2026-09-06.

**The implementation constraint was the one that could have been got wrong quietly, and it is
sharper than "shared with Anvil and Kraken".** `format_scaled` has nine call sites and one of them
is **`kraken_checksum.hpp`**, which formats levels to build the string **the venue's CRC32 is
computed over**. Trimming there would not misalign a column — it would silently break Kraken's
only external oracle, on the one venue whose decimals are meaningful in the strongest sense. So the
formatter's contract is untouched and the trim happens once, at the display edge, in
`TextField::price` — a separate factory from `TextField::scaled`, so the symbol id cannot acquire
the behaviour by being one argument away from it.

**Stated as a test, as the decision required.** The property is *trimming removes only characters a
re-parse at the same scale restores*: `parse_scaled(trim(format_scaled(v, d)), d) == v`, swept over
**nine scales × fourteen values, 540 assertions**. That is "trailing zeros only, never significant
digits" mechanically rather than promised. Plus the concrete half: Anvil's `10.0001` and `9.9972`
come back **byte-identical**, and an integer's trailing zeros survive (`100` at `decimals == 0`,
which is the symbol id's path through the same formatter).

**IT WORKED AND IT WAS NOT ENOUGH, and the second half is the part worth reading.** The value went
74 px → 44 px. The marker needs `left_limit ≥ 34` and the widest header this project renders
now leaves **31** — three pixels short:

| header | value | `left_limit` | `RESEED` (needs 34) |
| --- | --- | ---: | --- |
| BTCUSDT `108234.56`, age showing | 44 px | 20 | no |
| BTCUSDT `108234.56`, no reading | 44 px | 11 | no |
| BTCUSDT `108234.5`, age showing | 39 px | 1 | no |
| ATOMEUR `4.321`, age showing | 24 px | 16 | no |
| ATOMEUR `4.321`, no reading | 24 px | **31** | no |
| grey `NO LINK`, no reading | 34 px | 21 | no |

So the fifth decision alone left **D11 open with new arithmetic**, which is the branch it named — and the owner took the sixth the same day. See §5e.

**What it DID buy, and it is narrower than this stage first said: the `-` PLACEHOLDER.** The claim
here was that the trim "bought back the age", and **the table above contradicted it** — the
age-showing row reads `left_limit` 20, which by `draw_header` means the age *yielded*; had it drawn,
`left_limit` would be `age_x`, strictly below `value_x`. What the trim actually bought is 9 px
spent saying **NO READING**, where before the value clamped `value_x` to 0 and nothing left of it
could draw at all.

**And a real reading never fits, which is sharper still.** `AgeText` has no three-character form:
`"%u.%us"` is four at its shortest (`0.0s`, 19 px) and the minute and hour forms are five — so a
reading needs 24 px against the 20 a live Binance header offers, in any priority order. Only the
4 px dash fits. `age_ms` remains structurally undrawable **as a number** on this build, which has
been true since M4 stage D and which nobody had noticed until this stage measured the header for the
marker's sake.

**The interaction is worth stating where someone meets it:** that dash is what the panel shows for
the whole of the age estimator's baseline window — **639 s, ~11 minutes, on every Binance
connection** — so under the old priority it cost 9 px and pushed the marker out **at exactly the
time a re-seed is most likely**, the first eleven minutes of a connection when the seed is most
recently taken. That is what the sixth decision re-ranks away.

**And the tripwire I built for this did not fire, which is its own finding.** §4 said
`test_ladder_render.cpp` held a case *"written to FAIL when the value slot is fixed"*. The value
slot was fixed and it passed, because four of its five assertions pinned **`format_scaled`'s
output** — which the decision deliberately does not touch — and the fifth asserted the marker is
invisible, which it still is. **A test that announces it will fail and then cannot is worse than no
tripwire**, and it is the reassuring-instrument shape §9 keeps recording. It is retired in place
with that note, and the real signal is now `left_limit < 34` asserted over the six widths above,
which fails the moment a marker that fits is chosen.

#### 5e · The owner's SIXTH rendering decision, which is what made the marker appear

**Taken at the split on 2026-09-06, after §5d measured that the fifth decision alone was not
enough.** While a re-seed fetch is in flight the marker outranks the age: the standing priority
**VALUE > AGE > SYMBOL**, in force since M4 stage D, becomes **VALUE > MARKER > AGE > SYMBOL** for
the duration of a fetch and reverts the moment it ends. Implemented, not re-decided. It **amends a
priority standing since M4 stage D**, scoped to a fetch in flight and to nothing else — `Wanted`
and `None` are the standing order, asserted as such. D-B's four are otherwise untouched.

**A SHORTER MARKER WAS RULED OUT, AND THE ARITHMETIC IS THE ARGUMENT.** Allocated last, with
`value_x` at 20: an age of 4+ characters yields leaving 20, an age of 3 draws leaving 1, no reading
leaves 11. **So a marker short enough to fit would have appeared only when the age was too wide to
draw itself** — its presence would encode the age's width rather than a re-seed, which is worse
than not drawing at all. The slot was never the problem; the order was. (The middle case is
unreachable anyway: `AgeText` has no three-character form.)

**The text is `GET`**, three characters, 14 px, needing 19 against the 20 available. Not a
contraction of "reseed" — `"RSD"` is three characters a reader must be told the meaning of, while
`GET` is what is actually outstanding (`GET /api/v3/depth`) and correlates with the
`rest: fetch … HTTP 200` serial line. And it deliberately stays **out of `reason_text`'s
vocabulary**: every word in the value slot says something is wrong, and decision 2's whole premise
is that a re-seed fires on a book that has not gone wrong, so a marker reading as a fault word would
state the opposite of the decision that put it there.

**MEASURED: the marker now draws on every header this project renders**, including the widest live
one — BTCUSDT `108234.56`, `value_x` 20. **ROADMAP D11 closes.** The trade is visible and intended:
where the age was drawing, it yields for the length of a fetch. And the case the re-rank was for is
the `-` window — 639 s, ~11 minutes, on every Binance connection — where under the old order the
dash pushed the marker out at exactly the time a re-seed is most likely.

The tripwire is now the other way round: `test_ladder_render.cpp` asserts the marker DOES draw in
each configuration, so a future header change that squeezes it out fails the suite.

#### 6 · Verification, per §3, naming the track for each commit

Both tracks were run on the whole tree; the per-commit ladder runs when the split is approved.

- **ctest** `cmake --workflow --preset host-mingw` — **52/52**. Covers `engine/`, `harness/`,
  `tools/`, and `ladder_render.hpp` (via `test_ladder_render.cpp`).
- **AND `engine/` IS NOT A CTEST-ONLY CONCERN AT THIS VENUE**, which is where the first draft of the
  split below got a track wrong. `venue_build.hpp:109` includes `binance_adapter.hpp` under
  `DC_VENUE_BINANCE`, and `venue_build.hpp` reaches `main.cpp` and seven firmware headers — so
  **`pio` compiles the adapter too**, on a different compiler generation. Any commit touching it
  spans both tracks. `dc_engine_target_check` does compile that header on xtensa *on this desk*, but
  it is not a substitute: it compiles it **standalone**, without `DC_VENUE` or the firmware's flags,
  and it **skips silently** where no toolchain is installed. Recorded in §9 as a correction to the
  2026-08-30 row, which had only ever stated the boundary in the firmware-to-host direction.
- **`pio run`** — `depthcharge-binance` **SUCCESS** (RAM 42.7%, 140,080 B static), and
  `depthcharge` and `depthcharge-kraken` **SUCCESS** as controls, since `venue_budget.hpp` and
  `render_task.cpp` compile on all three.
- **`venue_budget.hpp` is `pio`-only and worse than un-included — it is not host-compilable**, so its
  assertions are invisible to a green ctest. Named in §9 as a correction to the 2026-08-30 row, whose
  "sixteen headers" figure is also wrong: **17 direct / 22 transitive**, and it was wrong when written
  (it missed `rx_budget.hpp`, included by relative path since 2026-08-15).

#### 8 · The ladder, and the commit it found red

Eight commits, each verified in its own **detached worktree** with `secrets.h` copied in and
`CMAKE_HOME_DIRECTORY` read back from `build/host/CMakeCache.txt` before any pass was believed.
`n/a` below is a track that compiles nothing the commit touches — not a track skipped.

| # | commit | ctest | `pio -e depthcharge-binance` | home dir |
| --- | --- | --- | --- | --- |
| 1 | `8c2df4a` engine: the bracketing event is not the continuity check's business | 52/52 | SUCCESS | confirmed |
| 2 | `e9f15fe` engine: hold the fetch's interval and roll the body forward across it | 52/52 | SUCCESS | confirmed |
| 3 | `1a7cf0c` harness: the driver issues the fetch a trace cannot | 52/52 | n/a | confirmed |
| 4 | `d8490b8` firmware: the re-seed ledger the panel cannot show | n/a | SUCCESS | n/a |
| 4b | `224efc7` firmware: the header trims trailing zeros — the fifth decision | 52/52 | SUCCESS | confirmed |
| 5 | `e944158` firmware: the marker, and the sixth decision that makes it visible | 52/52 | SUCCESS | confirmed |
| 6 | `c61de28` firmware: pin the venue headroom | n/a | SUCCESS | n/a |
| 7 | `ec80ec4` docs: strain 28 closes, D11 opens and closes, five §9 rows | 52/52 | SUCCESS | confirmed |

**AND IT CAUGHT ONE, WHICH IS THE ONLY REASON THIS SECTION IS WORTH READING.** Commit 2's first
form was **RED in isolation — 10 of 52** — on
`test_binance_adapter.cpp: 'struct dc::harness::ReplayOptions' has no member named
'issue_reseed_fetch'`. The synthesised-trace test had been put in the commit that builds the
mechanism, and the field it needs is born in the commit after it. **Green in the main tree, red
alone**: exactly the shape §9's 2026-08-30 row is about. The split was **amended** — the test moved
to commit 3, whose message records why — rather than the verification being waved through.

**Three record defects were found the same way, by re-reading rather than by any test.** The marker
comment in `ladder_render.hpp` carried both the superseded `"RESEED"` rationale and the new one;
ROADMAP **D11** said CLOSED in its heading and *"STAYS OPEN"* in a bullet, still quoting `"RESEED"`
and 34 px; and this log still listed D11 as an open next step. All three are the same species as the
defects §5c records — a document that has been half-updated reads as though it were whole — and
they cost commits 5–7 being rebuilt and re-laddered. **Prose has no compiler, which is the argument
for putting the arithmetic in a test wherever it can go.**

#### 6b · What has to be true of the board for the mechanism to run at all

**Asked at the split, and it is the right question: if the state cannot be reached deliberately then
`adopted = 0` at a bench confirms nothing.** Derived from the code, not estimated.

**THE ONLY ENTRY POINT IS THE COVERAGE TRIGGER.** `reseed_holding_` is set only by
`on_reseed_issued()`, and only when `seed_ == Seeded`; the transport calls that only on
`SeedAction::Issue`, which needs `wanted`; and on a seeded book `wanted` is `reseed_wanted()`. Every
writer of `reseed_wanted_` was enumerated: `drop_book` and `buffer_diff` leave the book `Unseeded`,
`on_rest_missing` raises only while `Unseeded`, and `on_reseed_abandoned` / `hold_for_reseed` /
`adopt_reseed` all require a hold already open. **`note_depth`'s cover trigger is the sole way in.**

So the board must satisfy, simultaneously:

1. **A seed that ARMS the trigger** — `limit=1000` returning at least
   `kBinanceReseedCoverLevels` (448) levels on **both** sides. Below that the trigger is never armed
   and `seeds_below_margin` counts it instead. (ATOMEUR cannot: its whole book is ~16 bids.)
2. **~552 levels of seeded coverage consumed on one side** — from ~1,000 down to 447.
3. **No `drop_book` anywhere in between** — no `SeqGap`, no socket drop, no overflow. This is the
   binding constraint.

**HOW LONG (2) TAKES, bounded from the two measurements in the tree.** B2's worst 15 s coverage loss
is 168 levels, so 552 is **~50 s** at a sustained worst-case burst; the mean walk gives a
`limit=1000` seed's $224.52 range against BTCUSDT's $0.33/s, so 55% of it is **~375 s**. The truth is
between, and closer to the second.

**HOW LONG THE BOOK LIVES, measured off the two committed soaks:**

| run | mean live stretch | median | p90 | >= 375 s |
| --- | ---: | ---: | ---: | ---: |
| D-C (2026-08-30, pre-stage-E) | **6.6 s** | — | — | — |
| stage E (2026-09-04) | **67.2 s** | 40 s | 180 s | **52 of 1,468 (3.5%)** |

**So the answer changed under this stage's feet, and both halves matter.** On the D-C-era board the
state was **unreachable** — mean book life 6.6 s against a 50 s floor — and `adopted = 0` there
would have confirmed nothing. **Stage E's publish boundary is what made it reachable**: the same
board now runs a median 40 s live and produced 52 stretches over 375 s in 32.25 h, with a maximum of
1,592 s. It is a TAIL phenomenon: **a short sitting will see nothing; a multi-hour soak has dozens of
chances.**

**THE SITTING MUST THEREFORE BE A SOAK, NOT A SITTING** — and that is D-C's second run, which is
where this belongs anyway.

**AND THE LINE WAS BLIND TO THE DIFFERENCE UNTIL NOW.** `min_bid_cover` / `min_ask_cover` are
tracked by the adapter and were printed nowhere, so a run could not say whether the trigger was
approached or nowhere near. `-- reseed :` now carries `cover=B/A of 448` and `below=`, which splits
`adopted = 0` into *never armed*, *never approached*, *came close* and *ran and failed* — only the
last of which is a verdict on the mechanism. Without it the owner's objection stands in full.

#### 7 · Exact next step

1. ~~Review the split and approve or amend it.~~ **Done 2026-09-06** — approved as re-proposed with
   4b inserted, and amended once during execution when the ladder found commit 2 red (§8).
2. ~~Create the commits, verify each in a detached worktree, push.~~ **Done** — eight commits,
   `origin/m5/stage-d-a4` = `ec80ec4`. **`master` is NOT fast-forwarded** and stays at `25510ec`
   until the owner publishes it, which is a separate deliberate act.
3. **The one thing this session could not do: flash it — and it must be a SOAK rather than a
   sitting.** §6b derives why: the coverage trigger is the only route to a re-seed on a live book,
   it needs ~552 levels of coverage consumed with no `drop_book` in between, and only 3.5% of stage
   E's live stretches are long enough. Read `cover=B/A of 448` and `below=` on the `-- reseed :`
   line before reading `adopted`; `adopted=0` with `cover` far above 448 is a run that was too
   short, not a mechanism that failed. `DisplaySnapshot::reseed` reaching
   `InFlight` *on the board* is unverified, and so is the `-- reseed :` line. The bench reading that
   settles it is `adopted` climbing while `greys` stays flat. Note that on the D-C capture **every**
   re-seed followed a `drop_book` (`resync_req=643`, `grey_n=642`, `seqbreak=641`, `no_slot=4708`) —
   the pipe's four slots, i.e. card 28's *other* half — so on a board in that state the mechanism is a
   no-op and `adopted` will read 0. **That is not a defect in the mechanism and must not be read as
   one**; it means the cover trigger, which is the only path to a re-seed on a *live* book, was never
   reached. D-C's second run is where both are read.
4. **D11 is closed** — the owner took the fifth and sixth rendering decisions at the split and the
   marker now draws. What remains of that thread is a note rather than a card: **`age_ms` is still
   undrawable as a NUMBER** on this build (a real reading needs 24 px against 20), which has been
   true since M4 stage D and which this stage found only by measuring the header.

