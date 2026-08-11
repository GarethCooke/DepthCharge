# Bench record — M3 stage D, the panel running, 2026-08-11

Serial capture from the ESP32-S3 (COM5, 115200) with the 64×64 HUB75 panel attached and
drawing a live Anvil ladder. This is stage D's feed-side and heap evidence.

**Provenance, stated first because it bounds everything below.** The monitor buffer truncated
the capture: what is reproduced here is **09:25:48 → 09:28:15, about 2 minutes 25 seconds** of a
run the owner reports as ten minutes. Every figure in the *Steady state* and *Events* sections
is quoted from those lines. The two figures in *Reported, not captured* came from the owner at
the bench and have **no log lines in hand** — they are recorded as claims, not as measurements,
and the next session should re-capture them rather than cite them.

## Configuration

```text
panel-hw: psram 8385975 B (present=yes, NOT used for the framebuffer)
panel-hw: dma-internal free=186616 largest=180212 | reserve=98304 budget=88312
          | rungs d6=78080 d5=57600 d4=43264 d3=32000 (double), d6single=39040
I2S-DMA:  Allocating 49152 bytes memory for DMA BCM framebuffer(s).
I2S-DMA:  lsbMsbTransitionBit of 0 gives 105 Hz refresh rate.
S3:       Clock divider is 24
panel-hw: pixel clock: nominal 8 MHz -> divider 24 -> ACTUAL 6.66 MHz
          | refresh: library says 105 Hz, really ~87 Hz
panel-hw: UP: 64x64 depth=6 double-buffered brightness=224 refresh=105 Hz
          | predicted=78080 measured=77888 B | dma-internal free 186616 -> 108728
```

**The allocation model is confirmed against hardware.** Predicted 78,080 against 77,888 and
77,904 measured across two boots — **0.25 % high**, which is the safe direction. The library's
own figure (49,152) is the framebuffer alone; the difference is DMA descriptors and allocator
bookkeeping, which is exactly the 44 % error `panel_budget.hpp` was rewritten to close.

The pin map reads back identical to `BRINGUP.md` including the trap — `LAT=12 OE=13 CLK=11`.

## Steady state — the feed side is unregressed with the panel running

Representative block, 09:28:13, 459 frames into the heap window:

```text
-- errors : parse=0 price=0 ticker=0 unknown=0 trunc=0 backseq=2
-- feed   : frames=589 wd_gaps=1 sock_gaps=1 connects=2 worst_gap=3977 ms worst_frame=8002 us
-- a->e   : <1:1 1-5:0 5-25:513 25-100:0 100-500:0 0.5-1k:0 >1k:0 | n=514 worst=8 ms | qwait=7263 us behind=1/6
-- cpu    : window c0=91% c1=87% over 10016 ms | healthy c0=91% c1=87% n=511
-- pipe   : published=590 oversize=0 no_slot=0 qfull=0 abandoned=0 cont=0 ctrl=1
-- channel: published_v=518 consumed_v=517 drawn=511 superseded=7
-- panel  : depth=6 double bright=224 refresh=105 Hz fb=78080 B | drew 49 at 4.89/s worst paint 13856 us of 33000 us period
-- rate   : in 5.69/s of 5.69/s attempted (0% lost) | events 4.99/s | 41.59 KB/s | mean 7481 B | 2.82 chunks/msg
heap: steady after 459 frames: free=30132 (-64) largest=12788 (-2048) low=21248
```

| Bar (stage C baseline) | Measured | |
| --- | --- | --- |
| `a→e` ≤ 22 ms | **8 ms** worst, every sample in the 5–25 ms bucket | pass |
| Core 0 ~90 % idle | **91 %**, against a healthy baseline of 91 % | pass |
| `parse_errors` 0 | **0** across 589 frames | pass |
| pipe drops 0 | `oversize=0 no_slot=0 qfull=0 abandoned=0 cont=0` | pass |
| `0 %` lost | **0 %** every window | pass |
| heap `free` delta 0 | **`free=30196 (+0)`** repeated across four consecutive blocks | pass |

**Core 1 sits at 87 % idle against an 87 % baseline — the render task costs nothing
measurable.** `worst paint` is **2,283 µs of the 33,000 µs frame period** in steady state, 7 %,
and it does not move with book depth as designed.

## Events

**One RX watchdog hole, 09:26:43.** Grey 626 ms, `wd_gaps` 0 → 1, `sock_gaps` unchanged.

```text
-- hole : #1 1605 ms c0=98%/93% c1=89% rssi=-38 seq+12 of +81 | recov 322,382,0,110 ms -> LINK-BOUND cadence
```

Classified link-bound with Core 0 at 98 % idle against a 93 % baseline: the board was waiting,
not busy. Invariant #5 greying on a real 1.6 s silence in book events.

**One socket drop, 09:27:41 — and the two-handle reconnect beat its own prediction.**

```text
09:27:41.079  ws down: event 2
09:27:41.104  *** STALE (disconnect) at v366 — panel greys here ***
09:27:41.327  feed down 250 ms — opening handle B (attempt #2, dns 0 ms)
09:27:44.964  socket up on handle B, 3635 ms into attempt #2
09:27:45.079  *** LIVE at v367 ***    grey for 3975 ms before resync
```

**3,975 ms against the ~4,700 ms the transport-residual work predicted**, and against 9,451 ms
before that work. The spare handle opened one poll after the drop; the remaining 3.6 s is DNS +
TCP + TLS + upgrade, which no constant in this repository reaches.

## Two findings worth carrying forward

**1. `largest` took a 2 KB step down across the reconnect and did not recover.** 14,836 →
12,788, while `free` returned to 30,132/30,196 exactly. Nothing leaked; the largest free block
fragmented once. One step is not a trend, but it is precisely the signal `heap_probe` exists to
raise, and **a long run with many reconnects is the test that would settle it.** Worth watching
before M4 adds a second venue.

Also in that window: `free=22168 (-8028)`. The TLS handshake transiently takes **8 KB out of
30 KB free** — it succeeded, but it is the tightest moment in the run and the reason
`kReserveInternalBytes` should not be lowered casually.

**2. `worst paint` jumps 2,283 → 13,856 µs during a reconnect**, in the window where Core 1
drops to 63 %. Still 42 % of the frame period, and it does not persist. Reads as contention with
the reconnect rather than the paint getting heavier — the steady-state figure is unchanged
either side of it. Worth a second look if it ever appears without a reconnect beside it.

## Open — and both need answering before M3 is ticked

**Three `rst:0x1 (POWERON)` resets in the first 35 seconds.** 09:25:48, 09:26:10, 09:26:23. The
board ran 21 s, reset; ran 13 s without ever getting Wi-Fi up, reset again; then ran clean for
the rest of the capture. `POWERON` is a genuine power event — not a panic, not a task watchdog.
**Not established whether this was the owner power-cycling or the board browning out.** If the
latter it matters: brightness 224 plus panel inrush on a USB-powered DevKit is a plausible
mechanism, and it would be a stability question hanging over the acceptance.

**`Reason: 202 - AUTH_FAIL` on the first association attempt of every boot**, ~130 ms in,
recovered ~700 ms later by Arduino's `first_connect` retry (which fires for all reasons on the
first attempt only). Benign — but it is the same reason code that leaves the framework
permanently disarmed *after* boot, and it is why `WifiSupervisor` exists.

## The pull-the-Wi-Fi test — reported by stopwatch, not captured

The owner ran it and reports **~3 s to grey** and **~9 s to recover**. No log lines for that
outage are in hand; both figures are wall-clock from the human action, which is a different
quantity from the one the DoD's "~1 s" refers to, and the difference is the point.

**The watchdog deadline is measured from when data stops arriving, not from when the AP was
switched off**, and those are seconds apart. This capture demonstrates the gap directly at
09:26:43 — hole #1 is a **1,605 ms** silence in book events, the panel greyed and was live again
after **626 ms**, which puts the grey at 1,605 − 626 ≈ **979 ms into the silence**. That is the
1,000 ms watchdog firing on its deadline, to within the log's own resolution. The 2026-08-10
runs showed the same thing from the other side: the panel greyed **6.9 s before** the Wi-Fi
driver reported the deauth at all.

So ~3 s by stopwatch decomposes as roughly 1 s of watchdog plus ~2 s of data that was still
arriving — frames buffered at the AP, TCP retransmits in flight — and **the firmware half of
the DoD line is evidenced by the capture above, on a different outage.**

The ~9 s recovery decomposes against figures this capture *does* contain: ~4.5 s for the station
to re-associate (the framework's — `BEACON_TIMEOUT`/`NO_AP_FOUND` are both in Arduino's
reconnectable list, so `WifiSupervisor` should stay silent and print no `rejoining` line),
≤0.25 s poll granularity, **3.6 s** measured socket bring-up, ~0.4 s for Anvil's snapshot.
Nothing anomalous in it.

**What is still not established for that specific run**, and wants one more capture rather than
one more stopwatch: the silence-to-grey interval from the `-- hole` line, and confirmation the
resync showed no torn frame, no frozen intermediate and no flash of a coloured stale book.

## Still owed for stage D

- The deliberate pull-the-Wi-Fi run, **with its log**, and the grey duration.
- The final statistics block of a full ten minutes — the cumulative `wd_gaps` / `sock_gaps` /
  `connects` and histogram totals. The monitor buffer truncated both attempts; capture with
  `pio device monitor -f log2file` next time.
- **A photo or clip in this directory** — live ladder and the grey. Also unblocks MP stage 2.
- The `POWERON` question above.
