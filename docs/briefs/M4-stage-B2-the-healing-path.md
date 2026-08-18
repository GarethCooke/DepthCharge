# M4 Stage B2 — the healing path

**Track:** Agentic [A] · **Status:** Complete 2026-08-18, awaiting split approval · **Size:** one evening, with a stated cut point
**Executor:** Claude Code, **desk only. No board, no flash, no dense-window book, no panel
decisions, no watchdog rewiring.**

B1 left a checksum parsed and carried but never verified, an adapter that has never been wrong on
purpose, and one assumption holding up a shipped number. This evening turns all three into
evidence.

**Read first**

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §4 | Frozen. CRC mismatch is expressed in the existing vocabulary or it stops. |
| `ARCHITECTURE.md` §6 | #2, #4, #6, allocation-free steady state. |
| `ARCHITECTURE.md` §9 | The staleness ruling, the mutation row, the coincidence-class row. The last one is load-bearing tonight. |
| B1's session log | The 4,878/4,878 result, `KrakenFrame`'s lack of a text buffer, the truncation defect. |
| `harness/replay/NOTES-kraken.md` | The taxonomy, `NOT_A_CHECKSUM_GOLDEN`, the options named and not taken. |
| `docs/briefs/M4-triage-of-the-twelve.md` | B2's scope. |

**Depends on:** B1 ✅. **Blocks:** C, D.

---

## 1 · Verify the checksum

B1 measured that Kraken's CRC32 reproduces from integer ticks alone across 4,878 checks on the
four slices with an opening snapshot. So this is a comparison, not a parser.

- Compute after each applied update, compare against the frame's carried value.
- The fifth slice stays `NOT_A_CHECKSUM_GOLDEN` — mid-stream, 0/49, correctly ungradeable.
- **Confirm and record what the checksum actually covers.** If it is computed over a fixed top-N
  rather than the full subscribed depth, then a depth-25 book has levels the check cannot reach,
  and the panel renders rows the CRC never validated. That is a limitation to write down in
  `NOTES-kraken.md`, not a defect — but shipping it unstated would be.

## 2 · The mismatch path, and where the vocabulary has to hold

On mismatch the book is not trustworthy and the adapter resyncs: drop the book, resubscribe, wait
for a fresh snapshot.

- **Express it in the existing `GapReason`.** §4 is frozen and this is the third time the wire has
  seemed to want a new word; the answer has been no twice. If nothing existing fits, **stop and
  raise** rather than adding one.
- The venue-free resync predicate already covers what arrives next: a snapshot is a resync when a
  book event preceded it. Do not add a Kraken branch.
- Between drop and fresh snapshot the book is unknown — that is `Gap`, and the panel greys, which
  is honest. The liveness clock keeps running through it; a resync is not a liveness event.

## 3 · Two different things called resync, and only one is capturable

The unknown flagged when this brief was proposed resolves into two cases with different answers.

**(a) A mid-stream snapshot following book events — capturable, and cheaply.** The client
initiates the resubscribe, so the capture tool can provoke one deliberately mid-capture. The venue
really sends a mid-stream snapshot; nothing about the resulting trace is synthetic.

- Capture it, commit it, pin its taxonomy. This is the resync slice the triage has owed since
  stage A.
- Capture policy: **the window must contain both endpoints of the event it claims** — the
  `t+65..t+160` lesson, generalised. Fix `slice_trace`'s `--mode reconnect` to enforce containment
  or refuse, and settle its self-containment question here as the triage assigned.

**(b) A CRC mismatch caused by the wire — not capturable, and mandatory anyway.** A genuine
venue-side corruption cannot be provoked and may never appear in a capture. Per the
coincidence-class row, a captured trace records a venue behaving, so this case belongs to a
**synthetic frame** and the synthetic case is the instrument, not a substitute for one.

- A synthetic frame with a deliberately wrong checksum drives the full mismatch path.
- The mutation is run, not written: break the comparison and confirm red.

## 4 · The queue-vs-shed experiment — the measurement that can falsify a shipped number

`age_ms` at Kraken is a deficit interpreted as an age. That interpretation holds **only if Kraken
queues**. Anvil's queueing is measured and contractual; Kraken's is neither, and B1 pinned the
assumption naming this stage as its owner.

Induce backpressure by throttling the reader socket — the lever `_local/drain-120ms.ndjson`
already uses at Anvil. **Three outcomes, all of them findings:**

| Observed | Conclusion | Consequence |
| --- | --- | --- |
| Frames arrive late, checksums still pass | Kraken queues | The deficit is an age. `age_ms` stands at Kraken. |
| Checksums start failing | Kraken sheds | The deficit is loss, not age. `age_ms` at Kraken is a fiction and must be suppressed or redefined — **stop and raise**, do not patch. |
| The server disconnects us | Neither, at this throttle | Report it as the finding it is; a gentler throttle is a second sitting, not a failure. |

Be a good citizen: one pair, short window, gentlest throttle that produces a signal. This is a
public API and the experiment does not need to be aggressive to be conclusive.

**Cut point.** If sections 1–3 fill the evening, they are the evening. Section 4 is a clean second
sitting and its result changes a shipped number, so it deserves a clear head rather than the last
hour of one.

---

## Constraints

- §4 frozen: no new `FeedEvent` variant, no new `GapReason`. Stop and raise.
- §6 frozen. No dense-window book — that is C. No watchdog rewiring, no `staleness.hpp` deletion —
  those are D's first act.
- Goldens add rows; none move. Pins are add-rows-only.
- Synthetic cases are mandatory where an assumption and a venue's behaviour could coincide; a
  golden there is decoration.
- Run the code-review skill before proposing the split. Per-commit verification runs as part of
  executing the approved split, in a detached worktree with `CMAKE_HOME_DIRECTORY` confirmed.
- `cmake --workflow --preset host-mingw`, green. **Commit nothing.**

## Known unknowns — resolve and record

What the Kraken checksum is computed over, and therefore what it cannot detect. Whether a
throttled reader produces queueing, shedding or a disconnect. Whether a client-initiated
resubscribe produces a snapshot distinguishable on the wire from the on-connect one — if it is
not, the resync slice and a fresh capture are the same artefact and the predicate is doing all the
work.

## Definition of done

- [x] Checksum verified after each update across the four gradeable slices; the fifth still
      excluded with its reason; coverage of the checksum recorded.
- [x] Mismatch path expressed in existing `GapReason` — `ChecksumFail`, already in §4 since M0.
- [x] Synthetic bad-checksum frame drives the full path; comparison mutation run and red.
- [x] Resync slice captured, committed, pinned, with both endpoints inside the window.
- [x] `slice_trace --mode reconnect` enforces containment or refuses; self-containment settled.
- [x] *(Section 4)* Backpressure result recorded against the three-outcome table — **row one:
      Kraken queues**, so nothing to raise.
- [x] Green (32/32); code review run; split proposed; nothing committed.

## Out of scope

The dense-window book and the uninitialised-book state (C). `staleness.hpp`, `kRxWatchdogMs`, the
watchdog rewiring and its soak (D's first act). Panel rendering decisions (D). The client ping
(M6). Binance (M5). The runtime venue toggle (M7).

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     measured figures / evidence · exact next step. -->

### 2026-08-18 · Claude Opus 5 · sections 1–4 all done, including the cut-point one

**Done.** The checksum stops being carried and starts being checked.
`engine/kraken/kraken_checksum.hpp` is new — an incremental CRC-32/ISO-HDLC and the
tokenisation, both `constexpr`, allocation-free, and compiled for the ESP32-S3 by
`dc_engine_target_check` from the day it landed. `KrakenAdapter` verifies after every applied
book message; a mismatch raises `Gap{ChecksumFail}`, drops the ladder and latches
`resync_wanted()`. `harness/replay/kraken_minagbp_d25_resync_20260818.ndjson` is new — the resync
slice, captured deliberately. `tools/kraken_backpressure_probe.py` is new and settled the
queue-vs-shed question. **32 ctest targets green, up from 29; 335 doctest cases, 855,854
assertions.**

**Section 1 — the checksum verifies, in the shipping adapter.**

| slice | seen | matched | failed | unverifiable |
| --- | ---: | ---: | ---: | ---: |
| BTC/USD d10 / d25 / d100 | 839 / 1,537 / 2,472 | same | 0 | 0 |
| MINA/GBP d25 (16th) | 30 | 30 | 0 | 0 |
| MINA/GBP d25 (17th) | 49 | **0** | **0** | **49** |
| | | **4,878 / 4,878** | | |

B1's figure reproduced by a second implementation in a second language from a different
representation — Python over stored decimal *text*, C++ over scaled *integers* — neither derived
from the other. The fifth slice reads **49 unverifiable, not 49 failed**, which is the answer that
keeps `NOT_A_CHECKSUM_GOLDEN` honest: with no opening snapshot there is no book to compare, and an
adapter that compared anyway would grey the panel and blame the wire for a fact about the file.
The three outcomes are counted separately and `seen == matched + failed + unverifiable` is
asserted, so a fourth cannot appear quietly. `book_msgs_unchecksummed` is pinned at **0** on all
six slices — a wire fact, and the thing that stops "carry no checksum" becoming a silent opt-out.

**What the checksum covers, measured rather than inherited.** Stage 0 confirmed the top-10 *rule*
from 8,677 captured checksums, which is no evidence at all about the *blind spot* — every captured
message agrees with both readings, because the venue never sends a message whose only effect is
invisible to its own checksum. So the discriminating input was synthesised: **a book edited below
level 10 leaves the CRC unmoved; the same edit at level 10 moves it.** At the shipped depth of 25
the check validates **20 of the 50 levels the ladder holds**, so the panel's 54 rows carry 20
validated levels, 30 unvalidated ones and 4 that are empty by construction. Stated in
`kraken_checksum.hpp`, in `NOTES-kraken.md`, on `dc_ladder`'s report line and in ARCHITECTURE §9.

**Section 2 — no new vocabulary, and this was the fourth asking.** `GapReason::ChecksumFail` has
been in §4 since M0, written down for this venue before any of this code existed. The mismatch
path needed no new `GapReason` and no new `FeedEvent::Kind`; no second `Gap{Resync}` is emitted on
the way out, because §4's rule is that the book stays unknown until the next **Snapshot**, not
until the next Gap. **The one new thing is a latch, not a word:** `resync_wanted()`, which the
transport reads, acts on and clears — the same shape as `refused()`. It is deliberately NOT set by
`on_transport_gap`: a reconnect subscribes on its own, whereas after a CRC failure the socket is
healthy, the heartbeat keeps arriving, and **Kraken never re-snapshots unasked** — so without the
latch a client that detected corruption would sit grey over a live socket for ever. The cadence
stays with the transport; a storm cannot come from the deltas that follow, because dropping the
baseline makes them *unverifiable* rather than *failing*.

**Section 3(a) — the resync slice, and the question it answers.** Captured with a new
`capture_kraken.py --resubscribe-after`: one connection, one deliberate unsubscribe/re-subscribe,
a genuine mid-stream snapshot. **The two snapshots are byte-shape identical** — same `channel`,
same `type`, same `data[0]` keys, no marker of any kind — so a resync is detectable *only* by
position, the venue-free predicate is the only thing that could work, and a Kraken branch would
have had nothing to branch on. Committed on the quiet pair (99 records, 14 KiB, `snapshots=2
resyncs=1`, taxonomy pinned, cross-checked against an independent Python pass on every figure)
because a resync slice needs a wall-clock window long enough for two heartbeat calibrations, and
only a slow pair fits 72 s of that into 14 KiB. The busy equivalent — BTC/USD d25, 2,661 frames in
46 s, 636 KiB — ran the same path and stays in `_local/`.

**Section 3(b) — the synthetic mismatch drives the whole path**, with an *honest twin* carrying
the correct checksum as the control: without it, "mismatch detected" would pass equally against an
adapter that raised `Gap` on every update, and no captured slice could tell the difference.

**Section 4 — KRAKEN QUEUES, and `age_ms` stands.** One connection, BTC/USD d25, 80 s in three
phases, throttled with the 120 ms drain lever:

| phase | rate | checksums | lag worst | slope |
| --- | ---: | ---: | ---: | ---: |
| baseline | 19.47/s | 367/367 | −1.315 s | −0.000 s/s |
| throttled | 8.63/s | **331/331** | **26.017 s** | **+0.690 s/s** |
| release | **64.20/s** | 1,207/1,207 | 26.117 s | −1.411 s/s |

Nothing lost + everything late + a backlog to deliver **is** a queue. Row one of the brief's
three-outcome table; rows two and three did not occur, and the socket survived a 26 s backlog
without closing. **The arithmetic closes on itself:** the slope implies the venue was producing
~27.8 msg/s during the throttle, and the release volume independently implies ~27 — two routes,
neither derived from the other, agreeing to 3%. The B1 test that asserted this as an *assumption*
now asserts it as a *measurement*.

**Two defects found, both by widening the corpus rather than by review.**

1. **The parser filed every `method` frame as a subscribe ack.** An unsubscribe ack is
   structurally identical to a subscribe ack — no `channel`, no `type`, `method` + `result` +
   `success` — and the classification never read the method name. Benign at `success:true`; **one
   refused unsubscribe from latching `refused()`, which the firmware turns into `die()`.** No
   capture could have caught it: the healing path is the first thing this project has built that
   *sends* an unsubscribe, so before tonight no trace at any venue contained one.
2. **My own `--resubscribe-after` wrote its `tx` record at the top of the message loop**, stamping
   it after a message it then wrote behind it — `rx_ns` went backwards and `TraceReader` rejected
   the whole file. Found by replaying the first capture. A capture tool contains no reader.

Both are the same shape and it is a *third* one, distinct from a missing golden and a missing
synthetic case: **a corpus that has never contained a frame kind.** The rule: when a stage makes
the client send something new, capture what comes back before trusting the code that handles it.
72 seconds of capture against two defects.

**One thing the harness could not see, and now can.** An adapter-raised `Gap` greyed the book and
opened **no stale episode** — `episodes` was "watchdog episodes" by accident, because until
tonight every Gap came from the watchdog. `StaleEpisode`'s own comment calls it "the invariant-5
evidence the goldens assert on", and the resync slice's entire purpose is a grey window it would
have reported as none. Fixed in `replay_driver.cpp`; no committed golden moved, because no
committed slice contained an adapter-raised Gap — which is also exactly why nothing caught it.

**`slice_trace --mode reconnect` now enforces two rules or refuses, and the self-containment
question is settled.** (1) **Containment** — the window must hold both endpoints of the outage,
the `t+65..t+160` lesson generalised. (2) **Self-containment** — the pre-resync half must contain
the frame that BASELINES it, or its deltas are amendments to nothing and the slice shows a book
that was never live going not-grey. **Refuse rather than extend**, because this tool writes a
committed artefact and silently widening the window would make `--before` mean something other
than it says; the refusal prints the number to use. A deliberately mid-stream window stays
available as `--mode baseline --start`, which is how `..._20260817` was cut.

**And the predicate for rule 2 is `rebaselines`, not `is_snapshot`, which was a real trap.** At
Anvil a `book` frame is a full replace the adapter turns into `FeedEvent::Snapshot`, while only
`type:"snapshot"` is a snapshot *record* — and `anvil_101_reconnect.ndjson` has **no
`type:"snapshot"` at all before its reconnect one.** A self-containment rule written on
`is_snapshot` would have refused to cut the very trace the mode exists for. `slice_trace` also
gained `--selfcheck` (16 checks, in ctest); it had no test of any kind before tonight, and it is
the tool that writes committed artefacts.

**Mutation-verification — five run, five red, all reverted.**

| mutation | result |
| --- | ---: |
| the CRC comparison compares the frame with itself | **RED** 2 cases / 7 assertions |
| `kChecksumLevels` widened 10 → 25 | **RED** 6 cases / 28 assertions |
| asks and bids swapped in the payload | **RED** 8 cases / 41 assertions |
| the unsubscribe ack filed as a subscribe ack (pre-B2) | **RED** 2 cases / 14 assertions |
| adapter gaps open no stale episode (pre-B2) | **RED** 1 case / 1 assertion |

The second is the one worth keeping: it proves the committed corpus discriminates the **venue's**
top-10 rule and not merely our implementation of one.

**Found at review, fixed:** a multi-entry `data` message was tested with our entry LAST only, and
B2 made the checksum's provenance load-bearing — the reversed ordering is one `if` away and is now
covered; `Frame.rebaselines` had a defaulted `False` that would have made the new rule refuse every
capture from a long way off its cause; `min()` over a possibly-empty lag list in the probe; and the
digit-boundary property (9→10, 99→100) that the 8,172 real tokens never reach. Also noted and
**not** changed: `checksum_token` and `format_scaled(v, 0, …)` produce the same digits today and
stay separate on purpose — one is a wire contract, the other the display edge, and sharing them
would put a ladder-formatting decision on the path that decides whether the book is trustworthy.

**Documentation.** `NOTES-kraken.md` gains a B2 section (five findings, with the older "known
unknowns" list left standing and pointed forward). `ARCHITECTURE.md` §9 gains four rows.
`DESIGN.html`: status strip, strain 17's third residue closed, strain 22's slice count, and the
stale §06 queued edit marked as still-owed — **§03 still draws one of two adapters**, which is
recorded rather than quietly left. Also fixed there: **there were two strain cards numbered 22**
(stage A's reader card and B1's age-divergence card). The newer one is renumbered **23** and the
four briefs that meant it were retargeted.

**Verification.** `cmake --workflow --preset host-mingw` green, **32/32**.
`dc_engine_target_check` compiled `kraken_checksum.hpp` with xtensa GCC 8.4 under `-Werror
-fno-exceptions -fno-rtti`. Code review run against the diff. **Nothing committed.**

**Exact next step: the owner approves or amends the proposed split, then it is executed** — each
commit created and verified in a detached worktree with `CMAKE_HOME_DIRECTORY` confirmed to point
at the worktree, nothing pushed until every commit has been shown green in isolation. After that,
**stage C — the dense-window book**.

**One thing B2 did not do, and it is not a gap in B2.** The firmware does not yet act on
`resync_wanted()`: nothing in `firmware/` reads it, because the transport half is D's and this was
a desk-only evening. The adapter is honest without it — the book is dropped and the panel greys —
but the *recovery* needs the transport to unsubscribe and re-subscribe, and until D wires that, a
CRC failure on the board would grey permanently rather than heal. **That is a hand-off, stated
here so it is not discovered on a bench.** D also inherits the resubscribe cadence, which this
evening deliberately did not choose, and the 3,548 ms book-event hole a resubscribe costs.
