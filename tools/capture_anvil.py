#!/usr/bin/env python3
"""Capture Anvil live WebSocket traffic to an NDJSON replay trace.

DepthCharge M0 ground-truth capture. Connects to the Anvil demo WebSocket
(``wss://anvil.garethcooke.com/ws?ticker=<id>``) and records every received
frame verbatim, one JSON object per line, for deterministic replay in the host
harness.

Output format (matches the M0 brief):

  line 1  : metadata object  {captured_at, url, ticker, tool_version, ...}
  line 2+ : one per received frame  {"rx_ns": <monotonic-ns>, "frame": <verbatim JSON>}

Frames are stored **verbatim**: the exact JSON text the server sent is spliced
into the ``frame`` field with no re-serialisation, so key order and number
formatting are byte-preserved. Each line is still a single valid JSON object
because every Anvil WS frame is itself one complete JSON object.

Deliberately dependency-free: a small RFC 6455 client over ``ssl`` + ``socket``
from the standard library only. The M0 brief suggested the ``websockets``
package, but the owner's box has no ``pip``/``ensurepip``, and a stdlib-only
tool is strictly more portable ("no exotic dependencies"). The trade-off is a
hand-rolled framer; it only has to read server->client text/close/ping frames
and answer pings, which is a small, well-tested surface. See the M0 session log.

**That client now lives in ``tools/wsclient.py``**, extracted at M4 stage 0 so
``capture_kraken.py`` uses the same one rather than a second copy. The move was
made under the brief's condition -- this tool's output must stay byte-identical
across it -- and that was checked by replaying a committed trace at both
versions from a loopback WS server, not assumed. The method and the result are
in ``wsclient.py``'s header. Nothing in the capture format, the metadata header,
the Origin probe or the reconnect cycle changed.

Reconnect trace: ``--cycles N --reconnect-after S`` opens the socket N times,
capturing S seconds each, so the trace contains N ``snapshot`` frames and the
per-connection ``seq`` baseline reset is on record without touching the host
network.

Python 3 stdlib only. Lives in tools/ (Python is allowed nowhere else).
"""
from __future__ import annotations

import argparse
import collections
import json
import ssl
import sys
import time
from datetime import datetime, timezone
from urllib.parse import urlsplit

from wsclient import HandshakeRejected, WsClient, install_sigint, stop_requested

TOOL_VERSION = "0.1.0"
# The exact handshake header this tool has always sent. Spelled out here rather
# than left to a default in wsclient, so the extraction cannot silently change
# what the server sees.
USER_AGENT = "depthcharge-capture/" + TOOL_VERSION


# -- capture orchestration ----------------------------------------------------


def _connect_with_origin_probe(url: str, origin: str | None, timeout: float):
    """Connect, transparently retrying with a nominated Origin if the server
    rejects an Origin-less upgrade. Returns (client, effective_origin)."""
    client = WsClient(url, origin=origin, timeout=timeout, user_agent=USER_AGENT)
    try:
        client.connect()
        return client, origin
    except HandshakeRejected as exc:
        client.close()  # tear the first socket down cleanly before retrying
        if origin is not None:
            raise
        host = urlsplit(url).hostname
        fallback = f"https://{host}"
        sys.stderr.write(
            f"[capture] Origin-less upgrade rejected ({exc.status_line}); "
            f"retrying with Origin: {fallback}\n"
        )
        client = WsClient(url, origin=fallback, timeout=timeout, user_agent=USER_AGENT)
        client.connect()
        return client, fallback


def capture(args) -> int:
    install_sigint()
    captured_at = datetime.now(timezone.utc).isoformat()
    # `depth` is omitted entirely when 0 rather than sent as `&depth=0`, because
    # those are the same request to the server and the shorter one is what every
    # trace captured before 2026-08-16 was taken with. Keeping the URL
    # byte-identical in the default case is what lets a new capture be compared
    # against the committed ones without the URL itself being a variable.
    url = f"{args.url}?ticker={args.ticker}"
    if args.depth > 0:
        url += f"&depth={args.depth}"

    frame_count = 0
    kind_counts: collections.Counter[str] = collections.Counter()
    reconnects = 0
    skipped_multiline = 0
    effective_origin = args.origin
    origin_note = None

    with open(args.out, "w", encoding="utf-8", newline="\n") as out:
        # Metadata header line (line 1). We stream frames as we go, so it is
        # written up front with the fields known at connect time; the per-run
        # summary goes to stderr. The achieved cycle count is recoverable from
        # the frames themselves — snapshot_count is the number of connections
        # and mid_stream_snapshots the number of reconnects, both of which
        # harness/src/trace.cpp already computes.
        cycles = max(1, args.cycles)
        per_cycle = args.duration if cycles == 1 else (args.reconnect_after or args.duration)

        # Establish the first connection to resolve the Origin question before
        # writing metadata (so the header records what actually worked).
        client, effective_origin = _connect_with_origin_probe(
            url, args.origin, timeout=args.connect_timeout
        )
        if args.origin is None and effective_origin is not None:
            origin_note = "server required a nominated Origin header"
        elif args.origin is None:
            origin_note = "server accepted upgrade with no Origin header"

        meta = {
            "captured_at": captured_at,
            "url": url,
            "ticker": args.ticker,
            "tool_version": TOOL_VERSION,
            "capture_mode": "reconnect" if cycles > 1 else "baseline",
            "cycles": cycles,
            "origin_sent": effective_origin,
            "origin_note": origin_note,
            "handshake_status": client.handshake_status,
        }
        out.write(json.dumps(meta) + "\n")
        out.flush()

        try:
            for cycle in range(cycles):
                if cycle > 0:
                    if stop_requested():
                        break
                    if args.reconnect_gap > 0:
                        sys.stderr.write(
                            f"[capture] simulated drop: {args.reconnect_gap}s gap\n"
                        )
                        gap_end = time.monotonic() + args.reconnect_gap
                        while time.monotonic() < gap_end and not stop_requested():
                            time.sleep(0.25)  # stay responsive to Ctrl-C
                        if stop_requested():
                            break
                    client, effective_origin = _connect_with_origin_probe(
                        url, effective_origin, timeout=args.connect_timeout
                    )
                    reconnects += 1
                    sys.stderr.write(f"[capture] reconnect #{reconnects}\n")

                deadline = time.monotonic() + per_cycle
                try:
                    for rx_ns, text in client.messages(deadline):
                        if stop_requested():
                            break
                        # Verbatim: validate it parses, but write original text.
                        try:
                            obj = json.loads(text)
                        except json.JSONDecodeError:
                            sys.stderr.write("[capture] skipping non-JSON frame\n")
                            continue
                        # The whole file format is one JSON object per line, and
                        # the frame text is spliced in unescaped. A pretty-printed
                        # frame parses perfectly and would still split the record
                        # across several lines. Every downstream reader rejects
                        # the result loudly, so this is belt and braces — but the
                        # invariant is one line to state and free to keep.
                        if "\n" in text or "\r" in text:
                            skipped_multiline += 1
                            sys.stderr.write("[capture] skipping frame containing a newline\n")
                            continue
                        out.write('{"rx_ns": %d, "frame": %s}\n' % (rx_ns, text))
                        frame_count += 1
                        kind = obj.get("type", "?") if isinstance(obj, dict) else "?"
                        kind_counts[kind] += 1
                        if frame_count % 200 == 0:
                            out.flush()
                            sys.stderr.write(
                                f"[capture] {frame_count} frames {dict(kind_counts)}\r"
                            )
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
            out.flush()

    sys.stderr.write("\n")
    sys.stderr.write(
        f"[capture] done: {frame_count} frames, kinds={dict(kind_counts)}, "
        f"skipped_multiline={skipped_multiline}, "
        f"reconnects={reconnects}, origin_sent={effective_origin!r}, "
        f"handshake={client.handshake_status!r}\n"
    )
    sys.stderr.write(f"[capture] wrote {args.out}\n")
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--url", default="wss://anvil.garethcooke.com/ws",
                   help="WS base URL (ticker query is appended)")
    p.add_argument("--ticker", type=int, default=101, help="ticker id to subscribe")
    # A7, live on the deployed server 2026-08-16. The board asks for 27 — its
    # kDisplayLevels — and this tool has to be able to ask for the same thing, or
    # a captured trace stops being a recording of what the firmware subscribes to
    # and the goldens quietly describe a different stream.
    #
    # DEPTH IS SERVED IN TIERS: 1,2,3,5,8,10,15,20,30,40,50,75,100,150,200,300,
    # 500,1000,unlimited. A request rounds UP, never down, so `--depth 27` is
    # served 30 levels a side. That is the server's contract, not this tool's
    # rounding, and it is why the trace a `--depth 27` capture produces carries 30.
    p.add_argument("--depth", type=int, default=0,
                   help="per-socket book depth (A7); 0 or absent = every published level. "
                        "Rounded UP server-side to a supported tier, so 27 is served 30.")
    p.add_argument("--out", required=True, help="output NDJSON path")
    p.add_argument("--duration", type=float, default=300.0,
                   help="capture seconds (single-connection baseline)")
    p.add_argument("--cycles", type=int, default=1,
                   help="number of connect cycles (>1 => reconnect trace)")
    p.add_argument("--reconnect-after", type=float, default=0.0,
                   help="seconds per cycle before reconnecting (reconnect mode)")
    p.add_argument("--reconnect-gap", type=float, default=0.0,
                   help="seconds to stay disconnected between cycles, simulating "
                        "a network drop so the gap is visible in rx_ns")
    p.add_argument("--origin", default=None,
                   help="Origin header to send (default: none, to probe the "
                        "server's allowlist; auto-retries with https://<host> "
                        "if an Origin-less upgrade is rejected)")
    p.add_argument("--max-frames", type=int, default=0, help="stop after N frames")
    p.add_argument("--connect-timeout", type=float, default=20.0)
    args = p.parse_args(argv)
    try:
        return capture(args)
    except HandshakeRejected as exc:
        sys.stderr.write(f"[capture] handshake rejected: {exc.status_line}\n")
        return 2
    except OSError as exc:
        sys.stderr.write(f"[capture] network error: {exc}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
