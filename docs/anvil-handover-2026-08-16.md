# Anvil hand-over — what DepthCharge is waiting on

**Date:** 2026-08-16 · **From:** DepthCharge · **To:** an Anvil session
**Status:** ready to hand over. Nothing here has been applied Anvil-side.

**Who is asking.** DepthCharge — an ESP32-S3 driving a 64×64 HUB75 panel that renders Anvil's
live book on a desk. It is Anvil's second independent client and the first that is not a browser
on the same continent: **transatlantic (87 ms median RTT, from 8 TCP connects to
`52.204.246.224` — ICMP to that host is filtered, so a `ping` will tell you nothing), 512 KB of
internal SRAM with PSRAM disabled (120–170 KB free at runtime), and 27 levels a side of visible
ladder** (`kDisplayLevels`, `engine/include/depthcharge/display_snapshot.hpp`). A desk capture
tool has been on the live feed since 2026-07-23; the board itself since 2026-08-09, and it has
just finished a 23.6-hour continuous soak.

**Scope and manners.** DepthCharge does not modify Anvil; this is a note of what one external
client is waiting on, working around, or has measured from the outside. Nothing is urgent except
by DepthCharge's own clock. Items are ordered **cheapest first**, and the cheapest one is the one
that matters most — which is a change from every previous version of this ask.

The IDs (`A1`…`A7`) are the ones DepthCharge's `ROADMAP.md` backlog uses, so citing one works in
both repos.

**On the existing backlog bullet** at the tail of Anvil's `docs/anvil-plan.md` (`5779836`): its
**central clause is withdrawn** — coalescing with cadence preserved — but the rest of it stands.
Its ~8.3 msg/s measurement is still true, and its closing sentence about deltas is correct and is
agreed with below. Detail in A3.

---

## The measurement that reorders everything

Two committed captures of `wss://anvil.garethcooke.com/ws?ticker=101`, taken two weeks apart
(`harness/replay/anvil_101_baseline.ndjson`, 2026-08-01, 1,406 frames / 90 s;
`anvil_101_baseline_20260809.ndjson`, 2026-08-09, 1,513 frames / 90 s). Byte counts are the
compact JSON as sent, capture wrapper excluded — not an estimate: re-serialising each captured
frame reproduces its bytes exactly, which the tool's `--verify` mode checks frame by frame.
**The two captures agree to within a percentage point on every ratio below**; the 08-09 numbers
are quoted.

| frame type | count | bytes      | mean      | share of wire |
| ---------- | ----: | ---------: | --------: | ------------: |
| `book`     | 1,210 | 10,198,672 | **8,428** |     **98.2%** |
| `summary`  |   181 |    157,369 |       869 |          1.5% |
| `trade`    |   121 |     16,305 |       134 |          0.2% |
| `snapshot` |     1 |      8,165 |     8,165 |          0.1% |

Three facts follow, and each of them is an ask below.

1. **The `book` frame is 98% of the wire, and it carries ~205 levels** (mean 210 in this
   capture; per frame, bids range 96–131 and asks 80–119, so 188–231 in total).
   `ANVIL_BOOK_DEPTH=0` — "all resting levels" — is the documented default and
   is deliberate: the web client's `BookLadder.tsx` renders every resting level and scrolls, so a
   far-from-touch manual limit stays visible. **DepthCharge renders 27 a side and discards the
   rest.** It is paying transatlantic bandwidth, ~13.4 times a second, for ~150 levels per frame
   that it throws away on arrival.
2. **Between consecutive `book` frames, a median of one level changes.** Mean 1.1, p90 2, p99 3,
   **max 4 across the entire 90-second capture** (max 5 in the other one). Roughly 99% of every
   frame is a retransmission of levels that did not move.
3. Therefore: truncating to DepthCharge's rendered depth costs **26.5%** of current `book` bytes;
   a delta encoding costs **1.3%**.

*Reproduce it yourself:* `tools/anvil_frame_economics.py` in this repo, against either committed
capture. The delta figure keeps Anvil's exact JSON level shape, re-sends a whole level when any
field moves, and adds an explicit `qty:0` removal entry per vanished price — it is an upper
bound, and a real encoder would beat it.

*(This replaces "a tenth the bytes", an estimate DepthCharge has quoted so often it began to read
as a result. It was a ~8× under-claim.)*

---

## A7 · A `depth` parameter on `/ws` — new, cheapest, and it should be done first

**Ask:** let a socket say how deep a book it wants — `GET /ws?ticker=101&depth=32` — and truncate
that socket's `snapshot`/`book` frames to it. Default unchanged (`0` = all levels), so **the web
client is untouched and its scroll-to-a-far-limit behaviour is preserved.**

**Why this is the whole fix, arithmetically.** At `depth=27` the `book` frame falls from 8,428 to
2,232 bytes and the entire stream to **27.8% of its current size.** Against the 30-minute desk
measurement of the unthrottled stream on 2026-08-11 — 15.70 msg/s, **110.4 KiB/s** — that is
≈31 KiB/s. DepthCharge's 23.6-hour soak sustained **56–87 KiB/s at every hour of the day**, floor
56 mid-morning. **A stream at 28% of today's fits inside the worst hour with roughly 2× headroom.
That closes DepthCharge's staleness problem outright, today, with no protocol redesign, no
sequencing, no resync path and no change to any existing client.**

*(Units, since we have tripped over this ourselves: every byte-rate figure in this note is
**binary** — KiB/s. `tools/anvil_freshness_probe.py` divides by 1024 and prints the result
labelled "KB/s"; so does the firmware. The labels are wrong and the arithmetic is consistent.)*

**We have read the objection you already wrote down, and we think it does not reach this ask.**
`server/app.hpp`'s `/api/book` comment says the `?depth` param is accepted but the published book
governs, *"because re-aggregating at an arbitrary depth would need an engine-thread round-trip,
which `snapshot_top_n` forbids off-thread"*. That is right, and it is an objection to
**re-aggregation** — asking for a depth the publisher did not compute. This ask is the opposite
and strictly weaker: **the published ladder is already sorted best-first, so a shallower view is
a prefix slice of bytes you have already computed.** No engine round-trip, no re-aggregation, and
`depth > published` can simply be clamped to what was published.

**Why it is small.** The parameter name, concept and validation already exist on the REST side.
`/ws`'s `onaccept` already parses `?ticker=` out of the upgrade request and stashes it in the
connection userdata (`server/app.hpp`), so `depth` would ride the same path. The frames are built
in `Broadcaster` from `market_.latest_book(ticker)`.

**The one real design question is yours, not ours:** the periodic fan-out currently serialises a
ticker's `book` **once** and delivers the same bytes to every subscriber, and per-socket depth
breaks that. The cheap answer is to serialise once **per distinct depth in use** and share within
the group — in practice one full-depth frame for the web clients and one shallow frame for the
boards. `Subscriber` already carries per-socket state (`ticker`), and the fan-out already
iterates. If even that is unwelcome, a coarser version solves DepthCharge's problem just as well:
**one alternate depth, opt-in by flag** (`?shallow=1`), fixed at build or config time.

**Nothing in DepthCharge needs changing to consume it.** The adapter already accepts 83–126
levels a side and truncates above `kMaxSnapshotLevels = 256` with the truncation counted; a
shallower book is trivially within contract. And a per-socket `N` arguably makes PROTOCOL §3.1's
existing "top-N" wording *true* rather than changing it.

**If A7 lands, A1 stops being urgent** and becomes what it should always have been: the right
long-term design, done properly, on Anvil's own schedule.

---

## A6 · One line in `deploy/README.md` — the other short item

DepthCharge pins **ISRG Root X1** as its sole TLS trust anchor
(`firmware/src/anvil_root_ca.hpp`). To be clear about whose constraint this is: **it pins one
root by choice, not by necessity.** The ESP-IDF certificate bundle became reachable when the
transport moved off `esp_websocket_client` to a hand-built `esp_tls_cfg_t`
(`firmware/src/ws_transport.cpp`, which has `crt_bundle_attach` available and a framework
shipping `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` with 200 certs); switching is a small change
DepthCharge has not made. *That is exactly why this item is a notification and not a plea.*

The chain `anvil.garethcooke.com` presents today:

```text
0  CN=anvil.garethcooke.com   issued by  Let's Encrypt YE1
1  Let's Encrypt YE1          issued by  ISRG Root YE
2  ISRG Root YE               issued by  ISRG Root X2
3  ISRG Root X2               issued by  ISRG Root X1   <-- the pinned anchor
```

Routine certbot renewals are invisible to the board **so long as the renewed leaf still chains up
to ISRG Root X1**. What breaks it is a change of CA, or **any shortening of the presented
chain** — note there are *two* cross-sign hops above, and the YE←X2 one is the newer and likelier
of the two to be retired. The failure is loud on DepthCharge's serial console and **silent on
Anvil's side**: a dead panel, with nothing in your logs to say why.

**Ask:** a note in `deploy/README.md` beside the certbot step — *"a DepthCharge board pins ISRG
Root X1 as its only trust anchor; changing CA or chain configuration requires a lock-step
firmware update — tell DepthCharge first."* If you would rather it not depend on anyone
remembering, `deploy/` currently pins nothing about the chain (`README.md`'s step is a bare
`certbot --nginx -d <SUBDOMAIN>`, and there are no TLS settings in `anvil.env.example`,
`redeploy.sh` or the systemd unit), so an explicit `--preferred-chain` would survive a certbot
default change in a way a README line would not.

---

## A3 · The per-socket send queue — correct one clause, document the behaviour, then decide

**First, the correction we owe you.** The backlog bullet's central clause — a slow socket
"receives coalesced book frames at a reduced rate with inter-frame cadence roughly preserved…
Lossless" — came from DepthCharge on 2026-08-09 and **DepthCharge withdrew it on 2026-08-11**
(`ARCHITECTURE.md` §9): a rate-and-gap measurement cannot distinguish a *shed* stream from a
*queued* one, and this one is queued. **The bullet's other two statements stand** — its
~8.3 msg/s measurement is still good, and its closing sentence ("lossless *because*
`snapshot`/`book` are idempotent full-replaces; a delta/incremental feed cannot be") is correct,
which is precisely why A1 below asks for deltas as an **opt-in frame alongside** the existing
full-replace `book`, never as a replacement. Sorry for leaving a wrong sentence on your backlog
for a week.

**What DepthCharge measured** (`tools/anvil_freshness_probe.py` — two sockets from one process,
one drained flat out and one sleeping 250 ms per message, matched on the wire `seq` so staleness
is a subtraction against a real clock rather than an inference from a rate):

- Every frame kind thinned by the **same** fraction (`book` 25.5%, `summary` 25.7% of full rate) —
  a delayed byte stream, not per-kind coalescing.
- Lag rising **linearly to 111 s over 150 s, no plateau**: +0.745 s/s measured, +0.745 s/s
  predicted from the drain fraction alone.
- Implied queue for that one socket at disconnect: **~1,746 messages, ≥12.4 MB, still growing.**
  A floor rather than a figure — see the two-stage point below. Separately, a DepthCharge board
  displayed a state **96–98 s old at 210 s of uptime** (that number includes the board's own
  pipeline, so read it as corroboration, not as a measurement of your queue).

**Visible in Anvil's own source, so it needs no re-measurement to document.**
`CrowWsSubscriber::deliver()` (`server/ws_registry.hpp`) calls `conn.send_text()`, which
`asio::post`s onto the connection's io_context and appends to
`crow::websocket::Connection::write_buffers_` — a `std::vector<std::string>` in the **vendored**
`server/third_party/crow_all.h`, drained by `do_write()` swapping into `sending_buffers_`. Four
things follow that we think you would want to know:

- **To pre-empt the obvious reply: yes, you coalesce — but upstream of this.** `Broadcaster`
  collapses many book changes into one frame per 70 ms tick, and `ws_sink`'s ring drops trades on
  overflow. **That bounds the production *rate*; nothing bounds the accumulated *total* once a
  socket stalls.** `do_write()`'s entire flow-control policy is "if a write is already in flight,
  return" — there is no else-branch, and no `.size()` test on `write_buffers_` anywhere in the
  file.
- **Two unbounded stages, not one:** the io_context handler queue (each posted message owning a
  full `std::string` copy) and then `write_buffers_`/`sending_buffers_` — and `deliver()`
  allocates a fresh copy per subscriber. Hence "≥12.4 MB".
- **Growth is unconditional, not activity-gated.** `publish_books()` stamps a new `seq` every
  coalesce tick, so the broadcaster's unchanged-book guard never trips: a stalled client accrues
  ~14 book + ~2 summary frames a second **on a completely idle market**. This is "any client on a
  quiet Sunday", not "a pathological client".
- **The failure is silent as well as unbounded.** `deliver()` is `try { … } catch (...) {}` and
  `Subscriber::deliver` returns `void` and is `noexcept`, so a subscriber **structurally cannot**
  signal backpressure — no log, no metric, nothing at any layer while the queue grows. And
  `onopen` has no admission check (`onaccept` tests only the Origin allowlist), so the exposure is
  N × queue with N uncapped.

**Ask, in two parts:**

1. **Document it in `PROTOCOL.md`** — a "slow consumers" note in §3 or §4: the server queues, it
   does not shed; a slow client receives *every* frame, late; the queue is unbounded in v1; a
   client must measure freshness against its own clock and never infer it from message rate. This
   matters beyond DepthCharge, and it is **coupled to A1**, for the reason your own bullet gives.
2. **Decide whether to bound it.** Owner's call, and arguably Stage-1 hardening rather than a doc
   item. The usual shapes: cap the per-socket queue and drop `book` frames on overflow (safe
   *today*, because the next full replace heals it — and A7's shallow frames make any cap bite far
   later), or close the socket past a threshold and let the client reconnect into a fresh
   snapshot. **DepthCharge would rather be disconnected than served 100-second-old data**; its
   supervisor already recycles a socket that goes silent for five minutes and reconnects clean.
   A pass condition DepthCharge can verify from outside, if you want one: *a socket that stops
   reading is either shed to the latest `book` or closed within N seconds* — happy to re-run
   `tools/anvil_freshness_probe.py` against it.

---

## A2 · Heartbeat / keepalive — mostly answered, with one thing worth your eyes

**Reason 1 — liveness — DepthCharge now believes it can solve on its own side.** Its panel greys
when the book stops republishing (an ~80 ms-republish liveness watchdog), which cannot on its own
distinguish a quiet-but-live book from a dead socket. Reading your vendored Crow: a client PING
is answered with a PONG with no application code (control frames never reach `onmessage`), and
the pong goes into the *same* `write_buffers_` as data.

**Stated precisely, because the loose version is wrong and the wrong version would mislead us
both:** the pong **cannot overtake any frame already queued on that connection**, so a round-trip
measures **this connection's server-side write-path backlog** — strictly stronger than TCP
liveness, and blind to producer-side lag. Frames still upstream in the broadcaster have not been
posted yet, and a pong *will* be delivered ahead of them. So if the fan-out itself is what lags —
which is exactly the reason-2 failure below — the round-trip stays fast while the data is stale.
Two more honesty notes: the ordering is one-sided (a data frame posted concurrently with the
ping's arrival may land either side of the pong), and this is **read from your source, never
captured under induced backpressure** — a cheap experiment we are happy to run.

**Ask (small), and framed as what it is — not "confirm you already guarantee this", because you
never claimed it:** we would like to depend on stock Crow behaviour you neither configured nor
tested. So: *please treat "a client ping is answered, and the pong is not reordered ahead of
already-queued frames" as a contract going forward*, and say so in `PROTOCOL.md` §3. It rests on
Crow pinning each connection to one io_context run by one worker thread, so it constrains your
threading model — worth knowing before someone runs multiple threads over one io_context. Two
things would silently break it, so treat them as "please don't change" rather than as defects: a
ping arriving after a two-way close handshake is never ponged, and the behaviour depends on
`max_payload` staying at its default with `CROW_ENFORCE_WS_SPEC` undefined. **If confirmed,
DepthCharge withdraws the request for a server-side heartbeat** and builds the client half
itself.

**Reason 2 — one stall you may want to know about.** 2026-08-16 00:12: the WS endpoint went
**silent for 2 min 56 s on a live TCP connection and then resumed on the same connection.** Plain
HTTPS to the same host answered 200 in ~0.5 s throughout. The board held the socket and went LIVE
the instant bytes resumed — this is why its silence-recycle threshold is 5 minutes rather than 15
seconds. **One occurrence, one board, never reproduced.**

*What DepthCharge can and cannot say about it:*

- **No nginx timeout matches this symptom in kind.** Every nginx timeout *terminates* the
  connection; none stalls and resumes it. The connection stayed up and the stream resumed on it.
  Separately, 176 s matches none of nginx's defaults (60 s proxy_read/send/connect, 75 s
  keepalive). For completeness the `/ws` location in your version-controlled template sets
  `proxy_read_timeout 3600s` and `proxy_send_timeout 3600s` — but that template has only a
  `listen 80` block and unfilled placeholders, and the board connects over TLS on 443 in a block
  certbot writes on the host, so we are not relying on it.
- **A healthy HTTPS request on a *different* connection cannot prove *this* flow was healthy.**
  The evidence does not separate "the WS server or broadcaster stalled" from "one transatlantic
  flow stalled in RTO backoff". We are not claiming your server wedged.
- **One thing only you can check:** the `/ws` location sets **no buffering directive at all** — no
  `proxy_buffering off` (the directive usually added for streaming), no `proxy_buffer_size`/
  `proxy_buffers`, no `tcp_nodelay` — so the effective values come from your host's `http{}`
  block, which is not in the repo. What does it set for `proxy_buffering` and `tcp_nodelay`?
- **The instrument that would settle it next time is the one A3 part 2 already needs:** a server
  log line at a queue-depth threshold. That makes A2 and A3 one piece of work rather than two
  asks.

---

## A1 · Sequenced incremental L2 feed — still right, no longer the cheapest way to fix DepthCharge

**Downgraded from "the thing that makes the hardware work at all" to "the right long-term
design", by A7.** DepthCharge had promoted this to blocking; its own committed captures then
showed a query parameter gets most of the benefit for a fraction of the work. **Please do A7
first and judge A1 on its own merits.**

On those merits it is still the best answer — **1.3% of current `book` bytes, against A7's
26.5%** — and DepthCharge's own side is out of levers: it rebuilt `liblwip.a` from the shipped
ESP-IDF v4.4.6 vintage to raise `TCP_WND` 5744 → 17232 (now its default build), and the
23.6-hour soak says that was load-bearing — 56–87 KiB/s all day, lag slope down from +0.57 s/s to
**+0.083 s/s over the final 10.9 h**, with the RX loop instrumented and exonerated (`wait 0 /
read 98–99 / feed 0` every hour: the board is bound by how fast bytes arrive, not by anything it
does with them).

**This is not Stage 5, and should not wait for it.** Stage 5 (Emit) is an ITCH-flavoured *binary*
message set over LAN **UDP multicast** with A/B arbitration, gated behind Stage 4 — and your plan
still lists the transport detail (raw multicast vs a thin MoldUDP64 clone) as an open decision.
Either way it is binary multicast, not JSON over WebSocket. Three facts from your own plan make
the case better than we could: the as-built log records completion through **Stage 3** with no
Stage 4 entry; the plan calls Stages 4–5 *"months of evenings"*; and Open decisions still asks
whether the 4–6 arc is a commitment at all, or whether Stages 0–3 are the shippable portfolio
with 4–6 as a next chapter. Folding this ask into Stage 5 attaches it to work that may never be
scheduled. (Stage 4's delta work is inbound anyway — an ITCH parser for canned sample files
feeding the engine, not a client-facing feed.)

**Minimum shape, from the outside:**

- **A per-ticker monotonic book sequence — the load-bearing prerequisite.** PROTOCOL §1 already
  names it as "the change if strict gap detection is ever wanted", and your broadcaster's header
  nominates the same two options (a merge-sort, or per-ticker counters). The global `seq` cannot
  serve, and we are not asking it to: your own docs call it a dedupe watermark and explicitly not
  an ordering oracle. Unlike a full replace, **a missed delta never heals.**
- **Diffs against the last published top-N** for that ticker: changed and removed levels only.
- **An anchored baseline on subscribe — and this is the part that needs real care.**
  `Broadcaster::add_subscriber()` delivers `market_.latest_book()` under `subs_mu_`, the same
  mutex the fan-out holds, so the new socket starts from a specific, non-torn publish generation
  and no single frame's fan-out can be split across its registration. **That is where the
  guarantee stops.** Both producers *sample* outside the mutex and lock only for the delivery
  loop (`emit_books()` reads `latest_book()` before taking the lock; `drain_ring()` pops before
  taking it), so a frame stamped *before* the new subscriber's snapshot can be delivered *after*
  it. For a full-replace feed that is a harmless one-tick rewind. **For a delta feed it is
  corruption** — a delta computed against generation N applied by a client holding N+1. So the
  ask is the anchoring itself: either sample the book inside `subs_mu_`, or add the per-ticker
  counter so a delta client can reject anything at or below its baseline.
- **A baseline is not always sent.** `add_subscriber()` delivers nothing when the ticker has never
  published or is unknown (and `ticker == 0` means no book stream), so a delta consumer connecting
  before the first publish tick gets no baseline **and no signal that it is missing one**.
- **A resync path** — on a gap, a fresh snapshot, asked for or pushed. Given A3, the honest
  pairing is that a client too slow for the delta stream is disconnected or snapshot-reset rather
  than queued.
- **Opt-in** (`/ws?ticker=101&mode=delta`) so the browser client is untouched, and so the
  full-replace guarantee your backlog bullet relies on stays exactly as it is for everyone who
  does not ask for deltas.

**Suggested order if it happens:** the per-ticker sequence **first, on its own**, with
full-replace frames still going out. That alone is independently useful, it is the piece PROTOCOL
already anticipates, and it lets deltas be added later without a second protocol change.

---

## A4 · Chaos flag — deterministic gap testing

A runtime switch to inject drop / reorder / duplicate on the WS stream, so a client can prove its
recovery path deliberately instead of waiting for weather. `POST /api/feeder` is the precedent for
a runtime control knob, and Stage 5 already plans fault injection for its own verification, so
this is mostly pulling a Stage 5 tool forward. Nice-to-have alone; **close to required alongside
A1**, whose gap-recovery path is otherwise untestable.

## A5 · Feeder realism

Hawkes-clustered arrivals / mirror mode / a FrontierView execution-algo participant, in place of
the current uniform-rate drifting mid with occasional crossers. DepthCharge is happy to be the
test client. No DepthCharge work is blocked on it.

---

## Housekeeping

**The backlog itself.** Anvil's backlog is one bullet at the tail of `docs/anvil-plan.md`. Worth
the same treatment DepthCharge's `ROADMAP.md` backlog got: a section with stable IDs, one item
per item, closed items kept at the bottom rather than deleted. Using A1–A7 would make citations
work in both directions, which is the only reason to suggest someone else's naming.

**Already done — verify and close, no work.** The **`seq` reconciliation** shipped (`e8d313f`,
2026-07-26; brief at Anvil's `docs/briefs/anvil-protocol-seq-reconciliation.md`). §1, §3.4 and §4
now tell one story, and DepthCharge has confirmed §1's sparse-and-non-monotonic property directly
on the wire — 42 backward steps in 5 minutes at M0, which is what raised the ask. §4's new
ring-overflow-loss clause is *not* something a client can confirm, by construction; it is taken on
trust and it is a better statement than the one the brief proposed. **No action.** DepthCharge's
vendored copy was three weeks stale against it and was re-pinned on 2026-08-16 — for the record,
the pin is `e8d313f`, not HEAD (`5779836` is a docs-only commit that does not touch
`PROTOCOL.md`).
