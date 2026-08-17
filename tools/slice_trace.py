#!/usr/bin/env python3
"""Slice a full local capture into a small committed replay trace.

The full captures (see tools/capture_anvil.py) run at ~100 KB/s, so a 5-minute
baseline is ~30 MB — too heavy to commit forever. We keep the full capture local
and untracked, and commit only a representative window sliced from it. This keeps
git light while the full capture stays available on the owner's box for deeper
analysis. (Policy set in M0; reused for Kraken/Binance traces from M4.)

Lines are copied **verbatim** — the tool parses each line only to read rx_ns /
type for the time filter, and writes the original text — so the committed slice
preserves byte-identical frames and the metadata header (line 1).

**The metadata header is copied through untouched, which is how the M4 stage-A
`venue` tag survives a slice** (deliverable 1). Nothing here rewrites, reorders
or re-serialises line 1: `meta_line` is the original text and is written back as
it was read. That is a property worth naming rather than relying on, because a
slice whose header lost its venue tag would read back as an Anvil trace and be
fed to the Anvil adapter.

Modes:
  baseline   keep every frame within --window seconds of the first frame.
  reconnect  keep the window [resync - --before, resync + --after] seconds around
             the first mid-stream `snapshot` (the resync), so the committed trace
             carries the pre-drop tail, the gap itself, and the post-resync
             stream.

Python 3 stdlib only; lives in tools/.
"""
from __future__ import annotations

import argparse
import json
import sys
from typing import NamedTuple

from tracefile import check_meta, clock_of, is_book_event, is_snapshot


class Frame(NamedTuple):
    """One capture line: the verbatim text plus the fields slicing reads."""

    raw: str
    rx_ns: int
    is_snapshot: bool   # replaces the book (venue-aware; see tracefile)
    is_book_event: bool  # reaches the book at all
    is_tx: bool


def load(path: str):
    with open(path, encoding="utf-8") as f:
        raw = f.read().splitlines()
    if not raw:
        sys.exit(f"empty trace: {path}")
    meta_line = raw[0]
    # Validate the header before slicing rather than after. This tool WRITES a
    # committed artefact, so it is the right place to hold the venue-conditional
    # metadata contract: a slice cut from a header the C++ reader will reject is
    # a file nobody finds out about until ctest does, and a slice of an unknown
    # venue would be cut with the wrong dialect's idea of a snapshot.
    try:
        meta = json.loads(meta_line)
    except json.JSONDecodeError as exc:
        sys.exit(f"{path}: line 1: metadata header is not JSON ({exc})")
    try:
        venue = check_meta(meta)
    except ValueError as exc:
        sys.exit(f"{path}: line 1: {exc}")
    frames = []
    for line_no, line in enumerate(raw[1:], start=2):
        if not line.strip():
            continue
        # Line numbers in errors, so a malformed capture says where — the C++
        # reader on the other half of this pipeline has always done so, and a
        # bare KeyError traceback naming only this script was the odd one out.
        try:
            obj = json.loads(line)
            frame = obj["frame"]
            # frames are copied verbatim; only read type when present as an
            # object (capture_anvil.py preserves whatever the server sent),
            # mirroring its own isinstance guard so a non-object frame does not
            # crash slicing.
            frames.append(Frame(line, obj["rx_ns"],
                                is_snapshot(venue, frame),
                                is_book_event(venue, frame),
                                obj.get("dir") == "tx"))
        except (json.JSONDecodeError, KeyError, TypeError, AttributeError) as exc:
            sys.exit(f"{path}: line {line_no}: malformed capture line ({exc})")
    return meta_line, meta, venue, frames


def _between(frames, lo: int, hi: int):
    """Verbatim text of every frame whose rx_ns falls in [lo, hi]."""
    return [f.raw for f in frames if lo <= f.rx_ns <= hi]


def slice_baseline(frames, window_s: float, start_s: float = 0.0):
    """Keep [start_s, start_s + window_s) seconds from the first record.

    `start_s` was added 2026-08-17 for the quiet-pair extreme slice, where the
    interesting stretch is 65 s into a 600 s capture. Offsets are measured from
    the FIRST RECORD, which in a Kraken capture is the subscribe we sent -- the
    same origin `dc_taxonomy` and `gap_stats` report against, so a window quoted
    from one of those reports can be handed straight to this flag.

    A window that starts mid-stream is a legitimate committed trace and the
    reader already handles it: the venue-free resync rule asks what came BEFORE a
    snapshot, so a slice beginning after the on-connect snapshot simply has none.
    """
    t0 = frames[0].rx_ns + int(start_s * 1e9)
    return _between(frames, t0, t0 + int(window_s * 1e9))


def find_resync(frames):
    """rx_ns of the first snapshot that is a RESYNC, or None.

    ONE RULE, EVERY VENUE: a snapshot with a book event before it in the trace.
    It is the same rule accumulate() applies in harness/src/trace.cpp, and it is
    the same rule deliberately — if the tool that cuts the window and the reader
    that counts resyncs disagreed, they would disagree about the same file.

    The reasoning is written out at that C++ call site. In short: the old
    Anvil-shaped test ("not the trace's first record") is wrong at Kraken, whose
    on-connect snapshot arrives third, and it is wrong in the direction that
    produces a plausible file — a slice named `reconnect` centred on the
    connection's own opening snapshot, with no reconnect in it.
    """
    seen_book_event = False
    for f in frames:
        if f.is_snapshot and seen_book_event:
            return f.rx_ns
        if f.is_book_event:
            seen_book_event = True
    return None


def slice_reconnect(frames, before_s: float, after_s: float, venue: str):
    resync = find_resync(frames)
    if resync is None:
        sys.exit(f"no resync snapshot found in this {venue} capture "
                 f"(no snapshot has a book event before it) — not a reconnect capture?")
    return _between(frames, resync - int(before_s * 1e9), resync + int(after_s * 1e9))


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("input", help="full local capture NDJSON")
    p.add_argument("output", help="committed slice NDJSON")
    p.add_argument("--mode", choices=("baseline", "reconnect"), required=True)
    p.add_argument("--window", type=float, default=90.0,
                   help="baseline: seconds of trace to keep")
    p.add_argument("--start", type=float, default=0.0,
                   help="baseline: seconds into the capture to start the window "
                        "(default 0 = from the first record)")
    p.add_argument("--before", type=float, default=30.0,
                   help="reconnect: seconds of pre-drop tail to keep")
    p.add_argument("--after", type=float, default=60.0,
                   help="reconnect: seconds after resync to keep")
    args = p.parse_args(argv)

    meta_line, meta, venue, frames = load(args.input)
    if not frames:
        sys.exit("no frames in input")
    if args.mode == "baseline":
        kept = slice_baseline(frames, args.window, args.start)
    else:
        kept = slice_reconnect(frames, args.before, args.after, venue)

    with open(args.output, "w", encoding="utf-8", newline="\n") as out:
        # Verbatim, byte for byte, including the venue tag and the clock name.
        out.write(meta_line + "\n")
        for raw in kept:
            out.write(raw + "\n")

    span = 0.0
    if kept:
        first = json.loads(kept[0])["rx_ns"]
        last = json.loads(kept[-1])["rx_ns"]
        span = (last - first) / 1e9
    # The clock is named in the summary because a slice's gap figures are only
    # readable beside it, and this is the moment an operator is looking.
    sys.stderr.write(
        f"[slice] {venue} {args.mode} (clock {clock_of(meta)}): {len(kept)} frames "
        f"over {span:.1f}s -> {args.output}\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
