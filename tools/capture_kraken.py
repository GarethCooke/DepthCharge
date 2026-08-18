#!/usr/bin/env python3
"""Capture Kraken live WebSocket v2 book traffic to an NDJSON replay trace.

DepthCharge M4 stage 0 ground-truth capture -- M0's ``capture_anvil.py``
repeated for the second venue. Connects to ``wss://ws.kraken.com/v2``, sends one
``book`` subscription, and records every frame verbatim, one JSON object per
line, for pricing and (later) deterministic replay.

Output format -- the same contract as ``capture_anvil.py``:

  line 1  : metadata object  {captured_at, url, symbol, depth, tool_version, ...}
  line 2+ : one per received frame  {"rx_ns": <monotonic-ns>, "frame": <verbatim JSON>}

Frames are stored **verbatim**: the exact JSON text the server sent is spliced
into the ``frame`` field with no re-serialisation, so key order and number
formatting are byte-preserved. That is load-bearing at this venue in a way it
never was at Anvil. Kraken v2 puts price and qty on the wire as bare JSON
numbers rather than quoted strings, and the CRC32 checksum is computed over the
*decimal text as sent*; a trace that had been through ``json.loads`` and back
would be a recording of Python's float formatting, not of Kraken's -- and the
checksum computed from it would not be Kraken's either. Invariant #3 (no float
ever touches book data) has to be satisfiable from this file alone.

**This tool sends.** ``capture_anvil.py`` is a pure consumer; a Kraken book
subscription is a client->server frame, so the RFC 6455 client gained
``send_text()`` (masking already existed for the close frame). The subscribe is
written into the trace like any other frame, because it is what provoked
everything after it and a trace that omits it does not record what was asked
for. It carries one extra key -- ``"dir": "tx"`` -- and **received frames carry
no such key**, so the Anvil line shape is untouched. The direction marker is not
decoration: without it a pricing tool counts our own upload as venue bytes and
an inter-message gap distribution grows one bogus interval at the head. Readers
that only want the stream filter on its absence.

The client itself is ``tools/wsclient.py``, shared with ``capture_anvil.py``
rather than cloned. The M4 stage-0 brief allowed a clone only if extraction
moved a byte of the Anvil tool's output; it did not, and that was checked by
replaying a committed trace at both versions (method and result in
``wsclient.py``'s header).

**What is still duplicated, stated rather than left to be discovered.** The
*capture loop* below -- the message iteration, the parse-and-write-verbatim
step, the newline guard, the flush cadence and the frame tally -- is roughly
sixty lines that closely resemble ``capture_anvil.py``'s. It was left duplicated
at M4 stage 0, deliberately and with a reason each way. Against extracting it:
the two loops differ in three places that are not parameters (this one sends a
subscribe and records it; that one runs an Origin probe and a reconnect cycle;
the frame tally keys on different fields because the venues shape their frames
differently), and the brief's binding constraint was that ``capture_anvil.py``'s
output must not move, which a third refactor of it that same evening does not
make more likely. For extracting it: a third venue arrives at M5, and two
copies becoming three is where this stops being a judgement call. **M5 should
extract it, or say why not.**

**Rate limits -- read before adding a retry loop.** Cloudflare fronts this
endpoint and limits *connection attempts* to roughly 150 per rolling 10 minutes
per IP, with a ban on breach. This tool therefore makes **one connect per
capture** and never reconnects on failure. ``--cycles`` exists for a deliberate
reconnect trace and enforces a minimum gap between cycles. A ban costs an
evening; a missing capture costs a minute.

Usage:
    python tools/capture_kraken.py --symbol BTC/USD --depth 25 \
        --duration 90 --out _local/kraken_btcusd_d25.ndjson

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

from wsclient import HandshakeRejected, WsClient, install_sigint, stop_requested

TOOL_VERSION = "0.2.0"   # 0.2.0: --resubscribe-after, the provoked mid-stream snapshot (M4 B2)
USER_AGENT = "depthcharge-capture/" + TOOL_VERSION

# rx_ns is stamped from perf_counter_ns, not monotonic_ns. Both are monotonic;
# on this box (Python 3.12, Windows 11) monotonic_ns advances in 15.0 ms steps
# and perf_counter_ns in 0.2 us ones, and Kraken's inter-message gaps on a
# liquid pair are smaller than 15 ms -- so the coarse clock would have reported
# a distribution made of its own quantisation. The M3 addendum in
# harness/replay/NOTES.md recorded that quantisation in an Anvil capture and
# proposed raising the process timer resolution as the cure; measured
# 2026-08-16, that does NOT work -- timeBeginPeriod(1) leaves monotonic_ns at
# 15.0 ms. The clock is the lever, not the timer period. capture_anvil.py
# deliberately keeps monotonic_ns, so its traces stay comparable with the four
# committed ones. The choice is recorded in every trace's metadata header rather
# than only here, because a gap figure is only readable beside its clock.
CAPTURE_CLOCK = time.perf_counter_ns
CAPTURE_CLOCK_NAME = "perf_counter_ns"

# Kraken's documented book depths, read from the WebSocket v2 docs on
# 2026-08-16. Anything else is *sent anyway* and the server's answer recorded --
# A7 taught this project that a venue's depth parameter is a request, not a
# contract, and the useful artefact is what came back, not what a client-side
# table predicted would come back.
DOCUMENTED_DEPTHS = (10, 25, 100, 500, 1000)

# One connect per capture. Cycles > 1 is a deliberate reconnect trace and still
# has to stay far away from the Cloudflare attempt limit.
MIN_RECONNECT_GAP_S = 5.0


def subscribe_text(symbol: str, depth: int) -> str:
    """The subscribe frame, spelled the way the firmware would spell it.

    Compact separators and this key order are what goes on the wire and what is
    recorded; `json.dumps` of a dict literal is used rather than an f-string so
    the symbol cannot inject unescaped JSON.
    """
    return json.dumps({
        "method": "subscribe",
        "params": {
            "channel": "book",
            "symbol": [symbol],
            "depth": depth,
            "snapshot": True,
        },
    }, separators=(",", ":"))


def unsubscribe_text(symbol: str, depth: int) -> str:
    """The unsubscribe frame. Same shape as the subscribe, minus `snapshot`.

    Sent only by `--resubscribe-after`, and sent BEFORE the re-subscribe rather
    than instead of it: Kraken answers a second subscribe on a channel it already
    holds, and what it answers is not documented. Unsubscribing first means the
    provoked snapshot is a fresh subscription's snapshot and not a special case
    somebody has to reason about later.
    """
    return json.dumps({
        "method": "unsubscribe",
        "params": {"channel": "book", "symbol": [symbol], "depth": depth},
    }, separators=(",", ":"))


def frame_kind(obj) -> str:
    """A short label for the stderr tally. Never used by any downstream reader.

    Kraken has no single `type` field the way Anvil does: book messages carry
    `channel` + `type`, control messages carry `channel` alone, and a method ack
    carries neither. This flattens all three into one string for counting, and
    deliberately does not invent a `type` key in the trace to make the frames
    look Anvil-shaped -- what the venue sends is what gets recorded.
    """
    if not isinstance(obj, dict):
        return "?"
    channel = obj.get("channel")
    if channel is not None:
        kind = obj.get("type")
        return f"{channel}/{kind}" if kind else str(channel)
    if obj.get("method") is not None:
        return "ack:" + str(obj["method"])
    if obj.get("error") is not None:
        return "error"
    return "?"


def capture(args) -> int:
    install_sigint()
    captured_at = datetime.now(timezone.utc).isoformat()

    if args.depth not in DOCUMENTED_DEPTHS:
        sys.stderr.write(
            f"[capture] NOTE: depth {args.depth} is not one of the documented tiers "
            f"{DOCUMENTED_DEPTHS}. Sending it anyway and recording the answer.\n")

    sub = subscribe_text(args.symbol, args.depth)
    frame_count = 0
    kind_counts: collections.Counter[str] = collections.Counter()
    connects = 0
    skipped_multiline = 0

    with open(args.out, "w", encoding="utf-8", newline="\n") as out:
        cycles = max(1, args.cycles)
        per_cycle = args.duration if cycles == 1 else (args.reconnect_after or args.duration)

        # Connect first, so the metadata header records the handshake that
        # actually happened rather than the one that was intended.
        client = WsClient(args.url, origin=args.origin, timeout=args.connect_timeout,
                          user_agent=USER_AGENT, clock=CAPTURE_CLOCK)
        client.connect()
        connects += 1

        meta = {
            "captured_at": captured_at,
            "url": args.url,
            "venue": "kraken",
            "symbol": args.symbol,
            "depth": args.depth,
            "subscribe": sub,
            "tool_version": TOOL_VERSION,
            "clock": CAPTURE_CLOCK_NAME,
            "capture_mode": ("reconnect" if cycles > 1
                             else "resubscribe" if args.resubscribe_after > 0
                             else "baseline"),
            "cycles": cycles,
            "origin_sent": args.origin,
            "handshake_status": client.handshake_status,
        }
        out.write(json.dumps(meta) + "\n")
        out.flush()

        try:
            for cycle in range(cycles):
                if cycle > 0:
                    if stop_requested():
                        break
                    gap = max(args.reconnect_gap, MIN_RECONNECT_GAP_S)
                    sys.stderr.write(f"[capture] reconnect after {gap}s gap\n")
                    gap_end = time.monotonic() + gap
                    while time.monotonic() < gap_end and not stop_requested():
                        time.sleep(0.25)  # stay responsive to Ctrl-C
                    if stop_requested():
                        break
                    client = WsClient(args.url, origin=args.origin,
                                      timeout=args.connect_timeout, user_agent=USER_AGENT,
                                      clock=CAPTURE_CLOCK)
                    client.connect()
                    connects += 1

                # The one thing capture_anvil.py never had to do. Recorded with
                # its own tx timestamp before anything can arrive in reply.
                tx_ns = client.send_text(sub)
                out.write('{"rx_ns": %d, "dir": "tx", "frame": %s}\n' % (tx_ns, sub))
                out.flush()

                deadline = time.monotonic() + per_cycle
                # THE PROVOKED RESYNC (M4 stage B2). One connection, two
                # subscriptions: at `--resubscribe-after` seconds this sends an
                # unsubscribe and then, `--resubscribe-gap` later, the original
                # subscribe again — so the venue serves a second `book/snapshot`
                # MID-STREAM, after book events, which is the resync the
                # venue-free predicate detects.
                #
                # It is deliberately NOT `--cycles 2`. That reconnects, which is
                # a Cloudflare connection attempt (150 per rolling 10 minutes per
                # IP, with a ban on breach) and produces a different artefact: a
                # hole in rx_ns and a Gap{Disconnect} as well as a snapshot. This
                # produces the snapshot alone, on a socket that never blinks,
                # which is the case a CRC-failure resync will actually take.
                resub_at = time.monotonic() + args.resubscribe_after \
                    if args.resubscribe_after > 0 else None
                resub_send_at = None
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
                        # One JSON object per line, frame text spliced in
                        # unescaped: a pretty-printed frame parses perfectly and
                        # would still split the record across several lines.
                        if "\n" in text or "\r" in text:
                            skipped_multiline += 1
                            sys.stderr.write("[capture] skipping frame containing a newline\n")
                            continue
                        out.write('{"rx_ns": %d, "frame": %s}\n' % (rx_ns, text))
                        frame_count += 1
                        kind_counts[frame_kind(obj)] += 1
                        if frame_count % 200 == 0:
                            out.flush()
                            sys.stderr.write(
                                f"[capture] {frame_count} frames {dict(kind_counts)}\r")
                        # THE PROVOKED RESYNC, SENT AFTER THIS MESSAGE HAS BEEN
                        # WRITTEN — and the order is the whole of it.
                        #
                        # The first draft ran this at the TOP of the loop body,
                        # which stamps the tx record with `now` and then writes
                        # the already-received message behind it: **rx_ns goes
                        # backwards, and TraceReader rejects the entire file at
                        # that line.** Found by replaying the first capture, not
                        # by writing it — a capture tool contains no reader, and
                        # that asymmetry is why ARCHITECTURE §9 (2026-08-07) made
                        # TraceReader::next the ONE definition of a valid trace.
                        # A capture is not finished until it has been read back.
                        # Checked per received message rather than on a timer:
                        # the 1 Hz heartbeat guarantees control comes back here
                        # at least once a second, so the two sends land within
                        # about a heartbeat of when they were asked for, and the
                        # capture loop stays single-threaded.
                        now = time.monotonic()
                        if resub_at is not None and now >= resub_at:
                            unsub = unsubscribe_text(args.symbol, args.depth)
                            tx = client.send_text(unsub)
                            out.write('{"rx_ns": %d, "dir": "tx", "frame": %s}\n'
                                      % (tx, unsub))
                            out.flush()
                            sys.stderr.write("\n[capture] unsubscribed at "
                                             f"t+{args.resubscribe_after:.0f}s\n")
                            resub_at = None
                            resub_send_at = now + args.resubscribe_gap
                        elif resub_send_at is not None and now >= resub_send_at:
                            tx = client.send_text(sub)
                            out.write('{"rx_ns": %d, "dir": "tx", "frame": %s}\n'
                                      % (tx, sub))
                            out.flush()
                            sys.stderr.write("[capture] re-subscribed; a mid-stream "
                                             "snapshot should follow\n")
                            resub_send_at = None
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
        f"skipped_multiline={skipped_multiline}, connects={connects}, "
        f"handshake={client.handshake_status!r}\n")
    sys.stderr.write(f"[capture] wrote {args.out}\n")
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--url", default="wss://ws.kraken.com/v2",
                   help="Kraken WebSocket v2 public endpoint")
    p.add_argument("--symbol", default="BTC/USD",
                   help="Kraken v2 symbol, e.g. BTC/USD (one per connection)")
    p.add_argument("--depth", type=int, default=25,
                   help="book depth to subscribe. Documented tiers: "
                        "10, 25, 100, 500, 1000. Sent as given.")
    p.add_argument("--out", required=True, help="output NDJSON path")
    p.add_argument("--duration", type=float, default=90.0,
                   help="capture seconds (single-connection baseline)")
    p.add_argument("--cycles", type=int, default=1,
                   help="number of connect cycles (>1 => reconnect trace). "
                        "Kept small on purpose: Cloudflare bans on attempt-rate breach.")
    p.add_argument("--reconnect-after", type=float, default=0.0,
                   help="seconds per cycle before reconnecting (reconnect mode)")
    p.add_argument("--reconnect-gap", type=float, default=MIN_RECONNECT_GAP_S,
                   help=f"seconds disconnected between cycles (floor {MIN_RECONNECT_GAP_S:g}s)")
    p.add_argument("--resubscribe-after", type=float, default=0.0,
                   help="seconds into the capture at which to unsubscribe and "
                        "re-subscribe on the SAME socket, provoking a mid-stream "
                        "snapshot (0 = never). Not a reconnect: no new connection, "
                        "so no Cloudflare attempt and no rx_ns hole.")
    p.add_argument("--resubscribe-gap", type=float, default=1.0,
                   help="seconds between the unsubscribe and the re-subscribe")
    p.add_argument("--origin", default=None,
                   help="Origin header to send (default: none). Unlike Anvil there "
                        "is no auto-retry: a rejected upgrade here is reported, not "
                        "retried, because a retry loop is what gets an IP banned.")
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
