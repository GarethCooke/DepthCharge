# Feed lag — how old is the book? (M3 stage E, deliverable 1)

Two halves. The **desk half** below is measured and complete. The **board half** is the
owner's lag-versus-uptime run and is still owed; its protocol and its table are laid out at the
bottom, ready to be filled in.

They answer different questions and neither substitutes for the other. The desk half settles the
*mechanism* — does a slow Anvil client get shed, or get queued? The board half settles the
*magnitude on the actual object*, on the actual stack, and calibrates the on-board estimator.

---

## Why this exists

Stage D's acceptance passed. Every transport counter on a clean single-connection bench run read
zero — `parse=0 cont=0 trunc=0 oversize=0 qfull=0 abandoned=0`, `no_slot=2` of 1,703 — and the
panel showed a book that was **96–98 seconds old**.

Nothing in M3 had ever measured freshness. The RX watchdog measures *stopped*. The parse counters
measure *wrong*. The stall probe measures *whose fault the stopping is*. A feed that is running
perfectly and a hundred seconds behind is invisible to all three, and was.

The prior finding this run had to re-open is ARCHITECTURE §9, 2026-08-09: *"Anvil sheds to a slow
consumer, and sheds evenly."* That came from `tools/anvil_drain_probe.py`, which measured **rate**
and **inter-message gaps** at several artificial drain speeds. Both are equally consistent with
pure queuing — a client draining at 4/s receives something every ~250 ms whether the thing it
receives is current or ninety seconds stale.

---

## The desk half — measured 2026-08-11

`tools/anvil_freshness_probe.py`. Two WebSocket sockets from one process against
`wss://anvil.garethcooke.com/ws?ticker=101`, started together:

- a **reference** socket drained flat out — its arrival time for a given frame is as close to
  "when the server sent it" as a desk client gets;
- a **subject** socket sleeping 250 ms per message, the same throttle the 2026-08-09 drain probe
  used for its 4 msg/s step.

Anvil's wire `seq` is one **global** engine counter (ARCHITECTURE §4), so the same broadcast
carries the same `seq` on both sockets. Matching on it turns "how stale is this message" into a
subtraction against a real clock instead of an inference from a rate.

### 1. Anvil has not slowed, the 2 Hz premise is exact, and Anvil does not drop fast clients

**30 minutes, one unthrottled socket** (`--reference-only --seconds 1800`):

```text
reference   1799.2s   28238 msgs   15.70 msg/s   110.4 KB/s
            book=13.55/s  summary=2.0003/s  trade=0.15/s  snapshot=0.00/s
summary broadcast: 3600 in 1799.2 s = 2.0003/s over 3599 intervals
period 499.9 ms against the assumed 500.0 ms  (+0.015%)
```

Four things follow, and one of them is the reason the run was made.

**The socket survived the full thirty minutes with no drop.** That is the control for the board's
every-few-minutes disconnects: **Anvil is not closing sockets on a timer, and it is not closing
everyone.** Whatever is dropping the board's connection is specific to the board — its own
slowness, its link, or its stack — and is not a property of the venue. One candidate eliminated
for the cost of leaving a socket open.

**The server is at 15.70 msg/s.** M0 measured 15.5 in July; the board reads 6.07. The gap is the
board's, not the venue's — the reading the 2026-08-09 amendment reached by a harder route, now
confirmed directly.

**`summary` is 2.0003/s — a 499.9 ms period, exact to 0.02% over half an hour.** That is the
denominator `firmware/src/staleness.hpp` divides by; it scales every age the board prints and
nothing on the board can measure it.

**`trade` is 0.15/s**, absent from the two short runs entirely. It settles a question the per-kind
data could not: the 46-minute soak's board saw 129 trades over 2,771 s = 0.047/s, which is **31%**
of the wire — right alongside its ~36% drain of everything else. **No per-kind anomaly on the
board**, which is what queuing predicts and per-kind coalescing would not.

> **⚠ The first version of this section quoted 15.89 msg/s and 2.017/s from a 60-second run, and
> both were artefacts of our own tool.** It computed a rate as `n/span` where the span between
> first and last arrival holds `n−1` intervals — a +0.84% bias at n=120, vanishing by n=3,600. The
> 2.017 was read as a real 0.85% departure from the 500 ms constant and used to justify a whole
> analysis of estimator drift, including a predicted false alarm on every healthy connection.
> There is no such drift. `tools/anvil_freshness_probe.py::rate` now carries the correction and the
> reason, and the figures above are the 30-minute ones. **This is precisely the failure this stage
> exists to name — a measurement that answers a nearby question, read as answering the one that
> mattered — committed this time by the instrument written to catch it.**

### 2. Every frame kind is thinned by the same fraction — that is queuing, not shedding

150 s, both sockets:

```text
socket           span    msgs    msg/s     KB/s   per-kind /s
reference      149.5s    2344    15.68    108.4   book=13.66  summary=2.01  snapshot=0.01
subject(250ms) 149.5s     598     4.00     27.6   book=3.48   summary=0.51  snapshot=0.01

subject as a fraction of reference, per kind:
  book         3.48/s of  13.66/s =  25.5%
  summary      0.51/s of   2.01/s =  25.7%
```

**The 2026-08-09 per-kind result does not reproduce.** That run reported `summary` and `trade`
arriving *intact* at every drain speed with only `book` shed. Here `summary` is thinned to 25.7%
and `book` to 25.5% — the same fraction, within a fifth of a percentage point. A uniformly
delayed byte stream does that. Per-kind coalescing in an application send path cannot.

### 3. The lag is linear, and there is no ceiling

```text
elapsed on subject         n   lag p50   lag p90   lag max
     0.0 -   18.7 s       75      6.95     12.52     13.84
    18.7 -   37.4 s       75     20.98     26.66     27.95
    37.4 -   56.1 s       74     34.83     40.38     41.72
    56.1 -   74.8 s       75     48.72     54.12     55.45
    74.8 -   93.4 s       75     62.67     68.30     69.62
    93.4 -  112.1 s       74     76.44     82.00     83.33
   112.1 -  130.8 s       75     90.31     96.02     97.33
   130.8 -  149.5 s       75    104.34    109.86    111.16

median lag rose +83.49 s across the run (+0.745 s per second of elapsed time)
the drain fraction predicts +0.745 s/s independently (subject gets 25.5% of the stream)
```

**Two independent derivations of the same quantity agree to three decimal places.** The lag
growth measured by matching `seq` across two sockets, and the growth predicted by the message
counts alone (a socket receiving fraction *f* falls behind by 1 − *f* seconds per second), are
both 0.745. That agreement is what rules out an artefact of the matching.

Eight bins, 598 matched messages of 598, **not one of them a plateau**. Whatever bounds Anvil's
per-socket queue, it is above 111 seconds of backlog.

### 4. The implied queue is ~12.4 MB, for one slow client

2,344 messages sent, 598 received, 7,074 B mean: **~1,746 messages, ~12.4 MB** still held for
this socket when the probe disconnected, and still growing linearly.

Derived rather than observed — Anvil publishes nothing about its own send queue — but it follows
from two counts and a mean size. It is the number that makes this a note worth putting on Anvil's
backlog: an application-level queue that does not level off is a different problem for the venue
than a coalescing window is, and DepthCharge is presumably not the only client that can be slow.

### What the desk half settles, and what it does not

| Question | Answer |
| --- | --- |
| Does Anvil shed to a slow consumer? | **No.** Not at a 25% drain, not for any frame kind. |
| Does it queue? | **Yes**, linearly, with no observed ceiling to 111 s / 12.4 MB. |
| Is `summary` a usable clock? | **Yes** — 2.0003/s unthrottled over 30 min, and thinned in exact proportion when the socket backs up, which is what makes the deficit an age. |
| Has the venue slowed since M0? | **No.** 15.70 msg/s against July's 15.5. |
| Does Anvil drop clients on a timer? | **No.** One unthrottled socket held 1,799 s with no drop. |
| Is the board's 6.07 msg/s the venue's fault? | **No.** |
| Is the ESP32's lag the same mechanism? | **Not established.** This is a desk client on a desk TCP/TLS stack. It reproduces a slow *reader*, not an ESP32. |

That last row is the whole reason the board half is still owed.

---

## The board half — MEASURED 2026-08-11, and it is worse than the desk predicted

**65 minutes of board, 390 age samples, 16 connections** — `firmware/logs/device-monitor-260811-140341.log`,
14:03:41 → 15:08:59, analysed by `tools/board_log_lag.py`. No stopwatch was used and none was
needed: the board prints a sample of its own lag curve every ten seconds, and a socket that dies
every few minutes yields many complete curves instead of no long one.

### The headline: the panel reached 328 seconds of age

```text
PEAK AGE: `run` high-water 328.2 s; largest single sample 328.6 s
final counters: frames=24855 wd_gaps=4 sock_gaps=15 connects=16 worst_gap=5303 ms
                parse=88 price=0 ticker=0 unknown=0
```

**Five and a half minutes.** Three separate connections reached 309–329 s. And the run that
contains them has `wd_gaps=4` in 65 minutes and `worst_gap=5303 ms` — **the feed never stopped.**
It flowed continuously at 40% of the wire and simply fell further behind, which is exactly the
failure every other instrument in this firmware is blind to.

### Sixteen curves, and every one of them is a straight line

```text
  #  samples  window s  peak age   slope  1-drain  agree
  1        9      94.5      57.0   0.595    0.603  0.008
  2       22     212.5     128.5   0.606    0.605  0.001
  4       29     283.5     174.0   0.613    0.614  0.000
  6       57     570.1     328.6   0.577    0.576  0.001
  9       53     530.3     309.3   0.583    0.583  0.000
 11       55     541.5     321.0   0.593    0.593  0.000
 15       38     376.2     225.7   0.599    0.600  0.001
 16       14     134.4      84.4   0.627    0.627  0.001
                                       (16 of 16 shown abridged)

lag slope across all connections:  median +0.599 s/s  => drain f = 0.401
the same figure from the summary counters: +0.601 s/s  => drain f = 0.399
```

**No plateau in any of them.** The desk finding transfers to the board unchanged: **the ESP32 is
queued, not shed.**

**And the estimator validates itself on hardware.** The lag *slope* and the *cumulative summary
deficit* are two independent derivations of 1 − *f* from different fields of the same line, and
they agree to **0.000–0.008 on every connection with more than two samples** — median 0.599 against
0.601. That is the calibration deliverable, obtained without a stopwatch and without the 1/*f*
bias that made the stopwatch unusable.

Post-reflash confirmation on the current build, one connection, 3.7 minutes:
`age 140.7 s over 226.2 s`, slope 0.621 against a counter-derived 0.622, and the `seq` join key
now present on the line.

### The implied server-side queue: ~39 MB, for one socket

At the 328.6 s peak the board is behind by 328.6 s × 15.70 msg/s ≈ **5,159 messages**, at the
run's own measured 7,520 B mean = **~38.8 MB** that Anvil is holding for this one connection. The
desk probe was killed at 12.4 MB; the board went three times further unaided.

### The drop cadence: SCATTERED. The queue-bound hypothesis is refuted

```text
20 outage(s); interval median 138 s, min 3, max 575
coefficient of variation 0.87  => SCATTERED
stale reasons: {'disconnect': 20}
```

A queue filling to a fixed bound produces a fixed interval. These range from 3 s to 575 s with a
CV of 0.87. **The prediction this file made a few hours earlier — that the ~5-minute cadence was
the queue hitting a ceiling — does not survive its own test.** Connection *durations* sorted:
13, 56, 94, 131, 134, 134, 145, 170, 194, 212, 242, 284, 376, 530, 542, 570 s. The top three do
cluster within 6%, but against an exponential of the same mean, P(all 16 ≤ 570 s) = 0.21 — **not
significant.** Recorded as a null result, because a measured negative is a result.

### What the board says about the cause, and the one thing it cannot say

| Signal | Reading |
| --- | --- |
| `-- holes` | `n=19 board=0 link=19 mixed=0` |
| `-- cpu` | `window c0=89% c1=88%` against `healthy c0=90% c1=86%`, n=21,576 |
| `-- rssi` | `now -43 min -52 max -35 dBm`, n=6,660 — strong throughout |
| `rejoining (#N)` | **absent** — `WifiSupervisor` never fired; the station never dropped hard |
| `ws down: event 2` | ×16 — `WEBSOCKET_EVENT_DISCONNECTED`, i.e. a socket read failure |
| handle order | `ABABABABABABABAB` — perfect alternation, DESIGN strain 14 intact |
| `connects` vs `socket up on handle` | 16 vs 16 — **no spurious retired-handle resets** |

**`link=19` is not evidence the radio is at fault, and this is the reading to get right.** The
stall probe's link-bound verdict means *Core 0 had headroom and nothing to do* — which is
precisely what a board being fed at 40% by a **queuing server** looks like. The classifier cannot
distinguish *the air delayed them* from *the server is holding them*, because both leave Core 0
idle with nothing arriving. It was built before queuing was on the table and it is honest within
its own terms; it simply does not have a category for this.

So the cause remains open, with the field narrowed: not the radio's signal strength (−43 dBm), not
Wi-Fi association, not CPU, not a spurious handle event, and **not a queue reaching a fixed size.**
Every drop is a socket read failure on a connection that is otherwise delivering. A
backlog-*related* but stochastic mechanism — a TCP retransmission timeout against a 5,744 B
receive window with tens of megabytes queued behind it — remains live and is not separable from a
link cause using anything the board can produce.

**The test that separates them, and it needs no board:** the throttled desk probe run long against
a quiet server. A desk socket at *f* ≈ 0.25 with no radio in the path that gets dropped after
several minutes proves backlog-causes-drops; one that holds for thirty minutes at 100 MB of queue
puts the board's drops on the board's own link or stack. **Do not run it beside a live bench
session** — see the warning in §1.

### THE CAUSE, and it is not a firmware defect: bandwidth-delay product

**Anvil is in Virginia.** `anvil.garethcooke.com` → `52.204.246.224`, AWS us-east-1. The board is
in the UK. Eight TCP connects from the desk: **85.7, 86.6, 86.6, 87.2, 87.6, 92.5, 92.8, 119.8 ms
— median 87.4 ms.** ICMP is filtered, which is presumably why no session ever had this number, and
every analysis in this repo until now has silently assumed a LAN.

That single number, against a receive window that is a compile-time constant, is the whole defect:

```text
ceiling = TCP_WND / RTT = 5,744 B / 0.087 s = 65.5 KiB/s
Anvil's stream                              = 110.4 KiB/s
=> the board can take at most 59% of the feed even at 100% window efficiency
   measured: 44.2 KiB/s = 67% of its own ceiling, 40% of the wire
```

**44.2 / 110.4 = 0.40 — the drain fraction, derived from first principles with no fitting.**

#### The measurement that proves it, and it was in the repo all along

Across **548 healthy 10-second windows** in the two board logs, the inbound *byte* rate is a flat
ceiling while everything anyone proposed as a cause varies wildly around it:

| | range across windows | ratio |
| --- | --- | --- |
| message rate | 3.03 – 7.79 msg/s | **2.6×** |
| mean message size | 3,376 – 7,819 B | **2.3×** |
| chunks per message | 1.56 – 2.96 | **1.9×** |
| **inbound KiB/s** | **41.1 – 44.2 mean, max 49.8** | **1.0×** |

And across runs, including one with **no panel at all** (2026-08-09 stage C, heap `free=124812`
proves the framebuffer was never allocated): 42.70 KiB/s no-panel, 41.54 with the panel drawing,
43.29 stage E. **Byte rate moves 4% while message size moves 46% and the panel comes and goes.**

That invariance refutes, from measured data alone:

- **every per-message and per-read mechanism** — they predict a fixed messages/s or reads/s, so
  byte rate would track message size. It does the opposite: msg/s fell 29% while KiB/s held.
- **the `kWsRxBufferBytes` lever.** Reads per message already varied **26%** across these windows
  at constant byte rate. *The natural experiment has been run and returned zero.*
- **the panel-emissions candidate** that ARCHITECTURE §9's `clkphase` entry makes tempting — the
  no-panel run sits inside the panel runs' scatter.
- **Wi-Fi power save** — separately dead on the board's own banner: `driver ps=0` from an
  `esp_wifi_get_ps()` readback, at −36 to −47 dBm.

#### Why `kWsRxBufferBytes` was never going to work, read out of the archive

`cfg.buffer_size` is nothing but the `len` argument to `mbedtls_ssl_read`
(`esp_websocket_client_task+0x2b2..+0x2c2` → `ws_read_payload+0x10` one `min()` and one read →
`esp_mbedtls_read`, a bare call with no loop). It is applied **above the TLS record layer** and is
invisible to lwIP: it cannot change bytes in flight, the advertised window, ACK timing, or
socket-read sizing. The stage E brief's stated rationale for lever #1 — "removes two partial-window
round trips per frame" — describes something our buffer is not in a position to do.

There is also **no artificial delay anywhere on the connected path**: no `vTaskDelay`, no event-group
wait, and the read loop drains a whole WebSocket frame per iteration with no wait between chunks
(back edge `+0x32c blt payload_offset, payload_len`). The single `vTaskDelay` in the task's literal
pool is reached only in state 3 (WAIT_TIMEOUT). The library is not pacing anything.

#### The only lever that reaches it is the one that is out of reach

To keep up, the window must satisfy `WND ≥ 110.4 KiB/s × 0.087 s ≈ 9.9 KB`. Options:

| Window | Ceiling | Verdict |
| --- | --- | --- |
| 5,744 B (today) | 65.5 KiB/s | 59% of the wire at best |
| 11,488 B (2×) | 130.9 KiB/s | would clear it, with 19% headroom |
| 22,976 B (4×) | 261.9 KiB/s | comfortable |

`CONFIG_LWIP_TCP_WND_DEFAULT` and `CONFIG_LWIP_WND_SCALE` are **both compile-time**, baked into the
shipped `liblwip.a` (`.literal.tcp_alloc+4` = `0x00001670` = 5,744; `CONFIG_LWIP_WND_SCALE` absent
from the sdkconfig, so scaling is off). **Reaching either means a rebuilt framework —
pioarduino or ESP-IDF from source.** That is milestone-weight and is exactly the outcome the stage
E brief pre-authorised: *"If it needs a rebuilt framework, that is a milestone-weight decision, not
this evening's — record it and stop."*

**So E2's answer is a measured negative on every lever it listed, and a named cause outside all of
them.** The eighth time this precompiled vintage has decided a design, and the most expensive.

#### What can be done without a framework rebuild

1. **Nothing in firmware raises the ceiling.** Say so plainly rather than trying levers that the
   invariance above has already tested for free.
2. **Reduce what has to cross the Atlantic.** Anvil sends the full book at 13.6/s × ~8.7 KB. Its
   backlog already carries a *sequenced incremental L2 feed*; that item is now load-bearing for
   DepthCharge rather than a nice-to-have, because a delta feed at a tenth the bytes fits inside
   the existing window. **Anvil is not modified from here** — this is a note for their backlog.
3. **Be honest about it**, which the panel now is: `-- age` reports the consequence, and the
   staleness-`Gap` proposal is how the ladder itself would say so.
4. `kWsRxBufferBytes = 8192` remains worth doing for an unrelated reason — see below — but must be
   filed with an explicit prediction of **no throughput change**.

#### A free win that is not a throughput win

`malloc_alwaysinternal_limit` is **4096** on this build (`start_cpu0_default+0x4f` →
`heap_caps_malloc_extmem_enable(4096)`; `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL 4096`). So an
allocation of **4097 or more goes PSRAM-first**. The four WebSocket buffers (rx+tx × two handles)
are `malloc`'d at `esp_websocket_client_init` and currently take **16,384 B of internal heap**;
at 8192 they move to PSRAM and **return all 16 KB of internal**. That matters because the real
internal constraint is mbedTLS, which is pinned internal (`esp_mbedtls_mem_calloc` with
`MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT`) and needs **two contiguous 16,717 B blocks per session**
(`mbedtls_ssl_setup+0x12/+0x62`, literal `0x414d`) — out of a 38,900 B hole at socket-down, leaving
5,450 B of slack. Raising the buffer *widens* the reconnect margin. Filed as a robustness change
with a predicted throughput delta of zero.

### Two smaller readings worth keeping

- **`parse=88` of 24,855 frames (0.35%)** — up from the 46-minute soak's 0.13%. Each rejected
  `summary` inflates the age by exactly 500 ms, so even if *all* 88 were summaries that is 44 s
  across the run, ~2.8 s per connection against a 329 s peak. **The age figures are not a parse
  artefact.**
- **`backseq=30`** — Anvil's global counter stepping backward on this socket, the M0 finding still
  reproducing, and correctly ignored for ordering.

---

## The original protocol, kept for the record

**CC does not drive the bench.** This was the protocol and the table before the board half was
measured; the seq-join protocol above supersedes it.

### ⚠ The stopwatch protocol this file originally specified was WRONG — read this before using it

The first version of step 3 below said: *freeze the web client, time the interval until the panel
shows the same price, **the interval is the lag***. It is not. Under the queuing model this file
itself establishes, the panel's market-time cursor advances at the drain fraction *f*, not at 1.0,
so catching up to a frozen reference takes **age ÷ f**, not *age*:

```text
displayed market time   M(t) = t0 + f·(t − t0)
age                     L(t) = t − M(t) = (1 − f)·(t − t0)        <- what `-- age` prints
stopwatch               S    = L(t_f) / f                          <- what the stopwatch reads
```

Both relations are confirmed on this file's own committed desk probe (*f* = 0.2551): across all
eight bins `L(t)/t` reads **0.743–0.748** against a predicted 1 − *f* = 0.745, and `L/(f·t)` reads
**2.91–2.92** against a predicted (1 − *f*)/*f* = 2.9197.

**At the board's *f* ≈ 0.41 the stopwatch reads about 2.4× whatever `-- age` prints, and that is
agreement, not disagreement.** Calibrating the estimator against a raw stopwatch would have
detuned a correct instrument by that factor. The bench rule, and the age line carries every term:

> **expected stopwatch ≈ `age` × 100 ÷ `drain %`**

Two consequences worth stating separately:

- **The 96–98 s figure of 12:05 is not an estimate of the lag.** It is a catch-up interval; at
  *f* = 0.41 it implies an age nearer **40 s**. The "estimator over-reads by ~26%" note that was in
  `staleness.hpp` is therefore withdrawn — the two numbers were different quantities and no
  over-read was ever demonstrated in either direction.
- **The later rows of the old table cannot be executed anyway.** At *f* = 0.41 the 20-minute
  reading takes 712/0.41 ≈ **29 minutes** to complete and lands at ~49 minutes of uptime — on a
  socket that, as of 2026-08-11, does not survive five.

### Protocol — the seq join: no stopwatch, no human, no 1/f

Anvil's wire `seq` is a single **global** engine counter (ARCHITECTURE §4). Useless for ordering —
which is exactly what makes it a join key: **the same broadcast carries the same value on every
socket.** The board now prints its latest one on the `-- age` line, so the serial log and a
simultaneous desk capture join into a continuous lag curve sampled at the publish rate, measuring
the age itself rather than the age divided by the drain fraction. It is the same subtraction
`tools/anvil_freshness_probe.py` already performs across two sockets.

1. **Start the desk control first** and leave it running for the whole session:
   `py tools/anvil_freshness_probe.py --reference-only --seconds 1800`.
2. **Flash and log to a file**, from `firmware/` so the log lands in the gitignored `firmware/logs/`:
   `pio run -e depthcharge -t upload -t monitor`. `log2file` is in `monitor_filters` now, alongside
   `time` — the per-line timestamp is what every interval below is read off.
3. **Leave it running for 30–60 minutes and walk away.** No stopwatch discipline, no checkpoints.
4. **Do not require one connection.** The old rule voided a run on `connects > 2`; on a board that
   drops every few minutes that voids every run there will ever be. `-- age` resets per connection
   and reports its own window, so **each connection is a complete lag curve from zero to peak**,
   twelve samples per two minutes. Twenty cycles is better data than one long run, not worse.
5. **Record the wall-clock time of every drop.** Regularity is the discriminator: periodic to
   within ~30 s is a queue filling to a bound; scattered is a link.

### Reading the log afterwards

| Line | What to pull |
| --- | --- |
| `-- age` | `age` / `worst` / `run`, the `drain %`, and `seq` — the join key |
| `-- feed` | `connects`, `sock_gaps`, `wd_gaps` — where each connection begins and ends |
| `-- errors` | `parse=` — a rejected `summary` counts as missing and inflates `age` by 500 ms each |
| `-- rate` | `in N/s`, the second independent read of the drain fraction against the desk's 15.70/s |
| `socket up on handle X, N ms into attempt #M` | **every legitimate `connects` increment is preceded by one of these** |

**That last row is a defect check, not bookkeeping.** `WEBSOCKET_EVENT_CONNECTED` is posted with
no live-handle filter (`ws_transport.cpp:313`), unlike DATA (`:379`), so a handshake abandoned on
the 7 s budget can complete afterwards and fire `CONNECTED` from a retired handle — incrementing
`connects` and resetting the age estimate on a socket that is genuinely behind. **A `connects`
increment or an `-- age` reset with no `socket up on handle …` line before it is that path.** It
needs a handshake past `kHandshakeBudgetUs`, so also grep for any `N ms into attempt` above 7000.
Not changed at the bench: the transport is what is keeping the panel alive tonight, and this took
three evenings to get right. Recorded for the owner to decide.

### The table to fill in

**Superseded — the board's own `-- age` line fills this in every ten seconds, and the stopwatch
column measures `age ÷ f` rather than the age. See the board half above for the measured curves.**

| Uptime | Stopwatch lag | Board `-- age` | Board `drain %` | Desk `summary/s` | connects |
| --- | --- | --- | --- | --- | --- |
| 1 min | | | | | |
| 2 min | ~2 min *(run 1, see below)* | | | | |
| 5 min | | | | | |
| 10 min | | | | | |
| 20 min | | | | | |

### Run 1 — 2026-08-11, started 13:48:30. **Void for the curve; one real result.**

Owner at the bench, reported verbatim: *"2 min lag — 2 mins, then disconnected, reconnected and
corrected."*

**Void past the 2-minute mark**, by this file's own rule 1: the socket dropped, so the 5/10/20
minute rows cannot come from this run. A reconnect discards the backlog with the socket and Anvil
sends a fresh snapshot, which resets the quantity being measured.

**But the drop is itself the result, and it is the first board-side confirmation of the
mechanism.** The desk probe established that Anvil queues rather than sheds; the direct
consequence is that *killing the socket must flush the lag*. The panel did exactly that —
**"reconnected and corrected"**. That is the same effect which made the 2026-08-09 86-minute run
look healthy at 21 reconnects, now observed deliberately rather than in hindsight, on the object.

**The 2-minute figure is unremarkable once it is paired correctly, and that took a wrong answer to
find out.** The first analysis of it read the stopwatch as the lag, computed *f* = 1 − 120/120 = 0,
and concluded the board had received nothing — impossible beside a panel that was visibly drawing.
The error was the pairing, not the board: the stopwatch measures a catch-up interval, so
*f* = T_f/(T_f + S) = 120/240 = **0.50** against the run clock, or 0.478 against the socket's true
age. Both sit inside the band every committed board run occupies (~0.34–0.50), and 0.50 reproduces
the highest board drain ever recorded (2026-08-09 run A, 8.49/s of a simultaneous 16.95/s desk
capture = 0.501) to within half a second. **The observation needs no rounding, no clock offset and
no drain collapse to explain it.**

Two caveats keep it from being a data point rather than a reassurance. The catch-up may have been
**truncated by the reconnect** rather than completed — "reconnected and corrected" is exactly what
an interrupted catch-up looks like — in which case *S* is a lower bound and *f* ≥ 0.5. And *T_f* is
the *connection* age, which is unknown if the socket had already cycled. **Only the log settles it,
and the `seq` join settles it without any of this arithmetic.**

### The second drop, and the question it opens

**13:55:20 — the socket dropped again**, ~4 min 50 s after the first. Two drops is not a pattern,
but it lands on a number this project has now written down three times without noticing it was
the same number:

| Run | Link | Spontaneous drops | Mean interval |
| --- | --- | --- | --- |
| 2026-08-09, ~86 min | flaky (pre-mesh-fix) | 21 reconnects | **~4.1 min** |
| 2026-08-11, 46 min stage D soak | good, rssi −32…−52 dBm | `sock_gaps=9 connects=10` over 46.2 min | **~5.1 min** |
| 2026-08-11, this run | good | 2 drops, 13:48:30 → ~13:50:30 → 13:55:20 | **~4.8 min** |

*(The 2026-08-09 17:41 and 17:44 outages in `bench-2026-08-09-ws-reconnect.md` are excluded —
those were the owner stopping the server deliberately, not spontaneous drops.)*

**The hypothesis this raises, stated so it can be killed.** The desk probe established that Anvil
queues without shedding and without an observed ceiling. A queue with no ceiling has to end
somewhere, and the obvious somewhere is the socket: Anvil's send buffer hits a bound, or a write
times out, and the connection is closed. If that is what is happening, then

- the interval between drops is **set by the fill rate**, not by the radio: at the board's ~60%
  deficit the queue fills at roughly 0.6 × 15.70 msg/s × 7 kB ≈ **66 kB/s**, which reaches ~20 MB
  in five minutes;
- **the reconnects are not flakiness, they are the relief valve** — which would mean the lag does
  not grow without bound in service, it *sawtooths*: up to two-to-five minutes, flush, repeat, with
  the panel permanently a minute or two behind on average;
- and three evenings of this milestone spent treating reconnects as a link problem were treating a
  symptom.

**One candidate is already eliminated.** A 30-minute unthrottled desk socket (§1 above) held
without a single drop, so **Anvil is not closing sockets on a timer and is not closing everyone.**
Whatever drops the board is specific to the board. That leaves the backlog hypothesis and the link
hypothesis standing, and rules out the venue.

**The counter-evidence, which is real and is why this is a hypothesis.** The 46-minute soak
classified all fifteen holes **link-bound**, with Core 0 at 98–99% idle against a 91% baseline, and
noted the holes clustered at the weak end of the rssi range (−40 to −49 of a −32…−52 spread).
Against that: `−40 dBm is not a weak signal`, and — more importantly — **the stall probe's
"link-bound" verdict means "Core 0 had headroom and nothing to do", which is exactly what a board
being fed at 40% by a queuing server looks like.** The classifier cannot distinguish *the air
delayed them* from *the server is holding them*, because both leave Core 0 idle with nothing
arriving. That is a limitation of the instrument, not a defect in it, and it was not visible until
queuing was on the table.

**The test, and it needs no board.** A 15-minute desk run of
`tools/anvil_freshness_probe.py --seconds 900 --delay 0.25`. The throttled socket fills at ~83 kB/s,
so it passes 20 MB at ~4 minutes and ~74 MB by the end. **If a server-side queue bound is closing
these sockets, the subject socket dies during this run and the reference socket does not** — and
the point at which it dies gives the bound. If both survive 15 minutes, the backlog does not kill
the socket at the desk and the board's cadence needs a different explanation. Two sockets in one
process is its own control: a network event takes both.

**It was started on 2026-08-11 and deliberately killed a few minutes in**, because it is a
confound while a bench run is live: a socket being starved toward tens of megabytes of server-side
queue could pressure Anvil into dropping *other* sockets, and a board drop caused by the desk probe
would look exactly like the board's own backlog closing its connection — manufacturing the evidence
it was run to find. **Run it against a quiet server, never beside the bench.** The
`--reference-only` control is the opposite case and is safe to run concurrently: one normal socket,
which is what `firmware/README.md` already prescribes as the bench control.

### What each shape of result means

Read against the corrected relation `stopwatch ≈ age ÷ f`, and preferably from the `seq` join
rather than from either number alone.

- **`age` rises linearly within each connection and resets at every reconnect.** The desk finding
  transfers to the board; the lag is bounded in service only because the socket keeps dying, and
  E2's job is to stop the queue forming. This is the predicted outcome.
- **`age` plateaus within a connection.** Something on the board's path bounds the backlog that the
  desk client's does not — TCP window, socket buffer, or Anvil treating this client differently.
  The ceiling is then the number, and `staleness.hpp` must say by how much the estimator over-reads
  above it.
- **A stopwatch reading ≈ `age` × 100 ÷ `drain %`.** That is the instruments AGREEING. Only a
  departure from that relation is evidence of anything.
- **`age` and the `seq` join disagree by a growing amount.** Summaries are being lost somewhere the
  estimator counts as queued — check `-- errors parse=` first, since each rejected `summary`
  inflates the age by exactly 500 ms.
- **`AHEAD n.n s (m%) — 2 Hz premise suspect` appears.** The broadcast period is not 500 ms on this
  server; re-run `--reference-only` and rescale. Note the warning now tests a *ratio* against a 5%
  tolerance: the wire's real 2.017/s is 0.85% and must never fire it, which an absolute threshold
  did at 3.9 minutes into every healthy connection.

### The one number this run should also produce

The board's peak `age` within a single connection, and whether it is still climbing when the socket
dies. If a connection that survives twenty minutes reaches ~700 s, the panel has been showing a
book from before the run started for most of the session, and that is the sentence M3's definition of done
now has to survive.

---

## Provenance

Desk half run by Claude Code from the project desk on 2026-08-11 with
`tools/anvil_freshness_probe.py` (committed with this record), Python 3, stdlib only, reusing
`capture_anvil.py`'s RFC 6455 client so both sockets are the same socket code the ground-truth
captures use and both `rx_ns` stamps come from one process-wide `time.monotonic_ns()`.

The `+1.12 s/s` figure printed by the first version of the tool was wrong — it divided the
inter-quartile growth by half the span where the two quartile centroids are three quarters of a
span apart. It was caught by the drain-fraction cross-check disagreeing with it, which is now a
permanent line in the tool's output for exactly that reason. The corrected run is the one quoted
above.
