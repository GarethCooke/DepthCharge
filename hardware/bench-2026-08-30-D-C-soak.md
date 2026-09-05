# Bench record — M5 stage D-C: the 34.6-hour Binance run, 2026-08-30/09-01

## The one hard constraint is NOT met, and nothing below changes that

D-C needs **one continuous stretch exceeding 24 hours**, because Binance closes the connection at
24 h by policy and a shorter stretch has not observed the one disconnect the venue guarantees
(brief §1). This capture spans **34.56 h of wall clock** and contains **seven boots**. The longest
continuous stretch is **9.84 h**.

| | B1 | B2 | B3 | B4 | B5 | B6 | B7 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| reset | POWERON | SW_CPU | SW_CPU | SW_CPU | SW_CPU | SW_CPU | SW_CPU |
| starts | 22:32:49 | 02:32:15 | 09:25:38 | 13:40:31 | 15:09:27 | 18:48:29 | 04:39:10 |
| runs | 3.99 h | 6.89 h | 4.25 h | 1.48 h | 3.65 h | **9.84 h** | 4.45 h |

**So the scheduled-disconnect question is untouched.** No connection in this run survived long
enough to meet a 24 h server-side close, and the run therefore closes **none** of D-C's
definition-of-done boxes that depend on the duration. The DoD box *"a single run exceeding 24
hours"* stays open, and so does the milestone.

**The six restarts are crashes, not power glitches.** All six are the task watchdog firing on a
starved `IDLE (CPU 0)` with `dc_feed` occupying core 0, then `abort()` at the **identical**
PC `0x4201c9f8` (`task_wdt_isr`, `task_wdt.c:176`). There are **zero** `Brownout` lines in
490,498 lines of capture. That defect is being chased separately and is **reported here, not
diagnosed** — this record's job is to say what the run does and does not entitle anyone to claim.

**What the run does support is real**, and it is the reason this document is longer than the
paragraph above: **all six checks in brief §4 read**, on 34.55 h of accumulated uptime — four of
them cleanly, one (check 4) not clean, and one (check 5) answered against a premise that turns out
to be wrong in a way only a multi-boot run could have shown. Check 2 is answered on **6,183
intervals against the ten** that set the constant. Those readings are not invalidated by the
reboots: they are per-connection and per-message quantities, not per-run ones. Only the duration
claim dies with the reboots — but it is the claim the stage exists for.

---

## 1 · Provenance

| | |
| --- | --- |
| raw capture | `hardware/bench-2026-08-30-D-C-soak.log.gz` (9,638,192 B gzipped) |
| raw sha256 | `d4c4fd12bf15d6883f9dc2b38a145d26b77e12fcb6572cdf32627622f429c442` |
| raw size | 71,901,480 B · 490,498 lines |
| source | `firmware/logs/device-monitor-260830-223245.log` (gitignored — `.gitignore:27`) |
| first line | 2026-08-30 22:32:49.831 |
| last line | 2026-09-01 09:06:09 |
| capture | 0 port opens, 0 losses, 0 gaps, 0 open failures — **and 0 markers** |
| board | ESP32-S3 DevKit, COM7, 64×64 HUB75, BTC/USDT, `venue=binance` from the banner |
| arm | `depthcharge-binance` — **inferred** (brief §7 names it; the log records no env) |
| image | **cannot name its own commit — see below** |

The committed `.gz` inflates to 71,901,480 B with sha256 `d4c4fd12…`, byte-identical to the source
file; verified, not assumed. The source is gitignored and nothing else preserves it, which is why
it is committed rather than quoted.

**The image is unidentified, and that is a defect of this run.** Deliverable 2 asked for the SHA
to be recorded in the log by hand, because the shipping image carries no build tag (brief §6). No
marker was written: `tools/soak_report.py` reports `markers : 0`. The only identity in 71.9 MB is
`main.cpp:114`'s banner, which reads

```
[main] DepthCharge M4 stage D — venue=binance liveness=server-ping
```

— itself stale text naming the **previous** milestone. What can be said from evidence rather than
inference: the image carries all four of D-A3's instruments (`-- signal`, a policy-derived
`grey at 40007 ms`, a reconnect-time largest-block reading, and a `-- signals` histogram), none of
which existed before `61640bf` (2026-08-30 18:04), and the run began at 22:32 the same evening.
That makes `61640bf`, as shipped in `daa8013`, the only candidate — **inferred from behaviour and
timing, not recorded.** This is the attribution problem of 2026-08-30 in exactly the longer form
brief §6 predicted, and the close-out should decide it rather than the next soak repeating it.

**Every figure in §§3–8 is derived from the capture by script**, either `tools/soak_report.py`
(repaired at D-A3) or by per-boot segmentation of the same grammars, since `soak_report.py` reads
the file as one series and this file is seven. The tool parses this capture and its census reports
non-zero counts on all five grammars it enrols:

```
  SOAK                       12425
  -- pipe                    12424
  -- signal                  12417
  -- age                     12417
  autopsy assoc=                 1
```

No grammar matched nothing, so deliverable 1's reader half is confirmed retroactively, and gaps
(a), (b) and (c) are confirmed from the board's own output rather than from the brief. **Four
grammars the report uses are not enrolled in that census** — see §12.4.

---

## 2 · The seven boots

```
capture: 2026-08-30 22:32:49 -> 2026-09-01 09:06:09   span 34.56 h
boots  : 7   (1 POWERON, 6 RTC_SW_CPU_RST)

  B1  POWERON         22:32:49    3.990 h    up=14363s
  B2  RTC_SW_CPU_RST  02:32:15    6.890 h    up=24794s
  B3  RTC_SW_CPU_RST  09:25:38    4.248 h    up=15283s
  B4  RTC_SW_CPU_RST  13:40:31    1.482 h    up= 5335s
  B5  RTC_SW_CPU_RST  15:09:27    3.650 h    up=13140s
  B6  RTC_SW_CPU_RST  18:48:29    9.845 h    up=35437s
  B7  RTC_SW_CPU_RST  04:39:10    4.450 h    up=16014s

  longest continuous segment: 9.84 h        accumulated uptime: 34.55 h
```

All six restarts share one shape:

```
E (…) task_wdt: Task watchdog got triggered. The following tasks did not reset the watchdog in time:
E (…) task_wdt:  - IDLE (CPU 0)
E (…) task_wdt: CPU 0: dc_feed
E (…) task_wdt: CPU 1: IDLE
abort() was called at PC 0x4201c9f8 on core 0
  #3  0x4201c9f8:0x3fc97420 in task_wdt_isr at …/task_wdt.c:176
```

Six for six, same PC, same starved task, same core. `Brownout` appears **0** times. The backtraces
below frame #3 are `<-CORRUPTED` on five of the six; the sixth — the crash that ended **B4** at
15:09:27 — carries four further frames (`0x4200545c 0x420056f9 0x4200589b 0x4200598e`) and is the
only one with a usable tail. Whoever chases this should start there.

---

## 3 · Check 1 — the largest free internal block, at every reconnect

> **If it ever falls below 16,717 B, the reserve cut is wrong at the second socket.**

**PASS, with margin, and the three stale numbers around it are now re-established.**

D-A3's reconnect instrument (gap (c) in the brief) exists and fires. `ws_transport.cpp:675` now
prints the reading on the socket-up line. **All eight readings in the run:**

| when | dns | conn+upg | rssi | largest **before** | largest **after** |
| --- | --- | --- | --- | --- | --- |
| B1 boot | 68 ms | 1868 ms | −40 | 102,388 | 51,188 |
| **B1 mid-session reconnect** | 40 ms | 1699 ms | −41 | **53,236** | 51,188 |
| B2 boot | 50 ms | 1728 ms | −42 | 102,388 | 51,188 |
| B3 boot | 80 ms | 1772 ms | −38 | 102,388 | 51,188 |
| B4 boot | 131 ms | 1878 ms | −42 | 102,388 | 51,188 |
| B5 boot | 40 ms | 1700 ms | −38 | 102,388 | 51,188 |
| B6 boot | 113 ms | 1908 ms | −40 | 102,388 | 51,188 |
| B7 boot | 94 ms | 1718 ms | −41 | 102,388 | 51,188 |

Minimum **before** across the run: **53,236 B — 3.18× the 16,717 B threshold**. Minimum **after**:
**51,188 B, 3.06×**, identical on all eight. Nothing came near the tripwire.

**The one reading that matters most is the second row**, because it is the only one taken at a
genuine mid-session reconnect rather than at a fresh boot's clean heap: 53,236 B, still 3.18×.

**The case the check guards did not occur, and the log says so directly rather than by inference.**
Brief §5 names it: a half-open socket whose context is still held while the new one is built. Here
the old context was freed first, and three lines in sequence show it:

```
00:50:04.463  autopsy(): socket end #1 [clean-close]: 8228346 ms, 133527569 bytes, …
00:50:04.865  heap: steady … free=113320 (+47484) largest=53236 (+2048)
00:50:07.150  socket up: … largest internal before=53236 after=51188
```

The free total jumps **+47,484 B** at the close, 2.7 s *before* the new socket is opened. So
`before=53,236` is a post-release reading, exactly the "allocates from a much larger hole" shape
brief §5 predicts for an ordinary reconnect (2026-08-11 saw free jump +54,720; this is +47,484).
**The half-open case remains unobserved on this build** — B3 saw two in 25.39 h, this run saw none
in 34.55 h, and the check therefore passes without having been stressed the way it was in M4.

**Re-establishing the three stale numbers.** D-A1's `free 32,244 / largest 17,396` and its **679 B**
margin are superseded: they were measured on the D-A1 build, before the reserve rise and before
FramePipe's slabs moved to PSRAM. **Steady state on this build is `largest = 51,188 B`** — the
dominant plateau in the whole 34.55 h series, and the value the periodic sampler returns whenever a
seed fetch is not in flight. Margin over the threshold is **34,471 B**, not 679 B.

**`kReserveInternalBytes` is confirmed, not moved.** `panel.hpp:265` reads `104u * 1024u`
(106,496 B) and the board reports `reserve=106496`; the brief's warning holds that the recorded
remedy *"goes back to 96 KiB"* is a **reduction** from where the constant sits today. No move is
warranted either way: the check passed at 3.06× on 34.55 h of uptime and eight sessions. **The
threshold itself — 16,717 B — is untouched by this run.**

---

## 4 · Check 2 — the multiplier falsifier

> **Falsifier: any interval reaching 2 × median on a healthy socket raises k.**

**k = 2.0 stands by the rule as written. The margin it stands on is 2.4%, not the 99% stage C
claimed — and that is this run's most consequential number.**

The falsifier bucket is **empty on every one of the 12,417 `-- signal` samples**: `>=2x med=0`,
without exception, run-wide. By the rule as stated, k is not raised.

What the run replaces is the *basis*. Stage C derived k from **ten intervals spanning 111 ms** in a
221 s calibration capture, whose worst healthy multiple was **1.005×**, and concluded the value
"clears the signal's jitter by 1.99×". This run admitted **6,183 healthy intervals** across seven
connections:

```
-- signals (summed over the seven boots' final histograms):
  <10s:1   10-15:30   15-18:110   18-20:3078   20-22:2824   22-25:101   25-40:39   >=40s:0
                                                                                    ^^^^^^^
```

- **39 of 6,183 intervals (0.63%) landed between 25 s and 40 s.** Stage C's ten-interval capture
  contained none, and could not have.
- **The worst healthy interval in the run is 39,061 ms** (B5, at `n=98`, median 20,002 ms,
  threshold 40,004 ms). That is **1.9529 × median**, and it fell **943 ms** short of greying the
  panel on a socket that was healthy.
- **No interval reached 40 s**: `>=40s: 0` on every boot's final histogram, which is why the
  bucket stayed empty and k is not falsified.

So the jitter clearance is **1.024×, not 1.99×.** The two statements — *k is not falsified* and
*the healthy population now reaches 97.6% of the threshold* — are both true, and printing only the
first would be the wave-through this project keeps naming. A dropped ping produces an interval of
exactly 2 × median = exactly the threshold; the healthy population has been measured to within
943 ms of that. **Whether the falsifier as worded is still the right rule is a close-out question,
not a reading this run is entitled to make.** It is recorded here so the close-out has it.

The board's median sits at **19,961–20,021 ms** across the seven connections, against the corpus
figure 19,963.97 — within 60 ms at the extremes and within 40 ms of it on five of seven. Derived
threshold ran **39,830–40,043 ms** against stage C's **39,927.94 ms**. The policy reaches the
firmware: gap (b) is closed, and the board is no longer running multiple 4.0 / ceiling 30,000 ms.

Every server ping was answered in every boot (`ping N/N`, N = 580, 2351, 1447, 471, 1214, 3389,
1537). No unanswered ping occurred, so none of the 25–40 s intervals is a dropped beat — they are
the server's own cadence jitter.

**Known unknown resolved.** Brief §6 asked how the ping interval should be exposed — "a maximum
alone answers the falsifier; a histogram answers the next question without a second run." D-A3
shipped **both**, inside the 128-byte budget: `-- signal` carries `max=` and `>=2x med=`,
`-- signals` carries the seven-bucket histogram. It is what let §4 above be written from one run.

---

## 5 · Check 3 — the frame-pipe reading, in D-A2's order

**`max_held` reached `kFrameSlots` in all seven boots and stayed there. This closes the constraint
the brief says the project has no lever for.**

Read in the order D-A2 finally settled (§4.3): `max_held` first, then `held(>100ms)`, then
`no_slot`, and `slow(>25ms)` / `max_run` last.

| | B1 | B2 | B3 | B4 | B5 | B6 | B7 | run |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **`max_held` of 4** | **4** | **4** | **4** | **4** | **4** | **4** | **4** | **4 of 4** |
| `held(>100ms)` | 5,573 | 7,981 | 5,070 | 2,041 | 4,091 | 11,506 | 5,185 | 41,447 (3.49%) |
| `no_slot` | 8,226 | 9,940 | 6,438 | 4,711 | 8,531 | 10,189 | 4,708 | 52,743 (4.44%) |
| `slow(>25ms)` | 4,777 | 8,295 | 5,180 | 1,426 | 3,002 | 14,959 | 6,526 | 44,165 (3.71%) |
| `max_run` of 4 | 19 | 18 | 11 | 8 | 12 | 19 | 12 | 19 |
| `qfull` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | **0** |

Over **1,188,879 published frames**:

1. **`max_held = 4 of 4`, every boot, no exception.** The leading indicator is pinned at the
   ceiling. It is not a transient — B4's 1.48 h reached it as surely as B6's 9.84 h.
2. **`held(>100ms)` — 41,447 frames (3.49%)** spent over 100 ms in a slot, with per-slot worst
   between 3.92 s and 4.84 s and `p99 = >250ms` in every boot. That is how the pressure built.
3. **`no_slot` — 52,743 drops (4.44% of published)**, distributed across all seven boots and not
   clustered in any.
4. **`slow(>25ms)` 44,165 (3.71%) and `max_run` up to 19 of 4 slots.** `max_run` exceeding the slot
   count by nearly 5× is a stronger statement than the "3 of 4 is explicitly not a safe margin"
   the brief carries forward.

The brief's own reading of this outcome: sustained `max_held=4` closes a constraint with no lever —
fewer slots is contraindicated by strain 27, more by memory. This run supplies the *sustained*.
**It does not, by §7 and §9, license an engine change from here**; if the reading argues for one it
argues for it at the close-out.

`qfull` stayed **0** run-wide, and the named PSRAM slab residue is not contradicted by anything
here.

---

## 6 · Check 4 — `oversize` at `kFrameCapacity` 64 KiB

**Not clean. `oversize` reached 1, and the observed message maximum has moved enough to shrink the
margin from 2.29× to 1.06×.**

- **One oversize event**, in B1, at `up ≈ 10,976 s` (2026-08-31 01:35:26), where `-- pipe` steps
  `oversize=0 → 1` at `published=102,020`. It never moved again — B2 through B7 all end at
  `oversize=0`, and the run-wide maximum of the counter is **1**.
- **The largest message the pipe accepted in the run is 61,823 B** (B3), against `cap 65536`. The
  per-boot maxima are 47,234 / 52,799 / 61,823 / 44,000 / 57,218 / 56,607 / 52,959 B.

Brief §4.4 sizes 64 KiB as **2.29× the largest observed (28,639 B)** and expects this run to
*confirm* the margin on a day's population. A day's population instead put the largest accepted
message at **61,823 B — 94.3% of the cap, a margin of 1.060×** — and produced one message the pipe
declined outright. The old 16,384 B slot overflowed 13 of 3,119 messages (0.417%); this build
overflowed **1 of 1,188,879 (0.000084%)**, a 5,000-fold improvement, so the *sizing decision* is
vindicated in the direction it was made.

**But the check as worded — "`oversize` stays 0" — did not hold, and the headroom sentence behind
it is now stale.** Per §9 the sizing question is closed by D-A2 and this stage does not re-open it;
recorded here for the close-out, with the two numbers it needs: **1 event in 1.19 M**, and
**61,823 B of 65,536**.

---

## 7 · Check 5 — the uncalibrated-default window

**Measured, and the check's premise is wrong in a way that favours the board.**

The brief expects the pre-calibration window "on **every connection**, and the board reconnects" —
60 s of default threshold for the **159.7 s** that `kMinSamples = 8` takes at a 20 s cadence.

| | B1 | B2 | B3 | B4 | B5 | B6 | B7 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| UNCALIBRATED window | 160.3 s | 180.3 s | 170.3 s | 160.3 s | 180.4 s | 180.4 s | 160.3 s |
| `age = -` (no reading) | 651 s | 671 s | 661 s | 651 s | 671 s | 672 s | 651 s |
| baseline latches at | up 661 s | 681 s | 671 s | 661 s | 681 s | 682 s | 661 s |

- **The 159.7 s prediction is confirmed**: 160.3–180.4 s measured, at a 10 s print cadence that
  can only quantise upward.
- **There were 8 connections and only 7 uncalibrated windows.** B1's mid-session reconnect at
  `up = 8,238 s` did **not** re-enter UNCALIBRATED — B1's uncalibrated samples all lie in
  `ticks 35,918 … 186,205`, i.e. the first 186 s of the boot, and the state reads CALIBRATED
  straight through the reconnect. **The window is per boot, not per connection.** The check was
  written expecting the opposite.
- **Nothing visible happened inside those windows.** No grey episode, watchdog firing or socket
  event falls in any of the seven; every one of them sits inside the ~11 min in which `age` reads
  nothing at all, so the 60 s default threshold was never the operative one for anything observed.
- Total cost: **1,192 s of 124,366 s — 0.96% of accumulated uptime.**

**Whether the clock *should* re-derive its median per connection is a real question and not this
stage's** — a threshold carried across a reconnect is a threshold derived from a socket that no
longer exists. Recorded as an observation, per the brief's own framing; it is a candidate for the
"fourth number" the close-out may or may not invent.

---

## 8 · Check 6 — parity's reduced claim, tested

> The panel greys within the calibrated liveness threshold of the **socket** falling silent —
> 39.9 s — and refuses to colour a ladder the feed has never confirmed. It does **not** detect a
> subscription that stops server-side while the socket stays up. `age_ms` is a lag estimate for a
> socket backlog only, and reads nothing for the first ~11 minutes of every connection.

Sentence by sentence:

**(a) "greys within the calibrated liveness threshold of the socket falling silent" — HELD, and the
silent-but-open socket arrived unprovoked.**

*Two different things get called "half-open" in this project and this run separates them.* §3's is
the **memory** case — an old TLS context still held while the new one is built — and it did **not**
occur. This one is the **liveness** case: a socket the transport still believes up while nothing
arrives on it. It occurred, and it lasted 26 minutes. All the liveness-watchdog activity in the run
is in B1:

```
  up=6657s   age=39.9s  worst_age=39.9s  wd=0  sock=0      (threshold 39,978 ms)
  up=6667s   age= 5.0s  worst_age=43.0s  wd=1  sock=0   <- watchdog fires
  up=6707s                               wd=2  sock=0
  up=6797s                               wd=3  sock=0
  up=8068s                               wd=4  sock=0
  up=8108s                               wd=5  sock=0
  up=8178s                               wd=6  sock=0
  up=8238s                               wd=6  sock=1   <- socket layer notices, 1,571 s later
  up=8248s                               connects 1->2
```

The clock crossed its **39,978 ms** threshold and fired; `worst_age` recorded 43.0 s at a 10 s
sample cadence, so detection is inside the threshold plus one sample. The socket layer believed the
connection up for a further **1,571 s (26.2 min)**.

**How it ended is worth the line.** The run contains exactly **one** socket-end autopsy in 34.55 h:

```
autopsy(): [ws] socket end #1 [clean-close]: 8228346 ms, 133527569 bytes, 81516 data / 1122 ctrl frames
           rc=0x0000 (n/a) errno=11 so_error=0 esp_tls=0x0/0x0   rssi=-42 dBm assoc=1
```

The peer closed it **cleanly**, after **8,228 s = 2.286 h** and 133.5 MB — having sent nothing for
the preceding 26 minutes. This is *not* the 24 h policy close D-C exists to observe, and it must
not be read as one; it is a single mid-session clean close at 2.3 h, cause unrecorded. The
supervisor reconnected in **2.0 s** (`socket up, 2000 ms into attempt #2`), the panel was LIVE
4.5 s later, and the grey episode it closed ran **316,703 ms** — the longest in the run. `-- feed`
for B1 ends `wd_gaps=6 sock_gaps=1 connects=2`.

**The liveness clock is the only thing that noticed for 26 minutes.** Without it the board would
have shown a stale-but-coloured ladder for that span, since neither the transport nor the TCP stack
reported anything until the peer hung up. That is the instrument D-A3 wired, doing the job stage C
specified, on the first run that could exercise it.

This adds to M4 B3's table rather than repeating it. B3 saw the half-open twice, with the watchdog
141 s and 291 s ahead; here it is **1,571 s ahead**, an order of magnitude further, and the
"`wd` fired / `sock` survived" cell gains three more instances — the firings at 6,667/6,707/6,797 s
recovered with the socket layer never reporting anything.

**(b) "refuses to colour a ladder the feed has never confirmed" — HELD, emphatically.** `live=1` on
**3,539 of 12,425** SOAK samples: the board showed a coloured ladder **28.5%** of the time and was
grey for the other 71.5%. See §9 — it is grey because it is re-seeding, and it stayed grey each
time rather than colouring an unconfirmed book.

**(c) "does not detect a subscription that stops server-side while the socket stays up" —
UNTESTED.** No such event is identifiable in the capture, and the run offers no way to stage one.
The claim is neither supported nor refuted here.

**(d) "reads nothing for the first ~11 minutes of every connection" — CONFIRMED to the minute.**
`age = -` for **651–672 s (10.8–11.2 min)** on all seven boots, with the baseline latching at the
next sample. The arithmetic closes: the socket comes up at 11.4–13.2 s after reset, and
`kBaselineSamples = 32` at the measured ~20.0 s cadence is 640 s, so the first reading is due at
651–653 s — which is what four of the seven boots print, the other three landing one 10 s sample
later. The two quantities the brief warns not to conflate stay distinct and both read correctly:
**160–180 s** to calibrate the threshold (`kMinSamples = 8`), **651–672 s** to latch the age
baseline (`kBaselineSamples = 32`).

Run-wide age distribution, for the record: 11,888 numeric samples, median **13.7 s**, p95 **29.6
s**, p99 **53.0 s**, max **79.0 s**; 525 samples (4.4%) at or above 39.9 s, 19 (0.16%) at or above
60 s, none at or above 120 s. High-water `worst_age` over the run: **82.0 s**.

---

## 9 · What conditioned every reading: the re-seed storm

Not one of the six checks, and **not diagnosed here** — but no honest reading of the six is possible
without it, because it is the load under which all of them were taken.

```
STALE by reason, run-wide : seq-gap 5,336   resync 7        (5,343 completed episodes)
grey                      : 88,803 s of 124,366 s uptime = 71.4%
                            median episode 18,249 ms, max 316,703 ms, 9 episodes over 100 s
seed fetches              : 5,562 (5,542 OK) -- one every 22.4 s of uptime
bracket                   : ok 5,344 / FAIL 198 / unconfirmed 198, reseeds 5,562
holes                     : 6,030 -- board 413, link 5,158 (85.5%), mixed 459, unknown 0
```

The board spends most of its life detecting a sequence gap, greying, fetching a fresh seed and
going live for a few seconds. The firmware's own hole classifier attributes **85.5% of the gaps to
the link**, printing `LINK-BOUND cadence` on the annotations sampled. `crc_fail` is **0** and
`crc_rows=0 (0.0%)` throughout, which is inert on Binance (`kValidatedDepth = 0`) and says nothing.

The counters total 5,346 grey episodes against 5,343 completed `grey for …` lines; three were still
open when the board went down.

This is a behaviour, on this build, at this desk, on this venue, and it belongs to whoever is
chasing the crash — not to this record. It is stated because **check 3's pipe pressure, check 4's
message sizes and check 6's 71.5% grey are all measurements of a board in this state**, and a
future run in a quieter state may read differently.

---

## 10 · The two readings that look like a check and are not

Both traps in brief §5 were live in this capture, and neither is reported above as a finding.

**(a) The final SOAK line's `largest=7668` is not the heap tripwire.** The last SOAK sample reads
`heap=18360 largest=7668` against `largest=102388` at boot, which has the shape of check 1 firing.
It is not. The periodic sampler runs every 10 s and **lands inside a seed fetch by design**; the
adjacent `rest: fetch` line for the same moment reads `largest 51188/4852/51188` — before, during,
after. Across the whole run the during-fetch largest block has median **4,084 B** and the
before/after medians are both **51,188 B**. The reading that counts for check 1 is the one taken at
the **reconnect**, on the `socket up:` line (§3), and it never fell below 53,236 B.

**(b) The 5,547 `rest: largest internal block … is BELOW the 16717 B` WARN lines are the expected
per-fetch warning, not the check.** `rest_fetch.cpp:396` raises one on every fetch reading below
the threshold, by design, and this run made 5,562 fetches. A 34-hour log full of that warning is
what brief §5 says it will be.

`ARCHITECTURE.md:335`'s "every reconnect AND every fetch" remains superseded by D-A2, and this
record follows D-A2: **the tripwire is the reconnect reading, and only that.**

---

## 11 · Scoreboard

| # | check | reading |
| --- | --- | --- |
| — | **> 24 h continuous** | **FAIL — 9.84 h longest, 7 boots, 6 task-watchdog crashes** |
| 1 | largest free block at reconnect | **PASS** — min 53,236 B at reconnect, 3.18× threshold; steady state re-established at 51,188 B; `kReserveInternalBytes` confirmed at 104 KiB. **Caveat: the half-open memory case it guards never occurred**, so 3.18× is not a stressed worst case |
| 2 | multiplier falsifier | **k = 2.0 stands** — `>=2x med` = 0 over 6,183 intervals; **but worst healthy = 39,061 ms = 1.953× median, 943 ms below threshold**; clearance is 1.024×, not 1.99× |
| 3 | frame-pipe, in D-A2's order | **`max_held = 4 of 4` sustained in all 7 boots**; `held(>100ms)` 3.49%, `no_slot` 4.44%, `slow(>25ms)` 3.71%, `max_run` 19 of 4, `qfull` 0 |
| 4 | `oversize` stays 0 | **NOT CLEAN** — 1 event in 1,188,879; largest accepted message 61,823 B of 65,536 (1.060× margin, not 2.29×) |
| 5 | uncalibrated-default window | **160.3–180.4 s per boot, 0.96% of uptime, nothing visible inside** — and the window is **per boot, not per connection**; the check's premise is wrong |
| 6 | parity's reduced claim | **(a) held — watchdog 1,571 s ahead of the socket layer; (b) held — `live=1` on 28.5%; (c) untested; (d) confirmed at 651–672 s** |

**DoD boxes this run closes:** none outright. It supplies the evidence for the
`kReserveInternalBytes` box and the `k` box, both of which are also gated on the run itself.

**DoD boxes this run leaves open:** the > 24 h run; and with it the scheduled-disconnect question,
which is the reason the constraint exists.

---

## 12 · What the next run needs, learned from this one

1. **Write the SHA into the log by hand before the run starts** (§1). One `###` marker line costs
   nothing and this capture cannot name its own image.
2. **The crash must land first.** Six task-watchdog aborts at a fixed PC make a 24 h stretch
   impossible, and every hour of soak spent before that is an hour that cannot close the box.
3. **`tools/soak_report.py` reads the file as one series and this file was seven.** Its
   `THE RUN` section reports `board uptime 10s -> 16014s (4.45 h)` — the *last boot* — and its
   grey-percentage and watchdog-versus-socket sections silently mix boots. It correctly prints
   `REBOOTS` with all six transitions, so nothing is hidden, but a reader taking the header at face
   value would understate the run by 30 h. **Per-boot segmentation is a tool change for the
   close-out, not a reading**; every per-boot figure in this record was produced outside the tool.
4. **`soak_report.py`'s census enrols five grammars; the report uses nine.** `owned()` is called
   for `SOAK`, `-- pipe`, `-- signal`, `-- age` and `autopsy assoc=`. `RE_STALE`, `RE_LIVE`,
   `RE_GREYFOR` and `RE_SOCK_UP` are read and printed but never enrolled, so if one of them drifts
   its section still goes quiet — which is the precise failure the census was added to remove, on
   the four grammars it does not cover. All four matched here (5,343 / 5,343 / 5,343 / 8), so
   nothing is wrong with **this** report; the gap is in the guard, not the reading. Five `owned()`
   calls at `tools/soak_report.py:175, 328, 380, 381, 428`.
5. **Check 5's wording should follow the firmware**: per boot, not per connection.
