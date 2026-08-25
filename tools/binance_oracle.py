#!/usr/bin/env python3
"""Three candidate oracles for a Binance book, measured against real captures.

M5 stage 0's central deliverable, and the thing the Kraken stage did not have to
do. **Kraken publishes a CRC32 over its top 10 levels**, so that venue hands over
an independent answer to *is my book the venue's book?* and every M4 golden was
built on it. **Binance publishes no checksum.** `U`/`u` bracketing detects a lost
or misordered message and says nothing about whether the book those messages
built is correct -- it can be 100% clean while the client is wrong, which is the
failure mode M4 stage 0 caught at Kraken depth 100.

So this tool names all three candidates, measures all three, and recommends one.
**It implements none of them in `engine/`** -- that is M5's work, with the adapter
in front of it.

  (a) THE VENUE'S OWN PARTIAL-DEPTH STREAM, ON THE SAME SOCKET.
      `@depth20@100ms` is the venue's top-20 book, computed venue-side, stamped
      with `lastUpdateId`. Structurally the closest thing this venue offers to
      Kraken's CRC32: an independent, venue-published answer, bounded to 20
      levels a side. Its whole viability rests on one measurable question --
      **does a partial payload's `lastUpdateId` ever equal a diff event's `u`
      exactly, and in what fraction of ticks?** Where it does, the comparison is
      exact. Where it does not, the instant is *unverifiable*, and that is
      reported as a fourth outcome rather than being allowed to hide inside
      "matched": `seen == matched + failed + unverifiable`.

  (b) REST RE-SNAPSHOT COMPARISON.
      Replay diffs to a captured snapshot's `lastUpdateId` and compare. Exact
      when that id equals some event's `u`; **ambiguous when it lands strictly
      inside an event's `[U, u]`**, because a coalesced event cannot be split.
      And the warning M4 stage 0 wrote for this paragraph: at Binance the REST
      re-snapshot **is the venue's healing event, and it is scheduled rather than
      incidental**, so a detector built on it risks measuring recovery instead of
      the defect -- and unlike a Kraken resync it cannot be dodged by choosing a
      clean window, because a client that never re-snapshots is a client that
      never recovers.

  (c) A SECOND, INDEPENDENT IMPLEMENTATION.
      A Python reference book differentially tested against the C++ adapter over
      the same trace -- the shape Anvil already runs. **This tool is half of it.**
      Said plainly: it catches divergence between two books, and it **cannot**
      catch a shared misreading of the wire, which is exactly the class (a) and
      (b) exist to catch. If both implementations mis-read `qty: 0`, they agree
      perfectly and are both wrong.

THE CLAUSE THAT TRANSFERS UNCHANGED (ARCHITECTURE §9, 2026-08-16, stage 0):
**whatever the oracle is, prove the trace catches a deliberately broken
implementation before pinning it.** `--mutants` runs four implementations against
the recommended oracle -- an honest control that must stay green, and three
deliberate breakages that must go red. An oracle that cannot fail is not an
oracle, and a trace nobody has broken on purpose is a trace nobody has checked.

Usage:
    python tools/binance_oracle.py <trace.ndjson>
    python tools/binance_oracle.py <trace.ndjson> --mutants
    python tools/binance_oracle.py <trace.ndjson> --window-sweep

Python 3 stdlib only. Lives in tools/.
"""
from __future__ import annotations

import argparse
from pathlib import Path

from tracefile import binance_payload, read_capture, read_meta


def ticks(text: str) -> int:
    """A quoted decimal as a scaled integer. Digits shifted, never rounded.

    Both sides of every comparison in this file go through here, so a level is
    compared as an integer and never as a float (invariant #3). Binance quotes
    every price and quantity to a fixed precision, so a common scale is safe --
    and section 7 counts, over the captures, whether that is actually true.
    """
    if "e" in text or "E" in text:
        raise ValueError(f"exponent notation on the wire: {text!r}")
    neg = text.startswith("-")
    body = text[1:] if neg else text
    whole, _, frac = body.partition(".")
    value = int(whole + frac) if (whole + frac) else 0
    return -value if neg else value


# --- the implementations under test -----------------------------------------
# One honest book and three deliberate breakages. They differ ONLY in `apply`
# and in the post-update step, so a mutant cannot accidentally differ in some
# other way and be caught for the wrong reason.

HONEST = "honest"
M_ZERO_QTY = "qty0-not-removal"
M_NO_TRUNCATE = "no-truncation-to-rendered-depth"
M_SWAP_SIDES = "bid-ask-swapped"
M_BOUNDED_WINDOW = "bounded-window-cannot-refill"


class RefBook:
    """A reference Binance book. `mutation` selects which one it is."""

    def __init__(self, mutation: str = HONEST, window: int = 0) -> None:
        self.mutation = mutation
        self.window = window          # 0 = unbounded
        self.bids: dict[int, int] = {}   # price ticks -> qty ticks
        self.asks: dict[int, int] = {}
        self.last_u: int | None = None

    def seed(self, snapshot: dict) -> None:
        self.bids.clear()
        self.asks.clear()
        b, a = (("asks", "bids") if self.mutation == M_SWAP_SIDES else ("bids", "asks"))
        for price, qty in snapshot[b]:
            self.bids[ticks(price)] = ticks(qty)
        for price, qty in snapshot[a]:
            self.asks[ticks(price)] = ticks(qty)
        self.last_u = snapshot["lastUpdateId"]
        self._post_update()

    def apply(self, event: dict) -> None:
        b_key, a_key = (("a", "b") if self.mutation == M_SWAP_SIDES else ("b", "a"))
        for side, key in ((self.bids, b_key), (self.asks, a_key)):
            for price, qty in event.get(key, []):
                px, q = ticks(price), ticks(qty)
                if q == 0:
                    if self.mutation == M_ZERO_QTY:
                        # THE MUTANT: a zero quantity is stored as a level worth
                        # nothing instead of deleting the level. The wire says
                        # "this price is gone"; this reads it as "this price has
                        # zero size", which is the single most common way to get
                        # a delta book wrong.
                        side[px] = 0
                    else:
                        side.pop(px, None)
                else:
                    side[px] = q
        self.last_u = event["u"]
        self._post_update()

    def _post_update(self) -> None:
        if self.mutation == M_NO_TRUNCATE or self.window <= 0:
            return
        # Keep only the best `window` levels a side. For a bounded-window client
        # this is not a mutation but a NECESSITY -- see the sweep below.
        if len(self.bids) > self.window:
            keep = sorted(self.bids, reverse=True)[:self.window]
            self.bids = {p: self.bids[p] for p in keep}
        if len(self.asks) > self.window:
            keep = sorted(self.asks)[:self.window]
            self.asks = {p: self.asks[p] for p in keep}

    def top(self, n: int) -> tuple[list, list]:
        b = sorted(self.bids.items(), key=lambda kv: -kv[0])[:n]
        a = sorted(self.asks.items())[:n]
        return b, a


def partial_top(payload: dict, n: int) -> tuple[list, list]:
    b = [(ticks(p), ticks(q)) for p, q in payload["bids"][:n]]
    a = [(ticks(p), ticks(q)) for p, q in payload["asks"][:n]]
    return b, a


class Outcome:
    """`seen == matched + failed + unverifiable`, with the fourth kind named.

    Modelled on the shape M4 stage B2 used for the CRC's top-10 reach: a
    comparison that could not be made must not be able to hide inside a pass.
    """

    def __init__(self) -> None:
        self.seen = 0
        self.matched = 0
        self.failed = 0
        self.unverifiable = 0
        self.reasons: dict[str, int] = {}
        self.first_failure = None

    def note_unverifiable(self, reason: str) -> None:
        self.seen += 1
        self.unverifiable += 1
        self.reasons[reason] = self.reasons.get(reason, 0) + 1

    def note(self, ok: bool, detail=None) -> None:
        self.seen += 1
        if ok:
            self.matched += 1
        else:
            self.failed += 1
            if self.first_failure is None:
                self.first_failure = detail

    def consistent(self) -> bool:
        return self.seen == self.matched + self.failed + self.unverifiable

    def line(self, label: str) -> str:
        pct = 100.0 * self.matched / self.seen if self.seen else 0.0
        return (f"{label:<34} seen {self.seen:>5}  matched {self.matched:>5} "
                f"({pct:>5.1f}%)  failed {self.failed:>5}  unverifiable "
                f"{self.unverifiable:>5}   {self.reasons or ''}")


def run(trace: Path, mutation: str = HONEST, window: int = 0, depth: int = 20,
        verbose: bool = False):
    """Replay a capture and grade the book at every opportunity the venue gives.

    Returns (oracle_a, oracle_b, uu) where the first two are `Outcome`s and the
    third is the `U`/`u` continuity tally.
    """
    meta = read_meta(trace, validate=True)
    book = RefBook(mutation, window)
    a_out, b_out = Outcome(), Outcome()
    uu = {"tested": 0, "ok": 0, "break": 0, "breaks": []}

    buffered: list[dict] = []
    # top-N book images keyed by the `u` that produced them, so a REST body that
    # arrives a second late can still be graded against the instant it names.
    history: dict[int, tuple[list, list]] = {}
    seeded = False
    pending_partials: dict[int, dict] = {}   # lastUpdateId -> payload, awaiting its u
    brackets: list[tuple[int, int]] = []
    first_seed_id = None

    def compare_image(payload: dict, image, out: Outcome, tag: str) -> None:
        want_b, want_a = partial_top(payload, depth)
        got_b, got_a = image
        n = min(len(want_b), len(got_b)), min(len(want_a), len(got_a))
        ok = (got_b[:n[0]] == want_b[:n[0]]) and (got_a[:n[1]] == want_a[:n[1]])
        out.note(ok, detail=(tag, payload["lastUpdateId"]))

    def compare(payload: dict, out: Outcome, tag: str) -> None:
        want_b, want_a = partial_top(payload, depth)
        got_b, got_a = book.top(depth)
        n = min(len(want_b), len(got_b)), min(len(want_a), len(got_a))
        ok = (got_b[:n[0]] == want_b[:n[0]]) and (got_a[:n[1]] == want_a[:n[1]])
        out.note(ok, detail=(tag, payload["lastUpdateId"],
                             ("bids", want_b[:3], got_b[:3]) if got_b[:n[0]] != want_b[:n[0]]
                             else ("asks", want_a[:3], got_a[:3])))

    for rec in read_capture(trace, kinds=("rest",)):
        if rec.kind == "rest":
            if rec.frame is None or "lastUpdateId" not in rec.frame:
                continue
            L = rec.frame["lastUpdateId"]
            if not seeded:
                # THE DOCUMENTED PROCEDURE. Drop buffered events wholly at or
                # before the snapshot, then require the first surviving event to
                # bracket L + 1.
                first_seed_id = L
                book.seed(rec.frame)
                history[L] = book.top(depth)
                survivors = [e for e in buffered if e["u"] > L]
                buffered = []
                seeded = True
                if survivors:
                    e0 = survivors[0]
                    uu["bracket_ok"] = e0["U"] <= L + 1 <= e0["u"]
                    uu["bracket_first_U"] = e0["U"]
                    uu["bracket_first_u"] = e0["u"]
                    uu["bracket_snapshot_id"] = L
                for e in survivors:
                    _apply(e, book, uu, brackets, pending_partials, a_out, compare, depth,
                           history)
            else:
                # ORACLE (b): a re-snapshot compared against the streamed book.
                #
                # **COMPARED AGAINST THE BOOK AS OF `L`, NOT THE CURRENT BOOK**,
                # and getting this wrong is the first thing this tool did. A
                # /api/v3/depth round trip measured ~1.0-1.5 s from this box, so
                # by the time the body is in hand the diff stream has moved 10-15
                # events past the id the body is stamped with. Comparing against
                # the live book therefore fails every time and -- worse -- fails
                # in a way that looks like "the venue's ids never line up", which
                # is the opposite of what the wire actually does (measured: they
                # line up exactly, 23/23 and 5/5). A snapshot is a statement
                # about a PAST instant and has to be graded against that instant.
                img = history.get(L)
                if img is not None:
                    compare_image(rec.frame, img, b_out, "rest")
                elif any(U < L < u for U, u in brackets):
                    b_out.note_unverifiable("inside-a-coalesced-bracket")
                elif L > (book.last_u or 0):
                    b_out.note_unverifiable("ahead-of-applied-stream")
                elif L < (first_seed_id or 0):
                    b_out.note_unverifiable("before-the-baseline")
                else:
                    b_out.note_unverifiable("no-event-ends-on-this-id")
            continue

        payload = binance_payload(rec.frame)
        if not isinstance(payload, dict):
            continue

        if payload.get("e") == "depthUpdate":
            if not seeded:
                buffered.append(payload)
                continue
            _apply(payload, book, uu, brackets, pending_partials, a_out, compare, depth,
                   history)
        elif "lastUpdateId" in payload:
            # ORACLE (a): the venue's own top-20, on the same socket.
            if not seeded:
                continue
            L = payload["lastUpdateId"]
            if L == book.last_u:
                compare(payload, a_out, "partial")
            elif L > (book.last_u or 0):
                # Arrived before the diff that ends on it. Hold and grade when
                # that diff lands -- the two streams are interleaved on one
                # socket and neither is owed the other's ordering.
                pending_partials[L] = payload
            else:
                inside = any(U < L < u for U, u in brackets)
                a_out.note_unverifiable("inside-a-coalesced-bracket" if inside
                                        else "no-event-ends-on-this-id")

    # Partials still waiting when the capture ended never got their diff.
    for L in pending_partials:
        inside = any(U < L < u for U, u in brackets)
        a_out.note_unverifiable("inside-a-coalesced-bracket" if inside
                                else "never-reached-before-capture-ended")

    uu["seeded_from"] = first_seed_id
    return a_out, b_out, uu


def _apply(event, book, uu, brackets, pending, a_out, compare, depth,
           history=None) -> None:
    prev_u = book.last_u
    if prev_u is not None and brackets:
        uu["tested"] += 1
        if event["U"] == prev_u + 1:
            uu["ok"] += 1
        else:
            uu["break"] += 1
            if len(uu["breaks"]) < 5:
                uu["breaks"].append((prev_u, event["U"]))
    brackets.append((event["U"], event["u"]))
    book.apply(event)
    if history is not None:
        history[event["u"]] = book.top(depth)
        if len(history) > 4000:      # a capture is 90 s; this never trims in practice
            for k in sorted(history)[:1000]:
                del history[k]
    held = pending.pop(event["u"], None)
    if held is not None:
        compare(held, a_out, "partial(deferred)")
    # Anything still pending that this event has now overshot can never be graded.
    for L in [k for k in pending if k < event["u"]]:
        payload = pending.pop(L)
        inside = any(U < L < u for U, u in brackets)
        a_out.note_unverifiable("inside-a-coalesced-bracket" if inside
                                else "overshot-by-a-coalesced-event")


def verdict(failed: int, matched: int) -> str:
    """GREEN / RED / **VACUOUS**, and the third one is the point.

    A detector that made no comparison at all has `failed == 0`, and reporting
    that as GREEN is precisely the "oracle that cannot fail" ARCHITECTURE §9
    (2026-08-16) forbids pinning. This tool printed exactly that on its first run
    against the deep-seed capture: oracle (b) scored GREEN against every mutant
    while grading nothing, because it was comparing a snapshot against the LIVE
    book instead of against the instant the snapshot names. The bug is fixed; the
    state it hid behind is now named, so the next one cannot hide there.
    """
    if failed:
        return f"RED ({failed})"
    return "GREEN" if matched else "VACUOUS (0 graded)"


def coincidence_report(trace: Path, depth: int) -> None:
    """The measurement candidate (a) lives or dies on, before any grading."""
    partial_ids, diff_us, brackets = [], set(), []
    for rec in read_capture(trace):
        p = binance_payload(rec.frame)
        if not isinstance(p, dict):
            continue
        if p.get("e") == "depthUpdate":
            diff_us.add(p["u"])
            brackets.append((p["U"], p["u"]))
        elif "lastUpdateId" in p:
            partial_ids.append(p["lastUpdateId"])
    if not partial_ids:
        print("  (no partial-depth stream in this capture)")
        return
    exact = sum(1 for L in partial_ids if L in diff_us)
    inside = sum(1 for L in partial_ids
                 if L not in diff_us and any(U < L < u for U, u in brackets))
    print(f"  partial payloads {len(partial_ids):,}   "
          f"lastUpdateId == some diff u: {exact:,} ({100.0 * exact / len(partial_ids):.1f}%)   "
          f"strictly inside a bracket: {inside:,}   "
          f"neither: {len(partial_ids) - exact - inside:,}")


def check(traces, depth: int) -> int:
    """The mutation clause as a build product. Non-zero unless it all holds.

    THREE MUTANTS AND A CONTROL, and the choice of which three is itself a
    finding. The Kraken stage's list was *`qty: 0` not treated as removal*, *no
    truncation to the rendered depth*, and *bid and ask sides swapped*. The first
    and third transfer unchanged. **The second does not, and is not quietly
    dropped** -- it is run, and asserted to be a NO-OP, because at Binance the
    diff stream is the whole book and every removal is explicit, so a client that
    never truncates is not wrong. Its Binance-shaped equivalent is the opposite
    defect: a book bounded to the rendered depth cannot refill from a stream that
    only reports changes, so levels that rise into view were never seen.

    Asserting the no-op rather than deleting it is the point. A mutant that
    silently stopped being a mutant is how a suite keeps its green while losing
    its reach, and this venue is the first place in the project where a
    transplanted mutant genuinely does not apply.
    """
    failures = []
    for t in traces:
        honest, _, _ = run(t, HONEST, 0, depth)
        if verdict(honest.failed, honest.matched) != "GREEN":
            failures.append(f"{t.name}: honest control is "
                            f"{verdict(honest.failed, honest.matched)}, expected GREEN")
        for mut, win in ((M_ZERO_QTY, 0), (M_SWAP_SIDES, 0), (M_BOUNDED_WINDOW, depth + 5)):
            mm = HONEST if mut == M_BOUNDED_WINDOW else mut
            out, _, _ = run(t, mm, win, depth)
            v = verdict(out.failed, out.matched)
            if not v.startswith("RED"):
                failures.append(f"{t.name}: mutant {mut} is {v}, expected RED -- "
                                "the oracle no longer catches it")
        noop, _, _ = run(t, M_NO_TRUNCATE, 0, depth)
        if verdict(noop.failed, noop.matched) != "GREEN":
            failures.append(
                f"{t.name}: {M_NO_TRUNCATE} is now caught. That mutant is asserted "
                "to be a NO-OP at this venue because the diff stream carries every "
                "removal explicitly. If it has started failing, the wire changed "
                "or the reference book did -- read NOTES-binance.md before editing "
                "this expectation.")
        print(f"  {t.name}: honest GREEN ({honest.matched} graded), "
              f"3 mutants RED, no-truncation asserted no-op")
    for f in failures:
        print(f"  FAIL {f}")
    print(f"binance_oracle --check: {'FAILED' if failures else 'OK'}")
    return 1 if failures else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("trace", type=Path, nargs="+")
    ap.add_argument("--depth", type=int, default=20,
                    help="levels a side to compare (the partial stream's own depth)")
    ap.add_argument("--window", type=int, default=0,
                    help="bound the maintained book to N levels a side (0 = unbounded)")
    ap.add_argument("--mutants", action="store_true",
                    help="run the honest control and three deliberate breakages")
    ap.add_argument("--window-sweep", action="store_true",
                    help="find the smallest bounded window the oracle still passes")
    ap.add_argument("--check", action="store_true",
                    help="ctest mode: exit non-zero unless the honest run is GREEN "
                         "and all three transferring mutants are RED")
    args = ap.parse_args(argv)
    if args.check:
        return check(args.trace, args.depth)

    for t in args.trace:
        meta = read_meta(t, validate=True)
        print(f"\n=== {t.name} ({meta.get('symbol')}, {meta.get('streams')}) ===")
        print("\n  THE COINCIDENCE QUESTION (a)'s viability rests on:")
        coincidence_report(t, args.depth)

        a, b, uu = run(t, HONEST, args.window, args.depth)
        print("\n  ORACLE OUTCOMES (honest implementation)")
        print("   " + a.line("(a) partial-depth stream")
              + f"  => {verdict(a.failed, a.matched)}")
        print("   " + b.line("(b) REST re-snapshot")
              + f"  => {verdict(b.failed, b.matched)}")
        assert a.consistent() and b.consistent(), "outcome accounting does not close"
        print(f"    accounting closes: seen == matched + failed + unverifiable  "
              f"[(a) {a.consistent()}, (b) {b.consistent()}]")
        if a.first_failure:
            print(f"    first (a) failure: {a.first_failure}")

        print(f"\n  U/u CONTINUITY: {uu['ok']:,}/{uu['tested']:,} events satisfy "
              f"U == prev_u + 1" + (f"   BREAKS {uu['breaks']}" if uu["break"] else "  (no breaks)"))
        if "bracket_ok" in uu:
            print(f"    documented bracketing on the FIRST attempt: {uu['bracket_ok']} "
                  f"(snapshot lastUpdateId {uu['bracket_snapshot_id']}, first surviving "
                  f"event [U={uu['bracket_first_U']}, u={uu['bracket_first_u']}])")

        if args.mutants:
            print("\n  MUTATION VERIFICATION -- the clause that transfers unchanged.")
            print("  An oracle that cannot fail is not an oracle.\n")
            print(f"   {'implementation':<34} {'(a) verdict':<12} {'(b) verdict':<12} "
                  f"{'U/u verdict':<12}")
            rows = [(HONEST, 0), (M_ZERO_QTY, 0), (M_NO_TRUNCATE, 0),
                    (M_SWAP_SIDES, 0), (M_BOUNDED_WINDOW, args.depth + 5)]
            for mut, win in rows:
                mm = HONEST if mut == M_BOUNDED_WINDOW else mut
                ao, bo, u2 = run(t, mm, win or args.window, args.depth)
                va = verdict(ao.failed, ao.matched)
                vb = verdict(bo.failed, bo.matched)
                # U/u is not an outcome tally -- it either broke or it did not,
                # and it is ALWAYS exercised, so it cannot be vacuous.
                vu = "GREEN" if u2["break"] == 0 else f"RED ({u2['break']})"
                label = mut + (f" (w={win})" if win else "")
                print(f"   {label:<34} {va:<12} {vb:<12} {vu:<12}")

        if args.window_sweep:
            print("\n  BOUNDED-WINDOW SWEEP -- how deep must the board's book be?")
            print(f"   {'window':>8} {'(a) matched':>12} {'(a) failed':>11}")
            for w in (20, 21, 25, 30, 50, 100, 200, 256, 500, 1000, 0):
                ao, _, _ = run(t, HONEST, w, args.depth)
                print(f"   {(w or 'unbounded'):>8} {ao.matched:>12,} {ao.failed:>11,}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
