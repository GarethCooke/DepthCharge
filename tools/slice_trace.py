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
import os
import sys
from typing import NamedTuple

from tracefile import check_meta, clock_of, is_book_event, is_snapshot, rebaselines


class Frame(NamedTuple):
    """One capture line: the verbatim text plus the fields slicing reads."""

    raw: str
    rx_ns: int
    is_snapshot: bool   # a snapshot RECORD (venue-aware; see tracefile)
    is_book_event: bool  # reaches the book at all
    is_tx: bool
    # After this frame the book is fully known. NOT is_snapshot: at Anvil a
    # `book` frame is a full replace too, and the reconnect trace this mode
    # exists for has no `type:"snapshot"` at all before its reconnect. See
    # tracefile.rebaselines().
    #
    # NO DEFAULT, deliberately. A defaulted False would let a future caller
    # construct a Frame without it and get "nothing ever re-baselines", which
    # makes the self-containment rule refuse every capture — a loud failure, but
    # a puzzling one a long way from its cause. Required, so the omission is a
    # TypeError at the call site instead.
    rebaselines: bool


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
                                obj.get("dir") == "tx",
                                rebaselines(venue, frame)))
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
    """INDEX of the first snapshot that is a RESYNC, or None.

    ONE RULE, EVERY VENUE: a snapshot with a book event before it in the trace.
    It is the same rule accumulate() applies in harness/src/trace.cpp, and it is
    the same rule deliberately — if the tool that cuts the window and the reader
    that counts resyncs disagreed, they would disagree about the same file.

    The reasoning is written out at that C++ call site. In short: the old
    Anvil-shaped test ("not the trace's first record") is wrong at Kraken, whose
    on-connect snapshot arrives third, and it is wrong in the direction that
    produces a plausible file — a slice named `reconnect` centred on the
    connection's own opening snapshot, with no reconnect in it.

    Returns the INDEX rather than the timestamp (it returned rx_ns until M4
    stage B2) because the containment rules below are about the frame's
    NEIGHBOURS, and a timestamp cannot be asked what came before it.
    """
    seen_book_event = False
    for i, f in enumerate(frames):
        if f.is_snapshot and seen_book_event:
            return i
        if f.is_book_event:
            seen_book_event = True
    return None


def last_baseline_before(frames, idx: int):
    """Index of the nearest frame at or before `idx - 1` that RE-BASELINES, or None.

    That frame is what the pre-resync deltas amend, and the predicate is
    `rebaselines` rather than `is_snapshot` because at Anvil they differ: a
    `book` frame is a full replace and re-baselines, while only a `type:
    "snapshot"` counts as a snapshot RECORD. So at Anvil this is almost always
    `idx - 1`; at a delta venue it is the connection's opening snapshot, which
    may be a long way back. That asymmetry is the whole content of the
    self-containment rule below — see tracefile.rebaselines() for the
    measurement that forced the distinction.
    """
    for j in range(idx - 1, -1, -1):
        if frames[j].rebaselines:
            return j
    return None


def _seconds_back(frames, idx: int, resync_idx: int) -> float:
    """How large --before would have to be to reach frames[idx]. Rounded up."""
    delta_ns = frames[resync_idx].rx_ns - frames[idx].rx_ns
    return delta_ns / 1e9


def slice_reconnect(frames, before_s: float, after_s: float, venue: str):
    """The window around the first resync — and it must CONTAIN what it claims.

    ========================================================================
    TWO RULES, AND BOTH WERE LEARNED FROM A SLICE THAT LOOKED FINE.
    ========================================================================

    **1. CONTAINMENT — the window must hold both endpoints of the event.** This
    is the `t+65..t+160` lesson generalised (M4 stage A: an order asked for
    t+70..t+140, and the fourth gap it claimed *closed* at t+158.1, so the file
    would have carried three gaps under a filename claiming four). A reconnect's
    event is the OUTAGE, and its endpoints are the last record before the hole
    and the resync snapshot that ends it. Cut the first one and the trace has no
    hole in it at all: the replay's watchdog is edge-triggered by the arrival of
    a frame after a silence, and a window that begins inside the silence has no
    "before" to measure from. The slice would be named `reconnect`, contain a
    snapshot, and demonstrate nothing.

    **2. SELF-CONTAINMENT — the pre-resync half must have its own baseline.**
    Left open deliberately at M4 stage A as "a capture-policy decision that
    belongs with the resync golden"; settled here, at B2, which is that golden.

    At a snapshot venue every frame re-baselines, so this costs Anvil nothing.
    At a DELTA venue the window's first frames are amendments to a book the
    window does not contain — and the adapter's rule since M4 stage B1 is *no
    baseline, no deltas*: they are counted and dropped, the book stays
    `Stale{Resync}`, and the panel is grey for the whole pre-resync half.
    The slice would then show a book that was never live going not-grey, which
    is not what a resync is. The measurement behind the rule is in
    NOTES-kraken.md: 49 mid-stream updates replayed onto an empty ladder
    reproduce **0 of 49** of the venue's checksums.

    REFUSE RATHER THAN EXTEND, and the reason is that this tool writes a
    COMMITTED artefact. Silently widening the window would make `--before` mean
    something other than what it says, and the operator would learn the real
    window from a file listing. Refusing costs one re-run with a number this
    function prints. A deliberately mid-stream window is a legitimate thing to
    want and `--mode baseline --start` is how to say so — that is how
    `kraken_minagbp_d25_20260817` was cut.
    """
    idx = find_resync(frames)
    if idx is None:
        sys.exit(f"no resync snapshot found in this {venue} capture "
                 f"(no snapshot has a book event before it) — not a reconnect capture?")

    resync = frames[idx].rx_ns
    lo = resync - int(before_s * 1e9)
    hi = resync + int(after_s * 1e9)

    # Rule 1. A resync always has a record before it (it needs a preceding book
    # event to be one), so idx >= 1 here and there is always something to check.
    if frames[idx - 1].rx_ns < lo:
        need = _seconds_back(frames, idx - 1, idx)
        sys.exit(
            f"--before {before_s:g} would cut the outage in half: the last record "
            f"before the resync is {need:.1f}s earlier, so the window would contain "
            f"the snapshot that ENDS the hole and not the silence itself. "
            f"A trace like that replays with no gap in it. "
            f"Use --before {_suggest(need)} or more.")

    # Rule 2.
    base = last_baseline_before(frames, idx)
    if base is None:
        sys.exit(
            f"this {venue} capture has nothing before the resync that re-baselines "
            f"the book, so no window can give the pre-resync half a baseline: the "
            f"capture itself begins mid-stream. Cut it with --mode baseline (a "
            f"mid-stream window is a legitimate committed trace; a mid-stream "
            f"RECONNECT slice is not).")
    if frames[base].rx_ns < lo:
        need = _seconds_back(frames, base, idx)
        sys.exit(
            f"--before {before_s:g} does not reach the baseline the pre-resync "
            f"deltas amend: the last snapshot before the resync is {need:.1f}s "
            f"earlier. Without it those deltas are amendments to nothing — dropped "
            f"by the adapter, and the panel is grey for the whole first half. "
            f"Use --before {_suggest(need)} or more.")

    return _between(frames, lo, hi)


def _suggest(seconds: float) -> str:
    """The next whole second above `seconds`, as a --before the operator can paste."""
    return f"{int(seconds) + 1}"




# ---------------------------------------------------------------------------
# --selfcheck (M4 stage B2)
# ---------------------------------------------------------------------------
#
# This tool WRITES COMMITTED ARTEFACTS and had no test of any kind until B2,
# which is the asymmetry `kraken_frame_economics.py`'s own `--selfcheck` was
# added to close for the other Python tool (CMakeLists.txt says why: it shipped
# two bugs that printed believable numbers).
#
# The cases below are synthesised rather than read from `harness/replay/`,
# deliberately. Every committed capture satisfies both containment rules — the
# one local Kraken reconnect capture fitted entirely inside its window, which is
# exactly why the defect was invisible for a milestone — so a corpus-driven test
# here would be five copies of one observation. This is ARCHITECTURE §9's
# 2026-08-18 rule applied to a Python tool: where the code and every available
# file agree, synthesise the input that discriminates.
#
# Each refusal case is its own mutation-verification: it asserts the check FIRES,
# and the two acceptance cases assert it does not fire always. A rule that
# refused everything would pass every refusal case here on its own.


def _frame(rx_s: float, *, snap: bool = False, delta: bool = False,
           replace: bool = False, tx: bool = False):
    """One synthetic capture line, in the three shapes that matter.

    `snap`    a snapshot RECORD: is_snapshot, and it re-baselines.
    `replace` Anvil's `book` — a full replace that is NOT a snapshot record.
    `delta`   Kraken's `book/update` — reaches the book, re-baselines nothing.

    The frame TEXT is venue-shaped rather than a placeholder, so the round-trip
    case at the end of `selfcheck` exercises `load()`'s own classification
    instead of trusting these flags twice. A placeholder passed the in-memory
    cases and made the round trip refuse its own fixture, which is the shape of
    mistake this whole selfcheck exists to catch.
    """
    rx_ns = int(rx_s * 1e9)
    if snap:
        frame = {"channel": "book", "type": "snapshot",
                 "data": [{"symbol": "BTC/USD", "bids": [], "asks": [], "checksum": 1}]}
    elif delta:
        frame = {"channel": "book", "type": "update",
                 "data": [{"symbol": "BTC/USD", "bids": [], "asks": [], "checksum": 1}]}
    elif replace:
        frame = {"type": "book", "ticker": 101, "bids": [], "asks": []}
    else:
        frame = {"channel": "heartbeat"}
    raw = json.dumps({"rx_ns": rx_ns, "frame": frame})
    return Frame(raw=raw, rx_ns=rx_ns, is_snapshot=snap,
                 is_book_event=snap or delta or replace, is_tx=tx,
                 rebaselines=snap or replace)


def _delta_venue_capture():
    """A Kraken-shaped reconnect: one opening snapshot, a long delta stream, a
    hole, then the resync snapshot and more deltas.

    The shape is the point. The baseline is at t=0 and the resync at t=40, so a
    window that reaches the outage (t=31.5) still misses the baseline by 30
    seconds — which is the normal case at a delta venue and cannot happen at a
    snapshot one."""
    frames = [_frame(0.0, snap=True)]
    frames += [_frame(t / 2.0, delta=True) for t in range(1, 64)]   # to t=31.5
    frames.append(_frame(40.0, snap=True))                          # the resync
    frames += [_frame(40.0 + t / 2.0, delta=True) for t in range(1, 20)]
    return frames


def _snapshot_venue_capture():
    """An Anvil-shaped reconnect, and it is shaped from the real file.

    `anvil_101_reconnect.ndjson` carries NO `type:"snapshot"` before its
    reconnect one — the stream is `book` frames, which are full replaces the
    adapter turns into `FeedEvent::Snapshot` without being snapshot RECORDS. So
    the resync is the reconnect snapshot (nothing earlier qualifies), and the
    nearest baseline is the record immediately before the hole. That is why
    rule 2 costs a snapshot venue nothing, and why it would refuse this file
    outright if it were written on `is_snapshot`."""
    frames = [_frame(t / 2.0, replace=True) for t in range(0, 64)]  # to t=31.5
    frames.append(_frame(40.0, snap=True))                          # the resync
    frames += [_frame(40.0 + t / 2.0, replace=True) for t in range(1, 20)]
    return frames


def _refuses(fn, needle: str) -> str:
    """Run `fn`, require a SystemExit whose message contains `needle`."""
    try:
        fn()
    except SystemExit as exc:
        msg = str(exc.code)
        if needle not in msg:
            raise AssertionError(f"refused for the wrong reason:\n  want ...{needle}...\n"
                                 f"  got  {msg}") from None
        return msg
    raise AssertionError(f"expected a refusal containing {needle!r}; it was accepted")


def selfcheck() -> int:
    import tempfile

    delta = _delta_venue_capture()
    snapshot_venue = _snapshot_venue_capture()
    checks = 0

    # --- the resync is found at all, and it is the SECOND snapshot ----------
    idx = find_resync(delta)
    assert idx is not None and delta[idx].rx_ns == int(40.0 * 1e9), idx
    # At the snapshot venue the resync is still the RECONNECT snapshot, not the
    # second record: `book` frames re-baseline but are not snapshot records, so
    # nothing earlier can be mistaken for a resync.
    assert snapshot_venue[find_resync(snapshot_venue)].rx_ns == int(40.0 * 1e9)
    assert find_resync([_frame(0.0, snap=True), _frame(1.0, delta=True)]) is None
    checks += 3

    # --- ACCEPTANCE: a window that contains everything it claims ------------
    kept = slice_reconnect(delta, before_s=45.0, after_s=5.0, venue="kraken")
    assert len(kept) == 1 + 63 + 1 + 10, len(kept)
    # The baseline really is in there — a rule that accepted while dropping it
    # would pass a bare length check on a different set of frames.
    assert kept[0] == delta[0].raw
    checks += 2

    # At a snapshot venue a --before that merely clears the OUTAGE is enough,
    # because the baseline is the record immediately before it. THIS IS THE CASE
    # THAT STOPS RULE 2 FROM BEING A RULE THAT REFUSES EVERYTHING — the same
    # 8.5s hole and the same 40s-old opening record as the delta fixture, and
    # here 9 seconds of --before is sufficient rather than 41.
    kept = slice_reconnect(snapshot_venue, before_s=9.0, after_s=1.0, venue="anvil")
    assert len(kept) == 2 + 1 + 2, len(kept)     # t=31.0/31.5, the resync, t=40.5/41.0
    checks += 1

    # --- REFUSAL 1: the window cuts the outage in half ----------------------
    # The last record before the resync is at t=31.5, the resync at t=40, so
    # --before 5 lands inside the silence and the file would carry no hole.
    msg = _refuses(lambda: slice_reconnect(delta, 5.0, 5.0, "kraken"),
                   "cut the outage in half")
    assert "--before 9" in msg, msg          # 8.5s back, rounded up
    checks += 2

    # --- REFUSAL 2: the window reaches the outage but not the baseline ------
    msg = _refuses(lambda: slice_reconnect(delta, 12.0, 5.0, "kraken"),
                   "does not reach the baseline")
    assert "--before 41" in msg, msg         # 40.0s back, rounded up
    checks += 2

    # And the boundary is not off by one in either direction: 41 accepts.
    assert len(slice_reconnect(delta, 41.0, 5.0, "kraken")) == 1 + 63 + 1 + 10
    checks += 1

    # --- REFUSAL 3: no baseline anywhere before the resync ------------------
    mid_stream = [_frame(t / 2.0, delta=True) for t in range(0, 20)]
    mid_stream.append(_frame(20.0, snap=True))
    msg = _refuses(lambda: slice_reconnect(mid_stream, 60.0, 5.0, "kraken"),
                   "begins mid-stream")
    assert "--mode baseline" in msg, msg      # it names the tool that CAN cut this
    checks += 2

    # --- REFUSAL 4: not a reconnect capture at all --------------------------
    _refuses(lambda: slice_reconnect([_frame(0.0, snap=True), _frame(1.0, delta=True)],
                                     30.0, 30.0, "kraken"),
             "no resync snapshot found")
    checks += 1

    # --- the metadata header survives a real round trip through main() ------
    # Claimed in this file's docstring since M0 and never checked. It is the
    # property that keeps a sliced Kraken trace from reading back as an Anvil
    # one, so it is worth a byte comparison rather than a promise.
    header = ('{"captured_at": "2026-08-18T20:33:55+00:00", "url": "wss://x/v2", '
              '"venue": "kraken", "symbol": "BTC/USD", "depth": 25, '
              '"tool_version": "0.2.0", "clock": "perf_counter_ns"}')
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "in.ndjson")
        dst = os.path.join(tmp, "out.ndjson")
        with open(src, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(header + "\n")
            for f in delta:
                fh.write(f.raw + "\n")
        main([src, dst, "--mode", "reconnect", "--before", "45", "--after", "5"])
        with open(dst, encoding="utf-8") as fh:
            out_lines = fh.read().splitlines()
        assert out_lines[0] == header, out_lines[0]
        assert len(out_lines) == 1 + 1 + 63 + 1 + 10, len(out_lines)
        checks += 2

    print(f"[slice] selfcheck OK: {checks} checks")
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("input", nargs="?", help="full local capture NDJSON")
    p.add_argument("output", nargs="?", help="committed slice NDJSON")
    p.add_argument("--mode", choices=("baseline", "reconnect"))
    p.add_argument("--selfcheck", action="store_true",
                   help="run this tool's own tests over synthesised captures and "
                        "exit. No input needed: the cases that matter are the ones "
                        "no committed capture contains.")
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
    if args.selfcheck:
        return selfcheck()
    if not args.input or not args.output or not args.mode:
        p.error("input, output and --mode are required (or pass --selfcheck)")

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
