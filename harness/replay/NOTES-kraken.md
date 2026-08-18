# Kraken replay traces — observations (M4 stage 0)

Ground-truth notes taken while capturing the first Kraken traces from the **live**
public endpoint (`wss://ws.kraken.com/v2`, server version `2.0.10`, `api_version
v2`) on **2026-08-16, 23:37–23:55 UTC** — which is 00:37–00:55 BST on the 17th at
the desk, so the wall clock and the record disagree by a date. Everything here is
dated by UTC, per
[`NOTES.md` § Trace naming and dating](NOTES.md#trace-naming-and-dating--binding-for-every-committed-trace-all-venues),
which these captures are the reason for: they are the first taken across midnight
UTC, and the convention they made explicit was already true of every dated trace
in the directory. This is M0's `NOTES.md` repeated for the second venue: what the
deployed server actually does, versus what the M4 stage-0 brief's doc summary said
it would.

**Nothing here is an adapter.** No `engine/` code, no C++ checksum, no
`FeedEvent`. The brief's job was to price the wire and settle the unknowns
*before* M4 chooses a depth, and everything below is measurement.

**The decisions those measurements fed have since been taken** by the owner the
same evening (recorded in `docs/briefs/M4-stage-0-price-the-kraken-wire.md` and as
`ARCHITECTURE.md` §9 rows dated 2026-08-16 stage 0): **depth 25, with `kDisplayLevels` staying 27** — the
two unfilled rows a side are §4's *"depth beyond N is unknown, not zero"*
rendered, and a panel that padded 25 levels into 27 rows would be inventing book,
so a ladder that differs in height by venue is correct rather than a wart — and
**strain 3 resolves as `static_assert`s, not a `concept`**, because host and
target would compile one spelling under two dialects, which type-checks and is
therefore worse than convention.

**Observation window.** The measurements are drawn from four **90 s** full
captures kept locally under `_local/` (git-ignored), plus a fifth reconnect
capture. The **committed** slices are the first 60 s of each (see
[Committed traces](#committed-traces)); the 60 s window was chosen by
measurement, not habit — it is the shortest window that still contains the worst
book-event gap of all four captures, which is the single most important thing
these traces record.

**The three BTC/USD depths were captured concurrently, on three sockets, over
the identical wall-clock window.** ARCHITECTURE §9 records twice that *a
throughput comparison across time on a WAN path is not an A/B unless the hour is
controlled* — the 2026-08-14 evening convicted the RX path on an uncontrolled
hour and the verdict had to be withdrawn. Depth 10 vs 25 vs 100 is exactly that
comparison, and book traffic varies minute to minute, so the hour is controlled
by construction here rather than by hope.

---

## Headline 1: `kRxWatchdogMs = 1000` would grey this panel constantly, and the heartbeat hides it

> **PARTLY SUPERSEDED 2026-08-17.** The core finding stands and is stronger than
> ever. What is withdrawn is the quiet pair's **maximum**: 9,007 ms is a sample
> maximum from one 90-second window, not a ceiling. A second window at a distant
> hour measured **25,843 ms** on the same pair at the same depth, with the socket
> provably healthy throughout. See
> [the review addendum](#m4-stage-a-review-addendum-2026-08-17--the-quiet-pairs-9-s-is-a-tail-not-a-ceiling-and-15000-ms-does-not-clear-it)
> before quoting the MINA/GBP row below.

This is the finding with the most reach, and it costs nothing to have now.

Anvil broadcasts on a timer; Kraken publishes on change. Measured book-event
silence, over 90 s per capture:

| capture | book msgs | rate/s | p50 | p90 | p99 | p99.9 | **max** |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| BTC/USD depth 10 | 1,412 | 15.62 | 1.4 | 102.8 | 1154.0 | 3808.9 | **5057.6** |
| BTC/USD depth 25 | 2,786 | 30.93 | 0.1 | 58.3 | 602.1 | 1767.4 | **4534.6** |
| BTC/USD depth 100 | 4,435 | 49.26 | 0.1 | 47.7 | 309.5 | 991.3 | **1370.7** |
| MINA/GBP depth 25 | 44 | 0.47 | 56.8 | 8480.0 | 9006.8 | 9006.8 | **9006.8** |

All figures in ms. Anvil's worst healthy gap over 20 minutes was **391 ms**, and
1000 ms was chosen as 2.6× above it (`NOTES.md`, M3 addendum).

**Even BTC/USD — one of the most liquid pairs in the world — goes 4.5 seconds
without a book event inside a 90-second window.** The quiet pair goes **9.0 s**,
and its p90 is 8.5 s, so that is its normal condition rather than an outlier. A
watchdog at 1000 ms armed on book events would open stale episodes on every one
of these captures. Invariant #5 would be greying a panel whose book is perfectly
fresh — the crying-wolf inversion ARCHITECTURE §7 already predicts for a venue
without an unconditional republish.

**And the trap is that the transport looks healthy the whole time**, because:

## M4 stage A review addendum (2026-08-17) — the quiet pair's 9 s is a TAIL, not a ceiling, and 15,000 ms does not clear it

**Read this before quoting any figure in Headline 1 for the quiet pair.** The
table below Headline 1 stands as a record of what the 23:37Z window contained;
what is withdrawn is the reading placed on it — that 9,007 ms was the quiet
pair's ceiling and that a 15,000 ms threshold clears it with 1.67× margin.
**It does not.** Measured at a second hour, the worst healthy book silence on
the same pair at the same depth is **25,843 ms**, which is **1.72× the declared
threshold**, not 0.58× of it.

The stage-A review asked for exactly this check, on exactly this suspicion: *the
tightness of the distribution argues it is a ceiling rather than a tail, but it
has been observed once, at one hour, on the pair whose quiet is most likely to be
time-of-day dependent.*

**The capture.** `MINA/GBP`, depth 25 — the same pair and depth as the committed
slice — for **600 s from 2026-08-17T16:00:46Z**, which is **7 h 37 min** away in
hour-of-day from stage 0's 2026-08-16T23:37:03Z. One connect, 1,165 frames,
`perf_counter_ns`, kept untracked in `_local/` per the M0 policy.

| | stage 0 · 23:37Z · 90 s | confirmation · 16:00Z · 600 s |
| --- | ---: | ---: |
| book events | 44 (29.1 /min) | 563 (**56.3 /min**) |
| p50 | 56.8 ms | 15.9 ms |
| p90 | 8,480.0 ms | 4,068.1 ms |
| p99 | 9,006.8 ms | 8,738.5 ms |
| p99.9 | 9,006.8 ms | **25,843.3 ms** |
| **worst book gap** | **9,006.8 ms** | **25,843.3 ms** |
| gaps > 9,007 ms | 0 | **4** — 14,228 / 16,645 / 18,302 / 25,843 |
| gaps > 15,000 ms (declared) | 0 | **3** |
| margin of 15,000 over the worst | 1.67× | **0.58×** |

**It is the market, not the socket, and that was checked rather than assumed.**
Kraken's heartbeat is unconditional and 1 Hz, so it is an independent liveness
oracle for exactly this question. Inside the 25,843 ms book gap there are
**26 heartbeats, at intervals of 936–1,042 ms**; inside the 18,302 ms gap, 19;
inside 16,645, 17; inside 14,228, 14. Every one matches a 1 Hz beat to within a
beat. Across the whole run: **600 heartbeats in 600.2 s**, worst *record* gap
**1,119 ms**, `connects=1`, no reconnect, no non-JSON frame, no multiline frame.
The connection was healthy for ten minutes and the book simply did not move.

**The consequence, stated as the panel would show it.** At the declared
15,000 ms, this capture produces **3 disconnects that did not happen and 15.8 s
of grey — 2.6% of the run** — on a feed that never lost a packet. That is the
crying-wolf inversion deliverable 4 was created to prevent, arriving through the
constant deliverable 4 chose.

**Why the first window looked like a ceiling, which is the part worth keeping.**
Stage 0's six long gaps were 8,113 / 8,480 / 8,488 / 8,582 / 8,672 / 9,007 ms —
a 900 ms spread, arriving about every 14 s. That regularity is what made 9,007
read as a bound. The confirmation reproduces that band exactly, and it also
contains something the shorter window could not: **all four gaps over 12 s fall
between t+70 s and t+140 s, and after t+167 s nothing exceeds 8,800 ms again.**
So the last 430 seconds of the confirmation look precisely like the whole of
stage 0's window. **Stage 0 sampled the calm regime for ninety seconds and read
its width as a limit.** Had it started seventy seconds later it would have seen
25.8 s and 15,000 would never have been proposed.

Note also that the busier hour is the one with the *longer* tail: 56.3 book
events/min against 29.1, and 2.9× the worst gap. **Rate does not predict
silence on a thin book** — it is bursty, not slow, and a burst has a gap on the
other side of it.

**General rule, and it is a new instance of one this project keeps paying for:**
*a distribution's tightness is evidence about the sample, not about the bound.*
Six gaps within 900 ms of each other is a strong-looking statistic and it
described one regime. §9 already holds "a throughput comparison across time on a
WAN path is not an A/B unless the hour is controlled"; this is its sibling for a
**bound** rather than a comparison — **a maximum measured in one window is a
sample maximum, and calling it a ceiling requires a second window at a different
hour.** Stage 0 controlled the hour *across the three BTC depths* precisely so
they would be comparable, and then took the quiet pair's absolute maximum from
that same single hour.

**Nothing was changed.** `venue_traits(Kraken).stale_gap_ms` is still 15,000 ms,
`kRxWatchdogMs` is untouched, and no golden or pin moved. Two artefacts now cite
a superseded premise and are deliberately left for the owner to move with the
number: `venue.hpp`'s `stale_gap_note` ("worst book silence 9,007 ms … 1.7x
margin") and the `> 9007.0 * 1.5` floor assertion in `test_trace_venue.cpp`.
Both still *pass*; both are now premised on a figure this capture beat.

**What this is, and is not.** It is not a tuning problem — picking 30,000 from
n=2 hours would repeat stage 0's error with a bigger number. It is a **stage D
bench criterion problem**, because strain 20's bar is *"the ladder holds its
colour through a nine-second legitimate silence, and greys when the feed
actually dies"* and the quiet pair's legitimate silence is now known to reach
26 s. Three things the owner may want to weigh, none of them taken here:

1. **A third window** settles whether 26 s is itself a sample maximum. Two hours
   is enough to falsify a ceiling and not enough to establish one.
2. **The quiet pair may be the wrong instrument for the bar.** MINA/GBP was
   chosen at stage 0 to be the extreme case; an extreme case with an unbounded
   tail cannot be the thing a threshold is sized against, only the thing it is
   tested against.
3. **The heartbeat is the oracle the threshold is not using.** This addendum
   diagnosed a 26-second silence as healthy in one step, from data already on
   the wire — a book-event threshold alone cannot make that distinction at any
   value, which is an argument about the *shape* of the rule rather than its
   number. Decision 2 split the clock in two; this is the evidence for whether
   two is enough.

### THE RULING (2026-08-17) — it is the third option, taken and extended

The owner ruled the same evening, and it is **option 3 above**: *the heartbeat is
the oracle the threshold is not using.* Options 1 and 2 were both declined, and
declining them is the interesting part — a third window and a different pair are
both ways of finding a better **number**, and the ruling is that **there is no
number.**

> **Book-event silence cannot be a staleness signal at any venue, and no
> threshold on it can be correct.** A quiet market and a silently dead
> subscription produce identical wire behaviour, so book silence carries no
> information about whether the displayed book is trustworthy. The bound on book
> silence is a market property, not a protocol one, and a market can be closed.

Full text as `ARCHITECTURE.md` §9, 2026-08-17 (M4 stage A, ruling). What it
changes here, in four parts:

1. **Staleness counts the venue's declared liveness signal** — Kraken's
   `heartbeat`, Anvil's `summary`. `venue_traits` carries that signal's **name**;
   the field that used to hold a duration is gone, and the rename is deliberate
   so a future reader cannot put a number back in it.
2. **The threshold is measured at runtime, not declared.** Kraken's 1 Hz is a
   protocol constant and Anvil's is `ANVIL_SUMMARY_HZ`, operator config — both
   are values DepthCharge cannot read back, so a hardcoded number is coupled to
   something that can change with no client change and no error. The threshold is
   `k x` the rolling median of the signal's own observed inter-arrival
   (`harness/include/dc_harness/liveness_clock.hpp`).
3. **Book silence becomes `age_ms`** — a number in the header, never a colour.
   Stage A2.
4. **`kRxWatchdogMs` is untouched.** The firmware half is stage B's.

**`k = 4`, and what it is made of.** Derived from the worst *healthy*
inter-arrival expressed as a multiple of that venue's own median:

| venue | signal | intervals | median | worst healthy | worst/median |
| --- | --- | ---: | ---: | ---: | ---: |
| Anvil | `summary` | 1,191 | 500.0 ms | 968.8 ms | **1.937x** |
| Kraken | `heartbeat` | 834 | 1000.3 ms | 1,119.0 ms | 1.119x |

Anvil is the binding case and its 1.937x is **one missed tick** in healthy M0
data — so any k ≤ 2 greys the panel whenever a single `summary` publish slips.
k = 4 means "three consecutive ticks missed" and clears the worst measured
healthy multiple by 2.07x, the same order of margin `kRxWatchdogMs` was derived
at (2.6x) but taken against a *multiple*, which is what lets one number serve
both venues. Measured effect: Anvil calibrates to 2,000 ms and Kraken to
4,000–4,003 ms, from the same constant and with no per-venue figure anywhere.

**What it does to this pair.** The quiet pair's heartbeat held 936–1,042 ms right
through the 25,843 ms book hole, so the liveness clock never fires and the
25,843 ms becomes age. All nine committed traces re-run: **the quiet pair
synthesises zero disconnects and `anvil_101_reconnect` still produces its one**,
now armed on `summary` (a 4,747.7 ms gap against a self-calibrated 2,000 ms).

**The accepted cost, stated because it is real.** A connection-level liveness
signal does not prove the *book subscription* is alive: Kraken's heartbeat is
per-connection and Anvil's `summary` is global fan-out to every socket whatever
it subscribed to. This trades an **observed** false-grey — three per ten minutes
on a healthy feed — for an **unobserved** false-colour. Stage B's CRC covers
updates that arrive and disagree; it does not cover silence.

### Two quiet-pair traces, two jobs — neither is redundant

Committed 2026-08-17. There are now **two** MINA/GBP depth-25 slices and they
cover the same phenomenon at different magnitudes. A future reader deleting
either as duplication would lose something specific.

| | `kraken_minagbp_d25_20260816` | `kraken_minagbp_d25_20260817` |
| --- | --- | --- |
| captured | 23:37Z | **16:00Z** — 7 h 37 min away in hour-of-day |
| window | first 60 s of a 90 s capture | **t+65..t+160 s of a 600 s capture** |
| records | 93 | 144 |
| worst book silence | **9,006.8 ms** | **25,843.3 ms** |
| liveness (heartbeat) | 60, median 1000.6, worst 1026.2 ms | 95, median 999.1, worst **1119.0** ms |
| greys, calibrated | **0** | **0** |
| greys at the WITHDRAWN 15,000 ms book rule | 0 | **3** |
| gzip | 2.4 KiB | **2.7 KiB** |

**The 08-16 slice is the typical case and the on-connect record.** It is the only
one that contains the subscribe, the ack and the `status` frame, so it is what the
record taxonomy and the untyped-record explanation rest on, and it is the one
whose 9,007 ms figure the stage-0 write-up is built from.

**The 08-17 slice is the EXTREME, and it is the golden the ruling exists to
serve.** Read its two columns together: at the withdrawn 15,000 ms constant the
book rule fires **three** times in 95 seconds on a feed that never lost a packet,
and at the self-calibrated 3,987 ms liveness threshold it fires **none**. That
pair is the ruling, in one file. It also happens to contain the run's worst
heartbeat interval (1,119.0 ms), which is one of the two numbers `k` was derived
from.

**Two deliberate properties of the 08-17 slice, so they are not read as defects.**

1. **It begins mid-stream** — 65 s into the capture — so it has `tx=0`,
   `snapshots=0` and no `status` frame. That is what it costs to contain the
   interesting stretch, and the window could not be moved: the fourth of its four
   >12 s gaps opens at t+139.8 s and **closes at t+158.1 s**, so a window ending
   at t+140 would have cut it and the file would have contained three of the four
   gaps it claims.
2. **It is a LIVENESS golden and NOT a checksum or truncation golden**, and
   `tools/kraken_frame_economics.py` refuses to pin it for exactly that reason:
   with no snapshot to baseline from, all 49 of its checksums are computed
   against a book built from deltas applied to nothing, so it scores **0/49 at
   any depth**. It is listed in that tool's `NOT_A_CHECKSUM_GOLDEN` table **with
   its reason**, rather than skipped — an unpinned trace is still a failure
   there, and "cannot be graded" had to become a third state instead of a hole.
   Its truncation column would also read CATCHES *for the wrong reason* (0 < 49
   is true only because everything fails), and `--verify` now says so.
   **The truncation goldens remain the d10 and d25 BTC/USD slices.**
### The defensive resubscribe — NAMED, NOT BUILT

The gap the cost above leaves is a subscription that dies while the connection
stays healthy: Kraken keeps heart-beating, the panel keeps its colour, and no
book update ever arrives again. Stage 0 already found the shape of it — a refused
`depth` leaves exactly that state — and the ruling's own point 6 concedes it.

The obvious defence is a **defensive resubscribe**: after N x the liveness
threshold with no book event, tear the subscription down and re-subscribe, on the
theory that a snapshot either arrives (the book was genuinely quiet, nothing lost
but bytes) or does not (the subscription really was dead).

**Deliberately not built, at the owner's instruction, and the cost of building it
is the reason:** the trigger is *book silence*, which is the exact quantity the
ruling just established carries no information. A resubscribe armed on it would
fire on MINA/GBP roughly every 26 seconds of a perfectly healthy market, throwing
away a good subscription and paying a fresh snapshot each time — the crying-wolf
inversion, moved from the panel to the wire where it costs more. Any version
worth having needs a second signal that distinguishes *quiet* from *dead*, and
this venue does not publish one. Recorded as an open option for stage B with
that condition attached, rather than as a to-do.

---

## Kraken sends a 1 Hz heartbeat, and it is not in the brief's doc summary at all

`{"channel":"heartbeat"}` — 23 bytes, exactly **90 in each 90 s capture** and 91
in the quiet one. Under the "any received frame" counting rule the same four
captures look completely different:

| capture | p50 | p90 | p99 | p99.9 | max |
| --- | ---: | ---: | ---: | ---: | ---: |
| BTC/USD depth 10 | 1.9 | 166.5 | 985.2 | 1002.7 | **1004.6** |
| BTC/USD depth 25 | 0.1 | 66.8 | 602.1 | 1000.6 | **1002.9** |
| BTC/USD depth 100 | 0.1 | 50.8 | 309.5 | 726.9 | **998.1** |
| MINA/GBP depth 25 | 973.7 | 1003.7 | 1022.6 | 1026.2 | **1026.2** |

Two consequences, and the second is the one to carry into M4.

1. **The heartbeat floors byte-silence at ~1 s and no higher.** Every capture's
   worst any-frame gap lands between 998 and 1026 ms — *straddling* the current
   1000 ms constant. A watchdog armed on byte arrival would sit exactly on the
   noise floor of the heartbeat period and trip intermittently on all four
   captures. There is no margin at all, in either direction.
2. **At Anvil the three counting rules agree on the worst gap; at Kraken they
   disagree by 9×.** `NOTES.md` records the agreement as a stable property
   surviving 3× sample growth (391 ms under all three rules). Here the same
   trace reads 1.0 s or 9.0 s depending only on which rule you arm, and the
   *book*-armed number is the honest one — invariant #5 says liveness is defined
   by events reaching the book, not by bytes arriving. Kraken is the venue that
   makes that sentence expensive rather than free.

**Not reopened here.** The brief forbids touching `kRxWatchdogMs`, and these
numbers are the input to that decision, not the decision. What they do settle is
that M4 cannot ship the Anvil constant unexamined, and that a per-venue
threshold — or a heartbeat-aware liveness rule — is now a design item with
evidence behind it rather than a worry.

---

## Headline 2: the client MUST truncate, and the checksum measures what happens if it doesn't

Kraken sends no removal for a level that merely falls out of the subscribed
depth; the client truncates its own book. That reads like housekeeping. The
CRC32 turns it into a number, because the checksum is a complete test of whether
the book you hold is the book the venue holds.

Maintaining each capture two ways — truncating to the subscribed depth, and
never truncating — and checking every message's checksum against the venue's:

| capture | checksums | **truncating (the rule)** | never truncating |
| --- | ---: | ---: | ---: |
| BTC/USD depth 10 | 1,412 | **1,412 / 1,412** | 379 / 1,412 (26.8%) |
| BTC/USD depth 25 | 2,786 | **2,786 / 2,786** | 910 / 2,786 (32.7%) |
| BTC/USD depth 100 | 4,435 | **4,435 / 4,435** | 4,435 / 4,435 (100%) |
| MINA/GBP depth 25 | 44 | **44 / 44** | 44 / 44 (100%) |

Read the depth-100 row carefully, because it is the dangerous one. **A
non-truncating client is 100% correct at depth 100 over 90 seconds and 27%
correct at depth 10.** At the depth a developer is most likely to test with, a
90-second test cannot find the defect at all — the untruncated book drifted to
165 bids / 148 asks and every top-10 checksum still matched, because the stale
levels were all far from the touch. This is a latent defect with a built-in
disguise, and the only reason it is on record tonight is that the same window was
run at three depths.

**Do not read that as "shallower detects better", which is the tempting and wrong
generalisation** — depth 25 catches on BTC/USD and misses on the quiet pair at the
same depth. What decides it is measured, not estimated, and it is written up under
[Which traces guard the truncation rule](#which-traces-guard-the-truncation-rule--and-it-is-not-the-shallow-ones)
below, together with two hypotheses that fitted these four captures and were
falsified by the fifth.

Levels held per side, untruncated, after 90 s: **90/50** at depth 25, **73/51**
at depth 10, **165/148** at depth 100, **27/25** at depth 25 on the quiet pair.

**Also proven by the same 8,677 clean checksums: the captures lost nothing.** A
delta stream that checksums end to end from its own opening snapshot has
provably no missing, duplicated or reordered message. That is a stronger
statement of trace integrity than any Anvil capture can make, and it is worth
having before these files become goldens.

---

## Headline 3: invariant #3 is not a stylistic rule at this venue — a float parser fails every single checksum

Kraken v2 puts prices and quantities on the wire as **bare JSON numbers**, and
the checksum is computed over their **decimal text as sent**. So:

| how the level text was obtained | checksums matching, depth 25 |
| --- | ---: |
| the verbatim capture text | **2,786 / 2,786** |
| after `json.loads` → `json.dumps` (a float round-trip) | **0 / 2,786** |

Zero. Not "degraded" — **zero**, in all four captures. The mechanism is visible
in a single level: the wire says `0.50930100`, a float round-trip says
`0.509301`, and the checksum tokenisation (strip the point, strip leading zeros)
turns those into `50930100` and `509301`. It gets worse at the small end: **92
messages in the depth-25 capture contain a quantity that a float round-trip
prints in exponent notation** (`0.00005100` → `5.1e-05`), which the checksum has
no spelling for at all.

Only **1,480 of 2,878** frames in that capture survive a JSON round-trip
byte-for-byte. **A trace stored through `json.loads` would be a recording of
Python's float formatting, not of Kraken's** — which is why
`tools/capture_kraken.py` splices frame text in verbatim, and why
`tools/kraken_frame_economics.py` takes every byte count from
`len(raw.encode())` and rebuilds derived variants from the original number
*tokens*.

The streaming parser at M4 must scale to integer ticks **from the token text**
and keep that text (or its exact digits) reachable for the checksum. Reconstructing
a decimal from a parsed number is not merely lossy here; it is a guaranteed
`Gap{ChecksumFail}` on every message.

---

## Frame kinds and shapes

Five kinds seen, and no others, across 9,932 received frames:

- **`status`** — one per connection, first frame, before the subscribe ack:
  ```json
  {"channel":"status","type":"update","data":[{"version":"2.0.10","system":"online","api_version":"v2","connection_id":15113583034416118705}]}
  ```

  `connection_id` exceeds 2⁵³, so it is **not** safely a double. Nothing here
  needs it; a parser that treats every JSON number as a float corrupts it
  silently, which is the same lesson as the level text one rung up.
- **subscribe ack** — carries no `channel` and no `type`:

  ```json
  {"method":"subscribe","result":{"channel":"book","depth":25,"snapshot":true,"symbol":"BTC/USD"},"success":true,"time_in":"…","time_out":"…"}
  ```

- **`book` / `snapshot`** and **`book` / `update`** — see below.
- **`heartbeat`** — `{"channel":"heartbeat"}`, 23 bytes, 1 Hz, no payload at all.

Book messages, both types:

```json
{"channel":"book","type":"update","data":[{"symbol":"BTC/USD","bids":[],"asks":[{"price":62807.0,"qty":0.00000000},{"price":62818.8,"qty":0.65540712}],"checksum":4249465476,"timestamp":"2026-08-16T23:37:03.745044Z"}]}
```

- **What a `snapshot` carries that an `update` does not: nothing.** The entry key
  set is identical — `symbol`, `bids`, `asks`, `checksum`, `timestamp` — on both,
  in that order, in every one of the 1,537 book messages in the committed depth-25
  slice. Only `type` and the size of the level lists differ. **The snapshot
  carries a checksum**, which settles the brief's open question (v1 did not).
- `data` is an **array**, and held exactly one entry in every book message
  captured. One subscribe *can* name several symbols (measured: six at once), so
  an adapter must iterate `data` rather than index `[0]`, even though nothing in
  these traces would catch it doing the latter.
- `bids`/`asks` are best-first (bids high→low, asks low→high) in the snapshot;
  **an update's level list is in no order at all** — a single message routinely
  carries an ask at 62807.0 and one at 62818.8 and then one at 62803.8.
- `qty: 0` removes a level. **24.0%** of all level entries in the committed
  depth-25 slice are removals (591 of 2,458).
- Level counts per message: mean **1.55** at depth 25 (p50 1, p90 3, p99 3,
  max 10). Almost the whole stream is one- to three-level touches.
- `timestamp` is an RFC 3339 string with microseconds — the venue's own send
  time, not an arrival time, and the only thing on this wire that could support a
  producer-side age measurement.

---

## There is no sequence number, and no gap signal

Confirmed by inspection of every frame kind: nothing on this wire carries a
sequence, an offset or a message count. ARCHITECTURE §4 already says so
("Kraken has no seq — its adapter synthesises one and converts a CRC failure
into `Gap{ChecksumFail}`") and the capture agrees. **The checksum is the gap
detector**, and it is a good one: it covers the top 10 levels on every message,
so a lost or reordered delta is caught within one message rather than never.

Its limit, stated because the adapter will lean on it: it says nothing about
levels 11..N. A delta lost at level 40 of a depth-100 subscription is invisible
to the checksum and will stay wrong until the next reconnect. That is an
argument for depth 25 over depth 100 that has nothing to do with bytes — see the
depth decision in the brief.

---

## Depth: the tiers are a whitelist, not a rounding — and 27 is **rejected**

The brief predicted "no 27 tier" and this project's most recent venue lesson
(ROADMAP A7) was that Anvil *rounds an unsupported depth up* and serves the next
tier. Kraken does not:

```text
→ {"method":"subscribe","params":{"channel":"book","symbol":["BTC/USD"],"depth":27,"snapshot":true}}
← {"error":"Subscription depth not supported","method":"subscribe","success":false,"time_in":"2026-08-16T23:54:19.629313Z","time_out":"2026-08-16T23:54:19.629343Z"}
```

**Two venues, one parameter, opposite behaviours.** Anvil's `depth` is a request
that is silently rounded; Kraken's is a strict enum that is refused. The general
rule A7 left behind — *a venue's depth parameter is a request, not a contract* —
survives, but M4's adapter must handle both failure modes and may assume
neither.

**And the refusal is silent afterwards, which is an invariant #5 trap.** The
connection stayed up, `status` had already arrived, and heartbeats kept coming
at 1 Hz — so a client that did not check `success` on the ack would sit on a
live, chattering socket with an empty book and nothing that looks like an error
after the first second. The subscribe ack is the only place this is ever said.

Depths 10, 25 and 100 were each accepted and served exactly (10/10, 25/25,
100/100 levels a side held after truncation). 500 and 1000 were not tested.

---

## Tick size and quantity step — and they *are* reachable over the WebSocket

`AssetPairs` over REST has them, but so does the v2 `instrument` channel, on the
same socket, with no REST call and no out-of-band configuration:

```json
{"symbol":"BTC/USD","base":"BTC","quote":"USD","status":"online","qty_precision":8,
 "qty_increment":1e-08,"price_precision":1,"cost_precision":5,"ws_display_price_precision":1,
 "cost_min":0.5,"tick_size":0.1,"price_increment":0.1,"qty_min":5e-05}
```

Measured against the captures, the metadata is honest: BTC/USD prices arrived
with at most **1** fractional digit and quantities with at most **8**, over
2,458 level entries; MINA/GBP prices with **4** and quantities with **8**. Those
are exactly `price_precision` and `qty_precision`.

Three things to carry into M4:

1. **ARCHITECTURE §4's declare-and-verify rule can become verify-against-the-venue
   here.** Anvil publishes no tick metadata, so DepthCharge declares a
   `SymbolSpec` and checks every price against it. Kraken publishes it, so the
   spec can be *read* and then still verified per price. That is a strictly
   better position and it costs one extra subscription.
2. **The `instrument` channel uses exponent notation** (`1e-08`, `5e-05`). The
   book channel never did in 9,932 frames. A number scanner written against the
   book alone will reject the metadata channel — so if M4 reads `instrument`, it
   needs an exponent path or a deliberate decision not to parse those fields.
3. The subscribe ack carries `"warnings":["tick_size is deprecated, use
   price_increment"]`. Both fields are present and equal today.

**A naming mismatch worth one line, because it will cost someone an hour.** The
v2 WebSocket symbol for bitcoin is **`BTC/USD`**. REST `AssetPairs` calls the
same pair `XXBTZUSD` and its `wsname` field says **`XBT/USD`** — the *v1* name.
So `wsname` is not the v2 name, and looking up a v2 symbol's tick size over REST
requires knowing that BTC and XBT are the same asset. The `instrument` channel
has no such problem: it is keyed by the v2 symbol.

---

## Reconnect

Captured with `capture_kraken.py --cycles 2 --reconnect-after 20 --reconnect-gap 6`
(`_local/kraken_btcusd_d25_reconnect_20260816.full.ndjson`, 886 frames, 2
connections).

- The drop is a **7,246 ms hole** in `rx_ns` (6 s requested + reconnect + TLS +
  handshake), and like Anvil it leaves **no marker on the wire at all** — the
  disconnect is observable only client-side.
- On reconnect the server sends `status` first, then the subscribe ack, then a
  **fresh full `book`/`snapshot`** ~100 ms later. There is no resume, no
  continuation and nothing to catch up: the new snapshot is the baseline and the
  old book must be discarded whole.
- **`slice_trace.py --mode reconnect` finds a Kraken resync unmodified.** Its
  sniffing looks for a mid-stream frame whose top-level `type` is `"snapshot"`,
  which was written for Anvil and happens to be exactly what a Kraken book
  snapshot carries. The brief allowed extending the tool if it could not; it
  can, so it was not touched.

---

## What each depth actually costs

Per capture, 90 s, bytes as sent (measured from verbatim text, never
re-serialised — `tools/kraken_frame_economics.py`):

| capture | frames | wire | **sustained** | book/update share |
| --- | ---: | ---: | ---: | ---: |
| BTC/USD depth 10 | 1,504 | 282,111 B | **3.05 KiB/s** | 98.8% |
| BTC/USD depth 25 | 2,878 | 564,275 B | **6.12 KiB/s** | 99.2% |
| BTC/USD depth 100 | 4,527 | 913,371 B | **9.91 KiB/s** | 99.0% |
| MINA/GBP depth 25 | 137 | 13,845 B | **0.15 KiB/s** | 68.2% |

Headroom, stated as a multiple the way A7 was:

| | vs **30.8 KiB/s** (Anvil today, `depth=27`) | vs **56 KiB/s** (soak's worst hour) |
| --- | ---: | ---: |
| depth 10 | **10.10×** | **18.36×** |
| depth 25 | **5.03×** | **9.15×** |
| depth 100 | **3.11×** | **5.65×** |
| MINA/GBP d25 | 206× | 375× |

**Every candidate depth, including the deepest, is cheaper than what the board
already runs on all day.** Depth 100 costs 9.91 KiB/s against Anvil's 30.8 — a
third. The staleness problem that dominated M3 does not exist at this venue at
any offered depth, and no depth choice can create it.

Two derived numbers, both rebuilt token-exact from the captures (and proven
byte-exact by `--verify`, which is what makes them measurements):

- **What this book would cost as full replaces**, i.e. if Kraken behaved like
  Anvil and re-sent the whole depth on every message: **56.06 KiB/s** at depth
  25 and **341.93 KiB/s** at depth 100. The delta encoding is worth **9.2×** and
  **34.9×** respectively. It is a coincidence worth not over-reading that the
  depth-25 figure lands on the soak's worst hour almost exactly.
- **The truncation cost** — what the depth-100 stream would weigh if it carried
  only the levels a 27-a-side ladder needs: **8.36 KiB/s of 9.81**, so
  discarding levels 28–100 saves **14.8%**. The waste in subscribing 100 to draw
  27 is much smaller than the level count suggests, because updates concentrate
  at the touch. The honest comparison for the depth decision is therefore
  depth 100's **9.81** against depth 25's **6.07** — **1.62×**, not 4×.

**Levels changed per message is degenerate at this venue, and that is the
finding.** The column that made A7 obvious — Anvil re-sent ~205 levels to change
a median of one — reads **identically to levels-per-message** here: 1.45 / 1.55 /
1.63 / 2.00 mean across the four captures, with **zero** messages changing
nothing, over 8,673 update messages. Against a correctly truncated book, every
level Kraken sends carries new information. There is no padding to remove and no
delta encoding to propose; this wire is already the thing A1 asks Anvil for.

*(Measured against an* untruncated *book the depth-100 capture appeared to have
124 no-op messages, 2.8%. Those are an artefact of the stale levels described in
Headline 2 — Kraken re-sending a level the client should already have dropped —
and not venue redundancy. Recorded because the wrong number was produced first.)*

---

## Committed traces

| File | Lines | Span | book msgs | Raw | gzip |
| ---- | -----: | ---: | ---: | --: | --: |
| `kraken_btcusd_d10_20260816.ndjson` | 902 | 60.0 s | 839 | 203,423 B | **23,636 B** |
| `kraken_btcusd_d25_20260816.ndjson` | 1,599 | 59.9 s | 1,537 | 372,924 B | **38,021 B** |
| `kraken_btcusd_d100_20260816.ndjson` | 2,535 | 60.0 s | 2,472 | 607,330 B | **59,451 B** |
| `kraken_minagbp_d25_20260816.ndjson` | 93 | 59.6 s | 30 | 14,020 B | **2,446 B** |

Line 1 of each is the metadata header; line 2 is the **subscribe this side
sent**, marked `"dir": "tx"` and excluded from every figure in this document.

The brief asked for three depth slices. The fourth — the quiet pair — is
committed as well because it costs **2.4 KiB** gzipped and it is the only
artefact in the repository that contains a 9-second healthy silence, which is
Headline 1's entire argument. Full 90 s captures stay untracked in `_local/`,
per the M0 policy.

### Which traces guard the truncation rule — and it is not the shallow ones

`kraken_btcusd_d10_20260816.ndjson` and `kraken_btcusd_d25_20260816.ndjson` are
the truncation goldens. Named, so they are not later dropped as "the least
production-like". Mutation-testing the self-check by deleting the truncate step
shows the mutant **caught by those two and missed by depth-100 and the quiet
pair.**

**The obvious explanation is wrong, and so was the second one.** It is not
*depth*: depth 25 on BTC/USD catches and depth 25 on MINA/GBP misses. It is not
*eviction rate* either — that was the next hypothesis and it is **anti-correlated**
with detection here:

| capture | levels evicted | per 1,000 book msgs | returning to the top 10 | never-truncating | verdict |
| --- | ---: | ---: | ---: | ---: | --- |
| BTC/USD d10 | 275 | 327.8 | 169 | 379 / 839 | **catches** |
| BTC/USD d25 | 585 | 380.6 | 11 | 460 / 1,537 | **catches** |
| BTC/USD d100 | 1,156 | **467.6** | 0 | 2,472 / 2,472 | misses |
| MINA/GBP d25 | 19 | **633.3** | 0 | 30 / 30 | misses |

Eviction is *necessary* — a non-truncating client cannot diverge until it is
wrongly keeping something — and the quiet pair shows why depth alone says nothing:
19 evictions in 60 s, so there is almost nothing wrongly kept to go wrong with.
But depth 100 evicts hardest of all three BTC captures and detects nothing, so
the rate is not the discriminator.

**"Returning to the checksummed top 10" looked like the discriminator, fitted all
four slices, and was falsified by the fifth capture the same evening.** The
reconnect trace scores 3 returns and detects nothing (840/840). Two reasons, both
read off the mechanism rather than inferred: a price can only re-enter the book
because the venue *sent* it, and that same message corrects a non-truncating book
too; and a resync **heals** a non-truncating book outright, because a snapshot
replaces every level. **A trace containing a reconnect is a worse truncation
golden than one without** — which is the opposite of the instinct that a
reconnect trace is the more thorough artefact.

What actually goes wrong is subtler than any of the three: the non-truncating
book carries extra levels the venue has stopped mentioning, and it is wrong when
one of those ranks inside *its own* best 10 while the venue's does not.

**So the criterion is the direct measurement, not a heuristic:**
`ok_never_truncating < checksummed`. `--verify` prints it as **CATCHES** or
**MISSES** per trace and `--pin` refuses to let a new trace be adopted as a
truncation golden quietly. The two counters above stay because the pair describes
*why* a trace does or does not exercise the rule; neither replaces running the
check.

### What transfers to M5, and — more importantly — what does not

The capture recipe that falls out of the above is: **a liquid pair, the shallowest
depth the venue offers, and no reconnect inside the committed window** — then check
it with the tool before trusting it.

**Read that alone and it will mislead you at Binance.** It works here for one reason
that is easy to miss: **Kraken publishes a CRC32 over the top 10 levels, so this
venue hands us an independent oracle.** Every claim in Headline 2 — that the
maintained book *is* the venue's book, that a non-truncating client is wrong 1,077
times in 1,537 messages, that the trace lost nothing — rests on comparing our
derived state against a number the venue computed itself. Without that, "did this
trace catch the defect?" is not answerable at all: a broken implementation and a
correct one produce two books and nothing to say which matches the exchange.

**Binance has no checksum.** Its correctness claim is a different construction
entirely — `U`/`u` sequence bracketing of the diff stream against a REST depth
snapshot — which detects *lost or misordered messages* and says nothing about
whether the book that resulted is right. Its failure modes are also different in
kind: the bracketing can be satisfied while the book is wrong (a mis-applied but
correctly-sequenced update), and the REST snapshot itself arrives out-of-band with
its own `lastUpdateId` to reconcile. So **M5 needs its own detector before it can
have a golden**, and inventing one is part of M5's work rather than a detail of it.
Candidates worth pricing when the time comes: bracketing continuity as a weaker
oracle, a second independent implementation to differential-test against, or a
periodic REST snapshot used as a checkpoint the streamed book must reproduce.

One checklist item for whoever builds that detector, from this session's own review pass
(stage-0 brief session log, 2026-08-16 late): **Binance's masking mechanism is a
*scheduled* REST re-snapshot, so it is constant rather than incidental** — a clean window
cannot simply be chosen the way it can here, and the detector must account for it by
construction.

**Only the last clause of the recipe transfers, and it transfers unchanged:**
*whatever the oracle is, prove the trace catches a deliberately broken
implementation before pinning it.* The depth heuristic does not transfer, the
liquidity heuristic does not transfer, and the reconnect exclusion transfers only
in so far as Binance has a healing event of its own (it does — the REST re-snapshot
on gap recovery, which is exactly the shape of thing that repairs a broken book
mid-trace and should therefore be kept out of a guarding window). The general form
of that last clause is now `ARCHITECTURE.md` §9, 2026-08-16 (stage 0).

Capturing the most production-like configuration and assuming it covers the rule is
exactly what would have happened here at depth 100, where a 90-second test is 100%
clean while the client is wrong.

### The tools are pinned too

Every figure in this document comes out of `tools/kraken_frame_economics.py`, and
that tool shipped two bugs on the way here — a truncation column that discarded
every level entering the top N, and a `--verify` float shadow that silently
skipped 92 real checksum comparisons. Both printed believable numbers. So the
committed slices are now goldens **for the tool** as well as inputs to it:
`--selfcheck` recomputes them against a `KNOWN_ANSWERS` table and `ctest` fails
loudly on any move. A moved number means either a tooling regression or a
deliberate re-capture — and in the second case the expectations and this
document's figures move in the same commit. Never the third thing.

### These traces were not readable by `dc_replay`, deliberately — CLOSED at M4 stage A

**Superseded 2026-08-17.** The section below is left standing unedited, because
it is the statement of a problem and the reasoning is the part worth keeping;
this is what happened to it. `dc_replay` now reads all four committed slices end
to end (`dc_replay_kraken_*` in ctest), and the resolution is the **first** of
the three options it names: the metadata contract gained a `venue` tag and one
reader dispatches to per-venue decoders. The rule is written up in
[`NOTES.md` § The trace metadata contract](NOTES.md#the-trace-metadata-contract--binding-for-every-trace-all-venues-both-languages)
and the decision is `ARCHITECTURE.md` §9, 2026-08-17. Nothing was reshaped: the
four slices are byte-for-byte the files stage 0 committed, and the heartbeat and
the ack — which this section says are the two frames the findings rest on — are
now *named and counted* rather than merely tolerated. The record taxonomy that
came out of reading them is the next section but one.

The one correction to the text below: it says the `status` frame has no `type`.
It has one — `{"channel":"status","type":"update",…}` — and it is the two
`subscribe` records (the one this side sent and the ack) that make up the 61
alongside 59 heartbeats. The count was right; one of the three names was not.

Run today, verbatim:

```text
dc_replay: kraken_btcusd_d25_20260816.ndjson: line 1: metadata line missing a
required field (need captured_at, url, ticker, tool_version)
```

It fails on the **metadata header**, before reaching a frame: `ticker` is an
Anvil integer id and a Kraken capture has a `symbol` instead. Even past that,
`TraceReader::next` requires every frame object to carry a **string `type`**
(`harness/src/trace.cpp:233`) — and `{"channel":"heartbeat"}`, the subscribe ack
and the `status` frame have no `type` at all, so **61 of the depth-25 slice's
1,599 records** would be rejected individually even after the header passed.

**Recorded rather than fixed, and the traces were not reshaped to suit it.** Both
requirements are ARCHITECTURE §9 (2026-08-07) decisions — one definition of a
valid trace, deliberately strict, arrived at because two readers had drifted.
Loosening them to admit Kraken is a real design change for the M4 brief to make
with the adapter in front of it: either the metadata contract becomes
venue-tagged, or the reader learns a second dialect, or Kraken traces get their
own reader. Bending the *capture* to fit today's reader would have thrown away
the heartbeat and the ack — the two frames Headline 1 and the depth trap rest
on — which is precisely the wrong trade.

---

## The record taxonomy, measured (M4 stage A)

Added 2026-08-17. Every figure below is read out of the committed files by
`dc_taxonomy`, the C++ reader that now dispatches on the venue tag, and every one
of them is **pinned** in `harness/include/dc_harness/taxonomy_pins.inc` with
`ctest`'s `trace_taxonomy_selfcheck` failing on any move. They were
cross-checked against an independent Python pass over the same files before
being pinned, and the pin was mutation-verified — four deliberate breakages
(renaming a kind, counting the sent subscribe as venue traffic, moving the
declared threshold, and dropping the resync rule) are each caught.

| kind | d10 | d25 | d100 | MINA/GBP d25 |
| --- | ---: | ---: | ---: | ---: |
| `book/update` | 838 | 1,536 | 2,471 | 29 |
| `book/snapshot` | 1 | 1 | 1 | 1 |
| `heartbeat` | 60 | **59** | 60 | 60 |
| `status/update` | 1 | 1 | 1 | 1 |
| `ack:subscribe` | 1 | 1 | 1 | 1 |
| `tx:subscribe` | 1 | 1 | 1 | 1 |
| **total records** | **902** | **1,599** | **2,535** | **93** |
| of which untyped | 62 | **61** | 62 | 62 |
| of which sent by us | 1 | 1 | 1 | 1 |
| book events | 839 | 1,537 | 2,472 | 30 |

**The 61 are explained, and they are three things, not one.** In the depth-25
slice: **59 heartbeats** (`{"channel":"heartbeat"}`, no payload at all),
**1 subscribe ack** (`method` + `result` + `success`, no `channel` and no
`type`), and **1 subscribe this side sent** (`"dir": "tx"`). The other three
slices read 62 because their 60-second window caught 60 heartbeats rather than
59 — the 1 Hz broadcast and the window edge, nothing else. No untyped record in
any committed slice is unaccounted for; total = typed + untyped exactly, on all
four.

Two of the three were the reason not to reshape the traces, and they earned it:
the heartbeat is Headline 1's whole argument, and the ack is where a refused
subscription is visible at all. The classifier names a refused one
`ack:subscribe REFUSED` rather than folding it into the ack count, so an
instrument counting acks cannot hide the trap.

**`status` is not one of the untyped.** It carries
`{"channel":"status","type":"update",…}` — the section above got that name wrong
while getting the count right, and the correction is recorded there.

### The two clocks, now measured per trace

The same pass counts what the M1 replay rule would do to these files at Anvil's
1,000 ms threshold and at Kraken's declared 15,000 ms. Every firing in the middle
two columns is a **disconnect that did not happen**:

| trace | worst record gap | worst book gap | record rule @1,000 | book rule @1,000 | either @15,000 |
| --- | ---: | ---: | ---: | ---: | ---: |
| BTC/USD d10 | 1,005 ms | 5,058 ms | 7 | 13 | **0** |
| BTC/USD d25 | 1,003 ms | 4,535 ms | 5 | 10 | **0** |
| BTC/USD d100 | 998 ms | 1,371 ms | **0** | 3 | **0** |
| MINA/GBP d25 | 1,026 ms | 9,007 ms | **25** | 12 | **0** |

Read the record-rule column across the four rows before anything else. It goes
7, 5, **0**, 25 — and the worst record gap in all four is within 27 ms of one
second (998.1, 1,002.9, 1,004.6, 1,026.2), because the 1 Hz heartbeat is the
floor. **Whether the Anvil constant
trips on a Kraken trace is heartbeat jitter, not signal.** Depth 100 happens to
come in at 998 ms and looks clean; the same subscription five minutes later would
not. That is stage 0's "straddles the constant" finding arriving as four numbers,
and it is the argument against the any-data-frame rule (ARCHITECTURE §9,
2026-08-17, decision 2) in the only form that can be checked.

The book-rule column is the honest one — it counts silence in the thing
invariant #5 actually cares about — and it says the quiet pair is legitimately
silent past a second twelve times a minute, worst 9,007 ms. At the declared
15,000 ms, every cell in both columns is zero.

---

## Every place the wire disagreed with the brief's doc summary

The brief said each line below was a hypothesis to confirm. Confirmed, corrected
or added:

| Claim in the brief | Verdict |
| --- | --- |
| `wss://ws.kraken.com/v2`, no auth, TLS with SNI required | **Confirmed.** SNI is sent by `ssl.wrap_socket(server_hostname=…)`; the upgrade was accepted with **no `Origin` header**, as at Anvil. |
| Subscribe frame shape | **Confirmed** exactly as written. |
| Depth tiers 10, 25, 100, 500, 1000; one depth per symbol per connection | **Confirmed for 10/25/100** (500/1000 untested). **Corrected:** an unsupported depth is **refused**, not rounded up — and one subscribe may carry many symbols at one depth (six tested). |
| `type:"snapshot"` then `type:"update"`; `qty: 0` removes | **Confirmed.** Removals are 24% of level entries. |
| CRC32 over the top 10 levels only, regardless of subscribed depth, asks ascending then bids descending, point removed, leading zeros stripped | **Confirmed, and verified**: 8,677 / 8,677 checksums reproduced at depths 10, 25 and 100 with the top-10 rule. |
| The client must truncate to the subscribed depth itself | **Confirmed, and quantified** — Headline 2. |
| v2 puts price and qty on the wire at full precision as bare JSON numbers | **Confirmed, and it is worse than the brief implies** — Headline 3. |
| — | **New: a 1 Hz `heartbeat` channel**, unmentioned in the summary, and it changes the liveness question (Headline 1). |
| — | **New: a `status` frame** on every connection, with a `connection_id` too large for a double. |
| — | **New: the `instrument` channel** publishes tick size and qty step over the WebSocket, so §4's declared `SymbolSpec` can be verified against the venue. |
| — | **New: `wsname` in REST `AssetPairs` is the v1 name (`XBT/USD`), not the v2 symbol (`BTC/USD`).** |

## Known unknowns still open

**Three of these were closed on 2026-08-18 (M4 stage B2) and the list below is
left standing unedited, because the statement of what was NOT known is the part
worth keeping.** The checksum-failure path, the resubscribe question and the
queue-vs-shed question are all answered in
[the B2 section](#m4-stage-b2-2026-08-18--the-healing-path-and-four-things-the-wire-said)
at the end of this file, which also restates what remains open.

- **Depths 500 and 1000 were never subscribed.** No reason to; recorded so the
  tier table is not read as fully tested.
- **`data` never held more than one entry.** A multi-symbol connection was
  measured (six pairs, one socket) but its messages were not captured to file,
  so "one symbol per message" is an observation, not a proven property.
- **No `error` frame was ever seen mid-stream**, only the `success:false`
  subscribe ack. Whether Kraken emits an in-stream error, and what it looks
  like, is unmeasured.
- **No checksum failure was ever observed**, so the `Gap{ChecksumFail}` path has
  no captured example. It cannot be provoked from outside; M4's coverage for it
  will have to be a synthesised trace, the same shape and for the same reason as
  the `FrameReassembler` tests (`NOTES.md`, M3 addendum).
- **Nothing is known about a long connection.** The longest capture here is 90
  seconds. Anvil needed a 23.6-hour soak to find its stall, and Kraken has had
  nothing comparable.

---

## Measurement caveat: the clock, and a correction to `NOTES.md`

`rx_ns` in every Kraken capture comes from **`time.perf_counter_ns`**, not
`time.monotonic_ns`, and the metadata header of each trace records which
(`"clock": "perf_counter_ns"`). This matters and it corrects a proposed remedy in
this directory's other notes file.

`NOTES.md`'s M3 addendum records that the 2026-08-09 Anvil capture's `rx_ns` sat
on a **15.625 ms** grid, making every gap figure ±16 ms, and suggests the fix is
to "capture from WSL or raise the process timer resolution first". **Measured
2026-08-16 on this box (Python 3.12, Windows 11): raising the process timer
resolution does not work.** `timeBeginPeriod(1)` leaves `time.monotonic_ns()`
advancing in **15.0 ms** steps, unchanged. `time.perf_counter_ns()` steps at
**0.0002 ms**.

The clock was the lever, not the timer period. It matters here far more than it
did at Anvil: the p50 book gap at depth 25 is **0.1 ms** and the p90 is 58 ms,
so a 15 ms grid would have reported a distribution largely composed of its own
quantisation. `capture_anvil.py` deliberately keeps `monotonic_ns`, so its
traces stay comparable with the four committed Anvil captures; switching it is a
one-line change and a decision for whoever next re-captures that venue.

---

## M4 stage B1 (2026-08-18) — what the adapter measured on the way in

Three findings, all taken *before* the adapter was written because each one
decides its shape, and one defect found at review that the five committed slices
could not catch.

### 1. Every number on the book channel carries EXACTLY its declared precision

Counted from the verbatim frame text — not through a JSON parse, which is the
substitution Headline 3 shows is fatal here:

| slice | price fractional digits | qty fractional digits | exponent tokens |
| --- | --- | --- | ---: |
| BTC/USD d10 | `{1: 1276}` | `{8: 1276}` | 0 |
| BTC/USD d25 | `{1: 2458}` | `{8: 2458}` | 0 |
| BTC/USD d100 | `{1: 4249}` | `{8: 4249}` | 0 |
| MINA/GBP d25 (16th) | `{4: 107}` | `{8: 107}` | 0 |
| MINA/GBP d25 (17th) | `{4: 82}` | `{8: 82}` | 0 |

**8,172 level entries, one spelling each, no exceptions.** `price_precision` and
`qty_precision` from the `instrument` channel are not approximations of what
arrives — they are what arrives, every time.

### 2. Therefore the CRC32 reproduces from INTEGER TICKS ALONE

The checksum token is "decimal text, point removed, leading zeros stripped". At
exactly the declared precision that string *is* the decimal spelling of the
scaled integer: `0.00005100` at 8 decimals is 5,100 steps and tokenises to
`5100` by either route. Verified end to end, book held as integers, truncated to
the subscribed depth, no wire text retained anywhere:

| slice | checksums reproduced from integers |
| --- | ---: |
| BTC/USD d10 | **839 / 839** |
| BTC/USD d25 | **1,537 / 1,537** |
| BTC/USD d100 | **2,472 / 2,472** |
| MINA/GBP d25 (16th) | **30 / 30** |
| MINA/GBP d25 (17th) | **0 / 49** |
| | **4,878 / 4,878** on the four with a snapshot |

**The fifth is not a failure, it is the mid-stream slice answering correctly.**
It begins with no snapshot, so the book being checksummed was assembled from
amendments to nothing — a different book that merely looks plausible. That is
the measurement behind the adapter's *no baseline, no deltas* rule, and it also
means **this slice can never be a CRC golden**.

What this buys B2: the checksum path is a *check*, not a parse. Nothing has to
retain wire text, so invariant #7 is satisfied structurally rather than by
budget. The residual assumption is finding 1 — a venue that started sending
`0.5` where it now sends `0.50000000` would break the reconstruction and *not*
the parse, so B2's first CRC mismatch means this section before it means a lost
message.

### 3. The adapter's eviction counts reproduce stage 0's, independently

`levels_evicted` is the count of levels **Kraken never sent a removal for** and
the client had to drop itself. The C++ adapter and `kraken_frame_economics.py`
were written months apart for different purposes and neither was tuned to the
other:

| slice | stage 0 (Python) | B1 adapter (C++) |
| --- | ---: | ---: |
| BTC/USD d10 | 275 | **275** |
| BTC/USD d25 | 585 | **585** |
| BTC/USD d100 | 1,156 | **1,156** |
| MINA/GBP d25 | 19 | **19** |

Two implementations, two languages, no shared parent — which is the independence
test ARCHITECTURE §9's close-out row of 2026-08-17 says to apply before calling
agreement corroboration. This one passes it.

### 4. The defect the five slices could not catch

`adopt_snapshot` truncated nothing: it seeded every level the frame carried,
capped only at the 256-level staging buffer rather than at the subscribed depth.
**All five slices were green.** Kraken serves exactly the depth requested
(10/10, 25/25, 100/100 — measured at stage 0), so the frame never carried more
than the subscription and the two caps coincided on every committed file.

It reproduces in one synthetic frame: a 5-level snapshot against a subscribed
depth of 2 left the ladder holding 5. The consequence is Headline 2's defect
exactly — a non-truncating book, wrong in 1,077 of 1,537 messages at depth 25.

Two live routes reach it, so it was not hypothetical: a capture whose metadata
records no `depth` falls back to the firmware constant while the file may be a
depth-100 slice, and `ack_depth_mismatch` exists precisely because a venue may
serve a depth other than the one asked for — **Anvil already rounds depth UP**,
and the depth section above requires this adapter to assume neither behaviour.

Recorded here rather than only in the fix, because the general form is this
milestone's own: *a corpus in which the interesting input never occurs produces a
green that is a statement about the corpus.* See ARCHITECTURE §9, 2026-08-18.

### Also worth having: the deep tiers do not fit

`kMaxSnapshotLevels` is 256 and this venue offers 500 and 1000. A subscription at
either would be clamped and the book would be short — **and the CRC32 would not
catch it, because it covers only the top 10 levels.** Nothing subscribes that
deep (the firmware constant is 25, and 500/1000 remain untested per the known
unknowns above), but the limit is now a `static_assert` on the compile-time
constant rather than a discovery.

---

## M4 stage B2 (2026-08-18) — the healing path, and four things the wire said

The checksum stops being a number the adapter carries and becomes a comparison it
makes. Everything below was measured on the evening of 2026-08-18; the first two
sections are about the check, the last three are about what building it exposed.

### 1. The CRC32 verifies in the SHIPPING adapter, and reproduces B1's figure

B1's 4,878 / 4,878 was a desk measurement in Python over stored decimal *text*,
used to derive goldens. The C++ adapter now makes the same comparison on the same
files, from scaled *integers*, with no wire text retained anywhere:

| slice | checksums | matched | failed | unverifiable |
| --- | ---: | ---: | ---: | ---: |
| BTC/USD d10 | 839 | **839** | 0 | 0 |
| BTC/USD d25 | 1,537 | **1,537** | 0 | 0 |
| BTC/USD d100 | 2,472 | **2,472** | 0 | 0 |
| MINA/GBP d25 (16th) | 30 | **30** | 0 | 0 |
| MINA/GBP d25 (17th) | 49 | 0 | **0** | **49** |
| | | **4,878 / 4,878** | | |

Two languages and two representations agreeing on the same 4,878 numbers, neither
derived from the other. That is a sharper independence than the eviction counts of
B1, because the two routes to the checksum token are different arithmetic rather
than the same arithmetic written twice.

**The fifth slice reads 49 UNVERIFIABLE, not 49 failures, and the distinction is
the whole reason it is in `NOT_A_CHECKSUM_GOLDEN`.** It has no opening snapshot,
so there is no book to compare against; an adapter that compared anyway would
report 49 mismatches, grey the panel, and blame the wire for a fact about the
file. The three outcomes are counted separately and their sum is asserted to equal
the number of checksums seen, so a fourth outcome cannot appear quietly.

**Also measured, and it is a wire fact rather than an assumption:
`book_msgs_unchecksummed` is 0 on all five slices.** Every `book/snapshot` and
every `book/update` this venue sends carries a checksum. That counter exists
because the failure it guards is silent — an adapter fed messages without
checksums verifies nothing while every other counter looks healthy.

### 2. WHAT THE CHECKSUM CANNOT SEE — measured directly, not inherited

Stage 0 confirmed "top 10 regardless of subscribed depth" by reproducing 8,677
captured checksums with a top-10 rule. That is strong evidence of the *rule* and
no evidence at all about the *blind spot*, because every captured message agrees
with both readings — the venue never sends a message whose only effect is
invisible to its own checksum, having no reason to.

So B2 measured the blind spot directly, with the input no capture contains: **a
book edited BELOW level 10 does not move the checksum, and the same edit at level
10 does.** Both are synthetic and both are mandatory (ARCHITECTURE §9,
2026-08-18: where an adapter's assumption and a venue's behaviour could coincide,
the synthetic case is the instrument and the golden is decoration).

**At the shipped depth of 25 the CRC validates the top 10 levels a side — 20 of
the 50 levels the ladder holds.** So the panel's 27 rows a side draw 10 validated
levels, 15 that are rendered and never reached by the check, and 2 that are empty
by construction. Stated plainly, with the arithmetic spelled out, because
shipping it unstated would be the defect:

- It is **not** a reason to distrust levels 11–25. A delta stream that checksums
  clean at the top has provably lost no message, so an error down there would have
  to be an error in DepthCharge rather than a wire loss.
- It **is** the reason a defect confined below level 10 is undetectable by this
  check — and stage 0 measured one of exactly that shape, the non-truncating book,
  which is 100% correct on a top-10 CRC at depth 100 over 90 seconds.
- It is why `kKrakenSubscribeDepth` is 25 rather than 100, and why the 500/1000
  tiers carry a `static_assert` rather than a comment.

The mutation that proves this is graded rather than decorative: widening
`kChecksumLevels` from 10 to 25 turns **6 test cases and 28 assertions red**,
including the four slices' 4,878. So the corpus discriminates the venue's rule,
not merely our implementation of it.

### 3. A CLIENT-INITIATED RESUBSCRIBE PRODUCES A SNAPSHOT INDISTINGUISHABLE FROM THE ON-CONNECT ONE

The open question this stage inherited, and the answer is the strong form of
"no difference". Captured 2026-08-18 with `capture_kraken.py --resubscribe-after`,
one connection, one deliberate unsubscribe/re-subscribe pair:

| | on-connect snapshot | mid-stream snapshot |
| --- | --- | --- |
| top-level keys | `channel`, `data`, `type` | `channel`, `data`, `type` |
| `channel` / `type` | `book` / `snapshot` | `book` / `snapshot` |
| `data[0]` keys | `asks`, `bids`, `checksum`, `symbol`, `timestamp` | identical |

**There is no marker of any kind.** A resync is therefore detectable ONLY by
position in the stream, which is exactly what the venue-free predicate uses — *a
snapshot is a resync when a book event preceded it* — and a Kraken-specific branch
would have had nothing to branch on. The predicate is not merely sufficient here;
it is the only thing that could work.

The committed artefact is
[`kraken_minagbp_d25_resync_20260818.ndjson`](kraken_minagbp_d25_resync_20260818.ndjson)
— 99 records, 14 KiB, `snapshots=2 resyncs=1`, taxonomy pinned, and the first
committed trace at any venue in which **this client speaks twice** (`tx=3`). The
quiet pair deliberately: a resync slice needs a wall-clock window long enough for
both subscriptions to calibrate their heartbeat clocks, and only a slow pair fits
72 s of that into 14 KiB. The busy equivalent — BTC/USD depth 25, 2,661 frames in
46 s — was captured the same evening, ran the same path, and is 636 KiB; it stays
in `_local/`.

**Two things it also settles.** Measured on that BTC/USD capture: between the
unsubscribe ack and the re-subscription's snapshot there is a **3,548 ms hole in
book events**, and the 1 Hz heartbeat runs straight through it unbroken. So no
watchdog fires and nothing on the connection looks wrong — which means a book left
baselined across that window renders live while standing still, invariant #5's one
forbidden output produced by our own resubscribe. The adapter therefore drops the
book on the unsubscribe ack and raises `Gap{Resync}`; on the committed slice that
is a **1,047 ms grey window that no watchdog produced**, and it is the first
committed trace anywhere in this project whose stale episode was opened by a
frame's CONTENT rather than by silence.

### 4. THE UNSUBSCRIBE ACK IS SHAPED EXACTLY LIKE A SUBSCRIBE ACK, AND THE PARSER DID NOT LOOK

Found by taking the capture above, not by reading the parser.

```
{"method":"subscribe",  "result":{...},"success":true}
{"method":"unsubscribe","result":{...},"success":true}
```

No `channel`, no `type`, `method` + `result` + `success` in both. The parser's
classification was `if (have_method) return SubscribeAck;` — it never read the
method NAME. Benign while the venue says `success:true`; one refused unsubscribe
from `on_ack` latching `Refused`, **which the firmware turns into `die()`**.

**No capture could have caught it, and that is the point rather than the excuse.**
The healing path is the first thing this project has ever built that SENDS an
unsubscribe, so before 2026-08-18 no trace at any venue contained one. It is B1's
truncation defect exactly: the discriminating input did not exist, so the code and
every committed file agreed. The general rule is already on record (ARCHITECTURE
§9, 2026-08-18) and this is its second instance in two evenings — worth noting
that the rule's cheap operational form, *write the case where the client and the
venue disagree*, would not have found this one either. What found it was
**capturing a frame the corpus had never contained**, which is a third thing:
neither a golden nor a synthetic case, but a deliberate widening of the corpus.

`ack:unsubscribe` now appears in the taxonomy, `FrameKind::UnsubscribeAck` in the
parser, and an unrecognised method (`ping`/`pong`, which M6 owns) falls to
`Unknown` — tolerated, counted, ignored — rather than to the nearest familiar
thing, since that fallback is how this happened.

### 5. KRAKEN QUEUES. `age_ms` STANDS AT THIS VENUE

The measurement B1 pinned as an open assumption with B2 named as its owner, and
the one result tonight that changes a number already on the panel.
`tools/kraken_backpressure_probe.py`, 2026-08-18, BTC/USD depth 25, **one
connection, 80 s, three phases**, throttled by sleeping 120 ms after every message
— the same lever `_local/drain-120ms.ndjson` uses at Anvil:

| phase | delay | msgs | rate | checksums | lag p50 | lag worst | slope |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| baseline | 0 ms | 389 | 19.47/s | **367 / 367** | −1.456 s | −1.315 s | −0.000 s/s |
| throttled | 120 ms | 343 | 8.63/s | **331 / 331** | 16.373 s | **26.017 s** | **+0.690 s/s** |
| release | 0 ms | 1,255 | **64.20/s** | **1,207 / 1,207** | 6.498 s | 26.117 s | −1.411 s/s |

**Three independent signals, all pointing the same way.**

1. **Not one checksum failed** — 331/331 through the throttle, 1,905/1,905 across
   all three phases. A shed update leaves a level wrong and the venue's own CRC32
   says so within a message or two. **Nothing was lost.**
2. **Lag grew +0.690 s per second**, to 26.0 s, read off the venue's own
   microsecond `timestamp` on every book message. **Everything was late.**
3. **On release the backlog drained at 64.2 msg/s against a 19.5 msg/s baseline**
   — 3.3× — and the lag decayed at −1.411 s/s back to where it started. A server
   that had shed would have had nothing to catch up with.

Nothing lost + everything late + a backlog to deliver **is** a queue. So the
deficit `age_estimator.hpp` computes is a queuing lag at Kraken as it is at Anvil,
and **`age_ms` stands**. Row one of the brief's three-outcome table; rows two
(sheds → stop and raise) and three (server disconnects us) did not occur, and the
socket survived a 26 s backlog without closing.

**The arithmetic closes on itself, which is what makes it more than three
plausible columns.** A slope of 0.690 means we drained at 31% of the venue's rate,
so the venue was producing 8.63 / 0.31 ≈ **27.8 msg/s** during the throttle. The
release phase implies the same figure independently: it cleared ~27.5 s of backlog
plus 19.5 s of live traffic — about 47 s of venue output — in 19.5 s at 64.2/s,
so ≈ **27 msg/s**. Two routes to the venue's true rate, neither derived from the
other, agreeing to within 3%.

**Three caveats, stated rather than discovered.**

- **The lever is TCP backpressure**, not an application-level slow consumer, so
  what was measured is the whole path's behaviour and not a statement about
  Kraken's internal send queue. That is the right question for `age_ms` — the
  panel experiences the path — but unlike Anvil's, whose `CrowWsSubscriber::
  deliver()` is readable source, it is an observation and not a contract. It could
  change without notice, and the probe is one command to re-run.
- **The absolute lag is meaningless; only the slope is.** The baseline reads
  −1.456 s, i.e. this desk's clock sits about 1.5 s off Kraken's. That constant
  offset cancels in every comparison made above and would not in any comparison
  someone made against a fixed threshold.
- **The `drop` figure of 56% understates the throttle.** It is computed against the
  baseline rate, and the market sped up during the run — the true drain fraction
  was 31%, from the slope. Read the slope, not the rate.

### Known unknowns — what B2 closed, and what it did not

**Closed.** *"No checksum failure was ever observed, so the `Gap{ChecksumFail}`
path has no captured example"* — still true of captures, and the path now has
synthetic coverage driving it end to end, which per ARCHITECTURE §9 (2026-08-18)
is the instrument for this class rather than a substitute for one. *"Whether a
client-initiated resubscribe produces a snapshot distinguishable on the wire"* —
answered above: it does not. *Kraken's queue-vs-shed behaviour* — answered above:
it queues.

**Still open, and unchanged.** Depths 500 and 1000 remain unsubscribed. `data`
has still never held more than one entry in a captured file. **No `error` frame
has ever been seen mid-stream** — and B2 adds a reason to care, because the resync
path now sends unsubscribes and a refused one has never been observed either; the
adapter's handling of it is synthetic and correct-by-construction rather than
measured. **Nothing is known about a long connection**: the longest capture here
is 90 s, and a resubscribe cadence under repeated CRC failures is the kind of
thing only a soak would show.

**New, and owed.** The 3,548 ms book-event hole across a resubscribe is measured
on one capture at one pair. The adapter is honest about it by construction — the
book drops on the ack, so the window is grey whatever its length — but the LENGTH
is what a firmware resubscribe cadence would have to be sized against, and D's
bench work is where that gets a second measurement.
