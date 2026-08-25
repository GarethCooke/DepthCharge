#!/usr/bin/env python3
"""What a Kraken v2 book subscription actually costs, per depth, in bytes as sent.

The Kraken half of `anvil_frame_economics.py`, written for M4 stage 0. Same
discipline, different venue, and the differences are the point:

  * **Anvil sends full replaces; Kraken sends deltas.** So the Anvil tool's
    central question ("what would a delta encoding cost?") is already answered
    here by the wire, and the interesting question inverts: what would this book
    cost if it were re-sent whole the way Anvil's is? That number is what makes
    the two venues comparable at all, and it is priced below as
    `full-replace equivalent`.
  * **Anvil quotes prices as JSON strings; Kraken puts bare JSON numbers on the
    wire.** `json.loads` -> `json.dumps` destroys them -- `0.50930100` comes
    back as `0.509301` -- so a byte count taken from a re-serialised frame would
    be a measurement of Python's float repr. Every byte figure here is
    `len(raw.encode("utf-8"))` over the verbatim capture text, and every derived
    figure (truncation, full-replace equivalent) is rebuilt from the original
    number *tokens*, never from floats. `--verify` proves that reconstruction is
    byte-exact, which is what makes those figures measurements rather than
    estimates -- the same claim `anvil_frame_economics.py --verify` exists to
    support, transplanted to a venue where it is much easier to get wrong.

Sharing rather than cloning: the capture reader is `tools/tracefile.py`, used by
both tools, extracted under the brief's rule (the Anvil tool's output over all
four committed traces did not move).

**The CRC32 here is an analysis instrument, not the adapter.** M4 stage 0 writes
no `engine/` code and no C++ checksum. This one exists because it is the only
way to answer three of the brief's known-unknowns from a capture: whether the
checksum is computable from the verbatim text (yes), whether it is computable
from a re-serialised copy (no -- and that is invariant #3 with a number against
it), and whether the trace is complete, since a delta stream that checksums
clean end to end has provably lost nothing.

Usage:
    python tools/kraken_frame_economics.py <trace.ndjson>
    python tools/kraken_frame_economics.py <trace> --verify
    python tools/kraken_frame_economics.py <trace> --truncate 27
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import zlib
from collections import defaultdict
from pathlib import Path

from tracefile import read_capture, read_meta, ticks

SIDES = ("bids", "asks")

# The two byte rates M4 is sized against, both measured rather than assumed.
# 30.8 KiB/s is what the board draws from Anvil today at depth=27 (ROADMAP A7);
# 56 KiB/s is the worst hour of the 23.6 h soak (DESIGN strain 19).
ANVIL_TODAY_KIBS = 30.8
SOAK_WORST_HOUR_KIBS = 56.0

PERCENTILES = (50.0, 90.0, 99.0, 99.9)

# Kraken's CRC32 covers the top 10 levels only, whatever depth was subscribed.
CHECKSUM_LEVELS = 10

# --- the known-answer self-check ---------------------------------------------
# Every figure in NOTES-kraken.md and in the M4 stage-0 brief comes out of this
# file, and this file had TWO bugs in the session that produced it -- a
# truncation column that discarded every level entering the top N, and a verify
# pass whose float shadow silently skipped 92 real checksum comparisons. Both
# printed entirely believable numbers. The firmware's instruments are held to a
# known-answer standard on the host (`test_replay_goldens.cpp` and friends); the
# Python ones were not, and that gap cost real time twice in one evening.
#
# So: the committed slices are now goldens for the TOOL, not only inputs to it.
# `--selfcheck` recomputes them and fails loudly on any move. A moved number
# means one of exactly two things, and both want a human:
#   * the tooling regressed  -- fix the tool; or
#   * a trace was re-captured -- then these expectations move WITH it, in the
#     same commit, and NOTES-kraken.md's figures move too.
# Never "adjust the expectation until it passes". The point of the row is that it
# was measured once, by code that was then reviewed.
#
# ---------------------------------------------------------------------------
# ADD ROWS. NEVER REGENERATE THE TABLE.
# ---------------------------------------------------------------------------
# A new trace's figures are produced by the very tool these pins exist to guard,
# so a wholesale regeneration launders a drifted tool into a green suite: the new
# numbers would agree with the bug by construction, and the old rows -- the only
# thing that could have contradicted it -- would be gone. The existing rows are
# the independent check. `--pin` therefore refuses to emit a row for a trace that
# already has one, so replacing a row requires deleting it first, which is
# visible in a diff and needs a sentence in a commit message.
#
# THE PROCEDURE, in order, every time:
#   1. run the mutants     -- prove the check still fails when the tool is broken
#                             (host: the three in the M4 stage-0 session log;
#                             at minimum: delete the truncate step, keep leading
#                             zeros in crc_token, widen CHECKSUM_LEVELS)
#   2. run --selfcheck     -- prove every EXISTING row still passes, unmodified
#   3. run --pin <trace>   -- take the new row from a tool just shown to be sound
#   4. commit              -- the new row, the trace, and the notes together
# Step 2 before step 3 is the whole point: figures are only worth pinning if the
# tool that produced them was known-good at the moment it produced them.
#
# Mutation-verified rather than assumed, 2026-08-16: removing the truncation,
# keeping leading zeros in the checksum token, and widening the CRC to the top 12
# are each caught, and the unmutated table passes.
#
# `evicted` and `stale_reentries` were ADDED to the existing rows on 2026-08-16
# under exactly the rule above -- the other eight figures were re-checked and
# passed unchanged first, so the tool was known-sound at the moment the two new
# ones were taken from it. Adding a FIELD is not regenerating a ROW.
#
# PINNED_FIELDS is the single source of truth for which figures a row carries.
# It was three lists at review time -- this tuple, `pin()`'s emitter, and a prose
# comment -- and the failure mode was quiet: add a figure, forget one list, and
# `selfcheck` would compare only the keys a row happened to have and stay green
# over an unchecked field. `selfcheck` now requires every row's keys to be exactly
# this set, so the omission fails instead of hiding.
PINNED_FIELDS: tuple[str, ...] = (
    "frames", "checksummed", "ok_truncating", "ok_never_truncating", "ok_from_float",
    "roundtrip_survivors", "price_decimals", "qty_decimals", "evicted",
    "stale_reentries")

# --- traces this tool deliberately cannot grade ------------------------------
# An unpinned trace is a FAILURE, not a skip -- that rule stands and is the whole
# reason this table exists. But "cannot be graded" is a third state, distinct
# from both "pinned" and "forgotten", and silently skipping it would recreate the
# hole the rule closed. So an ungradeable trace is listed HERE, WITH ITS REASON,
# and `--selfcheck` reports it as an explicit exclusion rather than passing over
# it or failing on it.
#
# Adding a row here is a claim that needs the same scrutiny as a pinned figure:
# it says "no checksum in this file can ever match, and that is a property of the
# capture rather than a bug in the tool."
NOT_A_CHECKSUM_GOLDEN: dict[str, str] = {
    "kraken_minagbp_d25_20260817.ndjson":
        "a MID-STREAM window (t+65..t+160 s of a 600 s capture), so it carries no "
        "book/snapshot to baseline from and every one of its 49 checksums is "
        "computed against a book built from deltas applied to nothing: 0/49, by "
        "construction and at any depth. It is the LIVENESS golden for the "
        "2026-08-17 staleness ruling -- 25,843 ms of healthy book silence with the "
        "heartbeat unbroken -- and the checksum/truncation goldens are the d10 and "
        "d25 BTC/USD slices, which is why those are separate files. Note also that "
        "its truncation column would read CATCHES for the wrong reason (see the "
        "guard in verify()), which is the second reason not to grade it.",
}

KNOWN_ANSWERS: dict[str, dict[str, int]] = {
    # `ok_never_truncating` is the column to read first, and it is the criterion
    # rather than a proxy: strictly below `checksummed` means this trace CATCHES a
    # missing truncation. It is not predictable from the subscribed depth, and
    # `evicted` is if anything anti-correlated with it -- depth 100 evicts hardest
    # of the three BTC captures and detects nothing at all.
    "kraken_btcusd_d10_20260816.ndjson": dict(
        frames=901, checksummed=839, ok_truncating=839, ok_never_truncating=379,
        ok_from_float=0, roundtrip_survivors=509, price_decimals=1, qty_decimals=8,
        evicted=275, stale_reentries=169),
    "kraken_btcusd_d25_20260816.ndjson": dict(
        frames=1598, checksummed=1537, ok_truncating=1537, ok_never_truncating=460,
        ok_from_float=0, roundtrip_survivors=815, price_decimals=1, qty_decimals=8,
        evicted=585, stale_reentries=11),
    "kraken_btcusd_d100_20260816.ndjson": dict(
        frames=2534, checksummed=2472, ok_truncating=2472, ok_never_truncating=2472,
        ok_from_float=0, roundtrip_survivors=1106, price_decimals=1, qty_decimals=8,
        evicted=1156, stale_reentries=0),
    "kraken_minagbp_d25_20260816.ndjson": dict(
        frames=92, checksummed=30, ok_truncating=30, ok_never_truncating=30,
        ok_from_float=0, roundtrip_survivors=67, price_decimals=4, qty_decimals=8,
        evicted=19, stale_reentries=0),
    # THE RESYNC SLICE (M4 stage B2). Two subscriptions on one connection, so 19
    # checksums split across two books -- and 19/19 clean, which is the figure
    # that says the second baseline is as good as the first.
    #
    # `ok_never_truncating` reads 19 as well, so THIS TRACE DOES NOT GUARD THE
    # TRUNCATION RULE and must never be cited as if it did. Two independent
    # reasons, both already on record: the quiet pair does not retrace enough
    # ranks for a wrongly-kept level to re-enter the top 10 (see
    # `stale_reentries` and its counterexample above), and a resync HEALS a
    # non-truncating book outright because `replace()` clears every level -- so a
    # trace containing one is a WORSE truncation golden than a trace without.
    # The d10 and d25 BTC/USD slices hold that role and this one holds the
    # resync; the tool prints the same warning after `--pin` rather than leaving
    # it to be inferred from a column.
    "kraken_minagbp_d25_resync_20260818.ndjson": dict(
        frames=96, checksummed=19, ok_truncating=19, ok_never_truncating=19,
        ok_from_float=0, roundtrip_survivors=91, price_decimals=4, qty_decimals=8,
        evicted=3, stale_reentries=0),
}


class RawNum(str):
    """A JSON number kept as the exact text the venue sent.

    Everything in this tool that touches a price or a quantity touches one of
    these. It is a `str` subclass so it cannot be arithmetic'd by accident:
    invariant #3 says no float ever touches book data, and the way that rule
    gets broken is not by a considered decision but by a `float()` somewhere
    convenient.
    """


def loads_raw(text: str):
    """Parse, keeping every number as its original token text."""
    return json.loads(text, parse_float=RawNum, parse_int=RawNum)


def dumps_raw(obj) -> str:
    """Re-serialise with number tokens emitted verbatim and compact separators."""
    if isinstance(obj, RawNum):
        return str(obj)
    if isinstance(obj, str):
        return json.dumps(obj, ensure_ascii=False)
    if obj is True:
        return "true"
    if obj is False:
        return "false"
    if obj is None:
        return "null"
    if isinstance(obj, list):
        return "[" + ",".join(dumps_raw(x) for x in obj) + "]"
    if isinstance(obj, dict):
        return "{" + ",".join(json.dumps(k, ensure_ascii=False) + ":" + dumps_raw(v)
                              for k, v in obj.items()) + "}"
    raise TypeError(f"unexpected {type(obj).__name__} in a capture frame")


def wire_bytes(text: str) -> int:
    """Bytes as sent. Measured off the capture, never re-serialised."""
    return len(text.encode("utf-8"))


def frame_kind(frame) -> str:
    """`channel/type`, `channel`, or `ack:<method>` -- Kraken has no one `type`.

    A REFUSED method ack is labelled separately, and that is not cosmetic. A
    failed subscribe carries both `method` and `error`, so classifying on
    `method` first would file it as an ordinary ack and hide it in a count --
    while the socket stays up and heartbeats at 1 Hz with a permanently empty
    book. That is the invariant #5 trap NOTES-kraken.md describes, and an
    instrument that cannot see it is no use for finding it in a capture.
    """
    if not isinstance(frame, dict):
        return "?"
    channel = frame.get("channel")
    if channel is not None:
        kind = frame.get("type")
        return f"{channel}/{kind}" if kind else str(channel)
    if frame.get("method") is not None:
        failed = frame.get("success") is False or frame.get("error") is not None
        return "ack:" + str(frame["method"]) + (" REFUSED" if failed else "")
    return "error" if frame.get("error") is not None else "?"


def crc_token(text: str) -> str:
    """Kraken's checksum spelling of one number: no decimal point, no leading zeros."""
    return text.replace(".", "").lstrip("0")


class Book:
    """The maintained book for one symbol, keyed by integer ticks.

    Keyed by ticks rather than by the price token because two spellings of one
    price (`62766.0` and `62766`) would otherwise be two levels; the original
    token is carried alongside, because the checksum is computed from the text
    and not from the number.
    """

    def __init__(self) -> None:
        # side -> {price_ticks: (price_text, qty_text)}
        self.levels: dict[str, dict[int, tuple[str, str]]] = {s: {} for s in SIDES}
        self.price_decimals = 0
        self.qty_decimals = 0
        self.max_held = {s: 0 for s in SIDES}
        # Levels dropped by the truncate step -- pushed out of the window by
        # something better arriving. NECESSARY for a trace to detect a missing
        # truncation, and measured 2026-08-16 to be nowhere near sufficient: the
        # depth-100 capture evicts at a HIGHER rate than depth-25 and detects
        # nothing. See `stale_reentries` below for the condition that does
        # discriminate.
        self.evicted = 0
        # Prices this book evicted and has not seen return, per side.
        self._evicted_prices: dict[str, set] = {s: set() for s in SIDES}
        # A MECHANISM PROBE, and explicitly NOT the criterion -- recorded with its
        # counterexample because it was briefly believed to be one.
        #
        # The idea was that a non-truncating client goes wrong once a level it
        # wrongly kept lands back inside the CHECKSUMMED top 10, which needs the
        # touch to retrace ~(depth - 10) ranks. It fits the four committed slices
        # exactly (169, 11 -> both detect; 0, 0 -> neither does) and is FALSIFIED
        # by the fifth capture taken the same evening: the reconnect trace scores
        # 3 re-entries and detects nothing (840/840). Two reasons, both certain
        # rather than inferred:
        #   * a price can only re-enter this book because the venue SENT it, and
        #     that same message corrects the non-truncating book too; and
        #   * `replace()` clears every level, so a resync snapshot HEALS a
        #     non-truncating book outright -- a trace containing a reconnect is a
        #     worse truncation golden than one without.
        # What actually goes wrong is subtler: the non-truncating book carries
        # extra levels the venue has stopped mentioning, and it is wrong when one
        # of those ranks inside its own best 10 while the venue's does not.
        #
        # So the criterion is the direct one, `ok_never_truncating < checksummed`
        # -- a measurement, not a proxy. This counter stays because the pair
        # (evicted, stale_reentries) is a useful description of why a given trace
        # does or does not exercise the rule; it is not a substitute for running
        # the check.
        self.stale_reentries = 0

    def apply(self, data: dict, *, truncate: int | None = None) -> int:
        """Apply one message's levels; return how many actually changed state."""
        changed = 0
        for side in SIDES:
            book = self.levels[side]
            for lv in data.get(side, []):
                px_text, qty_text = str(lv["price"]), str(lv["qty"])
                px, pdec = ticks(px_text)
                qty, qdec = ticks(qty_text)
                self.price_decimals = max(self.price_decimals, pdec)
                self.qty_decimals = max(self.qty_decimals, qdec)
                if qty == 0:
                    if book.pop(px, None) is not None:
                        changed += 1
                else:
                    before = book.get(px)
                    if before is None or ticks(before[1])[0] != qty:
                        changed += 1
                    book[px] = (px_text, qty_text)
            if truncate is not None:
                if len(book) > truncate:
                    for px in self.ordered(side)[truncate:]:
                        del book[px]
                        self.evicted += 1
                        self._evicted_prices[side].add(px)
                # Did anything previously evicted come back inside the checksummed
                # window? Counted once per return, then forgotten, so a level that
                # sits in the top 10 for a hundred messages counts once.
                for px in self.ordered(side)[:CHECKSUM_LEVELS]:
                    if px in self._evicted_prices[side]:
                        self._evicted_prices[side].discard(px)
                        self.stale_reentries += 1
            # Sampled AFTER the truncation, so the figure answers "how deep is
            # the book this client holds" rather than "how deep did it get
            # part-way through applying one message" -- which is one level
            # larger and means nothing.
            self.max_held[side] = max(self.max_held[side], len(book))
        return changed

    def replace(self, data: dict) -> None:
        self.levels = {s: {} for s in SIDES}
        # The eviction HISTORY dies with the book it described. A snapshot replaces
        # every level, so a price evicted before it has no stale copy left anywhere
        # and counting its later return as a "stale re-entry" would be measuring
        # nothing. Found in review: without this, the one figure that quantifies
        # how a resync HEALS a non-truncating book was itself contaminated by the
        # resync. `evicted` is cumulative over the capture and deliberately not
        # reset -- it counts work the venue made the client do, not live state.
        for side in SIDES:
            self._evicted_prices[side].clear()
        self.apply(data)

    def ordered(self, side: str) -> list[int]:
        """Prices best-first: bids high to low, asks low to high."""
        return sorted(self.levels[side], reverse=(side == "bids"))

    def checksum(self) -> int:
        """Kraken's CRC32: top 10 asks ascending, then top 10 bids descending."""
        parts = []
        for side in ("asks", "bids"):
            for px in self.ordered(side)[:CHECKSUM_LEVELS]:
                px_text, qty_text = self.levels[side][px]
                parts.append(crc_token(px_text))
                parts.append(crc_token(qty_text))
        return zlib.crc32("".join(parts).encode("ascii"))

    def full_replace_text(self, symbol: str, depth: int) -> str:
        """This book as Anvil would send it: every level, every message.

        Rebuilt from the stored number tokens, so it is byte-exact arithmetic on
        real wire spellings rather than an estimate from a mean level size.
        """
        body = {"symbol": symbol}
        for side in SIDES:
            body[side] = [{"price": RawNum(self.levels[side][px][0]),
                           "qty": RawNum(self.levels[side][px][1])}
                          for px in self.ordered(side)[:depth]]
        body["checksum"] = RawNum("4294967295")
        return dumps_raw({"channel": "book", "type": "snapshot", "data": [body]})


def truncated_text(frame: dict, keep_by_symbol: dict[str, dict[str, set]]) -> str:
    """The same message carrying only the levels in `keep`, rebuilt token-exact.

    `keep_by_symbol` is {symbol: {side: {price_ticks}}} because one message may
    in principle carry several symbols, each with its own top-N.
    """
    out = json.loads(dumps_raw(frame), parse_float=RawNum, parse_int=RawNum)
    for entry in out["data"]:
        keep = keep_by_symbol.get(str(entry["symbol"]))
        if keep is None:
            continue
        for side in SIDES:
            if side in entry:
                entry[side] = [lv for lv in entry[side]
                               if ticks(str(lv["price"]))[0] in keep[side]]
    return dumps_raw(out)


def percentile(ordered: list[float], pct: float) -> float:
    """Nearest-rank, no interpolation -- the rule gap_stats.py already uses."""
    if not ordered:
        return float("nan")
    return ordered[max(1, math.ceil(pct / 100.0 * len(ordered))) - 1]


def _float_text(value) -> str:
    """How a level's number reads after a plain float parse and re-print.

    This is not a strawman: it is what `json.loads` gives any tool that did not
    stop to think, and what a naive C++ `double` + `std::to_string` path would
    produce a variant of. It exists so invariant #3 has a number behind it here
    rather than only a rule.
    """
    return repr(value)


def measure(trace: Path) -> tuple[dict, dict]:
    """Run the checks and return (graded checks, measured figures).

    Split out from the printing at the owner's request so `--selfcheck` compares
    the same numbers `--verify` reports, rather than re-deriving them by a second
    route that could agree with a bug.

    The interesting figures are the two checksum runs. Kraken's rule is that the
    client truncates to its subscribed depth itself, because the venue does not
    send a removal for a level that merely falls out of scope. That reads like
    housekeeping until you check the checksum both ways: the top-10 CRC is a
    complete test of whether the maintained book IS the venue's book, so a
    truncating client and a non-truncating one are separated by it exactly.
    """
    checks = {
        "rebuilding a frame from its number TOKENS is byte-exact "
        "(so truncated and full-replace figures are measurements)": 0,
        "the venue's checksum matches, from the VERBATIM text, with the book "
        "TRUNCATED to the subscribed depth": 0,
        "no price or qty arrives in exponent notation": 0,
        "rx_ns never decreases": 0,
    }
    names = list(checks)
    depth = int(read_meta(trace).get("depth", 0)) or None
    total = 0
    roundtrip_differs = 0
    checksummed = 0
    kept_ok = 0          # checksum matches WITHOUT truncating
    float_ok = 0         # checksum matches from float-printed text
    float_exponent = 0   # ... and how often floats produce exponent notation
    trunc: dict[str, Book] = {}
    kept: dict[str, Book] = {}
    floated: dict[str, Book] = {}
    last_rx = -1

    for rec in read_capture(trace):
        total += 1
        if rec.rx_ns < last_rx:
            checks[names[3]] += 1
        last_rx = rec.rx_ns

        frame = loads_raw(rec.raw)
        if dumps_raw(frame) != rec.raw:
            checks[names[0]] += 1
        # The same frame through a plain float parse, which is what any tool
        # that did not think about it would do.
        if json.dumps(json.loads(rec.raw), separators=(",", ":"),
                      ensure_ascii=False) != rec.raw:
            roundtrip_differs += 1

        if frame.get("channel") != "book":
            continue
        snapshot = frame.get("type") == "snapshot"
        float_frame = json.loads(rec.raw)

        for entry, f_entry in zip(frame["data"], float_frame["data"]):
            symbol = str(entry["symbol"])
            a = trunc.setdefault(symbol, Book())
            b = kept.setdefault(symbol, Book())
            c = floated.setdefault(symbol, Book())
            f_levels = {"symbol": symbol}
            for side in SIDES:
                f_levels[side] = [{"price": RawNum(_float_text(lv["price"])),
                                   "qty": RawNum(_float_text(lv["qty"]))}
                                  for lv in f_entry.get(side, [])]
            # The venue's own text. An exponent token here would be a finding
            # about Kraken, and is graded as one.
            try:
                if snapshot:
                    a.replace(entry)
                    b.replace(entry)
                else:
                    a.apply(entry, truncate=depth)
                    b.apply(entry)
            except ValueError:
                checks[names[2]] += 1
                continue
            # The float shadow, in its OWN try: its failures are the experiment's
            # result, and must not skip the checks above the way they did in the
            # first draft of this function -- which silently dropped 92 of the
            # venue's own checksums on the floor.
            try:
                c.replace(f_levels) if snapshot else c.apply(f_levels, truncate=depth)
            except ValueError:
                float_exponent += 1
            if "checksum" not in entry:
                continue
            want = int(str(entry["checksum"]))
            checksummed += 1
            if a.checksum() != want:
                checks[names[1]] += 1
            if b.checksum() == want:
                kept_ok += 1
            if c.checksum() == want:
                float_ok += 1

    figures = {
        "depth": depth,
        "frames": total,
        "checksummed": checksummed,
        "ok_truncating": checksummed - checks[names[1]],
        "ok_never_truncating": kept_ok,
        "ok_from_float": float_ok,
        "float_exponent_msgs": float_exponent,
        "roundtrip_survivors": total - roundtrip_differs,
        # Both counted on the correctly-truncating book. `evicted` is necessary,
        # `stale_reentries` is what actually discriminates -- see Book.__init__.
        "evicted": sum(b.evicted for b in trunc.values()),
        "stale_reentries": sum(b.stale_reentries for b in trunc.values()),
        # One symbol per capture in practice; `max` keeps the figure defined if a
        # future multi-symbol trace arrives, rather than picking one arbitrarily.
        "price_decimals": max((b.price_decimals for b in kept.values()), default=0),
        "qty_decimals": max((b.qty_decimals for b in kept.values()), default=0),
        "held_truncating": {s: dict(b.max_held) for s, b in trunc.items()},
        "held_never_truncating": {s: dict(b.max_held) for s, b in kept.items()},
    }
    return checks, figures


def verify(trace: Path) -> int:
    """`--verify`: run the checks and report them, plus the ungraded figures."""
    checks, f = measure(trace)

    print(f"{trace}\n{f['frames']:,} frames checked, "
          f"{f['checksummed']:,} carrying a checksum\n")
    failures = 0
    for name, bad in checks.items():
        failures += bad
        print(f"  [{'FAIL' if bad else ' ok '}] {name}" + (f"  ({bad:,} violations)" if bad else ""))

    print("\n  measured, not graded -- the two ways to get this venue wrong:")
    if f["checksummed"]:
        print(f"    checksum OK truncating to depth={f['depth']} (the rule) : "
              f"{f['ok_truncating']:,} / {f['checksummed']:,}")
        catches = f["ok_never_truncating"] < f["checksummed"]
        print(f"    checksum OK if the client never truncates          : "
              f"{f['ok_never_truncating']:,} / {f['checksummed']:,}"
              f"   <- {'CATCHES' if catches else 'MISSES'} a missing truncation")
        # The mechanism behind that verdict, and the reason it is not predictable
        # from the subscribed depth: eviction rate is if anything ANTI-correlated
        # with detection across these captures.
        per_k = 1000.0 * f["evicted"] / f["checksummed"]
        print(f"      mechanism: {f['evicted']:,} levels evicted "
              f"({per_k:.1f} per 1,000 book msgs), "
              f"{f['stale_reentries']:,} returning to the checksummed top {CHECKSUM_LEVELS}")
        if not catches:
            print("      -- fine as coverage; do NOT rely on this trace to guard "
                  "the truncation rule")
        print(f"    checksum OK from float-parsed level text           : "
              f"{f['ok_from_float']:,} / {f['checksummed']:,}"
              f"   ({f['float_exponent_msgs']:,} messages printed exponent notation, "
              f"which the checksum has no spelling for)")
    print(f"    frames whose text survives json.loads -> json.dumps: "
          f"{f['roundtrip_survivors']:,} / {f['frames']:,}")
    print("    => byte counts and checksums here are taken from the verbatim capture "
          "text.\n       A tool that re-serialised first would be measuring Python.")
    for symbol, held in f["held_never_truncating"].items():
        t = f["held_truncating"][symbol]
        print(f"    {symbol}: levels held per side -- truncating "
              f"{t['bids']}/{t['asks']} max, never truncating "
              f"{held['bids']}/{held['asks']} max")
    # Outside the loop: these are whole-capture maxima, and printing them per
    # symbol would show one symbol's figure against another's name.
    print(f"    price decimals {f['price_decimals']}, qty decimals {f['qty_decimals']}")
    if failures:
        print(f"\n  {failures:,} check violation(s) -- see above")
    return 1 if failures else 0


def pin(traces: list[Path]) -> int:
    """`--pin`: emit a KNOWN_ANSWERS row for a trace that does not have one.

    Refuses any trace that is already pinned. That is the add-only rule made
    mechanical rather than remembered: to replace a row you must delete it from
    this file first, which shows up in a diff and wants a sentence in the commit
    message. Read the procedure above KNOWN_ANSWERS before using this -- in
    particular, `--selfcheck` must pass on the existing rows first, or the figures
    this prints came out of a tool nothing has vouched for.
    """
    ungradeable = [t for t in traces if t.name in NOT_A_CHECKSUM_GOLDEN]
    if ungradeable:
        for t in ungradeable:
            print(f"  [REFUSED] {t.name} is listed in NOT_A_CHECKSUM_GOLDEN and cannot "
                  f"be pinned: {NOT_A_CHECKSUM_GOLDEN[t.name]}", file=sys.stderr)
        return 1
    already = [t for t in traces if t.name in KNOWN_ANSWERS]
    if already:
        for t in already:
            print(f"  [REFUSED] {t.name} is already pinned. Add rows only -- see the "
                  f"procedure above KNOWN_ANSWERS. To replace this row deliberately, "
                  f"delete it from tools/kraken_frame_economics.py first.",
                  file=sys.stderr)
        return 1
    print("  # paste into KNOWN_ANSWERS, having run the mutants and --selfcheck first")
    for trace in traces:
        checks, got = measure(trace)
        if sum(checks.values()):
            print(f"  # {trace.name}: NOT PINNED -- {sum(checks.values()):,} graded check "
                  f"violation(s). Run --verify; a trace that fails its own checks is not "
                  f"a golden.", file=sys.stderr)
            return 1
        args = ", ".join(f"{k}={got[k]}" for k in PINNED_FIELDS)
        print(f'    "{trace.name}": dict(\n        {args}),')
        if got["checksummed"] and got["ok_truncating"] == 0:
            print(f"  # {trace.name}: its truncation verdict is MEANINGLESS -- not one "
                  f"checksum matches even WITH truncation (0/{got['checksummed']}), so "
                  f"'never-truncating scores lower' is true only because everything "
                  f"fails. A mid-stream window with no snapshot does this.",
                  file=sys.stderr)
        if got["ok_never_truncating"] >= got["checksummed"]:
            print(f"  # NOTE: {trace.name} MISSES a missing truncation "
                  f"(never-truncating scores {got['ok_never_truncating']}/"
                  f"{got['checksummed']}).\n  #       Fine as a coverage trace; do not "
                  f"rely on it to guard that rule. If you captured it to guard that "
                  f"rule,\n  #       re-capture: a liquid pair at a shallower depth, and "
                  f"without a reconnect in the window.", file=sys.stderr)
    return 0


def selfcheck(traces: list[Path]) -> int:
    """`--selfcheck`: hold this tool to the numbers it produced when reviewed.

    Not a re-measurement of Kraken -- a regression test of the measuring code
    against committed inputs. See KNOWN_ANSWERS for what a failure means.
    """
    rc = 0
    checked = 0
    for trace in traces:
        excluded = NOT_A_CHECKSUM_GOLDEN.get(trace.name)
        if excluded is not None:
            if trace.name in KNOWN_ANSWERS:
                print(f"  [FAIL] {trace.name}: listed BOTH as pinned and as "
                      f"ungradeable. One of the two rows is wrong.")
                rc = 1
                continue
            print(f"  [excl] {trace.name}: not a checksum golden -- {excluded}")
            continue
        expected = KNOWN_ANSWERS.get(trace.name)
        if expected is None:
            # A FAILURE, not a skip. A silently-skipped trace is the same species
            # of hole this whole check exists to close: adding or renaming a
            # committed Kraken trace without recording its figures would leave a
            # green test that measures nothing about it.
            print(f"  [FAIL] {trace.name}: no known answer on record. Add one to "
                  f"KNOWN_ANSWERS (run --verify to get the figures) rather than "
                  f"leaving this trace unpinned.")
            rc = 1
            continue
        # Compare over PINNED_FIELDS, not over the row's own keys: a row that is
        # missing a field must FAIL rather than quietly leave that field unchecked.
        missing = [k for k in PINNED_FIELDS if k not in expected]
        extra = [k for k in expected if k not in PINNED_FIELDS]
        checks, got = measure(trace)
        bad = [f"{k}: expected {expected[k]:,}, measured {got[k]:,}"
               for k in PINNED_FIELDS if k in expected and got[k] != expected[k]]
        if missing:
            bad.append(f"row is missing pinned field(s): {', '.join(missing)}")
        if extra:
            bad.append(f"row carries unknown field(s): {', '.join(extra)}")
        graded = sum(checks.values())
        if graded:
            bad.append(f"{graded:,} graded check violation(s) -- run --verify to see which")
        checked += 1
        if bad:
            rc = 1
            print(f"  [FAIL] {trace.name}")
            for line in bad:
                print(f"           {line}")
        else:
            print(f"  [ ok ] {trace.name}: {len(expected)} figures match, "
                  f"{got['checksummed']:,} checksums clean")
    if not checked:
        print("  no traces with known answers were checked", file=sys.stderr)
        return 1
    print(f"\n  {checked} trace(s) checked against KNOWN_ANSWERS: "
          f"{'FAIL' if rc else 'all match'}")
    return rc


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace", type=Path, nargs="+",
                    help="ndjson capture(s) from tools/capture_kraken.py")
    ap.add_argument("--truncate", type=int, action="append", default=None,
                    help="levels/side the panel would keep (repeatable; default 27)")
    ap.add_argument("--verify", action="store_true",
                    help="check the properties the figures rest on; non-zero on failure")
    ap.add_argument("--selfcheck", action="store_true",
                    help="hold this TOOL to the figures it produced when reviewed, over the "
                         "committed slices; non-zero on any move. Run by ctest.")
    ap.add_argument("--pin", action="store_true",
                    help="print a KNOWN_ANSWERS row for a NEW trace. Refuses one that is "
                         "already pinned -- add rows, never regenerate the table. Run the "
                         "mutants and --selfcheck first; see the procedure in this file.")
    args = ap.parse_args(argv)
    keeps = args.truncate or [27]

    missing = [t for t in args.trace if not t.is_file()]
    if missing:
        for t in missing:
            print(f"{t}: no such file", file=sys.stderr)
        return 1

    if args.pin:
        return pin(args.trace)
    if args.selfcheck:
        return selfcheck(args.trace)
    if args.verify:
        return max(verify(t) for t in args.trace)
    # Pricing, one report per trace. This used to re-enter main() with a rebuilt
    # argv, which worked and read like a puzzle; a named function taking the two
    # things it needs is the same behaviour without the indirection.
    return max(price(trace, keeps) for trace in args.trace)


def price(trace: Path, keeps: list[int]) -> int:
    """Print the full economics report for one capture."""
    meta = read_meta(trace)
    count: dict[str, int] = defaultdict(int)
    size: dict[str, int] = defaultdict(int)
    rx: list[int] = []
    book_rx: list[int] = []

    books: dict[str, Book] = {}
    levels_per_msg: list[int] = []
    changed_per_msg: list[int] = []
    update_bytes = 0
    full_replace_bytes = 0
    trunc_bytes: dict[int, int] = defaultdict(int)
    snapshot_levels = 0
    depth = int(meta.get("depth", 0))

    try:
        for rec in read_capture(trace):
            kind = frame_kind(rec.frame)
            count[kind] += 1
            size[kind] += wire_bytes(rec.raw)
            rx.append(rec.rx_ns)

            if not (isinstance(rec.frame, dict) and rec.frame.get("channel") == "book"):
                continue
            book_rx.append(rec.rx_ns)
            frame = loads_raw(rec.raw)
            is_update = frame.get("type") != "snapshot"

            # Per-frame accounting lives OUT here, per-book accounting inside the
            # entry loop. `data` is an array and held exactly one entry in every
            # message captured at M4 stage 0 -- but pricing a whole frame once per
            # entry would double-count a multi-symbol message, so the byte columns
            # are charged once and the `keep` sets are unioned across entries.
            keep_by_symbol: dict[str, dict[str, set]] = {}
            frame_levels = 0
            frame_changed = 0

            for entry in frame["data"]:
                symbol = str(entry["symbol"])
                book = books.setdefault(symbol, Book())
                if not is_update:
                    book.replace(entry)
                    snapshot_levels += sum(len(entry.get(s, [])) for s in SIDES)
                    continue

                n_levels = sum(len(entry.get(s, [])) for s in SIDES)
                # What a client rendering only N levels a side would still have
                # had to be told. A level qualifies if it is in the top N AFTER
                # the message (it is on screen now) or was in the top N BEFORE it
                # (it just left the screen, or was removed) -- the union, not
                # either half. Judging on the pre-state alone silently discards
                # every new best bid, which is the one level a ladder cannot
                # afford to miss; the first draft of this loop did exactly that
                # and undercounted the truncated feed by up to 14%.
                pre_n = {(s, k): set(book.ordered(s)[:k]) for s in SIDES for k in keeps}
                changed = book.apply(entry, truncate=depth or None)
                keep_by_symbol[symbol] = {
                    keep_n: {s: pre_n[(s, keep_n)] | set(book.ordered(s)[:keep_n])
                             for s in SIDES}
                    for keep_n in keeps}
                frame_levels += n_levels
                frame_changed += changed
                full_replace_bytes += wire_bytes(
                    book.full_replace_text(symbol, depth or 10**6))

            if is_update and keep_by_symbol:
                levels_per_msg.append(frame_levels)
                changed_per_msg.append(frame_changed)
                update_bytes += wire_bytes(rec.raw)
                for keep_n in keeps:
                    trunc_bytes[keep_n] += wire_bytes(
                        truncated_text(frame, {sym: k[keep_n]
                                               for sym, k in keep_by_symbol.items()}))
    except ValueError as exc:
        print(f"{trace}: {exc}", file=sys.stderr)
        return 1

    frames = sum(count.values())
    total = sum(size.values())
    if not frames or len(rx) < 2:
        print(f"{trace}: not enough frames", file=sys.stderr)
        return 1
    span = (rx[-1] - rx[0]) / 1e9

    print(f"{trace}")
    print(f"  {meta.get('symbol')}  depth={meta.get('depth')}  "
          f"captured_at {meta.get('captured_at')}  clock={meta.get('clock')}")
    print(f"  {frames:,} frames over {span:.1f} s, {total:,} bytes on the wire "
          f"= {total / 1024 / span:.2f} KiB/s\n")

    print(f"  {'kind':<16}{'count':>7}{'bytes':>12}{'mean':>8}{'share':>9}{'KiB/s':>9}")
    for kind in sorted(size, key=lambda k: -size[k]):
        print(f"  {kind:<16}{count[kind]:>7}{size[kind]:>12,}{size[kind] // count[kind]:>8}"
              f"{100 * size[kind] / total:>8.1f}%{size[kind] / 1024 / span:>9.2f}")

    if levels_per_msg:
        ordered_levels = sorted(levels_per_msg)
        ordered_changed = sorted(changed_per_msg)
        print(f"\n  {len(levels_per_msg):,} `update` messages priced "
              f"(snapshot carried {snapshot_levels} levels)")
        print(f"    levels/message  : {sum(ordered_levels) / len(ordered_levels):.2f} mean, "
              f"p50 {percentile(ordered_levels, 50):.0f}, p90 {percentile(ordered_levels, 90):.0f}, "
              f"p99 {percentile(ordered_levels, 99):.0f}, max {ordered_levels[-1]}")
        print(f"    levels CHANGED  : {sum(ordered_changed) / len(ordered_changed):.2f} mean, "
              f"p50 {percentile(ordered_changed, 50):.0f}, p90 {percentile(ordered_changed, 90):.0f}, "
              f"p99 {percentile(ordered_changed, 99):.0f}, max {ordered_changed[-1]}")
        noop = sum(1 for c in changed_per_msg if c == 0)
        print(f"    messages that changed nothing at all: {noop:,} "
              f"({100 * noop / len(changed_per_msg):.1f}%)")

        print(f"\n  {'feed':<34}{'bytes':>12}{'KiB/s':>9}{'of update':>11}")
        print(f"  {'delta, as sent (today)':<34}{update_bytes:>12,}"
              f"{update_bytes / 1024 / span:>9.2f}{100.0:>10.1f}%")
        for keep_n in keeps:
            print(f"  {f'delta truncated to {keep_n}/side':<34}{trunc_bytes[keep_n]:>12,}"
                  f"{trunc_bytes[keep_n] / 1024 / span:>9.2f}"
                  f"{100 * trunc_bytes[keep_n] / update_bytes:>10.1f}%")
        print(f"  {'full replace each msg (Anvil model)':<34}{full_replace_bytes:>12,}"
              f"{full_replace_bytes / 1024 / span:>9.2f}"
              f"{100 * full_replace_bytes / update_bytes:>10.1f}%")

    print(f"\n  inter-message gaps (ms), nearest-rank percentiles")
    print(f"  {'counting rule':<34}{'n':>7}{'rate/s':>9}"
          + "".join(f"{'p' + f'{p:g}':>9}" for p in PERCENTILES) + f"{'max':>9}")
    for label, stamps in (("any received frame", rx), ("book-affecting (snapshot/update)", book_rx)):
        gaps = sorted((b - a) / 1e6 for a, b in zip(stamps, stamps[1:]))
        if not gaps:
            continue
        print(f"  {label:<34}{len(stamps):>7}{(len(stamps) - 1) / span:>9.2f}"
              + "".join(f"{percentile(gaps, p):>9.1f}" for p in PERCENTILES)
              + f"{gaps[-1]:>9.1f}")

    kibs = total / 1024 / span
    print(f"\n  headroom at {kibs:.2f} KiB/s sustained:")
    for label, ref in (("Anvil today (depth=27)", ANVIL_TODAY_KIBS),
                       ("soak worst hour", SOAK_WORST_HOUR_KIBS)):
        print(f"    vs {label:<26}{ref:>6.1f} KiB/s  ->  {ref / kibs:>6.2f}x headroom")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
