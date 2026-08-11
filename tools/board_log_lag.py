#!/usr/bin/env python3
"""Read a DepthCharge serial log and reconstruct the feed-lag curve from it.

WHY THIS EXISTS INSTEAD OF A STOPWATCH.

The bench protocol used to say: freeze the web client, time how long until the
panel shows the same state, that interval is the lag. It is not. Under queuing
the panel's market-time cursor advances at the drain fraction f, so catching up
to a frozen reference takes age/f — at the board's f ~ 0.35 the stopwatch reads
nearly three times the age, and calibrating the estimator against it would
detune a correct instrument by that factor (hardware/bench-2026-08-11-feed-lag.md).

The board already prints everything needed, every ten seconds, for free. This
reads it back:

  * each `-- age` line is one sample of the lag curve, and the `over N s` field
    is the age of the CONNECTION, so a drop in it is a reconnect — which is how
    the run is segmented without needing `connects` to be trustworthy;
  * a connection's samples are a complete curve from zero to peak, so a board
    that drops every few minutes yields many short curves rather than no long
    one. That is better data, not worse, and it needs nothing from the operator;
  * the slope of each curve is 1 - f, which must agree with the drain fraction
    printed beside it. Two derivations of one number is the whole method here;
  * the `-- feed` counters and the socket-up lines give the drop cadence, which
    separates "a queue filling to a bound" (regular) from "a link" (scattered).

USAGE
    py tools/board_log_lag.py firmware/logs/device-monitor-*.log

Python 3 stdlib only; lives in tools/.
"""
from __future__ import annotations

import argparse
import glob
import re
import statistics
import sys

# `-- age : age 84.4 s (worst 83.8 s, run 328.2 s) | summary 100 of 268 over
#  134.4 s | drain 35% this window (7 in 10000 ms) | seq 25344555`
# The trailing `| seq N` is present only on builds after 2026-08-11; optional so
# one tool reads both.
AGE_RE = re.compile(
    r"-- age\s*:\s*age (?P<age>[\d.]+) s \(worst (?P<worst>[\d.]+) s, run (?P<run>[\d.]+) s\)"
    r"\s*\|\s*summary (?P<got>\d+) of (?P<want>\d+) over (?P<window>[\d.]+) s"
    r"\s*\|\s*drain (?P<drain>\d+)% this window \((?P<n>\d+) in (?P<dt>\d+) ms\)"
    r"(?:\s*\|\s*seq (?P<seq>-?\d+))?"
)
# The other shape, before the first summary of a connection.
AGE_NONE_RE = re.compile(r"-- age\s*:\s*no reading yet")

FEED_RE = re.compile(
    r"-- feed\s*:\s*frames=(?P<frames>\d+) wd_gaps=(?P<wd>\d+) sock_gaps=(?P<sock>\d+)"
    r" connects=(?P<connects>\d+) worst_gap=(?P<worst_gap>\d+) ms"
)
RATE_RE = re.compile(r"-- rate\s*:\s*in (?P<inrate>[\d.]+)/s")
# `I (3897078) panel:` — the board's own millis, which is the clock to trust;
# the leading wall-clock stamp is the monitor's `time` filter.
MILLIS_RE = re.compile(r"[I|W|E]\s*\((?P<ms>\d+)\)")
WALL_RE = re.compile(r"^(?P<wall>\d\d:\d\d:\d\d\.\d+)")
STALE_RE = re.compile(r"\*\*\* STALE \((?P<reason>[a-z-]+)\)")
LIVE_RE = re.compile(r"\*\*\* LIVE")
SOCKUP_RE = re.compile(r"socket up on handle (?P<handle>\w), (?P<ms>\d+) ms into attempt #(?P<n>\d+)")
REJOIN_RE = re.compile(r"rejoining \(#(?P<n>\d+)\)")


def parse(path: str) -> dict:
    ages, feeds, rates, stales, lives, sockups, rejoins = [], [], [], [], [], [], []
    no_reading = 0
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            mm = MILLIS_RE.search(line)
            ms = int(mm.group("ms")) if mm else None
            wm = WALL_RE.match(line)
            wall = wm.group("wall") if wm else ""

            m = AGE_RE.search(line)
            if m:
                d = m.groupdict()
                ages.append({
                    "ms": ms, "wall": wall,
                    "age": float(d["age"]), "worst": float(d["worst"]), "run": float(d["run"]),
                    "got": int(d["got"]), "want": int(d["want"]), "window": float(d["window"]),
                    "drain": int(d["drain"]), "n": int(d["n"]), "dt": int(d["dt"]),
                    "seq": int(d["seq"]) if d["seq"] else None,
                })
                continue
            if AGE_NONE_RE.search(line):
                no_reading += 1
                continue
            m = FEED_RE.search(line)
            if m:
                d = {k: int(v) for k, v in m.groupdict().items()}
                d["ms"], d["wall"] = ms, wall
                feeds.append(d)
                continue
            m = RATE_RE.search(line)
            if m:
                rates.append(float(m.group("inrate")))
                continue
            m = STALE_RE.search(line)
            if m:
                stales.append({"ms": ms, "wall": wall, "reason": m.group("reason")})
                continue
            if LIVE_RE.search(line):
                lives.append({"ms": ms, "wall": wall})
                continue
            m = SOCKUP_RE.search(line)
            if m:
                sockups.append({"ms": ms, "wall": wall, "handle": m.group("handle"),
                                "took": int(m.group("ms")), "attempt": int(m.group("n"))})
                continue
            m = REJOIN_RE.search(line)
            if m:
                rejoins.append({"ms": ms, "wall": wall, "n": int(m.group("n"))})
    return {"ages": ages, "feeds": feeds, "rates": rates, "stales": stales,
            "lives": lives, "sockups": sockups, "rejoins": rejoins,
            "no_reading": no_reading}


def segment(ages: list[dict]) -> list[list[dict]]:
    """Split the age samples into per-connection runs.

    The `over N s` window is the age of the CURRENT connection (it restarts at
    the first summary after each connect), so a window that does not increase is
    a new connection. Segmenting on this rather than on the `connects` counter is
    deliberate: `connects` can be incremented by a retired handle's CONNECTED
    event (ws_transport.cpp:313 has no live-handle filter), whereas the window is
    reset by the same call that resets the estimate, so it cannot disagree with
    the numbers printed beside it.
    """
    runs: list[list[dict]] = []
    cur: list[dict] = []
    for a in ages:
        if cur and a["window"] <= cur[-1]["window"]:
            runs.append(cur)
            cur = []
        cur.append(a)
    if cur:
        runs.append(cur)
    return runs


def describe(path: str) -> None:
    d = parse(path)
    ages = d["ages"]
    print(f"== {path}")
    if not ages:
        print("   no `-- age` lines — this log predates M3 stage E, or the build was not flashed")
        return

    # Both endpoints must have a millis stamp: a line whose ARDUHAL prefix was
    # chopped by the monitor gives `ms is None`, and `None - int` is a crash
    # rather than a missing figure. Guarding only the first endpoint (the first
    # version of this line) leaves the failure in place for a truncated tail,
    # which is the end of the log that truncation actually damages.
    stamped = [a["ms"] for a in ages if a["ms"] is not None]
    span_s = (stamped[-1] - stamped[0]) / 1000.0 if len(stamped) >= 2 else 0.0
    print(f"   {len(ages)} age samples over {span_s / 60:.1f} min"
          f"   ({d['no_reading']} blocks before a first summary)")
    if d["ages"][-1]["seq"] is None:
        print("   NOTE: no `seq` field — pre-2026-08-11 build; the desk-capture join is unavailable")

    if d["feeds"]:
        f = d["feeds"][-1]
        print(f"   final counters: frames={f['frames']} wd_gaps={f['wd']} sock_gaps={f['sock']}"
              f" connects={f['connects']} worst_gap={f['worst_gap']} ms")
    if d["rates"]:
        print(f"   board inbound rate: median {statistics.median(d['rates']):.2f}/s"
              f"  (min {min(d['rates']):.2f}, max {max(d['rates']):.2f})")

    # --- the headline: peak age ever reached -------------------------------
    peak_run = max(a["run"] for a in ages)
    peak_age = max(a["age"] for a in ages)
    print()
    print(f"   PEAK AGE: `run` high-water {peak_run:.1f} s;"
          f" largest single sample {peak_age:.1f} s")

    # --- per-connection curves ---------------------------------------------
    runs = segment(ages)
    print()
    print(f"   {len(runs)} connection(s) seen in the age stream")
    print(f"   {'#':>3} {'samples':>8} {'window s':>9} {'peak age':>9} {'slope':>7}"
          f" {'1-drain':>8} {'agree':>6}")
    slopes, implied = [], []
    for i, r in enumerate(runs, 1):
        if len(r) < 2:
            print(f"   {i:>3} {len(r):>8} {r[-1]['window']:>9.1f} {r[-1]['age']:>9.1f}"
                  f" {'-':>7} {'-':>8} {'-':>6}")
            continue
        dw = r[-1]["window"] - r[0]["window"]
        da = r[-1]["age"] - r[0]["age"]
        slope = da / dw if dw > 0 else float("nan")
        # The independent derivation of the same quantity: the cumulative drain
        # over the connection. lag grows at 1 - f, so these must agree.
        cum_drain = r[-1]["got"] / r[-1]["want"] if r[-1]["want"] else float("nan")
        agree = abs(slope - (1 - cum_drain))
        slopes.append(slope)
        implied.append(1 - cum_drain)
        print(f"   {i:>3} {len(r):>8} {r[-1]['window']:>9.1f} {max(x['age'] for x in r):>9.1f}"
              f" {slope:>7.3f} {1 - cum_drain:>8.3f} {agree:>6.3f}")

    if slopes:
        print()
        print(f"   lag slope across all connections: median {statistics.median(slopes):+.3f} s/s"
              f"   => drain f = {1 - statistics.median(slopes):.3f}")
        print(f"   the same figure from the summary counters: {statistics.median(implied):+.3f} s/s"
              f"   => drain f = {1 - statistics.median(implied):.3f}")
        print("   Two independent derivations of one number. They must agree; a gap is a bug in"
              " one of them.")
        if all(s > 0.05 for s in slopes if s == s):
            print("   Every connection's lag GROWS. No plateau => queuing, on the board, not"
                  " just at the desk.")

    # --- the drop cadence ---------------------------------------------------
    drops = [s for s in d["stales"] if s["ms"] is not None]
    if len(drops) >= 2:
        gaps = [(b["ms"] - a["ms"]) / 1000.0 for a, b in zip(drops, drops[1:])]
        print()
        print(f"   {len(drops)} outage(s); interval between them:"
              f" median {statistics.median(gaps):.0f} s,"
              f" min {min(gaps):.0f}, max {max(gaps):.0f}")
        if len(gaps) > 2:
            cv = statistics.pstdev(gaps) / statistics.mean(gaps)
            verdict = ("REGULAR — consistent with a queue filling to a bound" if cv < 0.35
                       else "SCATTERED — consistent with a link, not a bound")
            print(f"   coefficient of variation {cv:.2f}  => {verdict}")
        by_reason: dict[str, int] = {}
        for s in drops:
            by_reason[s["reason"]] = by_reason.get(s["reason"], 0) + 1
        print(f"   stale reasons: {by_reason}")

    # --- is it the radio, or only the socket? -------------------------------
    print()
    if d["rejoins"]:
        print(f"   WI-FI: WifiSupervisor rejoined {len(d['rejoins'])} time(s)"
              " — the station itself dropped")
    else:
        print("   WI-FI: no `rejoining` line — WifiSupervisor never fired. Arduino handles"
              " BEACON_TIMEOUT/NO_AP_FOUND itself, so this is suggestive, not proof.")

    # --- the spurious-CONNECTED check ---------------------------------------
    if d["feeds"]:
        connects = d["feeds"][-1]["connects"]
        ups = len(d["sockups"])
        print(f"   HANDLE CHECK: connects={connects} against {ups} `socket up on handle` line(s)"
              f" (+1 for the boot connect if it is not logged)")
        if connects > ups + 1:
            print("   ** connects EXCEEDS the socket-up lines: some increments have no attempt"
                  " behind them, which is the retired-handle CONNECTED path"
                  " (ws_transport.cpp:313, no live-handle filter). Those resets are spurious.")
        else:
            print("   ok — every connect has an attempt behind it; no spurious resets.")
        slow = [s for s in d["sockups"] if s["took"] > 7000]
        if slow:
            print(f"   ** {len(slow)} attempt(s) took >7000 ms, past kHandshakeBudgetUs — the"
                  " window in which an abandoned handshake can still fire CONNECTED")
        handles = "".join(s["handle"] for s in d["sockups"])
        if handles:
            print(f"   handle order: {handles}"
                  f"{'  (alternating, as DESIGN strain 14 requires)' if all(a != b for a, b in zip(handles, handles[1:])) else '  ** NOT ALTERNATING — strain 14 assumption has moved'}")


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("logs", nargs="+", help="serial log path(s); globs allowed")
    args = p.parse_args(argv)
    paths: list[str] = []
    for pat in args.logs:
        hits = glob.glob(pat)
        paths.extend(hits if hits else [pat])
    for path in paths:
        describe(path)
        print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
