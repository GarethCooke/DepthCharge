# Anvil replay traces — observations (M0)

Ground-truth notes taken while capturing the M0 traces from the **live** Anvil
demo (`wss://anvil.garethcooke.com/ws`, wire version 1) on 2026-07-23. These
answer the M0 brief's known-unknowns and record what the deployed server
actually does, versus what the vendored protocol (`docs/vendor/anvil-protocol.md`)
says.

**Observation window.** The measurements below are drawn from the **full 5-minute**
baseline capture (4658 frames) and the full reconnect capture (1836 frames), both
kept locally under `_local/` (git-ignored). The **committed** slices are 90 s
windows cut from those (see [Committed traces](#committed-traces)); their smaller
counts are what the goldens in `harness/tests/dc_tests.cpp` pin.

---

## Headline: the wire `seq` is non-monotonic in a single ticker's stream

This is the single most important finding for the adapter work (M1).

Every frame carries a `seq`, but it is **one global engine-thread counter across
all tickers and all frame types** (protocol §1), values ~2.44 × 10⁸. A socket
subscribed to one ticker therefore sees a *sparse* subsequence — and, critically,
**a non-monotonic one**:

- Full 5-min baseline (4658 frames): **42 backward `seq` steps**, i.e. a frame
  whose `seq` is *lower* than the one before it. Not a one-off — it recurs at
  roughly one per 7 s throughout the window. Committed 90 s slice: 14 backward
  steps (pinned as a golden).
- The backward steps are, by transition kind: `summary → book` ×24,
  `trade → book` ×17, `snapshot → summary` ×1.

Why:
1. **`summary` frames are cross-ticker and broadcast to every socket** (protocol
   §3.5) on a ~2 Hz timer, stamped from the global counter. They interleave with
   ticker-101 frames, so an older-stamped summary can land right after a newer
   book frame (and vice-versa).
2. **`book` frames are coalesced** and published on the ~12 Hz tick carrying the
   `seq` they held at coalesce time. An individually-streamed `trade` that
   arrives just before the coalesced book can carry a *higher* `seq` than the
   book — hence `trade → book` backwards.

Across a reconnect the counter simply **continues** (see below): the resync
snapshot's `seq` is ~current-global, not a reset to a low per-connection value.

### Consequence for the M1 Anvil adapter

Do **not** use the wire `seq` for gap detection. It cannot serve as
`FeedEvent.Seq` (which ARCHITECTURE §4 requires to be "monotonic per (venue,
symbol) stream"). The adapter must **synthesize** its own monotonic `Seq`
(e.g. a local counter incremented per emitted `FeedEvent`). This is safe because
v1 `snapshot`/`book` frames are **idempotent full top-N replaces** — there is no
delta to lose — so integrity does not depend on wire-seq continuity. `Gap`
events come from transport signals (disconnect/reconnect, ring overflow), not
from wire-seq gaps.

> This contradicts ARCHITECTURE §1 ("every frame carrying a monotonic `seq`") and
> §4 ("Anvil's frame `seq`" listed as a usable native scheme to normalise). The
> constitution is not edited here (M0 hand-off flags the needed §4 correction for
> the owner).

---

## Committed traces

| File | Frames | Span | snapshot / book / trade / summary | Raw | gzip (git-storage proxy) |
| ---- | -----: | ---: | --------------------------------- | --: | --: |
| `anvil_101_baseline.ndjson`  | 1406 | 89.9 s | 1 / 1088 / 136 / 181 | 8.75 MiB (9,171,782 B) | **181.6 KiB** (185,985 B) |
| `anvil_101_reconnect.ndjson` | 1288 | 89.8 s | 1 / 1012 / 103 / 172 | 8.00 MiB (8,389,733 B) | **167.7 KiB** (171,717 B) |
| `anvil_101_baseline_20260809.ndjson` | 1513 | 90.0 s | 1 / 1210 / 121 / 181 | 9.96 MiB (10,438,331 B) | **189.9 KiB** (194,461 B) |
| `anvil_101_feederoff_20260817.ndjson` | 1753 | 119.9 s | 1 / 1511 / **0** / 241 | 200.0 KiB (204,782 B) | **12.7 KiB** (13,021 B) |

The third is M3's re-measurement of the same thing the first one measures, seventeen
days and one bench bring-up later; it is added rather than substituted, because the M1
goldens pin the M0 trace and a re-capture that silently replaced it would move them. See the
[M3 addendum](#m3-addendum--the-watchdog-threshold-re-measured-and-left-alone).

`gzip -c <file> | wc -c` is recorded because it is the realistic proxy for what
git actually stores after zlib+delta — ~540 KiB for all three traces combined,
versus ~27 MiB raw. The book frames are ~8 KB and highly repetitive, so they compress
~50×. **This sets the trace-commit policy before Kraken traces arrive at M4:**
capture long locally, commit a compact sliced window, quote the gzip size.

### Trace naming and dating — binding for every committed trace, all venues

Added 2026-08-16 (M4 stage 0). This section **documents existing practice rather
than imposing a new rule**, and that was checked before it was written: every
dated trace in this directory already agrees with its own `captured_at`.

**A date in a trace filename is the UTC date of its `captured_at`, never the desk's
local date.** `anvil_101_baseline_20260809` was captured `2026-08-09T18:03Z`,
`anvil_101_depth27_20260816` at `2026-08-16T16:34Z`, and the four Kraken slices at
`2026-08-16T23:37Z`. The two M0 traces carry no date at all and are unaffected.

**The convention held by luck until it was written down.** Both dated Anvil
captures were taken in the afternoon, when the UK local date and the UTC date
coincide, so nothing had ever tested it. The Kraken captures are the first taken
across midnight UTC — 23:37Z is 00:37 BST *the following day* — and were briefly
named `_20260817` from the desk clock, which would have made a file disagree with
the header inside it. Fixed before commit; recorded here because the next capture
after 23:00 BST will face the same fork and should not have to re-derive the
answer.

Scope, stated so it is not over-read: this binds **trace files in this directory**.
Serial-monitor logs (`device-monitor-260815-002728.log`) and bench records
(`hardware/bench-2026-08-16-*.md`) are desk-local by nature — a serial monitor
stamps the wall clock in front of the owner — and are deliberately left alone.

### THE TRACE METADATA CONTRACT — binding for every trace, all venues, both languages

Added 2026-08-17 (M4 stage A). **This is the prose statement of a rule that has
three implementations** — `harness/include/dc_harness/venue.hpp` (C++ table),
`harness/src/trace.cpp` (C++ enforcement) and `tools/tracefile.py` (Python) — and
it is written here because it is the thing they must all agree with. Where a code
comment and this section disagree, this section is what was decided;
`ARCHITECTURE.md` §9 (2026-08-17) is what was decided *and why*.

**Line 1 of a trace is a JSON object. Three fields are required of every venue:**

| field | meaning |
| --- | --- |
| `captured_at` | ISO-8601 UTC, and the source of the filename's date (above) |
| `url` | the endpoint, verbatim, including any query parameters that shaped the stream |
| `tool_version` | the capture tool's version |

**`venue` is optional, and an absent tag reads as `anvil`.** That is the whole of
what makes the tag additive: the four Anvil traces committed before 2026-08-17
carry no `venue` key and did not move by a byte when it was introduced. A tag
naming a venue the build does not know is a **different failure** from a
malformed header — `UnknownVenueError` in C++, a distinct `ValueError` message in
Python, exit code 2 from `dc_replay` and `dc_taxonomy` — because "this file is
broken" and "this build cannot read this file" want different reactions and only
the first is a bug.

**What identifies the instrument is venue-conditional**, because it does not
generalise and requiring both of everyone would make one capture tool lie:

| venue | also required | also carried |
| --- | --- | --- |
| `anvil` | `ticker` (integer id) | `capture_mode`, `cycles`, `origin_sent`, `origin_note`, `handshake_status` |
| `kraken` | `symbol` (string pair, e.g. `BTC/USD`) | `depth`, `subscribe` (the frame verbatim), `capture_mode`, `cycles`, `handshake_status` |

**`clock` names which monotonic clock stamped `rx_ns`, and is never inferred.**
`monotonic_ns` at Anvil, `perf_counter_ns` at Kraken. An absent `clock` reads
back as **`undeclared`**, not as the clock the venue's tool happens to use —
that inference is sound today and is exactly the kind of sound inference that
stops being true without anyone noticing. Both capture tools declare it from
2026-08-17, so `undeclared` means "captured before then". **Do not compare gap
distributions across two traces whose clocks differ**: on this box `monotonic_ns`
steps at 15.0 ms and `perf_counter_ns` at 0.0002 ms, and Kraken's p50 book gap
is 0.1 ms, so the coarse clock would report a distribution made of its own
quantisation. The Anvil tool keeps the coarse clock deliberately, so a new Anvil
capture stays comparable with the committed ones that every cadence figure in
this repository is measured against.

**Record lines** are `{"rx_ns": <int>, "frame": {…}}`, with one optional key:
`"dir": "tx"` marks a record **this side sent** (`capture_kraken.py`'s subscribe).
Received records carry no `dir` at all, so the Anvil line shape is untouched. A
`tx` record is counted as a record and **excluded from every timing figure** —
our own upload is not the venue's traffic, and the interval between the subscribe
and the first reply is not an inter-message gap.

**Whether a frame must carry a string `type` is venue-conditional too.** This
narrows ARCHITECTURE §9's 2026-08-07 rule rather than abandoning it: Anvil frames
are held to exactly the rule they were always held to, and a frame that lost its
`type` still fails there. Kraken's do not carry one universally — its heartbeat
is `{"channel":"heartbeat"}`, its subscribe ack carries `method`, and 61 of the
depth-25 slice's 1,599 records have no `type` at all. The kind of a record is
named by the **venue's decoder** (`harness/include/dc_harness/trace_decoder.hpp`),
not by a field the reader assumes exists.

**One rule for a resync, at every venue: a snapshot with a book event before it
in the trace.** Not "a snapshot that is not the first record", which is Anvil's
shape rather than a general one. The reasoning, and the two venue-specific
repairs that were written and found wrong in opposite directions, is at the rule
itself in `harness/src/trace.cpp`.

**Slicing preserves all of it.** `tools/slice_trace.py` copies line 1 through
verbatim and never re-serialises it, so a committed slice carries the venue tag
and the clock name of the capture it came from. Checked rather than assumed: all
four committed Kraken slices re-slice **byte-identical** to the committed files.

Full local captures (git-ignored, `_local/`): baseline 4658 frames / 300 s /
~30 MB; reconnect 1836 frames / ~124 s / ~15 MB; the 2026-08-09 baseline 20,418
frames / 1,199.9 s / ~128 MB, plus `drain-120ms.ndjson` (1,992 frames / 239.9 s),
the deliberately throttled socket the M3 addendum's control uses.

---

## M4 stage A addendum (2026-08-17) — what an IDLE Anvil emits, and the clause in the vendored protocol that is false

Captured for the 2026-08-17 staleness ruling, which makes Anvil's `summary` frame
the liveness signal the panel's grey state is armed on. That ruling rests on a
claim about idle behaviour, and until now **no committed trace contained a single
idle second**: every Anvil capture in this directory was taken against a running
synthetic feeder, so the state "the server is healthy and nothing is happening"
had never been recorded.

**The capture.** `anvil_101_feederoff_20260817.ndjson` — a **local** server
(`build-msvc`, MSVC 19.44, Boost 1.86, Crow 1.2.1), `ANVIL_FEEDER=0` so the feeder
thread is constructed and never started, `ANVIL_TICKERS=101,102,103`,
`ANVIL_SUMMARY_HZ=2`, one client at `?ticker=101&depth=27`, **120 s**. No order
exists at any point: every book is empty from the first frame to the last. Full
capture untracked in `_local/`; the committed slice is the whole 120 s window
(13.0 KiB gzipped, so there was nothing to gain by cutting it).

**It is a local server, not the deployed one, and that is a real limit on what
follows.** The cadence constants are the same defaults, but a VPS under load is
not a desk. Treat the numbers as *what this code does when idle*, which is the
question the ruling asks, and not as a measurement of production.

### What it measures

| kind | count | rate | inter-arrival |
| --- | ---: | ---: | --- |
| `summary` | 241 | **2.008 /s** | min 125.0 · **p50 500.0** · p90 515.0 · **max 531.0 ms** |
| `book` | 1,512 | **12.597 /s** | min 46.0 · p50 78.0 · p90 140.0 · max 157.0 ms |
| `snapshot` | 1 | on connect | — |
| `trade` | **0** | — | the only committed trace with none |

**The ruling's load-bearing measurement, confirmed and now reproducible from a
committed file: `summary` fires at 2 Hz on an empty queue.** All 241 frames are
**byte-identical apart from `seq`** — one distinct payload, `restingBuy` and
`restingSell` zero and `last` empty on all three tickers — with `seq` advancing
895 → 6,280. It is a deadline, not a reaction to order flow: nothing in this
capture could have provoked it, because nothing happened.

### The thing nobody had measured: an idle Anvil also emits `book`, at ~12.6/s

1,511 `book` frames over an empty book, **byte-identical apart from `seq`**
(`{"type":"book","seq":…,"ticker":101,"bids":[],"asks":[]}`), 0 of them carrying a
level. The mechanism is certain from Anvil's source rather than inferred:

- `EngineHarness::run()` (`server/engine_harness.hpp:171`) checks a **book
  deadline** (`coalesce`, 70 ms ≈ 14 Hz) and a **summary deadline**
  (`summary_period`, 500 ms) on every loop iteration, and both are on the **same
  engine thread**.
- `publish_books()` (`:240`) stamps `snap->seq = ++seq_` **unconditionally**,
  whether or not a single level moved.
- `Broadcaster::emit_books()` (`server/broadcaster.hpp:187`) does contain the
  skip — `if (snap->seq == ls) continue;` — but because the publisher has just
  minted a fresh `seq`, **that skip can never fire on a live server.** It is
  reachable only if the engine thread stops publishing.

Two consequences worth carrying:

**1. `docs/vendor/anvil-protocol.md` §4 is false twice over, not once.** The
clause reads *"a genuinely idle book produces no frames"*. A genuinely idle book
produced **1,753 frames in 120 seconds** on this build.

**RESOLVED UPSTREAM the same day, and the sequence is the point.** The report was
raised against the **pre-refresh copy, `04db612`**, and was recorded only in
DepthCharge's own header block — the snapshot body was deliberately left untouched,
because a vendored file that has been edited stops being a record of what the other
side said. Anvil then corrected the clause at the source (`25ade0e`), and
`docs/vendor/anvil-protocol.md` is re-pinned at **`4801ed8`**. §3 now says the
server sends no WebSocket ping and no dedicated heartbeat *frame*, but does send
*"the data stream itself, on fixed timers that do not depend on order flow … Both
continue on a completely idle book"* — which makes the 12.6 book-frames/s above a
contract statement rather than an observation.

**The rule that closes this class of error, and it is why the finding could be
retired at all: a finding against a vendored file must name the SHA it was found
in.** A pinned copy is a moving target across re-pins, so "the protocol says X" is
not a durable claim — "`04db612` says X" is, and it is the only form that can be
checked as fixed later. This is §9's standing discipline about quoted claims
(2026-08-16 eve: a claim gets recomputed when it is quoted) applied to the vendor
boundary. §9 item 8 is closed with the SHA.

**2. A note for Anvil's A2b, offered rather than asserted.** A2b's
publish-starvation hypothesis reasons that *"`emit_books` deliberately skips a
ticker whose seq has not moved, so the wire goes silent while the connection
stays perfectly healthy."* The skip exists but is dead code against a running
publisher, so an unchanged book is **not** a route to silence. The hypothesis is
not weakened by this — it is *strengthened and simplified*, because both deadlines
live on one thread: if that thread stalls, `book` **and** `summary` stop together,
which is exactly the total-silence signature A2b describes and exactly why the
2026-08-17 ruling can say both internal hypotheses would silence `summary` too.

### What this trace does NOT prove — read before citing it

It was captured to be *the Anvil twin of the MINA/GBP case* and **it is not one**,
and the reason is the finding above rather than a defect in the capture.

At Kraken the quiet pair's book goes **silent for 25,843 ms** and the 1 Hz
heartbeat is what keeps the panel honest — book silence and liveness genuinely
separate, which is the whole reason the ruling exists. At Anvil, on an idle book,
**there is no book silence to rescue**: `book` frames arrive at 12.6/s, the worst
inter-arrival is 157 ms, and the panel stays coloured because book events keep
coming — not because a liveness signal saved it. `dc_ladder` over this trace
reports `watchdog 1000 ms -> 0 stale episode(s)` and renders `no book · ● LIVE`,
which is the right answer and is **not** the new rule being exercised.

So **the state "book age growing while liveness holds" still has no Anvil
golden**, and a feeder-off capture structurally cannot provide one. The Anvil
shape of that state is not an idle server, it is a **backlogged socket**: Anvil
queues and never drops, so a slow consumer receives every frame late and age grows
while frames keep arriving at a healthy-looking rate. `_local/drain-120ms.ndjson`
(the M3 addendum's deliberately throttled socket, 1,992 frames / 239.9 s) is the
existing candidate. The obstacle is that age is not measurable from one trace —
it needs a reference clock, which is what `tools/anvil_freshness_probe.py` gets by
matching wire `seq` across a throttled and an unthrottled socket. Naming it here
so the next session does not re-capture an idle server expecting a different
answer.

### The `summary` distribution, and the field that used to hold a number

Measured 2026-08-17 across every committed Anvil trace plus this idle capture —
**1,191 intervals**:

| trace | n | median | p90 | p99 | max | worst/median |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `anvil_101_baseline` | 181 | 500.1 | 506.0 | 598.5 | **968.8** | **1.937x** |
| `anvil_101_baseline_20260809` | 181 | 500.0 | 516.0 | 547.0 | 547.0 | 1.094x |
| `anvil_101_depth27_20260816` | 181 | 500.0 | 516.0 | 578.0 | 593.0 | 1.186x |
| `anvil_101_feederoff_20260817` | 241 | 500.0 | 515.0 | 516.0 | 531.0 | 1.062x |
| `anvil_101_reconnect` | 172 | 499.9 | 506.4 | 1020.9 | *4747.7* | *9.498x* |

The median is **500.0 ms on every trace**, which is what a fixed deadline looks
like. The reconnect row is italicised because its 4,747.7 ms is the capture's
deliberate socket drop — the fault case, not a healthy sample.

**The number that sizes the whole rule is `anvil_101_baseline`'s 968.8 ms —
1.937x the median, i.e. ONE MISSED TICK, in otherwise healthy M0 data.** It is
why `kThresholdMultiple` is 4 and not 2: a threshold at 2x would grey the panel
every time a single `summary` publish slipped, and this trace proves that
happens. Kraken's heartbeat, by contrast, never exceeded 1.119x over 834
intervals at two hours of day — so Anvil is the binding case and Kraken inherits
Anvil's margin for free.

**`venue_traits` no longer has a field that can hold a duration.**
`stale_gap_ms` became `liveness_signal`, a **name** — `"summary"` here,
`"heartbeat"` at Kraken — because the 2026-08-17 ruling is that no threshold on
book silence can be correct at any venue, and a `double` in that row is an
invitation to write one. The threshold is derived at runtime from this
distribution's own rolling median (`liveness_clock.hpp`), because
`ANVIL_SUMMARY_HZ` is operator config on a server DepthCharge does not own and
cannot read back. Anvil calibrates to **2,000 ms**, Kraken to **4,000 ms**, from
one dimensionless constant.
### The correction, in the terms the ruling wants it recorded

**The Anvil shape of *age grows while liveness holds* is a BACKLOGGED SOCKET, not
an idle server** — and **no committed trace can contain one, because a backlog is
a property of the client's socket rather than of the wire.**

That is the whole of it, and it is worth stating flatly because the feeder-off
capture was taken to be the Anvil twin of MINA/GBP and is not one. An idle Anvil
emits `book` at 12.6/s, so an idle server produces no book silence to hold colour
through. What Anvil *does* produce is the other shape: it queues and never drops
per socket, so a slow consumer receives every frame late rather than losing any —
111 s of accumulated lag over 150 s, measured by seq-matching a throttled socket
against an unthrottled one (`tools/anvil_freshness_probe.py`, ARCHITECTURE §9
2026-08-11).

**A capture cannot record that.** Two sockets reading the same server at the same
instant disagree about how old the book is, and `rx_ns` records only when *this*
client was handed the bytes. The lag lives in the gap between the server's clock
and ours, and a trace has neither. `_local/drain-120ms.ndjson` is a throttled
capture and still does not contain the answer — it contains the arrivals; the age
had to be computed against a second, unthrottled socket that no file holds.

**This is a KNOWN UNCOVERABLE-BY-CAPTURE CASE**, listed under *Owed by stage B*
in the stage A brief rather than left implied, **and it is why stage A2's
estimator has to be windowed**: the only instrument that can see the case is one
running live against the venue, and a cumulative expected-versus-received count
would read zero deficit through the whole 111 s because nothing was ever missed —
it all arrived, late.
### What it is good for

- **It pins the ruling's premise.** If Anvil's summary cadence ever stops being a
  deadline, this trace's `summary=241` moves and `trace_taxonomy_selfcheck` says
  so on the next build.
- **It is the only committed trace with an empty book end to end**, so it is the
  first thing that exercises the whole chain — parse → adapt → adopt → publish →
  draw — with zero levels on either side. `dc_ladder_feederoff` and
  `dc_replay_feederoff` run it in ctest. The distinction it guards is one the
  panel must get right and no other trace tests: **"no orders exist" is a live,
  correct, empty ladder; "no data arrived" is grey.**
- **It is the only committed trace with `trade=0`**, which is a cheap check that
  nothing in the chain assumes a trade ever happens.

---

## M4 stage A2 addendum (2026-08-17) — what the age meter reads on these files, and the one it cannot read

The estimator is `depthcharge/age_estimator.hpp` (it moved out of `dc_harness/` at M4 stage D, when the firmware began linking it); the definition it computes is **queuing
lag**, not time since the last frame and not time since the book last changed.

### The healthy floor is one interval, and the M1 trace's 0.9 s is real

| trace | baseline latched | worst age | grey episodes |
| --- | ---: | ---: | ---: |
| `anvil_101_baseline` | 500.6 ms | **0.9 s** | 0 |
| `anvil_101_baseline_20260809` | 500.0 ms | 0.5 s | 0 |
| `anvil_101_depth27_20260816` | 500.0 ms | 0.6 s | 0 |
| `anvil_101_feederoff_20260817` | 500.0 ms | 0.5 s | 0 |
| `anvil_101_reconnect` | 499.3 ms | **2.3 s** | 1 |

One interval is the instrument's resolution — between two liveness arrivals the elapsed
term grows and the delivered term does not. The rest of the M1 trace's 0.9 s is Anvil's
occasional slipped `summary` tick, and the interesting part is that **a slip is never
repaid**: the engine's publish deadline is a fixed timer, not a phase-locked schedule, so it
does not run fast afterwards to catch up. The deficit therefore stays in the window until it
ages out of it. Calling that lag is correct rather than generous — from one socket, a late
broadcast and a late delivery are the same observation.

### `_local/drain-120ms.ndjson` — the blind spot, on real bytes

This is the file the triage nominated for stage A2, and what it demonstrates is a **limit**
rather than a capability. Measured through the driver:

- 236 `summary` frames over 239.9 s = **0.98/s, against Anvil's 2.0/s broadcast** — the
  socket received 49% of the stream, so at the end of the run the true lag is near **122 s**;
- the meter latches its baseline at **969 ms** and reports **12.5 s**.

**A 10× under-read, and not a bug.** The capture tool sleeps 120 ms per message from the
*first* message, so the socket was already behind when the baseline was measured — and from
one socket that case is not identifiable at all: *"the venue broadcasts at 2 Hz and I am two
minutes behind"* and *"the venue broadcasts at 0.5 Hz and I am current"* are byte-identical
on the wire. The baseline is legitimate precisely because a **fresh** socket's server-side
queue is empty; a client that is too slow from birth never gets that moment.

Three consequences, all deliberate:

1. **No slice of this file is committed.** A golden here would pin a known-wrong number and
   read as coverage, which is what the triage's item 8 says must not happen. (It is also
   13.5 MB; a 60 s slice at pre-A7 frame sizes is ~4 MB, against a repo whose largest
   committed trace is 200 KiB.)
2. **The limit is pinned as a host test instead** (`test_age_estimator.cpp` drives a
   throttled-from-birth socket and asserts the meter reads zero), so the next reader meets
   it as a property rather than discovering it as a defect.
3. **The baseline is printed beside every age**, because it is the only visible symptom: a
   connection that latches 969 ms where its predecessor latched 500 ms has told a human what
   it cannot work out for itself.

What closes it is the client ping — a pong cannot overtake a backlog in Crow's
per-connection buffer, so a round trip prices the queue directly. Deferred to M6 with the
reason recorded (M4 triage, decision (a)).

---

## Frame kinds and shapes

Four kinds seen — `snapshot`, `book`, `trade`, `summary`. No `error` frame ever
arrived (protocol §3.4: reserved, not emitted by v1). No other kinds.

- **`snapshot`** and **`book`** are the **same shape** (this answers a known
  unknown): `{ "type", "seq", "ticker", "bids":[…], "asks":[…] }`, each level
  `{ "price": <string>, "qty": <number>, "orders": <number> }`. `bids` are
  best-first (highest price first), `asks` best-first (lowest first). `snapshot`
  is the on-connect / resync baseline; `book` is the periodic coalesced refresh —
  both are full top-N replaces, idempotent. **Prices are JSON strings**
  (`"10.012"`) — must be parsed to integer ticks in the adapter, never used as
  floats in book data (invariant #3).
- **Depth is ~84–126 levels/side** (per-side extremes measured across the
  committed baseline slice; varies frame to frame), far deeper than the ~27
  levels DepthCharge renders. The engine/book will window it (ARCHITECTURE §5).
- **`trade`**: `{ "type","seq","ticker","price"(str),"qty"(num),"aggr":"B"|"S",
  "takerId"(str),"makerId"(str),"ts"(num, epoch ms) }`. `price` is the resting
  (maker) price = trade price. Streamed individually, never coalesced.
- **`summary`**: `{ "type","seq","tickers":[ { "ticker","restingBuy",
  "restingSell","last"(str) } … ] }`. Cross-ticker roster for **all 12 tickers**
  (101–112), sent to every socket regardless of subscription. **It carries a
  `seq`** (answers the other known unknown). Not needed for a single-ticker
  ladder; the adapter can ignore it (but must tolerate it in the stream).

---

## Cadence (from the full 5-min baseline)

- Overall ≈ **15.5 frames/s**.
- `book` ≈ **12 /s**, median inter-frame gap ~70 ms → matches the protocol's
  "~10–15 Hz coalesced tick".
- `summary` ≈ **2 /s**, median gap ~500 ms → a steady 2 Hz timer.
- `trade` ≈ **1.5 /s**, **bursty** — median gap 363 ms but max gap 7.2 s;
  event-driven, not periodic.
- **No periodicity longer than 90 s** was visible: the three rates are stationary
  across the whole 5-minute window, and `last` drifts around 10.0 without a
  long-period feeder cycle. The backward-seq phenomenon recurs steadily
  (~1 / 7 s), so it too is a stationary property, not a startup transient.

---

## Reconnect behavior

Captured with a client-initiated reconnect and a **4 s simulated drop**
(`capture_anvil.py --cycles 2 --reconnect-after 60 --reconnect-gap 4`).

- On (re)connect the server sends **exactly one fresh `snapshot`** as the resync
  baseline (protocol §3 handshake), then resumes `book`/`trade`/`summary`.
- In the committed slice this shows as a **mid-stream `snapshot`** (frame index
  382) preceded by a **4.47 s `rx_ns` gap** — the largest gap in the trace. The
  goldens assert `mid_stream_snapshots == 1` and `4000 ms < max_gap < 6000 ms`.
- The resync snapshot's `seq` **continues the global counter**
  (244492842 → 244505216 across ~64 s), it does **not** reset to a low
  per-connection baseline. So protocol §4's "per-connection … starting at the
  snapshot's `seq`, increments by 1" does not describe the deployed server.
- **No `Gap`/`error` frame is emitted** — the disconnect is observable only
  client-side (socket close + time gap). DepthCharge's `Gap{Disconnect}` /
  stale state must be **synthesized transport-side** (firmware net task / adapter),
  never expected on the wire.

---

## Known unknowns — resolved

1. **Origin header on the WS upgrade.** The deployed `GET /ws` **accepts an
   upgrade with _no_ `Origin` header** — handshake `HTTP/1.1 101 Switching
   Protocols`, origin sent = none. Non-browser clients (the firmware) do **not**
   need an `Origin` for this instance. `capture_anvil.py` sends none by default
   and only auto-retries with `https://<host>` if an Origin-less upgrade is ever
   rejected (it never was here). Firmware should still keep the nominated-`Origin`
   fallback ready (ARCHITECTURE §7) in case the server's allowlist policy changes.
2. **`book` vs `snapshot` shape / does `summary` carry `seq`.** `book` and
   `snapshot` are byte-shape-identical (full top-N replace); `summary` **does**
   carry a `seq`. Both settled above.

---

## Other surprises vs the vendored protocol

- **`seq` monotonicity** (the headline) — §1/§4 overstate it.
- **Reconnect does not reset `seq`** — §4's per-connection framing is inaccurate;
  there is one global line and reconnect only re-baselines the client via a fresh
  full snapshot.
- **`GET /api/book?depth=5` returned ~100 levels/side**, not 5 — the `depth`
  query param appeared not to cap the deployed response. DepthCharge does not use
  the REST book (it subscribes to the WS stream), so this is noted only for
  completeness.

---

## M1 addendum — what the adapter needed and the wire did not carry

Added while building the Anvil adapter (M1, 2026-07-26). Measurements are over
both committed slices *and* both full 5-minute local captures.

### There is no tick size or qty step anywhere in the protocol

Searched the vendored protocol and every captured frame: no `tickSize`,
`qtyStep`, `increment` or equivalent, on any frame kind or REST endpoint. Prices
are decimal strings and quantities are whole numbers, and that is all a client is
told. **This is a genuine gap in the venue metadata, not an oversight in the
capture** — flagged rather than guessed, as the M1 brief required.

What was measured instead, as the basis for a *declared* scale:

| Fractional digits in a price string | baseline (full) | reconnect (full) |
| ---: | ---: | ---: |
| 0 (`"10"`) | 414 | 366 |
| 2 | 6,542 | 3,727 |
| 3 | 70,266 | 30,060 |
| 4 | 662,465 | 260,630 |
| **5 or more** | **0** | **0** |

So `price_decimals = 4` (tick 0.0001) represents every price Anvil has ever put
on this wire exactly, and quantities are integers (`qty_step = 1`; largest level
qty seen 512, `MAX_QTY` is 10⁹ per protocol §1). DepthCharge declares that in
`SymbolSpec` and the adapter **verifies** it per price: a 5-decimal price yields
`ParseStatus::BadPrice` and is counted, never rounded. If Anvil ever quotes finer,
the first such frame is loudly dropped instead of silently corrupting the ladder.

*Backlog item for Anvil (not a v1 blocker):* publish tick size / qty step in
`GET /api/health` or a symbols endpoint, so a client can configure itself rather
than being told out of band.

### Inter-frame silence is the only disconnect signal — 1000 ms is the line

The reconnect capture has no marker: just a hole. Measured across **6,494 frames**
of capture:

| | median gap | max healthy gap | the drop |
| --- | ---: | ---: | ---: |
| baseline (4,658 frames, 5 min) | 68.6 ms | **640 ms** | — |
| reconnect (1,836 frames) | 68.7 ms | 542 ms | **4,468 ms** |

The worst healthy silence is 640 ms; nothing else in either capture exceeds
1 s. A **1000 ms RX watchdog** therefore sits 1.6× above the loudest healthy
quiet and 4.5× below the observed drop — a decisive separation, not a tuned
threshold. `ReplayOptions::disconnect_gap_ms` carries it host-side; the M3
firmware net task implements the same number as an RX watchdog beside the real
socket-close callback.

### Two behaviours that look like bugs and are not

- **A `book` frame may carry pre-trade state.** `trade` frames stream
  individually while `book` frames are coalesced on the ~12 Hz tick, so a book
  published just after a print can still show the filled level. The ladder can
  therefore lag a trade it has already flashed, by up to one refresh (~80 ms).
  The streams are independent and phase-1 adopts the latest of each; **no later
  session should try to reconcile them.**
- **`summary` frames arrive on a single-ticker socket.** They are cross-ticker
  and broadcast to everyone (protocol §3.5). The M1 adapter parses, counts and
  **ignores** them (`summary_ignored` in the replay report: 181 baseline, 172
  reconnect). They are the input to the M7 board mode, not the ladder.

---

## M3 addendum — the watchdog threshold, re-measured and left alone

Added 2026-08-09, while chasing a firmware bug that turned out not to be one.
Measurements are from a fresh **20-minute** capture taken at the desk against the
same deployed server (`_local/anvil_101_baseline_20260809.full.ndjson`, 20,418
frames, one connection, no reconnects), plus two controlled experiments with
`tools/anvil_drain_probe.py`. The committed 90 s slice is
`anvil_101_baseline_20260809.ndjson`.

### The question, and why the expected answer was wrong

M3 stage C put the firmware on the bench and the panel greyed roughly every 15–20 s
on a socket that never dropped: `wd_gaps=5` in 90 s with `sock_gaps=0` and
`connects=1`, worst observed silence **2,461 ms** (`hardware/bench-2026-08-09-ws-reconnect.md`).
The board was reading ~6 messages/s where M0 measured 15.5, so the working theory
was that Anvil had slowed, healthy silences had stretched past `kRxWatchdogMs =
1000`, and the M1 derivation had simply been outlived — re-derive the threshold
upward and move on.

**The capture says Anvil did not slow.** It is, if anything, slightly faster and
markedly steadier than it was at M0:

| | M0 (2026-07-23, 5 min) | M3 (2026-08-09, 20 min) |
| --- | ---: | ---: |
| overall rate | 15.5 /s | **17.02 /s** |
| `book` | ~12 /s | 13.44 /s |
| `summary` | 2 /s | 2.00 /s |
| `trade` | ~1.5 /s | 1.58 /s |
| worst healthy gap | 640 ms | **391 ms** |

So the threshold, re-derived from current Anvil cadence the way the M3 brief asked,
would have moved **down**, not up — which would have made the false greys worse.
That is the point at which the premise had to be tested rather than the constant
tuned.

### Gap distribution, 20,418 frames over 1,199.9 s

Percentiles are nearest-rank (`tools/gap_stats.py`), no interpolation. The three
counting rules are the ones §05 of `docs/DESIGN.html` distinguishes — the host
replay driver arms on any frame, the firmware watchdog on a book event.

| counting rule | n | rate/s | p50 | p90 | p99 | p99.9 | max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| any frame (host driver) | 20,418 | 17.02 | 63 | 78 | 141 | 172 | **391** |
| event-producing (book/snapshot/trade) | 18,017 | 15.02 | 63 | 79 | 141 | 172 | **391** |
| book-affecting (book/snapshot) | 16,124 | 13.44 | 78 | 79 | 141 | 188 | **391** |

The M0/M1 finding that **all three rules agree on the worst gap** survives at 3× the
sample size — the 2 Hz summaries still never fill a hole the book frames left. The
ten largest gaps in the whole capture are 391, 343, 281, 266, 266, 265, 250, 235,
235, 234 ms, so the tail is short and there is no second population hiding in it:
1000 ms sits **2.6× above the worst gap in twenty minutes** and 5.3× above p99.9.

A watchdog at 1000 ms opens **zero** stale episodes over the whole capture. That is
now a golden (`test_replay_goldens.cpp`, "2026-08 capture: Anvil's cadence still
clears the watchdog by 6x") rather than a claim in a file, so an actual future
cadence change goes red on the desk instead of grey on the panel.

> **Measurement caveat, recorded because it bounds the numbers above.** This capture's
> `rx_ns` came from a Windows `time.monotonic_ns()` running at the default **15.625 ms**
> timer resolution — the whole capture contains only 35 distinct gap values, all on that
> grid (0, 15, 16, 31, 32, 46, 47, 62, 63, 78 …). M0's capture had sub-millisecond
> resolution, which is why its figures (68.6 ms median, 640.2 ms max) are not round and
> these are. Every number here is therefore ±16 ms, which changes nothing at the scale
> that matters (391 vs 1000 ms) but does mean p50 = 63 ms should be read as "~60–70 ms,
> unchanged from M0" and not as a real shift. To avoid it next time, capture from WSL or
> raise the process timer resolution first.
>
> **Half of that last sentence is wrong, measured 2026-08-16 (M4 stage 0) — see
> [`NOTES-kraken.md`](NOTES-kraken.md).** Raising the process timer resolution does
> **not** work: `timeBeginPeriod(1)` leaves `time.monotonic_ns()` on a 15.0 ms grid on
> Python 3.12 / Windows 11. Changing the *clock* does — `time.perf_counter_ns()` steps
> at 0.0002 ms, and `capture_kraken.py` uses it for exactly this reason.
> `capture_anvil.py` deliberately still uses `monotonic_ns`, so the four committed Anvil
> traces stay comparable with each other; switching it is a one-line change and a
> decision for whoever next re-captures this venue. Left standing above rather than
> edited, per ARCHITECTURE §9's own rule about superseded reasoning.

### Anvil sheds to a slow consumer — new, and not in the protocol doc

The board reading fewer messages than the desk is real; it is just not a property of
the server's *cadence*. `tools/anvil_drain_probe.py` opens the same socket and sleeps
a fixed delay after each message, so the receive window stays pinched the way an
ESP32 pinches it doing mbedTLS on 8.1 KB book frames. Run against a **simultaneous**
unthrottled capture that stayed flat at 16.9 msg/s throughout — so this is one server
at one instant, not a before/after:

| drain delay | rate | throughput | gaps p50 | gaps max |
| ---: | ---: | ---: | ---: | ---: |
| 0 ms | 16.95 /s | 106.5 KB/s | 63 ms | 156 ms |
| 50 ms | 16.91 /s | 107.4 KB/s | 62 ms | 141 ms |
| 120 ms | **8.32 /s** | 54.4 KB/s | 125 ms | 125 ms |
| 250 ms | **4.01 /s** | 26.9 KB/s | 250 ms | 266 ms |

A client that cannot keep up is **served less, not served late**. The rate collapses
by 4× and the gaps do not stretch at all — because `book` frames are coalesced
per socket (protocol §3.2), so a backed-up socket gets the *newest* book on the next
tick it can accept rather than a queue of stale ones. That is the correct behaviour
for market data and it is worth writing down for M4/M5: **a thinned Anvil stream is
not a broken one**, and a client must not treat its own slowness as a venue fault.
Under mild backpressure only `book` thins; under heavy backpressure (the 250 ms step)
`summary` and `trade` are shed too, from a per-socket egress queue.

This also reconciles the board's numbers exactly. Bench run A reported 8.59 msg/s at
42.70 KB/s, mean 5,148 B. Holding `summary` at 2.00/s and `trade` at 1.58/s (their
measured desk rates, both unaffected by mild backpressure) and solving for `book`
gives **5.14 book/s → 8.72 msg/s at mean 5,012 B** against the board's reported
8.59 msg/s and 5,148 B — inside 1.5%. The board is a mildly backpressured consumer
receiving a thinned book stream, and nothing more.

### The control that settles it

Throttle the desk to the board's own message rate and measure it with the board's own
rule. 120 ms drain delay, 4 minutes, **8.30 msg/s** against the board's 8.59:

| counting rule | n | rate/s | p50 | p99 | p99.9 | max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| any frame | 1,992 | 8.30 | 125 | 125 | 125 | 141 |
| event-producing | 1,756 | 7.32 | 125 | 250 | 250 | 250 |
| **book-affecting** (the firmware's rule) | 1,594 | 6.64 | 125 | 375 | 594 | **594** |

**594 ms** worst book-event silence at the board's message rate, and zero watchdog
trips at 1000 ms over four minutes. The board sees **2,461 ms** at the same rate.

That is the whole argument in two numbers. Anvil delivers a thinned stream *evenly*;
whatever is producing multi-second holes on the board is not doing so at the desk end
of the same LAN, at the same instant, at the same message rate, under the same
counting rule. **The 1000 ms threshold is not the bug, and raising it would hide a
real 1–2.5 s freeze of the feed pipeline** — precisely the frozen-ladder-reading-Live
outcome invariant #5 exists to forbid. Left at 1000 ms; `ARCHITECTURE.md` §9 records
the decision, and the next firmware session owns the stall.

Two caveats on the comparison, stated because they are the ways it could still be
wrong. The desk's default route is wired Ethernet while the board is on Wi-Fi, so the
last hop differs (same gateway, same WAN path); and the desk's capture ran ~1.4 h
after the bench run rather than literally alongside it. Neither can produce
*selective* multi-second silence on a TCP stream that the sender is filling evenly —
loss would be retransmitted, not skipped — but a bench run with the board and a desk
capture running in the same minute would close both, and the board can settle it
alone by printing a gap histogram rather than only `worst_gap`.

---

## M3 addendum — the connect burst, and what a capture can and cannot say about it

The 2026-08-10 bench run produced a second finding beside the stall: the board rejects
**~1,281 frames in the first ~60 s of every connect — about 85 % of the opening burst —
then goes flat**, with a smaller batch on each reconnect. `price_errors`, `other_ticker`,
`unknown_kind` and `truncated_frames` are all zero, so all of it is `parse_errors`: the
frames did not decode at all.

### The desk control, which is already committed

`anvil_101_baseline_20260809.ndjson` **opens at a connect** — its first frame line is the
on-connect `snapshot`, immediately followed by the cross-ticker `summary` — and runs 1,513
frames over 90 s. `test_replay_goldens.cpp` pins `parse_errors == 0` across the whole file,
which covers the same first-60-s window the board is failing in.

So, as far as a capture can see: **Anvil sends nothing at connect that this parser
rejects.** That is a constraint worth having before a bench evening is spent on it, because
it moves the first suspect from the venue to the client.

### The one thing the capture cannot exclude

`tools/capture_anvil.py` writes one line per *message*, and the `websockets` library
reassembles WebSocket-level fragmentation before handing a message over. A message Anvil
split into a text frame plus continuation frames therefore arrives at the capture whole and
is written as one line — **it cannot appear in a trace at all**.

That matters here specifically, because `firmware/src/frame_reassembler.hpp` documents
itself as *publishing such a message incomplete*: this IDF vintage does not surface the FIN
bit, so a continuation restarts `payload_offset` at zero and the reassembler cannot know
where the message truly ends. It publishes at the fragment boundary, the parser rejects the
partial JSON, and the adapter counts a parse error. The header says so in as many words and
calls the counter's existence the point. Anvil has never sent one across 2,694 captured
frames — but every one of those captures is blind to it by construction.

Two other candidates, neither excluded and both board-side: a message cut at the 4,096-byte
RX-buffer boundary, and two messages landing in one buffer. All three are distinguishable
from a single payload, which is what the reject log added on 2026-08-10 exists to print —
status, whole length, head, tail, and the offset of any second `{"type":` in the buffer.

**If it turns out to be server-side fragmentation, a trace will never reproduce it and the
committed goldens never can.** The honest coverage in that case is a synthesised trace plus
a `FrameReassembler` test, not a capture — the same shape as the reassembler's existing
host tests, and the same reason they exist.

---

## M4 stage C addendum (2026-08-19) — how sparse a real book is, and what that did to the window

Venue-neutral, and it belongs here rather than in `NOTES-kraken.md` because both
venues answer the same way and the conclusion is about the panel rather than
about a wire.

### 1. THE MEASUREMENT THAT CHANGED THE STAGE: books are sparse in price, everywhere

Taken over every committed trace before a line of the window was written, by
maintaining each book and measuring the tick distance between adjacent levels on
one side:

| book | adjacent-level gap p50 | p90 | max | contiguous (gap = 1 tick) | one side spans | levels held |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Kraken BTC/USD depth 10 | 8 | 29 | 62 | 21.1% | 96 tk | 10 |
| Kraken BTC/USD depth 25 | **5** | 18 | 62 | **19.5%** | **182 tk** | 25 |
| Kraken BTC/USD depth 100 | 6 | 20 | 67 | 16.4% | 838 tk | 100 |
| Kraken MINA/GBP depth 25 | 10 | **350** | **1,990** | 13.3% | **6,613 tk** | 25 |
| Anvil 101 baseline | 6 | 23 | 186 | 10.0% | 1,079 tk | 101 |
| Anvil 101 depth 27 | 7 | 22 | 99 | 12.6% | 252 tk | 30 |

**Only one adjacent level pair in five is a single tick from its neighbour, and
that is the densest book in the repository.** A side of 25 levels occupies 182
ticks on a liquid pair and 6,613 on a quiet one.

**This retires two of the stage's three proposed policies before they were
built.** The brief named a fixed tick band — one panel row per tick around the
mid — and a density-adaptive version of it. Against these numbers a 27-row band
covers 27 of BTC/USD's 182 ticks, so it renders **about four of twenty-five
levels** with twenty-three rows blank; on MINA/GBP it covers 27 of 6,613, which
is **the touch and twenty-six blank rows**. Widening a row to span several ticks
restores the coverage and destroys the reason for the policy: a row becomes an
AGGREGATE of levels rather than a level, and its width becomes a constant taken
from the market's own span — the mistake ARCHITECTURE §9 already has a row about.

So the window's axis is RANK, and the three policies differ in which levels earn
a row. Every one of them renders levels the book actually holds, which is what
keeps `DisplaySnapshot`'s contract true and its `sizeof` unmoved.

**A price-axis window is not refuted — it is blocked, and cheaply unblockable.**
It needs one thing the published snapshot cannot say: *row 7 is not a level*,
interleaved rather than trailing. `bid_count` can only express trailing unknowns.
Two 32-bit present-masks would carry it (27 rows fit a `uint32_t` exactly) at a
cost of **8 bytes on `DisplaySnapshot`, 24 in the three-slot mailbox**, plus
correcting the three documents that quote the size. That is a decision, and stage
C's brief says to stop and raise rather than take it.

### 2. The four answers per policy, which is what stage D reads at the panel

`dense` = more levels than rows (Anvil 101, Kraken depth 100). `sparse` = wide
gaps between levels (MINA/GBP). `one-sided` = one side empty. `under-filled` =
fewer levels than the 27 rows, which is **every Kraken slice at the shipped
depth**.

| | **top** (best 27) | **largest** (touch + biggest 26) | **thinned** (best 13, then sampled) |
| --- | --- | --- | --- |
| **dense** | The 27 nearest the mid; everything beyond is dropped. Tightest price coverage of the three — worst span **319 tk** at Kraken d100, **414 tk** at Anvil 101. Shows the trading, says nothing about the depth behind it. | Touch plus the 26 largest behind it, in price order. Widest reach — **1,128 tk** / **873 tk**. Shows the walls; the dust between them is invisible, and so is any thin level near the touch. | Best 13 individually, then 14 samples spread evenly across the tail. **1,060 tk** / **1,054 tk**. Detail where the trading is and a sense of the shape behind it, at the cost of a tail whose rows are a sample rather than a neighbourhood. |
| **sparse** | Unchanged in WHICH levels it picks — rank is not price — so only the span grows. On MINA/GBP one side spans 6,613 tk across 25 rendered rows. | Same selection rule, and the widest spans of the three get wider still. A row's price distance from its neighbour carries no meaning in any of the three. | Same. The stride is over RANKS, so a sparse tail is sampled exactly as a dense one is. |
| **one-sided** | Renders the side that exists; the other reports 0 rows and 27 unknown. `has_top()` is false, so `best_ask`/`spread_ticks` are not readable — which is the existing contract and not new here. | Identical to top. | Identical to top. |
| **under-filled** | Renders every level, remaining rows unknown. | **Byte-identical to top.** | **Byte-identical to top.** |

**The bottom-right corner is the finding.** At any depth at or below the panel's
27 rows the three policies are the same window — not similar, identical — because
every level gets a row and there is nothing to choose. Asserted as a property
(`test_window.cpp`, "a book no deeper than the panel makes all three policies the
same window") rather than left as a note, because it stops being true the moment
somebody raises the subscribed depth, and that is exactly when it matters.

### 3. What the panel shows that the venue never checked

B2 measured that Kraken's CRC32 covers the top 10 levels a side whatever the
subscribed depth. So a window's position decides whether the rows on screen were
ever confirmed, and the number differs sharply by policy once the book is deeper
than the panel:

| trace | policy | rendered rows | of which the venue checksummed |
| --- | --- | ---: | ---: |
| Kraken BTC/USD d25 | all three | 149,694 | 59,880 — **40.0%** (10 of 25 a side) |
| Kraken BTC/USD d100 | top | 281,124 | 104,120 — **37.0%** (10 of 27) |
| Kraken BTC/USD d100 | **largest** | 281,124 | **33,433 — 11.9%** |
| Kraken BTC/USD d100 | thinned | 281,124 | 104,120 — **37.0%** |
| Anvil 101 baseline | all three | 66,150 | **0 — this venue publishes no checksum at all** |

**`largest` at depth 100 renders a panel of which 88% was never confirmed by
anyone**, because it reaches for size deep in the book and the checksum does not
follow it out there. `thinned` matches `top` exactly, and that is arithmetic
rather than luck: its head is the best 13 ranks, which contains all 10 the CRC
covers.

The Anvil row is worth as much as the Kraken ones. Its `validated_depth` is 0 —
there is no checksum anywhere in that protocol — so the honest count is **zero
confirmed rows**, which is the reading a missing field would have quietly turned
into "all of them".

**C records this; C does not solve it.** It may well be that a panel showing
unvalidated levels is fine. Stage D should decide that knowing the number.

### 4. The reproduction

The sparsity table came from a scratch pass over the committed traces; every
other figure here is produced by the shipped code and pinned in
`harness/tests/test_window.cpp`, so a regression moves a golden rather than a
paragraph. `dc_ladder --window top|largest|thinned` prints the same figures for
any trace, which is the instrument stage D takes to the desk.
