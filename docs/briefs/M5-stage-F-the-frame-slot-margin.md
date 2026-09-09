# M5 Stage F — the frame-slot margin, and what may be claimed for a closed constant

**Track:** Host [engine + harness; no bench sitting required] · **Status:** ☐ **Open — brief is a
stub, written 2026-09-10 at D-C run 2's close. THE BRIEF ITSELF IS THE FIRST DELIVERABLE.** ·
**Size:** unknown until §2 is answered; the measurement half is an evening
**Written:** 2026-09-10 by the D-C run 2 seat, from check 4's third reading.
**Milestone assignment:** **OWED — see §6.** This is a stage rather than a backlog card because it
has a falsifiable question, a measurable answer and a decision at the end of it; it has no milestone
because M5 closed on its own definition of done and this is not part of that definition.

**Depends on:** nothing. **Blocks:** nothing, and that is deliberate — see §5.

---

## 1 · The measurement, and it is a trend rather than a number

`kFrameCapacity` is **65,536 B**. Three populations have now measured the largest message that
actually arrived, and the margin has moved one way each time:

| population | largest accepted | margin | oversize rate |
| --- | --- | --- | --- |
| corpus, 3,119 messages (M5 stage D-A1, at the old 16,384 B slot) | 28,639 B | **2.29×** | 13 of 3,119 = **0.417%** |
| D-C run 1, 1,188,879 published frames | 61,823 B | **1.060×** | 1 = 0.000084% |
| **D-C run 2, 2,038,036 published frames** | **65,220 B** | **1.0048×** | 1 = **0.000049%** |

**Both columns are moving, and they are moving in opposite directions.** The overflow *rate* keeps
vindicating the sizing decision — 0.417% at the old slot became one message in two million. The
*margin* has fallen from 2.29× to under half a percent.

> **AND THE MARGIN IS BEST READ IN BYTES, WHICH IS WHY THIS IS A STAGE.** 65,220 B of 65,536 is
> **316 bytes** — under half a percent, and less than one extra price level on a busy side. A
> ratio makes 1.0048× look like a rounding difference; the byte figure is what a reader should
> carry. **And one oversize event has already fired**, so this is not a margin that has held: it is
> a margin that has been crossed once and is now 316 B from being crossed again.

## 2 · The question, and it is not "should the constant change"

> **§9 keeps the sizing CLOSED and this stage does not re-open it.** D-A2 closed it; D-C run 1
> confirmed and was told in terms that confirmation is not re-opening; run 2 is a third
> confirmation of the same decision. **Nothing here argues for a bigger slot.**

The question is what may be **claimed** for the constant:

> **Is 1.0048× a margin, or is it a bound computed over a population that could not contain its own
> worst case?**

That phrase is not invented here. `docs/DESIGN.html` card 29 already applies it to two other figures
in this milestone, and D-C's own §4.2 records a third — stage C's *"clears the jitter by 1.99×"*,
computed over ten intervals spanning 111 ms and superseded at 1.024× once 6,183 intervals existed.
**Each of those was a bound that looked comfortable until the population grew.** This is the same
shape with a longer lever: every increase in observation has moved this margin toward the cap, and
no observation has ever moved it away.

**The falsifier this stage needs is a distribution, not another soak.** A fourth run would produce a
fourth point on the same curve and settle nothing. What settles it is knowing whether Binance's
`depthUpdate` message size has a **ceiling** the venue enforces, or a tail that keeps going.

## 3 · What the answer changes

- **If the venue caps it** — a documented or measurable maximum below 65,536 B — then 1.0048× is a
  margin against a bounded quantity, the constant is right, and the reading stops being alarming.
  The deliverable is the citation and a `static_assert` or a comment that pins the reasoning where
  the constant lives.
- **If it does not** — the tail is open and the margin is a sample maximum, not a bound. Then the
  honest options are a bigger slot, a documented and *counted* truncation policy, or a statement at
  the constant that the project accepts a known unbounded risk at a measured rate. **All three are
  decisions, and none of them is this stage's to take alone.**

> **AND "JUST MAKE THE SLOT BIGGER" IS THE OPTION D-A1 ALREADY PRICED AND FOUND EXPENSIVE — which
> is the whole reason this is a stage and not a list line.** `kFrameCapacity` sits in a fixed
> budget that `firmware/src/venue_budget.hpp` asserts at compile time so the panel cannot be lost
> at boot. D-A1's own first lever was **void** — a projected +3,488 B clearance was really a
> **12,920 B deficit** — and it was covered only by cutting `kReserveInternalBytes` 96 → 80 KiB,
> which D-A2 then had to raise again to 104 KiB. The frame pipe's slabs are already in PSRAM.
> **So raising the slot is a budget change with a chain of consequences, each of which has already
> bitten this project once**, and it interacts with the four-slot occupancy constraint that three
> soaks have now found sustained at `max_held = 4 of 4`. A sizing decision here moves memory that
> other decisions are pinned against.

## 4 · Where the evidence already is

| source | what it holds |
| --- | --- |
| `hardware/bench-2026-09-07-D-C-run2-soak.md` §4 check 4 | the 1.0048× reading, in context |
| `hardware/bench-2026-08-30-D-C-soak.md` | run 1's 1.060× and its ruling |
| `harness/replay/NOTES-binance.md` | the corpus distribution: p50 607, p90 2,399, p99 11,935, p99.9 23,391 |
| `docs/DESIGN.html` card 28 | the pipe's sizing argument as it stands |
| `engine/…/frame_pipe.hpp` | the constant, and run 1's ruling recorded beside it |

**The corpus percentiles are the useful starting point and they are 3,119 messages old.** The board
has since published 3.2 million frames across two soaks whose captures are committed; the
distribution can be rebuilt from those without any new bench time. **That rebuild is the first
measurement this stage should make**, because it converts "the margin is shrinking" from three
points into a shape.

> **AND THE RAW IS NEEDED FOR IT, WHICH IS WHY THIS IS WRITTEN DOWN HERE.** Run 2 is committed as a
> **derived** event log; the per-frame lines were dropped. `-- size` is retained, so the *maximum*
> survives — but a full size distribution needs the raw, archived at
> `C:\local\depthcharge-archive\bench-2026-09-07-D-C-run2-soak-RAW.log.gz`, inflated sha256
> `edc9c930…`. Check it against that digest before deriving anything from it.

## 5 · Why this blocks nothing

**The board is not at risk while this is open.** One message in two million is declined, the
decline is defined behaviour — the next diff fails `U == last_u + 1`, the board takes
`Gap{SeqGap}`, greys and re-seeds — and D-C run 2 measured that path working 2,942 times for other
reasons without a stale ladder. **The failure mode is honest and already exercised.** What is at
risk is the *claim*, and claims are what §9 exists for.

## 6 · The one thing this brief cannot decide: which milestone it belongs to

**M5 closed on its own definition of done, and this is not one of its clauses.** M6 and M7 are
B-track hardware. So this stage has a brief, a question and no milestone, and **inventing one is
the owner's call, not an executor's.** Three options, stated so the decision is one word:

1. **A stage of the next A-track milestone**, whenever that is drawn — the tidiest, and it means
   this brief waits.
2. **A standalone stage**, executed when someone has an evening, with the ROADMAP carrying it
   outside the milestone table.
3. **Fold it into whatever brief next touches `frame_pipe.hpp`**, on the grounds that a constant's
   claim is best revisited by the person already in that file.

**Until that is ruled, this brief is a stub and should not be executed** — a stage with no milestone
has no definition of done to be measured against, which is the failure this project has spent M5
recording in every other form.

## 7 · Out of scope

Re-opening the **sizing**. Changing `kFrameCapacity`. Anything in `firmware/`. The frame-pipe
**occupancy** constraint — `max_held = 4 of 4` sustained, three runs running — which is DESIGN card
28's other half and is contraindicated in both directions (fewer slots by strain 27, more by
memory); it is a separate question and it is **not** improved by anything here.

## Session log

<!-- Append one block per session: date · model · done · decisions with why · exact next step. -->

### 2026-09-10 · Opus 5 · stub written, nothing measured

**Done.** This brief exists and nothing else. Opened at the owner's instruction as a **stage needing
a brief** rather than a backlog card, because check 4's reading has a falsifiable question and a
decision at the end of it, which is more than a card carries.

**Exact next step.** **Rule §6 first** — which milestone, or standalone. Then the measurement in §4:
rebuild the message-size distribution from the two committed soak captures and the archived raw,
and find out whether the tail has a ceiling. Do not touch the constant.
