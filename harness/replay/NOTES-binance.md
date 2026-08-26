# Binance spot — what the wire actually costs, and what can tell us the book is right

Measured 2026-08-24 (M5 stage 0). Every figure below comes out of
`tools/binance_frame_economics.py` and `tools/binance_oracle.py` over captures taken
that evening with `tools/capture_binance.py`. Where this document and
[`docs/briefs/M5-stage-0-price-the-binance-wire.md`](../../docs/briefs/M5-stage-0-price-the-binance-wire.md)
disagree, **this document wins**: the brief is a summary of documentation and this is a
record of measurement. Every disagreement found is listed in
[§ Where the wire disagreed with the brief](#where-the-wire-disagreed-with-the-brief).

Endpoints answered from this IP on the first attempt: `data-api.binance.vision`
returned **HTTP 200**, `data-stream.binance.vision` returned **101 Switching
Protocols**. No API credential was needed, requested, or created.

---

## Headline 1 — the oracle exists, it is exact, and it is the venue's own top-20 stream

**`@depth20@100ms`'s `lastUpdateId` coincided exactly with a diff event's `u` on
every partial payload of every complete capture — 899/899, 901/901, 90/90 and
29/29, four for four, 100.0%.** Not "sometimes", not "usually": the two streams are
published from the same update boundary, so the comparison is **exact at every
tick**. That was the single measurement candidate (a) lived or died on.

**The boundary condition, stated because it is real and because the first draft of
this document contradicted itself over it.** A *committed slice* can carry one
partial payload that does not coincide, and one of them does: the 15 s
`binance_btcusdt_d100ms` slice reports 150/151 (99.3%). **It is a cut artefact, not
a venue behaviour, and it was chased rather than rounded away.** The odd payload is
`lastUpdateId = 99076452055` at line 306 — *the last record in the file*, record 301
of 301 — and its id is **beyond the highest diff `u` the slice contains**
(99076452014). The slice holds 151 partials against 150 diffs, because
`slice_trace.py` cuts on an `rx_ns` window boundary and that boundary fell between
the partial and the diff event ending on the same update. In the full 90 s capture
the same stretch is **901/901 with none odd**.

So the accurate claim has two halves, and the second is not a footnote:
**the venue publishes both streams from the same update boundary, exactly, always
so far — and a window cut on a time boundary can still end between the two
publications of one boundary, leaving a trailing partial that can never be graded.**
That is precisely what the *unverifiable* bucket is for, and it did its job: the
oracle reported it as `never-reached-before-capture-ended` rather than silently
matching or silently failing. **`unverifiable` is a live bucket, not a permanently
empty one**, and a stage that declared it empty for good would have been the
"oracle that cannot fail" one row down from itself.

### Three witnesses, not one — and one of them cannot answer the hardest question

Added at review (2026-08-24). The first draft rested the exactness claim, the 884/884 and
the entire mutant reach on **one capture, one pair, one day** — while the tool measuring
the coincidence is the tool whose correctness that coincidence is being used to establish.
Two more deep-seeded captures now stand behind it.

| witness | coincidence | honest | mutants exercised |
| --- | ---: | ---: | --- |
| `btcusdt_deepseed` | 899/899 (100%) | 884/884 | **3 of 3, all RED** |
| `btcusdt_deepseed2` — a later window, same evening | 901/901 (100%) | 886/886 | **3 of 3, all RED** |
| `atomeur_deepseed` — quiet pair | 8/8 (100%) | 8/8 | **2 of 3**, see below |

`deepseed2` is the one that genuinely de-single-sources the mutant suite: an independent
90 s window reproducing every headline figure.

**The quiet pair produced a result worth more than the confirmation it was taken for.** It
was added on the reasoning that a boundary mismatch would be likeliest to show on a thin
book. It showed something else: **on a book that never churns, the bounded-window mutant is
not caught — because it is not a mutant there.** ATOMEUR moved so little in 90 s (8 graded
ticks) that truncating the maintained book to 25 levels a side never removed a level later
needed, so the mutant's book is **byte-identical to the honest one at every graded tick**.
There is no defect to detect.

That is neither a pass nor a failure, and the tool now says so in those words: `--check`
reports **`NOT EXERCISABLE`** for a mutation that produced no observable difference on the
trace, and **fails outright if no trace in the set exercises all three**. *A capture too
quiet to break is indistinguishable from an oracle too weak to notice*, and a suite whose
every witness is too quiet reports green for the same reason an empty one does.

Exercisability is measured by comparing the mutant's graded books against the honest one's,
not by a per-mutant heuristic. **The first attempt used a heuristic — *did the book ever
exceed the window?* — and it answered EXERCISABLE for ATOMEUR, wrongly:** the seed held more
than 25 levels, but the ones below rank 20 never rose into view, so the truncation changed
nothing that was ever graded.

Graded against a correct client over the 90 s deep-seed capture:

```
(a) partial-depth stream    seen 884   matched 884 (100.0%)   failed 0   unverifiable 0   => GREEN
(b) REST re-snapshot        seen   4   matched   4 (100.0%)   failed 0   unverifiable 0   => GREEN
U/u continuity              888/888 events satisfy U == prev_u + 1
```

`unverifiable` is 0 here because this capture is complete. On the 15 s slice above
it is **1**, for the cut reason given, and the accounting still closes.

`seen == matched + failed + unverifiable` closes in every run, and a fourth outcome
cannot hide inside a pass.

## Headline 2 — `U`/`u` bracketing catches **none** of the book-correctness mutants

This is the sentence M5's brief opens with. Four implementations were replayed
against the same trace: one honest, three deliberately broken.

| implementation | oracle (a) | oracle (b) | `U`/`u` alone |
| --- | --- | --- | --- |
| honest control | **GREEN** | GREEN | GREEN |
| `qty: 0` not treated as removal | **RED (884)** | RED (4) | **GREEN** |
| bid and ask sides swapped | **RED (884)** | RED (4) | **GREEN** |
| book bounded to the rendered depth, cannot refill | **RED (722)** | RED (3) | **GREEN** |
| *no truncation to the rendered depth* | *GREEN — a **no-op** here, see below* | *GREEN* | GREEN |

**`U`/`u` scored 0 out of 3.** It is not a book oracle and must never be quoted as
one. What it *is* good at is the other thing entirely: in capture 4, where the
socket was deliberately dropped mid-capture, it caught the gap on the first event
after the reconnect — `prev_u = 99076647376`, next `U = 99076649580`, **2,204
updates missed**, one break in 899 events. So the honest characterisation is:

> `U`/`u` detects **transport** loss, exactly and cheaply. It says nothing whatever
> about whether the book those messages built is correct. At Binance those are two
> different questions and only the partial-depth stream answers the second.

## Headline 3 — the oracle immediately caught a defect nobody had designed as a mutant

Captures 1 and 2 seeded the book from a `limit=100` REST snapshot, which is the
obvious default and the one the brief's `--limit` argument defaults to. Over 90 s
the honest client scored **734/891 (82.4%)** — 157 failures — while
**`U`/`u` was 890/890 clean throughout**.

The cause, measured rather than guessed:

| REST `limit` | levels | price window covered, per side | IP weight |
| ---: | ---: | ---: | ---: |
| 100 | 100 | **$15.63 – $16.68** | 5 |
| 500 | 500 | $116.64 – $128.12 | 25 |
| 1000 | 1000 | $224.52 – $258.90 | 50 |
| 5000 | 5000 | $911.30 – $1,077.00 | 250 |

**BTCUSDT moved $29.85 in those 90 seconds.** The seeded ask window spanned
$15.47, so the best ask left the seeded window on **406 of 901 ticks**, and every
level above it was one the client had never been told about — a resting order that
predates the snapshot and is never restated is never in the diff stream. Re-seeded
at `limit=1000` the same client scores 884/884.

This is exactly the failure class M4 stage 0 caught at Kraken depth 100: **a
sequence check that is 100% clean while the book is wrong.** It arrived here
unplanned, on the first honest run, which is the best evidence available that the
oracle has real reach.

---

## What the wire costs

Full 90 s local captures (the committed slices are shorter windows cut from these).
Byte counts are of the frame **as sent**, wrapper included; `--verify` confirms all
**5,338 frames re-serialise byte-exactly**, so every figure here is a measurement
and not an estimate.

### Capture 1 — BTCUSDT, `@depth@100ms` + `@depth20@100ms` (the candidate configuration)

| stream | msgs | bytes | mean | share | KiB/s | lvl/msg | **chg/msg** |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `depth20@100ms` | 901 | 1,232,568 | 1,368 | 58.6% | **13.37** | 40.0 | 6.0 |
| `depth@100ms` | 901 | 872,326 | 968 | 41.4% | **9.47** | 26.0 | 22.6 |
| **total** | 1,802 | 2,104,894 | | | **22.84** | | |

Plus 7,208 B of WebSocket frame headers (2 B per frame below 126 B of payload,
4 B above) — 0.34% of payload, exact rather than estimated.

**Headroom: 1.3× against the board's 30.8 KiB/s Anvil load today, 2.5× against the
23.6 h soak's worst measured hour (56 KiB/s).** The diff stream *alone* is
9.47 KiB/s — 3.3× and 5.9× respectively.

**The price of the oracle, as the brief asked for it as a number: `@depth20` costs
+13.37 KiB/s on top of `@depth`'s 9.47 KiB/s — 141% more wire, 2.41× the total.**
It is the *larger* of the two streams, because it re-sends 40 levels every tick
whether or not any moved: 40.0 levels per message of which **6.0 changed**.

### Capture 2 — BTCUSDT, `@depth` + `@depth20` at the 1000 ms tick

| stream | msgs | bytes | mean | share | KiB/s | lvl/msg | chg/msg |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `depth` | 90 | 591,771 | 6,575 | 82.8% | **6.49** | 201.4 | 121.3 |
| `depth20` | 90 | 122,580 | 1,362 | 17.2% | **1.35** | 40.0 | 25.5 |
| **total** | 180 | 714,351 | | | **7.84** | | |

**The slower tick is 66% cheaper** (7.84 vs 22.84 KiB/s) and the saving is almost
all coalescing: 201.4 levels per message against 26.0, i.e. ten 100 ms ticks'
worth of change arrives as ~2.0× the levels, not 10×. Headroom rises to 3.9× / 7.1×.
And `@depth20` costs only **+21%** here rather than +141%, because the partial
payload is a fixed 40 levels while the diff message got ten times bigger.

### Capture 3 — ATOMEUR, the quiet pair, and the silence question

| stream | msgs | bytes | share | KiB/s | lvl/msg | chg/msg |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `depth20@100ms` | 29 | 31,092 | 86.0% | 0.35 | 33.7 | 2.3 |
| `depth@100ms` | 30 | 5,070 | 14.0% | **0.06** | 1.3 | 1.2 |
| **total** | 59 | 36,162 | | **0.40** | | |

**`@depth@100ms` emitted 30 messages in 90 seconds, not 900. The answer to the
silence question is: the stream goes silent — it does not tick on an unchanged
book.** Worst inter-message gap **10,503.8 ms**, p50 895 ms on a stream whose name
says 100 ms. `@depth20@100ms` behaves identically (29 messages, worst gap
10,503.7 ms), so **both** streams are change-driven and neither is a heartbeat.

Headroom here is 76.7× / 139.4×, and `@depth20` costs **+613%** — 7.13× the total —
because the diff stream has almost nothing to say while the partial still re-sends
its 34 levels every time anything moves.

### Capture 4 — the deliberate mid-capture reconnect

Single stream, 900 frames, 1,054,153 B, **10.81 KiB/s**, headroom 2.8× / 5.2×.
Worst gap **5,401.3 ms** — the deliberate 4 s disconnect plus reconnect. One `U`/`u`
break, described in Headline 2. **This capture is deliberately not a clean window**
and must never be used as a guard: it contains the system's own healing event, so
it measures recovery rather than the defect. Captures 1–3 and 5 are the clean ones.

### Inter-message gaps

| capture / stream | count | min | p50 | p90 | p99 | **worst** |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 · `depth@100ms` | 900 | 0.0 | 100.0 | 105.6 | 127.6 | **418.0** |
| 1 · `depth20@100ms` | 900 | 0.0 | 100.0 | 105.6 | 127.6 | **418.0** |
| 2 · `depth` (1000 ms) | 89 | 769.8 | 1000.2 | 1005.6 | 1050.7 | **1,226.9** |
| 3 · `depth@100ms` (quiet) | 29 | 94.6 | 991.2 | 9,064.8 | 10,475.0 | **10,503.7** |
| 4 · `depth@100ms` (reconnect) | 899 | 0.3 | 100.0 | 104.9 | 119.4 | **5,401.3** |

All milliseconds, `perf_counter_ns` clock (declared in every trace header — compare
gaps across traces only when their clocks agree).

### The combined-stream wrapper is worth its bytes

Measured as a constant rather than by comparing two live windows, because two
windows differ in market activity as well as in framing:

```
prefix  {"stream":"btcusdt@depth@100ms","data":     40 B  (42 B for depth20)
cost    3.64% - 3.96% of the bare stream on BTCUSDT
        7.17% on ATOMEUR, where the frames are smaller
```

**Recommendation: use the wrapper.** ~4% of a 22.84 KiB/s stream is under 1 KiB/s,
and the alternative is a second TLS session on an ESP32-S3 — which costs RAM the
board has much less of than bandwidth.

---

## The three oracles, and which one to build

### (a) The venue's own partial-depth stream — **RECOMMENDED**

**What it can see.** The venue's top 20 levels a side, computed venue-side,
compared exactly. It caught all three transferring mutants at 100% of graded ticks
(884/884 for both the `qty: 0` and side-swap mutants) and the bounded-window mutant
at 722/884. It caught the shallow-seed defect nobody designed.

**What it cannot see — stated in the shape M4 stage B2 used for the CRC's top-10
reach.** It is **blind below rank 20**. A client whose 21st level is wrong is
green forever. The board renders 25 rows a side (M4 stage D, `kLevels = 26`), so
**the oracle's reach is narrower than the panel's** — the opposite of Kraken, where
the CRC covers 10 and the panel drew 25, but the same class of gap and it must be
written down rather than discovered. `@depth20` is the deepest partial-depth stream
Binance offers on spot (5/10/20 are the tiers), so this cannot be closed by
subscribing deeper; it can only be closed by (b) or (c).

**Its second cost is bandwidth**: +141% at the 100 ms tick, and it is the majority
of the wire at 58.6%. See the easy-mode question below — this changes what
`@depth20` is *for*.

### (b) REST re-snapshot comparison — measurable, but do not build the detector on it

Exact when the snapshot's `lastUpdateId` equals some event's `u`, ambiguous when it
lands strictly inside a coalesced `[U, u]`. Measured:

| capture | `lastUpdateId` lands exactly on a diff `u` | inside a coalesced bracket | outside the streamed range |
| --- | ---: | ---: | ---: |
| 1 · BTCUSDT 100 ms, limit 100 | **23/23** | 0 | 0 |
| 5 · BTCUSDT 100 ms, limit 1000 | **5/5** | 0 | 0 |
| 2 · BTCUSDT **1000 ms**, limit 100 | 7/9 | **2/9** | 0 |
| 4 · reconnect, limit 100 | 10/10 | 0 | 0 |
| 3 · ATOMEUR quiet, limit 100 | 4/5 | 0 | **1/5** |

So the ids line up far better than the documentation implies — **but the ambiguity
the brief predicted is real and it is a function of the tick**: at the 1000 ms
cadence 22% of snapshots land inside a coalesced event and cannot be graded, while
at 100 ms none do. On a quiet pair a snapshot can be *ahead* of anything streamed.

**And the argument the brief asked to be made explicitly rather than left implicit:
at Binance the REST re-snapshot is the venue's *healing* event and it is scheduled
rather than incidental.** A client that re-snapshots is repairing itself; a detector
built on that same fetch is measuring recovery, not the defect. Unlike a Kraken
resync this cannot be dodged by choosing a clean window, because a client that
never re-snapshots is a client that never recovers. **That is an argument for (a)
over (b), and it is the deciding one** — (a) rides a stream the client never acts
on, so observing it changes nothing.

One further practical trap, found by getting it wrong: **a REST body must be graded
against the instant it names, not the live book.** The round trip measured
**~1,003 ms at `limit=100` and ~1,481 ms at `limit=1000`** from this box, so by the
time the body is in hand the stream has moved 10–15 events past its `lastUpdateId`.
The first version of `binance_oracle.py` compared against the live book, scored 0/4,
and reported GREEN against every mutant while grading nothing. That is what the
`VACUOUS` verdict now exists to name.

### (c) A second independent implementation — keep, but not as the primary

`tools/binance_oracle.py` is half of it already: a Python reference book that can be
byte-diffed against the C++ adapter over the same trace, the shape Anvil already
runs. **What it proves:** the two implementations agree. **What it cannot prove, and
this is the whole reason (a) is the recommendation:** it cannot catch a *shared*
misreading of the wire. If both books treat `qty: 0` as "size zero" rather than
"level gone", they agree perfectly, forever, and are both wrong — and that is
precisely the mutant class (a) catches 884/884.

### The mutation clause, and the one mutant that did not transfer

ARCHITECTURE §9 (2026-08-16): *whatever the oracle is, prove the trace catches a
deliberately broken implementation before pinning it.* Done, and wired into `ctest`
as `binance_oracle_mutants` so it is a build product rather than an evening's claim.
The check is itself mutation-verified — making the oracle grade nothing, making it
unable to fail, and silently un-mutating a mutant are each caught.

**The Kraken mutant list did not transfer intact, and the failure is recorded rather
than papered over.** *No truncation to the rendered depth* is a **no-op at this
venue**: it scored GREEN, identically to the honest control, on every capture. The
reason is structural — Kraken publishes a top-N book whose deltas never explicitly
remove a level pushed out of the window, so a non-truncating client accumulates
stale levels that wrongly re-enter. **Binance's `@depth` is the whole book and every
removal is explicit (`qty: 0`)**, so a client that never truncates is not wrong.

Its Binance-shaped equivalent is the opposite defect and it is now the third mutant:
a book *bounded* to the rendered depth cannot refill, because a stream that only
reports changes never restates the levels rising into view. `--check` runs the
no-op mutant anyway and **asserts that it stays a no-op**, because a mutant that
silently stopped mutating is how a suite keeps its green while losing its reach.

---

## Section 7 — the numbers, proven the way B1 proved Kraken's

### Precision is uniform, and it is **not** the declared tick size

| symbol | `PRICE_FILTER.tickSize` | `LOT_SIZE.stepSize` | price decimals on the wire | qty decimals on the wire |
| --- | --- | --- | ---: | ---: |
| BTCUSDT | `0.01000000` (2 dp) | `0.00001000` (5 dp) | **8** | **8** |
| ATOMEUR | `0.00100000` (3 dp) | `0.01000000` (2 dp) | **8** | **8** |

Counted over **202,012 level entries across the five 90 s captures: 202,012 prices
and 202,012 quantities carry exactly 8 fractional digits. Exponent notation: 0.
Bare JSON floats: 0.**

**So the adapter must NOT derive its scale from `tickSize`.** The wire is
fixed-point at 8 decimals regardless of the symbol's declared tick, and a scale
taken from `tickSize` would be wrong by 10^6 on BTCUSDT prices. `tickSize` and
`stepSize` remain useful for the §6 *verified representable* check — is this value a
whole multiple of the declared tick — but the **scale is 8, uniformly**.

Integer ticks are safe here with room to spare: BTCUSDT's `78564.00000000` scales to
7,856,400,000,000, which is 43 bits. Invariant #3 needs no new path at this venue,
and the adapter needs no float and no `Decimal`.

### The REST `limit` recommendation, against `kMaxSnapshotLevels = 256`

The board's ceiling is 256 levels per side. Replaying the deep-seed capture with the
maintained book bounded to N levels (which is what a fixed-capacity board *is*):

| bounded window | graded ticks matched | failed |
| ---: | ---: | ---: |
| 20 | 8 | 876 |
| 25 | 162 | 722 |
| 50 | 268 | 616 |
| 100 | 425 | 459 |
| 200 | 676 | 208 |
| **256** | **851** | **33** |
| 500 | 884 | **0** |
| 1000 | 884 | 0 |
| unbounded | 884 | 0 |

**`kMaxSnapshotLevels = 256` is not enough on BTCUSDT — it still failed 33 of 884
ticks inside 90 seconds.** The recommendation is therefore in two parts, and the
second matters more than the first:

1. **Use `limit=1000`** for the seed (IP weight 50 against a 6,000/min budget, and
   the 5,000 tier at weight 250 buys nothing this needs). It covers ~$240 of price
   movement per side against 256 levels' ~$40.
2. **Depth alone cannot fix this, so the board must re-snapshot on a schedule.** No
   fixed window survives indefinitely: price walks out of any of them, and a resting
   order that predates the seed and is never restated never enters the diff stream.
   M5 should set the re-snapshot interval from the measured walk rate (~$30 per 90 s
   on BTCUSDT on this evening's tape) against the chosen window, and treat the
   interval as a tunable with a measured basis rather than a constant.

Kraken's offered-depth whitelist is pinned by `static_assert`; the same treatment is
available here for the `limit` tiers and costs nothing.

---

## Section 6 — two protocol facts that reach the firmware. Recorded, not fixed.

### The server pings, it is answered, and there is now a trace that proves it

**Confirmed present in committed captures for the first time in this repository.**
Binance sent a WebSocket PING every ~20 s in every capture: **4, 4, 5, 4 and 4 pings**
across the five 90 s windows, and 11 of them survive into the committed slices
(`pings=5, 3, 1, 2` and one 15 s slice with none, which is expected at a 20 s cadence).

The pings carry a **13-byte payload** — a millisecond epoch timestamp
(`1787602466354`) — which matters because a pong must echo the payload back and an
implementation that replies with an empty pong would look correct against any venue
that sends empty pings. **`tools/wsclient.py` answered correctly**: measured pong
latency **0.065–0.141 ms**, payload echoed byte-for-byte (verified separately by a
loopback server that sends `dc-ping` and checks what comes back).

For the firmware side, read from source and **not modified**:
`WsTransport::on_ping` (`firmware/src/ws_transport.cpp:909`) writes a masked pong
carrying the ping's payload straight back, from inside the parser callback, so it
handles two pings in one read where a single *pong owed* flag would not. **The path
was already right; what did not exist was evidence.** It now does — B2's third
blind-spot class (*a corpus that has never contained a frame kind*) is closed for
this frame kind.

The trace format could not previously hold a control frame at all — a ping payload
is arbitrary bytes, not JSON. That is one of the two motivations for the `kind`
record shape below.

### The 24-hour scheduled disconnect

A connection to the stream host is valid for 24 hours and is then closed by the
venue. **M3's soak was 23.6 h and would have missed it by twenty-four minutes.**
Not reproduced this evening — a 90 s capture cannot — and recorded rather than
tested. The supervisor already reconnects, so this is probably benign, but it is the
first *scheduled* disconnect this project has met and **M5's soak must exceed 24 h or
it proves nothing about it.**

---

## The record shape for a REST fetch — **proposed, and the reader deliberately not built**

`@depth` is a diff stream and is ungradeable in replay without the snapshot it is
bracketed against, so the snapshot has to be in the file or the file is not ground
truth. The shape `capture_binance.py` writes, for M5 to accept or replace:

```
a venue text frame  {"rx_ns": N, "frame": <verbatim JSON>}
a frame we sent     {"rx_ns": N, "dir": "tx", "frame": <verbatim JSON>}
a REST fetch        {"rx_ns": N, "kind": "rest", "req": {...}, "frame": <body>}
a control frame     {"rx_ns": N, "kind": "control", "ctl": {...}, "frame": null}
```

One optional key, `kind`, before `frame`, exactly where `dir` already sits. **An
absent `kind` means what every existing record already is**, which is what keeps the
four Anvil traces and the six Kraken ones byte-identical — the same additivity rule
the `venue` tag was granted at M4 stage A. `tracefile.read_capture` skips kinded
records unless a caller opts in, so a tool written before Binance existed sees
exactly what it always saw.

**Where the pressure runs, stated rather than hidden.** M4 stage A's rule was *the
reader learns the wire; the wire does not learn the reader.* **There is no wire here
to learn.** A REST body is not something the venue said — it is *a fetch this client
chose to make*, and its meaning is inseparable from the request. So the record
carries `req` as well, and that is genuinely new: every other line in every other
trace is a transcript, and this one is a transcript plus a question.

Rejected alternatives: a second stream (two files that must be read together are two
files that can be separated), and synthesising the snapshot into the metadata header
(**already refused at M4 stage A** and not re-proposed).

**The reader change is named and not made.** `dc_replay` cannot read a Binance trace
today — there is no `binance` row in `harness/include/dc_harness/venue.hpp` and no
decoder, and adding either is a C++ change M5 stage 0 was told not to make. That is
the same position Kraken traces were in after M4 stage 0, and it closed at stage A.

Two `rx_ns` subtleties that a reader must know, both deliberate:

1. **A REST record's `rx_ns` is its position in the stream, not when the body
   landed.** The fetch runs on a worker thread so it cannot punch a 1 s hole in the
   gap distribution, and the record is stamped when the main loop writes it, because
   `rx_ns` is what orders the file and `TraceReader` rejects the first backwards
   step. The true span is in `req.sent_ns` / `req.recv_ns`.
2. **The record therefore lands ~1–1.5 s *after* the instant it describes.** A
   window cut at the record loses the diffs that bridge it forward — the oracle went
   from 884/884 green to 250/250 red on a slice cut that way. **Reconcile by id,
   never by position**, which is exactly what the venue's own buffered procedure
   says. `slice_trace.py --require-baseline` is a guard that the window *contains* a
   baseline, deliberately not an instruction to start at one.

The documented bracketing was satisfiable **on the first attempt**, with no retry:
snapshot `lastUpdateId = 99076734902`, first surviving event `[U=99076734903,
u=99076734994]`, so `U <= L+1 <= u` held immediately.

---

## Where the wire disagreed with the brief

1. **The brief says the server "pings every 20 s and closes if no pong arrives
   within 60 s".** The 20 s cadence is confirmed. The 60 s deadline was not tested
   and cannot be from a 90 s capture that always answers.
2. **The brief's mutant list does not transfer.** *No truncation to the rendered
   depth* is a no-op at Binance, for the structural reason given above. Replaced,
   with the original kept and asserted to stay a no-op.
3. **`limit=100` is not a safe default**, though it is the obvious one and the
   cheapest tier. It is wrong within 90 seconds on BTCUSDT.
4. **The precision does not follow `tickSize`.** The brief's section 7 asks whether
   every wire price carries "exactly the declared precision"; it does not — it
   carries 8 decimals uniformly, on both a 2 dp and a 3 dp tick.
5. **`@depth20` is the *larger* stream**, not a cheap addition: 58.6% of the wire at
   the 100 ms tick, and 7.13× the diff stream on a quiet pair.
6. **Both streams go silent**, not just `@depth`. The brief asks the silence question
   only of `@depth`; `@depth20@100ms` is equally change-driven.
7. **A latent bug in the shared WebSocket client**, found by this venue and fixed:
   see below.

---

## What this evening found in our own code

**`tools/wsclient.py` could not survive a quiet second on a TLS socket.** `_fill`
caught `socket.timeout` but not `ssl.SSLWantReadError`, which is what a TLS socket
raises when the read deadline passes with a **partially received TLS record** in
hand. `capture_*.py` reports that as `ssl.SSLError` and treats it as the end of the
stream. Capture 2 died after **4 frames** with

```
connection ended: The operation did not complete (read) (_ssl.c:2624)
```

and after the one-line fix took **180**. Anvil summarises every 500 ms and Kraken
heartbeats at 1 Hz, so on both venues a 1.0 s poll almost always completes inside
one record and the path was never exercised — ARCHITECTURE §9's never-observed
class, on the shared client, found by the first venue with a 1000 ms cadence and a
genuinely quiet pair. On capture 3 (ATOMEUR, 10 s gaps) it would have ended the
capture almost immediately.

**Both existing capture tools were proved byte-identical across every change** to
`wsclient.py` and `tracefile.py`, by the loopback-replay method `wsclient.py`'s own
header documents: a throwaway stdlib WS server replays a committed trace at the
tool, and the pre- and post-change outputs are diffed with only `rx_ns` and
`captured_at` normalised. `capture_anvil.py` identical over **1,514 lines**,
`capture_kraken.py` over **1,601** — including runs with a server PING injected
mid-stream. Separately, `anvil_frame_economics.py`, `kraken_frame_economics.py`,
`gap_stats.py` and both selfchecks were run over all eleven committed traces before
and after: **1,011 lines of output, identical.**

---

## The tools are pinned, as they were for Kraken

`binance_tool_selfcheck` recomputes 50 figures over the five committed slices;
`binance_oracle_mutants` re-runs the honest control and all four mutants. Both fail
loudly in the normal build loop. A moved number means either a tooling regression or
a deliberate re-capture — and in the second case the expectations and this document's
figures move in the same commit. Never the third thing.

## Committed slices

| file | window | what it is for |
| --- | ---: | --- |
| `binance_btcusdt_deepseed_20260824.ndjson` | 25 s | **the oracle golden** — `limit=1000` seed, honest GREEN 235/235, all mutants RED |
| `binance_btcusdt_deepseed2_20260824.ndjson` | 25 s | **the second, independent oracle witness** — a later window the same evening; honest GREEN 235/235, all mutants RED |
| `binance_atomeur_deepseed_20260824.ndjson` | 47 s | the quiet-pair witness, for **exactness only** — honest GREEN 8/8; the bounded-window mutant is `NOT EXERCISABLE` here and the tool says so out loud |
| `binance_btcusdt_d100ms_20260824.ndjson` | 15 s | the candidate configuration, both streams, `limit=100` |
| `binance_btcusdt_d1000ms_20260824.ndjson` | 60 s | the 1000 ms tick and its coalescing |
| `binance_atomeur_d100ms_20260824.ndjson` | 88 s | the quiet pair and the silence finding |
| `binance_btcusdt_reconnect_20260824.ndjson` | 60 s | the deliberate reconnect and the one `U`/`u` break. **Not a clean window; never use as a guard.** |

| `binance_btcusdt_DEFECT_silent_stream_20260826.ndjson` | 50 s | **NOT A GOLDEN AND NOT A WITNESS — a defect fixture.** See below. |

Full captures stay untracked in `_local/`.

### The defect fixture, and why it is named in capitals

`binance_btcusdt_DEFECT_silent_stream_20260826.ndjson` is a live capture taken 2026-08-26,
one connect, with the stream name in the URL path **misspelled by one character**
(`depth@100mss`). The venue accepted the socket (HTTP 101), answered our pongs, sent **three
pings** on the normal ~20 s cadence, served a **successful REST seed** from a different
host, and delivered **zero depth frames**. Four records: one `rest`, three `ping`.

**Replayed, it draws a populated, coloured, LIVE ladder over a feed that has never spoken.**
It is the first artefact in this repository that can produce invariant #5's forbidden output
**on demand** — M3's frozen ladder was real but incidental, and this one is staged.

It is **excluded on purpose** from `binance_adapter_oracle`, `binance_tool_selfcheck` and
`binance_oracle_mutants`: there is no book to grade and no wire to price, and a VACUOUS
verdict reads like a pass. It **is** in the taxonomy pin and read by `dc_replay`, because an
unpinned committed trace is a failure and not a skip whatever it is for.

**It pins a defect, not a contract, and it is expected to invert at C.** The expiry clause
lives in `test_binance_adapter.cpp`'s silent-stream case and in DESIGN strain 26: when C
lands a remedy the test must fail, the correct response is to invert it rather than delete
or relax it, and if C ships without inverting it the fixture moves to whichever stage next
touches liveness rather than lapsing.

**It earned its keep on its first run**, by exposing a defect shipped two commits earlier:
stage A moved the statistics pass to `event_ns` and changed one of the **two** liveness
clocks. The replay driver's — the one that decides when the panel greys — was still reading
`rx_ns`, and nothing caught it because at Anvil and Kraken the two stamps are equal. A
capture whose three pings share one `rx_ns` is what separated them.

---

## Addendum — M5 stage A, 2026-08-25: what the reader found when it was built

Everything above was measured by the Python tools. This section is what the **C++ reader**
found when it was given the same seven committed slices, and it is where this document and
the stage-A brief disagree. Same rule as the header: **where these notes and a brief
disagree, the notes win** — they were measured.

### The proposed record shape is accepted, with one correction the wire made

[§ The record shape for a REST fetch](#the-record-shape-for-a-rest-fetch--proposed-and-the-reader-deliberately-not-built)
proposed the `kind` discriminator and M5 stage A implements it unchanged, in both readers,
with `RecordForm { Frame, Rest, Control }` and an absent key meaning `frame`. All eleven
pre-existing traces are byte-identical through the change and no golden moved.

**The one correction: a `rest` record's `frame` may be `null`.** The first draft of
`TraceReader` required an object, and `capture_binance --selfcheck` refused a trace its own
capture loop had just written — `on_rest` **records** a failed fetch rather than dropping it,
because *the snapshot did not arrive* is a fact about the capture window, and so is a body
this line shape cannot hold. `req.status` and `req.error` say which. Both readers now accept
object-or-null, and both name a bodyless one **`rest:no-body`** rather than folding it into
the fetch count — the same rule, and the same reason, as Kraken's `ack:subscribe REFUSED`.

The reader was wrong and the wire was right, which is the whole reason the reader was built
before the adapter.

### The ping, counted in committed files for the first time

**17 pings survive into the seven committed slices** — 5, 5, 3, 0, 1, 1, 2. The figure of
**11** in [§ The server pings](#the-server-pings-it-is-answered-and-there-is-now-a-trace-that-proves-it)
is the count across the **original five** slices and is unchanged; the two later ones
(`atomeur_deepseed`, `deepseed2`) add 6. **Every one of the 17 carries a `pong_ns`**, which
is the evidence the 2026-08-25 ruling rests on — the path was exercised, not merely present.

Three slices are long enough to measure the cadence, and they reproduce stage 0's figure from
the committed files independently, through the C++ reader:

| slice | pings | median interval |
| --- | ---: | ---: |
| `binance_atomeur_d100ms` | 5 | 19,951.7 ms |
| `binance_atomeur_deepseed` | 5 | 20,011.6 ms |
| `binance_btcusdt_d1000ms` | 3 | 20,013.3 ms |
| `binance_btcusdt_reconnect` | 2 | 20,054.2 ms |

against the **19,970 ms** median over 23 intervals recorded above. The four are within
0.1–0.4% of it.

**These medians are only obtainable from `ctl.recv_ns`.** A ping's record is written when the
main loop next flushes, so `binance_atomeur_deepseed`'s three pings share **one** `rx_ns` —
they were flushed together after a 28.5 s silence. `TraceRecord::event_ns` is the field that
separates the two (ARCHITECTURE §9, 2026-08-25).

### The ~80 s threshold is not the number the code produces

`liveness_clock.hpp` clamps at `kThresholdCeilingMs = 30,000 ms`, so `4 × 19,970 = 79,880`
caps to **30 s**. And `kMinSamples = 8` intervals is **~160 s of wall clock** at this cadence,
which **no committed slice reaches** — the longest is 88 s carrying 5 pings, i.e. 4 intervals
— so all seven replay on the *uncalibrated* default, which is also 30,000 ms. Calibrated and
uncalibrated coincide here, so `liveness_firings = 0` on every slice and nothing misbehaves;
what is inert is the self-calibration itself. **Owned by C**, with the multiplier. Full
reasoning in ARCHITECTURE §9.

This is the one figure in this document that a longer capture would change, and it is a
second reason B2's soak matters: **no artefact in this repository is long enough to watch
this venue's liveness clock calibrate.**

### The pre-`48ba299` trailing snapshot: one of the seven lost one

`48ba299` fixed `capture_binance` losing an in-flight REST fetch at the end of a capture; the
seven slices predate it. A fetch is launched only when a **message** arrives at or after
`last send + snapshot_every_s`, and the old `finally` slept a fixed 0.3 s against a ~1.0–1.5 s
round trip. So the question is answerable from the artefacts, and the answer is:

| slice | verdict |
| --- | --- |
| `binance_atomeur_d100ms` | **LOST ONE.** Last fetch sent at +71.60 s, next due at +86.60 s, and a message arrived at +87.90 s — the last record in the file. The fetch fired and its record never landed. |
| `binance_atomeur_deepseed` | No. The capture's last message is at +18.50 s and the next fetch was not due until +20.00 s, so none was ever launched. The file ends with the `finally` drain writing two queued pings at +46.97 s. |
| the other five | Not applicable. Each is a **window that ends well before its capture's last record**, so a fetch lost at the capture's end falls outside the slice entirely. Each capture also ended with less than one fetch interval elapsed since its last send. |

Both ATOMEUR files are the whole capture rather than a window — their `_local/` copies are
byte-identical to the committed ones — which is exactly why they are the only two exposed.

**It costs nothing.** `binance_atomeur_d100ms` is the quiet-pair-and-silence witness, not an
oracle golden; both deep-seed goldens are unaffected, and their `U`/`u` bracketing and 235/235
GREEN results do not involve the missing record. **This adds no reason to re-capture** — that
is B2's, with the rest of the pin work.

---

## Addendum — M5 stage B1, 2026-08-25: the adapter, and what it measured

Stage A's addendum is what the C++ *reader* found. This is what the C++ *adapter*
found, and it is where this document and the B1 brief disagree. Same rule: **the notes
win**, because they were measured.

### The grammar is still a strict subset — re-measured, not inherited

Stage 0 measured Binance's wire over a capture window. Deliverable 1 asks for the claim
to be re-taken over the **seven committed slices**, because a grammar claim that was true
of one window is not automatically true of the venue. Every verbatim frame and every REST
body:

| | |
| --- | ---: |
| string escapes | **0** |
| exponent characters outside a string | **0** |
| bare `.` outside a string (a float token would appear here) | **0** |
| `true` / `false` / `null` literals | **0** |
| maximum container nesting | **4** |
| price / quantity entries | **188,372**, every one exactly 8 decimals, 0 in exponent notation, widest integer part 6 digits |

So the subset holds and **no third scanner was written**. Kraken's was lifted unchanged
into `engine/include/depthcharge/json_scan.hpp` and Binance reuses it — **with no venue
flag**, which was the brief's stop-and-raise condition. Anvil's was the wrong one to
reuse and the reason is worth keeping: it is deliberately bug-compatible with nlohmann
3.11.3's float handling, a specification this venue has no use for.

The 188,372 here and stage 0's 202,012 are different corpora — committed slices against
full captures — and agree on every proportion.

### The tick filters were verified before being declared

The scale is a **venue constant of 8 decimals** and `tickSize` is a **validator**, which
inverts what the same field meant at Kraken. A validator that rejects valid frames is
worse than none, so the declared filters were checked against the corpus first: the GCD
of every price and every quantity IS the declared filter.

| symbol | entries | price GCD | = tick | qty GCD | = step |
| --- | ---: | ---: | ---: | ---: | ---: |
| BTCUSDT | 91,665 | 1,000,000 | 0.01 | 1,000 | 0.00001 |
| ATOMEUR | 2,521 | 100,000 | 0.001 | 1,000,000 | 0.01 |

### How deep the book must be is a property of the market, not of the venue

`binance_oracle.py --window-sweep` bounds a correct client's maintained book and grades
it against `@depth20`. **The two deep-seed witnesses disagree by a factor of five**, on
the same pair, the same day, an hour apart:

| window | `deepseed` | `deepseed2` |
| ---: | --- | --- |
| 100 | 21/235 | **235/235 GREEN** |
| 256 | 202/235 (33 fail) | 235/235 |
| 500 | **235/235 GREEN** | 235/235 |
| 1,000 | 235/235 | 235/235 |

A book bounded at `kMaxSnapshotLevels` (256) is clean on one witness and fails 33 graded
ticks on the other — the *bounded window cannot refill* mutant occurring **by accident
rather than by design**. 500 is a 2.4% margin over a number that moved 5× between two
adjacent windows. The only size that is not a bet is one that holds the whole seed, so
the adapter's ladder is **1,024**.

**The cost is stated rather than buried**: 32 KiB of ladder, 32 KiB of staging frame and
32 KiB of pre-seed buffer — **~96 KiB of fixed, never-allocated state** against the
~144 KiB the whole Anvil image uses today. Invariant #7 holds (nothing is allocated);
where it lives on the board is **D's** decision.

### The pre-seed buffer, sized from measurement

The venue's documented procedure buffers diffs while the REST fetch is in flight. That is
not a nicety: a round trip is ~1.0–1.5 s, so the snapshot names an instant the stream has
already moved 10–15 events past. Measured worst case across the corpus: **15 events
carrying 823 levels (12.9 KiB)**, of which at most 5 events / 537 levels survive the
snapshot. Bounds are 64 events / 2,048 levels, ~2.5×, and an overflow raises
`Gap{Overflow}` rather than passing silently.

Largest single diff measured: **537 levels** in one `b` or `a`, on the 1000 ms tick where
a second of changes coalesces into one message.

### Deliverable 6 — no committed slice crosses, and none touches

Asked of the venue's **own published books** (REST bodies and `@depth20` partials) rather
than of a book we maintained, so the answer is about the venue and not about our
arithmetic. Across all seven slices: **0 crossed, 0 touched.** Tightest spread observed is
one tick — 1,000,000 ticks (0.01) on BTCUSDT and 200,000 (0.002) on ATOMEUR.

The rule therefore remains uncalibrated **for the same reason it did at Kraken** — and
unlike Kraken, the instrument to settle it now exists: a crossed book that grades clean
against `@depth20` is the venue's truth and must be drawn; one that grades RED is our
error. The next observation settles it rather than deferring again.

### Known unknown 4 — `event_ns` reaches REST records, and B2 can stop measuring live

Stage A set a REST record's `event_ns` from `req.recv_ns`, and both `sent_ns` and
`recv_ns` are in the trace. So the round trip is a **committed-file** measurement now,
and it reproduces stage 0's live figures:

| seed | fetches | min | median | max |
| --- | ---: | ---: | ---: | ---: |
| `limit=100` | 21 | 958.0 ms | ~995 ms | 1,059.9 ms |
| `limit=1000` | 5 | 973.0 ms | 1,481.8 ms | 1,539.9 ms |

**B2 does not need a live run to size the re-snapshot schedule.**

> **THESE ARE THE ROUND-TRIP NUMBERS. THE SECTION BELOW HAS A DIFFERENT PAIR AND
> THEY ARE ONE PARAGRAPH APART.** (Marker added M5 stage B2, because the wrong pair
> is the more memorable one.)
>
> `recv_ns − sent_ns` is **how long the venue took to answer** — a property of the
> venue and the path, present on the board, and the only one of the two that may
> appear in the shipped client's margin.
>
> `rx_ns − recv_ns` — the next section — is **how late the capture tool wrote the
> record down**, because it flushes when the next message arrives. **The board
> writes no trace file**, so that number cannot be a term in anything the board
> does. It is a *slicing* constraint and nothing else.
>
> This is the third appearance of §9's oldest drift shape — *a record's `rx_ns` is
> not the instant it describes* (2026-08-07, and again at M5 stage A). The first
> two were caught in code. This one would be caught in a quotation, which is why
> the marker is here rather than in a commit message.

### The REST lag has a two-order-of-magnitude tail, and it is B2's single most important input

**Stage A said a REST record lands "~1–1.5 s after the instant it describes". Measured
across 26 committed records: median 439 ms, MAX 42,721 ms.** The record is written when the
*next message arrives*, so on the quiet pair it can be **forty-two seconds** late.

| | median | max |
| --- | ---: | ---: |
| fetch round trip (`recv_ns - sent_ns`) | 1,008.6 ms | 1,539.9 ms |
| record lag (`rx_ns - recv_ns`) | **439 ms** | **42,721 ms** |

**Why this is the number B2 has to size against, and not the median.** A re-snapshot
schedule chosen on 439 ms is wrong in the tail by two orders of magnitude — and the
consequence is not merely a late figure. **The book is UNBRACKETED for the whole of that
interval**: the fetch has been issued, the snapshot names an instant already receding, and
until the record lands and its first surviving event satisfies `U <= L+1 <= u` there is
nothing tying the maintained book to the venue's. A schedule that assumes a sub-second
turnaround will re-fetch into a window where the previous fetch has not yet been reconciled,
on precisely the pair where the book is thinnest and the walk is hardest to bound.

The tail is a property of the *quiet* pair rather than the busy one, which is the opposite
of where a byte-budget intuition would look for it.

> **THIS PAIR IS FOR CUTTING SLICES. IT IS NOT THE MARGIN.** (Marker added M5 stage
> B2, and the heading above it was wrong about which input it was.)
>
> The heading calls 439 ms / 42,721 ms *B2's single most important input*, and B2
> found it is the input to **section 1 and to nothing else**. `rx_ns − recv_ns` is
> the interval between the venue answering and **the capture tool getting round to
> writing the record down** — it flushes when the next message arrives, so on a
> pair that goes quiet the record can land 42 s after the instant it describes, and
> on a stream that goes *silent* it lands at end of capture. The corpus maximum is
> now **48,969 ms**, from `binance_btcusdt_DEFECT_silent_stream_20260826`, where
> there was no next message at all — which is the mechanism stated as plainly as it
> can be.
>
> **The board writes no trace file.** There is no flush schedule on the board, so
> this quantity does not exist there and cannot be a term in the shipped client's
> re-snapshot margin. Sizing that margin against 42,721 ms would be sizing the
> firmware against `capture_binance.py`.
>
> The paragraph above is still right about everything except whose input it is: the
> book IS unbracketed for the whole of a fetch, and a slice cut without allowing for
> this lag loses the diffs that bring its snapshot forward. Section 1 needs the max.
> The margin needs the section above's round trip — and, as it turns out, not even
> that: see the B2 addendum, *the round trip has no tail worth sizing against*.

### Known unknown 1 — a misspelled stream is a healthy, silent, ping-answering socket

**Probed on the wire, with a control run.** `…/ws/btcusdt@depth@100mss` (one character
wrong) returns **HTTP/1.1 101 Switching Protocols**, holds the socket, sends its server
PING on the normal cadence — answered in 0.107 ms — and delivers **zero frames** in 12 s.
The REST seed is a different host and **succeeds**. The correct-stream control took **121
frames** over the same window.

There is no ack, no error and no close, because this venue names its subscription in the
URL path. **Nothing whatever confirms that the subscription succeeded**, and the
2026-08-25 liveness ruling arms the grey on the ping — which keeps arriving. Full
reasoning and the hand-off are in ARCHITECTURE §9; the short form is that this is
invariant #5's frozen-ladder-that-looks-live arriving through the mechanism installed to
prevent it, and the distinguishing signal is *never-started*, not *went-quiet*.

### Known unknown 2 — the trade ring is Anvil-only, and that is not an M5 question

The brief asks whether a Binance ladder renders trade prints and treats it as possibly a
third venue-shaped hole beside the liveness one. **The framing is wrong and the correction
matters more than the answer: the hole is already there at Kraken and M4 shipped with it.**
Kraken puts trades on a `trade` channel DepthCharge does not subscribe, so
`is_trade("kraken", …)` is always False on a book capture — `tools/tracefile.py` says so in
its own docstring. Binance is identical: `@trade` and `@aggTrade` are separate streams and
nothing subscribes them.

So the accurate statement is that **the trade ring is Anvil-only**, and has been since M4
without being written down. Nothing about it is new at the third venue, nothing was
subscribed to establish it, and nothing needs to be.

**It is therefore not an item on any M5 stage.** It is a §1-versus-reality question for the
owner — whether a terminal whose §1 describes trade prints, on a build where two of three
venues have none, is design or debt — and it is recorded as **ROADMAP backlog D0**.

### The C++ grader's evidential position, stated rather than hedged

`dc_binance_oracle` grades the real adapter and **agrees with `tools/binance_oracle.py` on
every count across the whole corpus** — including the slice that fails, where both report
28 matched and 32 failed. That agreement is worth naming precisely, because the modest
version of it undersells what has been shown:

- `binance_oracle.py` is **mutation-verified**: `binance_oracle_mutants` runs an honest
  control green and three deliberate breakages red, in the normal build loop.
- The C++ grader tracks that instrument exactly, on seven files, in both directions —
  green where it is green and **red where it is red, by the identical count**.

So the grader **has been shown red on a real failure**, not merely green on healthy data.
What it lacks is a *planted* one: a `--mutant` mode of its own. The three mutants were run
against it by hand at B1 (qty-0-not-a-removal, sides-swapped, book-bounded-at-256 — each
takes 235 matched to 0), and automating them wants a second link configuration, which is
the `dc_tests_streaming` shape and a stage of its own.

---

## Addendum — M5 stage B2, 2026-08-26: the seed, the walk, and the schedule

B1's addendum is what the adapter found. This is what the **schedule** found, and the
first thing it found is that the instrument B1 built for this stage measures a different
quantity from the one its own comment describes. Same rule as always: **the notes win**,
because they were measured.

### The ruling's evidence is in the repository, and it was committed WHOLE

`binance_btcusdt_mixed{1,2}_20260825` are now committed — byte-for-byte the captures the
2026-08-25 audit-stream ruling was signed on, not windows cut out of them. Both reproduce
the ruling's figures exactly, in two implementations:

| | partial payloads | `lastUpdateId == some diff u` | oracle (a), honest | `U`/`u` |
| --- | ---: | ---: | --- | --- |
| `mixed1` | 90 | **90 (100.0%)** | **88/88 GREEN** | 888/888 clean |
| `mixed2` | 90 | **90 (100.0%)** | **88/88 GREEN** | 890/890 clean |

`dc_binance_oracle` — the real C++ adapter — grades **88 matched / 0 failed / 2
unverifiable** on both, the same 88 as `tools/binance_oracle.py`. The 2 are the partial
payloads that arrive before the seed lands, which the Python tool skips rather than
counting; that is the documented `seen` difference and not a disagreement.

**Why whole and not sliced, since every other Binance file is a slice.** The clause being
discharged says the ruling's figures are *"the one set of numbers in this table with no
committed trace behind them"*. Cut a 40 s window out of each and the numbers that come back
are 40/40 and 38/38 — which evidence a weaker claim than the one the ruling was taken on,
so the row would have to be amended to fit the artefact rather than the artefact supplied
to satisfy the row. **A guard trace pins a BEHAVIOUR and should be cut to the smallest
window that exercises it** (M4 stage 0's capture recipe). **A ruling's evidence pins a
DECISION and cannot be cut at all**, because the figure is a rate over a complete capture.
Two categories, two slicing rules; the corpus had never carried the second. Recorded in
ARCHITECTURE §9 and in the taxonomy rows for these two files, so a future stage looking for
a mixed-cadence *guard* trace cuts a new one from `_local/` rather than re-cutting these.

The cost was priced before it was accepted rather than after: **gzip −9 makes the two
1,905,142 B and 1,426,098 B captures 246,677 B and 178,652 B**, so committing them whole
costs the pack about 425 KB against about 199 KB for 40 s slices. A quarter of a megabyte,
in a tree that already carries `anvil_101_baseline_20260809` at 10.4 MB raw. And the ≤900 KB
"convention" was not one: both ATOMEUR files are already byte-identical to their `_local`
originals, which was checked rather than assumed.

They also survive their own writer's contract: `slice_trace.py --mode baseline --window 120
--require-baseline` accepts both and emits a **byte-identical** file, so committing them
whole is not a way of dodging the guard that exists because a window cut *at* a baseline
record took the oracle from 884/884 green to 250/250 red. **And the mixed-cadence
coincidence question does not arise:** nothing was cut, which is the answer and the reason.

Both were mutation-verified **before** being pinned — honest GREEN, 3/3 mutants exercised
and RED, the asserted no-op still a no-op — and both joined `binance_oracle_mutants`. That
closes a gap in the mutant suite nobody had named: **every previous witness runs `@depth20`
at 100 ms, and the board runs it at 1000 ms.** Until this stage, no mutant had ever been run
at the cadence the shipped configuration will actually use.

### B1's low-water mark is not the seeded-window edge, and the difference is the whole trigger

B1 built `min_bid_levels` / `min_ask_levels` and described them as showing the seeded-window
edge — *when the market walks far enough that the rendered window approaches the edge of
what the seed contained, this is where it shows*. The description names the right quantity.
The counter is a different one, and on the two committed captures where the failure class
actually happens it reads perfect health:

| capture (`limit=100`) | low-water HELD | low-water COVERED | oracle |
| --- | ---: | ---: | --- |
| `binance_btcusdt_d1000ms_20260824` | 100 / 100 | **0** / 103 | RED (28/32) |
| `binance_btcusdt_reconnect_20260824` | 100 / 100 | **0** / 63 | VACUOUS |

**A count of levels HELD cannot fall**, because every diff that retires a level near the
touch arrives alongside others adding prices the seed never contained. It bottoms out at the
seed depth and stays there. A trigger on it is a trigger that never fires.

What erodes is **coverage**. A `/api/v3/depth` body is a complete picture of one price
*range* — from the touch down to its worst bid, up to its worst ask. Inside that range the
book stays complete for ever, because every later change arrives as a diff. Outside it the
client is permanently ignorant: a resting order that predates the seed and is never restated
never enters the diff stream. So

```
seeded coverage (bids) = held bids at px >= the seed's worst bid
seeded coverage (asks) = held asks at px <= the seed's worst ask
```

and when that falls below the emitted depth, the ladder is drawing rows from the region
nobody ever told us about. On the reconnect capture the bid side goes from 100 to **0 in
1.2 seconds** — the best bid fell $21.99 while the seed's floor was $15.68 below it — which
is **faster than a single REST round trip**. Both counters are kept and `dc_ladder` prints
them one line apart, because deleting a counter whose reading turned out to mean something
else is how the next reader repeats the mistake.

Coverage falls for two reasons and both are real losses of knowledge: the touch walking
toward a seeded boundary, and the 1,024-slot ladder evicting covered levels to make room for
better ones. The second is a storage bound rather than a market fact and `levels_evicted`
is beside it on the same report.

### Sizing the margin — and the round trip has no tail worth sizing against

The brief asked for `margin ≥ walk rate × p99 fetch latency`. **The p99 is not the right
instrument and the corpus does not need to supply one.** Measured over every committed REST
record after this stage — 36 of them, every one carrying both `req.sent_ns` and
`req.recv_ns`:

| tier | n | min | median | max | max/median |
| --- | ---: | ---: | ---: | ---: | ---: |
| `limit=100` | 23 | 958.0 ms | **1,009.4 ms** | 1,063.0 ms | **1.053** |
| `limit=1000` | 13 | 973.0 ms | **1,503.1 ms** | 1,590.7 ms | **1.058** |

Two tiers, ~5% spread both times. Choosing the max over the median moves the margin by
87.6 ms — $0.029 of BTCUSDT walk against a $224.52 seeded window, **0.013% of it**. So the
finding is not a percentile: **the round trip has no tail worth sizing against on this
path.**

**AND EVERY ONE OF THOSE 36 IS A DESK-BOX MEASUREMENT.** Wired ethernet, CPython, urllib.
The board is an ESP32-S3 on Wi-Fi doing TLS, and M4 stage B3 measured DNS failures on that
platform at a flat **14,000 ms**. A desk figure must never stand as the board's, which is
exactly the trap the 439 ms figure set one section earlier. So the margin is sized against a
**deadline the transport imposes**, not against a latency anybody measured:

```
margin  >=  walk rate  x  T          T = kBinanceFetchDeadlineMs = 15,000 ms
```

A fetch that exceeds T is abandoned and retried. That covers **100% of fetches by
construction** rather than 99% of a sample of 36, and it converts an unbounded unbracketed
window into a bounded one — where invariant #5 draws its line, and the same move B1's
remedies (a) and (b) both make. **T is a required property of the transport**; the adapter
has no clock and issues no fetch. Recorded in ARCHITECTURE §9; implementing it is C/D's.

**Why 15 s.** `tools/capture_binance.py` already runs this exact fetch on a 15 s cadence
behind `REST_DRAIN_TIMEOUT_S = 20`, so it is a value with an implementation behind it rather
than a fresh guess — and it survives the sanity check against the window it protects. At
BTCUSDT's measured $0.33/s walk, `limit=1000`'s $224.52 is ~677 s of walking and 15 s is
**2.2%** of it; `limit=100`'s $15.63 is ~47 s and 15 s is **32%**. That is an independent
argument for `limit=1000` arriving from the schedule rather than from the depth sweep.

**The walk rate, in the units the trigger counts.** Worst loss of seeded coverage over any
window of 15 s or less, across all nine BTCUSDT and ATOMEUR captures:

| window | worst coverage lost |
| ---: | ---: |
| 1.5 s | 154 levels |
| 5 s | 154 |
| **15 s** | **168** |
| 30 s | 168 |

It is **burst-dominated, not smooth**: the single worst event is one 100 ms tick in
`deepseed` that took the best bid down $15.99 and removed **135 covered levels at once**, so
a mean rate would understate it by an order of magnitude and the max is the honest term.

```
margin    = 192 levels    (168 measured, +14% because 168 is a max over nine captures
                           on two pairs on two days — a sample, not a distribution, in
                           the same market whose depth requirement moved 5x in an hour)
threshold = kBinanceEmitDepth + margin = 256 + 192 = 448 covered levels
```

The floor is the **emitted** depth rather than the panel's 25 rows, because 256 is what the
engine holds and a window policy chooses among them; sizing to 25 would be sizing to today's
window policy.

**What it costs when it is wrong, in each direction.**

* **Too eager** is wire and IP weight — 5 / 25 / 50 / 250 by `limit` tier, 50 for the
  shipped seed, against a 6,000/min budget — and the venue **bans on breach**. The adapter
  latches once per seed epoch and cannot do more, because it has no clock; **bounding the
  seeds is the transport's**, and is recorded beside T as the second required property.
* **Too late** is the 82.4% failure class stage 0 measured at `limit=100`: a clean `U`/`u`
  sequence over a book that is wrong, with nothing in the client saying so.

**And the trigger cannot rescue a seed that never satisfied its own margin.** A 100-level
seed is below the 448 threshold on arrival, and re-fetching would return the identical
shortfall at 50 weight a time — so the adapter does not arm the trigger for that epoch, it
counts `seeds_below_margin` and reports the coverage collapsing anyway. On BTCUSDT at
`limit=100` that fires **on arrival**, about a minute before the book actually goes wrong,
which is the 82.4% failure caught at the seed rather than at the ladder. On ATOMEUR it fires
because the venue's whole book is 16 bids deep and nothing is wrong at all — the adapter
cannot tell those apart, because it does not know what `limit` was asked for, **and it does
not need to: the response is the same either way.** Whoever holds the request can tell.

Measured across the corpus, with the trigger at 448:

| capture | seed | low-water coverage | trigger |
| --- | --- | ---: | --- |
| `btcusdt_deepseed` | 1000 | 888 / 923 | armed, never fired |
| `btcusdt_deepseed2` | 1000 | 981 / 922 | armed, never fired |
| `btcusdt_mixed1` | 1000 | 834 / 811 | armed, never fired |
| `btcusdt_mixed2` | 1000 | 771 / 973 | armed, never fired |
| `atomeur_deepseed` | 1000 | 14 / 304 | not armed (venue's book is 16 deep) |
| `btcusdt_d100ms` | 100 | 97 / 99 | not armed |
| `btcusdt_d1000ms` | 100 | **0** / 103 | not armed |
| `btcusdt_reconnect` | 100 | **0** / 63 | not armed |
| `atomeur_d100ms` | 100 | 12 / 100 | not armed |

**No committed capture exercises the firing path, and that is a fact about the corpus rather
than a gap in it**: every `limit=1000` seed stays at 771 or better and every `limit=100` seed
is below its margin on arrival. The crossing is synthesised in `test_binance_adapter.cpp`,
which is §9's 2026-08-18 rule as written — where the code and every available file agree,
synthesise the input that discriminates. Three mutants were applied and each was caught.

### A re-snapshot on a live book: measured, not adopted

B1 discarded these in silence; B2 counts them, and measures the thing D needs before it can
choose a re-seed mechanism. Adopting a body wholesale rewinds the book to the instant it
names. Rolling it forward instead needs the diffs it is behind by — and buffering those for
the whole of a 15 s deadline is ~150 events / ~8,200 levels, **about 128 KiB**, against the
pre-seed buffer's measured worst case of 15 events / 823 levels. So the three candidates are
a 128 KiB buffer, a `Gap` and a grey flash, or a live-book merge; the choice costs board
memory (**D's**) and a rendered state (**C's**), and *the board's re-seed behaviour is D*.

Adopting is **loss-free** whenever the body is not older than the book, and that is measured
two ways — against the stream at the record's position in the file, and against the stream at
`req.recv_ns`, which is the board's position. **Both agree on every capture**, so the answer
is not an artefact of the capture tool's flush schedule:

| | fetches on a live book | loss-free to adopt | worst deficit |
| --- | ---: | ---: | ---: |
| `limit=1000`, BTCUSDT | 7 | **0** | 783 update-ids |
| `limit=100`, any pair | 18 | **18** | 0 |
| `limit=1000`, ATOMEUR | 1 | **1** | 0 |

**And the discriminator is not the round trip.** Locating `lastUpdateId` between the stream
at `sent_ns` (0.0) and at `recv_ns` (1.0):

| tier | position of the snapshot's instant within the round trip |
| --- | --- |
| `limit=100` | **1.00** on every fetch — the body names the instant it was received |
| `limit=1000` | **0.21 – 0.86**, median ~0.76 |

So **the deeper the seed, the older it is when it lands**: at `limit=1000` the venue
snapshots the book roughly three quarters of the way through the round trip and spends the
rest serialising and shipping ~120 KB. That is a cost of `limit=1000` nobody had priced, it
is why the pre-seed buffer is not optional, and it says a `limit=1000` re-snapshot is a
statement about an instant ~370 ms in the past — never about now.

### What a scheduled re-snapshot does to every capture this project takes

M4 stage 0 left a paragraph for whoever met a **scheduled** healing event. This is it.

At Anvil and Kraken a healing event is an *incident*: a socket drops, a resync arrives, and
a capture window that contains none is the normal case — which is why the recipe reads
*liquid pair, shallowest depth, no healing event in the committed window*. **At Binance the
re-snapshot IS the healing event**, and once the client re-seeds on its own trigger the
healing event becomes **constant rather than incidental**.

Three consequences, and they are not hypothetical — the corpus already shows them, because
`capture_binance.py --snapshot-every` has been producing scheduled re-snapshots since stage 0
and every Binance row in the taxonomy reads `resyncs = the REST fetch count`:

1. **A Binance capture with no healing event in the window stops existing** at any depth
   where the trigger can fire. It can still be *made* to exist, by fetching once at connect
   and never again — `--snapshot-every 0`, which is exactly how this stage's own calibration
   capture was taken — but that is a client behaving differently from the shipped one.
2. **So a Binance guard trace is one of two things, and it has to say which.** Either it is
   captured with the trigger disabled and is a trace of a client that does not re-seed — in
   which case the healing path is not covered by it and something else must cover that — or
   it contains re-seeds and cannot also be the control for a defect that a re-seed would
   heal. There is no third file that is both.
3. **The fidelity cost of option one is real and belongs here rather than in a footnote.** A
   trace taken with the re-snapshot trigger disabled is a trace of a book that was allowed to
   decay past the point the shipped client would have acted — which is precisely what
   `binance_btcusdt_d1000ms` and `binance_btcusdt_reconnect` are, and precisely why they
   grade RED and VACUOUS. Those two are *useful* as decay witnesses and are **not** witnesses
   for anything the shipped client does. The corpus should keep saying so.

The recipe therefore gains a line at this venue: **state the seed depth and the re-snapshot
cadence in the filename or the taxonomy row**, because at Binance they are not capture
settings, they are the client under test.

### Section 5 — `age_ms` at this venue, measured through a throttled replay

The Kraken twin settled queue-versus-shed by measurement rather than carrying an assumption
into M5. Same move here, and the answer is **two answers**, which is the part the hypothesis
did not have.

`dc_age_probe` replays a committed trace, injects a backlog it therefore knows exactly —
which is what `anvil_freshness_probe.py` needed two sockets to obtain — and reports what the
real `AgeEstimator` makes of it. At 25% of the broadcast rate, throttled only after the
baseline has latched on clean stream:

| trace | socket backlog | feed backlog |
| --- | --- | --- |
| `binance_atomeur_d100ms_liveness_20260826` | **TRACKS** — 1,797.1 s read against 1,796.8 s injected, 1.00× | **BLIND** — 0.3 s peak through the same 1,797 s |
| `anvil_101_baseline` (control) | TRACKS — 382.4 s against a 384.4 s window ceiling, 0.99× | BLIND — 0.5 s |
| `kraken_btcusd_d25_20260816` (control) | TRACKS — 765.2 s against a 768.1 s ceiling, 1.00× | BLIND — 0.1 s |

**The hypothesis is CONFIRMED, and its scope was wider than the thing it is true of.** §9's
stage-B1 row predicted `age_ms` reads approximately zero through an arbitrarily large
backlog at Binance. It does — through a **feed** backlog. It does not through a **socket**
backlog, and the reason is the mechanism the row itself names: the ping is a WebSocket
control frame on the same TCP stream as the depth frames, so a socket that cannot be drained
delays it by exactly the same amount and the deficit appears normally.

So the correct statement is narrower and more useful than "the meter is broken here":

* **A socket backlog** — this client or the path to it cannot drain the wire. Physical at all
  three venues, and the meter reads it at all three.
* **A feed backlog** — the venue's publisher falls behind, or the subscription stops, while
  the WebSocket layer keeps its 20 s timer. **Physical at Binance and NOT CONSTRUCTIBLE at
  Anvil or Kraken**, where the subsystem that emits the liveness record is the subsystem that
  emits the book. The meter is blind to it everywhere; Binance is the first venue where
  "everywhere" includes a state the venue can actually be in.

The Anvil and Kraken columns are in the pin as controls, not as findings — a probe that only
ever printed BLIND would be a probe that had stopped working. Their socket figures land at
0.99× and 1.00× of the **documented window ceiling** rather than of the injected lag, and the
ceiling is computed from `age_estimator.hpp`'s own formula: that header's worked table says
384 s at Anvil and 768 s at Kraken for a 25% drain, and the probe independently reads 382.4 s
and 765.2 s. Binance's 20 s cadence puts its ceiling at 15,332 s, far above the injected lag
— the one thing the slow ping buys.

Mutation-verified: replacing the baseline term in the deficit with the rolling elapsed term —
the exact mistake `age_estimator.hpp`'s header argues at length against — turns all three
socket columns BLIND and `--check` red.

**AND A FINDING THAT ARRIVES BEFORE EITHER MODE DOES, WHICH NOTHING HAD NAMED.** The age
baseline latches on the `kBaselineSamples` = 32nd interval. At Anvil that is **16 s** and at
Kraken **32 s**. At Binance's 19,964 ms ping cadence it is **639 s after the first ping, so
about eleven minutes after connect** — during which the panel's age meter reads `-`, not
because anything is wrong but because the mechanism has not started. That is a 20–40×
regression of the same shape the ruling already accepted for grey latency, and it had not
been written down anywhere. **C's**, with the threshold work.

The probe's arrival cadence is measured from the committed trace; its **length** is
synthesised at that measured median, because no capture reaches 639 s and taking an
eleven-minute one to watch a metronome would be paying for a number the cadence already
gives. The report says how many arrivals were real and how many were not, on the same line as
the verdict.

### Two captures the later stages could not proceed without

**`binance_atomeur_d100ms_liveness_20260826`** — 221 s, quiet pair, single-stream, one
opening seed at `limit=100`, `snapshot_every_s: 0`, total IP weight **5**. 20,604 bytes.
**11 pings, 10 intervals**, against `kMinSamples`' 8 — so this is the first committed trace
at any venue that lets the host suite **enter the self-calibrated liveness branch at
Binance**, which C has to tune. Before it, every slice ran on `kUncalibratedThresholdMs` and
the branch was dead code as far as ctest was concerned.

Ping intervals: 19,957.0 / 20,057.6 / 19,963.3 / 19,974.7 / 20,062.8 / 19,956.9 / 19,964.0 /
20,068.0 / 19,977.5 / 19,962.6 ms. Median **19,964.0**, worst/median **1.005** — tighter than
the 1.01× stage A measured over four, and the tightest cadence of the three venues by a
distance.

**What it calibrates to is inert, and that is now asserted rather than observed.** 4 ×
19,964.0 = 79,856 ms clamps to `kThresholdCeilingMs` = 30,000, which is the identical number
`kUncalibratedThresholdMs` already held — so calibrating changes the threshold by exactly
zero. `test_binance_adapter.cpp` asserts the inertness in the form that C's change will
break: raise the ceiling or drop the multiplier below ~1.5 and the test goes red and has to
be rewritten deliberately. A test that merely reported 30,000 ms would stay green through
either change and say nothing. `ReplayResult::liveness_calibrated` exists for the same
reason — at this venue `threshold_ms` **cannot** distinguish calibrated from uncalibrated,
so a test asserting on it would be a test that cannot fail.

It also carries a **26,807.8 ms** book silence with the ping keeping 19,957–20,068 ms time
straight through it: the Binance twin of Kraken's MINA/GBP 25,843 ms hole, and the same
verdict — market information, not the book's age, and never a grey signal.

**`req.sent_ns` — answered, and then guarded.** Present on **36 of 36** committed REST
records along with `req.recv_ns`, so nothing was owed and nothing was fixed. What was added
is a corpus-wide assertion in `binance_frame_economics.py --selfcheck`: a REST record missing
either stamp fails the build, because a fetch schedule that cannot be replayed is a fetch
schedule that cannot be covered. It is an assertion rather than a pinned column deliberately
— a `rest_stamped` figure would mean editing all eleven existing rows, and that table's first
rule is add rows only.

### Found at review: the cadence figures in this file came from the wrong median

**`sample_window.hpp` says the median convention "matters enough to have one home" — a
lower median by nearest rank, because an interpolated one invents an interval that never
occurred on the wire. `harness/src/trace.cpp`'s statistics pass carries a second copy and
interpolates.** `LivenessClock` and `AgeEstimator` use the shared rule; `dc_replay`'s
report line does not, and it is `dc_replay`'s number that has been quoted.

| trace | intervals | the clock (`lower_median`) | the report (interpolated) |
| --- | ---: | ---: | ---: |
| `anvil_101_baseline` | 180 | 500.1 ms | 500.1 ms |
| `kraken_*` (all four) | 59–95 | — agree to 0.1 ms — | |
| `binance_atomeur_d100ms_20260824` | 4 | 19,947.7 | **19,951.7** |
| `binance_atomeur_deepseed_20260824` | 4 | 19,962.8 | **20,011.6** |
| `binance_btcusdt_d1000ms_20260824` | 2 | 19,973.9 | **20,013.3** |
| `binance_btcusdt_DEFECT_silent_stream_20260826` | 2 | 19,950.6 | **20,004.8** |
| `binance_atomeur_d100ms_liveness_20260826` | 10 | **19,964.0** | 19,969.4 |

The right-hand column is what `taxonomy_pins.inc`'s Binance comment quotes
(*19,951.7 / 20,011.6 / 20,013.3*) and what B1's session log quotes for the silent-stream
fixture (*20,004.8*). **So the cadence recorded for this venue is not the cadence the clock
on the board computes.** At Anvil and Kraken the two agree, because those cadences are flat
— the coincidence class again, and the reason this survived three milestones.

It is ~5 ms at worst and changes no decision. What it costs is the property
`sample_window.hpp` asserts outright: that a C++ figure and a Python figure over the same
trace are comparable, *"which is how several of this project's numbers have been checked"*.
On the report path that is false today.

**Recorded, not fixed — and the load-bearing reason is golden movement, not sweep size.**
Adopting the shared convention rewrites the cadence figures quoted inside
`taxonomy_pins.inc`, and *no existing golden moves* is precisely what makes a seven-commit
split reviewable: bundled into this stage, nothing in the diff would distinguish a convention
change from a defect. **A convention change that moves pins must be its own stage, so that the
moved pins have nothing else in the diff to hide behind.** (The sweep across three NOTES files
is real and is not the argument; it is the consequence.)

**Owner: M5 close-out** — not C, whose evening is already the threshold, the ceiling's changed
role, the panel decisions and strain 26's four unbuilt remedies. **Expiry:** when `harness/`
and `engine/` compute the median by one convention. **Tripwire:** if any stage before the
close-out needs to quote or re-pin a Binance cadence figure, this closes first. *"Its own
scope" is unowned, and commit 2 of this very split exists to close an unowned clause in §9 —
so this one is not left blank.*

Carried in three places, the shape the silent-stream fixture already uses:
`test_binance_adapter.cpp`'s two-conventions case, `taxonomy_pins.inc`'s Binance comment, and
DESIGN strain 29 — each saying that **the correct response is to INVERT the test, not to
delete or relax it**, and that if the close-out ships without inverting it the clause moves to
whichever stage next touches a cadence figure rather than lapsing. ARCHITECTURE §9,
2026-08-26.

**How it was found, which is the part worth keeping.** `dc_age_probe` was written with its
own interpolated median and quoted 19,969.4 ms as this venue's cadence into five documents
and one test — and the test passed, because `Approx(19969.4).epsilon(0.001)` is a relative
tolerance spanning 19.97 ms and therefore accepts 19,964.0 as well. **A tolerance wide
enough to accept both candidate answers is a test that cannot fail.** Every figure in the
B2 addendum above now comes from `lower_median`.
