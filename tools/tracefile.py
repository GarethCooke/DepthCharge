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

The format, set at M0 and extended once, at M4 stage A:

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

**The metadata contract is venue-conditional, and this is the Python half of
the rule** (the C++ half is ``harness/include/dc_harness/venue.hpp``; the prose
statement both share is ``harness/replay/NOTES.md``). Three fields are required
of every venue -- ``captured_at``, ``url``, ``tool_version`` -- and what
identifies the instrument is not portable: Anvil has an integer ``ticker``,
Kraken a string ``symbol``. ``venue`` itself is OPTIONAL and an absent tag reads
as ``anvil``, which is what makes the tag additive and what keeps the four
committed Anvil traces byte-identical: they predate it.

``clock`` names which monotonic clock stamped ``rx_ns``
(``monotonic_ns`` at Anvil, ``perf_counter_ns`` at Kraken -- 15.0 ms steps
against 0.0002 ms on this box). An absent ``clock`` reads back as
``undeclared`` rather than being inferred from the venue: the inference is sound
today, and a sound inference nobody re-checks is how this project has been wrong
before. Compare gaps across two traces only when their clocks agree.

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

# The marker capture_binance.py puts on a line that is NOT a WebSocket text
# frame from the venue (M5 stage 0). Two exist so far: `rest`, a
# /api/v3/depth fetch this client chose to make, and `control`, a WebSocket
# ping/pong/close. Both sit before `"frame"`, exactly where `dir` already sits.
#
# AN ABSENT `kind` MEANS WHAT EVERY EXISTING RECORD ALREADY IS -- a JSON text
# frame the venue sent over the WebSocket -- which is what keeps the four Anvil
# traces and the six Kraken ones byte-identical: nothing in them acquires a key.
# Same additivity rule the `venue` tag was granted at M4 stage A.
#
# `read_capture` SKIPS these by default, and the default is the point. Every
# caller that exists today is pricing a venue's wire or measuring inter-message
# gaps, and a REST body counted as a WS frame would inflate both by the largest
# single payload in the file. A caller that wants them says so.
KIND_KEY = '"kind": "'

# --- the venue-conditional metadata contract (M4 stage A) --------------------
# One table, mirroring kVenueTable in harness/include/dc_harness/venue.hpp.
# Adding a venue means adding a row in BOTH, and `check_meta` below is what
# makes a Python tool notice if only one of them was updated.
DEFAULT_VENUE = "anvil"
COMMON_REQUIRED = ("captured_at", "url", "tool_version")
VENUES: dict[str, tuple[str, ...]] = {
    "anvil": ("ticker",),
    "kraken": ("symbol",),
    "binance": ("symbol",),
}
UNDECLARED_CLOCK = "undeclared"


def decimals_of(text: str) -> int:
    """Fractional digit count of a decimal token. Never raises, never floats.

    Deliberately tolerant where `ticks` is strict: a caller COUNTING precision
    across a capture must be able to look at an exponent token and record it,
    not die on it. `ticks` is the one that refuses.
    """
    _, _, frac = text.partition(".")
    return len(frac)


def ticks(text: str) -> tuple[int, int]:
    """(scaled integer, fractional digits) for a decimal token, without floats.

    ONE implementation, shared by all three venues' tools (M5 stage 0 review).
    It had drifted into three: `kraken_frame_economics.ticks` and
    `binance_frame_economics.ticks` were character-identical, and
    `binance_oracle.ticks` was the same arithmetic returning only the value.
    Three copies of the routine that invariant #3 rests on is three places for
    the rounding to come back.

    The digits are SHIFTED, never rounded, and exponent notation is refused
    rather than parsed -- an adapter must do exactly this from the token text on
    the hot path, and a float here is the whole thing invariant #3 forbids.
    """
    neg = text.startswith("-")
    body = text[1:] if neg else text
    if "e" in body or "E" in body:
        raise ValueError(f"exponent notation on the wire: {text!r}")
    whole, _, frac = body.partition(".")
    value = int(whole + frac) if (whole + frac) else 0
    return (-value if neg else value), len(frac)


def venue_of(meta: dict) -> str:
    """The trace's venue. An absent tag is `anvil` -- that is the rule."""
    return meta.get("venue") or DEFAULT_VENUE


def binance_payload(frame):
    """Unwrap Binance's combined-stream envelope, or return the frame unchanged.

    `/ws/<stream>` delivers the bare payload; `/stream?streams=a/b` wraps it as
    `{"stream": "<name>", "data": {...}}` because one socket carries several
    streams and the payload alone does not say which. Every predicate below has
    to see through it, and NONE of them may strip it from the record -- whether
    the wrapper is worth its bytes is a measured question (M5 stage 0) and a
    reader that discarded it would have destroyed the evidence.
    """
    if isinstance(frame, dict) and "stream" in frame and "data" in frame:
        return frame["data"]
    return frame


def binance_stream_name(frame, meta: dict | None = None) -> str:
    """Which stream a Binance frame belongs to.

    From the combined-stream envelope when there is one, otherwise from the
    metadata header -- a capture without the envelope is single-stream by
    construction. Lives beside `binance_payload` because it is the same piece of
    knowledge (what the wrapper is) read for the other half; review found that
    knowledge open-coded in three files.

    NOTE the wrapper is never stripped from a byte count by any caller: whether
    it is worth its bytes is a measured question, and a reader that discarded it
    would have destroyed the evidence.
    """
    if isinstance(frame, dict) and "stream" in frame:
        return str(frame["stream"]).split("@", 1)[-1]
    streams = (meta or {}).get("streams") or ["?"]
    return streams[0]


def is_book_event(venue: str, frame) -> bool:
    """Does this frame reach the book? Mirrors the C++ decoders' answer.

    Anvil: a full replace (`snapshot`/`book`) or a `trade`. NOT `summary`, which
    is cross-ticker roster data the adapter tolerates and ignores.
    Kraken: anything on the `book` channel.
    """
    if not isinstance(frame, dict):
        return False
    if venue == "anvil":
        return frame.get("type") in ("snapshot", "book", "trade")
    if venue == "binance":
        p = binance_payload(frame)
        if not isinstance(p, dict):
            return False
        # A diff event, or a partial-depth payload (which has no event type at
        # all -- it is identified by carrying `lastUpdateId` and nothing else).
        return p.get("e") == "depthUpdate" or "lastUpdateId" in p
    return frame.get("channel") == "book"


def is_snapshot(venue: str, frame) -> bool:
    """Does this frame REPLACE the book rather than amend it?"""
    if not isinstance(frame, dict):
        return False
    if venue == "anvil":
        return frame.get("type") == "snapshot"
    if venue == "binance":
        # A partial-depth payload (`@depth20`) replaces the top 20 outright.
        # NOTE what this deliberately does NOT cover: at this venue the record a
        # RESYNC arrives as is a REST /api/v3/depth fetch, which is a
        # `kind: "rest"` record and not a WS frame at all -- the first venue where
        # the healing event does not come down the socket. That asymmetry is M5's
        # to resolve; this predicate answers only for frames.
        p = binance_payload(frame)
        return isinstance(p, dict) and "lastUpdateId" in p and "e" not in p
    return frame.get("channel") == "book" and frame.get("type") == "snapshot"


def rebaselines(venue: str, frame, kind: str = "") -> bool:
    """After this record, is the book FULLY KNOWN — no earlier record required?

    NOT the same question as `is_snapshot`, and the difference is Anvil's
    (added M4 stage B2, for `slice_trace`'s self-containment rule).

    At Anvil `snapshot` and `book` are the same thing on the wire — a full top-N
    replace, confirmed byte-identical at M0 — and `AnvilAdapter::on_frame`
    emits `FeedEvent::Snapshot` for BOTH (`anvil_adapter.hpp`'s switch is the
    authority). So every `book` frame re-baselines, and a window that begins on
    one is perfectly replayable. `is_snapshot` deliberately says False for them,
    because it answers a different question — *is this the record a resync would
    arrive as* — and at Anvil that is specifically the connect-time `snapshot`.
    Measured on `anvil_101_reconnect.ndjson`: there is no `type:"snapshot"`
    anywhere before the reconnect one at t+29.9, so a self-containment rule
    written on `is_snapshot` would refuse to cut the very trace the mode exists
    for.

    At Kraken the two coincide: only `book/snapshot` replaces, an update amends,
    and `KrakenAdapter::apply_update` drops deltas outright until a snapshot has
    arrived (*no baseline, no deltas*).

    NO C++ TWIN, deliberately. The three other predicates here mirror
    `harness/include/dc_harness/trace_decoder.hpp` because the C++ reader asks
    those questions too; nothing in the harness asks this one — only the slicer
    does — and adding an unused field to `RecordKind` would move the taxonomy
    pins to carry weight nobody lifts. If a C++ caller ever needs it, the
    authority to mirror is the adapters' own dispatch, named above.
    """
    if not isinstance(frame, dict):
        return False
    if venue == "binance" and kind == "rest":
        # THE ONLY THING AT THIS VENUE THAT RE-BASELINES (M5 stage 0). A
        # /api/v3/depth body is a complete book to its `limit`, stamped with the
        # `lastUpdateId` the diff stream is bracketed against -- so a window that
        # begins here is replayable and one that begins anywhere else is not.
        # `kind` is a parameter rather than something sniffed out of the frame
        # because a REST body and a `@depth20` payload are the same SHAPE
        # (`bids`/`asks`/`lastUpdateId`) and only the record that carries them
        # says which is which.
        return "lastUpdateId" in frame
    if venue == "anvil":
        return frame.get("type") in ("snapshot", "book")
    if venue == "binance":
        # ALWAYS FALSE FOR ANY WS FRAME, and this is not an oversight.
        #
        # A `@depth20` partial payload fully determines the top 20 and nothing
        # below it, so a book maintained from the diff stream is NOT fully known
        # after one: the levels outside the window keep whatever they had, and a
        # window cut here would replay a book with an unknown tail. Anvil and
        # Kraken both have a frame that makes the whole subscribed book known;
        # Binance does not, because its baseline arrives out of band over REST.
        #
        # The consequence for slicing is the branch above: a Binance diff
        # capture can only be cut at a `kind: "rest"` record, which is what
        # `slice_trace.py --from-baseline` exists for.
        return False
    return is_snapshot(venue, frame)


def is_trade(venue: str, frame) -> bool:
    """Is this a trade print rather than a book change?

    Anvil interleaves trades with book frames on one socket, so "reaches the
    book" and "changes a resting level" are different questions there and the
    watchdog literature distinguishes them (`gap_stats.py`'s two book-oriented
    counting rules). Kraken puts trades on their own `trade` channel, which
    DepthCharge does not subscribe to -- so this is always False on a book
    capture, and the two counting rules coincide. That coincidence is a property
    of the subscription, not of the venue, which is why it is spelled out rather
    than assumed.
    """
    if not isinstance(frame, dict):
        return False
    if venue == "anvil":
        return frame.get("type") == "trade"
    if venue == "binance":
        p = binance_payload(frame)
        # Same shape of coincidence as Kraken's, and stated for the same reason:
        # trades live on `@trade`/`@aggTrade`, which DepthCharge does not
        # subscribe to, so this is a property of the subscription and not of the
        # venue.
        return isinstance(p, dict) and p.get("e") in ("trade", "aggTrade")
    return frame.get("channel") == "trade"


def is_liveness(venue: str, frame) -> bool:
    """Is this the venue's DECLARED LIVENESS SIGNAL?

    Anvil's `summary`, Kraken's `heartbeat` -- the record whose arrival proves
    the feed is alive, and (since the 2026-08-17 ruling, ARCHITECTURE.md §9) the
    only clock the panel's grey state is armed on. Mirrors
    `harness/include/dc_harness/venue.hpp`'s `liveness_signal` field; if the two
    disagree, the C++ reader and the Python tools are measuring different things
    about the same file.

    The distinction from `is_book_event` is the whole ruling: `summary` is roster
    data the adapter ignores, so it is NOT a book event -- and that is exactly
    what makes it usable here. It proves the server is alive without pretending
    the book moved.
    """
    if not isinstance(frame, dict):
        return False
    if venue == "anvil":
        return frame.get("type") == "summary"
    if venue == "binance":
        # **ALWAYS FALSE, AND THAT IS THE FINDING** (M5 stage 0, measured).
        #
        # Anvil declares liveness with `summary`, Kraken with `heartbeat`, and the
        # 2026-08-17 ruling arms the panel's grey state on that record and on
        # nothing else. Binance's depth streams publish no such record: there is
        # no frame whose arrival proves the feed is alive without claiming the
        # book moved.
        #
        # What this venue has instead is a **WebSocket PING control frame every
        # ~20 s**, which is a liveness signal in every respect except that it is
        # not JSON, is not a record, and -- before the `kind` proposal above --
        # could not appear in a DepthCharge trace at all. So Binance's row in the
        # venue table cannot be filled in by copying either predecessor's, and the
        # thing that would fill it lives one layer down, in the transport.
        return False
    return frame.get("channel") == "heartbeat"


def record_kind(venue: str, frame, is_tx: bool = False) -> str:
    """A short label for the record's kind, agreeing with the C++ decoders.

    The strings are the ones `harness/include/dc_harness/trace_decoder.hpp` and
    `kraken_frame_economics.py` already produce -- `book/update`, `heartbeat`,
    `ack:subscribe`, `tx:subscribe` -- so a histogram printed by a Python tool
    and one printed by `dc_taxonomy` are counting the same buckets. At Anvil the
    kind is the bare wire `type`, unchanged.
    """
    if not isinstance(frame, dict):
        return "?"
    if venue == "anvil":
        return frame.get("type") or "?"
    if venue == "binance":
        p = binance_payload(frame)
        if not isinstance(p, dict):
            return "?"
        if p.get("e") is not None:
            return str(p["e"])            # `depthUpdate`
        if "lastUpdateId" in p:
            return "partialDepth"         # `@depth20` -- no event type on the wire
        if "result" in p or "id" in p:
            return "ack"
        return "?"
    channel = frame.get("channel")
    if channel is not None:
        kind = frame.get("type")
        return f"{channel}/{kind}" if kind else str(channel)
    method = frame.get("method")
    if method is not None:
        if is_tx:
            return "tx:" + str(method)
        refused = frame.get("success") is False or frame.get("error") is not None
        return "ack:" + str(method) + (" REFUSED" if refused else "")
    return "error" if frame.get("error") is not None else "?"


def clock_of(meta: dict) -> str:
    """Which clock stamped rx_ns, or `undeclared`. Never inferred."""
    return meta.get("clock") or UNDECLARED_CLOCK


def check_meta(meta: dict) -> str:
    """Validate the header against its venue's contract; return the venue.

    Raises ValueError with the same distinction the C++ reader draws: a venue
    this tooling does not know is a DIFFERENT failure from a malformed header,
    and only the second is a bug in the file.
    """
    venue = venue_of(meta)
    if venue not in VENUES:
        raise ValueError(
            f"capture declares venue {venue!r}, which these tools do not know how to "
            f"read (the header itself looks well-formed). Known: "
            f"{', '.join(sorted(VENUES))}")
    missing = [k for k in COMMON_REQUIRED + VENUES[venue] if k not in meta]
    if missing:
        raise ValueError(
            f"metadata line missing a required field for venue {venue!r}: "
            f"{', '.join(missing)}")
    return venue


def kind_of_line(line: str, upto: int) -> str:
    """The record's `kind`, or `""` for a plain venue WS text frame.

    Read off the raw line rather than the parsed object, because the parsed
    object is the FRAME and the kind is a property of the wrapper. Bounded to the
    text before `"frame"` for the same reason `is_tx` is: a REST body is the
    venue's text and must never be able to describe the record that carries it.
    """
    at = line.find(KIND_KEY, 0, upto)
    if at < 0:
        return ""
    at += len(KIND_KEY)
    end = line.find('"', at)
    return line[at:end] if end > at else ""


class Record(NamedTuple):
    """One capture line."""

    raw: str        # the frame's verbatim text, exactly as the venue sent it
    frame: object   # the same text parsed (usually a dict)
    rx_ns: int      # monotonic ns at arrival (or at send, for a tx record)
    is_tx: bool     # True only for a frame this side sent
    lineno: int
    kind: str = ""  # "" = a venue WS text frame; "rest"/"control" (M5 stage 0)
    wrapper: dict | None = None  # the non-frame half of a kinded record


def read_capture(path, *, skip_tx: bool = True, kinds: tuple[str, ...] = ()):
    """Yield a Record per frame line. Raises ValueError with a line number.

    `skip_tx` defaults to True because every existing caller is measuring the
    venue, and a caller that wants the subscribe back has to say so.

    `kinds` names the non-frame record kinds to ALSO yield (M5 stage 0); the
    default of none means a caller written before Binance existed sees exactly
    what it always saw -- the venue's WS text frames -- even when handed a trace
    full of REST fetches and control frames. A caller that wants them opts in,
    and gets `Record.kind` and `Record.wrapper` filled in.
    """
    want = frozenset(kinds)
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
            kind = kind_of_line(line, start)
            if kind and kind not in want:
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
            wrapper = None
            if kind:
                # The wrapper is this tool's own output and therefore safely
                # re-parsable; only the frame is the venue's.
                head = line[:start].rstrip()
                if head.endswith(","):
                    head = head[:-1]
                try:
                    wrapper = json.loads(head + "}")
                except json.JSONDecodeError as exc:
                    raise ValueError(
                        f"line {lineno}: unreadable {kind!r} wrapper ({exc})") from exc
            yield Record(raw, frame, rx_ns, is_tx, lineno, kind, wrapper)


def read_meta(path, *, validate: bool = False) -> dict:
    """Line 1: the capture's metadata header.

    `validate` is opt-in rather than always-on because the enforcement point for
    "is this a valid trace" is the C++ reader (ARCHITECTURE §9, 2026-08-07: one
    definition, and it is TraceReader::next). These tools MEASURE captures,
    including local scratch ones written by the probes, and a measurement tool
    that refuses to open a file it can read perfectly well would be its own kind
    of wrong. A caller that is about to make a claim about a venue says so.
    """
    with open(path, encoding="utf-8") as fh:
        line = fh.readline()
    if not line.strip():
        raise ValueError("empty trace (no capture header)")
    meta = json.loads(line)
    if validate:
        check_meta(meta)
    return meta


# --- the module's own tests (M5 stage 0, at review) -------------------------
# `tracefile.py` is the ONE definition of how a capture line is read, shared by
# every Python tool and mirroring the C++ reader -- and until this point it had
# no test of its own. Its venue predicates are the sharpest reason it needed
# one: each was written `if venue == "anvil": ... else: <the previous venue's
# answer>`, so a venue added without an explicit branch does not fail, it
# returns a CONFIDENT WRONG ANSWER. That is strain 22's real cost, and nothing
# was checking for it.


def _decimal_samples():
    """Decimal tokens spanning the shapes the three venues actually send."""
    out = []
    for whole in ("0", "1", "78564", "9" * 12):
        for frac in ("", "0", "5", "00000000", "10000000", "12345678"):
            out.append(whole if not frac else whole + "." + frac)
    return out + ["-1.5", "-0.00000001", "0.0", "000.100"]


def selfcheck() -> int:
    """Properties and shapes, not a corpus.

    Deliberately takes no trace argument, for the reason `slice_trace.py`'s
    selfcheck takes none: the cases that matter are the ones no committed
    capture contains. Every committed capture is well-formed, so a corpus-driven
    test here would be eleven copies of one observation.
    """
    checks = 0
    fails = []

    def check(cond, why):
        nonlocal checks
        checks += 1
        if not cond:
            fails.append(why)

    # -- ticks: properties, not examples ----------------------------------
    for text in _decimal_samples():
        value, decs = ticks(text)
        # Round-trip: the scaled integer is exactly the digits with the point
        # removed, sign preserved. Stated without a float anywhere in it.
        neg = text.startswith("-")
        digits = (text[1:] if neg else text).replace(".", "")
        expect = -int(digits) if neg else int(digits)
        check(value == expect, f"ticks({text!r}) value {value} != {expect}")
        check(decs == decimals_of(text),
              f"ticks({text!r}) decimals {decs} != decimals_of {decimals_of(text)}")

    # Monotonicity at a common scale: the ordering of scaled values must follow
    # the ordering of the numbers they came from.
    ordered = ("1.0", "1.00000001", "1.5", "2", "78564.00000000")
    scaled = [ticks(t)[0] * 10 ** (8 - ticks(t)[1]) for t in ordered]
    check(scaled == sorted(scaled), f"ticks is not monotonic: {scaled}")

    # Exponent notation is REFUSED by ticks and TOLERATED by decimals_of: a
    # caller counting precision across a capture must be able to see one
    # without dying on it, and the adapter path must never accept one.
    try:
        ticks("1e5")
        check(False, "ticks accepted exponent notation")
    except ValueError:
        check(True, "")
    check(decimals_of("1e5") == 0, "decimals_of raised or mis-counted on 1e5")

    # -- binance_payload / binance_stream_name ----------------------------
    wrapped = {"stream": "btcusdt@depth@100ms", "data": {"e": "depthUpdate"}}
    bare = {"e": "depthUpdate"}
    check(binance_payload(wrapped) == bare, "envelope not unwrapped")
    check(binance_payload(binance_payload(wrapped)) == bare,
          "binance_payload is not idempotent")
    check(binance_payload(bare) == bare, "a bare payload was altered")
    check(binance_stream_name(wrapped) == "depth@100ms",
          "stream name not read from the envelope")
    check(binance_stream_name(bare, {"streams": ["depth20"]}) == "depth20",
          "stream name not read from the metadata")

    # -- the venue predicates, INCLUDING the fallthrough hazard ------------
    diff = {"e": "depthUpdate", "U": 1, "u": 2, "b": [], "a": []}
    partial = {"lastUpdateId": 7, "bids": [], "asks": []}
    kraken_book = {"channel": "book", "type": "update"}
    anvil_book = {"type": "book", "ticker": 101}

    check(is_book_event("binance", diff), "binance diff is not a book event")
    check(is_book_event("binance", {"stream": "x@depth", "data": diff}),
          "a wrapped binance diff is not a book event")
    check(is_book_event("binance", partial), "binance partial is not a book event")
    # THE FALLTHROUGH TEST. Kraken's answer for a Binance frame is False, so a
    # venue added without its own branch reports an EMPTY capture rather than an
    # error -- the confident wrong answer, caught here rather than in a figure.
    check(is_book_event("kraken", diff) is False,
          "a Binance frame reads as a Kraken book event")
    check(is_snapshot("binance", partial), "a partial does not replace the top-N")
    check(is_snapshot("binance", diff) is False, "a diff is not a snapshot")
    check(rebaselines("binance", partial) is False,
          "a WS frame must never re-baseline at Binance")
    check(rebaselines("binance", partial, "rest") is True,
          "a REST record is the only thing that re-baselines at Binance")
    check(is_liveness("binance", diff) is False
          and is_liveness("binance", partial) is False,
          "Binance declares no liveness record -- this must stay False")
    check(is_liveness("kraken", {"channel": "heartbeat"}), "kraken heartbeat")
    check(is_liveness("anvil", {"type": "summary"}), "anvil summary")
    check(is_trade("binance", {"e": "aggTrade"}), "binance aggTrade")
    check(record_kind("binance", diff) == "depthUpdate", "record_kind binance diff")
    check(record_kind("binance", partial) == "partialDepth",
          "record_kind binance partial")
    check(record_kind("kraken", kraken_book) == "book/update", "record_kind kraken")
    check(record_kind("anvil", anvil_book) == "book", "record_kind anvil")

    # Every declared venue must ANSWER for every shape, including junk, rather
    # than raising -- these predicates run over whatever a venue actually sent.
    for venue in VENUES:
        for frame in (diff, partial, kraken_book, anvil_book, None, [], "x", 3):
            for fn in (is_book_event, is_snapshot, is_trade, is_liveness,
                       rebaselines):
                check(isinstance(fn(venue, frame), bool),
                      f"{fn.__name__}({venue!r}, {frame!r}) is not a bool")

    # -- the record line shape, against the spellings the writers emit -----
    lines = [
        '{"rx_ns": 5, "frame": {"e":"depthUpdate"}}',
        '{"rx_ns": 6, "dir": "tx", "frame": {"method":"subscribe"}}',
        '{"rx_ns": 7, "kind": "rest", "req": {"limit": 100}, "frame": {"lastUpdateId":1}}',
        '{"rx_ns": 8, "kind": "control", "ctl": {"op": "ping"}, "frame": null}',
    ]
    for line, want in zip(lines, ("", "", "rest", "control")):
        check(kind_of_line(line, line.find(FRAME_KEY)) == want,
              f"kind_of_line got {kind_of_line(line, line.find(FRAME_KEY))!r}, want {want!r}")

    # A frame body containing the kind marker must not be able to describe the
    # record that carries it -- which is why the search is bounded by `upto`.
    sneaky = json.dumps({"rx_ns": 9})[:-1] + ', "frame": ' + json.dumps(
        {"note": '"kind": "rest"'}) + "}"
    check(kind_of_line(sneaky, sneaky.find(FRAME_KEY)) == "",
          "a frame body was allowed to describe its own record kind")

    import os
    import tempfile
    header = ('{"captured_at": "X", "url": "ws://x", "venue": "binance", '
              '"symbol": "BTCUSDT", "tool_version": "0.1.0"}')
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "t.ndjson")
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(header + "\n")
            for line in lines:
                fh.write(line + "\n")
        plain = list(read_capture(path))
        check(len(plain) == 1, f"default read yielded {len(plain)} records, want 1")
        check(plain[0].kind == "", "a plain frame reported a kind")
        opted = list(read_capture(path, kinds=("rest", "control")))
        check(len(opted) == 3, f"opted-in read yielded {len(opted)}, want 3")
        rest = [r for r in opted if r.kind == "rest"][0]
        check(rest.wrapper["req"]["limit"] == 100, "rest wrapper not parsed")
        check(rest.frame["lastUpdateId"] == 1, "rest body not parsed")
        ctl = [r for r in opted if r.kind == "control"][0]
        check(ctl.frame is None, "a control record must carry no frame")
        check(ctl.wrapper["ctl"]["op"] == "ping", "control wrapper not parsed")
        withtx = list(read_capture(path, skip_tx=False))
        check(sum(1 for r in withtx if r.is_tx) == 1, "the tx record was not surfaced")
        check(check_meta(read_meta(path)) == "binance", "check_meta did not say binance")

    for why in fails:
        print("  FAIL " + why)
    print(f"[tracefile] selfcheck {'FAILED' if fails else 'OK'}: {checks} checks")
    return 1 if fails else 0


if __name__ == "__main__":
    import sys as _sys
    if "--selfcheck" in _sys.argv:
        raise SystemExit(selfcheck())
    _sys.stderr.write("tracefile.py is a library; pass --selfcheck to test it.\n")
    raise SystemExit(2)
