# M5 Stage D-C — the soak

**Track:** Bench [owner-driven, wall-clock] · **Status:** **Started and not done — one run taken
2026-08-30/09-01 and read; §1's > 24 h is NOT met (7 boots, longest 9.84 h), so a second run is
owed. §2's four gaps are closed by D-A3 and confirmed from the capture; see the session log. AND
THE SECOND RUN NOW CARRIES D-A4's BOARD BOX — that stage landed 2026-09-06 with its mechanism
proven on the host and unflashed, and §4's check 7 is the reading it is owed** ·
**Size:** a desk sitting, then a run longer than a day, then a reading
**Written:** 2026-08-30 by the desk seat at D-B's close.

**This is the one definition-of-done clause only wall-clock can close.** Everything else in M5 has
been settled by a host test, a measurement, or an evening at the panel. **Seven** checks have
collected here across six stages, each deferred because the thing it tests only happens over hours.
The seventh arrived last, on 2026-09-06, when D-A4 finished a mechanism it could not put on a board.

**And the first thing this brief has to say is that none of them can be read today.** Three of the
six need an instrument that does not exist, one needs a policy the firmware never receives, and the
tool that is supposed to read the result matches **zero lines** of a current board capture. Every
one of those was verified against the tree while writing this, not inferred. **A 24-hour run cannot
be re-asked**, so the instrument work is the first half of this stage and the run is the second.

**Read first**

| Source | Why |
| --- | --- |
| `M5-stage-C-…md` § *Owed by stage D*, **rows 3, 4, 5** | Three checks with their numbers, and why each was deferred. Row 3 is named as the soak's *first*. |
| `M5-stage-D-the-shape-and-three-decisions.md`, the sitting table | The one hard constraint: **> 24 hours**, and why that number. |
| `firmware/src/panel.hpp:181-215` | The block headed *the named check this decision owes the soak (D-C), with its number* — read the code, not the brief that quotes it. Its stated remedy is stale; §5 below. |
| `harness/replay/NOTES-binance.md` **§9** of the D-A2 addendum (:2043) | The frame-pipe reading order. It supersedes §8, which superseded §7, which superseded §6 — **and ROADMAP.md:28 still states the withdrawn one.** |
| `M5-stage-D-A2-…md`, *Not done, and owed* | Why the fetch is not the sampling point for the heap tripwire. |
| `ARCHITECTURE.md` §9, both 2026-08-30 rows | Written the day before this brief and both bear on a soak: what an observation window entitles you to say, and what a verification is entitled to claim. |

**Depends on:** D-B ✅ (`05e05d6`) — **and D-A3, not optionally.** **Blocks:** the M5 close-out.

---

## 1 · The one hard constraint

**The run must exceed 24 hours.** Binance closes the connection at 24 h by policy, so a shorter run
has not observed the one disconnect the venue guarantees.

> **THE PREMISE IN THAT SENTENCE IS REFUTED AND THE REQUIREMENT SURVIVES IT.** Stage E's soak ran
> **27.59 h on ONE connection with no close of any kind** — `hardware/bench-2026-09-04-E-soak.md` §4,
> carried as backlog **D9** — so the venue's documented 24 h close was **not** observed on a
> connection that outlived it by 3.6 h, on this host and on one sample. The bar stays at > 24 h
> because everything else here needs the hours anyway, but **it is no longer "the disconnect the
> venue guarantees" that justifies it**, and a run that sees no scheduled close is not a run that
> failed. Recorded rather than rewritten: this is D9's to settle, not this section's. **M3's 23.6-hour soak would have missed it
by twenty-four minutes.** The supervisor already reconnects, so this is probably benign — but it is
the first *scheduled* disconnect this project has met, and "probably benign" is what a soak is for.
M4 stage D's B3 is the comparison bar: 25.39 h, no reboots, `live=1` at the end.

## 2 · Why this cannot start — four gaps, all verified in the tree

**(a) The liveness signal is not wired, so the clock has zero samples.** `venue_build.hpp:325` ships
`liveness_count()` returning a constant `0`, with `kLivenessSignal = "server-ping (NOT WIRED — see
venue_build.hpp)"`. `armed_` is never set and `expired()` is permanently false. The board prints
`median 0 ms, grey at 30000 ms after 0 sample(s) UNCALIBRATED`. **D-A3's.**

**(b) The per-venue liveness POLICY never reaches the firmware, and this is a SEPARATE change from
(a).** `harness/include/dc_harness/venue.hpp:298` holds Binance's `{multiple = 2.0, ceiling_ms =
60000.0}`. `firmware/src/liveness_watchdog.hpp:316` default-constructs `LivenessClock clock_;` and
**no `LivenessPolicy` appears anywhere under `firmware/`** — verified by grep. So the board runs
multiple **4.0** and ceiling **30,000 ms**, and stage C's derived **39,927.94 ms** threshold exists
only in the harness. Checks 1 and 2 below are written against a policy the board does not have.
Raise this with D-A3 explicitly: wiring the ping does **not** route the policy.

**(c) There is no reconnect-time largest-block instrument**, and that is the sampling point the most
load-bearing check specifies. `heap_caps_get_largest_free_block` is called in exactly three places —
`heap_probe.cpp` (periodic, 10 s), `panel.cpp` (DMA caps, once at begin) and `rest_fetch.cpp`
(fetch-scoped). `ws_transport.cpp:637`'s socket-up line prints `dns / connect+upgrade / fd / rssi`
and **no heap figure**. The `SOAK` line's `largest=` is a periodic sample, not a reconnect-triggered
one.

**(d) `tools/soak_report.py` matches ZERO lines of a current capture.** Run its own regexes over
`device-monitor-260829-155908.log`: `RE_SOAK` 0, `RE_PIPE` 0, `RE_STALE` 0, `RE_LIVE` 0,
`RE_SOCK_UP` 0. Two independent breakages, and a third hiding behind them:

- `monitor_filters` (`platformio.ini:215`) prefixes every line with `HH:MM:SS.mmm > `, so every
  `^`-anchored grammar fails. De-prefixed, `RE_SOAK` recovers to 20 and `RE_LIVE` to 5.
- **`RE_PIPE` still matches 0 even de-prefixed**, because it expects `no_slot=(\d+) qfull=(\d+)`
  and the board now prints `no_slot=3 max_held=4 of 4 qfull=0` (commit `b9a37eb`). It is guarded by
  a bare `if pipe:`, so the whole pipe section **drops silently** — no error, just three counters
  absent from the report. That is the wave-through §9's 2026-08-30 row is about.
- There is no grammar at all for `-- frame`, `-- slot`, `-- slots`, `-- age`, `-- ping`, or the
  per-fetch heap line.

## 3 · Deliverables

**The four gaps in §2 are NOT this stage's work — they are D-A3's**, per D-A2's *Out of scope*
(*"the liveness ping wire, and the soak instrumentation — D-A3"*) and now
`M5-stage-D-A3-the-wire-and-the-instruments.md`, which carries all four as its deliverables 1-4
(the re-seed mechanism and the marker split off to **D-A4**). **This stage starts when they land**,
and its own deliverables are the run and the reading.

> **THAT CLAUSE SAID *"D-A4 … does not block this stage"*, AND IT IS NOW THE OTHER WAY ROUND.** It
> was written on 2026-08-30, when D-A4 was unstarted and the dependency ran one way: D-C did not
> need the mechanism in order to soak. **D-A4 landed 2026-09-06** with everything provable on a desk
> proven — ctest 52/52, `pio` green on three arms, the whole `None → Wanted → InFlight → None`
> transition asserted against a synthesised trace — and **one Definition-of-done box it could not
> close: `DisplaySnapshot::reseed` reaching `InFlight` ON THE BOARD.** There is no bench stage left
> in M5 to carry it, and it must not become a close-out item, because what it needs is not a sitting
> but *hours*: **§4's check 7 derives that the coverage trigger fires only in the tail of the
> live-stretch distribution.** So it comes here. **D-A4 does not block this stage; this stage now
> closes D-A4.**

1. **Confirm the four gaps are closed before flashing for the run.** `-- age` shows a non-zero
   median and a threshold near **39,927.94 ms**; a ping-interval maximum prints; a largest-block
   reading is taken at reconnect; `tools/soak_report.py` parses a current short capture with
   non-zero counts on every regex it owns. **A soak begun against an unproven reader is a day
   spent producing a fiction**, and this list is the whole of the pre-flight.
2. **The run: > 24 hours**, one board, `log2file` as always, flashed from a known commit with the
   SHA recorded in the log by hand (the shipping image carries no build tag - see §6).
3. **The seven checks read and recorded**, each with its number.
4. **`hardware/bench-2026-08-<dd>-m5-soak.md`** plus the capture gzipped beside it, following
   `bench-2026-08-30-D-B-silent-stream-*.log.gz`.
5. **D-A4's board box, read off `-- reseed :` — check 7.** Inherited 2026-09-06 and the only
   deliverable here this stage did not write for itself. **It is a READING, not a pass/fail**, and
   check 7 is explicit about which outcomes are verdicts on the mechanism and which are verdicts on
   the run's length. A `triggers=0` is a result and must be written down as one.
   **TAKE THE LAST `-- reseed :` OF EVERY BOOT, NOT THE LAST OF THE RUN.** `BinanceAdapter::Stats`
   is a plain member (`binance_adapter.hpp:1723`) and **nothing anywhere resets it**, so every
   figure on that line is per-boot and dies at reboot — and the first D-C run took **seven boots**.
   "The line at the end of the run" would silently discard six of them, including any boot in which
   the trigger fired. **`tools/soak_report.py` does not own this line** (`grep -i reseed` returns
   nothing), so deliverable 1's pre-flight — *"non-zero counts on every regex it owns"* — passes
   without covering it. Until the tool is taught it, this one is read by hand, per boot; teaching it
   is owed and is named in check 7.

## 4 · The checks, in order of what they can invalidate

**1 · The largest free internal block, at every reconnect.** The most load-bearing check here: it is
the only one that can invalidate a shipped constant **and** it is the designated evidence for a §6
invariant #7 permission that was granted conditionally on it (§9, 2026-08-28 — *"a ruling that said
'permitted' without that reading would be the unfalsifiable kind"*).

> **If it ever falls below 16,717 B, the reserve cut is wrong at the second socket.**

mbedTLS is pinned internal and needs **two contiguous 16,717 B blocks per session**. **Three numbers
around this check are stale and must be re-established before the margin means anything.** D-A1
measured steady state at `free 32,244 / largest 17,396`, a **679 B** margin — but that was the
D-A1 build, with an 80 KiB reserve and FramePipe in `.bss`. D-A2 raised the reserve **and** moved
FramePipe's slabs to PSRAM, and the board now reports `largest=51188`. And **the stated remedy is
backwards**: `panel.hpp:265` is `104u * 1024u` today, so *"`kReserveInternalBytes` goes back to
96 KiB"* is a **reduction**. The threshold itself — 16,717 B — is unaffected and stands.

**2 · The multiplier falsifier.** `k = 2.0`, derived from this signal's measured worst-healthy
multiple of **1.005×**, over **ten intervals spanning 111 ms** in B2's 221 s calibration capture.
It clears the signal's jitter by **1.99×** and a **dropped ping by exactly 1.000×** — one missed
ping equals the threshold, which is why this is worth a day.

> **Falsifier: any interval reaching 2 × median on a healthy socket raises k.**

**k rises alone**: the 60,000 ms ceiling already admits **k ≤ 3.005** (3.0 → 59,891.91 ms), so the
remedy is one value in one row, no ceiling change, no second attribution problem. Target threshold
**39,927.94 ms** — *which the board will not be running until §2(b) is fixed.*

**3 · The frame-pipe reading, in the order D-A2 finally settled.** **`max_held` against
`kFrameSlots` first** — the leading indicator, and it fires before anything is lost — then
`held(>100ms)` for how the pressure built, then `no_slot` for what it cost. `slow(>25ms)` and
`max_run` attribute pressure to the parse rather than reassembly and are **last**. *"Occupancy is
the mechanism; frame time is one contributor to it."* Three short runs show frame counts do not
predict drops, and `max_run` at 3 of 4 is explicitly **not** a safe margin. Sustained `max_held=4`
closes a constraint the project has no lever for — fewer slots is contraindicated by strain 27,
more is contraindicated by memory. **Do not quote `ROADMAP.md:28`'s reading order; the tree has
withdrawn it twice.** The named residue is PSRAM slab scanning: four frames in 1,808 over 50 ms.

**4 · `oversize` stays 0 at `kFrameCapacity` 64 KiB.** At the old 16,384 B slot, **13 of 3,119**
messages (0.417%) overflowed — one every ~23 s. 64 KiB is **2.29×** the largest observed (28,639 B).
D-A1's 70 s window saw `oversize=0` where ~2.4 were expected, so it neither confirms nor refutes.
A day's population does. **The sizing question is closed**; this is confirmation, not re-opening.

> **RULED AT THE M5 CLOSE-OUT (2026-09-06): a day's population put the margin at 1.060×, and the
> 2.29× is superseded rather than wrong.** Largest accepted **61,823 B of 65,536** over 1,188,879
> messages, one declined. Same quantity — capacity over the largest observed message — with
> "observed" meaning 3,119 corpus messages in one case and a day on the wire in the other.
> **The sizing stays closed and is vindicated**: 0.417% overflow → 0.000084%. What the reading
> changes is what may be claimed for the constant. `frame_pipe.hpp` carries the ruling beside
> the number.

**5 · The uncalibrated-default window.** `kUncalibratedThresholdMs` **is** the ceiling, so the
pre-calibration threshold is **60 s** under the Binance policy, for the **159.7 s** `kMinSamples = 8`
takes at a 20 s cadence — **on every connection, and the board reconnects**. Record how many
connections the run makes, how long each spends uncalibrated, and whether anything visible happened
inside those windows. An observation, not a pass/fail — it decides whether a fourth number gets
invented at the close-out.

**6 · Parity's reduced claim, TESTED rather than asserted.**

> The panel greys within the calibrated liveness threshold of the **socket** falling silent —
> 39.9 s — and refuses to colour a ladder the feed has never confirmed. It does **not** detect a
> subscription that stops server-side while the socket stays up. `age_ms` is a lag estimate for a
> socket backlog only, and reads nothing for the first ~11 minutes of every connection.

B2's per-venue limits need no re-measuring: **tracks a socket backlog 1.00×, BLIND to a feed
backlog, 638.8 s with no reading, 85.2 min supremum window.** Note **159.7 s and 638.8 s are
different quantities over different windows** — `kMinSamples = 8` calibrates the threshold,
`kBaselineSamples = 32` latches the age baseline — and both are correct. The silent-stream image is
**not** the test for the middle sentence: it never goes live, so it exercises the pre-seed path
rather than a subscription dying under a live book.

**7 · D-A4's re-seed mechanism, and the four-case reading that stops `adopted=0` meaning nothing.**
**Inherited from D-A4 on 2026-09-06** — its mechanism is proven on the host and has never run on a
board. This is the box it could not close, and the reason it is a SOAK check rather than a sitting
is derived rather than assumed.

> **The only route to a re-seed on a LIVE book is the coverage trigger.** Every other writer of
> `reseed_wanted_` either needs a hold already open or has just left the book `Unseeded`. So the
> board must arm the trigger (a `limit=1000` seed returning ≥ 448 levels on **both** sides), then
> consume **~552 levels of seeded coverage**, with **no `drop_book` anywhere in between**.

**That last condition is the binding one, and stage E is what made it reachable at all.** Consuming
552 levels takes ~50 s at B2's worst observed burst and ~375 s at BTCUSDT's mean walk. Mean live
stretch on the D-C capture was **6.6 s** — unreachable, and `adopted=0` on that board would have
confirmed nothing. After stage E's publish boundary it is **67.2 s**: median 40 s, p90 180 s, and
**52 stretches over 375 s in 32.25 h**, max 1,592 s. **It is a tail phenomenon — a short run sees
nothing and a long one has dozens of chances**, which is precisely why it belongs to this stage.

**THE LINE, AND IT IS READ RIGHT TO LEFT.** `cover=` and `below=` first, `triggers=` next, and
`adopted=` last — because the first two decide whether `adopted` is a verdict at all:

```
-- reseed : adopted=6 unbracketed=0 hold-overflow=0 | declined(no-hold)=0 adoptable=0 | triggers=6 below=0 cover=441/468 of 448
```

`cover=B/A` is the **low-water** seeded coverage per side **of that boot**, against the 448 the
trigger fires below.

> **READ IT PER BOOT, AND KNOW WHY.** `BinanceAdapter::Stats` is a plain member and nothing resets
> it, so every field here is cumulative within a boot and **gone at the next one**. The first D-C
> run took seven boots; a reading taken only at the end of the run would have shown one of them.
> **And `tools/soak_report.py` does not own this line** — so it survives deliverable 1's pre-flight
> (*"every regex it owns"*) without being covered by it, which is the wave-through shape §9 keeps
> recording. **Read it by hand, once per boot, until the tool is taught it.** Teaching it is owed
> and is the cheapest thing on this list.

The outcomes, with what each line actually looks like:

| what happened | the line | verdict? |
| --- | --- | --- |
| **the seed arrived under its own margin** — never armed, so re-fetching at that depth changes nothing | `… triggers=0 below=3 cover=100/100 of 448` | **no** — a configuration reading, not the mechanism |
| **armed, never approached** — the book did not live long enough, or the market did not walk | `… triggers=0 below=0 cover=947/961 of 448` | **NO. This is a verdict on the RUN, not the mechanism** |
| **came close and did not fire** — a longer run would do it | `… triggers=0 below=0 cover=455/471 of 448` | **no**, and it is the one that says to run again |
| **fired and adopted** | `… adopted=6 … triggers=6 below=0 cover=441/468 of 448` | **YES — the stage's claim, confirmed** |
| **fired and could not bracket** | `… adopted=0 unbracketed=4 … triggers=4 …` | **YES — and a defect.** Report it |

`cover=-/- ` means no seed ever latched bounds at all (a side came back empty) and is a fault in the
fetch, not a reading. `hold-overflow=` is documented as *"sized not to happen"* and any non-zero is
worth a line of its own. `declined(no-hold)=` counts bodies that arrived with no fetch outstanding —
on a board that should be 0, and a non-zero one means the transport issued without telling the
adapter.

**AND THE CLAIM TO READ IT AGAINST IS `adopted` CLIMBING WHILE `greys` STAYS FLAT.** That is the
whole of D-A4: a re-seed that lands on a live book rather than after a `drop_book`. **On a board in
the D-C capture's state it will read 0 either way**, because every re-seed there followed a
`drop_book` — `resync_req=643`, `grey_n=642`, `seqbreak=641`, `no_slot=4708`. That is the pipe's
four slots, **DESIGN card 28's other half**, and it is not a defect in D-A4's mechanism. If check 3
shows `no_slot` still climbing, check 7's `triggers=0` is explained by check 3 and says nothing
about the re-seed.

**Also worth recording while the line is in front of you:** `worst_parse_fetch_us` against
`worst_parse_quiet_us` on the `-- frame` line. D-A4's frame-path argument is that the hold's PSRAM
writes are negligible beside everything else on that path, and the falsifier is those two
converging. D-C's capture had them at **59,280 µs during a fetch against 1,499,017 µs quiet** — the
fetch window was the *fast* one by 25×. If that inverts, D-A4's §9 row is wrong and says so.

## 5 · Two readings that will look like the check and are not

**The fetch is not the reconnect.** A seed fetch takes the largest block to a few KB, so *"below
16,717 B"* is the **normal state during a fetch** and means nothing there. Worse,
`rest_fetch.cpp:396` raises a WARN on every reading below 16,717 B — **by design, on every fetch** —
so a 24 h log will be full of a warning that is not this check. The case being guarded is the
**half-open socket whose context is still held while the new one is built**; B3 saw two in 25.39 h.
An ordinary reconnect frees the old context first and allocates from a much larger hole (2026-08-11
saw free jump +54,720), so the reading should look calm most of the time. **`ARCHITECTURE.md:335`
says "every reconnect AND every fetch"; D-A2 supersedes it. Say which one the tripwire is, or the
check waves through.**

**An end-of-run heap number says nothing.** B3 measured the largest block moving as a **sawtooth**
over 25.39 h. The event is a mid-run dip.

## 6 · Known unknowns — resolve and record

- **How the ping interval should be exposed** — a maximum alone answers the falsifier; a histogram
  answers the next question without a second run. Cost it against the 128-byte `-- ping` buffer.
- **Whether the shipping image should carry a build identifier.** §9's 2026-08-29 rule requires a
  marker only where behaviour is *deliberately not* the shipping behaviour, so the working image
  correctly carries none — but a 24-hour capture that cannot name its own commit is the attribution
  problem of 2026-08-30 in a longer form. Arguably the close-out's.
- **Numbers that disagree with themselves.** Four, none to be silently picked:
  **(i)** the fetch round trip is **3,143–4,485 ms** in D-A2's log and **3.1–5.6 s** in D-B's brief;
  no source in the tree produces 5.6 s. **(ii)** largest-block-during-fetch is **3,060–4,596 B** in
  NOTES and **10,740 B** in §9, `seed_schedule.hpp` and `seed_task.hpp` — and 10,740 is within 1% of
  the *free-internal* range, so the likeliest account is a free-total figure quoted under a
  largest-block label. **This is exactly the quantity check 1 reads.** **(iii)** the Binance grey
  threshold has three values in the tree: **~80 s** (M5's DoD paragraph, superseded), **39,927.94 ms**
  (stage C's policy) and **30,000 ms** (what the board prints). **(iv)** strain 29's median
  convention differs by up to 48.8 ms on Binance captures while the row calls it "~5 ms at worst",
  and its tripwire fires on check 2 — which is a Binance cadence measurement.
- **Unassigned, flag rather than claim:** the ~302.9 s rx-silence recycle ("D-A3 or D-C", unchosen);
  what the header shows during the ~11 min no-reading window ("D's"); adding stage C's
  what-a-green-clock-means paragraph to `on_liveness` ("D's to add"); and the pull-the-Wi-Fi half of
  the honesty acceptance, assigned to "D · the bench" and to no sub-stage.
- **A precondition that is not D-C's work.** §2's DoD amendment — narrowing the greying clause to
  the **socket** and moving the mid-session server-side drop into the honesty clause — *"should land
  before D-C so the soak knows what it is accepting."* D-C's acceptance criteria are undefined until
  it does. The target is the DoD **paragraph** at `M5-the-shape-and-the-two-decisions.md:292-299`,
  **not** the stage-scope bullet — an owner acting on the first draft would have edited the wrong
  line.
- **An owner's ruling, probably not a D-C line.** §9's 2026-08-25 row is headed *the venue's
  `@depth20` audit stream ships on the board at the 1000 ms tick*; `binance_endpoint.hpp:81`
  subscribes to one stream and `venue_build.hpp:275` says the oracle "is a HOST instrument …
  deliberately not on the board". It decides which headroom column is measured.

## 7 · Constraints

- **Do not tune anything mid-run.** A soak whose subject changed is two short soaks. Findings go in
  the log; changes go to the close-out.
- **`upload_speed=921600` fails mid-write on this desk** (`hardware/BRINGUP.md`); use 115200. COM7
  is the board — kill orphaned monitor children and test-open the port before flashing, or an
  orphan will fake a brownout loop.
- **`monitor_filters` already tees** for every arm; run `pio` from `firmware/` so the log lands in
  `firmware/logs/`.
- **Five SOAK fields are structurally inert on today's Binance image** and the bench should not be
  told to watch them: `age=-`, `worst_age=0.0s`, `baseline=0ms` and `wd=0` are all downstream of the
  unwired liveness signal, and `crc_rows=0 (0.0%)` is inert because `kValidatedDepth = 0`. Several
  become live the moment D-A3 lands — re-read this line then.
- **Deliverables 1–3 touch `firmware/` and `tools/` only**, and 4 and 5 are records rather than
  code. If a reading argues for an engine change, it argues for it at the close-out.
- **Per `ARCHITECTURE.md` §9 (2026-08-30), a commit touching `firmware/` is not verified by ctest.**
  Deliverables 1 and 2 need `pio run -e depthcharge-binance` in the worktree.

## 8 · Definition of done

- ☐ **D-A3 landed and confirmed on the board**: the ping wire and the policy routing both
      (`-- age` shows a non-zero median and a threshold near 39,927.94 ms), a ping-interval maximum
      printing, a largest-block reading at reconnect, and `tools/soak_report.py` parsing a current
      capture with non-zero counts on every regex it owns.
- ☐ A single run **exceeding 24 hours**, captured and gzipped into `hardware/`.
- ☐ All **seven** checks read and recorded with their numbers.
- ☐ **D-A4's board box, inherited 2026-09-06**: `DisplaySnapshot::reseed` reaching `InFlight` on the
      board, and check 7's line recorded **whatever it says** — including a `triggers=0`, which is a
      result about the run's length and must be written down as one rather than left out.
      **Once per BOOT**: the counters are per-boot and nothing resets them, so an end-of-run reading
      discards every boot but the last.
- ☐ `kReserveInternalBytes` confirmed or moved on the evidence, with the **current** steady-state
      largest block re-established first and the stale remedy sentence corrected.
- ☐ `k` confirmed at 2.0 or raised alone, with the interval that raised it quoted.
- ☐ Any decision with architectural weight to `ARCHITECTURE.md` §9; `docs/DESIGN.html` where a card
      moves — cards 27 and 29 both have a plausible D-C half.
- ☐ ctest green; session log · ROADMAP; split proposed; nothing committed until approved.

## 9 · Out of scope

The **liveness ping wire** and **routing the per-venue policy** — both **D-A3**, and this stage was
blocked on them. The re-seed **mechanism** and its memory were listed here as D-A3's and were
**D-A4's**; that stage **landed 2026-09-06** and this stage now inherits its board box, so the
mechanism is no longer out of scope — **reading it is check 7**. Building or changing it still is. Every rendering decision — **D-B**, and **the owner's fifth and sixth, taken at D-A4's split on
2026-09-06** (the header trims trailing zeros; during a fetch the marker outranks the age, so the
standing priority becomes **VALUE > MARKER > AGE > SYMBOL** while one is in flight). **Six, not
four**, and all closed; the sixth is why check 7 has anything to look at on the panel at all. `worst_frame` — closed 2026-08-29 as the wrong instrument; do **not** carry D-A2's
"wants rooting out before the soak" into this stage, only the PSRAM slab residue survives.
`kFrameCapacity` **sizing** — closed by D-A2; this stage confirms the margin, it does not re-open it.
The median convention (card 29), strain 29's tripwire wording, the `CLAUDE.md` prose-versus-ordinal
line, §3b's defects and the 64,046 B correction at source — the **M5 close-out**. M4's residues D1,
D2 and D7, and card 30 — carried; a source reading "soak" in ROADMAP backlog D2 does **not** mean
this one.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->

### 2026-09-03 · Opus 5 · the first run, read — and it does not meet §1

**Done.** The 2026-08-30/09-01 Binance capture is preserved and read.
`hardware/bench-2026-08-30-D-C-soak.log.gz` (9,638,192 B) inflates to the 71,901,480 B source
`firmware/logs/device-monitor-260830-223245.log`, sha256 `d4c4fd12…`, verified byte-identical
rather than assumed. The reading is `hardware/bench-2026-08-30-D-C-soak.md`, one document covering
deliverable 4 and the six checks of §4. `tools/soak_report.py` was run over the capture first, as
D-A3 left it: it parses, and its census reports `SOAK` 12,425, `-- pipe` 12,424, `-- signal` 12,417,
`-- age` 12,417 — no grammar matched nothing, so deliverable 1's reader half is confirmed
retroactively, and so are gaps (a), (b) and (c) from the board's own output.

**§1 IS NOT MET, and this run closes no duration-dependent box.** 34.56 h of wall clock, **seven
boots** — 1 POWERON and 6 `RTC_SW_CPU_RST` — longest continuous stretch **9.84 h**. The six
restarts are the task watchdog on a starved `IDLE (CPU 0)` with `dc_feed` on core 0, then `abort()`
at the identical PC `0x4201c9f8`; zero `Brownout` lines in 490,498. **The scheduled-disconnect
question is therefore untouched**: no connection lived long enough to meet the venue's 24 h close.
The crash is being chased in `firmware/` by another session and is reported, not diagnosed, here.

**The six checks read anyway**, because they are per-connection and per-message quantities and only
the duration claim dies with the reboots. Scoreboard in the record's §11; the four that matter:

- **Check 1 PASSES with margin.** Eight reconnect readings, minimum `largest internal before=53236`
  (the one genuine mid-session reconnect), `after=51188` on all eight — **3.18× and 3.06×** the
  16,717 B threshold, never near it. **Steady state is re-established at 51,188 B**, superseding
  D-A1's `17,396 / 679 B margin`, which was a different build. `kReserveInternalBytes` is
  **confirmed at 104 KiB, not moved**; the brief's stale-remedy warning stands and the threshold is
  untouched.
- **Check 2: k = 2.0 stands by the letter of the falsifier, on a 2.4% margin rather than a 99%
  one.** `>=2x med` is 0 on all 12,417 samples, so the rule as written does not raise k. But the
  basis has changed: **6,183 healthy intervals** against stage C's ten, **39 of them in the 25–40 s
  band**, and the **worst healthy interval is 39,061 ms = 1.9529 × median, 943 ms short of that
  boot's 40,004 ms threshold**. Stage C's "clears the jitter by 1.99×" is refuted; the measured
  clearance is **1.024×**. Both facts are recorded because printing only the first would be the
  wave-through. **Whether the falsifier as worded is still the right rule is close-out work** — this
  run is not entitled to rewrite the rule it was run to test.
- **Check 3: `max_held = 4 of 4` sustained in all seven boots**, over 1,188,879 published frames,
  in B4's 1.48 h as surely as B6's 9.84 h. `held(>100ms)` 3.49%, `no_slot` 4.44%, `slow(>25ms)`
  3.71%, `max_run` **19 of 4**, `qfull` 0. This supplies the *sustained* the brief said would close
  a constraint with no lever — and per §7 and §9 it does **not** license an engine change from here.
- **Check 4 is NOT clean.** `oversize` reached **1** (B1, one event in 1,188,879 — against 13 of
  3,119 at the old slot, so the sizing decision is vindicated), but the largest accepted message is
  now **61,823 B of 65,536**: a **1.060×** margin where §4.4 states 2.29× over 28,639 B. §9 keeps
  sizing closed, so this is recorded for the close-out with the two numbers it needs.
- **Check 6(a) HOLDS on an unprovoked event, and it is the run's best argument for D-A3.** In B1 the
  feed went silent at `up=6657s`; the clock crossed its 39,978 ms threshold and fired at `up=6667s`
  (`worst_age=43.0s`, one 10 s sample of latency). **The transport believed the socket up for
  another 1,571 s (26.2 min)**, until the peer *cleanly closed* it — the run's only socket-end
  autopsy, `[clean-close]` after 8,228 s and 133.5 MB. Without the liveness clock the board would
  have shown a stale coloured ladder for 26 minutes. **The 2.3 h clean close is NOT the venue's 24 h
  policy close and must not be read as one.** (b) holds: `live=1` on 28.5% of samples. (c) is
  untested — no server-side subscription death is identifiable. (d) is confirmed at 651–672 s.

**Decisions, with why.**

1. **One document, not two.** The gzip's write-up and the six-check reading share a provenance
   section and every figure; splitting them would put the sha256 in one file and the numbers
   derived from it in another. Precedent `bench-2026-08-22-kraken-b3-soak.md` is also one file.
2. **Named `bench-2026-08-30-D-C-soak.md`, not `bench-2026-08-<dd>-m5-soak.md` as §3.4 says.** The
   capture was already gzipped as `bench-2026-08-30-D-C-soak.log.gz`, and "beside it" is worth more
   than the brief's placeholder name. Deliverable 4 is met on substance.
3. **Per-boot figures were produced outside `tools/soak_report.py`.** The tool reads the file as one
   series and this file is seven: its `THE RUN` header reports `board uptime 10s -> 16014s (4.45 h)`
   — the last boot alone — and its grey-percentage and watchdog-versus-socket sections mix boots. It
   prints all six `REBOOTS` transitions, so nothing is concealed, but a reader taking the header at
   face value understates the run by 30 h. **Per-boot segmentation is a tool change for the
   close-out**, and it is named in the record's §12 so it is not lost.
4. **The image cannot name its own commit, and that is recorded as a defect of the run rather than
   guessed past.** Deliverable 2 asked for the SHA by hand; `markers : 0`. The only identity in
   71.9 MB is `main.cpp:114`'s banner, which still reads *"DepthCharge M4 stage D"*. `61640bf` (in
   `daa8013`, 18:04) is the only candidate on behaviour and timing — the log carries all four D-A3
   instruments — **but that is inference, not evidence**, and it is written that way.
5. **Check 5's premise is wrong and the record says so.** The uncalibrated window is **per boot,
   not per connection**: B1's mid-session reconnect at `up=8238s` did not re-enter UNCALIBRATED.
   Measured 160.3–180.4 s per boot against the predicted 159.7 s, 0.96% of uptime, nothing visible
   inside any of the seven. Whether the clock *should* re-derive its median per connection — a
   threshold carried across a reconnect is derived from a socket that no longer exists — is left as
   the close-out's candidate fourth number, not decided here.
6. **The re-seed storm is stated but not diagnosed.** `live=1` on 28.5% of samples; grey 71.4% of
   uptime; 5,336 `seq-gap` STALEs; 5,562 seed fetches, one per 22.4 s; 85.5% of holes classified
   LINK-BOUND by the board itself. It is not one of the six checks, but checks 3, 4 and 6 were all
   measured on a board in this state and a quieter run may read differently — so it is named where
   a reader will see it before the numbers.
7. **Both §5 traps were live and neither is reported as a finding.** The final SOAK's
   `largest=7668` is the periodic sampler inside a fetch (the adjacent `rest: fetch` reads
   `largest 51188/4852/51188`; during-fetch median over the run is 4,084 B), and the 5,547
   `is BELOW the 16717 B` WARN lines are the per-fetch warning §5 predicts, one per fetch. The
   tripwire is the reconnect reading and only that, per D-A2 over `ARCHITECTURE.md:335`.
8. **"Half-open" means two different things and the record separates them explicitly**, because
   §3 and §8 of it would otherwise read as a contradiction. The **memory** case (an old TLS context
   still held while the new one is built — what check 1 guards) did **not** occur: the autopsy at
   `00:50:04.463` precedes the new socket by 2.7 s and the free total jumps **+47,484 B** at the
   close, so `before=53236` is a post-release reading. The **liveness** case (a socket the transport
   believes up while nothing arrives) **did** occur, for 26 minutes. B3 saw the memory case twice in
   25.39 h; this run saw it none in 34.55 h, so **check 1 passes without having been stressed the
   way M4 stressed it** — worth knowing before anyone treats 3.18× as a proven worst case.
9. **A gap in `soak_report.py`'s own guard is recorded, not fixed.** `owned()` enrols five
   grammars; `RE_STALE`, `RE_LIVE`, `RE_GREYFOR` and `RE_SOCK_UP` are read and printed but not
   enrolled, so a drift in one of those four still produces a silently missing section — the exact
   failure the census exists to remove. All four matched on this capture (5,343 / 5,343 / 5,343 /
   8), so nothing in this reading is affected. **Not fixed here** because `tools/` is D-A3's and the
   close-out's, and a tool change made while reading its output is a change nobody reviewed.

**Nothing committed.** Split proposed to the owner, not executed. No `ARCHITECTURE.md` §9 row is
proposed: every finding above is either a reading the brief already anticipated or a close-out
question, and §9 is not where open questions go. `docs/DESIGN.html` untouched — cards 27 and 29
have a plausible D-C half, but that box is gated on the milestone completing and this run does not
complete it.

**Exact next step.** **The crash lands before any re-run.** Six task-watchdog aborts at a fixed PC
make a 24 h stretch impossible, and soak hours spent before that fix cannot close the box. When the
board is stable: flash from a known commit, **write the SHA into the log by hand as a `###` marker
before starting** (the one thing this run cannot supply about itself), and repeat the run. Every
non-duration check above will then be re-read on a single-boot capture, and checks 2 and 4 are the
two whose margins moved.
