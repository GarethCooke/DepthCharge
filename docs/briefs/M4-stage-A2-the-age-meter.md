# M4 stage A2 — the age meter

**Track:** Agentic [A] · **One evening** · Derived from
[`M4-triage-of-the-twelve.md`](M4-triage-of-the-twelve.md), which is the work order this
expands: *"A2 — the age meter. Items 4, 5, 7, 8. §5 gains `age_ms`; the estimator is a
sliding-window deficit against the liveness signal; the header renders to minutes; the DoD
states the backlogged-socket gap as uncoverable rather than passing over it. Uses the
feeder-off Anvil trace and `drain-120ms`. No Kraken, no adapter, no capture. One evening,
unblocked now."*

The four items, in the triage's numbering:

| # | Item | Where it lands |
| --- | --- | --- |
| 4 | `age_ms` renders to at least minutes | `AgeText` + the console header |
| 5 | `age_ms` is a sliding-window deficit, not cumulative | `AgeEstimator`'s shape |
| 7 | `age_ms` is queuing lag — not time-since-frame, not time-since-change | the definition, pinned in the header comment and in §5 |
| 8 | the backlogged-socket coverage gap cannot be closed by a golden | this DoD, and a test that pins the limit |

---

## What this stage is for

The 2026-08-17 ruling took book silence away from the staleness clock — no threshold on it
can be correct, because a quiet market and a dead subscription are identical on the wire —
and said the panel should show the book's age **as a number** instead. This is that number.

The state it exists to make visible is one the object has never been able to draw: **live,
parsing perfectly, every counter healthy, and ninety seconds behind the market.** That
happened on 2026-08-11 and no instrument on the board could see it. Invariant #5 protects
against *stopped* and against *wrong*; it has no concept of *old*.

## Constraints

- §6 frozen. `engine/` gains two fields and no behaviour; no adapter, no `firmware/`
  change, no new capture, no Kraken.
- The number is rendered and **nothing branches on it** — an age that greys the panel would
  put a threshold back on a quantity the ruling removed one from.
- `cmake --workflow --preset host-mingw`, green.
- Nothing is committed until the `code-review` skill has been run against the diff and its
  findings are fixed. *(The owner authorised the commit after that review; the split and
  its verification are at the foot of this brief.)*

## Definition of done

- [x] §5 gains `age_ms`, with the definition written where the code is, not only in a log.
- [x] The estimator is a sliding-window deficit sized to catch stall-then-burst rather than
      average it away; the window's ceiling is stated, not discovered.
- [x] The header renders to minutes and hours and does not saturate.
- [x] The committed traces carry goldens: a healthy feed reads its noise floor and nothing
      greys; the reconnect trace peaks and then reports *no reading* until the new
      connection has measured the venue's clock for itself.
- [x] **The backlogged-socket case is stated as UNCOVERABLE, not covered.** Invariant #6
      cannot be satisfied for it by any golden, now or later: a backlog is a property of
      one client's socket rather than of the wire, and a capture's `rx_ns` records only
      when *that* client got the bytes. The 111 s figure this project owns exists only
      because two sockets were compared. What stands in its place is (i) synthesised
      arrival patterns whose true answer is known by construction, (ii) a test that pins
      the blind spot as a **known limit**, and (iii) a reported measurement over
      `_local/drain-120ms.ndjson` that is deliberately **not** committed as a golden.
- [x] Green; session log appended; reviewed, fixed, and committed as six.

---

## Session log

### 2026-08-17 · Claude Opus 5 (1M) · stage A2, one sitting

**24/24 green. Nothing committed. No `firmware/` change, no adapter, no new
capture, no Kraken.**

One thing needs reading before the rest, because it changed the design and it is
the only decision here that is not arithmetic.

**⚠ The obvious implementation — one median, shared with the grey threshold —
reads 0.0 s through a 111-second backlog.** The threshold clock's median is
*rolling*, and it must be: a feed that has permanently halved its rate should not
cry wolf for ever. But an age taken against an adapting median is blind to
exactly the failure it exists to catch. A socket throttled to a quarter of the
broadcast rate sees its rolling median settle at four times the interval within
one window, after which `elapsed − n × median` is **zero** — so the meter would
report a peak while the median adapted and then fall silently back to zero while
the true lag climbed to 111 s. That is the reading M3 stage E was built to make
impossible, arriving through the instrument meant to prevent it.

So the age is taken against a **baseline latched once per connection**, and the
justification is physical rather than a tuning choice: **a fresh socket's
server-side send queue is empty, so the cadence observed at the start of a
connection is the broadcast cadence.** It is the only moment a single client can
measure the venue's clock; after it, the queue is a confounder and every later
measurement is of the queue rather than of the venue. Nothing is hardcoded —
`PROTOCOL.md` §3.5's instruction to derive the cadence from the connection you
are using is satisfied per connection, and `kSummaryPeriodUs = 500000` in
`staleness.hpp` is exactly what this replaces.

---

### The definition, pinned before the code (item 7) ✅

Written into `age_estimator.hpp`'s header, `ARCHITECTURE.md` §5 and
`display_snapshot.hpp`, in the owner's words: **age is estimated queuing lag** —
not time since the last frame, not time since the book last changed; *a book that
has not changed is not old*; time since the last change is market information
that may be displayed but must never drive a rendered state.

The three coincide at Anvil-with-a-busy-feeder, which is why they were treated as
one for three milestones. Measured, they differ by orders of magnitude: time
since the last book **frame** is ~80 ms at Anvil always (12.6/s with the feeder
switched off), time since the book last **changed** is unbounded at both venues,
and queuing lag is zero until a socket backlogs and then 111 s.

### The estimator (item 5) ✅

`harness/include/dc_harness/age_estimator.hpp` — ESP-IDF-free and
allocation-free, beside `liveness_clock.hpp` and for the same reason: stage B
lifts it into `firmware/`.

**The shape:** the supremum of `(now − t_i) − n_i × baseline` over every suffix of
a 256-arrival window, clamped at zero. That is Lindley's recursion for a queue's
backlog, truncated to a window, and the three properties that matter fall out of
it rather than being coded in — it returns to zero when the socket catches up
(the suffix starting at the newest arrival always scores zero), it cannot ratchet
(a bias ages out instead of being carried for the life of the connection), and it
reports the peak of a stall while the burst is still arriving (the suffix that
starts before the stall is still in the window).

**Sliding rather than cumulative, for two measured reasons.** A cumulative
estimate never forgets a bias: one liveness frame that fails to parse — which
this project has measured at a chunk boundary — adds one interval to the reported
age **for ever**, and `staleness.hpp` can only ask the reader to cross-check a
counter. And it averages away **stall-then-burst**, which is the failure shape of
a venue that queues and never drops.

**The ceiling, stated rather than discovered.** The window holds the last 256
arrivals, so the largest age reportable is the lag accumulated *within* it: at a
drain fraction f the window spans `N × interval / f` and the lag accrued across it
is `(1 − f) × span`, which is exactly what the sup returns. The bound binds only
on a backlog older than the window — 128 s of history on a healthy Anvil socket,
384 s of reportable age at f = 0.25, 128 s at f = 0.5.

**A total silence has no ceiling at all, and that is a separate case worth
stating:** the window is over *arrivals*, so when nothing arrives it stops
refilling and `(now − t_i)` grows without bound. A2b's 176,000 ms — the largest
real fault on record — is that shape and would be reported in full. The ceiling
binds only on a sustained *partial* drain, where arrivals keep pushing the window
forward. Cost: 2 KiB of state and an O(256) scan per publish.

**What it costs at the boundary:** two fields on `DisplaySnapshot`, and
`sizeof` did **not** move — 1,168 B before and after, because `age_ms` fits the
4-byte hole after `version` and `has_age` fits the 2 bytes before `last_px`.
That is now a `static_assert`, because three documents quote a byte count derived
from it (`main.cpp`'s footprint block, `snapshot_channel.hpp`'s cost note, §9's
2026-08-07 row) and the next field added carelessly costs 3× its own size in the
mailbox while silently invalidating all three.

**Who stamps it:** the feed side, one line after `Book::publish`. `engine/` has no
clock and is not being given one — a book that reads a clock is a book whose
replay is not deterministic — so the book fills the market state and the same
single writer stamps how far behind it is (invariant #8, one line later).

### The header (item 4) ✅

`AgeText` renders `0.4s` · `59.9s` · `1m00s` · `2m56s` · `59m59s` · `1h00m` ·
`1193h02m`. The precedent is this project's own: `SecondsText` took uint32
microseconds, which is 71.6 minutes, and the 23.6 h soak printed `4294.9 s` for
eleven straight hours while the true age climbed past it. A seconds-only format
fails the same way more softly — `6821.4 s` is arithmetic a reader has to do at
the exact moment they are reading a line for a verdict.

The console header now carries ` age ` beside ` seq `, and **the age is a third
width driver** alongside seq digits and the gap reason: `-` is one column and
`1193h02m` is eight. Both of the other two overflowed the box before anyone swept
them, so the width test gained the axis.

`-` and not `0.0s` when there is no reading. "No reading yet" and "the book is
current" are different statements and exactly one of them is reassuring.

### What the committed traces say ✅

Goldens in `test_replay_goldens.cpp`, and what they can prove is narrower than it
looks — see the coverage gap below.

| trace | baseline | worst age | grey episodes |
| --- | ---: | ---: | ---: |
| `anvil_101_baseline` (M1) | 500.6 ms | **0.9 s** | 0 |
| `anvil_101_baseline_20260809` | 500.0 ms | 0.5 s | 0 |
| `anvil_101_depth27_20260816` | 500.0 ms | 0.6 s | 0 |
| `anvil_101_feederoff_20260817` | 500.0 ms | 0.5 s | 0 |
| `anvil_101_reconnect` | 499.3 ms | **2.3 s** | 1 |

**The healthy floor is one interval, and the M1 trace's 0.9 s is not noise — it is
real and worth knowing.** One interval is the instrument's resolution (the
elapsed term grows between arrivals while the delivered term does not). The rest
is Anvil's occasional slipped `summary` tick, which is **never repaid**: a fixed
engine deadline does not run fast afterwards to catch up, so a slip stays in the
window until it ages out. From one socket a late broadcast and a late delivery
are the same observation — the ruling's own point 7 — so calling it lag is
correct rather than generous.

**The reconnect trace is the separation that makes the number worth printing:**
2.3 s against a 0.5–0.9 s floor, and then *no reading at all* until the new
connection has re-measured the venue's clock. The grey frame itself still carries
the age the book had reached when the watchdog fired; everything after it reads
"no reading yet", because the backlog died with the socket and an estimate
carried across a reconnect would be measuring a queue that no longer exists.

### The coverage gap (item 8) — stated, and pinned as a limit ✅

**Invariant #6 cannot be satisfied here by a golden, now or ever.** A backlog is a
property of one client's socket rather than of the wire: two sockets on the same
server at the same instant disagree about the book's age, and a capture's `rx_ns`
records only when *this* client got the bytes. The 111 s figure exists only
because `anvil_freshness_probe.py` seq-matched a throttled socket against an
unthrottled one — two sockets, which a trace file is not.

**And the gap has a hard edge, which is new here: from one socket the case is not
identifiable at all.** *"The venue broadcasts at 2 Hz and I am two minutes
behind"* and *"the venue broadcasts at 0.5 Hz and I am current"* produce
byte-identical wire behaviour. No estimator separates them; this is a proof, not
a limitation of the implementation.

**Measured on the one real backlogged capture, and reported rather than pinned.**
`_local/drain-120ms.ndjson` (2026-08-09, capture tool sleeping 120 ms per
message): 236 summaries over 239.9 s is **49% of Anvil's 2 Hz broadcast**, so the
true lag at the end of that run is near **122 s**. The meter latches its baseline
at **969 ms** — the throttled cadence, because the socket was behind before its
first sample — and reports **12.5 s**, a 10× under-read. That is the blind spot,
on real bytes, and it is exactly the shape the client ping closes.

Three things follow, all deliberate:

1. **No slice of it is committed.** A golden here would pin a known-wrong number
   and read as coverage; the triage says the case must be stated rather than
   made to look covered. (It is also 13.5 MB, and a 60 s slice at pre-A7 frame
   sizes is ~4 MB against a repo whose largest committed trace is 200 KiB.)
2. **The limit is a test instead** — `test_age_estimator.cpp` drives a
   throttled-from-birth socket and asserts the meter reads zero, so a future
   reader meets the blind spot as a pinned property rather than discovering it as
   a bug, and the case for the client ping rests on an assertion rather than a
   paragraph.
3. **The one visible symptom is the baseline itself**, which is why it is printed
   beside every age: a connection that latches 969 ms where its predecessor
   latched 500 ms has told a human what it cannot work out for itself.

Bounding the damage, honestly: server-side this cannot happen — a queue that has
not been created yet cannot be full — so what is undetectable is a **client** too
slow to drain the wire from birth. The board's own RX loop was instrumented for
exactly that across the 23.6 h soak and exonerated (`wait 0 / read 98–99 / feed 0`
in every hour).

### What stage B inherits from this stage

1. **`firmware/src/staleness.hpp` is superseded, not extended.** Its cumulative
   estimator and its hardcoded `kSummaryPeriodUs = 500000` are both things the
   ruling forbids. Until stage B lifts `age_estimator.hpp` across, **the host and
   the target compute the book's age two different ways** — recorded in DESIGN
   §08 rather than left to be discovered at the bench. `liveness_clock.hpp` moves
   in the same trip.
2. **The panel header has no room for the age today** — 64 px, and the value slot
   is already the last price or the stale reason. Where the age goes on the panel
   is a stage D decision at desk distance, exactly like item 11's rendering.
3. **The premise is unproven at Kraken.** A deficit is an age only if the venue
   **queues**; if it sheds, the identical deficit appears and there is no lag at
   all. Anvil's queueing is measured and now contractual (`PROTOCOL.md` §4).
   Kraken's is neither, and the driver still refuses Kraken traces, so stage B
   gets the first look — and must not inherit the assumption silently.

### The review pass (2026-08-17, same sitting) — one defect, one measurement, three DRY fixes

The `code-review` skill was run against the working tree before anything was
committed. **It found a correctness defect in the design decision above, and
fixing it uncovered a second one that only a measurement could have caught.**

**1 · The baseline inherited the DEAD connection's cadence.** The first
implementation latched from `LivenessClock`'s rolling median. That clock is
deliberately *not* reset at a disconnect — cadence is a property of the venue, not
of the socket — so a reconnect away from a backlogged socket would hand the fresh,
healthy connection the *throttled* interval, and it would then report no lag for
ever. The reset three lines above it was doing nothing. **Fixed by making the
estimator measure its own baseline** from the connection's own first intervals,
which also removed a parameter and the ordering constraint that came with it.
Pinned by *"the baseline does not survive the connection that measured it"*.

**2 · Then the reconnect golden moved, and the reason was worth the trip.** With
the estimator measuring for itself, the trace's post-gap baseline came out at
**478.0 ms** against a 500 ms broadcast, and the healthy end-of-trace reading rose
to **2.8 s**. A stream *resumes with a burst*: the first intervals after the
socket comes back are `253 871 174 475 480 527 478 …` ms — the queue flushing,
plus a connect landing at a uniformly random phase. Lower medians of the first n:
**n=8 → 478.0, n=16 → 495.6, n=32 → 499.3**, against a whole-trace 499.9.

**A low baseline manufactures age at a constant rate, for ever** — 4.4% low is
4.4 s of phantom lag per 100 s, on a socket that is exactly current. So the latch
window is now a full 32 intervals (`kBaselineSamples`), and **the two gates
deliberately differ**: the threshold calibrates on 8 because greying early is
safe, the baseline waits for 32 because guessing the venue's clock early is not.
Cost: the meter reads `-` for the first 16 s of an Anvil connection. No reading is
a smaller claim than a wrong one.

**Every figure in this brief moved with it, for the better:** the healthy floor
tightened from 0.5–1.2 s to **0.5–0.9 s**, and the reconnect trace's end-of-trace
reading went from 2.8 s to **0.2 s** — a healthy connection now reads healthy.
The `drain-120ms` figures did not move (a uniformly throttled socket has the same
median at 8 and at 32), so the blind-spot measurement stands as written.

**3 · DRY.** The two instruments carried the same three lines of ring arithmetic
and, after the fix, would have carried two medians as well. Both now come from
`dc_harness/sample_window.hpp` — `SampleRing<T,N>` and one `lower_median`, with
the nearest-rank convention documented once, so a C++ figure stays comparable with
`gap_stats.py`'s. `sample()` became `read_and_bank()` (the pair `read`/`sample`
did not say which one moved the high-water mark), `clamp_ms` lost its repeated
literal, and the goldens' four spellings of the trace directory became one
`trace_path()`.

**4 · Property tests, which the review methodology asks for and this code wanted.**
The estimator is a pure function of (arrivals, baseline, now), so the scenario
cases were joined by 250 randomised patterns from fixed seeds, asserting the
invariants rather than remembered numbers: never negative and never older than the
window's own span; monotone in time while nothing arrives; **an arrival can never
make the book look older** (the property that breaks first if the suffix scan or
the ring indexing is wrong); the baseline is always this connection's own first
window; and a feed held at whatever cadence it measured for itself reads zero at
*any* interval, not just at 500 ms.

Not fixed, and named instead: `AgeText` duplicates `SecondsText`'s job
(firmware/src/staleness.hpp). That file is superseded by this stage and stage B
deletes it — merging them now would mean writing the same code twice.

### The commit split, as executed

Six commits, ordered so the type exists before the code that fills it and the
goldens land with the behaviour they pin.

| # | SHA | Files | Message |
| --- | --- | --- | --- |
| **1** | `52af6e0` | `engine/include/depthcharge/display_snapshot.hpp` | `engine: DisplaySnapshot carries the book's age, in padding it already had` |
| **2** | `98957be` | `harness/include/dc_harness/sample_window.hpp`, `liveness_clock.hpp` | `harness: one ring and one median rule, shared by the two clocks` |
| **3** | `abb25ab` | `harness/include/dc_harness/{age_estimator,replay_driver}.hpp`, `harness/src/{replay_driver,console_ladder,dc_ladder_main}.cpp` | `harness: the age meter -- a windowed deficit against a per-connection baseline` |
| **4** | `9b7c675` | `harness/tests/{test_age_estimator,test_replay_goldens,test_console_ladder}.cpp`, `CMakeLists.txt` | `harness: pin the age arithmetic, the healthy floor, and the blind spot` |
| **5** | `916c53a` | `harness/src/{trace_report,dc_replay_main}.cpp`, `tools/gap_stats.py` | `tools: BOOK AGE was not the age -- rename the row to BOOK SILENCE` |
| **6** | `91c7a8d` | `ARCHITECTURE.md`, `ROADMAP.md`, `README.md`, `docs/DESIGN.html`, the two briefs, `harness/replay/NOTES.md` | `docs: M4 triage and stage A2 -- the age meter, and what it cannot see` |

**`CMakeLists.txt` moved from 3 to 4 during the split, and the reason is the
ordering rule itself:** it adds `test_age_estimator.cpp` to `dc_tests`, so a
commit that carried the CMake entry without the file would not build. The two
travel together.

**"Green at every commit" was CHECKED rather than asserted** — 1 through 5 built
and tested in a detached worktree of their own (**24/24 at each**), because the
working tree passing at the end says nothing about the five states before it.
Commit 6 is documentation only, so its build is commit 5's.

**The first attempt at that check was itself wrong, and it is the more useful half
of this note.** It ran `cmake --workflow` in the main directory while checking out
commits in the worktree, so it rebuilt HEAD three times and reported three passes
that meant nothing. A verification that does not touch the thing it claims to
verify is this milestone's own recurring defect — *a measurement that answers a
nearby question, read as answering the one that mattered* — and it is the same
shape as a Kraken golden produced by the Anvil parser, which is the trap stage B
inherits. The rerun was confirmed to be real before its result was believed:
`CMAKE_HOME_DIRECTORY` in the worktree's cache points at the worktree, not at the
repository.

**Why 2 is separate from 3.** It is a pure extraction: `LivenessClock` keeps every
number it had, and a reviewer should be able to check that claim without the new
estimator in the same diff.

**Why 4 is separate from 3.** The tests are the only place the blind spot is stated as an
executable claim, and a reviewer should be able to read what is asserted without the
implementation moving underneath — particularly the case that asserts the meter reads
**zero** when it is wrong, which looks like a bug until the comment above it is read.

**Why 5 is separate from everything.** It is a **rename with no numeric change**, and that
is the claim to check: the figures in `gap_stats.py`'s output and in the Kraken report are
identical before and after (verified — quiet pair LIVENESS median 1000.6 / worst 1026.2,
book silence worst 9006.8, 0 episodes at 4003 ms). Folded into a commit that also changes
behaviour, "no number moved" would be unreviewable. The Anvil report is untouched, because
stage A gave the two venues separate reports.

**One thing a reviewer should check that the diffs will not show:** `git status` must show
**nothing under `firmware/`** and **no trace modified** — verified, and the only `engine/`
change is commit 1's two fields. The target half of invariant #1 was also checked by hand
with the real xtensa GCC 8.4 (`toolchain-xtensa-esp32s3@8.4.0+2021r2-patch5`), because the
`sizeof` pin is a claim about a **32-bit** layout and the host cannot prove it:
`display_snapshot.hpp` and `anvil_frame_streaming.cpp` both compile clean at `-Os -Werror`,
so the 1,168 B holds on the board as well as the desk.

### Exact next step

**B1 — the adapter.** Unchanged by this stage: parse, subscribe-ack handling
including *absence of a subscribe is not failure of a subscribe*, deltas onto the
phase-1 book, `kRxWatchdogMs` deleted rather than re-derived, and `symbol_for()` /
`console_ladder`'s hardcoded `" ANVIL "` fixed in the same sitting, because that
is the evening they break. The two items with teeth are still the dispatch
proving it dispatched right, and the resync slice that does not exist yet.
