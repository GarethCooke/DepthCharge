# M3 · Stage C→D interlude — why the panel greys in steady state

**Track:** Firmware, bench · **Status:** Instrument written and green (2026-08-10, host 11/11,
both firmware envs build) — **the run and the verdict are the owner's and are not done.** The
session log block is on `docs/briefs/M3-live-anvil-on-the-panel.md` as this brief asks; the
run protocol and how to read the new lines are in `firmware/README.md`.
**Executor:** CC writes the instrument and reviews; the owner flashes, runs, reads the panel.
**Read first:** `ARCHITECTURE.md` **§6 invariant #5** (binding — *any* `Gap` greys the panel
until a fresh `Snapshot`; **liveness is defined by events reaching the book, not bytes
arriving**) and **§7** (the two DepthCharge→Anvil couplings, including the republish-cadence
assumption — strain point 10); `firmware/src/feed_task.hpp` (the watchdog note and `Stats`);
the existing arrival / event / `a->e` instrument and `gap_histogram`; `docs/vendor/anvil-protocol.md`
§4; and the two runs this follows — `depthcharge` (PS off) and `depthcharge-ps` (PS on).

## Why this exists

Stage C is bench-proven and the render policy is now settled: **two-state, invariant #5 as
written** — a real >1 s book silence greys the panel, no "probably fine," no third state. The
steady-state runs show that silence happening **17–25×/10 min** (17 over 11 min PS-off; 25 over
10.5 min PS-on), each a genuine **1–2.5 s** window (worst 2461 ms off, 1893 ms on) where no book
event reached the board. Those greys are #5 telling the truth, not crying wolf.

The owner's bar is that the panel must **not grey in steady state at all**. Under two-state that
means the >1 s book-holes have to go to **~zero**. Whether firmware can deliver that depends
entirely on *why the board falls behind* — and that is the one thing we have not measured. This
evening measures it. It changes **nothing** in the feed path; it only instruments.

## The question — and why it is *not* "drain vs PHY"

The clean fact from the runs: `connects=1`, `sock_gaps=0` across a whole run that still logged 25
book-holes >1 s. The socket never dropped and Anvil (an unconditional ~80 ms republisher) never
stopped — the board simply *received the stream in bursts*.

The naïve split is "board too slow" vs "Wi-Fi silent," but those are **entangled**: Anvil's
undocumented per-socket coalescing **sheds** `book` frames whenever this socket backs up, so
shedding is a *symptom* of the board falling behind, not an independent cause. "Anvil shed it"
and "Wi-Fi delayed it" surface identically as *no book frame for 2 s*.

The un-entangled question is:

> When the board falls behind, is it **board-bound** (Core 0 can't drain the stream fast enough)
> or **link-bound** (the frames aren't arriving over the air)?

Only the first is firmware-fixable, and if it is, the fix is not a gradient — it can step-change
to zero, because **a board that keeps up is never shed**: its gaps collapse into the healthy
sub-640 ms band, all under the 1 s watchdog, and there is nothing left to grey. If it is
link-bound, firmware cannot zero it — that becomes 2.4 GHz channel / placement / antenna (the S3
is 2.4-only), the same bar in a different domain.

## The measurement — three board-side signals, one run, nothing else changed

**Precondition — state what the existing `arrive` histogram is stamped at.** If "arrival stamped
at read" is *when the app reads/reassembles the frame*, then an `arrive`-gap already folds in any
board-side drain delay, so `arrive >1 s = 25` does **not** by itself mean "socket was dry." Say it
precisely in the log so the new signals read against it correctly.

> **Answered (2026-08-10), and it is the second reading rather than the first.** The stamp is
> `esp_timer_get_time()` taken in `WsTransport::on_event` for each `WEBSOCKET_EVENT_DATA`, and
> `FrameReassembler::finish()` records the *last* chunk's stamp — so an arrival is "the final
> chunk of a whole message was handed to our callback". That callback runs on
> `esp_websocket_client`'s own task, which is **downstream of the Wi-Fi driver, lwIP, the socket
> read, the TLS record decrypt and the `esp_event` dispatch hop**, and **upstream of** only the
> FramePipe queue hop and the feed task. So `arrive >1 s = 25` means "no complete message reached
> our callback for a second" and is fully consistent with a busy Core 0 — it does not mean the
> socket was dry. The split's boundary is the queue hop, not the antenna. Corrected in
> `frame_pipe.hpp`, `serial_console.cpp`, `firmware/README.md` and DESIGN §05, where the old
> verdict table's first row ("both fill → transport") is now struck through as an over-claim.

For every >1 s book-silence, capture:

1. **Per-core idle across the hole — the verdict signal.** Enable FreeRTOS run-time stats
   (`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`) and sample Core 0 / Core 1 idle-fraction over each
   hole window.
   - Core 0 **starved** (idle low) → the board cannot drain the stream → **board-bound,
     firmware-fixable.**
   - Core 0 **idle** (idle high) → the board is waiting on data that isn't there → **link-bound.**

   Note this is a *different* claim from `a->e ≤ 8 ms`: that says per-frame *book-apply* is cheap;
   this asks whether aggregate Core-0 headroom (TLS decrypt + the `esp_event` dispatch hop —
   strain 11's ~37 alloc/free pairs/s — + Wi-Fi driver) is being exhausted.

2. **Recovery shape.** When the hole clears, log the inter-arrival of the next few DATA events and
   the `seq` advance:
   - **tight burst**, large contiguous-ish `seq` jump → the frames existed and were delivered late
     (a TCP backlog flush) — Anvil sent them; the delay was downstream.
   - **single frame**, cadence just resumes, `seq` skipped → Anvil **coalesced the middle away**
     upstream — the board never had them.

   Cross with (1): *burst + Core-0 idle* = the link delayed them; *burst + Core-0 pegged* = they
   sat in the socket buffer unread because Core 0 was busy (drain); *no burst* = Anvil shed them,
   which for an ~80 ms republisher still traces to this socket backing up (§7, strain 10).

3. **rssi through the run** (already in the banner) so a link verdict can be checked against signal
   — but note −69 dBm is fine-ish, so second-long holes point more at airtime/congestion or drain
   than at raw weak signal.

Bucket the >1 s book-holes across these and print the tally beside the existing histograms.

**Optional, no Anvil change:** if Anvil *already* surfaces per-connection send/shed counters in
its existing logs or metrics, eyeball them for this socket during the run for direct confirmation.
Do **not** modify Anvil to add them — that is an Anvil-backlog item, not this evening.

## Run protocol

- **`-e depthcharge` only** (PS off — the shipping default). Omitting `-e` uploads *both* envs;
  don't. One env, one board.
- **~10 min, steady state — do not pull the Wi-Fi.** We want `connects=1`; the holes in scope are
  mid-connection, not reconnects. If an organic reconnect happens, note it and carry on, but the
  target is a clean `connects=1` window.

## Guardrails (constitution)

- **Pure instrumentation.** No change to the feed, book, adapter, or watchdog. `kRxWatchdogMs`
  stays 1000 ms. **No board-side levers this evening** — applying buffer / priority / TCP-window
  fixes before the split says "board-bound" is exactly the assume-then-fix trap this milestone
  keeps paying for (the watchdog re-derivation, the starvation hypothesis, the token-carrying
  parser all cost a round because a fix preceded a measurement).
- **No golden moves.** Firmware-only; the host goldens are untouched. A moved golden means stop,
  out of scope.
- **Invariant #7.** New counters and timestamps are fixed-size, no per-frame allocation — same
  discipline as `gap_histogram`.

## Acceptance — and the fork it picks

The serial log classifies the >1 s book-holes as **board-bound** (Core 0 starved) vs **link-bound**
(Core 0 idle), reconciled against what `arrive` actually measures, with the recovery shape
separating "delivered late" from "shed upstream." That verdict chooses the next brief:

- **board-bound** → the lever bundle: `esp_websocket_client buffer_size` (fewer chunk/event hops
  per 5 KB frame — also trims the strain-11 alloc churn), LWIP TCP window / socket recv buffer
  (absorb a burst instead of TCP-backpressuring Anvil into shedding), WS-task priority/core — each
  re-benched against *this* instrument, with **near-zero steady-state >1 s book-holes** as the bar.
- **link-bound** → out of firmware: 2.4 GHz channel scan, placement, rssi-vs-hole correlation. The
  structural answer — Anvil's incremental-L2 feed so the board stops being a slow consumer of 5 KB
  full-books at 12.5 Hz — stays on the Anvil backlog for M4/M5.

Append the run and the verdict as a session-log block on `docs/briefs/M3-live-anvil-on-the-panel.md`.
