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

### These traces are not readable by `dc_replay`, deliberately

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
