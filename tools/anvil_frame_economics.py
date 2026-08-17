#!/usr/bin/env python3
"""Where Anvil's wire bytes actually go, and what two cheaper feeds would cost.

Run against a committed capture (`harness/replay/*.ndjson`) and it answers three
questions in one pass, in bytes as sent rather than as stored:

  1. Which frame type owns the wire?
  2. What would truncating each `book` frame to a rendered depth cost?  (Anvil's
     `ANVIL_BOOK_DEPTH` defaults to 0 = every resting level; DepthCharge renders
     27 a side and discards the rest -- ARCHITECTURE §5.)
  3. What would a delta encoding of `book` cost?

Written for the 2026-08-16 Anvil hand-over (`docs/anvil-handover-2026-08-16.md`),
whose figures are this script's output on `anvil_101_baseline_20260809.ndjson`.

Two things make the comparison honest, and both are deliberate:

  * **One base.** Every alternative feed is priced over the *same* set of `book`
    frames as the full-replace baseline it is compared against. The delta feed
    therefore pays full price for the first frame of each ticker, because a delta
    stream must open with a snapshot -- it has nothing to diff against.
  * **The delta estimate is an UPPER bound.** It keeps Anvil's exact JSON level
    shape, re-sends a whole level whenever any field moves, adds an explicit
    `{"price":...,"qty":0}` entry for every price that vanished, and charges a
    full-width `seq` in every envelope. A real encoder would beat it. If this
    script ever flatters the delta case, that is a bug -- `--verify` checks it.

Truncation assumes levels arrive **best-first**, so that a shallower book is a
prefix of a deeper one. PROTOCOL.md §3.1 guarantees exactly that ("top-N levels,
best-first"), and the whole `depth` ask rests on it.

Usage:
    python tools/anvil_frame_economics.py harness/replay/anvil_101_baseline_20260809.ndjson
    python tools/anvil_frame_economics.py <trace> --depth 27 --depth 32
    python tools/anvil_frame_economics.py <trace> --verify
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

from tracefile import read_capture

# The envelope a delta frame would still have to carry, charged in full and at
# the widest `seq` rather than the ~6 digits Anvil actually sends.
DELTA_ENVELOPE = b'{"type":"book_delta","seq":4294967295,"ticker":101,"bids":[],"asks":[]}'

SIDES = ("bids", "asks")


def wire_bytes(frame: dict) -> int:
    """Length of a frame as Anvil sends it: compact JSON, UTF-8, no capture wrapper."""
    return len(json.dumps(frame, separators=(",", ":"), ensure_ascii=False).encode("utf-8"))


def levels(frame: dict, side: str) -> dict[str, tuple]:
    """A side as {price string: (qty, orders)}. The price stays a string -- it is the
    wire's own decimal spelling, the only key stable across frames, and never parsed
    as a number here (invariant #3: no float touches book data)."""
    return {lv["price"]: (lv["qty"], lv["orders"]) for lv in frame.get(side, [])}


def sides_of(frame: dict) -> dict[str, dict]:
    return {side: levels(frame, side) for side in SIDES}


def delta_bytes(prev: dict[str, dict], cur: dict[str, dict]) -> tuple[int, int]:
    """(bytes, levels touched) for an upper-bound delta of one book against the last."""
    total = len(DELTA_ENVELOPE)
    touched = 0
    for side in SIDES:
        for price, value in cur[side].items():
            if prev[side].get(price) != value:
                touched += 1
                total += len(
                    b'{"price":"%s","qty":%d,"orders":%d},'
                    % (price.encode(), value[0], value[1])
                )
        for price in prev[side]:
            if price not in cur[side]:
                touched += 1
                total += len(b'{"price":"%s","qty":0},' % price.encode())
    return total, touched


def truncated_bytes(frame: dict, depth: int) -> int:
    """What this frame would weigh with at most `depth` levels a side (best-first prefix)."""
    shallow = dict(frame)
    for side in SIDES:
        shallow[side] = frame.get(side, [])[:depth]
    return wire_bytes(shallow)


def frames_in(trace: Path):
    """Yield (raw_frame_text, parsed_frame) per capture line, skipping the header.

    The reading itself moved to `tools/tracefile.py` at M4 stage 0, so this tool
    and the Kraken one cannot drift about what a capture line is. This wrapper
    keeps the two-tuple every call site here already expects.
    """
    for rec in read_capture(trace):
        yield rec.raw, rec.frame


def verify(trace: Path) -> int:
    """Check the properties every figure this script prints depends on."""
    checks = {
        "re-serialisation is byte-exact (wire_bytes is a measurement, not an estimate)": 0,
        "truncating deeper than the book is the identity": 0,
        "truncated size is non-decreasing in depth": 0,
        "delta never costs more than the full replace it replaces": 0,
    }
    names = list(checks)
    total = 0
    previous: dict[int, dict[str, dict]] = {}

    for raw, frame in frames_in(trace):
        if frame.get("type") != "book":
            if frame.get("type") == "snapshot":
                previous[frame.get("ticker")] = sides_of(frame)
            if json.dumps(frame, separators=(",", ":"), ensure_ascii=False) != raw:
                checks[names[0]] += 1
            total += 1
            continue

        total += 1
        full = wire_bytes(frame)
        if json.dumps(frame, separators=(",", ":"), ensure_ascii=False) != raw:
            checks[names[0]] += 1
        if truncated_bytes(frame, 10**6) != full:
            checks[names[1]] += 1
        sizes = [truncated_bytes(frame, d) for d in (1, 5, 27, 32, 128, 10**6)]
        if any(a > b for a, b in zip(sizes, sizes[1:])):
            checks[names[2]] += 1

        ticker = frame.get("ticker")
        current = sides_of(frame)
        if ticker in previous:
            nbytes, _ = delta_bytes(previous[ticker], current)
            if nbytes > full:
                checks[names[3]] += 1
        previous[ticker] = current

    print(f"{trace}\n{total:,} frames checked\n")
    failures = 0
    for name, bad in checks.items():
        failures += bad
        print(f"  [{'FAIL' if bad else ' ok '}] {name}" + (f"  ({bad:,} violations)" if bad else ""))
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace", type=Path, help="an ndjson capture from tools/capture_anvil.py")
    ap.add_argument("--depth", type=int, action="append", default=None,
                    help="levels/side to price a truncated feed at (repeatable; default 27 and 32)")
    ap.add_argument("--verify", action="store_true",
                    help="check the properties the figures rest on, and exit non-zero on any failure")
    args = ap.parse_args(argv)
    depths = args.depth or [27, 32]

    if not args.trace.is_file():
        print(f"{args.trace}: no such file", file=sys.stderr)
        return 1

    try:
        if args.verify:
            return verify(args.trace)

        count: dict[str, int] = defaultdict(int)
        size: dict[str, int] = defaultdict(int)
        trunc: dict[int, int] = defaultdict(int)

        previous: dict[int, dict[str, dict]] = {}
        priced_book = 0        # full-replace bytes over the frames priced below
        delta_book = 0
        priced_frames = 0
        touched_per_frame: list[int] = []
        levels_per_frame: list[int] = []

        for _raw, frame in frames_in(args.trace):
            kind = frame.get("type", "?")
            count[kind] += 1
            size[kind] += wire_bytes(frame)

            if kind not in ("book", "snapshot"):
                continue

            ticker = frame.get("ticker")
            current = sides_of(frame)

            if kind == "book":
                # Every alternative feed is priced over exactly these frames, so the
                # percentages below share one base. A book with no predecessor costs
                # a delta feed full price: a delta stream has to open with a snapshot.
                full = wire_bytes(frame)
                priced_book += full
                priced_frames += 1
                for d in depths:
                    trunc[d] += truncated_bytes(frame, d)
                if ticker in previous:
                    nbytes, ntouched = delta_bytes(previous[ticker], current)
                    delta_book += nbytes
                    touched_per_frame.append(ntouched)
                else:
                    delta_book += full
                levels_per_frame.append(sum(len(current[s]) for s in SIDES))

            previous[ticker] = current
    except ValueError as exc:
        print(f"{args.trace}: {exc}", file=sys.stderr)
        return 1

    frames = sum(count.values())
    total = sum(size.values())
    if not frames:
        print(f"{args.trace}: no frames", file=sys.stderr)
        return 1

    print(f"{args.trace}")
    print(f"{frames:,} frames, {total:,} bytes on the wire\n")
    print(f"{'type':<10}{'count':>8}{'bytes':>14}{'mean':>9}{'share':>9}")
    for kind in sorted(size, key=lambda k: -size[k]):
        print(f"{kind:<10}{count[kind]:>8}{size[kind]:>14,}"
              f"{size[kind] // count[kind]:>9}{100 * size[kind] / total:>8.1f}%")

    if not priced_frames:
        print("\nno `book` frames -- nothing to price")
        return 0

    def row(label: str, book_bytes: int) -> None:
        """One priced feed: its book bytes, against the book, and against the stream."""
        stream = 100 * (total - priced_book + book_bytes) / total
        print(f"  {label:<22}{book_bytes:>14,}"
              f"{100 * book_bytes / priced_book:>8.1f}%{stream:>10.1f}%")

    print(f"\n{priced_frames:,} `book` frames priced "
          f"({len(touched_per_frame):,} of them against a predecessor)")
    print(f"  levels/frame      : {sum(levels_per_frame) / len(levels_per_frame):.1f} mean")
    if touched_per_frame:
        ordered = sorted(touched_per_frame)
        print(f"  levels changed    : {sum(ordered) / len(ordered):.1f} mean, "
              f"p50 {ordered[len(ordered) // 2]}, "
              f"p90 {ordered[int(len(ordered) * 0.9)]}, "
              f"p99 {ordered[int(len(ordered) * 0.99)]}, "
              f"max {ordered[-1]}")
    print(f"\n  {'feed':<22}{'book bytes':>14}{'of book':>9}{'of stream':>11}")
    row("full replace (today)", priced_book)
    for d in depths:
        row(f"truncated to {d}/side", trunc[d])
    row("delta (upper bound)", delta_book)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
