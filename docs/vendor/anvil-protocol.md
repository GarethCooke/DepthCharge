<!--
DepthCharge vendored snapshot of Anvil's wire protocol.

  Wire version : 1
  Source       : Anvil repo, PROTOCOL.md
  Source commit: b4d31c207b16627f19f22209c2b4a63de904f4de  (2026-08-16)
  Vendored on  : 2026-08-16  (A7 integration + the A1-A7 hand-back)
  Supersedes   : e8d313f2dc71bc39adeb66c0e30a35cdfdcaa13e  (2026-07-26),
                 vendored 2026-08-16 at the M3 close-out
                 d501652e7b205e36e0c9e647ef3e720559e9f82d  (2026-07-07),
                 vendored 2026-07-23 at M0

Canonical source remains the Anvil repository; this pinned copy records exactly
what DepthCharge was built against. Do not edit here — re-vendor from Anvil to
update, and bump the commit/date above. See ARCHITECTURE.md "Boundaries".

WHAT MOVED IN THIS RE-PIN. Unlike the last one, this is not documentation only:
the wire gained a parameter and DepthCharge now sends it. Anvil's hand-back is
`docs/anvil-handback-2026-08-16.md`, answering `docs/anvil-handover-2026-08-16.md`.

  §2 `GET /api/book`  `depth` is now honoured. It was previously ACCEPTED AND
                      SILENTLY IGNORED — which corrects a premise in DepthCharge's
                      own ask, where the existing parameter was cited as proof
                      that "the name, concept and validation already exist". The
                      name existed; the validation did not. Both routes now share
                      one parser.
  §3 `depth`          NEW, and the reason this file moved: a per-socket book
                      depth on `/ws`. Absent or 0 is every published level, so
                      the web client is untouched.
  §3 Keepalive        NEW. Client pings are always answered, and the pong is
                      queued behind whatever is already waiting for that socket.
                      See the note below — this closes an open DepthCharge item.
  §4 Slow consumers   NEW. The server queues, it does not shed; unbounded in v1.
                      This closes the note this file has been carrying since
                      2026-08-11 — see below.

WHAT MOVES IN engine/ WITH IT: nothing, and that was the design goal of the ask.
The adapter already accepts 83-126 levels a side and truncates above
`kMaxSnapshotLevels` (256) with the truncation counted, so a 30-level book is
trivially in contract. No field, type, name, unit, frame shape or handshake
behaviour changed; `depth` only ever truncates a prefix of an already-sorted
ladder. What DID change is one firmware constant (`kAnvilPath` gains
`&depth=27`) and one new committed trace.

NOTE (A7, 2026-08-16) — DEPTH IS SERVED IN TIERS, AND THIS IS THE ONE CONTRACT
FACT A READER OF §3 MUST NOT SKIM. A request rounds **UP** to the next supported
value — 1,2,3,5,8,10,15,20,30,40,50,75,100,150,200,300,500,1000, then unlimited
— and never down, so a client is guaranteed at least what it asked for and must
truncate locally for an exact count. **DepthCharge asks for 27 and is served 30.**
The tiering is not in DepthCharge's ask; it came out of Anvil's own review, and
the reason is sound: `depth` arrives from an unauthenticated query string, and
free-form depth would have cost the broadcaster one serialisation per distinct
depth in use, per ticker, per tick. Tiering bounds that by a constant.

Measured on this side rather than taken on trust, because the ask's whole lesson
was that a nearby measurement gets read as answering the question that mattered.
`harness/replay/anvil_101_depth27_20260816.ndjson` — 90 s, 1,399 frames, priced
by `tools/anvil_frame_economics.py`:

    book frame mean   8,428 B  ->  2,471 B   (Anvil's deployed figure: 2,476 B)
    levels per frame  ~205     ->  60.0      (30 a side — the tier, confirmed)
    90 s of wire      10.4 MB  ->  2.84 MB   = 27.3% of the 2026-08-09 baseline
    sustained         112.6    ->  30.8 KiB/s

against the 23.6 h soak's worst measured hour of 56 KiB/s: **1.8x headroom where
there was 0.5x.** DepthCharge's own pre-measurement predicted 27.8%; this is
27.3%. The tier's cost is real and worth stating precisely because Anvil sized it
as "a few percent": 30 served against 27 rendered is **9.7% of book bytes** the
panel never draws.

NOTE (M3, 2026-08-11) — CLOSED 2026-08-16 BY §4 "Slow consumers". This header
used to carry, at length, what the deployed server does to a slow consumer —
**it queues, without bound** — precisely because §3 and §4 did not say so. They
say so now, in Anvil's own words, with DepthCharge's measured figures cited in
the text: every frame kind thinned by the same fraction, lag rising linearly to
111 s over 150 s with no plateau, ~12.4 MB implied for one socket. The client
rule DepthCharge asked for is now contract rather than folklore: **measure
freshness against your own clock, never infer it from message rate.** The two
consequences that outlive this pin are unchanged and still binding on M4/M5:
(1) that rule itself, and (2) this venue is lossless-but-stale ONLY because
§3.1/§3.2 are idempotent full replaces — the delta feed DepthCharge is still
asking for (backlog A1) would make the same queue lossy, so A1 and A3 part 2 have
to be answered together. Anvil's hand-back agrees and goes further: it warns that
if A3 part 2 is resolved by DROPPING `book` frames, end-to-end gap detection
breaks — benign under full replaces, fatal under deltas — so **A3 must be
decided before A1**, not alongside it. Bounding the queue remains open (A3 pt 2).

NOTE (A2, 2026-08-16) — THE PONG ORDERING IS MEASURED NOW, NOT READ FROM SOURCE,
and DepthCharge's D5 instrument rests on it. §3 Keepalive states that a client
ping is always answered and that the pong is queued behind whatever is already
waiting for that socket. DepthCharge had accepted that from reading Anvil's
vendored Crow and had recorded, in ROADMAP D5, that it was "never captured under
induced backpressure — read the number as evidence rather than as a guarantee".
It has now been captured: Anvil's `tests/tools/pong_ordering_probe.py` stops
reading until frames back up in the send queue, then sends one ping and reports
the pong's POSITION in the byte stream rather than its RTT — because a slow
reader inflates the round-trip whether or not the server reordered anything, so
timing alone cannot separate a server-side queue from a client-side one. Result:
425,890 bytes drained as 149 frames, **the pong at index 148 — last, with zero
frames after it.** Read it as a guarantee. Two things would still silently break
it and are "please don't change" rather than defects: a ping arriving after a
two-way close handshake is never ponged, and the behaviour depends on
`max_payload` staying default with `CROW_ENFORCE_WS_SPEC` undefined. §3 now says
to re-run the probe on a Crow upgrade rather than assume it survived.

NOTE (M0, 2026-07-23) — CLOSED. This header used to say that §1/§4's "monotonic
seq" was not what the deployed server does, the wire `seq` being a single global
counter that is non-monotonic within one ticker's subsequence. **Anvil fixed the
document on 2026-07-26** (`e8d313f`, brief:
`Anvil/docs/briefs/anvil-protocol-seq-reconciliation.md`, raised by this finding)
and the text below now says it directly, in §1 and §4, going further than the ask:
§4 additionally states that ring-overflow loss is not signalled on the wire at all
in v1. The correction stopped being DepthCharge's to carry three weeks before the
2026-08-16 re-pin; that it sat here unnoticed is the argument for re-vendoring on
a schedule rather than on a symptom. Background: `harness/replay/NOTES.md`.

NOTE (M3, 2026-08-09) — **WITHDRAWN 2026-08-11.** This header used to assert that
the deployed server sheds to a slow consumer — `book` frames coalesced per socket,
a slow consumer receiving proportionally fewer messages at an unchanged inter-frame
cadence rather than a queue of stale ones. That conclusion is retracted
(ARCHITECTURE §9, 2026-08-11). It rested on a rate-and-gap measurement, **and a
measurement of rate or of inter-message gap cannot distinguish a shed stream from a
queued one.** The numbers under it (16.95/s at full drain, 8.32/s at a 120 ms
drain, 4.01/s at 250 ms) are all still true; the sentence written under them was
not. Anvil's §4 now carries the correct version, and its own backlog retires the
bullet DepthCharge had put there — with the record of what it claimed, when it was
withdrawn and why, rather than by deletion. It also names the distinction the
original conflated: coalescing DOES exist, but it is the ~14 Hz book tick, which is
upstream and global, not per socket.

NOTE (M3, 2026-08-10) — CLOSED 2026-08-15, client-side, and Anvil is exonerated.
This header used to hold open a single wire-level hypothesis for the board's
~1,281 parse rejects in the first ~60 s of each connect: §1 says "one complete JSON
object each" per frame but does not say *unfragmented*, and a capture cannot see
WebSocket-level fragmentation because the capture tool's library reassembles it
first. The M3 transport rewrite settled it without needing to know: DepthCharge's
own client reads the FIN bit off the wire (the old library's reassembler could not,
and published each fragment separately for the parser to reject), and a day-long
soak on the rewritten client reports **zero `SPLIT@` rejects and a clean parser**.
Nothing here becomes a protocol claim. Detail: `harness/replay/NOTES.md` "M3
addendum — the connect burst", and
`docs/briefs/M3-transport-own-the-websocket-client.md`.
-->

# Anvil Demo — Wire Protocol

**Status:** canonical contract for the live-demo build (branch `rest-interface`).
**Wire version:** `1`. **Bindings (kept in lockstep, this file is the source of truth):**

| Side   | File                                             | Notes                              |
| ------ | ------------------------------------------------ | ---------------------------------- |
| Server | [`server/protocol.hpp`](server/protocol.hpp)     | C++ structs + hand-rolled writers  |
| Client | [`web/src/protocol.ts`](web/src/protocol.ts)     | TypeScript types + parser/guards   |

Any change here must land in both bindings in the same commit. A breaking change
bumps **wire version** (surfaced by `GET /api/health` so a client can detect a
mismatch on connect).

> Scope note: this is the *demo* transport — an unauthenticated, single-shared-book
> "trading floor". The production order-entry gateway (reliable FIX/binary sessions)
> is a named out-of-scope extension point, not this.

---

## 1. Conventions

- **Transport:** REST over HTTP for request/response; a single WebSocket (`GET /ws`)
  for the server→client event stream. The browser never polls the book in steady
  state — it subscribes once and consumes the stream.
- **Encoding:** all bodies and frames are UTF-8 JSON. WebSocket frames are discrete
  text messages, one complete JSON object each (no framing/newlines of our own).
- **Prices are JSON _strings_**, e.g. `"3.2"`, `"7"`, `"6.9"`. The server serialises
  them through the engine's own `append_price`, so a price on the wire is
  byte-identical to the same price in the CLI's trade/dump output (shortest decimal,
  trailing zeros trimmed). Clients render the string verbatim; parse to a number only
  for chart maths, accepting the rounding that implies.
- **Quantities, counts, `seq`, `ts`** are JSON numbers. All stay well under the
  2⁵³ safe-integer ceiling in any realistic demo run (`MAX_QTY` is 10⁹).
- **Sides** are `"B"` / `"S"`, matching the engine's `AggrSide`.
- **Order ids** are the raw id strings (`"A001"`), decoded from the engine's packed
  key — same charset/length the engine validates (`[A-Za-z0-9-]`, ≤10).
- **`seq`** is a **single global engine-thread stamp** carried on every server→client
  frame — *not* a per-connection counter, and not an ordering oracle. See the "single
  global line" note below and [§4 Reconnect](#4-reconnect--idempotency).
- **Ticker scope:** the protocol is ticker-aware (every book/trade frame names its
  `ticker`). A WebSocket subscribes to **one** ticker (`/ws?ticker=`) and receives
  that ticker's `snapshot`/`book`/`trade`; the cross-ticker `summary` frame (§3.5)
  goes to **every** socket regardless. Switching ticker = reconnect with a new
  `?ticker=`. (Phase 8 made this real across feeder + server + UI; the wire shapes
  for `snapshot`/`book`/`trade` were already ticker-scoped and did not change.)
- **`seq` is a single global line.** One engine-thread counter stamps every frame —
  trades, books and the summary across all tickers — so a socket subscribed to one
  ticker sees a *sparse* subsequence of `seq` (the gaps belong to other tickers'
  frames it never receives). It can also **step backwards**. The broadcaster delivers
  from two independent sources and does **not** merge-sort them: the trade ring is
  drained continuously (each fill stamped at *generation*), while the coalesced
  book/summary slots are sampled on the ~14 Hz tick (each stamped at *publish*). A
  frame from either source can therefore be delivered ahead of a lower-`seq` frame
  still queued on the other — a `book` delivered ahead of a lower-`seq` `trade` still
  draining from the ring, or a just-drained `trade` delivered ahead of the lower-`seq`
  `book` that was stamped before it. Both properties make `seq` **unusable for
  per-ticker gap detection**; clients apply frames idempotently and snapshot-heal, so
  this is benign. `seq` values are still globally *unique*, so
  they remain valid as a reconnect watermark and for dedupe (§4) — just never for
  ordering. A delivery-order per-ticker `seq` (a broadcaster merge-sort, or
  per-ticker counters) is the change if strict gap detection is ever wanted — no
  current client needs it.

---

## 2. REST endpoints

### `GET /api/health`

Liveness + contract check. Never requires a body.

```json
{ "status": "ok", "wireVersion": 1, "uptimeMs": 1234567, "clients": 3 }
```

| Field         | Type     | Meaning                                  |
| ------------- | -------- | ---------------------------------------- |
| `status`      | `"ok"`   | constant when serving                    |
| `wireVersion` | number   | server's wire version (compare to yours) |
| `uptimeMs`    | number   | process uptime, milliseconds             |
| `clients`     | number   | connected WebSocket clients              |

### `GET /api/book?ticker=<id>&depth=<n>`

Current book for one ticker, as a **snapshot-shaped** body (the same parser handles it
as a `snapshot` WS frame). `ticker` selects the book (default `ANVIL_DEFAULT_TICKER`).
`depth` is optional and caps the levels per side; **absent or `0` means every level the
server publishes** (`ANVIL_BOOK_DEPTH`, itself `0` = all resting levels by default). An
unknown or quiescent ticker returns empty `bids`/`asks` (not a 404) — idempotent and
simpler for the client.

`depth` only ever **truncates** the published book — see
[§3 `depth`](#depth--per-socket-book-depth) for the full semantics, which are identical
on both surfaces. A value larger than the published depth returns what exists rather
than an error, and a negative or unparseable value is treated as absent.

```json
{ "type": "snapshot", "seq": 42, "ticker": 101,
  "bids": [ { "price": "3.1", "qty": 1500, "orders": 2 } ],
  "asks": [ { "price": "3.2", "qty": 1000, "orders": 1 } ] }
```

### `GET /api/summary`

Cross-ticker roster one-shot (Phase 8): the resting-buy / resting-sell totals and a
`last` (book mid) for **every** ticker, for the initial page load and the summary
view's first paint. Live updates ride the `summary` WS frame (§3.5). Empty `tickers`
before the first publish (not a 404).

```json
{ "tickers": [
  { "ticker": 101, "restingBuy": 1820, "restingSell": 1640, "last": "10.0098" },
  { "ticker": 102, "restingBuy": 900,  "restingSell": 1200, "last": "" }
] }
```

| Field         | Type   | Meaning                                              |
| ------------- | ------ | ---------------------------------------------------- |
| `ticker`      | number | product id                                           |
| `restingBuy`  | number | sum of resting qty across **all** bid levels         |
| `restingSell` | number | sum of resting qty across **all** ask levels         |
| `last`        | string | book mid as a wire decimal ("10.0098"); "" if empty  |

> `restingBuy`/`restingSell` walk **all** levels (a true per-side total), not the
> top-N snapshot. `last` is the book **mid** `(bestBid+bestAsk)/2` — computed
> read-side from the book, zero extra engine state (the implementer's-choice
> alternative to tracking last-trade). Wire-formatted through the engine's
> `append_price`, so it is a JSON **string** like every other price.

### `POST /api/order`

Inject one order. **Body is a raw engine CSV line** (`Content-Type: text/plain`),
fed into the engine's existing `parse_line` — the exact validated path the CLI uses.

**The server owns order-id assignment** (Phase 9). The client never mints an id: it
can't see the global id space (the book snapshot is aggregated *quantity*, not
individual ids) and independent clients can't coordinate, so any client-minted scheme
collides across browser restarts and concurrent users. A single server-side monotonic
allocator mints ids for both the feeder and manual orders.

- **New (`N`)** — the body's **id field is empty** (six fields, the third blank — *not*
  five fields): `101,N,,B,500,10.00`. The server mints an id, splices it into field 3
  before `parse_line`, and returns it as `id`.
- **Cancel (`C`) / Amend (`A`)** — the body carries the **server-assigned id** the
  client received on the New; it passes through unchanged and is echoed back as `id`.

| Type         | Request id field   | Response `id`            |
| ------------ | ------------------ | ------------------------ |
| New (`N`)    | empty              | server-assigned (minted) |
| Cancel (`C`) | server-assigned id | echoed                   |
| Amend (`A`)  | server-assigned id | echoed                   |

The splice replaces field 3's content only — it never adds or removes a field — so a
client that drops the field (five fields) still earns a wrong-column rejection rather
than being silently "repaired". A New rejected for any reason (bad qty/price, wrong
column count) still returns its minted id; the value is simply spent and unused (the
counter only advances — rejects are not reclaimed).

```
POST /api/order
Content-Type: text/plain

101,N,,B,500,10.00
```

Response is an `OrderResult`. **Every engine verdict — accept or reject — returns
`200`** with `{accepted, reason, id}`: the POST was well-formed and reached the engine, so
a reject (even of a garbage CSV line) is a *business outcome in the body*, not an
HTTP-layer error. This mirrors how order entry actually works — a FIX session accepts
the message and the rejection comes back as an execution report, not a session-level
error; the client reads `accepted` from the body. Only genuine *non-verdicts* are
non-2xx, so a client can tell "the engine answered" from "it never got there":
**503** `{"reason":"engine busy"}` when the inbound queue is full and **504**
`{"reason":"engine timeout"}` when the engine did not answer in time (Crow itself
still returns **400** for malformed HTTP and **403** for a blocked origin). Any
resulting trades and book changes are observed asynchronously on the WebSocket stream
— they are **not** in this response.

```json
{ "accepted": true, "id": "o1" }
{ "accepted": false, "reason": "out-of-bounds price", "id": "o2" }
```

#### Session & ownership (Stage 1)

`POST /api/order` establishes a session on first contact. If the request carries no
`anvil_session` cookie, the server mints an opaque 128-bit token and returns it:

```
Set-Cookie: anvil_session=<32-hex>; HttpOnly; SameSite=Lax; Path=/
```

(with `; Secure` added under TLS — `ANVIL_SESSION_COOKIE_SECURE=true`). **Possession of
the cookie is the ownership principal — there is no login and no server-side session
store.** A **New** order is recorded to the calling session. A **Cancel** or **Amend**
is accepted only for an order the calling session *owns*; for any other order — another
session's, **or an unknown id** — the response is

```json
{ "accepted": false, "reason": "cannot modify another participant's order", "id": "o5" }
```

at **`200`** (an ownership reject is a business verdict like any other engine reject, not
an HTTP error), and is **rejected before the engine processes it**. The reason is
**uniform** for "not owner" and "unknown id", so it reveals nothing about which ids are
live — an enumeration walk (`C,o1; C,o2; …`) from a session that owns none of them earns
the same reject on every line. Order ids remain server-minted and sequential (§ above);
the **cookie, not the id, is the ownership boundary**, so guessing an id you do not own
buys nothing. The browser sends and receives the `HttpOnly` cookie transparently on
same-origin requests, so a normal client — which only ever cancels its own ids — sees no
behavioural change.

> The `/ws` stream is read-only market data and is **session-agnostic**: ownership is a
> `POST /api/order` concern exclusively. The feeder's own orders are submitted on a
> trusted internal path (a reserved system principal) that bypasses ownership, so a
> client can never cancel a feeder (`f`-prefixed) id.

### `POST /api/feeder` — _forward-declared (Phase 4), not in the v1 bindings_

Viewer control for the server-side dummy-order feeder.

```json
// request
{ "action": "start", "rate": 30 }
// response
{ "running": true, "rate": 30 }
```

> **Rate is clamped server-side.** A requested `rate` is bounded to
> `ANVIL_FEEDER_MAX_RATE` (default `2000/s`) inside `BasicFeeder::set_rate()` before it
> takes effect, so the response `rate` may be lower than requested. The ceiling is a
> safety cap: `ANVIL_FEEDER_MAX_RATE` = 0 or absent keeps the default ceiling — the cap
> cannot be disabled. The feeder and genuine manual orders share the one bounded inbound
> queue; the cap stops synthetic flow from `503`-ing real orders. See
> `docs/ARCHITECTURE.md` and Stage 0.

---

## 3. WebSocket stream — `GET /ws`

### Handshake

Connect with `GET /ws?ticker=<id>&depth=<n>&since=<seq>` (HTTP upgrade). `ticker`
selects the subscribed ticker (v1: required, single ticker). `depth` is optional and
caps this socket's book depth (below). `since` is **reserved** for sequence-based
replay; v1 has no replay buffer, so the server always resyncs by sending a fresh
`snapshot` regardless of `since`.

On connect the server sends exactly one **`snapshot`** (establishing the `seq`
baseline and the full visible book), then streams `book` and `trade` frames live.

#### `depth` — per-socket book depth

`depth` caps the price levels per side in **this socket's** `snapshot` and `book`
frames. It is negotiated once, on the upgrade request, and cannot be changed without
reconnecting.

| `?depth=` | Levels per side served |
| --------- | ---------------------- |
| absent, or `0` | every level the server publishes — **the default, unchanged** |
| `n` ≤ published depth | **at least** `n`, best-first — `n` rounded up to the next supported tier |
| `n` > published depth | every published level (no error, no padding) |
| negative / unparseable | treated as absent |

**Depth is served in tiers, so you may receive slightly more than you asked for.** The
request is rounded **up** to the next supported depth — `1, 2, 3, 5, 8, 10, 15, 20, 30,
40, 50, 75, 100, 150, 200, 300, 500, 1000`, then unlimited. A client asking for 27 is
served 30. A client needing exactly `n` must truncate locally; you will never be served
*fewer* than you asked for (except where the book simply has fewer levels).

This exists to bound server work: the fan-out formats one frame per *distinct depth in
use*, so free-form depths would let unauthenticated clients dictate how many distinct
frames the server formats per tick. Tiers cap that at a constant no matter how many
sockets connect.

- **It only ever truncates.** The server publishes each ticker at `ANVIL_BOOK_DEPTH`
  (`0` = all resting levels, the deployed default) and a socket slices that published
  view at serialise time. A socket therefore **cannot ask for more depth than the
  server publishes** — deepening would require re-aggregating against live engine
  state, which happens only on the engine thread.
- **It changes the payload, not the stream.** Frame types, cadence, `seq` values and
  the `trade`/`summary` frames are identical on every socket regardless of `depth`; a
  shallow socket receives the same frame *sequence* as a deep one, carrying fewer
  levels. `trade` frames have no depth. The `summary` frame is cross-ticker and
  unaffected.
- **Truncation is a prefix**, so the levels a shallow socket receives are exactly the
  first `n` a deep socket receives — the two views never disagree about a level they
  both carry, and `book` frames stay idempotent full replaces *of the depth requested*
  ([§4](#4-reconnect--idempotency)).
- **Same semantics on `GET /api/book?depth=`**, deliberately: one parameter, one
  meaning, whichever surface you read the book from.

> **Why it exists.** The `book` frame dominates this stream — on the deployed feed it
> is ~98% of all bytes sent, at ~8.4 KB per frame carrying ~200 levels, ~14 times a
> second. A client that renders a shallow ladder (an embedded display, a mobile view, a
> tile) otherwise pays for ~200 levels and discards most of them on arrival. At
> `depth=27` the frame falls to ~2.2 KB — the stream to roughly 28% of its full-depth
> size.
>
> Server-side, the **formatting** work is shared: a ticker's book is serialised once per
> distinct depth in use (in practice one or two — full-depth browsers, one shallow tier
> for boards), not once per socket. Delivery is not shared — each connection still
> receives its own copy of the frame into its own send buffer — so this bounds
> serialisation cost, not bytes on the wire or per-socket memory.

#### Keepalive — client pings are answered, and they measure stream freshness

**The server never initiates a ping, a heartbeat, or any other unsolicited liveness
traffic in v1.** A client must not wait for one, and must not treat a quiet socket as a
dead one — a genuinely idle book produces no frames.

Client-initiated pings are always answered:

- A **ping (opcode `0x9`) is answered with a pong** carrying the same payload,
  unconditionally. No application code is involved, there is no rate limit, and no
  maximum payload size is configured.
- **The pong is queued behind whatever is already waiting for that socket.** It is
  appended to the same per-connection send buffer as `book` and `trade` frames and does
  not jump the queue.

That second property is the useful one, and it makes a client ping the **recommended
way to measure freshness**. Because a pong cannot overtake queued frames, a ping
round-trip measures *true end-to-end stream freshness* rather than mere TCP liveness: a
socket that is 110 seconds behind ([§4](#4-reconnect--idempotency)) gets its pong back
110 seconds late. A transport-level keepalive would answer promptly on a badly
backlogged stream and tell the client nothing — which is exactly the trap the
[slow-consumer note](#slow-consumers--the-server-queues-it-does-not-shed) warns about.

The server does **not** use pongs to detect dead clients, and applies no
application-level idle timeout. The only upper bound on an idle connection is nginx's
`proxy_read_timeout` (3600s in the deployed configuration).

**The ordering is measured, not merely read from the source.** `tests/tools/pong_ordering_probe.py`
opens a raw socket, completes the WebSocket handshake, then deliberately **stops reading**
so the kernel receive buffer fills and frames genuinely back up in the server's
per-connection send queue. It then sends one ping and reports the pong's *position in the
byte stream* — which round-trip timing alone cannot do, since a slow reader inflates the
RTT whether or not the server reordered anything. Captured against the Crow build:

```
not reading for 6.0s to induce backpressure...
drained 425,890 bytes -> 149 frames
pong found at frame index 148
  data frames delivered BEFORE the pong: 148  (425,364 payload bytes)
  frames delivered AFTER the pong:       0
```

The pong arrived **last**, behind every frame queued ahead of it. Re-run it after any
change to the egress path.

> **Implementation note for whoever upgrades Crow.** This behaviour comes from the
> vendored Crow build — its frame handler answers opcode `0x9` directly — not from
> Anvil's own code. The build pins `crow_all.h` by SHA256, so it cannot change silently,
> but a Crow upgrade is an upgrade of a **documented wire promise**: re-run the probe
> above rather than assuming it survived.

### Envelope

Every frame is a JSON object with a `type` discriminator and a `seq`:

```ts
{ "type": "snapshot" | "book" | "trade", "seq": <number>, ... }
```

> **The v1 WS stream carries `snapshot` / `book` / `trade` only — no `error` frame.**
> A rejected order's verdict is the `POST /api/order` response, not a broadcast: a
> shared market-data feed shouldn't carry one participant's input errors to every
> watcher. Stream-integrity loss (a fill dropped on ring overflow) is **not signalled
> on the wire at all** in v1 — not by an error frame, and not by a detectable `seq`
> gap, since a single-ticker socket's `seq` subsequence is already sparse and
> non-monotonic (§1). The book self-heals from the next full-replace `book`/`snapshot`
> and the trade tape is best-effort; see [§4](#4-reconnect--idempotency). The `error`
> shape below is retained as a **reserved** type — both bindings still parse it
> defensively — for the documented override in which the
> server *deliberately* broadcasts engine rejects (`WsPublishSink::kEmitErrorFrames`).

### 3.1 `snapshot`

Authoritative on-connect / resync baseline for one ticker. Applying it **fully
replaces** the client's view of that ticker's book. `GET /api/book` returns this
same shape.

```json
{"type":"snapshot","seq":1,"ticker":101,"bids":[{"price":"3.1","qty":1500,"orders":2},{"price":"3","qty":800,"orders":1}],"asks":[{"price":"3.2","qty":1000,"orders":1},{"price":"3.3","qty":2200,"orders":3}]}
```

| Field             | Type          | Meaning                                            |
| ----------------- | ------------- | -------------------------------------------------- |
| `seq`             | number        | reconnect-watermark baseline (global stamp — §1)   |
| `ticker`          | number        | the ticker this book is for                        |
| `bids`            | `LevelView[]` | top-N levels, **best-first** (highest price first) |
| `asks`            | `LevelView[]` | top-N levels, **best-first** (lowest price first)  |
| `LevelView.price` | string        | wire decimal at this level                         |
| `LevelView.qty`   | number        | summed resting quantity at this price              |
| `LevelView.orders`| number        | count of resting orders at this price              |

> The aggregate `qty`/`orders` are computed by the read-side helper that walks each
> level's FIFO. The engine's `Level` stores **no** running total — a deliberate
> omission documented in the engine README; the snapshot helper is exactly the kind
> of consumer that would justify adding one later.
>
> **"top-N" is per socket.** N is `min(?depth=, ANVIL_BOOK_DEPTH)`, where `0` on either
> means "no limit from this one" — see [`depth`](#depth--per-socket-book-depth). Two
> sockets on the same ticker can legitimately receive different-length `bids`/`asks`
> for the same publish generation; both are correct and agree level-for-level as far as
> the shallower one goes.

### 3.2 `book`

A coalesced top-N **refresh** for one ticker, published on the server's ~10–15 Hz
tick. Identical payload to `snapshot`; it carries the latest full top-N (not a
delta), so it is idempotent — apply it as a full replace of the ticker's book.

```json
{"type":"book","seq":7,"ticker":101,"bids":[{"price":"3.1","qty":1500,"orders":2}],"asks":[{"price":"3.2","qty":1000,"orders":1}]}
```

### 3.3 `trade`

One fill, streamed **individually** (never coalesced) so the trade tape is complete.
`price` is the resting (maker) order's price — the trade price, per the settled
matching semantics.

```json
{"type":"trade","seq":8,"ticker":101,"price":"3.2","qty":400,"aggr":"B","takerId":"A002","makerId":"A001","ts":1718480000000}
```

| Field     | Type   | Meaning                                  |
| --------- | ------ | ---------------------------------------- |
| `ticker`  | number | ticker                                   |
| `price`   | string | resting order's price = trade price      |
| `qty`     | number | fill quantity                            |
| `aggr`    | `"B"`/`"S"` | aggressor side                      |
| `takerId` | string | aggressor (incoming) order id            |
| `makerId` | string | resting order id that was filled         |
| `ts`      | number | server wall-clock, epoch milliseconds    |

### 3.4 `error` — reserved, *not emitted by the v1 server*

The v1 WS stream does **not** broadcast `error` frames (see the note under
[Envelope](#envelope)): a rejected `POST /api/order` is the POST's HTTP response, and
overflow loss is not signalled on the wire at all ([§4](#4-reconnect--idempotency)) —
the `"resync"` code below is reserved for it, not emitted. The shape is retained here
as a **reserved** type — both bindings still parse it — for the documented override in
which the server deliberately broadcasts engine rejects
(`WsPublishSink::kEmitErrorFrames`). When emitted, `raw` and `ticker` are omitted
when absent.

```json
{"type":"error","seq":9,"code":"rejected","message":"out-of-bounds price","raw":"101,N,A003,B,1,200000","ticker":101}
```

| Field     | Type   | Meaning                                                    |
| --------- | ------ | --------------------------------------------------------- |
| `code`    | string | machine code: `"resync"` \| `"rate_limited"` \| `"rejected"` … |
| `message` | string | human-readable reason (an engine reason string forwards here) |
| `raw`     | string | offending input line, if any (omitted when absent)        |
| `ticker`  | number | ticker scope, if any (omitted when absent)                |

---

## 4. Reconnect & idempotency

- `seq` is the **single global engine-thread stamp** of [§1](#1-conventions), not a
  per-connection counter. A single-ticker socket sees a *sparse and non-monotonic*
  subsequence, so a client **cannot compute a "next expected `seq`"**. Track the
  last-applied `seq` only as the reconnect watermark (below), never for ordering.
- **Recovery is transport-driven, not `seq`-driven.** A reconnect is triggered by the
  socket closing, **never** by an unexpected `seq`. On reconnect the fresh `snapshot`
  is the new baseline; discard any buffered frame with `seq ≤` the snapshot's `seq`.
  Because every `snapshot`/`book` is a full replace, the book self-heals and a missed
  frame needs no client-side detection.
- **Idempotent book frames:** `snapshot` and `book` both carry the full top-N, so
  reapplying one is harmless — it is a full replace of the ticker's visible book.
- **Trade tape:** `trade` frames are append-only; on a reconnect overlap, dedupe by
  `seq`. This stays valid because `seq` values are globally *unique* even though they
  do not arrive in order — dedupe is a set membership test, not a comparison.
- **Ring-overflow loss is not signalled on the wire in v1.** When the engine→broadcaster
  ring is full the fill is dropped and a server-side `stale` latch is set; the
  broadcaster clears that latch without emitting anything. There is no `resync` frame,
  and the resulting `seq` gap is **not** client-detectable (per §1 the subsequence is
  already sparse and non-monotonic, so a gap is indistinguishable from another
  ticker's frame). The book is unaffected — the next `book`/`snapshot` is a full
  replace — and the **trade tape is best-effort**: dropped fills are lost silently. The
  reserved `error` frame with code `"resync"` ([§3.4](#34-error--reserved-not-emitted-by-the-v1-server))
  is the wire shape held for making this explicit; the v1 server does not emit it.
- v1 has no server-side replay buffer; resync = a fresh `snapshot`. A bounded replay
  window keyed by `seq` is a natural later addition that needs no protocol change.

### Slow consumers — the server queues, it does not shed

A client that consumes slower than the server publishes gets **every frame, late**. It
does *not* get a thinned, sampled or per-socket-coalesced stream. State this plainly
because the opposite is the intuitive guess and it is wrong:

- **There is no per-socket backpressure policy of any kind.** `Broadcaster::deliver()`
  calls Crow's `conn.send_text()`, which posts the frame onto that connection's
  io_context and appends it to the connection's private `write_buffers_`
  (`std::vector<std::string>`), drained by `do_write()`. On that path there is **no
  cap, no drop rule, no coalescing and no disconnect threshold**. The queue is
  **unbounded in v1** — it grows for as long as the client stays connected and behind.
- **The coalescing that does exist is upstream and global, not per socket.** The
  ~14 Hz book tick ([§3.2](#32-book)) collapses many book changes into one frame *for
  everybody*; that is what bounds egress independent of match rate. A slow socket is
  offered exactly the same frame sequence as a fast one and simply falls behind in it.
- **Therefore lag is unbounded and grows linearly, with no plateau.** A client draining
  at a fraction *f* of full rate accumulates staleness at `(1 − f)` seconds per second
  for as long as it is connected. Measured externally against a matched wire `seq`: a
  socket drained at ~25% of full rate reached **111 s of lag over 150 s (+0.745 s/s,
  linear, no plateau)** with an implied server-side backlog of ~1,746 messages /
  ~12.4 MB still growing at disconnect. Every frame kind thinned by the *same* fraction
  — a delayed byte stream, not per-kind coalescing.

**Client rule: measure freshness against your own clock, never infer it from message
rate.** A reduced arrival rate with roughly-preserved inter-frame cadence is exactly
what a *delayed* stream looks like, so rate and gap statistics cannot distinguish a
shed stream from a queued one — only a comparison against local time can. A client that
cares about freshness must timestamp on arrival and act on the delta itself. (An
application-level ping whose reply the client times works too, and measures true
end-to-end stream freshness rather than TCP liveness: a WebSocket pong is appended to
the *same* per-connection buffer as data, so during a backlog it arrives *behind* the
queued frames rather than jumping them.)

**Why this is currently safe, and what it is coupled to.** The stream is
lossless-but-stale rather than lossy *only* because `snapshot` and `book` are
idempotent full replaces: a client that falls behind and reconnects heals completely
from the next baseline, so lateness never becomes corruption. That property is
load-bearing for two future changes and neither is free:

- **Bounding the queue** (dropping frames or closing the socket past a threshold)
  trades staleness for loss, which is safe *today* — the next full replace heals a
  dropped `book` — but is not safe against a delta stream.
- **A delta / incremental feed** over this same unbounded queue would be **lossy**, not
  merely late: unlike a full replace, a missed delta never heals. A delta mode
  therefore has to arrive with its own gap detection and resync path, and with a slow
  consumer disconnected or snapshot-reset rather than queued indefinitely.

---

## 5. Phase status

| Area                                   | Status (branch `rest-interface`) |
| -------------------------------------- | -------------------------------- |
| Types + serialisers (both bindings)    | **Phase 0 — done**               |
| `EventSink`/`FillEvent` engine seam    | **Phase 1 — done**               |
| Engine thread, coalesced publication   | **Phase 2 — done**               |
| Crow REST + WS transport               | **Phase 3 — done**               |
| Feeder + `POST /api/feeder`            | Phase 4                          |
| Browser client consuming this contract | Phase 5                          |
