# M4 — triage of the twelve, and the shape of what's left

Working from the list as it accumulated through stage A. **If CC's numbering differs, the mapping
matters and the numbers don't — correct the mapping, not the labels.**

The headline: **five of the twelve are A2's, not stage B's.** Stage B is smaller than the list
made it look, A2 is larger, and one item turns out to belong to C.

---

## The mapping

| # | Item | Goes to | Why there |
| --- | --- | --- | --- |
| 1 | A Kraken golden fails if produced by the Anvil parser; decoder identity travels with the pin; mutation swaps dispatch and expects red | **B** | The failure it guards against does not exist until a second adapter links. Untestable before B, mandatory at B. |
| 2 | A deliberately captured resync slice, not one of the four truncation traces | **B** | The CRC-mismatch path is the first thing that exercises the resync predicate at Kraken. Capture belongs with the code that needs it. |
| 3 | Stage D's criterion — quiet pair holds colour through 26 s, greys within the liveness threshold of the heartbeat stopping | **D** | Bench. Already written; nothing to do until the panel runs Kraken. |
| 4 | `age_ms` renders to at least minutes | **A2** | Header format. |
| 5 | `age_ms` is a sliding-window deficit, not cumulative | **A2** | The estimator's core constraint. |
| 6 | Freshness needs the client ping; Crow answers unconditionally and a pong behind a backlog measures the backlog | **deferred out of M4** | See decision (a). It is transport work, not estimator work. |
| 7 | `age_ms` definition — queuing lag, not time-since-frame, not time-since-change | **A2** | Definition the code is written against. |
| 8 | Backlogged-socket coverage gap; invariant #6 cannot be satisfied by a golden here | **A2** | A2 must say so in its own DoD rather than appear covered. |
| 9 | The venue table no longer duplicates `kRxWatchdogMs`; stage B deletes a constant rather than reconciling two | **B** | Firmware-side, and the deletion is the easiest thing B does. |
| 10 | Absence of a subscribe is not failure of a subscribe | **B** | The adapter's state machine, and the trap that makes the extreme slice unusable if missed. |
| 11 | Healthy feed, no snapshot yet — two defensible renderings, unsettled | **split C / D** | See decision (b). The engine needs the state; the panel decides what it looks like. |
| 12 | Synthesised snapshots named and rejected | **record only** | Done. No work order. |

Also inherited, not in the twelve, and both landing at **B**: `symbol_for()` and
`console_ladder`'s hardcoded `" ANVIL "` and raw-tick qty, loud today only because `run_replay`
refuses — B removes that guard, so they go live in the same evening; and `slice_trace`'s
reconnect-window self-containment, deliberately left because it is a capture-policy decision that
belongs with item 2's resync slice.

---

## Three decisions the triage surfaces

**(a) Does the client ping land in M4?** → **Recommend no.** It is the only non-estimated
freshness source, but it is transport work in the firmware WS client, it needs its own bench
evidence, and the windowed deficit is sufficient for a numeric header. Record it as owed by
whichever milestone next opens the transport layer — M6, on current shape — with the reason
stated, so it is not mistaken for an oversight.

**(b) Item 11 splits.** The engine carries an explicit **uninitialised** state, distinct from
*empty book*, because a book that has received nothing and a book whose every level is genuinely
empty are different facts and only one of them is knowledge — that is **C**, and it is small.
What the panel shows for it is **D**, decided at desk distance, because the two candidate
renderings differ in nothing a host test can assert. Stage B renders whatever the console does
today and does not pre-judge it.

**(c) The defensive resubscribe.** → **Recommend against for M4.** It converts an honest grey into
a hidden retry, and the accepted cost in §9 was recorded deliberately rather than as an oversight
waiting to be patched. Revisit when a second venue's behaviour can inform it — M5 at the earliest.
Leave the option named in `NOTES-kraken.md`.

---

## The resulting shape

**A2 — the age meter.** Items 4, 5, 7, 8. §5 gains `age_ms`; the estimator is a sliding-window
deficit against the liveness signal; the header renders to minutes; the DoD states the
backlogged-socket gap as uncoverable rather than passing over it. Uses the feeder-off Anvil trace
and `drain-120ms`. No Kraken, no adapter, no capture. **One evening, unblocked now.**

**B1 — the adapter.** Parse, subscribe-ack handling including item 10, deltas onto the phase-1
book. Item 9's constant deleted. The `run_replay` guard comes off and `symbol_for()` /
`console_ladder` are fixed in the same evening because that is when they break. **One evening.**

**B2 — the healing path.** CRC32, resync, item 2's captured slice, item 1's decoder-identity
mutation test, and `slice_trace`'s reconnect self-containment settled alongside the capture
policy. **One evening.**

**C — the dense-window book.** `engine/` only, plus item 11's uninitialised state. Goldens.
**One evening, shares with nothing.**

**D — the bench.** Item 3's criterion, item 11's rendering decided at the panel, venue proven on
hardware. **One evening, [B] track, yours.**

Five evenings. Four agentic, one bench.

## What M4's DoD becomes

Written when B1 lands, not before — but the shape is now visible enough to state the constraint:
**M4 is done when the panel renders a Kraken book off the wire, greys within the calibrated
liveness threshold when the heartbeat stops, holds colour through 26 s of legitimate book silence,
and shows a book age that is a lag estimate rather than a time-since-anything.** Each clause maps
to exactly one stage, which is the test of whether the split was drawn in the right places.
