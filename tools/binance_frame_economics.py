#!/usr/bin/env python3
"""What a Binance spot depth subscription actually costs, per stream, in bytes as sent.

The Binance half of `anvil_frame_economics.py` and `kraken_frame_economics.py`,
written for M5 stage 0. Same discipline, third venue.

CLONED RATHER THAN GENERALISED, AND THE REASON IS MEASURED
-----------------------------------------------------------
The brief's rule was the one that governed `wsclient.py` and `tracefile.py`:
share only if the existing tool's output stays byte-identical, otherwise clone
and say so with the reason. This is a clone, and the reason is not schedule
pressure -- it is that **the part of the Kraken tool that looks shareable is
exactly the part that does not transfer.**

`kraken_frame_economics.py`'s core machinery is `RawNum`, `loads_raw` and
`dumps_raw`: a `str` subclass and a hand-rolled serialiser whose entire purpose
is to keep a JSON *number token* as the text the venue sent, because Kraken puts
bare numbers on the wire (`0.50930100`) and `json.loads` -> `json.dumps` destroys
their trailing zeros. **Binance quotes every price and quantity as a string**
(`"78564.00000000"`), and a string survives a round trip unchanged. So this tool
needs none of it: `json.dumps(obj, separators=(",", ":"))` reproduces a Binance
frame byte-for-byte, and `--verify` proves that over every frame in every capture
rather than asserting it. Lifting the Kraken machinery into a shared module would
have moved ~200 lines whose only Binance caller would immediately not use them,
through a tool whose figures are `ctest`-pinned.

What IS shared is the thing that was already extracted and already proven:
`tools/tracefile.py`. This tool reads captures through it, including the two
record kinds M5 stage 0 introduced.

WHAT THIS MEASURES THAT THE OTHER TWO DO NOT
---------------------------------------------
  * **Two streams on one socket.** Every figure is per stream as well as per
    capture, because the whole `@depth20` question is what the second stream
    costs on top of the first.
  * **Levels CHANGED per message, against a maintained book.** `@depth` is a
    diff stream, so its level count is already a change count -- unless the venue
    restates a level at a quantity it already has, which is measured here rather
    than assumed. `@depth20` re-sends twenty levels a side every tick whether or
    not any moved, so for that stream the two numbers diverge hard, and the
    divergence IS the cost of the oracle.
  * **`U`/`u` continuity**, counted. Spot carries no `pu`, so the test is
    one-sided, and this reports what that one-sided test actually catches.
  * **Number precision as text**, which is section 7's whole question: does every
    quoted decimal carry exactly the precision `exchangeInfo` declares, and does
    exponent notation ever appear.

Usage:
    python tools/binance_frame_economics.py <trace.ndjson>
    python tools/binance_frame_economics.py <trace> --verify
    python tools/binance_frame_economics.py <traces...> --selfcheck
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

from tracefile import binance_payload, read_capture, read_meta

# The two byte rates M4 and M5 are sized against, both measured rather than
# assumed. 30.8 KiB/s is what the board draws from Anvil today at depth=27
# (ROADMAP A7); 56 KiB/s is the worst hour of the 23.6 h soak (DESIGN strain 19).
ANVIL_TODAY_KIBS = 30.8
SOAK_WORST_HOUR_KIBS = 56.0

# The board's own ceiling, from engine/. Section 7 recommends a REST `limit`
# against it. Kept as a literal with its source named rather than parsed out of a
# header, because this tool must run without a build.
K_MAX_SNAPSHOT_LEVELS = 256

# WebSocket frame headers, server->client (never masked): 2 bytes for a payload
# below 126, 4 below 65536. Exact, not an estimate -- and worth counting at a
# venue that sends many small frames at 10 Hz, where the header is a real share.
def ws_header_bytes(payload_len: int) -> int:
    if payload_len < 126:
        return 2
    if payload_len < 65536:
        return 4
    return 10


def wire_bytes(text: str) -> int:
    """Bytes as sent. Measured off the capture, never re-serialised."""
    return len(text.encode("utf-8"))


def percentile(ordered: list[float], pct: float) -> float:
    if not ordered:
        return 0.0
    k = (len(ordered) - 1) * pct / 100.0
    lo = int(k)
    hi = min(lo + 1, len(ordered) - 1)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (k - lo)


def decimals_of(text: str) -> int:
    """Fractional digit count of a quoted decimal, without touching a float."""
    _, _, frac = text.partition(".")
    return len(frac)


def ticks(text: str) -> tuple[int, int]:
    """(scaled integer, fractional digits). Digits are shifted, never rounded.

    Same spelling as the Kraken tool's, and deliberately so: the adapter will
    have to do exactly this from the token text on the hot path, and invariant #3
    says no float ever touches book data.
    """
    neg = text.startswith("-")
    body = text[1:] if neg else text
    if "e" in body or "E" in body:
        raise ValueError(f"exponent notation on the wire: {text!r}")
    whole, _, frac = body.partition(".")
    value = int(whole + frac) if (whole + frac) else 0
    return (-value if neg else value), len(frac)


class Book:
    """The maintained book for one stream, keyed by integer price ticks.

    Exists here for one purpose: to answer *how many of the levels in this
    message actually changed anything*. Keyed by ticks rather than by the price
    token so that two spellings of one price cannot become two levels.
    """

    def __init__(self) -> None:
        self.levels: dict[str, dict[int, str]] = {"b": {}, "a": {}}
        self.applied = 0
        self.noop_entries = 0     # a level restated at the quantity it already had
        self.removals = 0         # qty == 0
        self.phantom_removals = 0  # qty == 0 for a price the book does not hold

    def apply(self, side: str, price: str, qty: str) -> bool:
        """Apply one entry. Returns True if the book actually moved."""
        px, _ = ticks(price)
        held = self.levels[side].get(px)
        is_zero = ticks(qty)[0] == 0
        if is_zero:
            self.removals += 1
            if held is None:
                self.phantom_removals += 1
                return False
            del self.levels[side][px]
            return True
        if held is not None and ticks(held)[0] == ticks(qty)[0]:
            self.noop_entries += 1
            return False
        self.levels[side][px] = qty
        return True

    def replace(self, bids: list, asks: list) -> int:
        """Replace both sides wholesale (a partial-depth payload). Returns moves."""
        moved = 0
        for side, rows in (("b", bids), ("a", asks)):
            new = {}
            for price, qty in rows:
                px, _ = ticks(price)
                new[px] = qty
                old = self.levels[side].get(px)
                if old is None or ticks(old)[0] != ticks(qty)[0]:
                    moved += 1
            # levels that fell out of the window are changes too
            moved += sum(1 for px in self.levels[side] if px not in new)
            self.levels[side] = new
        return moved


class StreamStats:
    def __init__(self, name: str) -> None:
        self.name = name
        self.count = 0
        self.payload_bytes = 0
        self.header_bytes = 0
        self.levels = 0
        self.changed = 0
        self.rx: list[int] = []
        self.book = Book()
        self.is_diff = False
        self.is_partial = False
        # U/u continuity (diff streams only)
        self.first_U = None
        self.prev_u = None
        self.seq_ok = 0
        self.seq_break = 0
        self.seq_breaks: list[tuple[int, int]] = []
        # partial-depth ids, for the oracle's coincidence question
        self.last_update_ids: list[int] = []
        self.diff_u_values: set[int] = set()
        self.diff_brackets: list[tuple[int, int]] = []

    def gaps_ms(self) -> list[float]:
        return sorted((b - a) / 1e6 for a, b in zip(self.rx, self.rx[1:]))


def stream_name_of(meta: dict, frame) -> str:
    """Which stream a frame belongs to.

    From the combined-stream wrapper when there is one; otherwise the capture is
    single-stream by construction and the metadata header names it. NOTE the
    wrapper is not stripped from the byte counts -- `payload_bytes` is the whole
    frame as sent, wrapper included, because whether the wrapper is worth its
    bytes is one of the questions this tool exists to answer.
    """
    if isinstance(frame, dict) and "stream" in frame:
        return str(frame["stream"]).split("@", 1)[-1]
    streams = meta.get("streams") or ["?"]
    return streams[0]


def measure(trace: Path) -> tuple[dict, dict]:
    meta = read_meta(trace, validate=True)
    if meta.get("venue") != "binance":
        raise ValueError(f"{trace.name}: venue is {meta.get('venue')!r}, not 'binance'")

    streams: dict[str, StreamStats] = {}
    rest_ok = rest_fail = 0
    rest_ids: list[tuple[int, int]] = []   # (rx_ns, lastUpdateId)
    rest_levels: list[int] = []
    controls: Counter[str] = Counter()
    ping_latencies_ms: list[float] = []
    precision: Counter[tuple[str, int]] = Counter()
    exponent_tokens = 0
    level_entries = 0
    all_rx: list[int] = []

    for rec in read_capture(trace, kinds=("rest", "control")):
        if rec.kind == "control":
            ctl = rec.wrapper["ctl"]
            controls[ctl["op"]] += 1
            if ctl["op"] == "ping" and ctl.get("pong_ns"):
                ping_latencies_ms.append((ctl["pong_ns"] - ctl["recv_ns"]) / 1e6)
            continue
        if rec.kind == "rest":
            if rec.frame is None or "lastUpdateId" not in rec.frame:
                rest_fail += 1
                continue
            rest_ok += 1
            rest_ids.append((rec.rx_ns, rec.frame["lastUpdateId"]))
            rest_levels.append(len(rec.frame["bids"]) + len(rec.frame["asks"]))
            for rows in (rec.frame["bids"], rec.frame["asks"]):
                for price, qty in rows:
                    level_entries += 1
                    precision[("price", decimals_of(price))] += 1
                    precision[("qty", decimals_of(qty))] += 1
                    if "e" in price or "E" in price or "e" in qty or "E" in qty:
                        exponent_tokens += 1
            continue

        name = stream_name_of(meta, rec.frame)
        st = streams.get(name)
        if st is None:
            st = streams[name] = StreamStats(name)
        payload = binance_payload(rec.frame)
        n = wire_bytes(rec.raw)
        st.count += 1
        st.payload_bytes += n
        st.header_bytes += ws_header_bytes(n)
        st.rx.append(rec.rx_ns)
        all_rx.append(rec.rx_ns)

        if not isinstance(payload, dict):
            continue
        if payload.get("e") == "depthUpdate":
            st.is_diff = True
            bids, asks = payload.get("b", []), payload.get("a", [])
            U, u = payload["U"], payload["u"]
            st.diff_u_values.add(u)
            st.diff_brackets.append((U, u))
            if st.first_U is None:
                st.first_U = U
            elif U == st.prev_u + 1:
                st.seq_ok += 1
            else:
                st.seq_break += 1
                st.seq_breaks.append((st.prev_u, U))
            st.prev_u = u
            moved = 0
            for side, rows in (("b", bids), ("a", asks)):
                for price, qty in rows:
                    level_entries += 1
                    precision[("price", decimals_of(price))] += 1
                    precision[("qty", decimals_of(qty))] += 1
                    if "e" in price or "E" in price or "e" in qty or "E" in qty:
                        exponent_tokens += 1
                    if st.book.apply(side, price, qty):
                        moved += 1
            st.levels += len(bids) + len(asks)
            st.changed += moved
        elif "lastUpdateId" in payload:
            st.is_partial = True
            bids, asks = payload.get("bids", []), payload.get("asks", [])
            st.last_update_ids.append(payload["lastUpdateId"])
            for rows in (bids, asks):
                for price, qty in rows:
                    level_entries += 1
                    precision[("price", decimals_of(price))] += 1
                    precision[("qty", decimals_of(qty))] += 1
                    if "e" in price or "E" in price or "e" in qty or "E" in qty:
                        exponent_tokens += 1
            st.levels += len(bids) + len(asks)
            st.changed += st.book.replace(bids, asks)

    span_s = ((max(all_rx) - min(all_rx)) / 1e9) if len(all_rx) > 1 else 0.0
    summary = {
        "trace": trace.name,
        "symbol": meta.get("symbol"),
        "streams_declared": meta.get("streams"),
        "combined": meta.get("combined"),
        "clock": meta.get("clock"),
        "mode": meta.get("capture_mode"),
        "span_s": span_s,
        "frames": sum(s.count for s in streams.values()),
        "payload_bytes": sum(s.payload_bytes for s in streams.values()),
        "header_bytes": sum(s.header_bytes for s in streams.values()),
        "rest_ok": rest_ok,
        "rest_fail": rest_fail,
        "rest_levels_each": rest_levels[0] if rest_levels else 0,
        "rest_ids": rest_ids,
        "controls": dict(controls),
        "ping_latency_ms": ping_latencies_ms,
        "level_entries": level_entries,
        "precision": {f"{k[0]}:{k[1]}": v for k, v in sorted(precision.items())},
        "exponent_tokens": exponent_tokens,
    }
    return summary, streams


def report(trace: Path) -> tuple[dict, dict]:
    summary, streams = measure(trace)
    span = summary["span_s"] or 1.0
    total_bytes = summary["payload_bytes"]

    print(f"\n=== {summary['trace']} ===")
    print(f"  symbol {summary['symbol']}  streams {summary['streams_declared']}  "
          f"combined={summary['combined']}  mode={summary['mode']}  clock={summary['clock']}")
    print(f"  span {span:.1f}s   frames {summary['frames']:,}   "
          f"payload {total_bytes:,} B (+{summary['header_bytes']:,} B of WS headers)")
    print(f"  REST snapshots: {summary['rest_ok']} ok, {summary['rest_fail']} failed, "
          f"{summary['rest_levels_each']} levels each")
    print(f"  control frames: {summary['controls'] or '{}'}", end="")
    if summary["ping_latency_ms"]:
        lat = summary["ping_latency_ms"]
        print(f"   pong latency {min(lat):.3f}-{max(lat):.3f} ms")
    else:
        print()

    print(f"\n  {'stream':<18} {'msgs':>6} {'bytes':>10} {'mean':>7} {'share':>7} "
          f"{'KiB/s':>8} {'lvl/msg':>8} {'chg/msg':>8} {'noop':>6}")
    for name, st in sorted(streams.items()):
        kibs = st.payload_bytes / span / 1024.0
        share = 100.0 * st.payload_bytes / total_bytes if total_bytes else 0.0
        print(f"  {name:<18} {st.count:>6,} {st.payload_bytes:>10,} "
              f"{st.payload_bytes / max(st.count, 1):>7.0f} {share:>6.1f}% "
              f"{kibs:>8.2f} {st.levels / max(st.count, 1):>8.1f} "
              f"{st.changed / max(st.count, 1):>8.1f} "
              f"{st.book.noop_entries:>6,}")

    total_kibs = total_bytes / span / 1024.0
    print(f"\n  TOTAL {total_kibs:.2f} KiB/s   "
          f"headroom {ANVIL_TODAY_KIBS / total_kibs:.1f}x vs Anvil-today ({ANVIL_TODAY_KIBS} KiB/s), "
          f"{SOAK_WORST_HOUR_KIBS / total_kibs:.1f}x vs soak-worst-hour ({SOAK_WORST_HOUR_KIBS} KiB/s)")

    # The oracle's price, as a number.
    diff = [s for s in streams.values() if s.is_diff]
    part = [s for s in streams.values() if s.is_partial]
    if diff and part:
        d_kibs = sum(s.payload_bytes for s in diff) / span / 1024.0
        p_kibs = sum(s.payload_bytes for s in part) / span / 1024.0
        print(f"  @depth20 ON TOP OF @depth: +{p_kibs:.2f} KiB/s on {d_kibs:.2f} KiB/s "
              f"= {100.0 * p_kibs / d_kibs:.0f}% more wire, {(d_kibs + p_kibs) / d_kibs:.2f}x total")

    print(f"\n  {'stream':<18} {'gaps':>6} {'min':>8} {'p50':>8} {'p90':>8} "
          f"{'p99':>8} {'WORST':>10}")
    for name, st in sorted(streams.items()):
        g = st.gaps_ms()
        if not g:
            continue
        print(f"  {name:<18} {len(g):>6,} {g[0]:>8.1f} {percentile(g, 50):>8.1f} "
              f"{percentile(g, 90):>8.1f} {percentile(g, 99):>8.1f} {g[-1]:>10.1f}")

    for name, st in sorted(streams.items()):
        if not st.is_diff:
            continue
        tested = st.seq_ok + st.seq_break
        print(f"\n  U/u continuity on {name}: {st.seq_ok:,}/{tested:,} events satisfy "
              f"U == prev_u + 1"
              + (f"   BREAKS: {st.seq_breaks[:5]}" if st.seq_break else "   (no breaks)"))
        print(f"    removals (qty==0): {st.book.removals:,}   "
              f"of which for a price the book did not hold: {st.book.phantom_removals:,}")
        print(f"    entries restating a quantity the book already had: "
              f"{st.book.noop_entries:,}")

    print(f"\n  number precision over {summary['level_entries']:,} level entries: "
          f"{summary['precision']}")
    print(f"  exponent notation: {summary['exponent_tokens']}")
    return summary, streams


def verify(trace: Path) -> int:
    """Is every frame byte-exactly reproducible from its parsed form?

    Kraken needed hand-rolled number-token preservation for this to be true. If
    it holds here with a plain `json.dumps(separators=(",", ":"))`, then every
    derived byte figure in this tool is a MEASUREMENT and not an estimate -- and
    that is the claim the clone-not-generalise decision rests on, so it is
    checked rather than argued.
    """
    meta = read_meta(trace, validate=True)
    checked = exact = 0
    first_bad = None
    for rec in read_capture(trace):
        checked += 1
        rebuilt = json.dumps(rec.frame, separators=(",", ":"), ensure_ascii=False)
        if rebuilt == rec.raw:
            exact += 1
        elif first_bad is None:
            first_bad = (rec.lineno, rec.raw[:120], rebuilt[:120])
    print(f"\n=== {trace.name} --verify ===")
    print(f"  {exact:,}/{checked:,} frames re-serialise byte-exactly with "
          f'json.dumps(separators=(",", ":"))')
    if first_bad:
        print(f"  FIRST DIVERGENCE at line {first_bad[0]}:")
        print(f"    sent    : {first_bad[1]}")
        print(f"    rebuilt : {first_bad[2]}")
        return 1
    print("  => every byte figure in this tool is a measurement, not an estimate.")
    print(f"  (venue={meta.get('venue')}, and NO number-token machinery was needed: "
          "this venue quotes its decimals as strings.)")
    return 0


# --- the pinning discipline, transplanted from kraken_frame_economics.py -----
# The committed slices are goldens FOR THE TOOL as well as inputs to it. A moved
# number means either a tooling regression or a deliberate re-capture, and in the
# second case the expectations and NOTES-binance.md move in the same commit.
KNOWN_ANSWERS: dict[str, dict[str, int]] = {}
try:
    from binance_pins import KNOWN_ANSWERS as _PINS  # noqa: E402
    KNOWN_ANSWERS = _PINS
except ImportError:
    pass


def pin(traces: list[Path]) -> int:
    rows = []
    for t in traces:
        summary, streams = measure(t)
        diff = next((s for s in streams.values() if s.is_diff), None)
        rows.append((t.name, dict(
            frames=summary["frames"],
            payload_bytes=summary["payload_bytes"],
            level_entries=summary["level_entries"],
            exponent_tokens=summary["exponent_tokens"],
            rest_ok=summary["rest_ok"],
            pings=summary["controls"].get("ping", 0),
            seq_ok=diff.seq_ok if diff else 0,
            seq_break=diff.seq_break if diff else 0,
            removals=diff.book.removals if diff else 0,
            phantom_removals=diff.book.phantom_removals if diff else 0,
        )))
    print('"""Pinned figures for the committed Binance slices (M5 stage 0).\n')
    print("Regenerate with:  python tools/binance_frame_economics.py <slices> --pin")
    print('"""')
    print("KNOWN_ANSWERS = {")
    for name, vals in rows:
        args = ", ".join(f"{k}={v}" for k, v in vals.items())
        print(f'    "{name}": dict(\n        {args}),')
    print("}")
    return 0


def selfcheck(traces: list[Path]) -> int:
    if not KNOWN_ANSWERS:
        print("selfcheck: no pins yet (tools/binance_pins.py absent). Run --pin first.")
        return 1
    failures = 0
    checked = 0
    for t in traces:
        want = KNOWN_ANSWERS.get(t.name)
        if want is None:
            print(f"  {t.name}: NOT PINNED -- add it or stop committing it")
            failures += 1
            continue
        summary, streams = measure(t)
        diff = next((s for s in streams.values() if s.is_diff), None)
        got = dict(
            frames=summary["frames"], payload_bytes=summary["payload_bytes"],
            level_entries=summary["level_entries"],
            exponent_tokens=summary["exponent_tokens"], rest_ok=summary["rest_ok"],
            pings=summary["controls"].get("ping", 0),
            seq_ok=diff.seq_ok if diff else 0, seq_break=diff.seq_break if diff else 0,
            removals=diff.book.removals if diff else 0,
            phantom_removals=diff.book.phantom_removals if diff else 0)
        for k, v in want.items():
            checked += 1
            if got.get(k) != v:
                print(f"  {t.name}: {k} = {got.get(k)}, pinned {v}   MOVED")
                failures += 1
        if all(got.get(k) == v for k, v in want.items()):
            print(f"  {t.name}: OK ({len(want)} figures)")
    print(f"selfcheck: {checked - failures}/{checked} pinned figures reproduced")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("trace", type=Path, nargs="+", help="capture NDJSON file(s)")
    ap.add_argument("--verify", action="store_true",
                    help="prove every frame re-serialises byte-exactly")
    ap.add_argument("--pin", action="store_true", help="emit a KNOWN_ANSWERS block")
    ap.add_argument("--selfcheck", action="store_true",
                    help="recompute the pinned figures and fail on any move")
    args = ap.parse_args(argv)
    if args.pin:
        return pin(args.trace)
    if args.selfcheck:
        return selfcheck(args.trace)
    rc = 0
    for t in args.trace:
        if args.verify:
            rc |= verify(t)
        else:
            report(t)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
