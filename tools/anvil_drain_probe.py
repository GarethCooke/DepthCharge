#!/usr/bin/env python3
"""Does Anvil's per-socket cadence depend on how fast the client drains it?

*** SUPERSEDED, 2026-08-11 — THE CONCLUSION BELOW IS WRONG, AND THE METHOD IS WHY.

    Everything this tool MEASURES is still true. What it CONCLUDES from those
    measurements — "Anvil sheds, and sheds smoothly", "only `book` frames are
    shed, `summary` and `trade` arrive intact" — does not follow, because rate
    and inter-message gap cannot tell shedding from queuing. A client draining
    at 4/s receives something every ~250 ms whether the thing it receives is
    current or ninety seconds stale. This never checked freshness.

    `tools/anvil_freshness_probe.py` does check it, by matching wire `seq`
    across a throttled socket and a simultaneous unthrottled one. At the same
    250 ms drain delay used below, on 2026-08-11:

        book     3.48/s of 13.66/s = 25.5%      lag rose LINEARLY to 111 s
        summary  0.51/s of  2.01/s = 25.7%      over 150 s, with no plateau

    So `summary` is thinned by exactly the same fraction as `book` — the
    per-kind reading below is not reproducible — and the messages that do
    arrive are stale by an amount that grows at 0.745 s per second, which is
    1 minus the drain fraction to three decimal places. **Anvil queues. It does
    not shed.** The implied per-socket application queue was ~12.4 MB after
    150 s and still growing.

    This file is kept, and its original text below is kept unedited, because the
    method is a useful control and because the mistake is the point: a
    measurement that answers a nearby question gets read as answering the one
    that mattered. See ARCHITECTURE.md §9, 2026-08-11.

Written for M3 to settle a question the board raised and a capture alone cannot
answer. The ESP32 reads ~6 messages/s where a desk client on the same LAN reads
~17, and the firmware's RX watchdog was greying the panel on the difference. Two
explanations fit that observation and they call for opposite fixes:

  * the server slowed down       -> re-derive the watchdog threshold
  * the server sheds to a slow
    consumer, and the board is
    a slow consumer              -> the threshold is fine; find what stalls the board

This distinguishes them by *being* a slow client. It opens the same socket the
capture tool opens and sleeps for a fixed delay after every message, so the TCP
receive window stays pinched the way an ESP32 pinches it doing mbedTLS on 8.2 KB
book frames. Run it beside a normal capture and the two sockets are a controlled
comparison: same server, same instant, different drain speeds.

The 2026-08-09 answer (50 s per step, against a simultaneous unthrottled capture
that stayed flat at 16.9 msg/s throughout):

    drain delay      0 ms | 16.95 msg/s | 106.5 KB/s | gaps p50  63 max 156 ms
    drain delay     50 ms | 16.91 msg/s | 107.4 KB/s | gaps p50  62 max 141 ms
    drain delay    120 ms |  8.32 msg/s |  54.4 KB/s | gaps p50 125 max 125 ms
    drain delay    250 ms |  4.01 msg/s |  26.9 KB/s | gaps p50 250 max 266 ms

So Anvil sheds, and it sheds *smoothly*: a client drained at a quarter of the
server's rate still receives something every ~250 ms and is never left silent for
longer than one drain interval plus a tick. The rate collapses; the gaps do not.
That is what rules the server out as the source of the board's multi-second holes
— reproducing the board's message rate exactly does not reproduce its silences.

Only `book` frames are shed. `summary` (a 2 Hz cross-ticker broadcast) and
`trade` (streamed individually) arrive intact at every drain speed, which is why
the shed stream is invisible in a message count alone and shows up plainly in a
per-kind one.

Python 3 stdlib only; lives in tools/. Reuses capture_anvil's client rather than
re-implementing RFC 6455, so it is the same socket code the ground-truth captures
use.
"""
from __future__ import annotations

import argparse
import collections
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import capture_anvil as ca  # noqa: E402
# Nearest-rank percentile is imported, not re-implemented: a second copy that
# rounded its rank differently would make this tool and gap_stats.py quietly
# disagree about the same trace, which is the one thing neither may do.
from gap_stats import percentile  # noqa: E402


def probe(url: str, delay_s: float, seconds: float, per_kind: bool,
          out_path: str | None = None) -> None:
    client, _ = ca._connect_with_origin_probe(url, None, timeout=20.0)
    times: list[int] = []
    sizes: list[int] = []
    kinds: collections.Counter[str] = collections.Counter()
    kind_times: dict[str, list[int]] = collections.defaultdict(list)
    # Optionally write the throttled stream as a normal capture trace, so
    # tools/gap_stats.py can measure it under the same three counting rules as a
    # real capture — including the book-event rule the firmware watchdog arms on,
    # which a per-kind summary cannot express.
    out = open(out_path, "w", encoding="utf-8", newline="\n") if out_path else None
    if out:
        out.write('{"captured_at": "%s", "url": "%s", "ticker": %d, '
                  '"tool_version": "%s", "capture_mode": "drain-probe", '
                  '"drain_delay_ms": %.0f}\n'
                  % (ca.datetime.now(ca.timezone.utc).isoformat(), url,
                     101, ca.TOOL_VERSION, delay_s * 1000))
    try:
        deadline = time.monotonic() + seconds
        for rx_ns, text in client.messages(deadline):
            times.append(rx_ns)
            sizes.append(len(text))
            if out and "\n" not in text and "\r" not in text:
                out.write('{"rx_ns": %d, "frame": %s}\n' % (rx_ns, text))
            # Cheap kind sniff — a full json.loads per message would itself slow
            # the reader and contaminate the very thing being measured.
            start = text.find('"type":"')
            kind = text[start + 8:text.find('"', start + 8)] if start >= 0 else "?"
            kinds[kind] += 1
            kind_times[kind].append(rx_ns)
            if delay_s:
                time.sleep(delay_s)  # the whole point: stall the reader
    finally:
        client.close()
        if out:
            out.close()

    if len(times) < 3:
        print(f"  drain delay {delay_s * 1000:6.0f} ms | too few messages ({len(times)})")
        return

    span = (times[-1] - times[0]) / 1e9
    gaps = sorted((b - a) / 1e6 for a, b in zip(times, times[1:]))
    mean_b = statistics.mean(sizes)
    print(f"  drain delay {delay_s * 1000:6.0f} ms | {len(times):5d} msgs / {span:5.1f} s "
          f"= {len(times) / span:5.2f} msg/s | mean {mean_b:5.0f} B "
          f"| {len(times) * mean_b / span / 1024:6.1f} KiB/s "  # binary: divisor is 1024
          f"| gaps p50 {percentile(gaps, 50):6.1f} p99 {percentile(gaps, 99):7.1f} "
          f"max {gaps[-1]:7.1f} ms")

    if per_kind:
        for kind, n in sorted(kinds.items(), key=lambda kv: -kv[1]):
            ts = kind_times[kind]
            worst = max(((b - a) / 1e6 for a, b in zip(ts, ts[1:])), default=float("nan"))
            print(f"      {kind:<9} {n:5d}  {n / span:5.2f}/s  worst gap {worst:7.1f} ms")


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--url", default="wss://anvil.garethcooke.com/ws")
    p.add_argument("--ticker", type=int, default=101)
    p.add_argument("--seconds", type=float, default=50.0, help="per drain-delay step")
    p.add_argument("--delays", type=float, nargs="+", default=[0.0, 0.05, 0.12, 0.25],
                   help="drain delays in SECONDS, one step each")
    p.add_argument("--per-kind", action="store_true",
                   help="also break each step down by frame kind (shows what is shed)")
    p.add_argument("--out-prefix", default=None,
                   help="write each step as <prefix>-<delay>ms.ndjson, in capture "
                        "format, for tools/gap_stats.py to measure")
    args = p.parse_args(argv)

    url = f"{args.url}?ticker={args.ticker}"
    print(f"Anvil per-socket cadence vs client drain speed ({args.seconds:.0f}s per step)")
    for delay in args.delays:
        out = f"{args.out_prefix}-{delay * 1000:.0f}ms.ndjson" if args.out_prefix else None
        probe(url, delay, args.seconds, args.per_kind, out)
        if out:
            print(f"      wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
