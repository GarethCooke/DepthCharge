#!/usr/bin/env python3
"""One definition of how a DepthCharge capture line is read.

Extracted at M4 stage 0 for the same reason ``wsclient.py`` was, and under the
same rule: the second venue needed the reader, and the brief allowed a copy only
if extraction moved a byte of the Anvil tool's output. It did not -- checked by
running ``anvil_frame_economics.py`` over all four committed traces (plain,
``--verify``, and two ``--depth`` values) plus its error path, before and after,
and diffing the lot.

ARCHITECTURE §9 (2026-08-07) already has the general form of this: `read_trace()`
and `TraceReader` were written separately, drifted, and gave two answers to "is
this trace valid" in a project whose premise is that replay files are ground
truth. That was the C++ half. This is the Python half, before it can happen
again across two venues.

The format, set at M0 and unchanged:

  line 1  : metadata object
  line 2+ : {"rx_ns": <ns>, "frame": <verbatim JSON>}

with one addition at M4 stage 0: a line the capture tool *sent* rather than
received carries ``"dir": "tx"`` between the two (``capture_kraken.py``'s
subscribe). Received frames carry no ``dir`` key at all, so an Anvil trace is
byte-for-byte what it always was. Callers that are pricing the venue's wire, or
measuring inter-message gaps, must skip ``is_tx`` records -- our own upload is
not the venue's traffic, and the interval between the subscribe and the first
reply is not an inter-message gap.

**Frames are returned as raw text as well as parsed objects, and the raw text is
the authority.** At Anvil that was a nicety; at Kraken it is the whole game.
Kraken v2 puts prices and quantities on the wire as bare JSON numbers with
significant trailing zeros (``0.50930100``), which do not survive
``json.loads`` -> ``json.dumps``, so a byte count taken from a re-serialised
frame is a measurement of Python's float repr and not of the venue.

Python 3 stdlib only; lives in tools/.
"""
from __future__ import annotations

import json
from typing import NamedTuple

# Where the frame starts on a capture line. The capture tools write the wrapper
# with default separators and splice the frame in as its original compact text,
# so this recovers the exact bytes the venue sent.
FRAME_KEY = '"frame": '

# The marker capture_kraken.py puts on a line it sent rather than received.
TX_KEY = '"dir": "tx"'


class Record(NamedTuple):
    """One capture line."""

    raw: str        # the frame's verbatim text, exactly as the venue sent it
    frame: object   # the same text parsed (usually a dict)
    rx_ns: int      # monotonic ns at arrival (or at send, for a tx record)
    is_tx: bool     # True only for a frame this side sent
    lineno: int


def read_capture(path, *, skip_tx: bool = True):
    """Yield a Record per frame line. Raises ValueError with a line number.

    `skip_tx` defaults to True because every existing caller is measuring the
    venue, and a caller that wants the subscribe back has to say so.
    """
    with open(path, encoding="utf-8") as fh:
        header = fh.readline()
        if not header.strip():
            raise ValueError("empty trace (no capture header)")
        for lineno, line in enumerate(fh, start=2):
            line = line.strip()
            if not line:
                continue
            start = line.find(FRAME_KEY)
            if start < 0:
                raise ValueError(
                    f"line {lineno}: no {FRAME_KEY!r} field -- not a DepthCharge capture?")
            is_tx = TX_KEY in line[:start]
            if is_tx and skip_tx:
                continue
            raw = line[start + len(FRAME_KEY):].rstrip()
            if raw.endswith("}"):
                raw = raw[:-1]  # the capture wrapper's own closing brace
            try:
                frame = json.loads(raw)
            except json.JSONDecodeError as exc:
                raise ValueError(f"line {lineno}: unparseable frame ({exc})") from exc
            # rx_ns is read from the wrapper, which is this tool's own output and
            # therefore predictably shaped -- unlike the frame, which is the
            # venue's. A capture line always opens `{"rx_ns": <int>,`.
            try:
                rx_ns = int(line[len('{"rx_ns": '):line.index(",")])
            except ValueError as exc:
                raise ValueError(f"line {lineno}: no readable rx_ns ({exc})") from exc
            yield Record(raw, frame, rx_ns, is_tx, lineno)


def read_meta(path) -> dict:
    """Line 1: the capture's metadata header."""
    with open(path, encoding="utf-8") as fh:
        line = fh.readline()
    if not line.strip():
        raise ValueError("empty trace (no capture header)")
    return json.loads(line)
