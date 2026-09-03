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

☐ §2's per-slice frames/events/publishes table recorded in the session log — done 2026-09-03
☐ Driver commit laddered green in its own fresh detached worktree
☐ Firmware commit laddered green; `pio run` clean
☐ `crossed_publishes` 0 and `worst_cross_ticks` 0 on all eight Binance slices
☐ The census is a `CHECK`, not a `MESSAGE`
☐ Anvil unmoved at 6,404 publishes; Kraken recorded at 9,837 → 4,899
☐ The two Kraken window goldens re-derived, each expected value written down
  before the run that produced it (§7)
☐ The stale rationale corrected: `test_replay_goldens.cpp:934–937` no longer
  claims Kraken applies a whole message before emitting
☐ All five known unknowns answered in the log, including the ones whose answer
  was "unchanged"
☐ Soak re-run >24 h continuous; reset count recorded whatever it is
☐ ARCHITECTURE §9 gets the decision: the publish boundary is the message, not
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
