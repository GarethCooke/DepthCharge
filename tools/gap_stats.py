#!/usr/bin/env python3
"""Inter-frame gap distribution for an Anvil capture, and what a threshold does to it.

This is the tool behind the RX watchdog number. M1 derived `kRxWatchdogMs = 1000`
from a max-gap measurement by hand; M3 re-derived it from a percentile after the
deployed server's cadence fell from ~15.5 msg/s to ~6 msg/s and the max stopped
being a stable statistic. Keeping the measurement in a committed tool rather than
in a session's scratch buffer is the point: the next cadence change re-runs this
and gets a comparable answer.

Two things it prints, from the same pass:

  distribution   inter-arrival gaps at p50 / p90 / p99 / p99.9 / max, under each
                 of the three counting rules the design distinguishes (any frame,
                 event-producing, book-affecting). The firmware watchdog arms on
                 a *book event*, the host replay driver on *any frame*, so a
                 threshold has to clear the worst of both.

  --threshold T  what a T-millisecond watchdog would do to this capture: every
                 hole longer than T, and the grey window it would open
                 (grey = gap - T, the replay driver's stale_ms). On a healthy
                 capture that list should be empty — each entry is a false grey.
                 On the reconnect capture it is the invariant-5 proof, and this
                 is where the golden's expected numbers come from: derived from
                 the trace by a second implementation, never captured from the
                 C++ under test.

Percentiles are nearest-rank on the sorted sample (no interpolation): at a p99.9
over a few thousand gaps the rank is what matters and an interpolated value would
invent a gap that never occurred.

Python 3 stdlib only; lives in tools/ (invariant #1 — nothing here touches the
engine, and no analysis code goes near the hot path).
"""
from __future__ import annotations

import argparse
import math
import sys
from typing import NamedTuple

from tracefile import (clock_of, is_book_event, is_liveness, is_trade,
                       read_capture, read_meta, record_kind, venue_of)

# The three counting rules, as PREDICATES over a classified record rather than
# as sets of Anvil's wire `type` names.
#
# M4 stage A review, 2026-08-17. They were sets — {"book","snapshot","trade"}
# and {"book","snapshot"} — matched against a bare `frame["type"]`, which is
# Anvil's complete kind vocabulary and nobody else's. At Kraken the kind is
# `channel` + `type`, so a book update ({"channel":"book","type":"update"})
# matched NEITHER set, both rules produced an empty gap list, and `describe()`
# dropped both rows from the table with no message and exit 0. The surviving
# "any frame" row reports the 1 Hz heartbeat's noise floor — measured 1,026 ms
# against a true worst book silence of 9,007 ms on the same file, 8.8x low.
#
# That is this tool's own headline being silently wrong at the second venue, and
# this tool is the one the repository cites for the RX watchdog number
# (`test_replay_goldens.cpp` names it as the origin of the golden expectations).
# The dialect now comes from tracefile.py, which is the one place both languages
# agree on what a book event is.
COUNTING_RULES = (
    ("any record arriving", None),
    ("LIVENESS signal -> GREY", lambda r: r.liveness),
    ("BOOK SILENCE (book + trades)", lambda r: r.book_event),
    ("BOOK SILENCE (book only)", lambda r: r.book_event and not r.trade),
)

# THE TWO DISTRIBUTIONS, AND WHY THE LABELS SHOUT (2026-08-17 ruling).
#
# Reading one of these as the other is the mistake the ruling exists to prevent,
# and this tool made it easy: the rows used to be called "any frame",
# "event-producing" and "book-affecting", three plausible names for three
# distributions with no hint that only one of them may drive a threshold.
#
#   LIVENESS  is the venue's declared liveness signal -- Anvil's `summary`,
#             Kraken's `heartbeat`. It is the ONLY one that decides the grey,
#             and `--threshold` is judged against it.
#   BOOK SILENCE is how long the book went without changing. **No threshold on
#             it can be correct at any venue**: a quiet market and a silently
#             dead subscription are identical on the wire. MINA/GBP was measured
#             at 25,843 ms of healthy book silence on a socket whose heartbeat
#             kept 936-1,042 ms time throughout.
#
# **RENAMED FROM "BOOK AGE" AT M4 STAGE A2, AND THE RENAME IS THE POINT.** Stage
# A called this row BOOK AGE on the strength of the ruling's "book-event silence
# becomes age_ms". The follow-up sharpened the definition and the two parted
# company: `age_ms` is the book's estimated QUEUING LAG, and it is explicitly
# **not** time since the book last changed -- *a book that has not changed is not
# old*. At MINA/GBP nobody traded and the displayed book was exactly correct
# throughout 25,843 ms of this row's measurement. So the two quantities differ by
# orders of magnitude and one of them was wearing the other's name, in the tool
# this repository cites for its watchdog numbers. Book silence is market
# information: it may be displayed, it must never drive a rendered state, and it
# is not the age. The age is computed from the LIVENESS row, not from this one --
# see `dc_harness/age_estimator.hpp`.
THRESHOLD_RULE = "LIVENESS signal -> GREY"

PERCENTILES = (50.0, 90.0, 99.0, 99.9)


class Rec(NamedTuple):
    """One received record, classified once by the venue's dialect."""

    rx_ns: int
    kind: str          # `book/update`, `trade`, `heartbeat`, ... — venue's spelling
    book_event: bool   # reaches the book — BOOK SILENCE, which is not the age
    trade: bool        # a trade print rather than a resting-level change
    liveness: bool     # the venue's declared liveness signal — GREY


def load(path: str):
    """Return (meta, venue, [Rec, ...]) from a capture NDJSON.

    Reads through `tracefile.read_capture`, which is the ONE definition of how a
    capture line is read, rather than through a private parser. This file used to
    carry its own — the same drift ARCHITECTURE §9 (2026-08-07) recorded between
    the two C++ readers, in Python, and it cost two things beyond the duplication:
    the private reader never skipped a `"dir": "tx"` record, so our own subscribe
    was counted as venue traffic and put a bogus interval at the head of every
    Kraken gap distribution; and it never read the venue tag, which is what let
    the counting rules above go silently blind.
    """
    meta = read_meta(path)
    venue = venue_of(meta)
    recs = []
    try:
        for r in read_capture(path):          # skip_tx defaults True — see above
            recs.append(Rec(r.rx_ns,
                            record_kind(venue, r.frame, r.is_tx),
                            is_book_event(venue, r.frame),
                            is_trade(venue, r.frame),
                            is_liveness(venue, r.frame)))
    except ValueError as exc:
        sys.exit(f"{path}: {exc}")
    return meta, venue, recs


def gaps_ms(recs, match):
    """Inter-arrival gaps in ms between consecutive records matching `match`."""
    times = [r.rx_ns for r in recs if match is None or match(r)]
    return [(b - a) / 1e6 for a, b in zip(times, times[1:])], len(times)


def instrument(meta, venue: str) -> str:
    """How this venue names the thing being captured.

    `ticker=` was printed unconditionally and reads as `ticker=None` on a Kraken
    trace, which is the header of the report claiming the capture has no
    instrument. The clock is printed beside it because a gap distribution is only
    readable next to the clock that produced it, and this tool prints
    distributions from two clocks that differ by four orders of magnitude.
    """
    who = (f"ticker={meta.get('ticker')}" if venue == "anvil"
           else f"symbol={meta.get('symbol')} depth={meta.get('depth')}")
    return f"venue={venue} {who} clock={clock_of(meta)}"


def percentile(sorted_gaps, pct: float) -> float:
    """Nearest-rank percentile: the smallest sample at or above `pct` of them."""
    if not sorted_gaps:
        return float("nan")
    rank = max(1, math.ceil(pct / 100.0 * len(sorted_gaps)))
    return sorted_gaps[rank - 1]


def describe(path: str, top: int) -> None:
    meta, venue, frames = load(path)
    if len(frames) < 2:
        sys.exit(f"{path}: need at least two frames")
    span_s = (frames[-1].rx_ns - frames[0].rx_ns) / 1e9
    kinds: dict[str, int] = {}
    for r in frames:
        kinds[r.kind] = kinds.get(r.kind, 0) + 1

    print(f"== {path}")
    print(f"   captured_at {meta.get('captured_at')}  mode={meta.get('capture_mode')} "
          f"{instrument(meta, venue)}")
    print(f"   {len(frames)} frames over {span_s:.1f} s = {len(frames) / span_s:.2f} frames/s")
    print(f"   kinds: {kinds}")
    print()
    header = f"   {'counting rule':<40}{'n':>7}{'rate/s':>9}"
    for p in PERCENTILES:
        header += f"{('p' + (f'{p:g}')):>10}"
    header += f"{'max':>10}"
    print(header)
    print("   " + "-" * (len(header) - 3))
    for label, match in COUNTING_RULES:
        g, n = gaps_ms(frames, match)
        if not g:
            # Say so rather than dropping the row. A rule with no matching
            # records used to vanish from the table, which is how this tool
            # reported a Kraken capture as though it had no book at all.
            print(f"   {label:<40}{n:>7}{'  (no gaps — rule matched nothing)':>28}")
            continue
        s = sorted(g)
        row = f"   {label:<40}{n:>7}{n / span_s:>9.2f}"
        for p in PERCENTILES:
            row += f"{percentile(s, p):>10.1f}"
        row += f"{s[-1]:>10.1f}"
        print(row)
    print()

    if top:
        print(f"   largest {top} gaps, by counting rule (ms):")
        for label, match in COUNTING_RULES:
            g, _ = gaps_ms(frames, match)
            worst = sorted(g, reverse=True)[:top]
            print(f"     {label:<40}{'  '.join(f'{v:.0f}' for v in worst)}")
        print()


def apply_threshold(path: str, threshold_ms: float) -> None:
    """Replay the watchdog rule over the capture, in Python, at `threshold_ms`.

    Mirrors dc::harness::run_replay's arithmetic deliberately and independently:
    a hole longer than the threshold opens an episode dated `prev_rx + threshold`,
    and the grey window runs from there to the frame that re-baselines the book
    (only a `snapshot` clears stale — `book` frames do too on this wire, since the
    adapter maps both to FeedEvent::Snapshot). Holes arriving while already grey
    fold into the open episode.

    Scope, so the parity claim is not read wider than it is: this models the
    in-stream watchdog only. `ReplayOptions::end_of_trace_silence_ms` — trailing
    silence the caller reports because a file has no "now" — has no equivalent
    here, because a capture's trailing silence is not a property of the capture.
    """
    _, _venue, all_recs = load(path)
    span_s = (all_recs[-1].rx_ns - all_recs[0].rx_ns) / 1e9
    # ARMED ON THE LIVENESS SIGNAL (2026-08-17 ruling), not on every arrival.
    # The old any-record arming is what let a 1 Hz heartbeat floor a Kraken
    # distribution at ~1 s and read as health; and a book-armed version would
    # grey a market that was merely quiet. The clearing frame stays a book event,
    # because what ends a grey window is the book being re-baselined.
    frames = [r for r in all_recs if r.liveness] or all_recs

    episodes = []  # each: dict(frame_before, gap_ms, watchdog_ns, gap_events, cleared*)
    open_ep = None
    for idx in range(1, len(frames)):
        prev_rx = frames[idx - 1].rx_ns
        rx = frames[idx].rx_ns
        gap_ms = (rx - prev_rx) / 1e6
        if gap_ms > threshold_ms:
            watchdog_ns = prev_rx + int(threshold_ms * 1e6)
            if open_ep is not None:
                open_ep["gap_events"] += 1
                open_ep["gap_ms"] = max(open_ep["gap_ms"], gap_ms)
            else:
                open_ep = {
                    "frame_before": idx,      # 1-based frame index, as the C++ counts
                    "gap_ms": gap_ms,
                    "watchdog_ns": watchdog_ns,
                    "gap_events": 1,
                    "cleared_frame": None,
                    "cleared_rx": None,
                }
                episodes.append(open_ep)
        # A liveness arrival clears the grey window: the feed is talking again.
        #
        # M4 stage A review, 2026-08-17: this tested `ftype in ("book",
        # "snapshot")`, Anvil's spelling for a full replace, so at Kraken nothing
        # after the on-connect snapshot could ever match and every hole folded
        # into one permanently-open episode ("never cleared", "grey nan ms",
        # exit 0). Since the ruling the series is liveness arrivals, so every
        # entry clears by construction — the loop keeps the shape because the
        # episode arithmetic is what the C++ driver is checked against.
        if open_ep is not None:
            open_ep["cleared_frame"] = idx + 1
            open_ep["cleared_rx"] = rx
            open_ep = None

    # Name the arming rule on the line. This function models `run_replay`, which
    # is armed on RECORD ARRIVAL — the weaker byte-cadence definition ARCHITECTURE
    # §9 (2026-08-09) says the host driver pins. At Anvil that is the same clock
    # as the book, so the distinction never mattered and was never printed. At
    # Kraken it is a different clock by 9x, and the table above now shows both:
    # a reader seeing a 25,843 ms book gap and "0 stale episode(s)" two lines
    # later needs to be told they are not the same question.
    print(f"   watchdog at {threshold_ms:g} ms over {span_s:.1f} s, armed on the "
          f"LIVENESS signal ({len(frames)} arrivals): {len(episodes)} stale episode(s)")
    for ep in episodes:
        if ep["cleared_rx"] is not None:
            grey = (ep["cleared_rx"] - ep["watchdog_ns"]) / 1e6
            cleared = f"cleared by frame {ep['cleared_frame']}"
        else:
            grey = float("nan")
            cleared = "never cleared"
        print(f"     after frame {ep['frame_before']}: gap {ep['gap_ms']:.1f} ms, "
              f"{ep['gap_events']} firing(s), grey {grey:.1f} ms, {cleared}")
    if episodes and span_s > 0:
        print(f"     => one grey every {span_s / len(episodes):.1f} s")
    print()


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("traces", nargs="+", help="capture NDJSON path(s)")
    p.add_argument("--threshold", type=float, action="append", default=None,
                   help="also replay a watchdog at this many ms (repeatable)")
    p.add_argument("--top", type=int, default=8,
                   help="print the N largest gaps per counting rule (0 = off)")
    args = p.parse_args(argv)

    for path in args.traces:
        describe(path, args.top)
        for t in args.threshold or []:
            apply_threshold(path, t)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
