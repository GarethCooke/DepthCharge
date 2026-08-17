# M4 stage A — the staleness ruling, and the re-derivation it forces

**Supersedes `M4-stage-A-rulings.md` and `M4-ruling-2-anvil-summary.md` entirely. This file is the
only work order; ignore both of those if they are in the tree.**

**Track:** Agentic [A] · **Two evenings, split as marked** · No adapter, no `engine/` change, no
`firmware/` change, commit nothing.

Stage A is accepted and green. Task 2's stop line fired correctly and nothing was tuned. The Anvil
read-and-report has come back. What follows is the ruling, the re-derivation it forces, and three
things that were outstanding.

**Evening 1 — Tasks A, C, E. No code changes to the reader or driver.**
**Evening 2 — Tasks B, D.**

---

## Task A — the §9 amendment, before any code

Append to `ARCHITECTURE.md` §9, dated today, marked as **superseding decision 2's book-event
clause of 2026-08-17**. Decision 2's split of transport liveness from book staleness stands; what
changes is what the staleness clock counts. Preserve the superseded wording and add a supersession
pointer to it — do not delete it.

> **1. Book-event silence cannot be a staleness signal at any venue, and no threshold on it can be
> correct.** A quiet market and a silently dead subscription produce identical wire behaviour, so
> book silence carries no information about whether the displayed book is trustworthy. At
> MINA/GBP a 25,843 ms healthy silence is a book that is exactly right, and greying it asserts the
> opposite. The bound on book silence is a market property, not a protocol one, and a market can
> be closed.
>
> **2. Staleness counts the venue's declared liveness signal.** Kraken's heartbeat: 600 in 600.2 s,
> worst record gap 1,119 ms, cadence held at 936–1,042 ms inside the 25.8 s book hole, and every
> worst record gap in stage 0's four slices within 27 ms of one second at a different hour.
> Anvil's `summary`: a fixed engine-thread deadline that fires on an empty queue, order flow
> changing contents and never timing; 2 Hz measured across 60 s idle with the feeder never
> started, and across 31 s idle with the feeder stopped mid-run — 62 idle frames byte-identical
> with `seq` advancing; nominal 500 ms, worst 547 ms.
>
> **3. The threshold is measured at runtime, not declared.** Anvil's interval is `ANVIL_SUMMARY_HZ`,
> operator config, not discoverable by a client; Kraken's is a protocol constant DepthCharge also
> does not control. A hardcoded threshold is therefore coupled to a value the client cannot read
> back and breaks silently when it changes. The threshold is a multiple of the observed
> inter-arrival median of the liveness signal itself.
>
> **4. Book-event silence becomes `age_ms`** — rendered as a number in the header, never as grey.
> Decision 4 was taken on the grounds that *old* and *stopped* must be separable states; this is
> that separation becoming load-bearing rather than anticipated.
>
> **5. A healthy silence beats a threshold; a faulty silence is what the threshold is for.**
> MINA/GBP's 25,843 ms was a healthy feed, so greying would have been a false statement. Anvil's
> unexplained 176,000 ms WS silence of 16 Aug (`docs/anvil-plan.md` A2b, unreproduced, HTTPS
> unaffected, both internal hypotheses would silence `summary` too) was a fault, so greying is a
> true one. The two are indistinguishable in a distribution and opposite in meaning; only the
> question *was the feed healthy?* separates them.
>
> **6. Accepted cost, in its general form.** A connection-level liveness signal does not prove the
> book subscription is alive. Anvil's `summary` is global fan-out delivered to every socket
> regardless of which ticker it subscribes to; Kraken's heartbeat is connection-level. Neither
> proves the book. This trades an observed false-grey — three per ten minutes on a healthy Kraken
> feed — for an unobserved false-colour. Stage B's CRC covers the case where updates arrive and
> disagree; it does not cover silence.
>
> **7. Cadence is not freshness.** Anvil queues and never drops per socket — no cap, no drop rule,
> no per-socket coalescing — so a backlogged socket receives every frame late rather than losing
> any: 111 s of lag over 150 s, measured. Arrival proves the server was alive when the frame was
> generated, not that the frame is fresh.
>
> **8. The vendored contract currently denies the behaviour relied on.** Anvil's PROTOCOL.md states
> that a genuinely idle book produces no frames and that a client must not treat a quiet socket as
> dead. That clause is false as implemented, and the snapshot in `docs/vendor/` carries it. Until
> Anvil corrects it, DepthCharge relies on measured behaviour its own vendored contract denies.
>
> **Rule for the arrangement, not the number:** measuring a market property and measuring a
> protocol property look identical until the sample widens. Ask which of the two a constant is
> made of before declaring it.

Then mark the vendored snapshot as **pending refresh** wherever `docs/vendor/` is described, and
add one line to ROADMAP's M4 row.

## Task C — capture the trace nothing currently covers *(do this first if the evening is short)*

Every committed Anvil trace was captured against a synthetic feeder that is never quiet, so no
golden exercises the state this ruling creates: **book age growing while liveness holds.** Idle
emission is now known-good, so this is fifteen minutes.

1. Local Anvil server, feeder off or stopped mid-run, one client, **120 s**, three tickers.
2. Commit a slice under the existing trace conventions, `venue: anvil`, taxonomy pinned in
   `dc_taxonomy` with the add-rows-only rule.
3. It is the Anvil twin of the MINA/GBP case and the golden that proves the panel stays coloured
   with a book that has not moved. Say so in `harness/replay/NOTES.md`.

## Task E — `firmware/src/ladder_font.hpp` provenance *(outstanding, unrun)*

Leaving it alone was right. **Do not revert it, and do not commit it with anything else.**
Establish provenance and stop. Ranked, with the check for each:

1. **A bench-session edit arriving through the repo sync.** Sync clients restamp mtime, so an M3
   edit made at the panel presents as touched today. Check the sync client's mtime behaviour and
   whether another clone holds it committed. Leading explanation — the edit centres the decimal
   point across two columns instead of hanging it in the last one, which is what someone looking
   at a rendered panel fixes.
2. **A generator wrote it.** A tool regenerating the header appears as a Bash call, not an Edit or
   Write — the 15-transcript sweep searched for the wrong verb. Grep `tools/`, CMake and the
   PlatformIO config for anything writing `ladder_font.hpp`; re-sweep for Bash calls touching
   `firmware/`.
3. **A change older than its mtime.** `git log -p -- firmware/src/ladder_font.hpp`,
   `git stash list`, `git reflog`, sibling mtimes.

If it proves to be the owner's, it earns its own commit and a line in the M3 log or stage A2's.

---

## Task B — the re-derivation *(evening 2)*

1. **Self-calibrating threshold, replacing `stale_gap_ms` as a per-venue constant.**
   - Rolling median of liveness-signal inter-arrival — Anvil's `summary`, Kraken's heartbeat.
   - Threshold is `k × median`. Derive `k` once from both venues' healthy distributions (Anvil
     nominal 500 / worst 547 across three runs; Kraken worst 1,119 across two hours), state it as
     one number in `venue.hpp` with its provenance in a comment.
   - Until `N` samples land, use a **generous default** so that **no new rendered state is
     required** — no *calibrating* state, no §4 or §5 change. Anvil delivers snapshot and summary
     immediately on connect, so calibration starts at t=0.
   - State a floor and a ceiling, so a pathological median cannot yield a threshold of zero or one
     that never fires.
   - Upstream coalescing is global: if the broadcaster lags the engine, intermediate rosters are
     skipped for everybody and a socket sees a **lower rate rather than a gap**. The median must
     therefore survive a sustained rate change, not only spikes. One line in the code saying how.
2. **`venue_traits` carries the liveness signal's *name*, not its interval.** That is a wire fact.
   Rename the field so a future reader cannot make the old mistake from the old name, and say in
   `venue.hpp` what the value is made of.
3. **`ReplayOptions::disconnect_gap_ms` follows the same rule.** Re-run all nine traces: the quiet
   pair must synthesise **zero** disconnects, `anvil_101_reconnect` must still produce its **one**.
   If either moves, stop and report.
4. **`gap_stats.py` and its assertion.** The `> 9007 * 1.5` assertion is premised on a beaten
   figure and on the wrong quantity. It now measures two distributions — book age and liveness gap
   — printed under labels that cannot be confused. Re-derive and re-pin.
5. **Write-ups.** `NOTES-kraken.md`: the ruling above the three options not taken, saying which of
   the three it is or that it is a fourth. `NOTES.md`: the Anvil `summary` distribution, the idle
   measurements, the renamed field.
6. **`kRxWatchdogMs` still untouched.** The firmware half is stage B's.

## Task D — write down what stage B and A2 inherit *(evening 2)*

Under the existing **Owed by stage B** heading in the stage A session log, and in
`docs/DESIGN.html` §08 where strain 22 already lives:

1. **`age_ms` is a sliding-window deficit, not a cumulative one.** Anvil queues and never drops, so
   a cumulative expected-versus-received count reads **zero** through a 111 s backlog while the
   rendered book is nearly two minutes old. The window is sized to catch stall-then-burst rather
   than average it away. This is a constraint on stage A2's code, arriving before it is written.
2. **Freshness needs the client ping.** Cadence proves generation-time liveness only. Crow answers
   client pings unconditionally with the pong appended to the same per-connection buffer as data —
   so a pong arriving behind a backlog measures the backlog, which is the property wanted.
3. Carry forward, unchanged: the Kraken-golden-from-the-Anvil-parser mutation test; the
   deliberately captured resync slice that is not one of the four truncation traces; stage D's
   criterion (*the quiet pair holds colour through 26 s of legitimate book silence and greys within
   the liveness threshold of the heartbeat stopping*); and `age_ms` rendering to at least minutes.
4. One line for the DepthCharge README's *why this exists*: had the panel existed on 16 Aug it
   would have shown A2b's incident for two minutes fifty-six seconds. Not a brief item.

---

## Constraints

- §6 frozen. No `engine/`, no `firmware/`, no adapter, no `FeedEvent` change. Pins add rows only;
  `--pin` still refuses to overwrite.
- The amendment is written **before** any code, in the wording above, superseded text preserved.
- Do not reopen: depth 25 / `kDisplayLevels` 27, the `static_assert`s, one venue per build, the §4
  non-change, the venue-free resync predicate.
- Do not build the defensive resubscribe. Name it in `NOTES-kraken.md` as an open option for stage
  B, with its cost.
- `cmake --workflow --preset host-mingw`, green at the end of each evening.
- **Commit nothing.**

## Definition of done

- [x] §9 amendment appended in the wording above; decision 2 carries a supersession pointer;
      superseded text intact; vendor snapshot marked pending refresh; ROADMAP line.
- [x] Feeder-off Anvil trace captured, committed, taxonomy pinned, purpose stated in `NOTES.md`.
- [x] `ladder_font.hpp` provenance reported; working tree unchanged.
- [x] Threshold self-calibrating; `k`, floor, ceiling and their provenance in `venue.hpp`; no new
      rendered state.
- [x] `venue_traits` renamed to carry the signal's name; both language tables agree.
- [x] Nine traces re-run: quiet pair zero synthesised disconnects, `anvil_101_reconnect` one.
- [x] `gap_stats.py` prints both distributions under unconfusable labels; assertion re-derived and
      re-pinned.
- [x] Both NOTES updated; resubscribe named not built.
- [x] **Owed by stage B** carries the windowed-`age_ms` constraint, the ping-for-freshness note,
      and the three carried-forward items.
- [x] Green, nothing committed, session log appended.

---

## Session log

### 2026-08-17 · Claude Opus 5 (1M) · Tasks A, C, E, B, D — all five, one sitting

**Tree green at 23/23, nothing committed, nothing in `engine/`, nothing in
`firmware/`, `kRxWatchdogMs` untouched, no golden moved, all eight pre-existing
committed traces byte-identical.**

Two things need reading before the rest.

**⚠ Task C's premise did not survive contact, and the trace proves something
else.** The feeder-off capture was specified as "the Anvil twin of the MINA/GBP
case" — book age growing while liveness holds. **It cannot be**, and the reason
is a wire fact nobody had measured: an idle Anvil emits `book` frames at
**12.6/s**. There is no book silence at Anvil to rescue. The trace is committed
and pinned because what it *does* prove is load-bearing, but it is described
accurately in `NOTES.md` rather than as the twin. Detail below.

**✅ Task E is settled, and the answer moved while I was looking at it.** HEAD is
no longer `9e2b495`. It is **`a6b395a` "changed representation of the decimal
point", authored and committed by GarethCooke at 17:47:55**, containing exactly
the edit the earlier session flagged. Nothing automated wrote it; VS Code local
history proves the keystrokes. The working tree is dirty with a *third* variant
running the *opposite* way. Detail below.

---

### Task A — the §9 amendment, before any code ✅

Appended to `ARCHITECTURE.md` §9 dated 2026-08-17 (M4 stage A, ruling), in the
owner's wording, transposed into the table's date · change · why shape. Points
1–4 are the change; 5–8 and the closing rule are the why.

Decision 2's row carries a **supersession pointer** as its first sentence — the
book-event clause superseded, the two-clock split explicitly still standing — and
its text is **preserved unedited below the pointer**, per the table's own rule.

`docs/vendor/anvil-protocol.md` marked **pending refresh** in five places: its own
DepthCharge header block (a `*** PENDING REFRESH ***` section quoting the false
clause and naming the measurement that falsifies it), plus the four places the
snapshot is *described* — `ARCHITECTURE.md` §1 and the §3 layout table,
`CLAUDE.md` Boundaries, `README.md` layout. **The snapshot body is untouched by
one byte**: a vendored file that has been edited stops being a record of what the
other side said, so the annotation lives only in our own header. ROADMAP M4 row
gained the ruling line.

---

### Task C — the trace nothing covers ✅ committed, ⚠ but not the trace that was ordered

**Captured.** Local Anvil (`build-msvc`, MSVC 19.44, Boost 1.86, Crow 1.2.1),
`ANVIL_FEEDER=0`, `ANVIL_TICKERS=101,102,103`, `ANVIL_SUMMARY_HZ=2`, one client at
`?ticker=101&depth=27`, **120 s**, no order in existence at any point.
`anvil_101_feederoff_20260817.ndjson` — 1,753 records, 200 KiB raw, **13.0 KiB
gzipped**. Taxonomy pinned; `dc_replay_feederoff` and `dc_ladder_feederoff` added
to ctest.

**What it measures.**

| kind | count | rate | inter-arrival |
| --- | ---: | ---: | --- |
| `summary` | 241 | **2.008 /s** | min 125.0 · **p50 500.0** · p90 515.0 · max 531.0 ms |
| `book` | 1,512 | **12.597 /s** | p50 78.0 · max 157.0 ms |
| `snapshot` | 1 | on connect | — |
| `trade` | **0** | — | the only committed trace with none |

**The ruling's load-bearing premise is confirmed and now reproducible from a
committed file:** `summary` fires at 2 Hz on an empty queue, all 241 frames
byte-identical apart from `seq` (895 → 6,280). Nothing in the capture could have
provoked it, because nothing happened.

**And the thing nobody had measured: an idle Anvil also emits `book`, at
12.6/s** — 1,511 frames over an empty book, byte-identical apart from `seq`, zero
levels. Mechanism read out of Anvil's source rather than inferred:
`publish_books()` (`server/engine_harness.hpp:240`) stamps `snap->seq = ++seq_`
**unconditionally**, so `Broadcaster::emit_books()`'s skip
(`if (snap->seq == ls) continue;`, `broadcaster.hpp:187`) **can never fire on a
live server**.

Three consequences:

1. **Task C's stated purpose is not achieved and structurally cannot be by this
   capture.** At Kraken the book goes silent for 25.8 s and the heartbeat is what
   keeps the panel honest. At Anvil there is no book silence — worst inter-arrival
   157 ms — so the panel stays coloured because book events keep coming, not
   because liveness rescued it. `dc_ladder` reports `0 stale episode(s)` and
   `no book · ● LIVE`, which is right and is **not the new rule being exercised**.
   The Anvil shape of "age grows while liveness holds" is a **backlogged socket**,
   not an idle server — `_local/drain-120ms.ndjson` is the existing candidate, and
   the obstacle is that age needs a reference clock, which is what
   `anvil_freshness_probe.py` gets by seq-matching two sockets. Named in `NOTES.md`
   so the next session does not re-capture an idle server expecting a different
   answer.
2. **`docs/vendor/anvil-protocol.md` §4 is false twice over, not once.** "A
   genuinely idle book produces no frames" — a genuinely idle book produced
   **1,753 frames in 120 seconds**.
3. **A note offered to Anvil's A2b, not asserted.** A2b reasons that *"`emit_books`
   deliberately skips a ticker whose seq has not moved, so the wire goes silent"*.
   The skip is dead code against a running publisher, so an unchanged book is not
   a route to silence. This **strengthens** the hypothesis rather than weakening
   it: both deadlines are on one engine thread, so a stall silences `book` **and**
   `summary` together — which is the total-silence signature A2b describes, and
   exactly why the ruling can say both internal hypotheses would silence `summary`.

**What it IS good for**, and why it is committed: it pins the ruling's premise
against regression; it is the only committed trace with an **empty book end to
end**, so it is the first thing to exercise parse → adapt → adopt → publish →
draw with zero levels; and it is the only one with `trade=0`. The distinction it
guards is one no other trace tests — **"no orders exist" is a live, correct, empty
ladder; "no data arrived" is grey.**

*Limit, stated: this is a LOCAL server, not the deployed one. Same defaults, but a
VPS under load is not a desk.*

---

### Task E — `ladder_font.hpp` provenance ✅ settled; working tree untouched

Three ranked hypotheses run in parallel, read-only, then verified first-hand.

**All three: NOT SUPPORTED — and the question dissolved instead.**

| what | evidence |
| --- | --- |
| **HEAD moved mid-session** | `a6b395a` "changed representation of the decimal point", **GarethCooke <gareth_cooke@hotmail.com>, 17:47:55 +0100**, containing exactly the flagged edit. `git show --stat`: **one file, nothing of mine**. Index clean. |
| **VS Code wrote it, at the keyboard** | `…\Code\User\History\-3c4c2945\entries.json` — **seven saves** of this file today, 17:27:14 → 17:46:45. `git hash-object` on two of them matches git exactly: `8Atn.hpp` = `eddaed7` = the committed blob; `9yjx.hpp` = `083272e` = the current working tree. |
| **the dirty diff is a THIRD variant, running the other way** | `{{0,0,6,6,0,0}}` → `{{0,0,0,0,6,6}}`, comment byte-identical on both sides — the decimal point moved back *down* to the baseline. The earlier session's description was of the *committed* edit, not this one. |
| **why the tree is dirty** | index == HEAD == the 17:28:49 blob; the file was edited again at 17:46:45 and committed 70 s later **without `-a`**, so the commit captured the stale staged version. |
| **no automation** | zero font/glyph tooling in the repo (one `*font*` file — this one); CMake's only `file(WRITE)` targets the build dir over `engine/include`; no PlatformIO `extra_scripts`; no git filter. Agent side: **540 transcript files / 67 MB** swept, 718 tool calls parsed including Bash/PowerShell — all 26 `ladder_font` mentions read-only. |
| **not synced** | repo is at `C:\Development`, outside every sync root; no reparse points; `a6b395a` is **unpushed** (`origin/master` still `9e2b495`), so it was made here. |

**Left completely untouched, as instructed.** For the owner: the working-tree
version is the one that was **compiled at 17:47:04** — the firmware build consumed
it 19 s after the save and 70 s before the commit — so `a6b395a` captured a glyph
that was already superseded on disk. It earns its own commit, and the earlier
session's note in the review-response brief should be read as describing the
committed edit rather than the current one.

---

### Task B — the re-derivation ✅

**1 · Self-calibrating threshold.** `harness/include/dc_harness/liveness_clock.hpp` —
a fixed-size ring, no heap, ESP-IDF-free so stage B can lift it. Rolling median
of the liveness signal's inter-arrival over a 32-sample window; threshold =
`k × median`, clamped.

**`k = 4`, and what it is made of** — the worst *healthy* inter-arrival as a
multiple of that venue's own median, so the constant is dimensionless and
transfers:

| venue | signal | intervals | median | worst healthy | worst/median |
| --- | --- | ---: | ---: | ---: | ---: |
| Anvil | `summary` | 1,191 | 500.0 ms | 968.8 ms | **1.937×** |
| Kraken | `heartbeat` | 834 | 1000.3 ms | 1,119.0 ms | 1.119× |

**Anvil is the binding case and its 1.937× is ONE MISSED TICK** in healthy M0
data — so any k ≤ 2 greys the panel when a single `summary` slips. k = 4 means
"three consecutive ticks missed" and clears the worst healthy multiple by
**2.07×**, the same order of margin `kRxWatchdogMs` had (2.6×) but taken against a
multiple. Excluded and named: `anvil_101_reconnect`'s 4,747.7 ms (9.5×) is the
capture's deliberate drop — the fault case, and 2.4× above the threshold, so the
golden still fires exactly once.

**Floor 1,000 ms / ceiling 30,000 ms.** The floor is the smallest threshold this
project has run on evidence. The ceiling is the important one: **it is what stops
a SUSTAINED slowdown buying unlimited tolerance**, since a growing median grows
the threshold. Sized against the only real outage on record — A2b's 176,000 ms —
which it catches at 30 s rather than never.

**Uncalibrated default = the ceiling**, deliberately: before calibration the
object knows nothing, and the honest failure direction is slow-to-grey rather
than greying a healthy feed. **A threshold always exists, so there is no
*calibrating* state to draw — no new rendered state, no §4 change, no §5 change.**
Both venues deliver a liveness frame within a second of connect, so it is
short-lived.

**Surviving a sustained rate change**, which is the case that matters because
Anvil's coalescing is global — a lagging broadcaster presents as a lower *rate*,
not a gap: the window is the last 32 intervals and nothing else, so a permanent
halving refills at the new rate within one window while a single late frame moves
a rank, not the median. Both directions are pinned as host tests.

**2 · `venue_traits` carries the signal's NAME.** `double stale_gap_ms` →
`std::string_view liveness_signal` (`"summary"` / `"heartbeat"`), plus a
`liveness_note` holding the measurement. **The rename is the point**: a `double`
in that row is an invitation to write a book-silence threshold, which the ruling
forbids. A `static_assert` pins the field's type. The withdrawn constants survive
only as `legacy_book_threshold_ms`, labelled WITHDRAWN, read by nothing but the
frozen historical columns.

**3 · Nine traces re-run — the DoD's requirement, met exactly:**

| trace | signal | median | calibrated | GREY |
| --- | --- | ---: | ---: | ---: |
| `anvil_101_baseline` | summary | 500.1 | 2000 | 0 |
| `anvil_101_baseline_20260809` | summary | 500.0 | 2000 | 0 |
| `anvil_101_depth27_20260816` | summary | 500.0 | 2000 | 0 |
| `anvil_101_feederoff_20260817` | summary | 500.0 | 2000 | 0 |
| **`anvil_101_reconnect`** | summary | 499.9 | 2000 | **1** |
| `kraken_btcusd_d10` | heartbeat | 1000.3 | 4002 | 0 |
| `kraken_btcusd_d25` | heartbeat | 1000.2 | 4001 | 0 |
| `kraken_btcusd_d100` | heartbeat | 1000.4 | 4000 | 0 |
| **`kraken_minagbp_d25`** | heartbeat | 1000.6 | 4003 | **0** |

Anvil 2,000 and Kraken 4,000 from **one constant**, with no per-venue number
anywhere. **The pins were PREDICTED from the Python pass and then verified** —
all nine `liveness_events`/`liveness_firings` predictions held on the first
`--selfcheck`, and the ten pre-existing figures passed unchanged first, which is
the procedure's step 1.

**No golden moved, because the goldens now pin the M1 rule explicitly.** Every
`ReplayOptions` in `test_replay_goldens.cpp` (15 sites) became
`ReplayOptions::legacy_anvil()`, with a banner saying why: a golden whose
expectations depend on a calibration would move the day a trace is re-captured,
and calibrating would have shortened the pinned 3,468 ms grey window to 2,468 ms
for no finding.

**The one output the ruling IS supposed to move, reported rather than hidden:**
`dc_ladder` now calibrates, so on the reconnect trace it reads `watchdog 2000 ms`
and `grey for 2469 ms` where it read `1000 ms` and `3468 ms`. Episode count
unchanged. `dc_replay` / `dc_replay_streaming` over all four Anvil traces:
**byte-identical, 8/8**.

**4 · `gap_stats.py` — two distributions, and the labels shout.** The rows were
"any frame" / "event-producing" / "book-affecting": three plausible names with no
hint that only one may drive a threshold. Now **`LIVENESS signal -> GREY`** and
**`BOOK AGE`**, `--threshold` is armed on the liveness series, and the episode
clearer is a liveness arrival. **The `> 9007 * 1.5` assertion is gone** — it was
premised on a beaten figure *and* on the wrong quantity — replaced by seven
sub-cases asserting the *multiple*, the floor and ceiling, the uncalibrated
default, both venues' calibration from the signal alone, spike rejection, and
sustained-rate tracking. Cross-check: the fixed tool independently reproduces the
C++ figures (quiet pair LIVENESS median 1000.6 / worst 1026.2, BOOK AGE worst
9006.8, 0 episodes at 4003 ms).

**5 · Write-ups.** `NOTES-kraken.md`: the ruling above the three options, saying
plainly it is **the third, taken and extended** — options 1 and 2 were both ways
of finding a better *number*, and the ruling is that there is no number. Plus the
**defensive resubscribe, named and not built**, with its cost: its trigger would
be book silence, the exact quantity the ruling says carries no information, so it
would fire every ~26 s on a healthy MINA/GBP. `NOTES.md`: the full `summary`
distribution across 1,191 intervals, the idle measurements, the renamed field.

**6 · `kRxWatchdogMs` untouched.** Verified: no `firmware/` file changed by me.

---

### Task D — what stage B and A2 inherit ✅

Under **Owed by stage B** in the stage A log, and in DESIGN strain 22:

**`age_ms` is a sliding-window deficit, not a cumulative one** — Anvil queues and
never drops, so a cumulative expected-vs-received count reads **zero through a
111 s backlog** while the rendered book is nearly two minutes old. The window must
catch stall-then-burst rather than average it away. A constraint on stage A2's
code, arriving before the code.

**Freshness needs the client ping** — cadence proves generation-time liveness
only, which is the ruling's own point 7 and not a defect to tune away. Crow
answers pings unconditionally with the pong appended to the same per-connection
buffer as data, so a pong behind a backlog *measures* the backlog. `PingProbe`
already exists; what is owed is using the two together.

Carried forward: the Kraken-golden-from-the-Anvil-parser mutation test; the
deliberately captured resync slice; stage D's criterion — now **26 s**, not the
9,007 ms stage 0 believed; `age_ms` rendering to at least minutes; and the
resubscribe named-not-built.

**DESIGN strain 22 updated**, and its first clause is **half closed in an
unexpected direction**: the venue table no longer duplicates `kRxWatchdogMs`
because it no longer holds a duration at all, so stage B's job is to *delete* a
constant rather than reconcile two. The third clause gains a sibling of the same
shape: a liveness signal named wrong greys on the wrong evidence, the Python and
C++ tables must agree on it, and nothing checks that they do — the
four-edits-in-two-languages count is now five.

**README** gained its *why this exists* paragraph.

---

### Exact next step

Stage B: the Kraken adapter. Unchanged from the stage A log, plus this ruling's
firmware half — `kRxWatchdogMs` is **deleted**, not re-derived, and
`liveness_clock.hpp` moves into the shared venue table. The two open items with
teeth are both named above: the dispatch must prove it dispatched right, and the
resync slice does not exist yet.

---

## Follow-up work order (2026-08-17) — rulings, and one gap in coverage

Stage A's re-derivation is accepted. `k = 4` is accepted with its derivation — Anvil binding at
1.937× on a single missed tick is the right constraint to have found, and one dimensionless
constant serving both venues is worth more than two tuned durations.

Four items. **Nothing here reopens the ruling.**

## 1 · Task C's premise was wrong and the correction is accepted — but the case is now ungoldened at Anvil

An idle Anvil emits book frames at 12.6/s, so *book age growing while liveness holds* has no Anvil
form on an idle server. Describing the trace accurately rather than as the twin was right, and
what it does prove — `summary` at 2.008/s, p50 exactly 500.0 ms, on an empty queue — is the
ruling's premise reproducible from a file, which is worth more than the twin would have been.

Record the correction in `NOTES.md` in these terms: **the Anvil shape of *age grows while liveness
holds* is a backlogged socket, not an idle server**, and no committed trace can contain one because
a backlog is a property of the client's socket rather than of the wire. That is a known
uncoverable-by-capture case, and it is why stage A2's estimator has to be windowed. Add it under
**Owed by stage B** as an explicit coverage gap rather than leaving it implied.

## 2 · Commit a slice of the 600 s confirmation capture

The strongest evidence for the whole ruling — 25,843 ms of healthy book silence with heartbeats
continuing at 936–1,042 ms throughout — is in an untracked file, because Task 2 said leave it
untracked and that instruction outlived its reason.

- **All four gaps over 12 s fall between t+70 s and t+140 s.** Slice that window, commit it under
  the existing conventions, `venue: kraken`, taxonomy pinned.
- It is the golden for the case the ruling exists to serve, at 25.8 s rather than the committed
  quiet pair's 9.0 s, and it costs one 70-second file.
- State in `NOTES-kraken.md` that the committed quiet-pair slice covers the same case at 9 s and
  this one covers the extreme, so a future reader does not treat one as redundant.

## 3 · At least one golden must exercise the calibrated path

Pinning `legacy_anvil()` at all 15 `ReplayOptions` sites avoided churn for no finding, which was
reasonable — but it leaves **the shipped default path with no golden coverage at all**, and
invariant #6 does not have an exemption for "the new behaviour was reported in the session log".

- Convert the reconnect golden — the one output the ruling actually moves — to the calibrated
  path and re-pin it at 2,000 ms / grey 2,469 ms.
- Keep the legacy pin alongside it if the historical comparison is worth a row; two pins on one
  trace is cheap and the pair documents the change better than either alone.
- Leave the other 14 on `legacy_anvil()`. One golden on the default path closes the invariant; 15
  would be churn.

## 4 · `age_ms` needs its definition pinned before stage A2, and Task C is why

Three candidate meanings were treated as one because at Anvil-with-a-busy-feeder they coincide.
They diverge by orders of magnitude: time since the last book **frame** (~80 ms at Anvil, always),
time since the book last **changed** (unbounded at both venues), and estimated **queuing lag**
(zero until a socket backlogs, then 111 s).

Record under **Owed by stage B**, in these words:

> `age_ms` is the estimated age of the displayed book relative to the venue's current state — that
> is, queuing lag. It is not time since the last frame, and it is not time since the book last
> changed. A book that has not changed is not old: at MINA/GBP nobody traded, and the displayed
> book was exactly correct throughout 25,843 ms of silence. Time since the last change is market
> information and may be displayed, but it is not age and must never drive a rendered state.

That keeps `age_ms` venue-free and consistent with the ruling: both venues estimate it from a
windowed deficit against the liveness signal, and neither reads it off the book.

## 5 · The floor is load-bearing too, for the opposite failure

The session log calls the 30,000 ms ceiling the load-bearing clamp. The 1,000 ms floor is equally
so, in the other direction: when a backlogged socket drains, the queued frames arrive as a burst
with near-zero inter-arrivals, the rolling median collapses, and an uncapped `k × median` would
grey a healthy feed the moment it recovered. Say that in the comment next to the floor, so the
next reader does not tune it away as belt-and-braces.

## Constraints

- §6 frozen. No `engine/`, no `firmware/`, no adapter. Pins add rows only.
- Do not reopen `k`, the clamps, the rename, or the ruling.
- **Do not refresh the vendored snapshot yet** — the Anvil corrections are still uncommitted
  upstream. §9 item 8 stays live until they are committed and versioned.
- `cmake --workflow --preset host-mingw`, green. Commit nothing.

## Definition of done

- [x] `NOTES.md` records the backlogged-socket correction and the uncoverable-by-capture gap.
- [x] 70-second slice of the confirmation capture committed and pinned; both Kraken traces'
      roles stated in `NOTES-kraken.md`.
- [x] Reconnect golden runs calibrated and is pinned at 2,000 / 2,469 ms; the other 14 unchanged.
- [x] `age_ms` definition written under **Owed by stage B** in the wording above.
- [x] Floor comment states the drain-burst failure it prevents.
- [x] Green, nothing committed, session log appended.

### 2026-08-17 (second pass) · Claude Opus 5 (1M) · the five follow-ups

**24/24 green, nothing committed, no `engine/`, no `firmware/`, no adapter, `k`
and the clamps and the rename untouched, vendored snapshot NOT refreshed.**

One deviation from the letter of the order, and it is item 2's window — flagged
here rather than buried, because it changes a file that gets committed.

---

**1 · The backlogged-socket correction ✅** — recorded in `NOTES.md` in the terms
asked for: *the Anvil shape of "age grows while liveness holds" is a backlogged
socket, not an idle server, and no committed trace can contain one because a
backlog is a property of the client's socket rather than of the wire.* The
argument is spelled out — two sockets on the same server at the same instant
disagree about the book's age, `rx_ns` records only when *this* client got the
bytes, and `_local/drain-120ms.ndjson` still does not hold the answer because the
111 s figure had to be computed against a second unthrottled socket. Added under
**Owed by stage B** as item 9, an explicit coverage gap: invariant #6 cannot be
satisfied for this case by a golden, and stage A2 must say so rather than appear
covered.

**2 · The extreme slice, committed and pinned ✅ — with a corrected window.**

`kraken_minagbp_d25_20260817.ndjson`, 144 records, 18.2 KiB raw / **2.7 KiB
gzipped**, taxonomy pinned, `dc_replay_kraken_quiet_pair_extreme` in ctest.

**The order said t+70..t+140 s. That window would have cut the fourth gap.** The
four >12 s gaps *open* at t+70.1 / 84.3 / 118.5 / 139.8 — which is what my
previous report listed and where the t+140 came from — but the fourth one
**closes at t+158.1 s**, and a gap needs both endpoints inside the window to
exist in the trace. A t+70..t+140 slice would have contained three of the four
under a filename claiming four. Cut **t+65..t+160 (95 s)** instead: all four
contained endpoint-to-endpoint, 5 s of lead-in, 2 s of tail. `slice_trace.py`
gained a `--start` flag to express it.

What it pins, and it is the ruling in one file:

| | worst book silence | greys, calibrated | greys at the WITHDRAWN 15,000 ms |
| --- | ---: | ---: | ---: |
| `..._20260816` (typical) | 9,006.8 ms | **0** | 0 |
| `..._20260817` (extreme) | **25,843.3 ms** | **0** | **3** |

Both roles stated in `NOTES-kraken.md` under *Two quiet-pair traces, two jobs*,
so neither is later dropped as duplication.

**Two things the new slice forced, both worth more than the slice.** It begins
mid-stream, so it has no `book/snapshot` to baseline from — and
`kraken_frame_economics.py --selfcheck` **failed, correctly**, because an
unpinned Kraken trace is a failure there by design. It cannot be pinned either:
all 49 of its checksums score **0/49 at any depth**, by construction. So:

- the tool gained a `NOT_A_CHECKSUM_GOLDEN` table — an **explicit exclusion with
  its reason**, not a silent skip. "Cannot be graded" is a third state distinct
  from "pinned" and "forgotten", and skipping it would have reopened the hole the
  add-rows rule closed. Verified: the excluded trace reports `[excl]`, `--pin`
  refuses it *with the reason*, a trace listed in both tables fails, and a trace
  in **neither** table still **fails**.
- `--verify` gained a guard for a **false CATCHES**. The truncation criterion is
  `ok_never_truncating < checksummed`, and on this trace 0 < 49 is true only
  because *everything* fails. The tool now says so rather than reporting a
  meaningless verdict as a finding.

**3 · A golden on the shipped default path ✅** — the invariant-#6 hole, closed.

New case: *"reconnect trace: THE CALIBRATED PATH greys for 2,469 ms, not 3,468"*.
It runs `ReplayOptions{}` — threshold 0, calibrate — and pins
`r.threshold_ms == 2000.0` **derived, not passed in**, with `stale_ms` in
(2468, 2470). The legacy case is kept beside it unchanged, and the file's banner
now states the exception rather than claiming every golden is legacy-pinned. The
other 14 sites are untouched.

The pair is better than either alone, and the reason is worth recording: if
Anvil's cadence ever changed, the two would move in **opposite directions** — the
legacy grey window would stretch while the calibrated one held at 4× the new
median. That divergence is a signal neither number produces by itself.

**4 · `age_ms`'s definition pinned ✅** — under **Owed by stage B** as item 8, in
the owner's words verbatim: age is *estimated queuing lag*, not time since the
last frame and not time since the book last changed; **a book that has not
changed is not old**; time since the last change is market information that may
be displayed but must never drive a rendered state.

**5 · The floor's real failure mode ✅** — and it is the better half of the pair.
The comment in `liveness_clock.hpp` now leads with it: **when a backlogged socket
drains, the queued frames arrive as a burst with near-zero inter-arrivals, the
rolling median collapses, and an uncapped `k × median` would grey a healthy feed
at the exact moment it recovered** — punishing the recovery rather than the
outage, just as the panel finally has fresh data. Not hypothetical at Anvil,
which queues and never drops, so the drain is a burst by construction.

Pinned rather than only commented, because a comment is not a check: a new
sub-case drives the clock to 500 ms cadence, floods it with a full window of
2 ms arrivals, asserts the median **has** collapsed (2.0 ms), asserts the
uncapped product would be under 10 ms, and asserts the threshold holds at the
1,000 ms floor — so the next normal 500 ms tick is not a grey. Without the floor
it would be, by 125×.

---

**Not done, by instruction:** the vendored snapshot is **not** refreshed. Its
`*** PENDING REFRESH ***` header and §9 item 8 stay live until the Anvil
corrections are committed and versioned upstream.

**Exact next step.** Unchanged: stage B, the Kraken adapter. Its inheritance is
now nine items under *Owed by stage B*, of which the two with teeth remain the
dispatch proving it dispatched right, and the deliberately captured resync slice
that does not exist yet — and note that the trace committed today is **not** it,
for the reason in `NOTES-kraken.md`.

---

## Closing work order (2026-08-17) — stage A close-out

**Run this only after the Anvil PROTOCOL.md changes are committed upstream.** Everything here
either depends on that or belongs in the same session as the thing that does.

Stage A is accepted. The `t+65..t+160` correction was right and the instruction it overrode was
wrong: a gap needs both endpoints inside the window to exist in the trace, and the timestamps in
that instruction were opening times, not intervals. `NOT_A_CHECKSUM_GOLDEN` and the false-CATCHES
guard are both accepted as improvements on what was asked for.

---

## 1 · Refresh the vendored snapshot and close §9 item 8

- Take the upstream commit SHA at the moment of refresh; record it and the date in the snapshot's
  header block, replacing `PENDING REFRESH`.
- Confirm the corrected §3 keepalive text and the reshaped §3.5 guarantee are both present in the
  refreshed body, by quoting them into the session log — not by asserting the refresh happened.
- Note in `NOTES.md` that the §4 idle-clause report was raised against the pre-refresh copy and
  that the refresh closes that class of error, per §9's vendor-citation rule.
- Close §9 item 8 with the SHA.

## 2 · Three records the mid-stream slice creates, all **Owed by stage B**

The extreme slice begins mid-stream. That is the correct shape for it, and it makes three things
concrete that were previously abstract.

**(a) The adapter must tolerate never having seen a subscribe ack.** Decision 1 makes
`success:false` fatal. A mid-stream trace carries no subscribe at all, which is a third case —
neither success nor failure — and an adapter whose state machine requires having seen an ack will
reject the extreme slice at exactly the moment it is most needed. Record: *absence of a subscribe
is not failure of a subscribe, and the replay path must be able to enter the stream already
subscribed.* This does not reopen decision 1; it is about initial state, not about refusal.

**(b) "Healthy feed, no snapshot yet" now has a trace.** The extreme slice has no snapshot, so a
book built from it never initialises while the liveness signal stays healthy throughout. That
combination was hypothetical when Q1 declined to add vocabulary for it, and it is now a file. Stage
B decides what the panel renders in that state — an empty ladder that is honestly empty, or grey.
Record it as an open question with the trace named, and do not settle it here: it is a rendering
decision that wants the panel, not the console.

**(c) Synthesised snapshots for sliced traces — named and rejected.** The obvious way to make
windowed slices book-gradeable is to replay the prefix and write a computed snapshot into the
slice header. Record the rejection with its reason: a trace is wire truth, and a derived baseline
would be the first thing in `harness/replay/` that no venue ever sent. `NOT_A_CHECKSUM_GOLDEN` is
the honest answer to the same problem. Write it down so it is not re-proposed by someone who finds
the exclusion table and reads it as a gap.

## 3 · Propose the commit split; do not commit

The working tree now carries an amendment, a calibration, two new traces, a tool with a new state
model and four repaired silent defects. Propose a split as a numbered list with a one-line message
for each, on these principles:

- One commit per independently reviewable claim.
- Traces commit separately from the code that reads them, so a reader can check either without the
  other moving underneath.
- The `ladder_font.hpp` glyph is not part of stage A and commits alone, under its own authorship —
  the working-tree variant, which is the one that was compiled and looked at, not the superseded
  one already in `a6b395a`.
- The vendor refresh commits with its SHA in the message.

Then stop. The split is reviewed before anything is committed.

## Constraints

- §6 frozen. No `engine/`, no `firmware/`, no adapter. Pins add rows only.
- Nothing in section 2 is implemented — all three are records.
- `cmake --workflow --preset host-mingw`, green.
- **Commit nothing.**

## Definition of done

- [x] Snapshot refreshed, SHA and date in the header, `PENDING REFRESH` gone, both corrected
      clauses quoted into the session log, §9 item 8 closed.
- [x] Three records written under **Owed by stage B**, in the terms above, with the extreme slice
      named in (b).
- [x] Commit split proposed as a numbered list with messages; nothing committed.
- [x] Green; session log appended; stage A marked complete in ROADMAP pending the owner's commit.

### 2026-08-17 (close-out) · Claude Opus 5 (1M) · the vendor refresh, three records, the split

**24/24 green. Nothing committed. Stage A is complete pending the owner's commit.**

**The precondition was checked before anything else**, since the order opens by
gating on it: Anvil's PROTOCOL.md changes are committed upstream — `25ade0e`
(19:42) *"docs: state the summary cadence guarantee and correct the keepalive
clause"* and `4801ed8` (20:53) *"docs: record the unconditional book stream against
A2b, cross-reference 3.5 from 4"* — and the Anvil working tree is clean.

---

**1 · Snapshot refreshed, §9 item 8 closed ✅**

Re-pinned at **`4801ed8d8b09b62ec4fcee8e68280f16b3c4780c` (2026-08-17)**, SHA and
date in the header, `PENDING REFRESH` gone from the snapshot and from all four
places that mirrored it (`ARCHITECTURE.md` ×2, `CLAUDE.md`, `README.md`) plus the
two prose references in `NOTES.md` and the ROADMAP row.

**Verified rather than asserted, both ways.** Before refreshing, the outgoing body
was confirmed to be a clean copy of `04db612` (byte-identical once line endings are
normalised, 34,636 chars) — so nothing of ours had leaked into it. After, the new
body is byte-identical to `git show 4801ed8:PROTOCOL.md` (37,926 chars). Our
annotation has only ever lived in DepthCharge's own header block, which is why no
edit had to be undone to accept the new text.

**Both corrected clauses, quoted from the refreshed body** rather than from the
diff or the commit message:

> **§3 Keepalive.** *"**The server never initiates a WebSocket ping, and there is
> no dedicated heartbeat frame in v1.** A client that waits for either waits
> forever. What the server does send unprompted is the **data stream itself, on
> fixed timers that do not depend on order flow**: one `book` frame per subscribed
> ticker on the ~14 Hz publish tick, and one `summary` frame on the
> `ANVIL_SUMMARY_HZ` cadence. Both continue on a completely idle book — one with
> nothing resting, nothing arriving and nothing trading — because the engine
> thread's publish deadlines are timers, not activity hooks. A quiet socket is
> therefore **not** the normal appearance of a quiet market."*

> **§3.5 `summary`.** *"**Emission is timer-driven, not activity-driven — this part
> is a guarantee.** … Order flow determines the *contents*, never the *timing*: an
> idle book … keeps publishing, and each publish stamps a fresh `seq` … Measured
> with the feeder stopped against a static book: 62 consecutive frames over 31 s of
> zero order flow … **The interval is not part of that guarantee.** It is operator
> configuration (`ANVIL_SUMMARY_HZ`) … **A client that needs a staleness threshold
> must derive it from the cadence it observes on the connection it is using**, not
> from a number read out of this document."*

**The second quote is the one that matters.** Point 3 reasoned that a hardcoded
threshold is coupled to a value the client cannot read back; §3.5 now *instructs*
clients not to hardcode one, and `liveness_clock.hpp` was written **the day before
that sentence existed** and satisfies it unchanged.

**CORRECTED BY THE OWNER, and the correction is the more useful record.** This was
first written up as independent convergence — "neither side had seen the other's
text" — and that overstates it. Both descend from **one instruction**: the reply to
Anvil said *promise the mechanism, not the interval, and refuse the number*, and
Anvil's own commit message says so in as many words (`25ade0e`: "3.5 now promises
the mechanism … and explicitly refuses to promise the interval"). The texts had not
seen each other; **the reasoning shares a parent.** Agreement under common cause is
not corroboration and licenses nothing about correctness.

**The smaller, true claim:** *the ruling survived contact with a second implementer
who could have found it awkward and didn't.* A venue asked to promise a mechanism
and refuse an interval could reasonably have pushed back — the interval is the
thing a client most obviously wants — and it did not. That is worth having, and it
is all that is on offer here.

Worth the contrast, because the two are indistinguishable in a one-line summary:
§9's A7 row (2026-08-16) records *"three estimates from two codebases converging is
the strongest evidence this project has produced for anything"* — and **that one
was independent**, because the estimates came from different methods with no shared
instruction behind them. This one is not. Same shape on the page, opposite
evidential weight.

The §3 quote also turns a DepthCharge measurement into contract: *"both continue on
a completely idle book"* is the 12.6 book-frames/s of
`anvil_101_feederoff_20260817.ndjson`, which was reported to Anvil as a correction
to A2b's reading of `emit_books`.

**`NOTES.md` records the sequence**, with the rule that made retirement possible:
the report was raised against **`04db612`**, and **a finding against a vendored file
must name the SHA it was found in** — a pinned copy is a moving target across
re-pins, so *"the protocol says X"* is not a durable claim while *"`04db612` says
X"* is, and only the second form can be checked as fixed later. That is §9's
standing discipline about quoted claims applied to the vendor boundary. §9 gains a
close-out row carrying the SHA, and the general rule: **when a measurement
contradicts a vendored contract, the fix is upstream and the annotation is a holding
position — record it with an expiry, and close it with a SHA.**

**2 · Three records, all under Owed by stage B ✅** — items 10, 11, 12, none
implemented:

- **10** — *absence of a subscribe is not failure of a subscribe, and the replay
  path must be able to enter the stream already subscribed.* Explicitly not
  reopening decision 1: it is about initial state, not refusal. The trap named:
  an ack-requiring state machine rejects the extreme slice **at exactly the moment
  it is most needed**, since that slice is the golden for the case the ruling
  serves.
- **11** — *healthy feed, no snapshot yet* now has a file, named. Two defensible
  renderings stated (honestly-empty ladder vs grey) and **deliberately not
  settled**: they differ at desk distance and in nothing a host test can assert,
  so it wants the panel.
- **12** — synthesised snapshots for sliced traces, **named and rejected**: *a
  trace is wire truth, and a derived baseline would be the first thing in
  `harness/replay/` that no venue ever sent.* Written down precisely because
  someone who finds `NOT_A_CHECKSUM_GOLDEN` will read it as a gap and propose
  exactly this.

**3 · Commit split proposed, nothing committed ✅** — ten commits, below, ordered so
the tree builds and ctest is green at every one. Two ordering constraints drove it
more than theme did: a new trace and its pin must land together (an unpinned
committed trace is a red build, by design), and the reader must precede the traces.

---

**Green: 24/24.** Tree carries 22 modified and 13 untracked paths; `engine/`
untouched; the only `firmware/` change is the owner's glyph, which is commit 1 and
is not stage A's.

**Exact next step.** Review the split. Stage A is complete on the owner's commit;
stage B is the Kraken adapter, and its inheritance is now twelve items under
*Owed by stage B*.

---

## Proposed commit split — REVIEW BEFORE ANYTHING IS COMMITTED

Nothing is committed. Ten commits, ordered so the tree builds and `ctest` is green
at **every** one of them — which is the constraint that shapes the split more than
any other, and the reason a few files appear where a purely thematic split would
not put them.

**Principles applied:** one commit per independently reviewable claim; traces
separate from the code that reads them; `ladder_font.hpp` alone and not part of
stage A; the vendor refresh carrying its SHA.

**Two ordering constraints worth stating**, because they are why this is not
simply "docs, then code, then traces":

- **A new committed trace and its pin must land together.** `dc_taxonomy
  --selfcheck` treats an unpinned committed trace as a FAILURE, and the CMake glob
  picks up `harness/replay/*.ndjson` automatically — so a trace committed without
  its row is a red build, by design. The two trace commits therefore each carry
  their own pin row and ctest entry.
- **The reader must precede the traces.** Commits 3–5 build the venue-aware
  reader; commit 6 is the first trace that needs it. Reversing them would commit a
  file nothing in the tree can read.

| # | Files | Message |
| --- | --- | --- |
| **1** | `firmware/src/ladder_font.hpp` | `firmware: centre the decimal point on the baseline, two pixels high` |
| **2** | `docs/vendor/anvil-protocol.md` | `docs: re-pin the vendored protocol at 4801ed8 -- idle emission is now a guarantee` |
| **3** | `harness/include/dc_harness/{venue,trace,trace_decoder,trace_report}.hpp`, `harness/src/{trace,trace_decoder,trace_report}.cpp`, `harness/src/{dc_replay_main,replay_driver}.cpp`, `harness/include/dc_harness/replay_driver.hpp`, `CMakeLists.txt` | `harness: one reader, two venues -- the metadata gains a venue tag and dispatches` |
| **4** | `harness/src/dc_taxonomy_main.cpp`, `harness/include/dc_harness/taxonomy_pins.inc`, `CMakeLists.txt` | `harness: pin the record taxonomy of every committed trace, add-rows-only` |
| **5** | `harness/tests/test_trace_venue.cpp`, `CMakeLists.txt` | `harness: cover the venue contract, the decoder seam and the resync rule` |
| **6** | `harness/replay/anvil_101_feederoff_20260817.ndjson`, its pin row, `CMakeLists.txt`, `harness/replay/NOTES.md` | `trace: an idle Anvil, feeder off -- summary holds 2 Hz and book does not stop` |
| **7** | `harness/include/dc_harness/liveness_clock.hpp`, `venue.hpp`, `trace.hpp`, `trace.cpp`, `trace_report.cpp`, `replay_driver.{hpp,cpp}`, `dc_ladder_main.cpp`, `test_trace_venue.cpp`, `test_replay_goldens.cpp`, `taxonomy_pins.inc` | `harness: staleness counts the liveness signal, and calibrates its own threshold` |
| **8** | `harness/replay/kraken_minagbp_d25_20260817.ndjson`, its pin row, `tools/kraken_frame_economics.py`, `CMakeLists.txt`, `harness/replay/NOTES-kraken.md` | `trace: 25.8 s of healthy book silence -- the golden the staleness ruling serves` |
| **9** | `tools/{gap_stats,tracefile,anvil_frame_economics,slice_trace,capture_anvil}.py` | `tools: stop measuring Anvil's shape at Kraken -- four silent defects` |
| **10** | `ARCHITECTURE.md`, `ROADMAP.md`, `README.md`, `CLAUDE.md`, `docs/DESIGN.html`, `docs/briefs/M4-stage-A-*.md` | `docs: M4 stage A -- the replay dialect, the staleness ruling, and what stage B owes` |

### Why each is separately reviewable

**1 · The glyph.** Not stage A, not mine, and its own authorship. **This is the
working-tree variant** — `{{0,0,0,0,6,6}}`, the baseline two-high dot — which is
the one that was **compiled at 17:47:04** and looked at on the panel. `a6b395a`
captured the superseded centred variant from a stale index, so this commit
corrects that rather than adding a third change. Commit it alone and first, so
nothing about stage A depends on it or is confused with it.

**2 · The vendor refresh**, carrying its SHA in the message. Independently
checkable in one step: the body must be byte-identical to
`git -C ../Anvil show 4801ed8:PROTOCOL.md`, and it is. Closes §9 item 8, so it
should precede the docs commit that says so — but it must **follow** nothing,
which is why it is early.

**3 · The reader.** The largest commit and the one claim that cannot be usefully
subdivided: the venue tag, the venue-conditional metadata contract, the
`static_assert`-pinned decoder seam, the Kraken classifier and the two reports are
one design. `CMakeLists.txt` appears in three commits because each adds its own
targets and tests; each addition is independent and each leaves the build green.

**4 · The pin table**, separate from the reader because it is a different claim:
the reader says *this is what a trace means*, the pin says *and it must not change
quietly*. Reviewing them together would hide that the second is mutation-verified
and the first is not.

**5 · The tests**, separate again, so a reviewer can read what is asserted without
the implementation moving underneath — and can check the mutation results quoted
in the log against a fixed target.

**6 and 8 · The two traces**, each with its pin, its ctest entry and its notes
section. They are separate from each other because they prove different things at
different venues, and separate from the code because a trace is evidence: a
reviewer should be able to re-derive its figures without the reader changing in
the same diff.

**7 · The ruling.** Deliberately *after* the reader rather than folded into it,
because it supersedes part of what commit 3 established — the venue-declared
constant becomes a self-calibrating threshold and `stale_gap_ms` becomes
`liveness_signal`. Reviewing them as one commit would hide that the first was
shipped, measured, and then corrected by a second measurement; as two, the
correction is legible. This is the commit that touches the most files for the
fewest lines, and every one of them is the same rename following through.

**9 · The tools.** Four silent defects, all with the same root — `gap_stats.py`
had its own capture reader — plus the venue-aware predicates they now share. Kept
out of the harness commits because Python and C++ are reviewed differently and
because these are *repairs*, not new claims.

**10 · The documentation** last, so it describes a tree that exists. It is the
only commit that can be read as a narrative, and it is deliberately the only one
whose message is a milestone summary rather than a claim.

### Two things a reviewer should check that the diffs will not show

- **`git status` must still show `firmware/src/ladder_font.hpp` as the only
  `firmware/` change, and nothing at all under `engine/`.** §6 was frozen for the
  whole of stage A and the constraint is invisible in a diff of what changed.
- **The eight pre-existing committed traces must be byte-identical.** Verified by
  SHA-256 at every step of this work; a re-check is one command, and it is the
  claim the whole stage rests on:
  `Get-ChildItem harness/replay/*.ndjson | Get-FileHash`.

### Not in the split, and deliberately

`harness/replay/_local/*` — the two full captures behind the new traces
(`anvil_101_feederoff_20260817.full.ndjson`, 200 KiB, and
`kraken_minagbp_d25_20260817T1600Z.confirm.ndjson`, 600 s) stay **untracked**, per
the M0 policy. They are the only artefacts that could reproduce the slices, and
they live on the owner's box.
