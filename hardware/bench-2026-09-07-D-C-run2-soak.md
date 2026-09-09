# Bench 2026-09-07/10 — D-C run 2, the soak that met §1

**M5 stage D-C deliverables 2, 3, 4 and 5.** One board, one image, 57.26 h of board uptime across
two boots. **§1 is met**: the second boot ran **40.25 h continuous**, and D-A4's board box is
closed. This document is the reading; the capture is committed beside it.

## 1 · Provenance

**THIS CAPTURE IS COMMITTED AS A DERIVED EVENT LOG, NOT AS THE RAW.** The raw gzip is **17,133,772 B
— over the ~10 MB bar in `docs/briefs/SEND-TO-depthcharge-cc-soak.md:20`**, and nearly double the
largest blob this repository has ever taken (D-C run 1, 9.2 MiB). A git blob is permanent and
removing one later means rewriting history, so the rule was followed rather than noted as an
exception. §1.2 says exactly what was dropped and §1.3 proves the drop is lossless.

### 1.1 · What is committed, and what it derives from

| | |
| --- | --- |
| **committed** | `hardware/bench-2026-09-07-D-C-run2-soak-derived.log.gz` — **3,642,785 B (3.47 MiB)** |
| derived inflated | 37,106,317 B · 246,341 lines · **2 markers** |
| derived gz sha256 | `a470803c1275f547f6b1d4ae1804e0dda7ef5606d5cfeaf057e49986426de6f4` |
| derived inflated sha256 | `b2442f51f20c20e424e2f0ca593097c05333f493071b8bfc8b113c1a8dcfe769` |
| **RAW — not committed** | `bench-2026-09-07-D-C-run2-soak-RAW.log.gz` |
| **RAW gz sha256** | `d567d2f2fce9a5155dd76120f27f4af12a65ff9da5072b015cc62f20f7f8d1a6` (17,133,772 B) |
| **RAW inflated sha256** | `edc9c93017b26759873c32cf5e4c5f575ab7cfd5a11c29a1dd50af477b5e520d` (123,644,923 B, 825,408 lines) |
| **RAW kept at** | `C:\local\depthcharge-archive\` — **outside the repository**, copy verified byte-identical against the digest above |
| capture source | `firmware/logs/device-monitor-260907-144913.log` (gitignored) + its `.marker` sidecar |
| image | `dab312b`, arm `depthcharge-binance`, flashed 2026-09-07 09:20 |
| span | 2026-09-07 14:49 → 2026-09-10 00:05 · **57.26 h** board uptime, **2 boots** |
| pre-flight | `hardware/bench-2026-09-07-D-C-preflight.md` — deliverable 1, met before this run |

Both raw digests are pinned so the archive is identifiable, and the raw's **inflated** sha256 is the
one to check a restored copy against — the gz's own digest depends on the compressor.

The marker sidecar is **concatenated at the head**, per the corrected deliverable 2. `markers : 2`
is that mechanism working: this is the first D-C capture that can name its own image. The image
still cannot name itself — `main.cpp:114` reads *"DepthCharge M4 stage D"*, **backlog D13**.

### 1.2 · What was kept and what was dropped

**Kept in full — 246,339 lines of 825,406 (29.8%), 37.1 MB of 123.6 MB (30.0%):**

- every line any `tools/soak_report.py` grammar owns or reads — `SOAK`, `SOAK note`, `-- pipe`,
  `-- signal`, `-- age`, `-- reseed`, `*** STALE`, `*** LIVE`, `grey for`, `socket up:`,
  `warm_dns()`, `assoc=`, the Wi-Fi shapes, `### ` markers and ROM `rst:0x` lines;
- every line this record quotes a figure from that no grammar owns — `-- signals`, `-- ping`,
  `-- size` (**check 4's 65,220 B margin lives here**), `-- seed`, `-- holes`;
- **every `rest:` and `heap:` line**, which carry §5's per-fetch `is BELOW the 16717 B` WARN trap,
  the fetch round-trip figures §6 lists as disputed, and the `heap: steady` series;
- every **event**: the `[ws]` socket-end autopsy block, `supervise()`, `open_socket()`, the
  `task_wdt` / `abort()` / `Backtrace:` block that ended B1, and the boot banner.

**Dropped — 1,404,477 lines, 86.5 MB (70.0%), of three kinds:**

| dropped | lines | ≈ bytes | why it carries no figure |
| --- | --- | --- | --- |
| `panel: v<N>` per-frame ladder lines | 185,701 | 26.2 MB | the 1 Hz ladder echo — no grammar reads it and no figure in any D-C record derives from one |
| unread periodic stats (`-- a`, `-- panel`, `-- event`, `-- arrive`, `-- errors`, `-- rate`, `-- frame`, `-- cpu`, `-- rx`, `-- frames`, `-- slots`, `-- channel`, `-- feed`, `-- reject`, `-- adapter`, `-- slot`, `-- book`, `-- rssi`, `-- hole`) | ~392,000 | 59.5 MB | a 10 s time series nothing in this reading or in `soak_report` consumes |
| blank lines | 825,415 | 0.8 MB | the monitor's line spacing |

**This is a real loss and it is stated as one.** The dropped stats lines are genuine board output
and a future question — frame-time attribution, RSSI over 57 h, the arrival histograms — would need
the raw. **That is what the archive is for**, and its digests are above.

### 1.3 · The proof that it is lossless for this reading

**Asserting "lossless" is worth nothing; this was tested.** `tools/soak_report.py` was run over the
raw gzip and over the derived log, and the two reports were diffed:

> **Every measurement section is byte-identical** — boot boundaries, ROM reset count, the boot
> witnesses' disagreement, `REBOOTS`, the per-boot uptime/grey/wd table, grey episodes, the frame
> pipe table, the largest-free-block series, **the `(b3)` re-seed ledger and its verdict**, DNS,
> the autopsy and Wi-Fi lines, and **the full regex census**.

Two things differ, both by construction:

1. **The PROVENANCE block** — file name, digests, byte and line counts. That is the point of a
   derived log, not a defect in it.
2. **`out-of-order lines: 63` → `58`.** This is the one figure that does not survive, and it is
   named here rather than left to be discovered.

> **AND IT IS NOT LOST DATA — IT IS AN ADJACENCY ARTEFACT, WHICH WAS CHECKED RATHER THAN ASSUMED.**
> The count measures a tick decreasing *relative to the previous line in the file*, so removing
> lines changes it by definition. **All 63 out-of-order lines are `rest:` lines and all 63 are
> present in the derived log** — verified by matching each one — so nothing is missing; five of
> them merely no longer sit next to the `panel:` line that made them look out of order. Preserving
> the figure at 63 would require the derived log to claim an interleaving it does not have, which
> would be a fabrication rather than a fidelity.
>
> **No figure in this record derives from it.** Its purpose is defensive — it stops `boot_starts()`
> mistaking an interleaved line for a reboot — and that defence returned the *same* answer on both
> inputs: **2 boots by panel clock, 11 by ROM**. Recount it against the archived raw if it is ever
> wanted.

| boot | span | grey | wd | connects | ended by |
| --- | --- | --- | --- | --- | --- |
| **B1** | 10 s → 61,253 s (**17.01 h**) | 138.7 min, 13.6% | 0 | 1 | **D10** — `task_wdt: IDLE (CPU 0)`, `CPU 0: dc_feed`, `Aborting`, `abort() at PC 0x4201ce64` |
| **B2** | 20 s → 144,895 s (**40.25 h**) | 415.2 min, 17.2% | 8 | 2 | stopped deliberately on the reading |

**Eleven `rst:0x..` lines against two panel-clock boots**, and the tool flags the disagreement
rather than mis-segmenting. Nine are the monitor's DTR/RTS auto-reset chatter in the capture's
first six seconds; the marker sidecar's second line says so. The tenth and eleventh are B1's
power-on and the watchdog reset that started B2.

## 2 · Definition-of-done scoreboard

| clause | verdict |
| --- | --- |
| §1 — one continuous stretch **> 24 h** | ✅ **40.25 h** (B2) |
| D-A4's board box — `reseed` reaching `InFlight` on the board | ✅ **adopted=5**, `unbracketed=0` |
| Check 1 — largest free block at every reconnect | ✅ **and stressed for the first time**, 3.31× |
| Check 2 / **D12** — `>=2x med` after calibration | ✅ **0**, and the answer does not turn on "healthy" |
| Check 3 — frame-pipe occupancy | ⚠️ `max_held=4 of 4` sustained, as run 1 |
| Check 4 — `oversize` at 64 KiB | ⚠️ **1** event, and the margin is now **1.0048×** |
| Check 5 — uncalibrated window | ✅ observed, 34 of 20,584 `-- age` lines |
| Check 6 — parity's reduced claim | ✅ (a) held on the venue close; (c) still untested |
| **D9** — the venue's 24 h close | 🔵 **first positive evidence, and it is not 24 h** |

---

## 3 · The four questions this reading was asked

### 3.1 · The close is one observation, and the interval is unestimated

```
[ws] socket end #1 [clean-close]: 115961672 ms, 1167663207 bytes, 1158303 data / 16815 ctrl frames
     rc=0x0000 (n/a) errno=11 so_error=0 esp_tls=0x0/0x0   rssi=-49 dBm assoc=1 stack_free=3276 B
```

**One socket, 32.21 h, 1.17 GB, closed cleanly by the peer at 2026-09-09 16:03:11.** This is the
first time this project has observed a Binance close at all — M3's 23.6 h missed it, D-C run 1
never lived long enough, stage E ran 27.59 h and saw none.

> **32.21 h IS NOT THE NEW 24 h, AND THIS RECORD REFUSES TO MAKE IT ONE.** §1 was built on *"the
> disconnect the venue guarantees"* at 24 h. That premise has now been refuted twice — stage E's
> 27.59 h with no close, and this connection running 8.2 h past the documented figure before
> closing. **Replacing it with "Binance closes at ~32 h" would be the same error with a better
> number.** This is **n = 1**, on one host, one venue endpoint, one connection. **The interval is
> unestimated and this run does not estimate it.** What D9 gains is its first *positive* evidence —
> the venue does close, cleanly, and not at 24 h. What D9 does not gain is a period.

A second close would need another ~32 h and would make n = 2, which is still not an interval. If
the question is worth a number, it wants a campaign, not a longer soak.

### 3.2 · The pre-close stretch, and which side of "healthy" it falls on

The lines immediately before the close:

```
-- age    : - (worst 41.7s) | baseline 19998 ms | server-ping median 20032 ms, grey at 40063 ms
-- ping   : rtt 37027 ms (worst 37027 ms, run 37027 ms) | ping 11022/11023 | waiting 65422 ms
-- signal : server-ping n=5790 max=30841 ms >=2x med=0 | median 20032 ms threshold 40063 ms CALIBRATED
```

**Two quantities are being confused if this is read quickly, and the falsifier turns on which.**
`37,027 ms` is a **ping round-trip** off `-- ping`. The falsifier — *"any interval reaching 2 ×
median on a healthy socket"* — is about the **inter-arrival interval** on `-- signal`, which is a
different measurement. At that moment `-- signal max` was **30,841 ms**, and the close window took
it only to **31,675 ms** (1.584× median).

**And the answer does not depend on the healthy/unhealthy judgement at all, which is the cleanest
thing in this section.** The run's **worst signal interval is 35,052 ms**, and it occurred at
**21:30:04 — five and a half hours AFTER the reconnect, on an unambiguously healthy socket.** That
is **1.753 × median**, short of the 2.0× bar by 4,930 ms.

> So: the pre-close stretch is **not** a healthy interval — a socket the venue is winding down is
> not healthy, and the board's own instruments said so 90 s before the close (§3.3) — **but the
> falsifier is not saved by that exclusion.** Include the close window or exclude it: no interval
> in 57.26 h reaches 2 × median. **k = 2.0 stands, and it stands on the strong form of the
> statement.** Recorded here so the next session does not re-derive it.

`>=2x med = 0` on **10,260 calibrated samples** (B1 n=3,061, B2 n=7,199). **All four of stage E's
crossings fell inside the uncalibrated window; this run produced none inside or outside it.** That
is D12's reading, and it is the reading the owner ruled the falsifier's remedy on.

### 3.3 · `wd=8` is one event, not a rate

**All eight firings are inside a 461-second window**, and `wd` was 0 for the preceding 32 h of B2
and the whole of B1:

| # | time | board uptime |
| --- | --- | --- |
| 1 | 16:01:40 | 115,880 s |
| 2 | 16:02:20 | 115,920 s |
| — | **16:03:11 — the venue's clean close** | 115,961 s |
| — | **16:03:14 — reconnected** (`feed down 499 ms`, attempt #2) | 115,974 s |
| 3–8 | 16:05:40 · 16:06:20 · 16:07:10 · 16:07:50 · 16:08:41 · 16:09:21 | 116,120–116,340 s |

**Two before the close, six after the reconnect, none anywhere else in 57.26 h.** The attribution
is unambiguous: this is the venue close and its recovery, not a background rate. Eight greys spread
over 40 h would have been a finding about the liveness clock; eight inside nine minutes around a
socket ending is the clock **doing its job**.

**And the first two are check 6(a) holding on the strongest case yet.** The liveness clock fired
**~90 seconds before the peer closed the socket** — the feed went quiet, the clock crossed its
40,063 ms threshold and greyed the panel, and only then did the transport learn the socket was
gone. Run 1 saw this on an unprovoked stall with a 26-minute transport lag; here the lag was 91 s
because the venue closed cleanly rather than vanishing. **Without the clock the panel would have
shown a stale coloured ladder across the venue's own close.**

The six after the reconnect are the re-seed and re-bracket window: a fresh socket has no book until
a diff brackets the seed, and greying through that is the honest behaviour, not a fault.

### 3.4 · The trigger/adoption gap: 11 against 5, and where the six went

Run-wide `triggers=11`, `adopted=5`, and **every failure counter is 0** — `unbracketed`,
`hold-overflow`, `declined(no-hold)` all zero in both boots. Six triggers are accounted for by
nothing on the line. They are accounted for in the source.

**Per boot, which the run-wide totals hide:**

| boot | adopted | triggers | cover |
| --- | --- | --- | --- |
| B1 | **0** | **3** | 366/495 of 448 |
| B2 | 5 | 8 | 205/262 of 448 |

> **THE TOOL'S VERDICT DOES NOT SAY THIS, AND THE SHORT-CIRCUIT IS WHY.** `(b3)`'s ladder tests
> `adopted > 0` first, so with a run-wide `adopted=5` it printed *"5 RE-SEED(S) ADOPTED ON A LIVE
> BOOK"* and **stopped** — B1's `triggers=3, adopted=0` appears in the table and in no verdict.
> This is the residue check 7's blockquote names, arriving on its first real capture. **Written in
> by hand, as that note instructs.**

**Where the six went.** `cover_triggers` increments **once per seed epoch**
(`binance_adapter.hpp:1695`, guarded by `cover_trigger_latched_`). `reseeds_adopted` increments at
`:1184` — **but only `if (seed_ == SeedState::Seeded)`**. So a trigger whose fetch is still in
flight when a `seq-gap` resync takes the book `Unseeded` has its body adopted as an **ordinary
seed**, not a re-seed: `reseeds_adopted` is not bumped, and **no failure counter is bumped either**,
because nothing failed. `on_reseed_abandoned` (`:788`) is explicit about the same discipline for
its own case — *"`reseeds_requested` is deliberately NOT bumped: this is the same request"*.

**Verified on an instance rather than inferred.** Trigger 3 of B2 latched at 09:02:43; a
`*** STALE (seq-gap)` fired at **09:02:50**, seven seconds later, and `resync_req` climbed
**1,623 → 1,681** across the following 90 s. The adoption that eventually landed was counted
against the *next* trigger (09:09:34), which is why triggers 3 and 4 show one adoption between
them. The same pattern accounts for trigger 6, and trigger 8 (17:34:17) simply had no body land
before the run was stopped 6.5 h later.

> **THE FINDING IS NOT A LOST RE-SEED. It is that `triggers` and `adopted` are not comparable
> quantities, and their difference is not a failure rate.** With `resync_req=2942` over the run —
> one per ~70 s — a trigger being absorbed by the resync path is the *common* case, not the
> exception. Subtracting one counter from the other yields a number that means nothing, and this
> record says so because the next reader will otherwise try. Whether the adapter should distinguish
> *"re-seed body adopted as a seed because the book had dropped"* from *"seed"* is a question for
> the close-out; today it increments nothing, by design, and the design is documented at the site.

---

## 4 · The remaining checks

### Check 1 — the largest free block, and this run earned it

**Three `socket up:` readings, and the third is the one two runs have been waiting for.**

| # | when | context | `largest internal before` | after |
| --- | --- | --- | --- | --- |
| 1 | 14:49:35 | B1 power-on | 102,388 | 51,188 |
| 2 | 07:50:30 | B2 after the watchdog reset | 102,388 | 51,188 |
| 3 | **16:03:14** | **mid-session, after the venue's close** | **55,284** | 51,188 |

> **This is the exact case §5 says the check exists for**, and both previous runs passed it without
> meeting it. D-C run 1 recorded *"check 1 passes without having been stressed the way M4 stressed
> it — worth knowing before anyone treats 3.18× as a proven worst case"*; the pre-flight had only a
> boot-time reading. **Reading 3 is a genuine mid-session reconnect**, and its `before` figure is
> duly *lower* than a fresh boot's — 55,284 against 102,388 — which is the old context still being
> held while the new one is built, exactly the shape the reserve cut is sized for.
>
> **55,284 B is 3.31 × the 16,717 B threshold.** Never near it. **`kReserveInternalBytes` is
> confirmed at 104 KiB on evidence rather than on absence of evidence**, and the stale-remedy
> warning in §4.1 of the brief still stands — `panel.hpp:265` is `104u * 1024u`, so *"goes back to
> 96 KiB"* remains a reduction.

Steady state re-confirmed at **51,188 B** on all three readings, superseding D-A1's 17,396 B / 679 B
margin as run 1 did. §5's two traps were live again and neither is reported as a finding: the
periodic sampler's fetch-scoped dips, and the per-fetch `is BELOW the 16717 B` WARN.

### Check 3 — the frame pipe, reproduced on a 40 h boot

| boot | published | oversize | no_slot | max_held | qfull |
| --- | --- | --- | --- | --- | --- |
| B1 | 607,071 | 1 | 5,308 | **4 of 4** | 0 |
| B2 | 1,430,965 | 0 | 16,466 | **4 of 4** | 0 |
| **run** | **2,038,036** | **1** | **21,774** | **4 of 4** | **0** |

`max_held = 4 of 4` sustained in both boots, as in all seven boots of run 1. `no_slot` is 1.07% of
published here against run 1's 4.44%. Per §7 and §9 this **does not license an engine change from
this record**; it is the third independent run to supply the *sustained* the brief said would close
a constraint with no lever.

### Check 4 — `oversize` is not clean, and the margin has narrowed sharply

**One `oversize` event** (B1), in 2,038,036 published frames. But the reading that matters is the
largest accepted message:

```
-- size   : msg min=128 max=65220 B (cap 65536), slots 4
```

> **65,220 B of 65,536 is a margin of 1.0048×.** §4.4 of the brief states 2.29× over 28,639 B; run
> 1 superseded that with **1.060×** over 61,823 B and ruled the sizing *"closed and vindicated"*.
> **This run narrows it again, to under half a percent.** Three populations, one quantity, moving
> one way: 2.29× → 1.060× → **1.0048×**.
>
> §9 keeps the sizing closed and this record does not re-open it. **But the trend is the finding**,
> and it is recorded with its three numbers so the close-out can decide whether a constant whose
> observed margin halves with every population increase is still *"closed"*. The overflow **rate**
> continues to vindicate the decision — 0.417% at the old 16,384 B slot, 0.000049% here.

### Check 5 — the uncalibrated window

**34 of 20,584 `-- age` lines** report fewer than 8 samples — 0.17%. Per boot the window is the
~160 s `kMinSamples = 8` takes at a 20 s cadence, and run 1's finding is reproduced: **it is per
boot, not per connection.** B2's mid-session reconnect at 16:03:14 did **not** re-enter
UNCALIBRATED, so a threshold derived from a socket that no longer exists was carried across the
venue's own close. Whether the clock *should* re-derive per connection remains the close-out's
candidate fourth number; this run strengthens the case by supplying the first reconnect on which it
actually mattered.

### Check 6 — parity's reduced claim

**(a) holds, on the best case this project has captured** — §3.3. **(b) holds**: `live=1` at the
end of both boots. **(c) remains untested** — no server-side subscription death occurred that is
identifiable as one; the venue's clean close is the opposite case, an honest end rather than a
silent one. **(d)** the ~11-minute no-reading window is unchanged in mechanism.

### Check 7 — D-A4's board box, closed

```
-- reseed : adopted=5 unbracketed=0 hold-overflow=0 | declined(no-hold)=0 adoptable=0 | triggers=8 below=0 cover=205/262 of 448
```

**Five re-seeds adopted on a live book**, at 10:44:04, 15:15:49, 09:09:34, 09:36:25 and 12:49:16 —
`unbracketed=0`, `hold-overflow=0`, `declined(no-hold)=0` throughout. **`DisplaySnapshot::reseed`
reached `InFlight` on the board**, five times, and reconciled every time. That is the outcomes
table's *"fired and adopted — YES, the stage's claim, confirmed"*, and it is not a single-event
fluke.

**`adopted` climbing while `greys` stays flat is the claim, and the honest version is narrower.**
Greys did not stay flat — 2,884 episodes in B2, 17.2% of uptime, on a board carrying `resync_req=2942`.
What `reseeds_adopted` guarantees is stronger and more specific than the grey rate: by construction
at `:1184` it counts **only** bodies adopted onto a still-`Seeded` book, so all five landed on a
live book rather than after a `drop_book`. The resync storm is real and is not what D-A4 was about.

`below=0` in both boots — the seed never arrived under its own margin, so the trigger was armed
throughout and the mechanism was genuinely exercised rather than configured out.

---

## 5 · What this run does not entitle anyone to claim

- **That Binance closes at ~32 h.** One close, n = 1. See §3.1.
- **That the watchdog crash is fixed.** B1 died of it at 17.01 h. **D10** stands, unowned, and this
  run is its third measurement: 6 aborts in 34.56 h (run 1), 1 in 32.25 h (stage E), **1 in 57.26 h**
  here. The rate is falling and the defect is not gone.
- **That `k = 2.0` is proven.** It is *unfalsified* on 10,260 calibrated samples across two
  populations. §4.2's falsifier is a one-way test.
- **That the frame-slot count is safe.** `max_held = 4 of 4` is sustained, again.
- **That the panel was verified against the venue.** `SOAK note: binance publishes no checksum`,
  printed throughout, and still true.
- **That check 7's mechanism is exercised under load.** Five adoptions in 57 h is the tail
  phenomenon behaving as predicted, not a stress test of it.
