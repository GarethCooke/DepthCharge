# M5 — the shape, and the two decisions stage 0 forced

**Drafted from the planning seat, 2026-08-25, on stage 0's measurements.** This is M5's brief in the
sense M4 used the word: it frames the decisions, draws the stage split, and says what each evening
owes. The stage briefs are written one at a time from here, as M4's were.

> ### ✅ STATUS: BOTH DECISIONS **TAKEN — owner, 2026-08-25**
>
> **Decision 1 · liveness — the decoder contract widens, and the threshold is ~80 s at the 4×
> margin.** Ping arrival stamps the liveness clock. That is not a table value: it widens
> `classify()`'s input, which is the same widening the REST record needs, so it is **one trace-contract
> change made once in stage A, not two made separately.** The margin stays at the 4× both shipping
> venues use, so the threshold is ~80 s against a 19.97 s median ping. The consequence — a Binance
> ladder greys roughly twenty times slower than a Kraken one — is **named in the definition of done**
> rather than inherited.
>
> **Decision 2 · the audit stream ships on the board, at the 1000 ms tick.** The mixed configuration
> was measured rather than inferred and the oracle stays exact across cadences: **90/90 on two
> independent captures**, so the board gets a real oracle and not a degraded one. It costs a
> near-constant **1.33 KiB/s (+8–14%)**, which the 1.7× market swing in `@depth` does not touch. The
> honesty asymmetry against Kraken closes: a Binance board verifies its own book against the venue's
> once a second.
>
> **The stage split below assumes exactly these two answers**, and it now does so as a record rather
> than an assumption. **Stage A transcribes both into `ARCHITECTURE.md` §9 as its first act, before
> any code** — the drafts are in *Deliverable 0* immediately below. A decision that exists only in a
> brief is one document away from being lost; this project has lost one that way already.
>
> **One caveat that travels with decision 2, and it must not be quietly dropped.** The measurement it
> rests on lives in **untracked** files — `harness/replay/_local/binance_btcusdt_mixed{1,2}_20260825.ndjson`
> — deliberately unsliced, uncommitted and unpinned, because pinning means editing `CMakeLists.txt`
> and `binance_pins.py` and that was outside the amendment's scope. **So a signed decision currently
> cites figures with no committed trace behind them.** B2 slices, commits and pins both, or the
> ruling above is an assertion in a repository whose whole method is that assertions are pinned.
>
> ---
>
> **The corrections caveat is DISCHARGED (2026-08-25).** `SEND-TO-depthcharge-cc-M5-stage-0-corrections.md`
> ran and all four items landed. The 150/151 resolved in the strong direction: a slice-boundary
> artefact — the last record of the file, an id beyond the highest diff `u` the slice contains,
> because `slice_trace.py` cut on an `rx_ns` boundary that fell between a partial and the diff
> ending on the same update. The full capture over that stretch is **901/901**. So the oracle claim
> stands with its boundary condition stated — **exact on every payload of every COMPLETE capture** —
> and `unverifiable` is a live bucket rather than a decorative one, demonstrated by the oracle
> naming that very payload.
>
> The oracle now has five witnesses (`deepseed` 899/899, `deepseed2` 901/901, `atomeur` 8/8,
> `mixed1` 90/90, `mixed2` 90/90) and `--check` reports **NOT EXERCISABLE** as a state distinct from
> pass and from failure — because a quiet pair cannot break the bounded-window mutant, so a naive
> third witness would have been one that could only ever agree. That rule is in §9 and it outranks
> the instance.

**The headline. Binance removes both of the mechanisms this panel's honesty runs on, and M5 is the
milestone that decides what replaces them.** At Kraken a CRC32 arrives free on every message and a
1 Hz heartbeat arrives unasked: the board self-verifies, and the calibrated grey threshold has
something to arm on. Binance publishes neither. Stage 0 found a replacement for the checksum — the
venue's own `@depth20` stream, exact at every tick — and established that there is **no replacement
for the heartbeat** except a control frame that currently cannot cross the §4 boundary. Everything
else in M5 is ordinary adapter work that M4 has already rehearsed.

---

---

## Deliverable 0 — the two §9 rows, before any stage-A code · ✅ **TRANSCRIBED 2026-08-25**

> Both rows are now in `ARCHITECTURE.md` §9, dated *2026-08-25 (M5, owner's ruling)*, shaped
> into the table's three columns with the wording preserved. **Do not transcribe them again** —
> a second pass would duplicate the rows rather than update them. Row 2 carries one addition
> that is not in the draft below: the untracked-evidence caveat from the status block, written
> into the row itself so that B2's obligation cannot be lost with this brief.

**Transcribe these into `ARCHITECTURE.md` §9 first, then start work.** Shape them into the table's
`Date · Change · Why` columns; the wording below is the ruling and should survive the transposition.

**Row 1 — liveness.**

> **BINANCE'S LIVENESS SIGNAL IS ITS WEBSOCKET PING, AND CARRYING IT WIDENS THE DECODER CONTRACT'S
> INPUT — IT IS NOT A VALUE IN THE VENUE TABLE.** What stamps the liveness clock is
> `RecordKind::is_liveness`, set per decoder inside `classify()` from the frame's own content and
> consumed at `replay_driver.cpp:88`; the signature is pinned by a `static_assert`
> (`trace_decoder.hpp:155`) and **`classify` takes a frame, which a control-frame arrival does not
> have.** So a ping that stamps the clock is a widening of what a decoder may be asked to classify.
> **That is the same widening the REST snapshot record needs, and the two are therefore one change,
> made once, in stage A.** The threshold is **~80 s** — the 4× margin both shipping venues use,
> against a measured ping median of **19.97 s** (min 19.85, max 20.2, n=23; the single 40.7 s is a
> deliberate reconnect restarting the schedule).
>
> *Why.* The alternative was to grey on socket death alone, and B3 priced that: the liveness
> watchdog beat the transport's own detection by **141 s and 291 s**, with half-open the *plurality*
> case at 4 of 7 socket losses. **What the ruling accepts in exchange is a 20× regression in grey
> latency** — ~80 s here against Kraken's ~4 s and Anvil's ~2 s — which is a property of the venue's
> cadence and not a defect in this object, and which is therefore **named in M5's definition of done
> rather than left to be discovered at the panel.** The margin is a live lever: the ping's
> worst/median is **1.01×** against Kraken's 1.12×, so a tighter multiplier is defensible on measured
> grounds and would land nearer 40 s. It is deliberately **not** taken now — 4× is what the other two
> venues run, and changing the constant and the venue in one step leaves no way to attribute a
> regression.

**Row 2 — the on-board oracle.**

> **THE VENUE'S `@depth20` AUDIT STREAM SHIPS ON THE BOARD AT THE 1000 ms TICK, AND THE MIXED
> CONFIGURATION WAS MEASURED RATHER THAN INFERRED.** Two independent 90 s deep-seeded captures of
> `@depth@100ms` + `@depth20` (`limit=1000`): the partial's `lastUpdateId` coincided with a diff `u`
> on **90/90 and 90/90**, graded **88/88 GREEN** both times. **The oracle stays exact across mixed
> cadences**, so the board runs a real oracle and not a degraded one — it verifies its own book
> against the venue's once a second. Cost is a **near-constant 1.33 KiB/s** (1.33, 1.33, 1.35 across
> every window measured; mean payload 1,362 B at 1000 ms against 1,368 B at 100 ms), which is
> **+8–14%** on the diff stream against the per-tick oracle's +141% — 9.98× cheaper, exactly the same
> payload at a tenth the rate.
>
> *Why, and the number that reframed the decision.* `@depth`'s own cost is **not stable**: 9.47,
> 10.94 and 16.16 KiB/s across three 90 s windows, a **1.7× spread on market activity alone with no
> configuration change.** So the audit stream is not what threatens the byte budget — the market is,
> by 6.7 KiB/s against the audit's 1.33 — and a decision to save 1.33 in that budget would have been
> saving the stable term. **The stage-0 headroom figure of 3.25× was a quiet-window number**; in the
> busiest window measured, `@depth` alone is 1.9× and `@depth` + audit is 1.8×. **A headroom claim at
> this venue needs a distribution, not a 90-second sample** — that is strain 19's shape arriving in a
> new place — and **B2's soak is where it comes from.** What the ruling buys is the thing §1 claims:
> a Binance ladder that is honest about its book to the same standard a Kraken one is, instead of a
> venue-shaped hole in that guarantee.

## Decision 1 — liveness · **TAKEN 2026-08-25: widen the decoder contract in stage A, threshold ~80 s at the 4× margin**

*The reasoning below is the case as it was argued to the owner; the ruling is Deliverable 0's row 1.*

The §9 row of 2026-08-24 names two options: publish ping arrival across the §4 boundary, or grey by
socket death alone. **There is a third, and it is cheaper than either, because both halves of the
mechanism already exist.**

- **⚠ CORRECTED 2026-08-25, from the source rather than from a brief.** This bullet first claimed
  that which-arrival-stamps-the-clock is *already venue-declared metadata*, so Binance's answer
  would be "a value in a table." **That is wrong, and the code says so.** What stamps the clock is
  `RecordKind::is_liveness`, set per decoder inside `classify()` from the frame's own content
  (`f.type == "summary"`; `ch == "heartbeat"`) and consumed at `replay_driver.cpp:88`. The contract
  is pinned by a `static_assert` at `trace_decoder.hpp:155` — *"a venue decoder must provide
  RecordKind classify(const TraceFrame&)"* — and **`classify` takes a frame. A control-frame arrival
  has none.** So this is a widening of the decoder contract's **input**, not a row in a table.
  **The conclusion below survives and its reason changes:** it is the *same* widening the REST record
  needs, so "solve it once, in stage A" is strengthened — but the cheapness rests on that bundling
  and on nothing else. Quoting M4 stage 0's *"beside the venue's other declared metadata"* as if it
  described this mechanism was reasoning about the codebase from a brief instead of from the code.
- **§4 already lets the transport manufacture feed-boundary facts from non-record evidence.**
  `Gap{Disconnect}` is synthesised transport-side from a socket close or an RX watchdog — *no venue
  is required to send one*. A ping arrival stamping the liveness clock is the same shape, and needs
  no new `FeedEvent` and no new `GapReason`.

**What is genuinely hard is invariant #6.** A ping is not JSON, so it cannot reach a trace as a
`frame`, so a host replay could never exercise Binance liveness — and an adapter that cannot be
covered is exactly what M4 stage A exists as a warning about.

**But that is the same problem stage 0 already proposed a shape for.** The REST body is not a frame
either. **Two record kinds, one contract question — solve it once, in stage A.** Settle the trace
contract twice and the second answer is constrained by the first, which is precisely how `ticker`
became a required field at M0 and had to be undone at M4 stage A.

**If you would rather not spend the trace contract on it**, grey-by-socket-death is a legitimate
answer — but it then belongs in M5's definition of done as *the panel's claim at this venue*, not
left to emerge as a consequence. The B3 soak already priced it: the liveness watchdog beat the
transport's own socket detection by **141 s and 291 s**, and half-open was the *plurality* case at 4
of 7 socket losses. A Binance ladder without it sits coloured and frozen for minutes.

**And the number this decision was missing, added 2026-08-25 because the brief's own standard
demanded it.** Across the seven captures, 23 ping intervals: **median 19.97 s, min 19.85 s, max
20.2 s** (the single 40.7 s is the deliberate reconnect restarting the schedule). At the ~4×
margin both shipping venues use — Kraken's threshold settled at 4,006 ms from a 1,001 ms median — a
**ping-armed threshold is ~80 s**, against Kraken's ~4 s and Anvil's ~2 s.

Two things follow, and they point opposite ways. **It still wins decisively:** B3's half-open cases
were caught at 141 s and 291 s, so 80 s beats the transport by roughly a minute and three and a half
minutes, and the margin is a real lever because **the ping is *more* regular than Kraken's
heartbeat** — worst/median **1.01×** against Kraken's 1.12× — so a tighter multiplier is defensible
on measured grounds and could roughly halve it. **But a 20× worse grey latency than Kraken is a
property of the venue that the panel inherits**, and by this brief's own standard — *named rather
than inherited* — it belongs beside the definition of done's honesty clause, not only inside stage
C. It is now there.

## Decision 2 — does the oracle ship on the board? · **TAKEN 2026-08-25: YES, at the 1000 ms tick**

*⚠ The case below was argued for `no` and is superseded by the measurement that followed it. It is kept unedited — the mixed-cadence captures are what changed the answer, and a brief that quietly rewrote its own losing argument would hide that. The ruling is Deliverable 0's row 2.*

`@depth` alone is **9.47 KiB/s** — 3.25× headroom on the everyday 30.8. Both streams at the 100 ms
tick is **22.84 KiB/s**, 1.35×. The oracle is **58.6%** of the wire and **7.13×** the total on a
quiet pair. As a runtime feature it is expensive; as a desk instrument it is free, because the
harness has no wire budget.

So the default: **the oracle is a test instrument, the board subscribes `@depth` alone**, and the
easy-mode question is thereby answered — `@depth20` is kept, in `harness/` only. Stage 0 was right
that section 4(a) changed the terms: it stopped being a stepping stone and became the instrument.

**Name the consequence rather than inheriting it.** A Kraken board self-verifies every message; a
Binance board never does. The CRC is a runtime check that costs nothing, the oracle is a build-time
check that costs 141%. That asymmetry is defensible — but §1's claim is a terminal that is *honest
about what it is showing*, so it belongs in the DoD as a stated limit beside the liveness answer,
rather than emerging from two independent cost arguments that never met.

**One middle option, to be priced rather than assumed away:** `@depth20` at the **1000 ms** tick as
a periodic on-board audit instead of a per-tick one. Stage 0 priced the 1000 ms *pair* at 7.84 KiB/s
but never separated the audit stream's own cost at that cadence. If it is cheap, the board gets a
slower version of Kraken's self-verification and the asymmetry above mostly closes. **That number is
B2's to measure, not anyone's to guess.**

---

## The stage split

| Stage | Name | Why there | Depends |
| --- | --- | --- | --- |
| **0** | price the wire, invent the oracle | ✅ **Done 2026-08-24** | M4 |
| **A** | the trace dialect, and the two records that are not frames | Invariant #6, and the argument is stronger than at M4: two record *kinds* are needed, not a metadata tag | 0 |
| **B1** | the adapter | The first evening with an oracle available before the code is written | A |
| **B2** | the seed, the walk, and the schedule | 256 is not enough, and no fixed window survives indefinitely | B1 |
| **C** | liveness at a venue that declares none | Where decision 1 actually lands | A, decision 1 |
| **D** | the bench | The soak must outlive the venue's own 24 h connection limit | B2, C |
| **close-out** | the sweeps that must not travel inside another stage's diff | **Added 2026-08-26 at B2, because B2 assigned a clause to it and it did not exist.** M3 and M4 both had one (`M3-closeout-transport-and-docs.md`, `M4-closing-bench-sitting.md`); M5's table did not, so *"owner: M5 close-out"* named nothing and was functionally the blank the B2 approval note forbade. **It carries the median-convention change** — `harness/src/trace.cpp` interpolates where `sample_window.hpp` says nearest rank, so every Binance cadence figure quoted in the repository is not the one the shipped clock computes — which cannot ride inside another stage because it rewrites figures in `taxonomy_pins.inc` and *no existing golden moves* is what makes a split reviewable. Expiry and tripwire in ARCHITECTURE §9, 2026-08-26; carried in three places. | D |

### A · the trace dialect, and the two records that are not frames

`dc_replay` cannot read a Binance capture, and this time the gap is not a metadata tag. **The REST
body and the control-frame arrival are both records with no JSON frame**, and both are required
before an adapter can be covered.

- The REST record carries the **request** as well as the response — it is a fetch this client chose
  to make, not something the venue said — and is reconciled **by id, never by position.** Stage 0
  paid for that sentence once already: a slice cut *at* the baseline record rather than containing
  it took the oracle from 884/884 green to 250/250 red.
- The control-frame record, if decision 1 goes that way. Nothing needs measuring —
  `wsclient.on_control` already produces `(opcode, payload, recv_ns, replied_ns)`; only a shape is
  missing.
- The `venue.hpp` row and the `tools/tracefile.py` `VENUES` row.
- **Strain 22, which stage 0 says the card understates.** The costly edit was not the `VENUES` row;
  it was **six predicates defaulting to Kraken's answer** — a missing branch returning a confident
  wrong answer rather than a refused file. **Fix the shape, not the six:** make the unmatched venue a
  hard failure, so a seventh predicate cannot repeat it. This is the repo's own standing preference
  for structural over remembered, and the card should be updated to the stronger reading.

No adapter this evening.

### B1 · the adapter

- **No third scanner.** Strain 25's trigger arrived and did not fire: zero floats, zero exponents,
  zero string escapes, max nesting 4 — a strict subset of both existing scanners. Reuse; the
  extraction stays unbuilt and the strain closes on a measurement rather than on a schedule.
- **The scale is a constant, not `tickSize`.** 8 decimals uniformly across 202,012/202,012 entries,
  on a 2 dp and a 3 dp symbol alike. `tickSize`/`stepSize` become **validators** — *is this value a
  whole multiple of the tick* — not the source of the scale. `78564.00000000` scales to
  7,856,400,000,000, 43 bits: invariant #3 needs no new path, no float, no `Decimal`.
- **The `U`/`u` state machine is a transport check and must never be quoted as anything else.** It
  caught the reconnect's 2,204 missed updates and **0 of 3** book mutants. Wire it to `Gap`; wire
  nothing to a claim about correctness.
- **Use the oracle from day one** — B1 at Kraken did not have one until B2. The removal rule, the
  crossed-book question B1 deliberately left unanswered at Kraken, and truncation all have an
  independent answer here *before* the code is written rather than after.
- The REST seed at `limit=1000`.

### B2 · the seed, the walk, and the schedule

- `kMaxSnapshotLevels = 256` fails **33 of 884** graded ticks in 90 seconds. `limit=1000` covers
  ~$240 against a measured **$29.85 per 90 s** walk on BTCUSDT — but **no fixed window survives
  indefinitely**, so the re-snapshot interval needs a measured basis (walk rate against chosen
  window, with a margin, per symbol class) and not a constant.
- **And the sting, which is worth seeing before the schedule is written:** at this venue the
  re-snapshot **is** the healing event and it is *scheduled*. Once the client re-snapshots on a
  timer, **every long Binance trace contains healing events by construction** — a clean guard window
  does not merely become hard to find, it stops existing. Say what that does to the capture recipe
  in the same evening that creates the problem.
- **Slice, commit and pin the two mixed-cadence captures.** They are the evidence decision 2 was signed on and they are currently untracked in `_local/`. Pricing `@depth20@1000ms` is done (1.33 KiB/s, constant); what is owed is the pin, plus the **headroom distribution** that replaces stage 0's single-window 3.25×.

### C · liveness at a venue that declares none

- Whatever decision 1 settles, implemented: the venue-table row, the publish path if it is that, and
  the host coverage that stage A's record kind makes possible.
- **What the panel does when the book is fine and the feed is silent for 10.5 s** — which at Binance
  is the venue behaving normally, and which at Kraken was the 26 s question the entire 2026-08-17
  grey ruling came out of.
- **No new vocabulary.** If it appears to need one, stop and raise. That has been the answer four
  times and the answer has been right four times.

### D · the bench

- The panel renders a Binance book off the wire.
- **The soak must exceed 24 hours.** The venue closes the connection at 24 h by policy; M3's 23.6 h
  would have missed it by twenty-four minutes. This is the first *scheduled* disconnect the project
  has met, and a soak that stops short of it proves nothing about the case.
- The honesty acceptance, in two halves: pull the Wi-Fi, as at M3 — and the new one, **a feed that
  goes quiet without the socket dying**, which is the case Binance makes reachable and Kraken never
  did.

---

## M5's definition of done

M5 is complete when the panel renders a Binance book off the wire; **greys when the feed dies
despite the venue publishing no heartbeat**; holds colour through a ten-second silence that is the
venue behaving normally; recovers from a sequence gap by re-snapshot rather than by reconnect; holds
a book that does not silently drift out of its seeded window over hours; and **is honest, in the
DoD's own words rather than by implication, about three things it inherits from this venue and does
not choose: the levels the oracle never checks, whether it checks anything at runtime at all, and
that a Binance ladder greys roughly twenty times slower than a Kraken one** — ~80 s on a ping-armed
threshold against Kraken's ~4 s, which is the venue's cadence and not a defect in this object.

Each clause belongs to exactly one stage. That was the test of whether M4's split was drawn in the
right places, and it is the test of this one.

## Out of scope

The carrier PCB (M6). The enclosure and the encoder (M7). The runtime venue toggle (M7). The client
ping (M6) — and note it is **not** a substitute for decision 1: a client ping proves the round trip
*when we ask*, whereas the whole point of a liveness signal is that it arrives when we do not. The
scanner extraction (strain 25 — answered, not needed). M4's carried bench residues: D1, D2 and D7's
scope trace.

## What does not recur, and it is worth saying out loud

The dense-window book (M4 stage C), the age meter (M4 stage A2) and the healing-path vocabulary (M4
stage B2) are all built and all venue-agnostic. **M5 is a narrower milestone than M4 was** — one
adapter, one contract question, and two decisions.

And the vocabulary held again before anyone looked: **§4's `Gap{SeqGap}` was written down for this
venue at M0**, a month and eleven milestones before the code that will need it, exactly as
`ChecksumFail` was written for Kraken. Fourth asking, fourth time §4 did not have to move.
