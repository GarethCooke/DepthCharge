#!/usr/bin/env python3
"""Capture Binance spot depth traffic to an NDJSON replay trace.

DepthCharge M5 stage 0 ground-truth capture -- the third venue, after
``capture_anvil.py`` (M0) and ``capture_kraken.py`` (M4 stage 0). Connects to
the **market-data-only** host ``wss://data-stream.binance.vision``, records every
frame verbatim, and -- this is the new part -- also records the REST depth
snapshots the diff stream is meaningless without.

Output format -- the same contract as the other two tools:

  line 1  : metadata object  {captured_at, url, venue, symbol, tool_version, ...}
  line 2+ : one per record   {"rx_ns": <monotonic-ns>, ... , "frame": <verbatim JSON>}

Frames are stored **verbatim**, for the same reason as at Kraken but arrived at
from the opposite direction. Kraken puts bare JSON numbers on the wire and
``json.loads`` destroys their trailing zeros. Binance quotes every price and
quantity as a **string** (``"78564.00000000"``), which does survive a round
trip -- so verbatim storage is not load-bearing for the *numbers* here. It is
still load-bearing for the *bytes*: every figure in
``binance_frame_economics.py`` is a byte count of what the venue sent, and a
re-serialised frame would be a measurement of Python's separator defaults. It is
also load-bearing for a claim this venue makes and Kraken does not -- that the
quoted decimals carry exactly the precision ``exchangeInfo`` declares -- which is
a statement about text and can only be checked against text.

WHAT IS NEW HERE, AND WHY THE RECORD SHAPE HAD TO GROW
------------------------------------------------------
Two things reach this trace that have never been in a DepthCharge trace before,
and neither is a WebSocket text frame:

  1. **A REST response.** ``@depth`` is a *diff* stream. Replayed on its own it
     is ungradeable: without the ``/api/v3/depth`` snapshot it is bracketed
     against there is no book for the diffs to be applied to, and no
     ``lastUpdateId`` to reconcile them with. So the snapshot has to be in the
     file, or the file is not ground truth for anything.

  2. **A WebSocket control frame.** Binance's server pings every 20 s and closes
     the socket if no pong arrives inside 60 s. Recording that a ping arrived
     and a pong went back is the only evidence this project can have that the
     pong path works against a venue that enforces it -- and a ping payload is
     arbitrary bytes, not JSON, so the existing line shape cannot hold one.

**The record shape this tool proposes** (M5 decides; the reader is deliberately
NOT built this evening):

    a venue text frame  {"rx_ns": N, "frame": <verbatim JSON>}
    a frame we sent     {"rx_ns": N, "dir": "tx", "frame": <verbatim JSON>}
    a REST fetch        {"rx_ns": N, "kind": "rest", "req": {...}, "frame": <body>}
    a control frame     {"rx_ns": N, "kind": "control", "ctl": {...}, "frame": null}

One optional key, ``kind``, placed before ``frame`` exactly where ``dir`` already
sits. **Absent means what every existing record already is**: a JSON text frame
the venue sent over the WebSocket. That is the same additivity rule the ``venue``
metadata tag was granted at M4 stage A, and it is chosen for the same reason --
the four Anvil traces and the six Kraken ones must stay byte-for-byte what they
are, and they do, because nothing in them acquires a key.

**Where the pressure runs, stated rather than hidden.** M4 stage A's rule was
*the reader learns the wire; the wire does not learn the reader*. There is no
wire here to learn. A REST body is not something the venue said; it is **a fetch
this client chose to make**, and its meaning is inseparable from the request --
which symbol, which ``limit``, at which instant. So the honest record carries the
request too, in ``req``, and that is a genuinely new kind of thing for this format
to hold: every other line in every other trace is a transcript, and this one is a
transcript plus a question. The alternative shapes were a second stream (rejected:
two files that must be read together are two files that can be separated) and
synthesising the snapshot into the metadata header (**already rejected at M4 stage
A** and not re-proposed -- a derived baseline would be the first thing in
``harness/replay/`` no venue ever sent).

``rx_ns`` ON A REST RECORD MEANS SOMETHING SLIGHTLY DIFFERENT, AND IT MUST
--------------------------------------------------------------------------
The fetch runs on a worker thread, because a 200 ms blocking HTTP round trip
inside the read loop would insert a 200 ms hole into the very inter-message gap
distribution this capture exists to measure. The result is written by the main
loop when it next comes round, and its ``rx_ns`` is stamped **at that moment** --
not when the body landed. That is deliberate and it is not sloppiness: ``rx_ns``
is what orders the file, and ``TraceReader`` rejects a trace outright at the first
backwards step. ``capture_kraken.py``'s header records that exact bug being found
by replaying a capture rather than by writing one. A body that landed mid-frame
would step backwards if stamped honestly, so the true timings are kept where they
cannot corrupt the ordering: ``req.sent_ns`` and ``req.recv_ns`` carry the fetch's
own span, and ``rx_ns`` carries its position in the stream.

RATE LIMITS -- READ BEFORE ADDING A RETRY LOOP
-----------------------------------------------
300 connection attempts per 5 minutes per IP. 5 *incoming* messages per second on
a connection. 1,024 streams per connection. A connection to the stream host is
valid for 24 hours and is then closed by the venue. REST ``/api/v3/depth`` costs
IP weight by tier: 5 for ``limit`` <= 100, 25 to 500, 50 to 1,000, and **250** for
1,001-5,000 against a 6,000/minute budget. This tool therefore defaults to
``--limit 100`` (weight 5), makes one connect per cycle, and never retries a
refused handshake. The docs' own worked example polls at ``limit=5000``; do not
copy it. A ban costs the evening.

No API credential is required by anything here and none is to exist in this
repository at any point. ``data-stream.binance.vision`` and
``data-api.binance.vision`` serve public market data only.

Usage:
    python tools/capture_binance.py --symbol BTCUSDT \
        --streams depth@100ms,depth20@100ms --duration 90 \
        --snapshot-every 10 --out _local/binance_btcusdt_d100ms.ndjson

Python 3 stdlib only. Lives in tools/ (Python is allowed nowhere else).
"""
from __future__ import annotations

import argparse
import base64
import collections
import json
import queue
import ssl
import sys
import threading
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone

from wsclient import (OP_CLOSE, OP_PING, OP_PONG, HandshakeRejected, WsClient,
                      install_sigint, stop_requested)

TOOL_VERSION = "0.1.0"
USER_AGENT = "depthcharge-capture/" + TOOL_VERSION

# perf_counter_ns for the same measured reason capture_kraken.py gives: on this
# box monotonic_ns advances in 15.0 ms steps, and Binance's @100ms cadence would
# be reported as its own quantisation. Recorded in every trace's metadata header,
# because a gap figure is only readable beside its clock.
CAPTURE_CLOCK = time.perf_counter_ns
CAPTURE_CLOCK_NAME = "perf_counter_ns"

# Market-data-only hosts. Not stream.binance.com / api.binance.com.
DEFAULT_WS_HOST = "wss://data-stream.binance.vision"
DEFAULT_REST_HOST = "https://data-api.binance.vision"

# /api/v3/depth IP weight by limit tier, documented 2026-08-24. Recorded in the
# trace so a later reader can price a capture's REST cost without re-reading the
# docs, and printed at the end so an operator sees it before running again.
def depth_weight(limit: int) -> int:
    if limit <= 100:
        return 5
    if limit <= 500:
        return 25
    if limit <= 1000:
        return 50
    return 250


OP_NAMES = {OP_PING: "ping", OP_PONG: "pong", OP_CLOSE: "close"}


def stream_url(host: str, symbol: str, streams: list[str], combined: bool) -> str:
    """`/ws/<stream>` for one stream, `/stream?streams=a/b` for several.

    Binance names its streams IN THE URL PATH rather than in a subscribe message,
    which is why this tool may send no application frame at all -- the single
    largest structural difference from `capture_kraken.py`, and the reason the
    `dir: "tx"` record can be absent from a perfectly complete Binance trace.
    """
    low = symbol.lower()
    names = [f"{low}@{s}" for s in streams]
    if len(names) == 1 and not combined:
        return f"{host}/ws/{names[0]}"
    return f"{host}/stream?streams=" + "/".join(names)


class RestFetcher:
    """Fetches /api/v3/depth off the read loop and hands results back by queue.

    One worker thread, one outstanding fetch at a time. Serialising the fetches
    is not a simplification for its own sake: it makes the used-weight header
    readable in order, and it means a slow response can never stack up requests
    against an IP weight budget while nobody is looking.
    """

    def __init__(self, rest_host: str, symbol: str, limit: int, clock):
        self.url = (f"{rest_host}/api/v3/depth?symbol={symbol}&limit={limit}")
        self.limit = limit
        self.clock = clock
        self.results: queue.Queue = queue.Queue()
        self._busy = threading.Lock()

    def request(self) -> bool:
        """Start a fetch if none is in flight. Returns True if one was started."""
        if not self._busy.acquire(blocking=False):
            return False
        threading.Thread(target=self._run, daemon=True).start()
        return True

    def _run(self) -> None:
        sent_ns = self.clock()
        record = {"method": "GET", "url": self.url, "limit": self.limit,
                  "weight": depth_weight(self.limit), "sent_ns": sent_ns}
        body = None
        try:
            req = urllib.request.Request(self.url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(req, timeout=15) as resp:
                raw = resp.read()
                record["status"] = resp.status
                # Binance reports the IP weight it has charged this minute. Worth
                # recording: it is the only in-band evidence of how close a
                # capture ran to a ban, and it costs nothing to keep.
                used = resp.headers.get("x-mbx-used-weight-1m")
                if used is not None:
                    record["used_weight_1m"] = used
                body = raw.decode("utf-8")
        except urllib.error.HTTPError as exc:
            record["status"] = exc.code
            record["error"] = exc.reason if isinstance(exc.reason, str) else str(exc.reason)
        except Exception as exc:  # noqa: BLE001 -- a capture must not die of a fetch
            record["status"] = 0
            record["error"] = f"{type(exc).__name__}: {exc}"
        record["recv_ns"] = self.clock()
        self.results.put((record, body))
        self._busy.release()


def frame_kind(obj) -> str:
    """A short label for the stderr tally. Never used by any downstream reader.

    Handles both stream shapes: a combined-stream frame is
    `{"stream": ..., "data": {...}}` and a single-stream frame is the bare
    payload. The wrapper is UNWRAPPED for counting only -- what gets written to
    the trace is the verbatim text either way, wrapper included, because whether
    the wrapper is worth its bytes is one of the questions this capture exists to
    answer and a tool that strips it has destroyed the evidence.
    """
    if not isinstance(obj, dict):
        return "?"
    if "stream" in obj:
        inner = obj.get("data")
        name = str(obj["stream"]).split("@", 1)[-1]
        if isinstance(inner, dict) and "e" in inner:
            return f"{name}:{inner['e']}"
        return f"{name}:partial" if isinstance(inner, dict) else name
    if "e" in obj:
        return str(obj["e"])
    if "lastUpdateId" in obj:
        return "partial"          # a bare partial-depth payload has no event type
    if "result" in obj or "id" in obj:
        return "ack"
    return "?"


def capture(args) -> int:
    install_sigint()
    captured_at = datetime.now(timezone.utc).isoformat()
    streams = [s.strip() for s in args.streams.split(",") if s.strip()]
    if not streams:
        sys.stderr.write("[capture] --streams is empty\n")
        return 2
    url = stream_url(args.ws_host, args.symbol, streams, args.combined)

    fetcher = RestFetcher(args.rest_host, args.symbol, args.limit, CAPTURE_CLOCK)
    frame_count = 0
    rest_count = 0
    control_counts: collections.Counter[str] = collections.Counter()
    kind_counts: collections.Counter[str] = collections.Counter()
    connects = 0
    skipped_multiline = 0
    last_used_weight = None

    # Control frames are observed on the client's read path and queued here; the
    # main loop writes them, so the file has exactly one writer and rx_ns cannot
    # go backwards. `_send_frame` has already put the pong on the wire by the
    # time this runs -- the venue is never waiting on a queue.
    controls: queue.Queue = queue.Queue()

    def on_control(opcode, payload, recv_ns, replied_ns):
        controls.put({
            "op": OP_NAMES.get(opcode, f"op{opcode:#x}"),
            "recv_ns": recv_ns,
            "pong_ns": replied_ns,
            "payload_len": len(payload),
            "payload_b64": base64.b64encode(payload).decode("ascii"),
        })

    with open(args.out, "w", encoding="utf-8", newline="\n") as out:
        def drain(now_ns: int) -> None:
            """Write everything the side channels have produced, in arrival order.

            Called between messages. `rx_ns` is `now_ns` for every record written
            here -- see this file's header on why a REST body is not stamped with
            the instant it landed.
            """
            nonlocal rest_count, last_used_weight
            while True:
                try:
                    ctl = controls.get_nowait()
                except queue.Empty:
                    break
                control_counts[ctl["op"]] += 1
                out.write('{"rx_ns": %d, "kind": "control", "ctl": %s, "frame": null}\n'
                          % (now_ns, json.dumps(ctl)))
                out.flush()
                if ctl["op"] == "ping":
                    sys.stderr.write(
                        "\n[capture] server PING (%d payload bytes); pong sent %.3f ms later\n"
                        % (ctl["payload_len"],
                           (ctl["pong_ns"] - ctl["recv_ns"]) / 1e6
                           if ctl["pong_ns"] else float("nan")))
            while True:
                try:
                    req, body = fetcher.results.get_nowait()
                except queue.Empty:
                    break
                if "used_weight_1m" in req:
                    last_used_weight = req["used_weight_1m"]
                if body is None or "\n" in body or "\r" in body:
                    # A failed fetch is recorded, not dropped: "the snapshot did
                    # not arrive" is a fact about the capture window.
                    out.write('{"rx_ns": %d, "kind": "rest", "req": %s, "frame": null}\n'
                              % (now_ns, json.dumps(req)))
                else:
                    out.write('{"rx_ns": %d, "kind": "rest", "req": %s, "frame": %s}\n'
                              % (now_ns, json.dumps(req), body))
                    rest_count += 1
                out.flush()

        client = WsClient(url, origin=args.origin, timeout=args.connect_timeout,
                          user_agent=USER_AGENT, clock=CAPTURE_CLOCK,
                          on_control=on_control)
        client.connect()
        connects += 1

        meta = {
            "captured_at": captured_at,
            "url": url,
            "venue": "binance",
            "symbol": args.symbol,
            "streams": streams,
            "combined": len(streams) > 1 or bool(args.combined),
            "rest_url": fetcher.url,
            "rest_limit": args.limit,
            "rest_weight_per_fetch": depth_weight(args.limit),
            "snapshot_every_s": args.snapshot_every,
            "tool_version": TOOL_VERSION,
            "clock": CAPTURE_CLOCK_NAME,
            "capture_mode": "reconnect" if args.cycles > 1 else "baseline",
            "cycles": max(1, args.cycles),
            "origin_sent": args.origin,
            "handshake_status": client.handshake_status,
        }
        out.write(json.dumps(meta) + "\n")
        out.flush()

        # The opening snapshot goes out before the first frame is read, so the
        # buffered-diff procedure the venue documents is satisfiable from this
        # file: every capture opens with one.
        fetcher.request()

        cycles = max(1, args.cycles)
        per_cycle = args.duration if cycles == 1 else (args.reconnect_after or args.duration)
        try:
            for cycle in range(cycles):
                if cycle > 0:
                    if stop_requested():
                        break
                    gap = max(args.reconnect_gap, 1.0)
                    sys.stderr.write(f"\n[capture] reconnect after {gap}s gap\n")
                    gap_end = time.monotonic() + gap
                    while time.monotonic() < gap_end and not stop_requested():
                        time.sleep(0.25)
                    if stop_requested():
                        break
                    client = WsClient(url, origin=args.origin,
                                      timeout=args.connect_timeout, user_agent=USER_AGENT,
                                      clock=CAPTURE_CLOCK, on_control=on_control)
                    client.connect()
                    connects += 1
                    # A reconnect is exactly when a real client must re-snapshot,
                    # so the capture does what the client would.
                    fetcher.request()

                deadline = time.monotonic() + per_cycle
                next_snapshot = (time.monotonic() + args.snapshot_every
                                 if args.snapshot_every > 0 else None)
                try:
                    for rx_ns, text in client.messages(deadline):
                        if stop_requested():
                            break
                        try:
                            obj = json.loads(text)
                        except json.JSONDecodeError:
                            sys.stderr.write("[capture] skipping non-JSON frame\n")
                            continue
                        if "\n" in text or "\r" in text:
                            skipped_multiline += 1
                            sys.stderr.write("[capture] skipping frame containing a newline\n")
                            continue
                        # Side channels first, so a record that logically
                        # preceded this frame is written before it.
                        drain(rx_ns)
                        out.write('{"rx_ns": %d, "frame": %s}\n' % (rx_ns, text))
                        frame_count += 1
                        kind_counts[frame_kind(obj)] += 1
                        if frame_count % 200 == 0:
                            out.flush()
                            sys.stderr.write(
                                f"[capture] {frame_count} frames {dict(kind_counts)}\r")
                        now = time.monotonic()
                        if next_snapshot is not None and now >= next_snapshot:
                            if fetcher.request():
                                next_snapshot = now + args.snapshot_every
                        if args.max_frames and frame_count >= args.max_frames:
                            raise KeyboardInterrupt
                except (ConnectionError, ssl.SSLError) as exc:
                    sys.stderr.write(f"\n[capture] connection ended: {exc}\n")
                finally:
                    client.close()
                if stop_requested() or (args.max_frames and frame_count >= args.max_frames):
                    break
        except KeyboardInterrupt:
            pass
        finally:
            # Anything still in flight when the deadline hit belongs in the file.
            time.sleep(0.3)
            drain(CAPTURE_CLOCK())
            out.flush()

    sys.stderr.write("\n")
    sys.stderr.write(
        f"[capture] done: {frame_count} frames, kinds={dict(kind_counts)}, "
        f"rest={rest_count}, control={dict(control_counts) or '{}'}, "
        f"skipped_multiline={skipped_multiline}, connects={connects}\n")
    if last_used_weight is not None:
        sys.stderr.write(f"[capture] last reported x-mbx-used-weight-1m: "
                         f"{last_used_weight} (budget 6000/min)\n")
    if not control_counts:
        sys.stderr.write("[capture] NOTE: no control frame arrived in this window. "
                         "The venue pings every ~20 s; a shorter capture may miss it.\n")
    sys.stderr.write(f"[capture] wrote {args.out}\n")
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--ws-host", default=DEFAULT_WS_HOST,
                   help="market-data WebSocket host (NOT stream.binance.com)")
    p.add_argument("--rest-host", default=DEFAULT_REST_HOST,
                   help="market-data REST host (NOT api.binance.com)")
    p.add_argument("--symbol", default="BTCUSDT", help="Binance spot symbol, e.g. BTCUSDT")
    p.add_argument("--streams", default="depth@100ms",
                   help="comma-separated stream suffixes, e.g. 'depth@100ms,depth20@100ms'. "
                        "The symbol is prefixed automatically.")
    p.add_argument("--combined", action="store_true",
                   help="force the /stream?streams= wrapper even for one stream, so the "
                        "wrapper's cost can be measured against the bare shape")
    p.add_argument("--out", required=True, help="output NDJSON path")
    p.add_argument("--duration", type=float, default=90.0, help="capture seconds")
    p.add_argument("--snapshot-every", type=float, default=0.0,
                   help="seconds between REST depth snapshots (0 = opening one only). "
                        "Each costs IP weight -- see this file's header.")
    p.add_argument("--limit", type=int, default=100,
                   help="REST depth limit. Weight tiers: <=100 costs 5, <=500 costs 25, "
                        "<=1000 costs 50, above that 250.")
    p.add_argument("--cycles", type=int, default=1,
                   help="connect cycles (>1 => a deliberate mid-capture reconnect trace)")
    p.add_argument("--reconnect-after", type=float, default=0.0,
                   help="seconds per cycle before reconnecting (reconnect mode)")
    p.add_argument("--reconnect-gap", type=float, default=3.0,
                   help="seconds disconnected between cycles")
    p.add_argument("--origin", default=None, help="Origin header to send (default: none)")
    p.add_argument("--max-frames", type=int, default=0, help="stop after N frames")
    p.add_argument("--connect-timeout", type=float, default=20.0)
    args = p.parse_args(argv)
    try:
        return capture(args)
    except HandshakeRejected as exc:
        sys.stderr.write(f"[capture] handshake rejected: {exc.status_line}\n")
        sys.stderr.write("[capture] NOT retrying -- see the rate-limit note in this "
                         "file's header before running again.\n")
        return 2
    except OSError as exc:
        sys.stderr.write(f"[capture] network error: {exc}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
