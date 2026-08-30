# M5 Stage D-A3 — the wire, the mechanism, and the instruments the soak needs

**Track:** Agentic [desk] · **Status:** Not started · **Size:** the last desk evening before the
bench, and it is not a small one
**Written:** 2026-08-30 by the desk seat, after D-C's brief found four reasons the soak cannot start
and D-A2's *Out of scope* turned out to have assigned all four here already.

**D-A3 is the stage that makes D-C runnable.** That is not how it was scoped — it was scoped as the
re-seed mechanism plus the ping wire — but D-C's brief established that **the soak has four
blockers, and every one of them is this stage's**. Two were always named here (the ping wire, the
soak instrumentation); one was named nowhere (the policy routing); one had been drafted into D-C by
mistake and belongs here by D-A2's own words:

> The re-seed **mechanism** and its memory, `DisplaySnapshot::reseed` → `InFlight`, the **liveness
> ping wire**, and the **soak instrumentation** — **D-A3**, which is the last desk evening before
> the bench.

**Read first**

| Source | Why |
| --- | --- |
| `M5-stage-D-C-the-soak.md` **§2** | The four gaps, each verified against the tree. This brief is their work order; that one is why they matter. |
| `M5-stage-C-…md` § *Owed by stage D*, **row 2** | The three re-seed candidates, priced, and the measurement that decides between them. |
| `firmware/src/venue_build.hpp:316-326` | The ping wire's ownership note, written by D-A1 rather than improvised: **what it costs and why it was not done there**. |
| `M5-stage-D-B-…md`, decision **2** | What `InFlight` draws, and the header slot pinned to the symbol's. **It constrains which mechanism you may build** — §2 below. |
| `docs/DESIGN.html` strain 28 | The card this stage closes, and the C-half already done. |
| `ARCHITECTURE.md` §6 invariants **#7** and **#8** | Both are load-bearing here: the mechanism allocates, and the ping wire crosses a task boundary. |

**Depends on:** D-A2 ✅ (`83c0bf6`), D-B ✅ (`05e05d6`) — D-B's decision 2 is an input, not a
courtesy. **Blocks:** **D-C entirely.**

---

## 1 · The four gaps — the soak's blockers

**Deliverable 1 · Wire the liveness ping to the watchdog.** Binance's liveness signal is the venue's
20 s **server ping**, a control frame the adapter cannot see. `venue_build.hpp:325` currently ships
`liveness_count(const Adapter&) { return 0; }`, so `armed_` is never set and `expired()` is
permanently false — five `SOAK` fields are structurally inert as a result (`age`, `worst_age`,
`baseline`, `wd`, and the whole `-- age` reading). **The cost, stated by D-A1 rather than
discovered here:** the count lives in `PingProbe` on the **RX task** while the watchdog is stamped
on the **feed task**, so it crosses a task boundary **invariant #8 governs**, and
`liveness_count`'s signature has to change **for all three venues**. That signature change is the
reason this was not folded into D-A1 or D-A2, and it is the reason this stage is not small.

**Deliverable 2 · Route the per-venue liveness policy to the firmware clock. This is a SEPARATE
change from deliverable 1 and no brief before D-C's had named it.** `harness/include/dc_harness/
venue.hpp:298` holds Binance's `{multiple = 2.0, ceiling_ms = 60000.0}`.
`firmware/src/liveness_watchdog.hpp:316` default-constructs `LivenessClock clock_;`, and **no
`LivenessPolicy` appears anywhere under `firmware/`**. So the board runs multiple **4.0**, ceiling
**30,000 ms**, and stage C's derived **39,927.94 ms** threshold — the number D-C's parity check and
multiplier falsifier are both written against — **is not on the board at all**. Wiring the ping
without routing the policy produces a calibrated clock against the wrong constants, which is worse
than an uncalibrated one because it looks right.

**Deliverable 3 · A ping-interval instrument.** D-C's first named check is *"any interval reaching
2 × median on a healthy socket raises k"*. The board prints a **median** and a **sample count**
(`render_task.cpp:824`); the falsifier is about the **maximum**, and `-- ping`'s `worst` / `run` are
**round-trip times**, a different quantity. Emit a maximum beside the median; prefer a coarse
histogram reusing `gap_histogram.hpp`'s existing shape rather than a third one. At a 20 s cadence a
24 h run holds ~4,300 intervals. **`LivenessWatchdog::worst_gap_ns()` already exists and is never
printed** — check whether it is the quantity wanted before adding a counter beside it.

**Deliverable 4 · A reconnect-time largest-block reading, and a repaired `soak_report.py`.**

- `heap_caps_get_largest_free_block` is called in exactly three places — `heap_probe.cpp`
  (periodic), `panel.cpp` (DMA caps, once) and `rest_fetch.cpp` (fetch-scoped). The socket-up line
  at `ws_transport.cpp:637` prints `dns / connect+upgrade / fd / rssi` and **no heap figure**, and
  the `SOAK` line's `largest=` is a 10 s periodic sample. **Sample it where the socket comes up**,
  which is the point D-C's most load-bearing check specifies. Do not reuse the fetch reading: a
  fetch takes the largest block to a few KB by design, and `rest_fetch.cpp:396` already WARNs below
  16,717 B on **every** fetch.
- **`tools/soak_report.py` matches zero lines of a current capture.** The `time` filter's
  `HH:MM:SS.mmm > ` prefix breaks every `^`-anchored grammar; `RE_PIPE` fails **even de-prefixed**
  because the board now prints `no_slot=3 max_held=4 of 4 qfull=0` (`b9a37eb`) against a pattern
  expecting `no_slot=… qfull=…`; and it is guarded by a bare `if pipe:`, so the section **drops
  silently**. Fix all three, **make the guard loud**, and add grammars for `-- age`, `-- ping`,
  `-- frame`, `-- slot` / `-- slots`. **Prove it against a real capture** — a reader that has never
  parsed a real line is not a reader, and this one has been broken through at least two stages that
  believed they were leaving it usable.

## 2 · The re-seed mechanism — and the choice D-B has already narrowed

Stage C priced three candidates and left the choice here:

| | candidate | cost |
| --- | --- | --- |
| **(a)** | a ~128 KiB deferred buffer | no gap, no grey — and it more than doubles the adapter's ~96 KiB of fixed state |
| **(b)** | drop-gap-reseed | free in memory; **greys the panel for the length of a fetch on a book that was still correct** |
| **(c)** | merge below the touch | cheapest, least proven, nothing in the corpus exercises it |

B2's adoptability measurement decides how often this matters: **0 of 7 adoptable at `limit=1000`
on the liquid pair, 19 of 19 everywhere else** — the deeper the seed the older it lands. A fetch
measures **3,143–4,485 ms** on this board (body 64,046 B every time).

> **THE FINDING THIS BRIEF EXISTS TO RAISE: D-B's decision 2 appears to eliminate (b), and nobody
> has said so.** D-B decided that on `ReseedState::InFlight` **the live palette stays selected and
> every ladder `Ink` is unchanged** — the ladder keeps its colour and only the header changes.
> Candidate (b) **drops the book**, so a `Gap` reaches the engine, `status` becomes `Stale` and the
> panel **greys** — keeping its levels, which `book.hpp`'s `mark_stale` retains deliberately *"for
> the renderer to grey"*, so the picture is a grey ladder rather than an empty one. Either way it is
> grey, and D-B's decision says it stays coloured. Under (b) that decision is not merely
> undesirable, it is **unreachable**. Note the two are the same argument seen twice: stage C priced
> (b)'s cost as *"greys the panel for the length of a fetch on a book that was still correct"*, and
> D-B's reason for header-only was that greying a correct book lies in the safe direction for
> **3.1–5.6 s**. **D-B rejected (b)'s cost without naming (b).** So the choice is between **(a)** and **(c)** — unless the owner reopens
> D-B's decision 2, which is theirs to do and should be a deliberate act rather than a consequence
> discovered mid-implementation.

**Deliverable 5 · Build the chosen mechanism**, with its memory cost stated against invariant #7 the
way D-A1 and D-A2 both stated theirs, and with the adoptability figure re-derivable from the
harness. **Deliverable 6 · Advance `DisplaySnapshot::reseed` to `InFlight`** — it exists, C put it
in existing padding, and D-A2 left it deliberately unreachable so the open card would be visible on
the panel. **`sizeof(DisplaySnapshot)` stays 1,168** unless ROADMAP D6 is being done, which it is
not.

## 3 · The header marker — implement, do not re-decide

**Deliverable 7.** D-B pinned this and handed constraints rather than a choice:

- The marker takes the **symbol's slot**, under the standing priority **VALUE > AGE > SYMBOL**.
- **At most eight characters**, asserted against the real header width in `test_ladder_render.cpp`
  the way every `reason_text` string already is.
- It **yields exactly as the symbol yields today**, so a header too narrow drops the marker rather
  than overlapping the price or the age.
- Drawn in **`Ink::Symbol`** — the slot's ink, not a new one. A new `Ink` costs a stale-palette
  entry and a `static_assert`, for a marker that never appears on a grey panel.

## 4 · Constraints

- **Invariant #8 governs deliverable 1.** The ping count and the watchdog stamp live on different
  tasks; whatever carries the count across must keep the single-writer property, and the crossing
  must be argued in the brief's log rather than assumed. D-A2's third task is the precedent for how
  such a crossing gets stated.
- **Invariant #7 governs deliverable 5.** Candidate (a) is ~128 KiB of fixed state against an
  adapter that already holds ~96 KiB; whichever is chosen, the footprint is stated and asserted the
  way `venue_budget.hpp` asserts the rest.
- **`engine/` stays host-buildable** and free of ESP-IDF includes; the ping wire is firmware-side.
- **Deliverables 1–7 all touch `firmware/`**, so per `ARCHITECTURE.md` §9 (2026-08-30) **ctest does
  not verify them** — each commit needs `pio run -e depthcharge-binance` in its worktree, and
  `secrets.h` copied in first (`CLAUDE.md`, commit discipline).
- **Do not start the soak.** A stage that ends by launching a 24 h run has bundled D-C into itself
  and made a failure unattributable — which is the reason D-A2 gave for splitting this stage out in
  the first place.

## 5 · Known unknowns — resolve and record

- **Whether (a) or (c)**, given §2's narrowing. (c) is cheapest and least proven — *nothing in the
  corpus exercises it* — so choosing it means building the trace that does, not asserting it works.
- **What the header shows during the ~11 minute age no-reading window.** `NOTES` §C.4 says only
  "D's", and D-B's four decisions did not include it. It becomes visible the moment deliverable 1
  lands, so it is likely this stage's by arrival rather than by assignment.
- **Whether `worst_gap_ns()` is the ping-interval maximum** deliverable 3 wants, or a different
  silence. It is present and never printed; two counters for one event is how `wd=` already
  disagrees with `LivenessWatchdog::firings()`.
- **Whether routing the policy changes Anvil or Kraken.** Stage C's claim was that they *"do not
  move at all, by construction"* because `firmware/` passes nothing. Deliverable 2 makes
  `firmware/` pass something — so that construction ends, and the two venues' thresholds must be
  shown unmoved rather than assumed unmoved. **This is the attribution problem stage C avoided,
  arriving one stage later.**
- **The ~302.9 s rx-silence recycle** — sources say "D-A3 or D-C" and choose neither. If it is a
  transport change it is this stage's; if it is only a question of whether it recurs on the working
  image over a day, it is D-C's.

## 6 · Definition of done

- ☐ The ping is wired; `-- age` shows a **non-zero median and a real sample count** on the board.
- ☐ The policy is routed; the board's threshold reads near **39,927.94 ms** once calibrated, and
      **Anvil's and Kraken's are shown unchanged** rather than assumed.
- ☐ A ping-interval maximum (or histogram) prints, and D-C's falsifier is computable from a capture.
- ☐ A largest-free-block reading is taken **at reconnect** and prints.
- ☐ `tools/soak_report.py` parses a **current** capture — non-zero on every regex it owns — with the
      silent `if pipe:` guard made loud, proved on a real file.
- ☐ The re-seed mechanism is built, its candidate chosen **with the reason recorded**, and its
      memory stated against invariant #7.
- ☐ `DisplaySnapshot::reseed` reaches `InFlight` on the board; `sizeof` still 1,168.
- ☐ The header marker renders per D-B's four constraints, with the width assertion in
      `test_ladder_render.cpp`.
- ☐ Strain 28 closed; any decision with architectural weight to `ARCHITECTURE.md` §9.
- ☐ ctest green **and** `pio run -e depthcharge-binance` green in a worktree for every firmware
      commit; session log · ROADMAP; split proposed; nothing committed until approved.

## 7 · Out of scope

**The soak itself — D-C**, which this stage unblocks and must not pre-empt. Every rendering
decision — **D-B**, closed 2026-08-30; decision 2 is an input here and is not reopened by this
stage. `worst_frame` — closed as the wrong instrument. `kFrameCapacity` sizing — closed by D-A2.
The median convention (card 29), strain 29's tripwire, §3b's defects and the 64,046 B correction —
the **M5 close-out**. M4's card 30 (`armed_`'s single setter) — a live dependency, not this
stage's to close, though deliverable 1 should record whether wiring the ping gives `armed_` a second
setter, because that is what reopens remedy (b) in strain 26.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->
