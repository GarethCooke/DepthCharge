#!/usr/bin/env python3
"""Does Kraken QUEUE to a slow consumer, or SHED? — and therefore is `age_ms` real?

M4 stage B2, section 4. This settles the assumption `test_kraken_adapter.cpp`
pinned at B1 with B2 named as its owner, and the answer changes a number the
panel already displays.

WHY THE QUESTION IS NOT ACADEMIC
--------------------------------
`age_estimator.hpp` is venue-free arithmetic: over a window of wall time W the
venue's liveness signal should have arrived W/interval times, and the shortfall
is read as the book's queuing lag. **A deficit is an AGE only if the venue
QUEUES.** If it SHEDS, the identical deficit appears and there is no lag at all —
the frames that arrive are current, there are simply fewer of them. Rate alone
cannot tell the two apart, and DepthCharge has been wrong about exactly this
before: `tools/anvil_drain_probe.py` concluded "Anvil sheds" from a rate
measurement in 2026-08-09 and was superseded on 2026-08-11 by a freshness
measurement showing it queues (ARCHITECTURE §9). At Anvil the premise is now
measured and contractual. At Kraken it is neither, and none of the five committed
slices can answer it: every one was captured by a client that kept up, and a feed
nobody fell behind on produces a zero deficit under both hypotheses.

TWO INDEPENDENT DISCRIMINATORS, AND THEY DISAGREE UNDER OPPOSITE HYPOTHESES
---------------------------------------------------------------------------
1. **THE CHECKSUM.** Kraken's CRC32 over the top 10 levels is a complete test of
   whether the book we hold is the book the venue holds. A SHED update leaves a
   level wrong and the next checksum fails; a QUEUED update arrives late and
   every checksum stays clean. This is the discriminator B2 exists to build and
   the reason this stage owns the experiment.

2. **THE VENUE'S OWN TIMESTAMP.** Every `book` message carries
   `data[0].timestamp` at microsecond resolution, so freshness is directly
   measurable rather than inferred: `now - timestamp` is how stale the message
   was when we read it. Absolute values include clock skew and network latency
   and are not the point; the SLOPE under throttle is. Queuing makes lag grow
   without bound (Anvil's grew at 0.745 s/s at a 25% drain); shedding leaves it
   flat however hard the reader is stalled.

Neither alone would be conclusive — the first cannot see a stream that is merely
late, the second cannot see a stream that is merely thinned — and together they
have four distinguishable outcomes, one of which is "the throttle never bit",
which a single-signal probe would silently report as a result.

METHOD, AND ITS ONE HONEST WEAKNESS
-----------------------------------
One connection, three phases on the same socket: drain freely (baseline), then
sleep a fixed delay after every message so the OS receive buffer fills and TCP
backpressure reaches the server, then release and watch what happens. The release
phase is not decoration — a queueing server DELIVERS THE BACKLOG when the window
reopens, so a burst above the natural rate followed by lag decaying to baseline
is a third, independent confirmation of queuing that neither discriminator gives
on its own.

The weakness: TCP backpressure is not the same lever as an application-level slow
consumer, and where Kraken's own send queue sits relative to the socket is
unknown to us. What this measures is the behaviour of the whole path, which is
also the thing the panel experiences, so it is the right question for `age_ms`
even though it is not a statement about Kraken's internals.

BE A GOOD CITIZEN. One pair, one connection, a short window, and the gentlest
throttle that produces a signal. This is a public endpoint; the experiment does
not need to be aggressive to be conclusive, and Cloudflare limits CONNECTION
ATTEMPTS (~150 per rolling 10 minutes per IP, ban on breach) so this tool
connects exactly once and never retries.

Usage:
    python tools/kraken_backpressure_probe.py --symbol BTC/USD --depth 25 \\
        --baseline 20 --throttled 40 --release 20 --delay-ms 250

Python 3 stdlib only. Lives in tools/.
"""
from __future__ import annotations

import argparse
import collections
import json
import ssl
import statistics
import sys
import time
from datetime import datetime, timezone

import capture_kraken as ck
from kraken_frame_economics import CHECKSUM_LEVELS, Book, loads_raw
from wsclient import HandshakeRejected, WsClient, install_sigint, stop_requested

TOOL_VERSION = "0.1.0"


def parse_ts(text: str) -> float:
    """Kraken's `2026-08-18T20:28:41.694808Z` as a POSIX timestamp.

    `fromisoformat` handles the trailing `Z` only from Python 3.11, and this
    project's tools already require 3.11+ elsewhere; the replace keeps it working
    on 3.10 rather than failing at the last moment of a live capture.
    """
    return datetime.fromisoformat(text.replace("Z", "+00:00")).timestamp()


class Phase:
    """One drain regime, and everything measured during it."""

    def __init__(self, name: str, delay_s: float) -> None:
        self.name = name
        self.delay_s = delay_s
        self.messages = 0
        self.book_messages = 0
        self.checksummed = 0
        self.checksum_ok = 0
        self.first_fail_at: float | None = None
        self.lags: list[float] = []          # seconds, venue timestamp -> our clock
        self.lag_first: float | None = None
        self.lag_last: float | None = None
        self.t0: float | None = None
        self.t1: float | None = None
        self.kinds: collections.Counter[str] = collections.Counter()

    def note(self, now: float) -> None:
        if self.t0 is None:
            self.t0 = now
        self.t1 = now

    @property
    def span(self) -> float:
        if self.t0 is None or self.t1 is None:
            return 0.0
        return self.t1 - self.t0

    @property
    def rate(self) -> float:
        return self.messages / self.span if self.span > 0 else 0.0

    def lag_slope(self) -> float:
        """Seconds of lag added per second of wall clock, first sample to last.

        Deliberately the crude endpoint slope rather than a regression: the
        hypothesis under test predicts a straight line with slope
        `1 - drain_fraction` (queuing) or zero (shedding), and the two differ by
        far more than a fit would refine. A regression here would be false
        precision over a live 40-second window.
        """
        if self.lag_first is None or self.lag_last is None or self.span <= 0:
            return 0.0
        return (self.lag_last - self.lag_first) / self.span

    def report(self) -> str:
        if self.messages == 0:
            return f"  {self.name:<9} | no messages"
        lags = sorted(self.lags)
        p50 = statistics.median(lags) if lags else float("nan")
        worst = lags[-1] if lags else float("nan")
        crc = (f"{self.checksum_ok}/{self.checksummed}"
               if self.checksummed else "-")
        return (f"  {self.name:<9} | delay {self.delay_s * 1000:4.0f} ms "
                f"| {self.messages:5d} msgs / {self.span:5.1f} s = {self.rate:6.2f}/s "
                f"| CRC {crc:>11} "
                f"| lag p50 {p50:7.3f} s worst {worst:7.3f} s "
                f"| slope {self.lag_slope():+.3f} s/s")


def verdict(baseline: Phase, throttled: Phase, release: Phase, drop: float) -> str:
    """The three-outcome table from the brief, plus the null result.

    Written as a function rather than as prose in the output so that the reading
    is mechanical and the same every run. A probe that leaves the operator to
    eyeball five columns is how `anvil_drain_probe.py` reached the wrong answer.
    """
    if throttled.messages == 0:
        return ("INCONCLUSIVE: nothing arrived during the throttled phase. Either the "
                "server closed the socket (check the connection line above) or the "
                "window was too short.")
    if drop < 0.15:
        return (f"NULL RESULT: the throttle did not bite — the delivered rate fell only "
                f"{drop * 100:.0f}%. The OS receive buffer absorbed it. Re-run with a "
                f"larger --delay-ms or a busier pair before reading anything into the "
                f"CRC and lag columns, both of which are what a healthy feed looks like.")
    shed = throttled.checksummed > 0 and throttled.checksum_ok < throttled.checksummed
    queued = throttled.lag_slope() > 0.10
    if shed:
        return ("SHEDS. The venue dropped updates: the book diverged and its own CRC32 "
                f"said so ({throttled.checksummed - throttled.checksum_ok} failure(s), "
                f"first at t+{throttled.first_fail_at:.1f}s of the throttled phase). "
                "A deficit at this venue is LOSS, NOT AGE. `age_ms` at Kraken is a "
                "fiction and must be suppressed or redefined — STOP AND RAISE, do not "
                "patch (M4 stage B2 brief, section 4).")
    if queued:
        # `lags` is non-empty whenever the slope is non-zero, so the max below is
        # safe by construction; `release.lags` is guarded separately because a
        # release phase can legitimately hold heartbeats and no book message.
        return (f"QUEUES. Every checksum stayed clean under a {drop * 100:.0f}% drain — "
                "so no message was lost — while the venue's own timestamp shows the "
                f"messages arriving {throttled.lag_slope():.3f} s staler per second, "
                f"reaching {max(throttled.lags):.1f} s. Nothing was dropped and "
                "everything was late, which is the definition of a queue. **The deficit "
                "IS an age: `age_ms` stands at Kraken.**"
                + (f" Confirmed on release: the backlog drained at {release.rate:.1f}/s "
                   f"against a baseline of {baseline.rate:.1f}/s and lag fell back to "
                   f"{min(release.lags):.3f} s." if release.lags else ""))
    return ("INCONCLUSIVE: the checksums stayed clean (nothing lost) but the lag did not "
            "grow (nothing queued). Those cannot both be true of a genuinely throttled "
            "socket, so the throttle probably did not reach the server. Treat as a null "
            "result and re-run harder.")


def probe(args) -> int:
    install_sigint()
    phases = [Phase("baseline", 0.0),
              Phase("throttled", args.delay_ms / 1000.0),
              Phase("release", 0.0)]
    durations = [args.baseline, args.throttled, args.release]

    book = Book()
    baselined = False
    ended = "deadline reached"

    client = WsClient(args.url, origin=None, timeout=args.connect_timeout,
                      user_agent=f"depthcharge-backpressure/{TOOL_VERSION}",
                      clock=time.perf_counter_ns)
    client.connect()
    sub = ck.subscribe_text(args.symbol, args.depth)
    client.send_text(sub)

    out = None
    if args.out:
        out = open(args.out, "w", encoding="utf-8", newline="\n")
        out.write(json.dumps({
            "captured_at": datetime.now(timezone.utc).isoformat(),
            "url": args.url, "venue": "kraken", "symbol": args.symbol,
            "depth": args.depth, "subscribe": sub,
            "tool_version": f"backpressure-{TOOL_VERSION}",
            "clock": "perf_counter_ns", "capture_mode": "backpressure-probe",
            "cycles": 1, "drain_delay_ms": args.delay_ms,
        }) + "\n")

    total = sum(durations)
    start = time.monotonic()
    bounds = []
    acc = 0.0
    for d in durations:
        acc += d
        bounds.append(start + acc)

    try:
        for rx_ns, text in client.messages(start + total + 5.0):
            if stop_requested():
                ended = "interrupted"
                break
            now_mono = time.monotonic()
            if now_mono >= bounds[-1]:
                break
            phase = phases[0] if now_mono < bounds[0] else (
                phases[1] if now_mono < bounds[1] else phases[2])

            # Wall clock, not perf_counter: it is compared against the VENUE's
            # clock and only a wall clock can be.
            now_wall = time.time()
            phase.note(now_mono)
            phase.messages += 1

            if out and "\n" not in text and "\r" not in text:
                out.write('{"rx_ns": %d, "frame": %s}\n' % (rx_ns, text))

            frame = loads_raw(text)
            if not isinstance(frame, dict):
                continue
            phase.kinds[ck.frame_kind(frame)] += 1
            if frame.get("channel") != "book":
                continue
            for entry in frame.get("data", []):
                if entry.get("symbol") != args.symbol:
                    continue
                phase.book_messages += 1

                ts = entry.get("timestamp")
                if ts:
                    lag = now_wall - parse_ts(str(ts))
                    phase.lags.append(lag)
                    if phase.lag_first is None:
                        phase.lag_first = lag
                    phase.lag_last = lag

                if frame.get("type") == "snapshot":
                    book.replace(entry)
                    baselined = True
                elif baselined:
                    book.apply(entry, truncate=args.depth)
                else:
                    continue

                want = entry.get("checksum")
                if want is None:
                    continue
                phase.checksummed += 1
                if book.checksum() == int(str(want)):
                    phase.checksum_ok += 1
                elif phase.first_fail_at is None:
                    phase.first_fail_at = now_mono - (phase.t0 or now_mono)

            if phase.delay_s:
                time.sleep(phase.delay_s)   # the whole point: stall the reader
    except (ConnectionError, ssl.SSLError) as exc:
        ended = f"SERVER CLOSED THE CONNECTION: {exc}"
    except KeyboardInterrupt:
        ended = "interrupted"
    finally:
        client.close()
        if out:
            out.close()

    print(f"\nKraken backpressure probe — {args.symbol} depth {args.depth}, "
          f"one connection, {total:.0f}s")
    print(f"  ended: {ended}")
    print(f"  checksum covers the top {CHECKSUM_LEVELS} levels a side\n")
    for ph in phases:
        print(ph.report())
    drop = 0.0
    if phases[0].rate > 0:
        drop = max(0.0, 1.0 - phases[1].rate / phases[0].rate)
    print(f"\n  delivered rate fell {drop * 100:.0f}% under the throttle")
    if "CLOSED" in ended:
        print("\nVERDICT: THE SERVER DISCONNECTED US at this throttle. That is the third "
              "outcome in the brief's table and it is a finding, not a failure: report "
              "it, and a gentler throttle is a second sitting.")
    else:
        print("\nVERDICT: " + verdict(phases[0], phases[1], phases[2], drop))
    if args.out:
        print(f"\n  stream written to {args.out}")
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--url", default="wss://ws.kraken.com/v2")
    p.add_argument("--symbol", default="BTC/USD")
    p.add_argument("--depth", type=int, default=25)
    p.add_argument("--baseline", type=float, default=20.0, help="seconds draining freely")
    p.add_argument("--throttled", type=float, default=40.0, help="seconds throttled")
    p.add_argument("--release", type=float, default=20.0,
                   help="seconds draining freely again — a queue delivers its backlog here")
    p.add_argument("--delay-ms", type=float, default=250.0,
                   help="sleep after every message during the throttled phase")
    p.add_argument("--out", default=None, help="also write the stream as an NDJSON trace")
    p.add_argument("--connect-timeout", type=float, default=20.0)
    args = p.parse_args(argv)
    try:
        return probe(args)
    except HandshakeRejected as exc:
        sys.stderr.write(f"[probe] handshake rejected: {exc.status_line}\n")
        sys.stderr.write("[probe] NOT retrying — see the rate-limit note in "
                         "tools/capture_kraken.py before running again.\n")
        return 2
    except OSError as exc:
        sys.stderr.write(f"[probe] network error: {exc}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
