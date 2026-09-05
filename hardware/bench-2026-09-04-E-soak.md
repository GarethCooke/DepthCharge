# Bench record — M5 stage E: the 27.8-hour Binance run, 2026-09-04/05

## The one hard constraint IS met, and the acceptance it was blocking passes

D-C needs **one continuous stretch exceeding 24 hours**. This capture spans **32.25 h of wall
clock** in **two boots**, and the second runs **27.81 h**. That is the first stretch in this
project's history to clear the bar, and it clears it by 3.8 h.

| | B1 | B2 |
| --- | --- | --- |
| reset | POWERON | RTC_SW_CPU_RST |
| starts | 2026-09-04 09:09:23 | 2026-09-04 13:36:14 |
| runs | 4.45 h | **27.81 h** |
| ends | task watchdog | monitor stopped, board still LIVE |

Accumulated uptime 116,106 s = 32.25 h, which equals the wall clock to the second — there is no
unaccounted time in this capture.

**And stage E's acceptance passes on the panel's own output: `0` crossed LIVE ladder lines out of
`92,656`.** The run this replaces drew **1,066** of them in 35,177 — 3.03%. That is the whole point
of the stage, measured where it matters rather than on the host.

## Provenance

The capture is preserved before it is read, and verified rather than assumed.

| | |
| --- | --- |
| source | `firmware/logs/device-monitor-260904-090921.log` |
| sha256 | `eb638db136dea82c8e1bb5bb546ddd671875811554748fb98d42bc2314321128` |
| bytes | 65,658,299 |
| lines | 880,353 physical, of which **440,180** carry the monitor timestamp |
| first stamp | `09:09:23.371` (2026-09-04) |
| last stamp | `17:24:40.639` (2026-09-05) |
| committed as | `hardware/bench-2026-09-04-E-soak.log.gz`, 9,048,127 B (7.3×) |

**The gzip was verified by inflating it and re-hashing**, not by trusting the writer: the inflated
stream's sha256 is byte-for-byte the source's. `tools/soak_report.py` independently reports the same
sha256 from its own provenance block, which is a second reader agreeing about the same bytes.

The two line counts are both true and neither is a typo: the monitor writes a blank line between
records, so the file is 880,353 lines of which 440,180 are records. `soak_report.py` prints the
second. Recorded because a future reader comparing the two numbers should not have to work out
which of us was wrong.

### The image cannot name its own commit — but this time the provenance is evidence

`markers : 0`. No `###` commit marker was written into the log by hand, and `main.cpp:114`'s banner
still reads **`DepthCharge M4 stage D`** — the stale banner already on the M5 close-out list. So the
capture cannot identify its own build from the inside, exactly as D-C found.

**The difference from D-C is that the identification here is not inference.** The flash is in this
repository's own record: `b49c923` was built and flashed to COM7 as `depthcharge-binance` at
2026-09-04 08:17, hash verified by `esptool`, logged in
`docs/briefs/M5-stage-E-the-publish-boundary.md`; the soak's POWERON is at 09:09:23 the same
morning, and no flash happened in between. The banner remains a defect of the image and is the
reason this paragraph has to exist at all.

## What the run says, in the order the claims can invalidate each other

### 1 · The crossed touch is gone — the stage's acceptance

Both logs were counted by the same pass, so this is a comparison rather than two quotations:

| | pre-fix (D-C, 34.6 h) | this run (32.25 h) |
| --- | --- | --- |
| LIVE ladder lines | 35,177 | **92,656** |
| `bid > ask` (crossed) | 1,032 | **0** |
| `bid == ask` (locked) | 34 | **0** |
| total `bid >= ask` | **1,066** = 3.03% | **0** |
| worst spread | **−$39.79** | — |

At the pre-fix rate, 92,656 LIVE lines would have carried about 2,800. There are none, in either
boot. The host corpus went 11,062 → 0 at `d2618d8`; this is the same result on the panel.

**The two published figures for the pre-fix run reconcile exactly, and the difference is the
comparison operator.** Everything in this repository quoted **1,032** until 2026-09-05, which is
`bid > ask`. `Book::publish`'s own guard is `out.bids[0].px >= out.asks[0].px`, which also catches
the **34** locked frames, giving **1,066**. So the panel figure was 34 short of the engine's own
criterion, while sitting in sentences that said "at or above" and next to a counter that means
`>=`. It changes nothing about the fix — the post-fix count is 0 on the stricter test, therefore 0
on both — but a reader reconciling 1,032 against `crossed_publishes` was reconciling two different
questions and could never have closed it. **Corrected at every site on 2026-09-05**, each one
keeping the old number and saying what it counted rather than quietly overwriting it.

### 2 · The publish reduction, measured on the board

B2 published **719,618** `DisplaySnapshot` frames in 100,102 s = **7.19/s**. Integrating the
`-- rate` windows over the same boot gives **14,647,852 events**, a mean of **146.35 events/s**.
Before this stage a publish followed every event, so the board was doing **20.4× the publish work
per message** that it does now — on this run's traffic. (The figure is traffic-dependent: a quieter
90 s sample right after the flash measured 40×. Both are the same mechanism priced against different
markets, and neither is a constant to be quoted as one.)

The panel is better fed as a result, not worse: `published_v=719,620`, `drawn=674,672` —
**93.8% of published frames reached the panel**, with 44,948 superseded. Before the change the panel
drew of order 6 frames a second out of ~146 published, so the frame it drew was an arbitrary sample
of a book part-way through a message. That is the mechanism of the crossed line, and this counter is
it going away.

### 3 · The task watchdog SURVIVED the fix — once

B1 ended at `up=16004s` with the identical signature to all six of D-C's:

```text
E task_wdt: Task watchdog got triggered. …
E task_wdt:  - IDLE (CPU 0)
E task_wdt: CPU 0: dc_feed
E task_wdt: CPU 1: IDLE
abort() was called at PC 0x4201ca9c on core 0
rst:0xc (RTC_SW_CPU_RST)
```

The PC differs from D-C's `0x4201c9f8` because the binary differs; it is `task_wdt_isr` either way
and says nothing about which of our lines was running, exactly as stage E §8 warned.

**Stage E predicted this outcome and declared in advance that it does not fail the stage.** §8:
*"If resets survive this fix, that is a separate stage and not a failure of this one — record the
count and close E on its own acceptance in §6."* The count is **1**.

| | pre-fix | this run |
| --- | --- | --- |
| watchdog resets | 6 in 34.56 h | **1 in 32.25 h** |
| longest continuous | 9.84 h | **27.81 h** |

**That is suggestive and it is not a result.** One event is one event; the reset happened 4.45 h in
and then 27.81 h passed without another, which is equally consistent with "much rarer" and with
"needed a condition that did not recur". `-- cpu` still reports **`healthy c0=93%`**, so core 0 is
no less busy in steady state — whatever starves IDLE0 has not been removed, only made less frequent
or less likely. The next stage owns it.

### 4 · Binance did NOT close the connection at 24 hours

This is the finding the run was long enough to make, and it goes against the premise D-C §1 is
built on.

There are **three** socket ends in the capture, all before 13:49 on the first day:

| # | boot | ended | lived | bytes |
| --- | --- | --- | --- | --- |
| 1 | B1 | 13:33:42 | 4.40 h | 177,743,117 |
| 2 | B1 | 13:36:09 | 2.44 min | 4,660,313 |
| 1 | B2 | 13:49:19 | 12.92 min | 29,432,168 |

The socket opened at **13:49:21.937** on 2026-09-04 and is still up at the last line of the capture,
**17:24:40.639 on 2026-09-05** — **27.59 h on one connection, with no close of any kind**. All three
closes that did occur are `[clean-close]`, `errno=128`, at 4.4 h, 2.4 min and 12.9 min — nowhere
near 24 h.

**So the venue's documented 24 h close was not observed on a connection that outlived it by 3.6 h.**
Two things keep this from being a flat contradiction of the venue's documentation, and both belong
in the record rather than in a later argument:

- the endpoint is **`data-stream.binance.vision`**, not the production `stream.binance.com`, and the
  24 h policy is documented for the latter;
- one connection is one sample.

**What it does settle is D-C §1's reasoning.** That constraint exists so the run would observe "the
one disconnect the venue guarantees". This run exceeded 24 h and observed no such disconnect, so the
guarantee is either not in force on this host or not what we believed. **Whether >24 h is still the
right bar is close-out work, not this record's** — carried as backlog **D9** — the bar was met, and the thing it was meant to
catch did not appear.

### 5 · Grey time fell sharply, and it is still the same mechanism

| | pre-fix | this run (B2) |
| --- | --- | --- |
| grey | **71.4%** of uptime | **10.97%** |
| seq-gap episodes | 5,336 | 1,155 (B1: 358) |

Grey episodes across the run: 1,514 closed, **median 4.75 s, p90 18.9 s, max 12.34 min**. Reasons:
`seq-gap` 1,513, `resync` 2. `resync_req=1262`, `heals=0`, `owed=0`.

**This is not stage E's doing and must not be claimed as it.** The board still greys because the
Binance diff stream breaks its `U/u` bracket and a fresh seed is fetched — the reseed mechanism,
which is **D-A4 and explicitly out of scope for stage E**. Market conditions differ between the two
runs and no controlled comparison exists. What the number is good for is stating that this stage did
**not** make greying worse, which was a live risk when the publish rate fell by a factor of twenty.

### 6 · Instruments that were watched and did not move

- **Pipe**: `max_held=4 of 4` sustained again (B2: 990,776 published frames), `qfull=0`,
  `oversize=0`, `no_slot=9,005` = 0.91%. D-C's check 3 finding is reproduced on a longer boot.
- **Heap**: `free=65,832 largest=51,188 low=6,800`, steady after 674,618 frames, `+216 B` against
  its own baseline over 27.8 h. No leak.
- **Age**: `worst 1m15s`, `baseline 19,981 ms`, threshold `39,979 ms`, **CALIBRATED** on 32 samples.
- **RSSI**: `-39 dBm now, min -50, max -31` over 165,477 samples.
- **Holes**: `n=2,335 board=434 link=1,790 mixed=111`, `burst=1,016 cadence=1,299`.

### 7 · One reading that is NOT this record's to adjudicate

`-- signal : server-ping n=4,977 max=53,163 ms **>=2x med=4** | median 19,989 ms threshold
39,979 ms CALIBRATED`.

D-C's check 2 rested on `>=2x med` being **0 on all 12,417 samples**, with the worst healthy
interval at 1.9529× the median. **Here it is 4 of 4,977.** The four are also the run's four watchdog
arms (`wd=4`) and the four `>=40s` entries in `-- signals`, so on the face of it they are intervals
that legitimately greyed rather than healthy ones that nearly did — but separating "healthy" from
"greyed" in that counter is precisely the ambiguity D-C's check 2 left open.

**Recorded, not resolved.** Stage E ran to test a publish boundary and is not entitled to rewrite
the multiplier rule, exactly as D-C said of itself. It belongs to the M5 close-out with these two
numbers beside D-C's.

## What this run does not entitle anyone to claim

- **That the watchdog crash is fixed.** One reset in 32.25 h against six in 34.56 h. `c0` is still
  93% busy. See §3.
- **That greying improved because of this change.** See §5.
- **That Binance never closes at 24 h.** One connection, one host, and not the production one.
  See §4.
- **That the panel was verified against the venue.** `SOAK note: binance publishes no checksum, so
  NO rendered row on this build was ever externally confirmed` — printed 11,597 times in this
  capture, and still true.

## Scoreboard

| claim | verdict |
| --- | --- |
| D-C §1: one continuous stretch > 24 h | ✅ **27.81 h** |
| Stage E §6: crossed lines on the panel → 0 | ✅ **0 of 92,656 LIVE** |
| Stage E §8: reset count recorded whatever it is | ✅ **1**, same signature, separate stage |
| Publish boundary reaches the board | ✅ 7.19 publishes/s against 146.35 events/s |
| Panel fed rather than starved | ✅ 93.8% of published frames drawn |
| 24 h venue close observed | ❌ **not observed** on a 27.59 h connection |
| Grey mechanism addressed | — out of scope (D-A4) |
| Checksum coverage of rendered rows | — none at this venue, by construction |
