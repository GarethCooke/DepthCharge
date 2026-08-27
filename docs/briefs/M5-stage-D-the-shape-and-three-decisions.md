# M5 Stage D — the shape, and the three decisions that block briefing it

**Track:** Mixed [desk + bench] · **Status:** Not started · **Size:** **four sittings, not one**
**Seat:** planning (chat) · **Written:** 2026-08-27 · **§§1–3 rewritten 2026-08-27** after the
execution seat verified the first draft against the tree.

*The first draft of §1 was built on a **~120 KB** seed body and asked which of two ways to transport
it. The body is **64,046 B**, and the seed was never the blocker. Rewritten rather than annotated,
because this document has never been committed and B2's rule reaches only published claims — but the
error is kept in view below, since where it came from is the reusable part.*

D is where M5 ends, and it is larger than the stage table implies — partly for a reason this seat
created. C's scoping ruling pushed **every** question of the form *what does the panel look like*
into D, and that is what let C fit in an evening. The bill arrives here.

*Sizing warning, now three for three in one direction: B2 was called two evenings and was one; C's
ruling quietly made D into four; and §1's first draft quoted the body 1.92× too large, and omitted
the blocking constraint entirely — two separate failures, and only the first is a factor. Discount
this seat accordingly.*

---

## 1 · The board has no room for the adapter, and that is the decision

### What the first draft got wrong, and where the number came from

`NOTES-binance.md:1138` says the `limit=1000` body is **~120 KB**. Measured off the committed
corpus — **eleven** distinct BTCUSDT bodies (plus two ATOMEUR, reconciling with B2's n=13 at this
tier), every one **exactly 64,046 B**. *The obvious reason is the wrong one: 8-dp padding does not
give a constant length, and ATOMEUR is padded identically while its two bodies differ — 9,340 and
9,369 B. Constant length also needs constant integer-digit width, which is a property of this pair
on this tape: 22,000 entries, every price string 14 chars and every quantity 10.* The figure is
wrong by **1.92×** — `~120 KB` read as **122,880 B**, because §9's 2026-08-16 row records that
*"every figure in this repo has always been binary and consistent"*; on a decimal reading it is
1.87×. The figure has propagated unchecked into stage C, `docs/DESIGN.html` and this document's
first draft, and its likely origin is the adjacent and genuinely-128-KiB deferred-diff buffer in
the same section.

**A number nobody re-measured, sitting in the same section as a similar number that is real.** The
128 KiB is at `NOTES-binance.md:1112` and the ~120 KB was at `:1138` — 26 lines and two tables
apart as it stood when the figure was written. (At HEAD they are 71 lines apart, because the
correction block now sits between them.) **Close enough to be the likeliest explanation, not close
enough to be a demonstrated one** — nothing records the conflation, and it does not by itself
explain 120 rather than 128. Same family as the median with two homes: not a defect, a figure that
was never checked against the thing it describes. It should be corrected at source with the
eleven-body measurement beside it.

**And the class already has a rule, from the row that establishes the unit convention.** §9's
2026-08-16 row is itself *"one arithmetic correction to the row below, and to the four documents
that quote it"* — the same shape and the same count — caused the same way: *"a ratio got eyeballed
instead of divided, and then quoted five times without anyone redoing the division."* Its general
rule is the one this seat broke: **a percentage in prose is a claim, and claims get recomputed when
they are quoted — a ratio nobody re-derives is a ratio nobody has checked.** The 1.87× was that
error twice over: a ratio not re-derived, against a unit convention not checked.

The conclusion survives — 64,046 B still does not fit the 47,092 B largest free block — but every
margin the transport question was priced on was roughly doubled, and the transport question was not
the blocking one anyway.

### The blocking constraint

`sizeof(BinanceAdapter)` is **100,824 B (98.5 KiB)**, against Kraken's 16,744 and Anvil's 8,400 — a
delta of **84,080 B**. `FeedTask g_feed` is namespace-scope (`main.cpp:77`), so `adapter_` is
**`.bss`: internal SRAM, claimed before the heap exists.** Compiled against the real headers:

| member | bytes |
| --- | ---: |
| `frame_` (`BinanceFrame`, 32,824 incl. its own 2×1024 levels) | 32,768 of levels |
| `bids_[1024]` + `asks_[1024]` — the ladder | 32,768 |
| `buf_lvl_[2048]` — the pre-seed buffer | 32,768 |
| everything else | 2,520 |
| **total** | **100,824** |

The board's own logs give the mechanism: Anvil `dma-internal free=179,300`, Kraken `168,720` — a
10,580 B drop against an 8,344 B adapter delta, and **that alone already cost Kraken a colour rung**
(`depth=5` against Anvil's 6). Projected forward, Binance's free internal is ~**84,640 B** against
`kReserveInternalBytes` of 98,304, so `choose_depth` receives a budget of **0** and `panel.cpp:105`
prints *"no colour depth fits in 0 B — running WITHOUT a panel."*

**B1 handed exactly this to D — *"where it lives on the board is D's"*, said in three places** —
and the first draft did not carry it forward.

### The three levers, in the order they should be spent

**Shrinking the arrays cannot be the primary lever, and this is arithmetic rather than preference:**

| sizing | adapter | budget | outcome |
| --- | ---: | ---: | --- |
| today | 100,824 | −13,664 | no panel |
| all three at their **measured worst** (537 / 500 / 823) — margin **1.000×** | 48,872 | 38,288 | short of Kraken's rung by **19,312 B** |
| all three at **2× measured worst** — B1's own discipline | 95,224 | **−8,064** | still no panel |

At zero margin it does not reach the rung; at the margin B1 actually chose it does not reach a
panel. **There is no setting of those three constants that both restores the rung and keeps
headroom.** And the derivation is worse than the arithmetic: the ladder's 500 comes from **the same
two witnesses whose 5× disagreement is B1's stated reason for not fitting the bound** — *"storage
must be 1,024 deep because the sweep says the requirement is a property of how far the market
walked."* Re-fitting it to that evidence at 1.000× over the worst observed is stage C's multiplier
finding repeated on three constants at once.

**So spend the free room first, and let the arrays cover only what is left.**

1. **The four WS buffers → PSRAM. +16,384 B, and no code.** `malloc_alwaysinternal_limit` is 4096
   on this build (measured off the ELF, `bench-2026-08-11:355`), so anything ≥ 4,097 B already goes
   PSRAM-first. Measured and left unbuilt for sixteen days. **Take it first**, because it costs
   nothing and it proves the mechanism on this board before anything depends on it.
2. **`buf_lvl_` → PSRAM. +32,768 B.** The one adapter array `heap_probe.cpp`'s rule does not reach:
   it is the pre-seed and re-seed buffer, written once and drained once, never DMA'd, and not the
   per-diff path the rule's three grounds are about. `bids_`, `asks_` and `frame_` are touched on
   every diff at 100 ms and **stay internal** — that objection is correct and this proposal does not
   test it. Note the constitution is already on side: **ARCHITECTURE §5 says *"on target the window
   lives in internal SRAM, the tail in PSRAM"*** — the only prohibition is a source comment and a
   README line, and the tail is what `buf_lvl_` holds.
3. Those two give **+49,152 B for no margin spent at all** — `.bss` to 68,056 — against the array
   trim's 38,288 bought by spending every safety margin the adapter has.
4. **Price FramePipe; do not assume it.** 4 × 16 KiB = 65,536 B static is the only lever large enough
   to matter alone, but **DESIGN strain 27 says its slot count is already wrong in the other
   direction** — B3 measured `no_slot=1,594` and heal clustering at 8.36× the Poisson baseline. It
   may want *more* slots. Moving its buffers is a different question from reducing them, and only
   the first is on the table.
5. **The arrays are the residual lever only**, sized by whatever is still missing after 1–4 and
   never re-fitted to the sweep. If they must move, the number comes from the deficit.

### Retarget the acceptance: the double-buffer floor, not Kraken's rung

`choose_depth` walks **two** ladders — double-buffered from `kMaxColourDepth` down to
`kMinDoubleBufferedDepth`, then single-buffered down to `kMinColourDepth`. The real discontinuity is
between them, and `panel.cpp` argues it in its own comment: *"lower the colour depth before giving
up the second buffer… tearing on a book that redraws 13 times a second is a visible defect on a
panel whose whole job is to be believed."* **D's acceptance is that the Binance build boots
double-buffered** — a lower and better-argued bar than matching Kraken's rung, and one that may be
reachable on steps 1–3 alone.

### What the two transport options actually cost, corrected

Both were overstated in the first draft and neither is now the decision, but the corrections matter
if the question returns:

- **PSRAM is cheaper than stated.** ARCHITECTURE §5 already sanctions it; `malloc_alwaysinternal_limit`
  4096 means a 64 KB body allocated normally lands there with no code at all.
- **Stream-parsing is dearer than stated, and it reopens nothing.** There is **no §9 row on parser
  count** — `ARCHITECTURE.md:249` is Anvil-scoped and permissive, so the first draft had the polarity
  backwards. But `anvil_frame_streaming.cpp` is not the precedent claimed: *streaming* there means
  **no-DOM, not chunk-resumable**, and it holds its input whole. The real obstacle is
  `json::Scanner` — a pointer pair whose `at_end()` means truncation — **shared with Kraken's
  shipped parser**, so a Binance-only problem would put Kraken's goldens on the line.
- **`limit` has a hard floor nobody had stated.** `static_assert(kBinanceReseedCoverLevels <
  kBinanceRestLimit)` with `kBinanceReseedCoverLevels = 448` means **`limit ≤ 448` fails to
  compile.** `limit=500` compiles and graded clean on both sweep witnesses, but clears the trigger by
  52 levels against a measured 168-level worst-case burst — it would re-fetch almost continuously.

### And the probe correction that cuts the other way

The first draft asked for the heap probe to be *widened*, as though it were a new capability.
`HeapSample::free_total` **already samples a PSRAM-inclusive figure and is simply never printed** —
so it is one format specifier, not a feature. Separately, `heap_probe.hpp:77` described its own
sample as *"DMA-capable"* until `a3c23d8`, when `kCaps` is `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`
with no DMA bit: a comment naming a quantity the code does not measure, which is the instrument
class this project already has a §9 row for.

## 2 · One clause of M5's definition of done cannot be met — and it is not the clause the first draft quoted

The failing clause is in the **DoD paragraph** (`M5-the-shape-and-the-two-decisions.md:292-299`), not
the stage-scope bullet under *### D · the bench* that the first draft cited. **An owner acting on the
first draft would have edited the wrong line.**

The clause is *"greys when the feed dies despite the venue publishing no heartbeat"*, and C narrowed
it to the **socket**. It is now two cases:

| case | stageable? | detected? |
| --- | --- | --- |
| the feed **never speaks** — connect-time | **Yes.** A misspelled stream returns 101, answers pings, delivers nothing — live-probed at B1, committed as `binance_btcusdt_DEFECT_silent_stream_20260826.ndjson`. | **Yes, since C** — remedy (a) withholds the Snapshot until a diff brackets it. |
| the feed **goes quiet mid-session** — server-side subscription drop | **No.** The venue publishes no subscription-state signal; a drop presents identically to a healthy socket. | **No**, and C's parity answer says so. |

**The stageable half needs no `DC_TEST_MUTE_LIVENESS`.** M4 had to invent that flag because both
network-side methods deauthenticated; here the misspelled stream stages the case from the wire, on
the board, with no test-only firmware — which makes remedy (a) a **hardware** acceptance.

**Recommendation:** narrow the DoD clause to the socket, and add the mid-session case to the DoD's
**honesty clause**, which already lists three inherited limits and has room for a fourth. That is
where it belongs — the DoD's own test is that *each clause belongs to exactly one stage*, and as
written this one belongs to none.

## 3 · The client ping's home — the count was low and the payoff was wrong

**Corrected, and both corrections weaken the first draft's framing:**

- **The triage did not assign it to M6 flatly.** It assigned it to *"whichever milestone next opens
  the transport layer — M6, on current shape"* — a hedged predicate, preserved verbatim in engine
  code. It is **already conditional**, so the first draft's *"waiting on a PCB"* overstates it.
- **The ping does not close M5's parity gap**, and the tree says so twice
  (`M5-stage-0:328`, `ARCHITECTURE.md:310` — *"the other half and does not substitute"*). M5's
  parity NO has **one** stated cause: the liveness signal is emitted below the subscription. The
  first draft's payoff is refuted.
- **The ping shipped at M3.** `DC_WS_PING=1` by default, `ws_ping.hpp`, `PingProbe`, host tests, and
  `[env:depthcharge-noping]` as the control arm. What is deferred is **feeding its RTT into
  `AgeEstimator`** — a much smaller thing than the first draft implied.

**What survives is the sweep, and it is bigger than stated: ~21 mentions across 16 files, three of
them in code** — `age_estimator.hpp:114`, `test_age_estimator.cpp:284`, `NOTES.md:437`. One is in
`engine/`. **A docs-only sweep leaves the portable core pointing at a milestone predicate that has
gone stale** — the species §3 was written to name, arriving inside §3. Rehoming is worth doing; it is
worth doing to `engine/` as well as to the briefs, and the hedged wording should be preserved or
resolved deliberately, not flattened.

## 3b · Three live defects found while verifying this document — **FIXED 2026-08-27**

*Status: all three are fixed, laddered green and pushed on `m5-stage-D-corrections`, as commits 2–4
beside the source correction. The proposed owner below is superseded; the list is kept because
D-A1 cites this section and a fixed defect with no record reads like one that was never found.*

Not D's work, and they will be lost if nobody owns them. **Proposed owner: the M5 close-out**, as
one small commit each.

- `CMakeLists.txt:592` and `:618` still describe the pre-C world — *"Replayed, it produces a
  populated, coloured, LIVE ladder"* and *"asserts today's BROKEN behaviour… so that C's remedy flips
  the test."* C landed and the test inverted. Stale prose in a build file.
- `ROADMAP.md:28` — backlog item **D7 sits inside the milestones table**, between M4 and M5, with the
  wrong column count. Anyone sweeping the backlog will not find it.
- `heap_probe.hpp:77` — see §1's closing note.

---

## 4 · The shape — four sittings

D's inheritance is **not restated here**: it is the six-row *Owed by stage D* table at the end of
`M5-stage-C-what-a-green-clock-is-entitled-to-mean.md`. This is only how it divides, and **the split
changed with §1**: the footprint now leads, and the REST client follows it.

| sitting | track | what it is | why it ends here |
| --- | --- | --- | --- |
| **D-A1** | desk | **The footprint** (§1, levers 1–5) and the venue build — `[env:depthcharge-binance]`, `DC_VENUE=3`, `venue_build.hpp`, and the TLS anchors for **two hosts**: `data-stream.binance.vision` for the stream and **`data-api.binance.vision`** for the seed. Both under `binance.vision`, so whether that is one anchor or two is a **measurement** — M4's precedent is to take it off the live server twice. | **It ends with the board booting double-buffered, connected to the stream, and the ladder honestly GREY** — because nothing has bracketed yet and remedy (a) withholds the Snapshot. That is remedy (a) demonstrated on hardware and §2's stageable half already half-done, before a REST client exists. |
| **D-A2** | desk | The REST client and the seed; the re-seed mechanism and its memory (*Owed* item 2, decided by B2's **0-of-7 / 19-of-19** adoptability); `DisplaySnapshot::reseed` advanced to `InFlight`; **the soak's instrumentation, designed before the soak.** | **A 24 h run cannot be re-asked.** The ping-interval distribution (*Owed* item 3's falsifier) and the uncalibrated-window observation (item 4) must be derivable by `tools/soak_report.py` before the run. M4 learned this twice — the two-clock finding that *"nearly put a fiction in the log"*, and the heap alarm retracted by medians. |
| **D-B** | bench | The **four rendering decisions**, taken by eye (*Owed* item 1), plus §2's acceptance on hardware. | M4's precedent exactly: its two rendering decisions were taken at the closing bench sitting, and one — the ramp failing at six-bit depth — was legible only at an oblique angle. |
| **D-C** | bench | The soak. **It must exceed 24 hours** — the venue closes the connection at 24 h by policy and M3's 23.6 h *would have missed it by twenty-four minutes*. First named check: the multiplier falsifier. Then the uncalibrated window, then parity's reduced claim. | The one DoD clause only wall-clock can close. |

A fifth for residues is likely — M4's stage D had B4 — but it is not planned in advance.

## 5 · What "finishing DepthCharge" means, honestly

The **software** finish line is close. The rest is not all evenings.

- **M5:** D's four sittings, then the **close-out** — card 29's median convention, strain 29's
  tripwire wording, the `CLAUDE.md` prose-versus-ordinal line, §3b's three defects, and the ~120 KB
  correction at source.
- **Carried:** M4's residues — D1, D2's weak-node hour, D7's scope trace; **card 30** (`armed_`),
  a live dependency rather than a residue; strain 27's pipe; the DNS fallback; ROADMAP backlog
  **D0**; and the client ping's RTT once §3 resolves its predicate.
- **M6 — Carrier PCB.** Track B. KiCad, DRC, **fabrication and shipping**: calendar weeks, and the
  only item whose duration this project does not control.
- **M7 — Enclosure, encoder, runtime venue toggle.** Depends on M4 **and M6**.
- **MP — the portfolio portal**, in a different repository.

**The sequencing consequence worth seeing now:** M6 is the long pole and depends only on M3, which is
long done. **Its KiCad and fab order can start in parallel with D** — the board being fabricated
while the soak runs costs nothing and takes weeks off the end.

## Next step

§1's levers are ordered and D-A1 is briefable on them; §2 and §3 are amendments the close-out can
carry, though §2 should land before D-C so the soak knows what it is accepting. **No further scoping
round trip** — the sizeof probe settled what a comparison would have gone looking for.
