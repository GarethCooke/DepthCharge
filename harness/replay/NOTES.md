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

The third is M3's re-measurement of the same thing the first one measures, seventeen
days and one bench bring-up later; it is added rather than substituted, because the M1
goldens pin the M0 trace and a re-capture that silently replaced it would move them. See the
[M3 addendum](#m3-addendum--the-watchdog-threshold-re-measured-and-left-alone).

`gzip -c <file> | wc -c` is recorded because it is the realistic proxy for what
git actually stores after zlib+delta — ~540 KiB for all three traces combined,
versus ~27 MiB raw. The book frames are ~8 KB and highly repetitive, so they compress
~50×. **This sets the trace-commit policy before Kraken traces arrive at M4:**
capture long locally, commit a compact sliced window, quote the gzip size.

Full local captures (git-ignored, `_local/`): baseline 4658 frames / 300 s /
~30 MB; reconnect 1836 frames / ~124 s / ~15 MB; the 2026-08-09 baseline 20,418
frames / 1,199.9 s / ~128 MB, plus `drain-120ms.ndjson` (1,992 frames / 239.9 s),
the deliberately throttled socket the M3 addendum's control uses.

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
