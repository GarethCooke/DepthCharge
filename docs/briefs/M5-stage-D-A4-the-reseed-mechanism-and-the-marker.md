# M5 Stage D-A4 — the re-seed mechanism, `InFlight`, and the marker

**Track:** Agentic [desk] · **Status:** Not started · **Size:** one desk evening — **and it is meant
to be spent while the soak is running**
**Written:** 2026-08-30 by the desk seat, splitting D-A3 on wall-clock grounds.

**This stage exists because of the calendar, not the code.** D-A3 was scoped as the ping wire plus
the re-seed mechanism plus the soak instrumentation. Only the wire and the instruments unblock D-C —
**all six of D-C's named checks are satisfiable without anything in this brief** — so the mechanism
was split out to run **concurrently with the 24-hour soak** rather than in front of it. The soak is
the only remaining item in M5 whose duration this project cannot compress; everything that can be
moved off its critical path should be.

**It therefore blocks nothing that is waiting.** Strain 28 stays open until this lands, and the M5
close-out wants it, but no bench sitting is idle for it.

**Read first**

| Source | Why |
| --- | --- |
| `M5-stage-D-A3-…md` **§2** | **The ruling.** The re-seed choice is **(a) or (c)**; (b) is closed. **Do not re-derive it** — and see below for what reopening it would cost. |
| `M5-stage-C-…md` § *Owed by stage D*, **row 2** | The three candidates, priced, and B2's adoptability measurement that decides how often the choice matters. |
| `M5-stage-D-B-…md`, decision **2** | What `InFlight` draws and the four constraints on the marker. **Implement, do not re-decide.** |
| `docs/DESIGN.html` strain 28 | The card this stage closes; C's half is already done. |
| `ARCHITECTURE.md` §6 invariant **#7** | The mechanism allocates. Whichever candidate wins, the footprint is stated and asserted. |

**Depends on:** D-B ✅ (`05e05d6`) — decision 2 is a binding input — and D-A3 for the ruling it
records. **Does NOT depend on D-C, and does not block it.** **Blocks:** strain 28's closure and the
M5 close-out.

---

## The ruling this stage inherits

D-A3 §2 settles it, and it is restated here only so that a session reading this brief alone does not
reach for stage C's table and find (b) the cheapest row on the page:

> **The re-seed mechanism is (a) or (c). (b) is CLOSED** — not on cost or taste, but because
> stage C priced (b) as *"greys the panel for the length of a fetch on a book that was still
> correct"* and D-B's decision 2 refused exactly that. (b) drops the book, so the panel greys;
> decision 2 says it stays coloured. **Reopening (b) means reopening decision 2, deliberately and
> first.**

| | candidate | cost | status |
| --- | --- | --- | --- |
| **(a)** | a ~128 KiB deferred buffer | no gap, no grey — more than doubles the adapter's ~96 KiB of fixed state | **open** |
| **(b)** | drop-gap-reseed | free in memory; greys a correct book for the length of a fetch | **closed by ruling** |
| **(c)** | merge below the touch | cheapest, least proven — **nothing in the corpus exercises it** | **open** |

**How often it matters:** B2 measured **0 of 7 adoptable at `limit=1000`** on the liquid pair and
**19 of 19 everywhere else** — the deeper the seed the older it lands, because the venue snapshots
about three-quarters of the way through the round trip and spends the rest shipping the body. A
fetch measures **3,143–4,485 ms** on this board, body **64,046 B** every time. A re-seed is not rare
at the shipped depth, which is what makes this a mechanism rather than an edge case.

## 1 · Deliverables

**Deliverable 1 · Build the chosen mechanism**, (a) or (c), with the reason recorded. Its memory
cost is stated against **invariant #7** the way D-A1 and D-A2 both stated theirs, and asserted where
`venue_budget.hpp` asserts the rest — a footprint that is argued in a log and not checked by the
build is the thing D-A1's guard exists to prevent. **If (c) is chosen, the trace that exercises it
is part of the deliverable**, not a follow-up: *nothing in the corpus exercises it* is stage C's
own description, and "least proven" stops being acceptable the moment it is also "shipped".

**Deliverable 2 · Advance `DisplaySnapshot::reseed` to `InFlight`.** The state exists — stage C put
it in the four pad bytes the struct already carried, verified on host *and* xtensa — and D-A2 left
it **deliberately unreachable** so that an open card would be visible on the panel rather than only
in a report line. This is the field that advances. **`sizeof(DisplaySnapshot)` stays 1,168** and
`sizeof(SnapshotChannel)` stays 3,528; ROADMAP **D6** is not being done here.

**Deliverable 3 · The header marker.** D-B pinned it and handed constraints rather than a choice:

- The marker takes the **symbol's slot**, under the standing priority **VALUE > AGE > SYMBOL**.
- **At most eight characters**, asserted against the real header width in `test_ladder_render.cpp`
  the way every `reason_text` string already is.
- It **yields exactly as the symbol yields today**, so a header too narrow drops the marker rather
  than overlapping the price or the age.
- Drawn in **`Ink::Symbol`** — the slot's ink, not a new one. A new `Ink` costs a stale-palette
  entry and a `static_assert`, for a marker that never appears on a grey panel.

On `InFlight` the **live palette stays selected and every ladder `Ink` is unchanged**; the only
difference from `None` is inside `draw_header`. Grey is not used.

## 2 · Constraints

- **Invariant #7 governs deliverable 1**, and the two candidates sit on opposite sides of it: (a) is
  ~128 KiB of fixed state against an adapter already holding ~96 KiB, while (c) allocates nothing
  and pays in unproven behaviour instead. Whichever wins, the cost is stated in the same terms.
- **`engine/` stays host-buildable.** The mechanism's engine half must not acquire a transport.
- **Nothing branches on `reseed`** (invariant #5). It is a state the renderer may draw, never a rule
  the engine applies — the same standing constraint `age_ms` carries.
- **This stage touches `firmware/` and `engine/`**, so per `ARCHITECTURE.md` §9 (2026-08-30) ctest
  verifies the engine half only — each firmware commit also needs `pio run -e depthcharge-binance`
  in its worktree, with `secrets.h` copied in first.
- **Do not read the soak.** If it is running while this stage is worked, its log is D-C's and a
  finding taken from it here is a finding taken without D-C's instrument repairs.

## 3 · Known unknowns — resolve and record

- **(a) or (c).** The ruling narrows it to two and does not choose. (a) is proven and expensive;
  (c) is cheap and has never run. The memory figure is the honest axis: state what (a) costs against
  the current footprint rather than against D-A1's, which two stages have since moved.
- **What the header shows during the ~11 minute age no-reading window.** Raised in D-A3 because
  wiring the ping makes it visible; if it turns out to need a rendering decision it is the owner's,
  and this stage is where such a decision would be implemented rather than taken.
- **Whether `InFlight` is reachable often enough to be seen at the bench.** B2 says a re-seed is not
  rare at `limit=1000`, but "not rare in a capture" and "visible on a panel for 3–5 s" are different
  claims. If it cannot be seen, say so — an unreachable state was the *point* before this stage and
  becomes a defect after it.

## 4 · Definition of done

- ☐ The mechanism is built, its candidate chosen **with the reason recorded**, and its memory stated
      against invariant #7 and asserted in `venue_budget.hpp`.
- ☐ If (c): a trace that exercises it is committed, with a golden.
- ☐ `DisplaySnapshot::reseed` reaches `InFlight` on the board; `sizeof` still **1,168 / 3,528**.
- ☐ The header marker renders per D-B's four constraints, with the width assertion in
      `test_ladder_render.cpp`.
- ☐ **Strain 28 closed** — the mechanism and the memory were its D-half.
- ☐ Any decision with architectural weight to `ARCHITECTURE.md` §9; `docs/DESIGN.html` updated where
      the card moves.
- ☐ ctest green **and** `pio run -e depthcharge-binance` green in a worktree for every firmware
      commit; session log · ROADMAP; split proposed; nothing committed until approved.

## 5 · Out of scope

**The ping wire, the policy routing, and the soak instrumentation — D-A3**, and this stage must not
wait for them. **The soak — D-C.** Every rendering decision — **D-B**, closed; decision 2 is an
input here and is not reopened by this stage. Reopening candidate (b) — the owner's, and it means
reopening decision 2 first. ROADMAP **D6**'s +8/+24 bytes and the price-axis window — not here;
`sizeof` does not move. The median convention (card 29), strain 29's tripwire, §3b's defects and the
64,046 B correction — the **M5 close-out**.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->
