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
import enum
from pathlib import Path

from tracefile import binance_payload, read_capture, read_meta
from tracefile import ticks as _ticks


def ticks(text: str) -> int:
    """The scaled integer alone. `tracefile.ticks` is the one implementation;
    this file only ever wants the value, and both sides of every comparison
    here go through it so a level is compared as an integer and never as a
    float (invariant #3)."""
    return _ticks(text)[0]


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
        # The deepest either side ever got BEFORE truncation. This is what makes
        # "the bounded-window mutant could not fire on this trace" a measured
        # statement rather than an inference from a green.
        self.high_water = 0

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
        self.high_water = max(self.high_water, len(self.bids), len(self.asks))
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


class Replay:
    """One replay of one capture against one implementation of the book.

    A class rather than a 127-line function with a dozen closures (review, M5
    stage 0). The state below was being closed over by five nested helpers, and
    `_apply` took NINE parameters to carry it between them -- which is the
    missing-abstraction signal in its plainest form. Nothing about the grading
    changed in the move; the pinned figures are identical, which is what
    `--check` and `binance_tool_selfcheck` are for.
    """

    def __init__(self, trace: Path, mutation: str = HONEST, window: int = 0,
                 depth: int = 20) -> None:
        self.trace = trace
        self.depth = depth
        self.meta = read_meta(trace, validate=True)
        self.book = RefBook(mutation, window)
        self.a_out = Outcome()          # oracle (a), the partial-depth stream
        self.b_out = Outcome()          # oracle (b), the REST re-snapshot
        self.uu = {"tested": 0, "ok": 0, "break": 0, "breaks": []}
        self.buffered: list[dict] = []
        self.graded: list = []
        # top-N book images keyed by the `u` that produced them, so a REST body
        # that arrives a second late can still be graded against the instant it
        # names.
        self.history: dict[int, tuple[list, list]] = {}
        self.pending: dict[int, dict] = {}   # partials awaiting their diff
        self.brackets: list[tuple[int, int]] = []
        self.seeded = False
        self.first_seed_id = None

    # -- grading ----------------------------------------------------------

    def grade_against(self, payload: dict, got, out: Outcome, tag: str,
                      detail: bool) -> None:
        """Grade one venue-published top-N against a book image.

        ONE routine. It was two -- `compare` and `compare_image` -- sharing four
        of their six lines and differing only in where the book came from and
        how much detail a failure carried.

        `got` is the image to grade against: the LIVE book for a partial
        payload, and the image AS OF the snapshot's id for a REST body, because
        a REST body is a statement about a past instant.
        """
        want_b, want_a = partial_top(payload, self.depth)
        got_b, got_a = got
        keep_b = min(len(want_b), len(got_b))
        keep_a = min(len(want_a), len(got_a))
        bids_ok = got_b[:keep_b] == want_b[:keep_b]
        asks_ok = got_a[:keep_a] == want_a[:keep_a]
        why = ()
        if detail:
            why = ((("bids", want_b[:3], got_b[:3]) if not bids_ok
                    else ("asks", want_a[:3], got_a[:3])),)
        out.note(bids_ok and asks_ok,
                 detail=(tag, payload["lastUpdateId"]) + why)

    def compare(self, payload: dict, tag: str) -> None:
        """Grade a partial payload against the LIVE book (oracle (a))."""
        image = self.book.top(self.depth)
        # The book AS GRADED, kept so that "this mutation changed nothing
        # observable on this trace" is a comparison rather than a heuristic.
        self.graded.append(image)
        self.grade_against(payload, image, self.a_out, tag, detail=True)

    # -- the two REST paths -----------------------------------------------

    def seed_from(self, body: dict) -> None:
        """THE DOCUMENTED PROCEDURE, once, where it can be read.

        Buffer the diffs, fetch the snapshot, drop everything it already
        contains, and require the first surviving event to bracket L + 1.
        """
        L = body["lastUpdateId"]
        self.first_seed_id = L
        self.book.seed(body)
        self.history[L] = self.book.top(self.depth)
        survivors = [e for e in self.buffered if e["u"] > L]
        self.buffered = []
        self.seeded = True
        if survivors:
            first = survivors[0]
            self.uu["bracket_ok"] = first["U"] <= L + 1 <= first["u"]
            self.uu["bracket_first_U"] = first["U"]
            self.uu["bracket_first_u"] = first["u"]
            self.uu["bracket_snapshot_id"] = L
        for event in survivors:
            self.apply_event(event)

    def grade_resnapshot(self, body: dict) -> None:
        """ORACLE (b): a re-snapshot graded against the book AS OF its own id.

        NOT against the live book, and getting that wrong is the first thing
        this tool did. A /api/v3/depth round trip measured ~1.0-1.5 s from this
        box, so by the time the body is in hand the stream has moved 10-15
        events past the id it is stamped with. Comparing against the live book
        fails every time and -- worse -- fails in a way that looks like "the
        venue's ids never line up", which is the opposite of what the wire does
        (measured: they line up exactly, 23/23 and 5/5). A snapshot is a
        statement about a PAST instant and must be graded against that instant.
        """
        L = body["lastUpdateId"]
        image = self.history.get(L)
        if image is not None:
            self.grade_against(body, image, self.b_out, "rest", detail=False)
        elif any(U < L < u for U, u in self.brackets):
            self.b_out.note_unverifiable("inside-a-coalesced-bracket")
        elif L > (self.book.last_u or 0):
            self.b_out.note_unverifiable("ahead-of-applied-stream")
        elif L < (self.first_seed_id or 0):
            self.b_out.note_unverifiable("before-the-baseline")
        else:
            self.b_out.note_unverifiable("no-event-ends-on-this-id")

    # -- the diff stream ---------------------------------------------------

    def apply_event(self, event: dict) -> None:
        """Apply one diff event, count its `U`/`u` continuity, grade what it unblocks."""
        prev_u = self.book.last_u
        if prev_u is not None and self.brackets:
            self.uu["tested"] += 1
            if event["U"] == prev_u + 1:
                self.uu["ok"] += 1
            else:
                self.uu["break"] += 1
                if len(self.uu["breaks"]) < 5:
                    self.uu["breaks"].append((prev_u, event["U"]))
        self.brackets.append((event["U"], event["u"]))
        self.book.apply(event)
        self.history[event["u"]] = self.book.top(self.depth)
        if len(self.history) > 4000:   # a capture is 90 s; never trims in practice
            for key in sorted(self.history)[:1000]:
                del self.history[key]
        held = self.pending.pop(event["u"], None)
        if held is not None:
            self.compare(held, "partial(deferred)")
        # Anything still pending that this event has overshot can never be graded.
        for L in [k for k in self.pending if k < event["u"]]:
            self.pending.pop(L)
            inside = any(U < L < u for U, u in self.brackets)
            self.a_out.note_unverifiable("inside-a-coalesced-bracket" if inside
                                         else "overshot-by-a-coalesced-event")

    def on_partial(self, payload: dict) -> None:
        """ORACLE (a): the venue's own top-N, on the same socket."""
        L = payload["lastUpdateId"]
        if L == self.book.last_u:
            self.compare(payload, "partial")
        elif L > (self.book.last_u or 0):
            # Arrived before the diff that ends on it. Hold and grade when that
            # diff lands -- the two streams are interleaved on one socket and
            # neither is owed the other's ordering.
            self.pending[L] = payload
        else:
            inside = any(U < L < u for U, u in self.brackets)
            self.a_out.note_unverifiable("inside-a-coalesced-bracket" if inside
                                         else "no-event-ends-on-this-id")

    # -- the driver --------------------------------------------------------

    def execute(self):
        for rec in read_capture(self.trace, kinds=("rest",)):
            if rec.kind == "rest":
                if rec.frame is None or "lastUpdateId" not in rec.frame:
                    continue
                if not self.seeded:
                    self.seed_from(rec.frame)
                else:
                    self.grade_resnapshot(rec.frame)
                continue
            payload = binance_payload(rec.frame)
            if not isinstance(payload, dict):
                continue
            if payload.get("e") == "depthUpdate":
                if not self.seeded:
                    self.buffered.append(payload)
                else:
                    self.apply_event(payload)
            elif "lastUpdateId" in payload and self.seeded:
                self.on_partial(payload)

        # Partials still waiting when the capture ended never got their diff.
        for L in self.pending:
            inside = any(U < L < u for U, u in self.brackets)
            self.a_out.note_unverifiable("inside-a-coalesced-bracket" if inside
                                         else "never-reached-before-capture-ended")

        self.uu["seeded_from"] = self.first_seed_id
        self.uu["high_water"] = self.book.high_water
        self.uu["graded"] = self.graded
        return self.a_out, self.b_out, self.uu


def run(trace: Path, mutation: str = HONEST, window: int = 0, depth: int = 20,
        verbose: bool = False):
    """Replay a capture and grade the book at every opportunity the venue gives.

    Returns (oracle_a, oracle_b, uu) where the first two are `Outcome`s and the
    third is the `U`/`u` continuity tally. Kept as a function because every
    caller wants exactly this triple.
    """
    return Replay(trace, mutation, window, depth).execute()


class Verdict(enum.Enum):
    """GREEN / RED / **VACUOUS**, and the third one is the point.

    An enum rather than a string (review, M5 stage 0): `check` used to branch on
    `.startswith("RED")`, so a typo in a comparison string would silently turn a
    failure into a pass -- in the one file whose whole thesis is that a pass and
    an unchecked thing must never look alike.
    """

    GREEN = "GREEN"
    RED = "RED"
    VACUOUS = "VACUOUS"


def grade(failed: int, matched: int) -> Verdict:
    """A detector that made no comparison is VACUOUS, not GREEN.

    `failed == 0` is true both of a detector that checked everything and passed
    and of one that checked nothing. This tool printed the second as GREEN on its
    first run against the deep-seed capture -- oracle (b) graded nothing while
    reporting green against every mutant, because it compared a REST body against
    the LIVE book instead of the instant the body names. The bug is fixed; the
    state it hid behind is named so the next one cannot hide there.
    """
    if failed:
        return Verdict.RED
    return Verdict.GREEN if matched else Verdict.VACUOUS


def verdict(failed: int, matched: int) -> str:
    """The printable form of `grade`."""
    v = grade(failed, matched)
    if v is Verdict.RED:
        return f"RED ({failed})"
    if v is Verdict.VACUOUS:
        return "VACUOUS (0 graded)"
    return "GREEN"


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


def _check_trace(t, depth: int) -> tuple[list, bool]:
    """Assert the mutation contract for ONE trace.

    Returns (failures, exercised_all_three). Split out at review: `check` was
    ninety lines carrying both the per-trace assertions and the across-the-set
    coverage rule, which are two different claims.
    """
    failures = []
    # The honest baseline is deliberately UNBOUNDED (window 0) whatever
    # `--window` says: the pin means "a correct client grades clean", and a
    # correct client at this venue does not truncate its book. `main` refuses
    # `--window` alongside `--check` rather than ignoring it silently.
    honest, honest_b, uu = run(t, HONEST, 0, depth)
    if grade(honest.failed, honest.matched) is not Verdict.GREEN:
        failures.append(f"{t.name}: honest control is "
                        f"{verdict(honest.failed, honest.matched)}, expected GREEN")
    # Oracle (b) is computed anyway, so it is ASSERTED rather than discarded
    # (review). It may legitimately be VACUOUS -- a short slice holds few REST
    # records -- but it must never be RED against an honest client.
    if grade(honest_b.failed, honest_b.matched) is Verdict.RED:
        failures.append(f"{t.name}: oracle (b) is "
                        f"{verdict(honest_b.failed, honest_b.matched)} against the "
                        "honest control, expected GREEN or VACUOUS")
    # How deep the honest book ever got. A trace whose book never exceeds the
    # bounded window cannot exercise the bounded-window mutant AT ALL -- the
    # truncation step never removes anything, so the "mutant" is the honest
    # implementation. Measured, not inferred from a green.
    honest_books = uu.get("graded", [])
    inert = []
    for mut, win in ((M_ZERO_QTY, 0), (M_SWAP_SIDES, 0), (M_BOUNDED_WINDOW, depth + 5)):
        mm = HONEST if mut == M_BOUNDED_WINDOW else mut
        out, out_b, u2 = run(t, mm, win, depth)
        v = grade(out.failed, out.matched)
        if u2.get("graded", []) == honest_books:
            # NOT a pass and NOT a failure: on THIS trace the mutation
            # produced no observable difference at any graded tick, so the
            # trace cannot ask the question. Detected by comparing the graded
            # books rather than by a per-mutant heuristic -- a first attempt
            # asked "did the book exceed the window", which said EXERCISABLE
            # for a quiet pair whose deep levels never rise into the top 20.
            #
            # Reported loudly, because a capture too quiet to break is
            # indistinguishable from an oracle too weak to notice, and the
            # whole point of this file is that those must never look alike.
            inert.append(f"{mut} NOT EXERCISABLE (its book is identical to the "
                         f"honest one at all {len(honest_books)} graded ticks)")
            continue
        if v is not Verdict.RED:
            failures.append(f"{t.name}: mutant {mut} is "
                            f"{verdict(out.failed, out.matched)}, expected RED -- "
                            "the oracle no longer catches it")
        # (b) is weaker and may grade nothing; when it DID grade, it must agree.
        if grade(out_b.failed, out_b.matched) is Verdict.GREEN:
            failures.append(f"{t.name}: mutant {mut} is GREEN under oracle (b), "
                            "which graded and did not catch it")
    for line in inert:
        print(f"  {t.name}: {line}")
    noop, _, _ = run(t, M_NO_TRUNCATE, 0, depth)
    if grade(noop.failed, noop.matched) is not Verdict.GREEN:
        failures.append(
            f"{t.name}: {M_NO_TRUNCATE} is now caught. That mutant is asserted "
            "to be a NO-OP at this venue because the diff stream carries every "
            "removal explicitly. If it has started failing, the wire changed "
            "or the reference book did -- read NOTES-binance.md before editing "
            "this expectation.")
    exercised = 3 - len(inert)
    print(f"  {t.name}: honest GREEN ({honest.matched} graded), "
          f"{exercised}/3 mutants exercised and RED, no-truncation asserted no-op")

    return failures, exercised == 3


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
    full_coverage = False
    for t in traces:
        trace_failures, full = _check_trace(t, depth)
        failures.extend(trace_failures)
        full_coverage = full_coverage or full
    if not full_coverage:
        failures.append(
            "NO trace in this set exercised all three mutants. At least one must: "
            "a suite whose every witness is too quiet to ask the hardest question "
            "reports green for the same reason an empty one does.")
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
        if args.window:
            ap.error("--window cannot be combined with --check: the pinned honest "
                     "baseline is unbounded by definition, and silently ignoring "
                     "the flag would make the check mean something other than it "
                     "says.")
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
