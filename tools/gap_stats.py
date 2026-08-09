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
import json
import math
import sys

# The three counting rules, and which wire kinds each one considers a heartbeat.
# `None` means "every frame, whatever it is" — the host replay driver's rule,
# which cannot distinguish them because in a capture every frame parses.
COUNTING_RULES = (
    ("any frame (host replay driver)", None),
    ("event-producing (book/snapshot/trade)", {"book", "snapshot", "trade"}),
    ("book-affecting (book/snapshot)", {"book", "snapshot"}),
)

PERCENTILES = (50.0, 90.0, 99.0, 99.9)


def load(path: str):
    """Return (meta, [(rx_ns, type), ...]) from a capture NDJSON."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    if not lines:
        sys.exit(f"empty trace: {path}")
    meta = json.loads(lines[0])
    frames = []
    for line_no, line in enumerate(lines[1:], start=2):
        if not line.strip():
            continue
        try:
            obj = json.loads(line)
            frame = obj["frame"]
            ftype = frame.get("type") if isinstance(frame, dict) else None
            frames.append((obj["rx_ns"], ftype))
        except (json.JSONDecodeError, KeyError, TypeError, AttributeError) as exc:
            sys.exit(f"{path}: line {line_no}: malformed capture line ({exc})")
    return meta, frames


def gaps_ms(frames, kinds):
    """Inter-arrival gaps in ms between consecutive frames matching `kinds`."""
    times = [rx for rx, ftype in frames if kinds is None or ftype in kinds]
    return [(b - a) / 1e6 for a, b in zip(times, times[1:])], len(times)


def percentile(sorted_gaps, pct: float) -> float:
    """Nearest-rank percentile: the smallest sample at or above `pct` of them."""
    if not sorted_gaps:
        return float("nan")
    rank = max(1, math.ceil(pct / 100.0 * len(sorted_gaps)))
    return sorted_gaps[rank - 1]


def describe(path: str, top: int) -> None:
    meta, frames = load(path)
    if len(frames) < 2:
        sys.exit(f"{path}: need at least two frames")
    span_s = (frames[-1][0] - frames[0][0]) / 1e9
    kinds: dict[str, int] = {}
    for _, ftype in frames:
        kinds[ftype or "?"] = kinds.get(ftype or "?", 0) + 1

    print(f"== {path}")
    print(f"   captured_at {meta.get('captured_at')}  mode={meta.get('capture_mode')} "
          f"ticker={meta.get('ticker')}")
    print(f"   {len(frames)} frames over {span_s:.1f} s = {len(frames) / span_s:.2f} frames/s")
    print(f"   kinds: {kinds}")
    print()
    header = f"   {'counting rule':<40}{'n':>7}{'rate/s':>9}"
    for p in PERCENTILES:
        header += f"{('p' + (f'{p:g}')):>10}"
    header += f"{'max':>10}"
    print(header)
    print("   " + "-" * (len(header) - 3))
    for label, kinds_set in COUNTING_RULES:
        g, n = gaps_ms(frames, kinds_set)
        if not g:
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
        for label, kinds_set in COUNTING_RULES:
            g, _ = gaps_ms(frames, kinds_set)
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
    _, frames = load(path)
    span_s = (frames[-1][0] - frames[0][0]) / 1e9

    episodes = []  # each: dict(frame_before, gap_ms, watchdog_ns, gap_events, cleared*)
    open_ep = None
    for idx in range(1, len(frames)):
        prev_rx, _ = frames[idx - 1]
        rx, ftype = frames[idx]
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
        # A book/snapshot frame re-baselines the book and clears the grey window.
        if open_ep is not None and ftype in ("book", "snapshot"):
            open_ep["cleared_frame"] = idx + 1
            open_ep["cleared_rx"] = rx
            open_ep = None

    print(f"   watchdog at {threshold_ms:g} ms over {span_s:.1f} s: "
          f"{len(episodes)} stale episode(s)")
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
