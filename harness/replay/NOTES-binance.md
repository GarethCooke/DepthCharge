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

Full captures stay untracked in `_local/`.
