# M3 Stage E — the age of the book

**Track:** Mixed [A+B] · **Status:** ◐ **E1 landed 2026-08-11 except its bench half** — writeback,
`staleness.hpp` + `-- age`, the `Gap` proposal and the desk control are in the tree; the
lag-versus-uptime stopwatch run is owed and **E2 is gated on it** · **Size:** two evenings, E1 then E2
**Executor:** Mixed. Claude Code writes the instrument and the docs; **Gareth runs the board and
holds the stopwatch.** CC does not drive the bench.

**This is not polish deferred behind M4. M3's claim is a market-data terminal that is honest
about what it is showing, and on the evidence below it is not one yet.** The pull-the-Wi-Fi
acceptance genuinely passed and the ladder genuinely renders; what nothing in M3 ever measured
is *how old the book on the panel is*. It is about a hundred seconds old, and getting older.

**Read first**

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §6 | Frozen. #5 is the one this stage stresses, and it turns out to have a hole. |
| `ARCHITECTURE.md` §9, entry 2026-08-09 (`kRxWatchdogMs`) | Contains the per-socket shedding finding this stage **corrects**. Read it before you read deliverable 0. |
| `tools/anvil_drain_probe.py` | The measurement that produced that finding, and the one whose method has the gap. |
| `docs/briefs/M3-live-anvil-on-the-panel.md` | Milestone DoD and full session log. |
| `docs/briefs/M3-stage-D-the-panel.md` | The stage this follows, and its DoD — note what it measures and what it does not. |
| `firmware/src/frame_pipe.hpp` | `kWsRxBufferBytes` and the note explaining why it was deliberately left at 4096. That reason has expired. |
| `firmware/README.md` | "What runs where", the reading table, and the **`dc_feed` never logs** rule. |

**Depends on:** Stage D, landed and merged. Bench captures in `hardware/`.

---

## The finding

A clean bench run on 2026-08-11, single connection, **effectively zero transport error** —
`parse=0 cont=0 trunc=0 oversize=0 qfull=0 abandoned=0`, `no_slot=2` of 1,703, `connects=2`.
Every message that arrived was whole and was adopted. Nothing was corrupted, reordered or
mis-parsed, and the two slot misses cost two refreshes of an idempotent full replace.

The web client was paused at 12:05:02 showing `last 10.0216`, `bid/ask 10.0064 / 10.0081`.
The panel reached `bid 10.0034 → 10.0064` at 12:06:38.735 and `last=10.0216` at 12:06:40.028.

**The panel took 96–98 seconds to display a state that already existed when the reference was
frozen.** Board uptime at the moment of the pause: 210 seconds.

Two symptoms that had looked like separate bugs collapse into this one:

- **The "frozen bid".** `bid 10.0034 x67` held identical across 502 consecutive published
  versions and 87 seconds. It was not stuck. The board was replaying a real stretch of history —
  roughly 12:03:30 to 12:05:00 — during which the bid genuinely did not move.
- **The "5 Hz refresh".** The board receives ~41% of the stream. `summary` is Anvil's fixed 2 Hz
  timer broadcast and arrives at **0.82/s**; `book` arrives at **5.60/s** against 13.44/s. Two
  independent counters, the same ratio.

**The missing 59% is not being discarded. It is queuing.** That is what the lag *is*: at a 41%
drain rate the board falls behind by ~0.6 seconds for every second the socket stays open.
Predicted lag at 210 s of uptime is ~124 s; observed ~98 s, which is the right number given
Anvil's current rate is below its 20-minute average.

**Where the queue is: not on the board.** Four 16 KiB slots, `behind=2/6`, `a→e worst=20 ms`,
both cores 85–92% idle. Ninety-eight seconds at 7 KB and 7 msg/s is four to five megabytes. That
can only be an application-level send queue on the server side.

**Why the earlier 86-minute run looked better: it had 21 reconnects.** Every reconnect discards
the backlog with the socket and Anvil sends a fresh snapshot, so the lag was being flushed every
four minutes or so. The flaky link was concealing the defect. That is also the shape of the
prediction this stage exists to confirm.

### The evidence that has to be corrected, and how it was wrong

ARCHITECTURE §9 (2026-08-09) records: *"Anvil sheds to a slow consumer, and sheds evenly — 4×
fewer messages, gaps unchanged."* That came from `anvil_drain_probe.py`, which measured **rate**
and **inter-message gaps** at several artificial drain speeds.

Both of those observations are equally consistent with pure queuing. A client draining at 4/s
receives something every ~250 ms whether the thing it receives is current or ninety seconds
stale. **The probe never checked freshness, and nor did anything else in M3.** The conclusion is
not merely unproven — it is load-bearing in the constitution and it is probably wrong.

This is the same failure mode the milestone has now paid for four times: a measurement that
answers a nearby question and gets read as answering the one that mattered.

---

## What this stage is NOT

Named explicitly, because two of these were live discussions that never reached the repo and one
is a red herring that has already cost an hour.

- **Not the panel layout.** The row budget, the two-pixel sparkline, the palette swap, the
  spread rule, the bar scale — all real, all recorded in *Deferred* below, all after this.
- **Not the orientation.** The bench photo was taken with the module on its side. There is
  nothing wrong with the renderer's geometry and nothing to rotate.
- **Not the RX watchdog.** `kRxWatchdogMs` stays at 1000 ms and
  `ReplayOptions::disconnect_gap_ms` is not touched. Settled twice already.
- **Not M4.** Kraken does not start until the panel can say how old it is.

---

## E1 — measure it and make the board say it *(one evening)*

### 0. The writeback, first, before any code

Three docs changes, no firmware:

1. **A session-log entry** in `M3-live-anvil-on-the-panel.md` recording the finding above with
   its numbers, and stating plainly that Stage D's acceptance passed while never measuring
   freshness.
2. **An ARCHITECTURE §9 amendment** against the 2026-08-09 shedding entry. Do **not** delete or
   rewrite the original — the project's rule is that §9 records decisions and their reasoning,
   and the reasoning here is the valuable part. Add an entry saying the shedding conclusion rests
   on rate-and-gap measurements that cannot distinguish shedding from queuing, that a board
   observation now contradicts it, and that the durable rule is **measure freshness, not
   cadence.**
3. **An Anvil backlog line** in DepthCharge's own ROADMAP cross-reference. The existing item is
   *"Document per-socket coalescing / backpressure-shedding in PROTOCOL.md"*; it becomes *"...and
   establish whether it bounds queue depth — a DepthCharge board on one socket accumulated ~98 s
   of backlog in 210 s of uptime."* **Anvil is not modified from here**, and no Anvil change is
   proposed. This is a note on their backlog, nothing more.

### 1. Confirm the lag-versus-uptime curve

This is the measurement that turns the hypothesis into a fact, and it also settles a second
question the numbers cannot: **does the lag grow without bound, or plateau?** Pure queuing grows
linearly forever. Partial shedding plateaus. The two want different fixes and the curve tells
them apart in one run.

Protocol, owner-driven:

- Boot the board. **One connection, no deliberate disconnects** — if `connects` moves past 2 or
  `sock_gaps` past 1, the run is void and the queue has been flushed. Note that.
- At **1, 2, 5, 10 and 20 minutes** of uptime, freeze the web client and record the wall-clock
  interval until the panel displays the same `last` price. That interval is the lag.
- Run `tools/capture_anvil.py` from the desk for the whole session as the control, so the
  venue's own rate over the same window is on record rather than assumed.
- Plot lag against uptime. Linear confirms queuing; a plateau names the ceiling.
- Commit as `hardware/bench-YYYY-MM-DD-feed-lag.md`.

CC's job here is the tooling and the write-up, not the run.

### 2. The staleness meter — and it is free

`summary` is a fixed 2 Hz broadcast. The board already counts it. **The deficit between 2.00/s
and the observed rate, integrated over the connection, is the accumulated lag** — no desk
capture, no server change, no new protocol field.

- Estimator lives in a new `firmware/src/staleness.hpp`, **ESP-IDF-free and host-tested**,
  following the precedent of `frame_reassembler.hpp`, `gap_histogram.hpp`, `stall_probe.hpp` and
  `ws_supervisor.hpp`. Every piece of firmware logic the desk can check should be checkable on
  the desk.
- Reset on connect. The backlog dies with the socket, so the estimate must too.
- Print it on the stats block in seconds, beside the ratio it is derived from, so the bench can
  see both the instantaneous drain fraction and the accumulated age.
- **State its assumption in the header**: this measures lag *only if* the missing frames are
  queued rather than dropped. If deliverable 1 shows a plateau, the estimator over-reads and the
  header must say by how much. Calibrate it against deliverable 1's stopwatch figures — that
  pairing is the whole reason the two land in the same evening.

### 3. The invariant question — propose, do not merge

Invariant #5 protects against *stopped* and against *wrong*. It has no concept of *old*.
`DisplaySnapshot` carries no age, and `GapReason` is `{SeqGap, ChecksumFail, Disconnect,
Overflow, Resync}` — there is no member meaning "this book is too old to trust".

**Adding one is a §4 change to the `FeedEvent` vocabulary, which is stop-and-raise, not a
session's call.** Do not add it. Do not repurpose `Resync` to mean staleness — that would be
exactly the kind of quiet semantic drift the vocabulary exists to prevent.

What this stage delivers is the *proposal*, written up in the session log for the owner:
what a staleness `Gap` would mean, what threshold it would fire at and on what evidence, what it
would do to the goldens (a new reason is a new rendered state and the reconnect golden may move),
and whether the honest alternative is a §5 change putting an age on `DisplaySnapshot` instead.
Then it is Gareth's decision, taken once, recorded in §9.

---

## E2 — stop the queue forming *(one evening, gated on E1)*

E2 is second **because E1 builds the instrument that tells you whether E2 worked.** Pulling
levers before the meter exists is how this milestone lost two evenings already.

**The bar is the summary ratio: 2.00/s received, or as near as the link allows.** A board that
keeps up never builds a queue and the lag question disappears at the root.

Levers, ranked by reachability rather than by suspicion — and note the board is **not** short of
CPU or bandwidth. Both cores idle at 85–92%, and 7 msg/s × 7 KB is 49 KB/s. Nothing here is
throughput-limited; the arrival distribution is modal in the **100–250 ms** bucket against
Anvil's 63 ms p50, which is the fingerprint of a per-message round-trip stall, not of a slow
client.

1. **`kWsRxBufferBytes`, 4096 → above the largest observed message (~9 KB).** One DATA event per
   message instead of 2.7, which removes two `esp_event` dispatch hops and two partial-window
   round trips per frame. `frame_pipe.hpp` records that this lever was deliberately *not* pulled
   at Stage C, to keep chunk reassembly exercised on the wire. **That reason has expired** — the
   wire has proven reassembly works, and `cont`/`SPLIT@` rejects give a better reason to want
   fewer chunk boundaries. Reachable: it is a config field, not an sdkconfig symbol.
2. **The lwIP receive window.** `CONFIG_LWIP_TCP_WND_DEFAULT` ships at 5,744 B, smaller than one
   book frame, so a whole message cannot be absorbed without a window update. **Check whether it
   is reachable at all before planning on it** — this is a *precompiled* Arduino framework, and
   this vintage has already decided eight designs by not exposing something (`esp_crt_bundle`,
   `heap_trace_start`, `reconnect_timeout_ms`, `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`, the
   blocking `stop()`, the self-leaking `setupDMA()`, the non-blocking buffer flip, the
   `AUTH_FAIL` reconnect list). If it needs a rebuilt framework, that is a milestone-weight
   decision, not this evening's — record it and stop.
3. **WS task priority and core placement.** The client task is unpinned and shares Core 0 with
   Wi-Fi and lwIP.

Re-measure the summary ratio after each lever, one at a time, and record what each bought. If
none of them moves it, the finding is that the bottleneck is below the firmware and it goes to
the bench record as such — a measured negative is a result.

---

## Guardrails

- **All §6 invariants apply and are frozen.** If a step seems to need violating one, stop and
  raise it. In particular: **nothing in this stage touches `engine/`**, and the `GapReason`
  question above is exactly the case where the answer is *ask*, not *implement*.
- **A moved golden means stop.** E1 and E2 are `firmware/` and docs only.
- **`cmake --workflow --preset host` stays green on every commit.**
- **`dc_feed` contains no `ESP_LOGx`.** Still true, still non-negotiable.
- **The new estimator is host-tested or it does not land.** The precedent is unbroken and it is
  the reason this firmware is debuggable at all.
- **Commit only when asked.** Report what changed and what it measured; Gareth decides.
- **Do not reopen** the watchdog constants, the panel orientation, or the layout list below.

## Known unknowns — resolve and record

**Resolved 2026-08-11:**

- **Whether the lag is linear or plateaus.** ~~Open.~~ **Linear at the desk**, +0.745 s/s, no
  plateau anywhere in 111 s of backlog, two independent derivations agreeing to three decimals
  (`hardware/bench-2026-08-11-feed-lag.md`). Still to confirm on the board's own stack.
- **Whether `CONFIG_LWIP_TCP_WND_DEFAULT` is reachable on a precompiled framework.** ~~Open.~~
  **No.** `lwipopts.h` has `#define TCP_WND CONFIG_LWIP_TCP_WND_DEFAULT` with
  `ESP_PER_SOC_TCP_WND 0`, and `liblwip.a`'s `tcp.c.obj` carries 5,744 as a link-time literal
  (`.literal.tcp_alloc + 4` = `0x00001670`, stored into `rcv_wnd` at `tcp_alloc + 0xe0`; `+8` is
  `0x16701670`, both window fields at once). Our build cannot recompile an archive. **Reaching it
  is a framework rebuild — milestone-weight, recorded, stopped**, exactly as this brief instructed.
- **A third, not asked but answered by a 30-minute unthrottled desk socket:** Anvil has not slowed
  (**15.70 msg/s** against M0's 15.5), `summary` really is **2.0003/s** — a 499.9 ms period, exact
  to 0.02% — so the estimator's denominator is exact, and **the socket held all 1,799 s without a
  drop**, which eliminates "Anvil closes everyone on a timer" as a cause of the board's
  every-few-minutes disconnects.
- **A fourth, self-inflicted and recorded because it is this stage's own lesson:** a 60-second run
  of the same probe reported 15.89 msg/s and 2.017/s. Both were `n/span` where the span holds
  `n−1` intervals — a +0.84% bias at n=120. The 2.017 was believed, and an analysis of estimator
  drift plus a predicted false alarm was built on it before the 30-minute run dissolved it.

**Still open:** Whether the 100–250 ms modal inter-arrival is a delayed-ACK interaction, a
window-update stall, or something else — and whether it can be identified at all without a packet
capture the board cannot produce. Whether raising `kWsRxBufferBytes` costs anything now that the
reassembly path has been proven on the wire. Whether the desk's linear-queuing result transfers to
the ESP32's own TCP/TLS stack. What a *healthy* board reads on `-- age`, which is the floor any
staleness threshold has to clear.

## Definition of done

- [x] Session-log entry written; §9 amendment recording that the shedding conclusion measured
      cadence and not freshness; Anvil backlog line updated. *(2026-08-11. The §9 amendment goes
      further than "cannot distinguish": the desk probe **refuted** the shedding claim outright.)*
- [x] Lag-versus-uptime curve measured ~~on a single connection~~ and committed to `hardware/`,
      with a ~~simultaneous~~ desk capture as the control. *(2026-08-11, and it departs from the
      wording twice on measured grounds. **Not one connection: sixteen.** The socket does not
      survive long enough for a single-connection run to exist, and it does not need to — the
      board prints a sample of its own curve every ten seconds, so each connection is a complete
      curve from zero to peak and sixteen of them is better data than one. **Not a stopwatch:**
      that method measures age ÷ f and is biased by ~2.4× (see the file). 390 samples over 65
      minutes; every connection linear, no plateau; peak age **328.6 s**.)*
- [x] `staleness.hpp` lands, host-tested, ESP-IDF-free, resetting on connect, with its assumption
      stated in the header and calibrated against ~~the stopwatch figures~~ **the board's own
      curve**. *(2026-08-11. `harness/tests/test_staleness.cpp`, 14 cases. **Calibration closed,
      and by a better route than the DoD asked for:** the lag *slope* and the cumulative *summary
      deficit* are two independent derivations of 1 − f from different fields of the same log
      line, and they agree to 0.000–0.008 on every connection with more than two samples — median
      0.599 against 0.601. The stopwatch pairing the DoD named is unusable and the reason is
      recorded in the header.)*
- [x] The stats block prints accumulated age in seconds and the instantaneous drain fraction.
      *(`-- age`, immediately under `-- rate`, because they are one reading.)*
- [x] The staleness-`Gap` proposal written up for the owner — meaning, threshold, golden impact,
      and the §4-versus-§5 alternative. **Not implemented.** *(Session log below. The
      recommendation is §5, and the reason is structural rather than a preference.)*
- [x] E2: each lever tried in order, summary ratio re-measured after each, results recorded
      including the ones that bought nothing. *(2026-08-11. **Every lever this brief listed is a
      measured negative, and the cause is outside all of them.** The board is bandwidth-delay-product
      limited: a 5,744 B TCP receive window across an **87 ms** RTT to Anvil — which is in AWS
      us-east-1, not on the LAN, a fact no session had ever measured — is a 65.5 KiB/s ceiling
      against a 110.4 KiB/s stream. Lever 1 (`kWsRxBufferBytes`) is refuted twice over: it is the
      `len` argument to `mbedtls_ssl_read`, above the TLS record layer and invisible to lwIP; and
      reads-per-message already varied 26% across the run at constant byte rate, so the experiment
      ran itself and returned zero. Lever 2 (`CONFIG_LWIP_TCP_WND_DEFAULT`) is the one that would
      work and is unreachable without a framework rebuild — recorded and stopped, as instructed.
      Lever 3 (WS task priority) cannot reach a constraint that is not CPU. Full record in
      `hardware/bench-2026-08-11-feed-lag.md`.)*
- [x] M3 DoD updated to carry a freshness line, because it never had one.

## Out of scope

M4 and anything Kraken. Any change to `engine/`, `FeedEvent`, `DisplaySnapshot` or the watchdog
constants. Any change to Anvil. The carrier PCB, the enclosure, OTA, provisioning. And the
legibility list below, which is deferred but no longer undocumented.

## Deferred — recorded here so it is not lost a third time

These are real, they came out of the 2026-08-11 review against the project-start mock-up, and
they have twice been agreed in conversation and then existed nowhere. They are a brief of their
own once the panel is honest about age.

- **The bottom strip is two pixels tall.** The mock-up gives the price chart roughly a quarter of
  the panel; `kStripRows` is 2 and the code calls it "a tape, not a chart". Cause: §5's
  parenthetical — *"top ~27 levels/side (fits 64 rows with header, spread gap, sparkline strip)"* —
  is arithmetically false at one pixel per level. 27+1+27 is 55 rows and nine will not hold a
  header, two rules and a chart. `kLevels` is derived and clamped, so dropping it to ~20 frees
  ~14 rows without touching `kDisplayLevels`. **Whether §5's text gets corrected is an owner
  decision.**
- **The palette is swapped against the mock-up.** Mock-up: white spread marker, amber last price
  and tape. Build: `Ink::Value` white, `Ink::Spread` amber, `Ink::Symbol` cyan.
- **The spread draws full width** (`hline(0, kSpreadRow, kPanelWidth, …)`) as a divider between
  the sides. The mock-up treats it as a short marker at the touch.
- **Bars have no right margin.** `bar_length` returns the full 64 px for anything at
  `window_max_qty`, so the longest bar always touches the edge and the scale re-normalises every
  frame — absolute size changes are normalised away and the ladder reads as a wall.
- **The header ghosting and the "green line at the right" are one defect.** 1/32 scan pairs rows
  0–4 with 32–36, so the header shares a shift-register slot with `asks[0]`, the spread and
  `bids[0..2]`, and the right-aligned price glyphs bleed green into those bid rows at the same x.
  Two-minute test: blank the header, or left-align the price, and see whether the stack moves.
  If it confirms, moving the header off rows 0–4 fixes both symptoms — and since the row budget
  is being rewritten anyway, it is free.
- **The mock-up is not in the repo.** It has never been an input to any brief, which is why none
  of the above was caught. It should be committed as the named visual spec with the DoD comparing
  against it.
- **Rank versus price axis.** Rows are ordered by price rank, not plotted against a price axis, so
  a moving market does not slide the picture and gaps in the book collapse. Fine on Anvil, an open
  question at Kraken, where 27 rank-ordered levels may span a trivial price band. Price-bucketed
  aggregation is an M4 design question that currently exists nowhere.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     measured figures / serial evidence · exact next step. -->

### 2026-08-11 · Opus 5 · E1 landed, minus the bench half — and the shedding claim is refuted, not merely unproven

**Done.** Deliverables 0, 2 and 3 complete; deliverable 1's *desk control* complete and its board
half owed. Full account in the M3 brief's session log; this entry carries the two things that
belong to this stage rather than to the milestone — the measurement that changed the finding, and
the invariant-#5 proposal.

**The finding is stronger than the brief predicted.** The brief said the shedding conclusion "is
not merely unproven — it is load-bearing in the constitution and it is probably wrong". It is
wrong. `tools/anvil_freshness_probe.py` (new, committed) opens two sockets from one process — one
drained flat out as a reference, one sleeping 250 ms per message — and matches Anvil's **global**
wire `seq` across both, so the staleness of a message is a subtraction against a real clock rather
than an inference from a rate. Over 150 s:

- `book` 3.48/s of 13.66/s = **25.5%**; `summary` 0.51/s of 2.01/s = **25.7%**. **Every kind is
  thinned by one fraction.** The 2026-08-09 per-kind reading — `summary` and `trade` intact, only
  `book` shed — does not reproduce at the same drain delay. A uniformly delayed byte stream does
  this; per-kind coalescing cannot.
- Lag rose **linearly through all eight bins to 111 s**, no plateau, at **+0.745 s per second**
  measured by seq-matching and **+0.745 s/s** predicted independently from the drain fraction
  alone.
- Implied per-socket application queue at disconnect: **~1,746 messages, ~12.4 MB**, still growing.

So the answer to the brief's second question — *does the lag grow without bound, or plateau?* — is
**linear, unbounded to at least 111 s**, at the desk. The board still has to confirm it on its own
stack, because a desk client reproduces a slow *reader*, not an ESP32's TCP window, TLS record
handling or radio.

**A 30-minute unthrottled desk socket closes three arguments running since stage C.** Anvil is
**15.70 msg/s** (M0 measured 15.5 in July), `summary` is **2.0003/s** — a 499.9 ms period against
the assumed 500.0 — `trade` is 0.15/s, and **the socket held all 1,799 s with no drop**. So: the
venue has not slowed; the board's 6.07 msg/s is the board's; the estimator's denominator is exact;
and Anvil does not close sockets on a timer, which eliminates one candidate for the board's
every-few-minutes disconnects without touching the bench.

The `trade` figure settles a fourth: the 46-minute soak's board saw 0.047 trades/s against the
wire's 0.15/s = **31%**, right beside its ~36% drain of every other kind. **No per-kind anomaly on
the board** — which is what queuing predicts and coalescing would not.

---

## Deliverable 3 — the staleness-`Gap` proposal, for the owner (NOT IMPLEMENTED)

Recommendation up front: **do not add a `GapReason`. Put an age on `DisplaySnapshot` (§5) and
extend invariant #5 to cover *old* as well as *stopped*.** The reason is structural, not a
preference, and it was not obvious before writing the estimator.

### Why the `Gap` route does not work, and this is the finding of the exercise

Invariant #5's state machine is: *any `Gap` greys the panel; **only a `Snapshot` clears it**.* That
is exactly right for absence of data, which is what every existing `GapReason` describes —
`SeqGap`, `ChecksumFail`, `Disconnect`, `Overflow`, `Resync` are all "something is missing".

Staleness by age is not absence. It is the **presence of old data**, and under queuing the board is
receiving a dense stream of `book` frames which the phase-1 book adopts as `Snapshot`s at 5.6/s.
So a `Gap{TooOld}` raised on the age crossing a threshold **would be cleared by the very next
stale book frame, ~180 ms later.** The panel would not grey; it would flicker, once per hole in
the arithmetic, which is worse than saying nothing because it is a lie with a mechanism.

Making it work means changing the *clear* condition from "a `Snapshot`" to "a `Snapshot` that is
**fresh**" — which puts the age inside the book's state machine, in `engine/`, where the age is
not knowable: the age is a transport-side measurement (a summary deficit against a wall clock) and
`engine/` is deliberately clock-free and replay-driven. **So the §4 route does not stay inside §4.
It reaches §5 and `engine/` anyway, and by the longer road.**

### The §5 alternative, which is the recommendation

Add one field to `DisplaySnapshot`:

```cpp
std::uint32_t age_ms{0};   // how old the book state is; 0 = unknown/not measured
```

- **Written by the feed task** at publish time from `StalenessEstimator::lag_us`, which already
  exists and is already reset per connection. No new mechanism.
- **`FeedEvent` is untouched.** §4 does not move, the `GapReason` vocabulary does not move, the
  book's stale/live state machine does not move, and no adapter has to learn anything.
- **The render side decides what to do with it**, which is where a presentation decision belongs.
  Two options, and they compose:
  1. **Numeric, in the header.** `101 +97s` in the value slot when the age is over the threshold,
     in place of the last price. Cheapest honest thing the panel can do, and it is the one that
     scales to a venue whose lag is normal.
  2. **A third palette.** Stage D made palette selection structural: `palette_for(FeedStatus)`
     returns a `Palette`, `kStalePalette` is proven all-grey by `static_assert`, and the renderer
     cannot name a colour. A `kAgedPalette` — dimmed, hue retained, or a single amber wash — drops
     into that machinery with the same compile-time proof, and `test_ladder_render.cpp` already
     has the shape of the test (the `Ink` grid must be identical across palettes; only hue moves).
- **`FeedStatus` stays two-valued.** Aged is a *modifier* on Live, not a third status, precisely so
  that nothing which currently branches on `snap.live()` changes meaning.

### Threshold, and on what evidence

**Proposed: fire at 2,000 ms, clear at 1,000 ms (hysteresis), and do not fix the number until the
board run says what a healthy board reads.**

The reasoning: the estimator's resolution is one summary period, so a socket that is exactly
current reads a **0–500 ms sawtooth** and any threshold at or below 500 ms fires on nothing.
Anvil's worst healthy inter-frame gap is 391 ms and it republishes the whole book every ~80 ms, so
a second of age is already outside anything the wire produces. 2,000 ms is 4× the estimator's
quantum and ~5× the worst healthy gap — the same kind of margin `kRxWatchdogMs` was derived with,
and derived the same way, from measurements rather than from roundness.

**But the board's healthy floor is not known yet.** If a board keeping up still reads 700 ms of
age because of its own scheduling, the threshold has to clear *that*, and only the lag-versus-uptime
run can say. Setting the number before then would repeat the 2026-08-09 watchdog mistake in
miniature — the constant that was going to be "re-derived upward" on a premise that turned out to
be false.

### Golden impact

- **The §5 route moves no golden by itself.** `age_ms` defaults to 0 and the committed goldens pin
  status, reason and levels. The host replay driver *could* compute an age (a trace has `rx_ns`
  and carries `summary` frames, so the same deficit arithmetic runs on a file) — and it **should**,
  because that turns the estimator into replay-covered behaviour rather than firmware-only
  behaviour. Both committed traces are healthy desk captures and would read ~0, so a new golden is
  needed rather than an existing one moving.
- **The trace for it already exists in shape.** `tools/anvil_drain_probe.py --out-prefix` writes a
  throttled stream in capture format — `harness/replay/_local/drain-120ms.ndjson` is one — so a
  committed throttled capture would be a genuine ground-truth trace of a *queued* stream, and the
  first one this project has. That is the cheapest way to satisfy invariant #6 for this feature.
- **The §4 route moves several.** A new `GapReason` member fires `-Wswitch` under `-Werror` in
  five places, which is the good news — `reason_text()` in `ladder_render.hpp`, `reason_name()` in
  `render_task.cpp`, `reason_text()` in `harness/src/console_ladder.cpp`, and the exhaustive
  reason loops in `test_book.cpp`, `test_console_ladder.cpp` and `test_ladder_render.cpp`. It also
  adds a new rendered header string and therefore a new rendered state, and the reconnect golden
  is the one at risk, since it is the only committed trace with real stale episodes.

### What is being asked

One decision, taken once, recorded in §9: **§5 field, or nothing yet.** Nothing in the firmware
today depends on the answer — the estimator prints and does not branch — so "nothing yet" is a
real option and costs only that the panel remains honest about *stopped* while being silent about
*old*.

---

---

## The board half landed the same evening — 328 seconds

Owner reflashed and ran; 65 minutes of log analysed by `tools/board_log_lag.py` (new, committed).
Full record in `hardware/bench-2026-08-11-feed-lag.md`. The four things that change this stage:

1. **Peak age 328.6 s.** Three connections reached 309–329 s, on a run with `wd_gaps=4` in 65
   minutes and `worst_gap=5303 ms` — **the feed never stopped.** It flowed at 40% of the wire and
   fell further behind. Every other instrument in this firmware read healthy throughout.
2. **Sixteen connections, sixteen straight lines, no plateau.** The desk's queuing result
   transfers to the ESP32 unchanged. Median slope +0.599 s/s ⇒ *f* = 0.401.
3. **The estimator calibrated itself.** The lag slope and the cumulative summary deficit are
   independent derivations of 1 − *f* from different fields of one line; they agree to 0.000–0.008
   per connection. **E1's calibration is closed**, by a better route than the DoD specified.
4. **The queue-bound hypothesis is refuted.** Drop intervals 3–575 s, CV 0.87 — scattered, not
   regular. Recorded as a null result. Cause narrowed but open: not signal (−43 dBm), not
   association, not CPU (89% idle), not a spurious handle event (`connects` 16 = 16 socket-up
   lines), not a fixed queue size. `link=19` on the stall probe is **not** evidence for the radio —
   that verdict means "Core 0 had headroom and nothing to do", which is what a board fed at 40% by
   a queuing server looks like. The classifier has no category for this and predates the question.

---

## E2 answered the same evening, and the answer is that E2 cannot be done

**The board is bandwidth-delay-product limited. No firmware lever reaches it.**

```text
Anvil = 52.204.246.224  (AWS us-east-1, Virginia; the board is in the UK)
RTT   = 87.4 ms median over 8 TCP connects   <- never measured before; ICMP is filtered
ceiling = TCP_WND / RTT = 5,744 B / 0.087 s  = 65.5 KiB/s
Anvil's stream                                = 110.4 KiB/s
=> at most 59% of the feed at perfect efficiency; measured 44.2 KiB/s = 40%
```

44.2 / 110.4 = **0.40**, the drain fraction, from first principles with nothing fitted.

**Every candidate was eliminated by one measurement that had been sitting in the logs**: across
548 healthy windows the inbound *byte* rate is flat (41–44 KiB/s, max 49.8) while message rate
varies 2.6×, message size 2.3× and reads-per-message 1.9× — and it is 42.70 KiB/s on the
2026-08-09 run with **no panel at all**. Byte rate moves 4% while everything proposed as a cause
moves by factors. That kills every per-message mechanism, kills `kWsRxBufferBytes` (the natural
experiment already ran), and kills the panel-emissions candidate. `driver ps=0` kills power save.

**The lever list in this brief was wrong in its premise, and the object code says why.**
`cfg.buffer_size` is the `len` argument to `mbedtls_ssl_read` — above the TLS record layer,
invisible to lwIP, unable to change bytes in flight or window timing. There is no artificial delay
anywhere on the connected path: no `vTaskDelay`, no event-group wait, and the read loop drains a
whole frame per iteration. The library was never pacing anything.

**Stop-and-raise, as this brief instructed.** `CONFIG_LWIP_TCP_WND_DEFAULT` and
`CONFIG_LWIP_WND_SCALE` are compile-time in the shipped `liblwip.a`. Reaching either is a framework
rebuild (pioarduino / IDF from source) — milestone-weight, and now the only route to a board that
keeps up. ARCHITECTURE §9 carries the finding because it changes what M4 and M5 may assume: **a
venue's byte rate is a design input, sized against `WND / RTT` before an adapter is written.**

**Exact next step — owner's decision, three options and they are not exclusive:**

1. **Rebuild the framework** (pioarduino / IDF from source) to raise the window to ≥11,488 B. Fixes
   it outright at 2×; re-runs M2's first light and every precompiled-vintage finding in §9.
2. **Reduce what crosses the Atlantic.** Anvil's backlogged *sequenced incremental L2 feed* is now
   load-bearing rather than optional — a delta feed at a tenth the bytes fits inside the existing
   window. Anvil is not modified from here; this is a note on their backlog.
3. **Ship it honest.** The panel now reports its own age; the staleness-`Gap` proposal is how the
   ladder itself would say so. This is the only option available without a decision elsewhere.

Not gating any of the above: the throttled desk probe run long **against a quiet server** still
separates "the backlog drops the socket" from "the board's link does" — the drop cadence is
scattered and remains unexplained. It must not run beside a bench session.
