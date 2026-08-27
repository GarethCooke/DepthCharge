# SEND TO DEPTHCHARGE CC — the footprint decision, and where the corrections land

**The verification is accepted in full, and it found the thing this seat should have found.** §1 was
built on a figure I never measured, and I had told the owner two turns earlier that this seat's job
is to run one measurement against the artefact rather than none. I did that for the CRLF traces and
for the multiplier, and then took ~120 KB off `NOTES-binance.md:1138` because it sat beside a real
128 KiB buffer and looked right. §2 citing a stage-scope bullet instead of the DoD paragraph, and §3
reading as though the ping were unbuilt when it shipped at M3 with a control arm, are the same
failure in a second and third place: **documents checked against each other and not against the
tree.**

**§§1–3 of `M5-stage-D-the-shape-and-three-decisions.md` are already rewritten** — see § *Where the
corrections land* below for why they were rewritten rather than annotated, and why that is the
opposite of what the ~120 KB fix needs.

## 1 · The decision — reclaim, in this order. Not the arrays.

**Neither of the four options as worded.** The recommended one has no viable operating point, and
that is arithmetic rather than preference. Compiled against the real headers — `sizeof` confirms your
100,824 B, as `frame_` 32,768 + `bids_`/`asks_` 32,768 + `buf_lvl_` 32,768 + 2,520:

| sizing | adapter | budget | outcome |
| --- | ---: | ---: | --- |
| today | 100,824 | −13,664 | no panel |
| three arrays at **measured worst** (537 / 500 / 823) — margin **1.000×** | 48,872 | 38,288 | short of Kraken's rung by **19,312 B** |
| three arrays at **2× measured worst** — B1's own discipline | 95,224 | **−8,064** | still no panel |

At zero margin it misses the rung; at B1's margin it misses having a panel. **And the ladder's 500
comes from the same two witnesses whose 5× disagreement is B1's stated reason for not fitting that
bound** — re-fitting it at 1.000× over the worst observed is stage C's multiplier finding repeated
on three constants at once, three days later, by the seat that found it.

**Spend the free room first; let the arrays cover only the residual.**

1. **Four WS buffers → PSRAM. +16,384 B, no code.** `malloc_alwaysinternal_limit` 4096, so ≥4,097 B
   already lands there. **Do it first** — it costs nothing and proves the mechanism on this board
   before anything depends on it.
2. **`buf_lvl_` → PSRAM. +32,768 B.** The one adapter array `heap_probe.cpp`'s rule does not reach:
   pre-seed and re-seed only, written once, drained once, never DMA'd. **`bids_`, `asks_` and
   `frame_` stay internal** — your objection to them is correct and nothing here tests it.
   ARCHITECTURE §5 is already on side: *"on target the window lives in internal SRAM, the tail in
   PSRAM."*
3. Steps 1–2 give **+49,152 B for no margin spent**, against the array trim's 38,288 bought by
   spending every margin the adapter has.
4. **Price FramePipe; do not assume it.** 65,536 B is the only lever big enough alone, but strain 27
   says the slot count is already wrong in the *other* direction (`no_slot=1,594`, 8.36×). **Moving
   its buffers is a different question from reducing them, and only the first is on the table.**
5. **Arrays are the residual lever only** — sized by what is still missing after 1–4, never re-fitted
   to the sweep.

**Retarget the acceptance.** `choose_depth` walks two ladders, double-buffered then single, and
`panel.cpp` argues the discontinuity itself: *"lower the colour depth before giving up the second
buffer… tearing on a book that redraws 13 times a second is a visible defect on a panel whose whole
job is to be believed."* **D-A1's acceptance is that the Binance build boots DOUBLE-BUFFERED**, not
that it matches Kraken's rung — a lower and better-argued bar, possibly reachable on 1–3 alone.

**No to re-scoping.** The sizeof probe already answered what a costed comparison would have gone
looking for.

## 2 · Where the corrections land — and the voice is inverted in the question

**Tick "fix at source" and "fix the three defects". The brief rewrite is done.** But the second
question assigns the correction voice to the wrong one of the two:

- **The stage D brief is UNCOMMITTED.** B2 settled this exactly: the amend-in-place rule *"does not
  reach an uncommitted draft, where there is no published claim to preserve — so the count is simply
  right, with one italic line inside the section saying what it used to be."* It has been rewritten
  on that basis, with that italic line. **Annotating it in the M4-stage-D voice would be applying a
  rule to the one document it does not cover.**
- **`NOTES-binance.md` and `DESIGN.html` ARE committed, and that is where the correction voice
  belongs** — amend in place, leave the original standing, so the record cannot be tidied into
  something that never happened. `:1138` and `:1514`, plus the three downstream repeats. Put the
  **eleven-body measurement beside it** — every BTCUSDT `limit=1000` body exactly 64,046 B, and
  **not because of 8-dp padding**: ATOMEUR is padded identically and its two bodies differ (9,340 /
  9,369 B). Constant length needs constant integer-digit width too, which is a property of this pair
  on this tape. State the reason that holds, or the corrected figure carries a false one.
- **Do the source fix BEFORE D-A1 is briefed**, not at the close-out. D-A1 cites the footprint
  arithmetic, and a brief that cites an uncorrected source is how this propagated through four
  documents in the first place.
- **The three live defects: separate commits, close-out owns them** — they are documentation drift
  caused by C landing, which is what the close-out is for, and §3b of the brief now names all three
  so they cannot be lost. Take them now instead if you would rather not carry them; they are
  one-liners and nothing depends on the timing.

Worth recording as its own §9-adjacent note, because it is the same shape as the median with two
homes and the `min_bid_levels` instrument: **a figure nobody re-measured, one paragraph from a
similar figure that is real, propagating through four documents unchecked.** Not a defect — a number
that was never checked against the thing it describes. `heap_probe.hpp:77`'s *"DMA-capable"* comment
over a `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` mask is the same class in an instrument.

## Constraints

- **No behaviour changes in any of this.** §2 is documentation; the footprint work is D-A1's.
- **Nothing here moves a pin or a golden.** The ~120 KB figure is prose, not a pinned column.
- Anything touching `NOTES-binance.md` or `DESIGN.html` keeps the original claim visible.
- **Do not act on the first draft's §2 or §3 wording** — both pointed at the wrong line, and the
  rewrite carries the corrected targets.

## Definition of done

- [ ] `NOTES-binance.md:1138` and `:1514` corrected **in the M4-stage-D voice**, with the eleven-body
      measurement beside the figure; the three downstream repeats corrected with it.
- [ ] The three live defects fixed as separate commits, or explicitly left to the close-out with
      §3b as the list.
- [ ] The rewritten §§1–3 read back and disagreed with if anything in them is still wrong — the
      first draft's error rate here was three sections out of three.
- [ ] Green; nothing else moved; split proposed.

**Then D-A1 gets its brief**, targeted at the double-buffer floor with the five levers in order.
