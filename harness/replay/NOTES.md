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

`gzip -c <file> | wc -c` is recorded because it is the realistic proxy for what
git actually stores after zlib+delta — ~350 KiB for both traces combined, versus
~17 MiB raw. The book frames are ~8 KB and highly repetitive, so they compress
~50×. **This sets the trace-commit policy before Kraken traces arrive at M4:**
capture long locally, commit a compact sliced window, quote the gzip size.

Full local captures (git-ignored, `_local/`): baseline 4658 frames / 300 s /
~30 MB; reconnect 1836 frames / ~124 s / ~15 MB.

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
