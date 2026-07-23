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


def load(path: str):
    with open(path, encoding="utf-8") as f:
        raw = f.read().splitlines()
    if not raw:
        sys.exit(f"empty trace: {path}")
    meta_line = raw[0]
    frames = []  # (raw_text, rx_ns, type)
    for line in raw[1:]:
        if not line.strip():
            continue
        obj = json.loads(line)
        frame = obj["frame"]
        # frames are copied verbatim; only read type when present as an object
        # (capture_anvil.py preserves whatever the server sent), mirroring its
        # own isinstance guard so a non-object frame does not crash slicing.
        ftype = frame.get("type") if isinstance(frame, dict) else None
        frames.append((line, obj["rx_ns"], ftype))
    return meta_line, frames


def slice_baseline(frames, window_s: float):
    t0 = frames[0][1]
    limit = t0 + int(window_s * 1e9)
    return [raw for (raw, rx, _t) in frames if rx <= limit]


def slice_reconnect(frames, before_s: float, after_s: float):
    resync = next((rx for (_r, rx, t) in frames[1:] if t == "snapshot"), None)
    if resync is None:
        sys.exit("no mid-stream snapshot found — not a reconnect capture?")
    lo = resync - int(before_s * 1e9)
    hi = resync + int(after_s * 1e9)
    return [raw for (raw, rx, _t) in frames if lo <= rx <= hi]


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("input", help="full local capture NDJSON")
    p.add_argument("output", help="committed slice NDJSON")
    p.add_argument("--mode", choices=("baseline", "reconnect"), required=True)
    p.add_argument("--window", type=float, default=90.0,
                   help="baseline: seconds from first frame to keep")
    p.add_argument("--before", type=float, default=30.0,
                   help="reconnect: seconds of pre-drop tail to keep")
    p.add_argument("--after", type=float, default=60.0,
                   help="reconnect: seconds after resync to keep")
    args = p.parse_args(argv)

    meta_line, frames = load(args.input)
    if not frames:
        sys.exit("no frames in input")
    if args.mode == "baseline":
        kept = slice_baseline(frames, args.window)
    else:
        kept = slice_reconnect(frames, args.before, args.after)

    with open(args.output, "w", encoding="utf-8", newline="\n") as out:
        out.write(meta_line + "\n")
        for raw in kept:
            out.write(raw + "\n")

    span = 0.0
    if kept:
        first = json.loads(kept[0])["rx_ns"]
        last = json.loads(kept[-1])["rx_ns"]
        span = (last - first) / 1e9
    sys.stderr.write(
        f"[slice] {args.mode}: {len(kept)} frames over {span:.1f}s "
        f"-> {args.output}\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
