# M5 Stage E — the publish boundary

**Track:** Mixed [host first, then one flash, then a soak] · **Status:** Not started · **Size:** one evening for the host half; the soak runs overnight
**Read first:** `/ARCHITECTURE.md` §4 and §6, commit `eff1ee9` and its message, `docs/briefs/M5-stage-D-C-the-soak.md`.

## Goal

One `depthUpdate` becomes N single-side `Delta` events, and every one of them
currently drives a full `DisplaySnapshot` publish. That granularity is one
defect with two faces:

- **The panel drew a book that cannot exist.** 1,032 LIVE ladder lines in the
  34.5 h soak with the best bid at or above the best ask — 2.9% of the time the
  panel claimed to be live, worst spread −$39.79. Across the committed corpus,
  `crossed_publishes` = **11,062**. Anvil 0, Kraken 0.
- **The IDLE task on CPU 0 starved.** Six task-watchdog aborts in the same soak,
  six for six with `dc_feed` running on CPU 0.

The panel samples between the bid levels that lift the touch and the ask
removals in the same message that pay for them. Stage E moves the publish to the
message boundary, where the book is whole. At the end `crossed_publishes` is 0
on every committed Binance slice, Anvil and Kraken are still 0, and the soak
runs past 24 h without a reset.

`eff1ee9` landed the guard and stopped. **This is the fix, and 11,062 is the
number it has to take to zero.**

## 1 · The change, and why it needs no engine edit

All three adapters funnel every event through one `emit()` — `anvil_adapter.hpp:188`,
`kraken_adapter.hpp:449`, `binance_adapter.hpp:725`. Above them, both drivers
hand the adapter a per-event sink and publish *inside* it. The message boundary
already exists in both: it is the return of the decode call.

So the sink becomes apply-only, and one publish happens after the call returns.

| Site | Today | After |
|---|---|---|
| `harness/src/replay_driver.cpp:134` | `decoder_.decode(frame, [this](ev){ on_event(ev); })`, `on_event` applies *and* publishes | sink applies and does episode bookkeeping; one publish + `note_window` + `stamp_age` + `stamp_reseed` + `channel_.publish` after `decode` returns |
| `firmware/src/feed_task.cpp:357` | `adapter_.on_frame(text, …apply_and_publish)` | `…apply_only`, then one `publish_current()` |
| `firmware/src/feed_task.cpp:245` | `on_rest_body(…apply_and_publish)` | same treatment — a REST seed is one message |
| `firmware/src/feed_task.cpp:582` | `on_transport_gap(…apply_and_publish)` | same treatment |

`FeedEvent` is untouched: no new `Kind`, no end-of-message flag, no field, so the
trivially-copyable static assert at `feed_event.hpp:121` (invariant #7) is not in
play. `engine/` is not edited at all. **If you find yourself adding a `Kind`, stop
and report — the seam you needed was already there and you have missed it.**

`FeedTask::republish_if_due` (`feed_task.cpp:538`) is untouched and is the floor:
a book that produces no publish still gets one every `kRepublishPeriodUs`.

**The publish is conditional on the message having emitted ≥1 event.**
Unconditional would publish on Anvil's summary frames too — 1,225 → 1,406 —
and every Anvil golden would move. Every figure in §2's `after` column assumes
the conditional form.

## 2 · Measured, 2026-09-03 — and the hypothesis is half wrong

Done before any edit, with a scratchpad tool linked against the host libs and
nothing in the tree touched. `publishes(after)` = decode calls that emitted ≥1
event.

| venue | frames | events | publishes now | after | factor | crossed |
|---|---|---|---|---|---|---|
| anvil | 7,359 | 6,404 | 6,404 | 6,404 | 1.00× | 0 |
| kraken | 5,372 | 9,836 | 9,837 | 4,899 | 2.01× | 0 |
| binance | 4,184 | 123,813 | 123,814 | 2,984 | 41.5× | 11,062 |

Census-8 alone: 123,681 → 2,892 (42.8×), crossed 11,062 — reproducing `eff1ee9`
exactly. Per-slice figures are in the session log.

**Anvil holds.** `max/msg == 1` on all five slices. The one Gap in the reconnect
trace is a watchdog gap with no frame — its own decode call.

**Kraken is refuted, and the reason matters more than the number.**
`kraken_adapter.hpp:506–514` emits one `Delta` per level, exactly as Binance
does. Every Kraken slice is 1 Snapshot + N Deltas; 1,920 of 2,472 messages on
the d100 slice carry more than one event. Kraken's publish count halves.

**So Kraken's `crossed_publishes == 0` is luck, not structure.** It has the same
publish granularity and the same exposure; these six captures happened never to
straddle the touch. The census comment at `test_replay_goldens.cpp:934–937` —
"Kraken's adapter applies a whole message before emitting" — **is wrong, and so
is the corresponding paragraph of `eff1ee9`'s commit message.** Correct the
comment in this stage's driver commit and record the correction in §9; the
commit message is pushed and stands as written, which is why the correction has
to live somewhere a reader will reach.

That upgrades the fix from "Binance had a defect" to "two venues had it and one
was never caught in the act".

## 3 · `first_stale_` is the one place this is not a refactor

`replay_driver.cpp:379–383`:

```cpp
book_.publish(latest_);
…
if (is_stale && !saw_stale_ && ev.kind == FeedEvent::Kind::Gap) {
    first_stale_ = latest_;
    saw_stale_ = true;
}
```

Today that captures `latest_` immediately after the Gap event's own publish.
After the change the Gap event does not publish, so a naive move captures the
*previous* message's book and `first_stale_snapshot` becomes a different object
— which is golden content, not a count.

The requirement is stated in terms of what it means, not what it does: **the
snapshot recorded as first-stale must be the one the panel would actually have
shown at the moment it went grey** — the publish that follows the message
containing the Gap. Set a flag during the events, capture after the boundary
publish.

Record in the log whether the captured object is byte-identical to the one
before the change, and if it is not, why the new one is the correct one. For the
Anvil reconnect trace, where one frame yields one event, it should be identical
— and if it is not, that is a finding about the change, not about the trace.

## 4 · What else is per-publish, and moves with it

- Driver: `note_window()`, `stamp_age()`, `stamp_reseed()`, `channel_.publish()`.
- Firmware: `age_and_bank`, `stats_.grey.note()`, `last_publish_us_`.

`publish_current`'s own comment calls `age_and_bank` "the once-per-publish
caller". One publish per message is a *better* fit for that sentence than one per
level, not a worse one. Say so in the log rather than leaving a reader to wonder
whether the age meter was collateral.

## 5 · Order — three commits, not one

1. **The driver alone.** `harness/src/replay_driver.cpp`. Host suite green, the
   census numbers recorded before and after.
2. **The firmware mirroring it.** `firmware/src/feed_task.cpp`. `pio run`, then
   flash.
3. **The soak re-run.** Per D-C, but see §7.

Do not begin 2 until 1 is laddered. The driver is where the numbers are
measurable; the firmware is where they are not.

## 6 · Acceptance

- `crossed_publishes == 0` on every Binance slice, and `worst_cross_ticks == 0`.
  **Turn the census at `test_replay_goldens.cpp:899–943` from a `MESSAGE` into a
  `CHECK`.** That is what the guard was for; leaving it as a print after the fix
  lands would be an instrument that reports nothing.
- **Anvil unchanged**: 6,404 publishes, crossed 0. Any movement here is a defect
  in the change, not a consequence of it.
- **Kraken**: crossed still 0, and its new publish total recorded — expected
  9,837 → 4,899. The total is *expected to move*; that is the corrected clause.
- The two Kraken window goldens re-derived — see §7.
- Full host suite green from a fresh detached worktree, `CMAKE_HOME_DIRECTORY`
  read from the cache and matched.
- `pio run` for `firmware/`. **The host suite is not entitled to claim anything
  about `firmware/` — a verification claims only what it compiled.**

## 7 · The two Kraken goldens, and the rule for moving them

They move in the driver commit, not a separate one: a pin re-derived in a commit
that does not contain the change that moved it has nothing to check it against.

`test_window.cpp:486–489` (d25) and `:515–519` (d100). Most of it is arithmetic
you can do on paper — uniform 50/4/20 rows per publish × the new publish count —
and those values are to be **stated before the run prints them**.

Two are not uniform: `levels_dropped` (760,185) and `worst_tick_span`. The rule
for both: **write down what you expect to happen to the number and why, then
run.** `worst_tick_span` is a max over publishes, so it can only fall or hold —
if it rises, the change is wrong. `levels_dropped` must first be classified: if
it is summed per publish it should fall by roughly the publish factor; if it is
a property of the book at publish it should hold. Say which, then check.

**A golden whose new value is "whatever the code printed" is not a golden.** If
either of the two non-uniform numbers cannot be predicted before the run, say so
and stop rather than adopting the output.

`check_row_arithmetic` (`:430–437`) is relative to `book.publishes` and survives.
The cross-policy equality checks (`:465–471`) survive. Anvil's subcase is
unaffected.

## 8 · The crash is a hypothesis, not a finding

Re-derived from `firmware/logs/device-monitor-260830-223245.log` (71.9 MB,
2026-08-30 22:32 → 2026-09-01 08:06), six occurrences, uniform:

```
E task_wdt: Task watchdog got triggered. …
E task_wdt:  - IDLE (CPU 0)
E task_wdt: Tasks currently running:
E task_wdt: CPU 0: dc_feed
E task_wdt: CPU 1: IDLE
abort() was called at PC 0x4201c9f8 on core 0
  #3  0x4201c9f8 in task_wdt_isr at components/esp_system/task_wdt.c:176
```

**That PC is the watchdog's own abort site.** It is identical across all six
because `task_wdt_isr` is always where the abort is called from — it says nothing
about which of our lines was running. The discriminating evidence is the two
lines above it: IDLE on CPU 0 did not get scheduled, and `dc_feed` was what held
it. A constant PC treated as a bug fingerprint here would be one more instrument
read in place of its subject.

Publishing once per message instead of once per level cuts feed-task work per
message by the delta count, which is *plausibly* why IDLE0 starves. **It is not
proven.** The soak either shows zero resets or it does not. **If resets survive
this fix, that is a separate stage and not a failure of this one** — record the
count and close E on its own acceptance in §6.

## Known unknowns — resolve and record

1. ~~Do Anvil and Kraken publish counts move?~~ **Answered §2:** Anvil no,
   Kraken yes (2.01×). Kraken's zero crossed count is empirical, not structural.
2. Is `first_stale_snapshot` byte-identical across the change on the Anvil
   reconnect trace? Structurally it should be — the Gap is a lone-event watchdog
   message — but that is reasoning, not a run. **Verify it.**
3. **Is the panel under-fed on a quiet book?** One publish per message at
   Binance's cadence should stay well above panel refresh, and
   `republish_if_due` is the floor beneath it — but confirm from the bench
   `pipe: published=` counter rather than assuming.
4. ~~`binance_btcusdt_DEFECT_silent_stream_20260826`?~~ **Answered §2:** 4
   frames, 0 events, 1 terminal publish. Unchanged by the fix. Same for
   `kraken_minagbp_d25_20260817` (144 frames, 0 events).
5. Does any figure in `tools/soak_report.py` derive from a publish rate that is
   about to change? (`soak_report.py:53` parses `published=`.) Binance's rate
   falls ~42×; this is now a near-certainty rather than a question.

## Definition of done

☑ §2's per-slice frames/events/publishes table recorded in the session log — done 2026-09-03
☑ Driver commit laddered green in its own fresh detached worktree — `d2618d8`, 2026-09-04
☐ Firmware commit laddered green; `pio run` clean
☑ `crossed_publishes` 0 and `worst_cross_ticks` 0 on all eight Binance slices — on all
  eleven, and on the Anvil and Kraken slices too
☑ The census is a `CHECK`, not a `MESSAGE` — per slice as well as in total
☑ Anvil unmoved at 6,404 publishes; Kraken recorded at 9,837 → 4,899
☑ The two Kraken window goldens re-derived, each expected value written down
  before the run that produced it (§7)
☑ The stale rationale corrected: `test_replay_goldens.cpp:934–937` no longer
  claims Kraken applies a whole message before emitting
☐ All five known unknowns answered in the log, including the ones whose answer
  was "unchanged"
☐ Soak re-run >24 h continuous; reset count recorded whatever it is
☑ ARCHITECTURE §9 gets the decision: the publish boundary is the message, not
  the event, and why
☐ ROADMAP status updated

## Out of scope

- **D-A4** — the reseed mechanism and the header marker in the symbol slot.
- The M5 close-out list (card 29's median convention, strain 29's tripwire
  wording, the `CLAUDE.md` prose-versus-ordinal line, the client-ping rehoming,
  the PSRAM slab-scan residue, `-- rx : read 101%`, `main.cpp:114`'s stale M4
  banner).
- Any change to the three adapters.
- Any new `FeedEvent::Kind` or end-of-message field.
- **Asserting** rather than counting inside `Book::publish`. It counts, the
  renderer never reads it, and nothing branches rendering on rendered state
  (invariant #5). That does not change here.

## Session log
<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->

### 2026-09-03 · Claude Opus 5 · §2, the measurement, before any edit

**Done.** Frames, events and publishes for all 22 committed slices, plus the
publish count one-per-message would produce. Nothing in the tree was touched:
the numbers come from a scratchpad tool linked against the host libs, driving
`run_replay` with an observer. `publishes(after)` counts decode calls that
emitted at least one event — the conditional form §1 now states.

| venue | slice | frames | events | publishes now | after | max ev/msg | msgs >1 ev | crossed |
|---|---|---|---|---|---|---|---|---|
| anvil | `anvil_101_baseline` | 1406 | 1225 | 1225 | **1225** | 1 | 0 | 0 |
| anvil | `anvil_101_baseline_20260809` | 1513 | 1332 | 1332 | **1332** | 1 | 0 | 0 |
| anvil | `anvil_101_depth27_20260816` | 1399 | 1218 | 1218 | **1218** | 1 | 0 | 0 |
| anvil | `anvil_101_feederoff_20260817` | 1753 | 1512 | 1512 | **1512** | 1 | 0 | 0 |
| anvil | `anvil_101_reconnect` | 1288 | 1117 | 1117 | **1117** | 1 | 0 | 0 |
| kraken | `kraken_btcusd_d10_20260816` | 902 | 1532 | 1532 | **839** | 6 | 488 | 0 |
| kraken | `kraken_btcusd_d25_20260816` | 1599 | 2994 | 2994 | **1537** | 6 | 1058 | 0 |
| kraken | `kraken_btcusd_d100_20260816` | 2535 | 5206 | 5206 | **2472** | 6 | 1920 | 0 |
| kraken | `kraken_minagbp_d25_20260816` | 93 | 77 | 77 | **30** | 4 | 28 | 0 |
| kraken | `kraken_minagbp_d25_20260817` | 144 | 0 | 1 | **1** | — | 0 | 0 |
| kraken | `kraken_minagbp_d25_resync_20260818` | 99 | 27 | 27 | **20** | 3 | 6 | 0 |
| binance | `binance_btcusdt_d100ms_20260824` | 305 | 1435 | 1435 | **138** | 44 | 124 | 0 |
| binance | `binance_btcusdt_d1000ms_20260824` | 131 | 9468 | 9468 | **60** | 743 | 60 | 537 |
| binance | `binance_btcusdt_deepseed_20260824` | 503 | 11497 | 11497 | **233** | 822 | 232 | 2493 |
| binance | `binance_btcusdt_deepseed2_20260824` | 502 | 7838 | 7838 | **231** | 689 | 223 | 774 |
| binance | `binance_btcusdt_mixed1_20260825` | 997 | 46764 | 46764 | **855** | 932 | 830 | 5320 |
| binance | `binance_btcusdt_mixed2_20260825` | 999 | 28355 | 28355 | **826** | 813 | 783 | 323 |
| binance | `binance_btcusdt_reconnect_20260824` | 556 | 18287 | 18287 | **519** | 560 | 511 | 1615 |
| binance | `binance_atomeur_d100ms_20260824` | 69 | 37 | 37 | **30** | 3 | 6 | 0 |
| binance | `binance_atomeur_d100ms_liveness_20260826` | 95 | 116 | 116 | **83** | 4 | 27 | 0 |
| binance | `binance_atomeur_deepseed_20260824` | 23 | 16 | 16 | **8** | 4 | 5 | 0 |
| binance | `binance_btcusdt_DEFECT_silent_stream_20260826` | 4 | 0 | 1 | **1** | — | 0 | 0 |

The two zero-event slices publish once each — the driver's terminal
`published_version() == 0` publish, which the change leaves alone.

**Two cross-checks that the tool is measuring the real thing**, both against
numbers already committed: the crossed total over the census-8 is 11,062, which
is `eff1ee9`'s figure to the unit; and the publish counts 5,206
(`kraken_btcusd_d100`) and 1,225 (`anvil_101_baseline`) are exactly the
`frames_with_drops` pins standing in `test_window.cpp:518` and `:545`. A
measurement that agreed with neither would have been the first thing to doubt.

**Decisions.**

- **The publish must be conditional on the message having emitted ≥1 event**,
  and that is now §1's last paragraph rather than a session-local note. Anvil's
  summary frames emit nothing; an unconditional publish-per-decode would take
  its 1,225 to 1,406 and move every Anvil golden, which would have turned a
  refactor into a re-baseline for no gain. It is the one implicit choice in §1's
  table that changes the answer.
- **The hypothesis was recorded as refuted for Kraken rather than repaired.**
  `emit_delta` (`kraken_adapter.hpp:506–514`) emits one `Delta` per level, so
  Kraken has Binance's granularity exactly; 1,920 of 2,472 messages on the d100
  slice carry more than one event. Its `crossed_publishes == 0` is a property of
  these six captures, not of the adapter. The brief's §2, §6 and §7 were
  rewritten around that rather than the measurement being narrowed to the venue
  the defect was found at.
- **Gaps do not share a message with anything on this corpus** (measured:
  `gap_not_last = 0` on all 22 slices; the three traces containing a Gap —
  `anvil_101_reconnect`, `binance_btcusdt_reconnect_20260824`,
  `kraken_minagbp_d25_resync_20260818` — each have it as the last event of its
  message). So §3's flag-then-capture cannot currently observe a message that
  greys and heals within itself, and no guard for that case is being written
  blind. Worth re-measuring if a future capture contains one.

**Known unknowns answered:** 1 (Anvil no, Kraken yes — 2.01×) and 4 (the silent
stream slice is 4 frames, 0 events, 1 terminal publish; unchanged. Same for
`kraken_minagbp_d25_20260817`). 2 is reasoned but not run. 3 and 5 are bench and
firmware questions and stay open; 5 is now a near-certainty, since Binance's
publish rate falls ~42×.

**Next step.** §5 step 1 — the driver alone: move the publish to the message
boundary in `harness/src/replay_driver.cpp`, re-derive the two Kraken window
goldens with each expected value written down before the run (§7), correct the
stale rationale at `test_replay_goldens.cpp:934–937`, and turn the census into a
`CHECK`.

### 2026-09-04 · Claude Opus 5 · §5 step 1 — the driver alone

**Done.** `harness/src/replay_driver.cpp` publishes once per message
(`d2618d8`), host suite green 52/52, and the commit laddered green in a fresh
detached worktree at `C:/tmp/dc-E1-ladder` with `CMAKE_HOME_DIRECTORY` read back
from the cache as the worktree and not the main tree. Worktree deleted. **No
`pio run` for this commit and none is owed:** it touches `harness/src`,
`harness/include` and `harness/tests` only — no `firmware/src/*.cpp`, no
`platformio.ini` — so the mechanical test in `CLAUDE.md` says the host suite is
entitled to claim all of it.

**The numbers.** `crossed_publishes` and `worst_cross_ticks` are **0 on all 22
committed slices at all three venues**; every slice's publish count landed on
the figure §2 predicted, to the unit. Anvil unmoved at 6,404. Kraken 9,837 →
4,899. Binance 123,814 → 2,984.

**Decisions.**

- **The census is a `CHECK` per slice, not only in total**, and it now names all
  eleven Binance traces rather than the eight it carried while it was a print —
  §6 asks for zero on *every* Binance slice and a list naming most of them
  cannot say that. `worst_cross_ticks == 0` is asserted beside
  `crossed_publishes == 0` because they answer different questions: one counts
  occurrences, the other measures the worst one, and a counter that failed to
  increment should not look like a book that never crossed. `CHECK(slices ==
  std::size(cases))` was added because the loop skips a missing file, and a
  corpus that lost one would otherwise pass on what remained.
- **The two Kraken goldens were predicted with a second replayer, not with
  paper.** §7 allows the uniform ones to be arithmetic and demands a stated
  expectation for the rest. The uniform ones were done on paper and were right
  (50/4/20 a publish at d25, 54 at d100). For the rest a standalone replayer was
  built from `TraceReader` + decoder + `Book` + `note_window`'s arithmetic,
  **validated by reproducing all ten committed figures in its per-event mode**,
  and only then read for its per-message column. That is what let
  `levels_dropped` be predicted exactly rather than bounded, and it caught a
  third non-uniform figure §7 had not listed: `rows_validated` under
  `LargestFirst`, which paper arithmetic put at ≈15,876 and the replayer at
  15,797. The 0.5% between those two is the finding restated — a settled book is
  not a mid-message book.
- **`levels_dropped` at d100 fell to exactly 146 × 2,472.** The old 760,185 was
  109 more than 146 × 5,206; all 109 were levels the book held only part-way
  through a message. This is the crossed touch measured by a different
  instrument, and it is the strongest evidence in the stage that the mechanism
  is publish granularity rather than anything Binance-specific.
- **`worst_tick_span` fell twice and held twice** (d25 308 → 308, d100 Top 319 →
  319, LargestFirst 1128 → 1116, ThinnedTail 1060 → 1050). Never rose, which
  §7 names as the falsifier.
- **`ReplayStep` is now documented as one publish rather than one event.** The
  header already said "called after every publish" and "handed to the observer
  with every published snapshot" — the contract was publish-shaped all along and
  the code has stopped disagreeing with it. `step.kind` is the message's last
  event; nothing reads `event_index`. The only behavioural consequence is
  `dc_ladder --follow`, which now paces one rendered frame per message, which is
  what a panel does.
- **The age meter is not collateral (§4).** `stamp_age` calls
  `read_and_bank(current_rx_ns_)`, and within one message that argument does not
  move while `bank` is a max — so dropping the duplicate calls is provably
  identical, not merely green. `publish_current`'s "the once-per-publish caller"
  reads better against one publish per message than against one per level.

**Known unknown 2, answered by measurement.** The whole `DisplaySnapshot` was
FNV-hashed against a build of the pre-change driver: `first_stale_snapshot` is
**byte-identical on `anvil_101_reconnect` and `kraken_minagbp_d25_resync`,
version stamp included**; on `binance_btcusdt_reconnect` every byte matches
except `version` (16094 → 430), which must differ because the publish count did.
So no first-stale object moved in content anywhere.

**One thing found and deliberately not fixed.** The driver's terminal
`published_version() == 0` publish calls `book_.publish` and the stamps but not
`note_window()`, so a zero-event trace reports `book.publishes == 1` with an
empty window report — `check_row_arithmetic` would fail on
`kraken_minagbp_d25_20260817` if it were ever applied to it. Pre-existing, not
touched by this change, and out of a stage whose whole point is that its diff
moves nothing it did not have to.

**Not done, and next.** §5 step 2 — `firmware/src/feed_task.cpp` mirroring this
at its three sites, then `pio run`, then flash. `docs/DESIGN.html` is
deliberately **not** updated yet: its two sequence diagrams already draw
`publish` after the adapter call returns, so they are accurate for the first time
rather than wrong, but the firmware half still publishes per event and a doc
updated now would describe a world that is half true. Update it when step 2
lands. Known unknowns 3 and 5 need the bench. ROADMAP untouched until the
milestone moves.
