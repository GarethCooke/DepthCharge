#!/usr/bin/env python3
"""Is a slow Anvil client SHED, or is it QUEUED? Measures freshness, not cadence.

THE GAP THIS EXISTS TO CLOSE.

`anvil_drain_probe.py` (2026-08-09) measured a throttled socket's message *rate*
and its inter-message *gaps* and concluded "Anvil sheds to a slow consumer, and
sheds evenly". That conclusion went into ARCHITECTURE §9 and was read downstream
as "a slow client still sees a current book".

Both observations are equally consistent with pure queuing. A client draining at
4/s receives something every ~250 ms whether the thing it receives is current or
ninety seconds stale — **the probe never checked freshness, and nor did anything
else in M3.** On 2026-08-11 a board with every transport counter clean put a
state on the panel 96-98 seconds after the web client already had it.

So this tool asks the question the other one could not:

    open TWO sockets from one process. Drain one as fast as possible (the
    REFERENCE — its arrival time for a given frame is as close to "when the
    server sent it" as a desk client can get). Throttle the other (the SUBJECT)
    the way an ESP32 doing mbedTLS on 8 KB book frames throttles itself. Anvil's
    wire `seq` is a single GLOBAL engine counter, so the SAME broadcast carries
    the SAME seq on both sockets — match on it and the difference in arrival
    time IS the subject's lag, measured against a real clock rather than assumed
    from a rate.

WHAT THE ANSWER LOOKS LIKE, AND WHY IT DECIDES THE NEXT FIX.

    lag grows linearly with elapsed time   -> QUEUING. Nothing is shed; the
                                              subject is walking through a
                                              backlog and will never catch up.
                                              The fix is to stop the queue
                                              forming (drain faster).
    lag rises and PLATEAUS                 -> bounded queue / shedding. The
                                              ceiling is the number, and a
                                              client that tolerates it is fine.
    lag ~ 0 with a thinned message count    -> pure shedding, and the 2026-08-09
                                              conclusion was right after all.

And the per-kind table separates the two mechanisms independently of the clock:

    every kind thinned by the SAME ratio    -> the whole byte stream is delayed:
                                              queuing, below the application.
    `summary` intact, `book` thinned        -> per-kind coalescing in Anvil's
                                              own send path: shedding.

Those two readings must agree. If they do not, say so in the write-up rather
than picking the convenient one — that disagreement is itself the finding.

A THIRD THING IT MEASURES, ALMOST FOR FREE. `--reference-only` reports the
unthrottled socket's per-kind rates, which is how you check that Anvil's
`summary` broadcast really is the 2.00/s that `firmware/src/staleness.hpp`
divides by. Every age the board prints scales by that number, and nothing on the
board can measure it.

CAVEAT, STATED SO IT IS NOT READ WIDER THAN IT IS. This is a desk client on a
desk stack. It reproduces a slow *reader*; it does not reproduce an ESP32's TCP
window, its TLS record handling or its Wi-Fi link. It settles the MECHANISM
question cheaply and it is not a substitute for the board's own lag-versus-uptime
run (M3 stage E, deliverable 1) — it is the control that run is read against.

Python 3 stdlib only; lives in tools/. Reuses capture_anvil's client rather than
re-implementing RFC 6455, so both sockets are the same socket code the
ground-truth captures use, and `rx_ns` on both is one process-wide
`time.monotonic_ns()`.
"""
from __future__ import annotations

import argparse
import collections
import os
import statistics
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import capture_anvil as ca  # noqa: E402
# Nearest-rank percentile is imported, not re-implemented, for the same reason
# anvil_drain_probe.py imports it: two copies that rounded their rank
# differently would make two tools quietly disagree about one trace.
from gap_stats import percentile  # noqa: E402

# How much of a message to scan for its `seq`. Anvil puts `"type"` and `"seq"`
# in the first few dozen bytes of every frame; scanning the whole 8 KB payload
# would risk matching a `seq` nested inside a summary's per-ticker array and
# would cost real time in the reader we are deliberately trying not to slow.
HEAD_CHARS = 160


def _sniff(text: str) -> tuple[str, int]:
    """(kind, seq) from a frame's head, without a full JSON parse.

    A json.loads per message would itself slow the reader and contaminate the
    very thing being measured — the same reasoning as anvil_drain_probe.py's
    kind sniff, extended to pull the seq out too.
    """
    head = text[:HEAD_CHARS]
    kind = "?"
    at = head.find('"type":"')
    if at >= 0:
        end = head.find('"', at + 8)
        if end > 0:
            kind = head[at + 8:end]
    seq = -1
    at = head.find('"seq":')
    if at >= 0:
        i = at + 6
        while i < len(head) and head[i] == " ":
            i += 1
        start = i
        while i < len(head) and head[i].isdigit():
            i += 1
        if i > start:
            seq = int(head[start:i])
    return kind, seq


class Socket:
    """One socket, read on its own thread, recording arrivals."""

    def __init__(self, name: str, url: str, delay_s: float):
        self.name = name
        self.url = url
        self.delay_s = delay_s
        # (rx_ns, kind, seq, size) in arrival order.
        self.records: list[tuple[int, str, int, int]] = []
        self.error: BaseException | None = None
        self.started_ns = 0

    def run(self, deadline: float) -> None:
        client = None
        try:
            client, _ = ca._connect_with_origin_probe(self.url, None, timeout=20.0)
            self.started_ns = time.monotonic_ns()
            for rx_ns, text in client.messages(deadline):
                kind, seq = _sniff(text)
                self.records.append((rx_ns, kind, seq, len(text)))
                if self.delay_s:
                    time.sleep(self.delay_s)  # the whole point: stall the reader
        except BaseException as exc:  # noqa: BLE001 — reported, not swallowed
            self.error = exc
        finally:
            if client is not None:
                client.close()

    # -- derived views --------------------------------------------------------

    def span_s(self) -> float:
        if len(self.records) < 2:
            return 0.0
        return (self.records[-1][0] - self.records[0][0]) / 1e9

    def kinds(self) -> collections.Counter:
        return collections.Counter(r[1] for r in self.records)

    def kind_span_s(self, kind: str) -> float:
        """First-to-last seconds for one frame kind."""
        ts = [r[0] for r in self.records if r[1] == kind]
        return 0.0 if len(ts) < 2 else (ts[-1] - ts[0]) / 1e9

    def first_seen(self) -> dict[int, int]:
        """seq -> the FIRST rx_ns it was seen at. First, not last: Anvil's global
        seq is shared across tickers and frame types, so a value can recur, and
        the earliest sighting is the one that dates the broadcast."""
        out: dict[int, int] = {}
        for rx_ns, _kind, seq, _n in self.records:
            if seq >= 0 and seq not in out:
                out[seq] = rx_ns
        return out


def rate(n: int, span_s: float) -> float:
    """Events per second from N arrivals spanning first-to-last `span_s`.

    (N-1)/span, NOT N/span, AND THAT IS NOT PEDANTRY — IT COST THIS PROJECT A
    WRONG CONSTANT FOR AN AFTERNOON.

    `span_s` is measured between the FIRST and LAST arrival, so it contains N-1
    inter-arrival intervals, not N. Dividing by N overstates the rate by
    N/(N-1) — negligible on a long run, 0.84% on a short one. A 60-second
    reference run of this tool reported Anvil's `summary` broadcast at
    "2.017/s", which was read as a real 0.85% departure from the 2.000/s
    constant in firmware/src/staleness.hpp and used to justify a whole analysis
    of estimator drift. A 30-minute run of the same socket reads 2.0003/s: the
    broadcast is exact and the 0.017 was this function's predecessor.

    Which is this milestone's own lesson turned on its own tooling — a
    measurement that answers a nearby question ("how many did I see per second
    of observation") read as answering the one that mattered ("what is the
    period of the broadcast"). The two differ by exactly one interval.
    """
    if n < 2 or span_s <= 0:
        return 0.0
    return (n - 1) / span_s


def _rate_table(sockets: list[Socket]) -> None:
    print()
    print("  per-kind rates — the mechanism test that does not need a clock")
    print(f"    {'socket':<12}{'span':>7}{'msgs':>8}{'msg/s':>9}{'KB/s':>9}   per-kind /s")
    for s in sockets:
        span = s.span_s()
        if span <= 0:
            print(f"    {s.name:<12}  (too few messages: {len(s.records)})")
            continue
        kb = sum(r[3] for r in s.records) / span / 1024
        # Each kind against ITS OWN span, so a kind that started late or stopped
        # early is not averaged over the whole run.
        per_kind = "  ".join(
            f"{k}={rate(n, s.kind_span_s(k)):.3f}"
            for k, n in sorted(s.kinds().items(), key=lambda kv: -kv[1])
        )
        print(f"    {s.name:<12}{span:>6.1f}s{len(s.records):>8}"
              f"{rate(len(s.records), span):>9.2f}{kb:>9.1f}   {per_kind}")

    if len(sockets) == 2:
        ref, sub = sockets
        rs, ss = ref.span_s(), sub.span_s()
        if rs > 0 and ss > 0:
            rk, sk = ref.kinds(), sub.kinds()
            print()
            print("    subject as a fraction of reference, per kind:")
            for kind in sorted(set(rk) | set(sk)):
                r_rate = rate(rk.get(kind, 0), ref.kind_span_s(kind))
                s_rate = rate(sk.get(kind, 0), sub.kind_span_s(kind))
                frac = (s_rate / r_rate * 100) if r_rate > 0 else float("nan")
                print(f"      {kind:<10} {s_rate:6.3f}/s of {r_rate:6.3f}/s = {frac:5.1f}%")
            print("      -> all kinds at the SAME fraction  => the whole stream is delayed"
                  " (queuing)")
            print("      -> summary near 100%, book far below => Anvil coalesces per kind"
                  " (shedding)")


def _backlog(ref: Socket, sub: Socket) -> None:
    """How much the server is holding for the subject at the end of the run.

    Derived rather than observed — Anvil reports nothing about its own send
    queue — but it follows from two counts and a mean size, and it is the figure
    that decides whether an application-level queue is BOUNDED. A ceiling would
    show as this number levelling off; unbounded growth is a different problem
    for the venue than a coalescing window is.
    """
    rs, ss = ref.span_s(), sub.span_s()
    if rs <= 0 or ss <= 0 or not sub.records:
        return
    # RATES, not raw counts. The first version differenced the two message
    # counts directly, which is only correct when both sockets ran for the same
    # time — and the case this figure matters most in is precisely the one where
    # they did not, because the subject died early. Differencing counts there
    # credits the reference's extra seconds to the backlog.
    ref_rate, sub_rate = rate(len(ref.records), rs), rate(len(sub.records), ss)
    missing = (ref_rate - sub_rate) * ss
    if missing <= 0:
        return
    mean_b = sum(r[3] for r in sub.records) / len(sub.records)
    print()
    print(f"  implied server-side backlog after {ss:.0f} s: ~{missing:.0f} messages "
          f"(~{missing * mean_b / 1e6:.1f} MB at a {mean_b:.0f} B mean)")
    if abs(rs - ss) > 0.05 * rs:
        print(f"    NOTE: the two sockets ran for different spans ({rs:.0f} s vs {ss:.0f} s) —"
              f" the subject probably died early, and that is itself the result.")
    print("    Anvil publishes nothing about its own send queue, so this is arithmetic on"
          " two counts — but an application-level queue that does not level off is a"
          " different finding for the venue than a coalescing window is.")


def _lag_table(ref: Socket, sub: Socket, bins: int) -> None:
    ref_seen = ref.first_seen()
    matched: list[tuple[float, float]] = []  # (subject elapsed s, lag s)
    unmatched = 0
    if not sub.records:
        print("\n  no subject messages — nothing to match")
        return
    t0 = sub.records[0][0]
    for rx_ns, _kind, seq, _n in sub.records:
        if seq < 0:
            continue
        ref_ns = ref_seen.get(seq)
        if ref_ns is None:
            unmatched += 1
            continue
        matched.append(((rx_ns - t0) / 1e9, (rx_ns - ref_ns) / 1e9))

    print()
    print(f"  freshness — {len(matched)} of {len(sub.records)} subject messages matched to the"
          f" reference by wire seq ({unmatched} unmatched)")
    if not matched:
        print("    nothing matched. If this is not a connection failure it means the two"
              " sockets share no seq values, and the matching premise is wrong.")
        return

    span = matched[-1][0]
    width = span / bins if bins > 0 else span
    print(f"    {'elapsed on subject':<22}{'n':>6}{'lag p50':>10}{'lag p90':>10}{'lag max':>10}")
    for b in range(bins):
        lo, hi = b * width, (b + 1) * width
        window = sorted(lag for el, lag in matched if lo <= el < hi or (b == bins - 1 and el >= hi))
        if not window:
            continue
        print(f"    {lo:8.1f} - {hi:6.1f} s{len(window):>6}"
              f"{percentile(window, 50):>10.2f}{percentile(window, 90):>10.2f}"
              f"{window[-1]:>10.2f}")

    first = [lag for el, lag in matched if el < span / 4]
    last = [lag for el, lag in matched if el > span * 3 / 4]
    if first and last:
        growth = statistics.median(last) - statistics.median(first)
        # THE SEPARATION IS 0.75 x span, NOT span / 2. The first quarter's
        # samples are centred at span/8 and the last quarter's at 7*span/8, so
        # the two medians are three quarters of the run apart. The first draft
        # of this line divided by span/2 and reported 1.12 s of lag per second
        # of elapsed time — a rate above 1.0, which would mean the subject
        # travelling backwards through the stream, and it read as a plausible
        # headline right up until it was checked against the drain fraction.
        separation = span * 0.75
        rate = growth / separation if separation > 0 else 0.0
        print()
        print(f"    median lag rose {growth:+.2f} s across the run "
              f"({rate:+.3f} s per second of elapsed time)")
        # The independent derivation of the same quantity, from the message
        # counts alone: a socket receiving fraction f of the stream falls behind
        # by (1 - f) seconds per second. The two must agree, and their agreeing
        # is what rules out an artefact of the seq matching.
        rs, ss = ref.span_s(), sub.span_s()
        ref_rate = rate(len(ref.records), rs)
        if ref_rate > 0:
            frac = rate(len(sub.records), ss) / ref_rate
            print(f"    the drain fraction predicts {1.0 - frac:+.3f} s/s independently "
                  f"(subject gets {frac * 100:.1f}% of the stream)")
        if rate > 0.05:
            print("    => GROWING. Consistent with QUEUING: the subject is walking a backlog"
                  " and will not catch up.")
        elif abs(rate) <= 0.05 and statistics.median(last) > 1.0:
            print("    => PLATEAUED at a non-zero lag. A BOUNDED queue; the ceiling is the"
                  " number above.")
        else:
            print("    => FLAT AND SMALL. Consistent with SHEDDING: the subject gets fewer"
                  " messages but current ones.")
        print("    Read this beside the per-kind table. If the two disagree, the"
              " disagreement is the finding.")


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--url", default="wss://anvil.garethcooke.com/ws")
    p.add_argument("--ticker", type=int, default=101)
    p.add_argument("--seconds", type=float, default=120.0)
    p.add_argument("--delay", type=float, default=0.25,
                   help="subject socket's per-message sleep, in SECONDS "
                        "(0.25 reproduces the 4 msg/s of the 2026-08-09 drain probe)")
    p.add_argument("--bins", type=int, default=8,
                   help="how many elapsed-time buckets the lag table is split into")
    p.add_argument("--reference-only", action="store_true",
                   help="one unthrottled socket only: measures Anvil's own per-kind rates, "
                        "which is how the 2.00/s summary denominator in staleness.hpp is "
                        "checked")
    args = p.parse_args(argv)

    url = f"{args.url}?ticker={args.ticker}"
    ref = Socket("reference", url, 0.0)
    sockets = [ref]
    if not args.reference_only:
        sockets.append(Socket(f"subject({args.delay * 1000:.0f}ms)", url, args.delay))

    print(f"Anvil freshness probe — {args.seconds:.0f}s, {len(sockets)} socket(s) on {url}")
    if not args.reference_only:
        print(f"  reference drains flat out; subject sleeps {args.delay * 1000:.0f} ms per"
              f" message. Matching on wire seq.")

    deadline = time.monotonic() + args.seconds
    threads = [threading.Thread(target=s.run, args=(deadline,), daemon=True) for s in sockets]
    # Started together deliberately: a reference that connected first would have
    # a head start on the subject and would report it as lag.
    for t in threads:
        t.start()
    for t in threads:
        t.join(timeout=args.seconds + 30.0)

    failed = False
    for s in sockets:
        if s.error is not None:
            print(f"  !! {s.name}: {type(s.error).__name__}: {s.error}")
            failed = True

    _rate_table(sockets)
    if len(sockets) == 2 and len(ref.records) >= 2:
        _lag_table(ref, sockets[1], args.bins)
        _backlog(ref, sockets[1])

    if args.reference_only:
        n_sum = ref.kinds().get("summary", 0)
        sum_span = ref.kind_span_s("summary")
        if sum_span > 0:
            r = rate(n_sum, sum_span)
            print()
            print(f"  summary broadcast: {n_sum} in {sum_span:.1f} s = {r:.4f}/s over"
                  f" {n_sum - 1} intervals (staleness.hpp divides by 2.0000/s)")
            print(f"  period {1e3 / r:.1f} ms against the assumed 500.0 ms"
                  f"  ({(r / 2.0 - 1) * 100:+.3f}%)")
            print("  A rate materially away from 2.0000/s on an UNTHROTTLED socket means the"
                  " estimator's denominator is wrong and every age it prints is scaled by"
                  " the ratio. RUN THIS FOR MINUTES, NOT SECONDS: at n=120 the estimator's"
                  " own sampling error is still ~0.03%, but any rate computed as n/span"
                  " rather than (n-1)/span is biased +0.84% at that n — which is how a"
                  " 60-second run once reported 2.017/s for a broadcast that is exact.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
