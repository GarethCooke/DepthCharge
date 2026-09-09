# Bench 2026-09-07 — D-C run 2's pre-flight, and RE_RESEED's first real board line

**What this is.** M5 stage D-C **deliverable 1** — the pre-flight that must pass before a >24 h
soak is started, per that brief's *"a soak begun against an unproven reader is a day spent
producing a fiction"*. It is **not** a soak reading; none of §4's seven checks is answered here.

**It was an accident, and that is recorded rather than dressed up.** The capture was started as a
few-minute confidence check, the workstation crashed while it ran, and the monitor kept the port
until it died — so what was intended as two minutes is **4.10 h**. Nothing about the reading
depends on the length; the length is why it is worth committing.

## 1 · Provenance

| | |
| --- | --- |
| capture | `hardware/bench-2026-09-07-D-C-preflight.log.gz` |
| source | `firmware/logs/device-monitor-260907-092240.log` (gitignored) |
| gz sha256 | `2344ba451798fb86d7b45b35f09d2c921c19a63e155ad62616baac1acce425db` |
| **inflated sha256** | `61f0a0f2524af02642639685b7ad89ae3a3cd7954fe19c6612686a98e2ecfc52` |
| bytes | 1,151,089 on disk · 8,364,301 inflated · 57,083 lines |
| image | `dab312b`, arm `depthcharge-binance`, flashed 2026-09-07 09:20 at 115200 (`Hash of data verified`) |
| span | 09:22:42 → 13:28:37 · **4.10 h** · **one boot**, `rst:0x1 (POWERON)`, uptime monotonic |

The round trip was **verified byte-identical**, not assumed: the gzip was inflated and compared to
the source before the source was left behind.

**`markers : 0`, and that is a defect of this capture that the run it precedes does not repeat.**
The image cannot name itself — `main.cpp:114` still prints *"DepthCharge M4 stage D"*, now
**backlog D14** — so the commit above is attributed from the desk's own flash record, taken
minutes earlier in the same session. That is stronger than the first D-C run's inference from
behaviour and timing, and it is still not something the file says about itself.

## 2 · Deliverable 1, item by item

All four gaps §2 of the brief opened are closed **on this image**, read off the board rather than
off D-A3's brief:

| item | what the board printed | verdict |
| --- | --- | --- |
| `-- age` non-zero median, threshold ≈ 39,927.94 ms | `server-ping median 19992 ms, grey at 39985 ms after 32 sample(s)` | ✅ gaps (a) and (b) |
| a ping-interval maximum prints | `-- ping : rtt 245 ms (worst 2973 ms, run 2973 ms) \| ping 1418/1418` | ✅ |
| a largest-block reading at reconnect | `socket up: … largest internal before=102388 after=51188 (a session needs 2 x 16717 B)` | ✅ gap (c) |
| every owned regex non-zero | census below | ✅ gap (d) |

**The threshold is 39,985 ms, not 39,927.94 ms, and the difference is not drift.** Both are
`2.0 × median`; the brief's figure derives from the harness corpus median of 19,963.97 ms and the
board's from its own rolling median of 19,992 ms. The quantity is the same and the population is
not — the same distinction §4.2 draws between ten intervals and 6,183.

### The census

```
  SOAK                        1473        -- pipe                     1473
  grey for                     114        -- signal                   1472
  *** STALE                    114        -- age                      1472
  *** LIVE                     114        -- reseed                   1473
  *** STALE (checksum) lenient   0        warm_dns FAILED                0
  socket up:                     1        autopsy assoc=                 0
                                          wifi down|rejoining            0
```

Four grammars matched nothing and **all four are legitimately empty**: Binance publishes no
checksum (`kValidatedDepth = 0`), and there were no DNS failures, no link autopsies and no Wi-Fi
drops in 4.10 h. No grammar that should have matched did not.

## 3 · The finding: `RE_RESEED` has met a real board line

**This is the first time, and it is the whole reason this capture is committed.** Until now every
belief `tools/soak_report.py` held about `-- reseed :` was checked against `render_task.cpp`'s
format string and synthetic text — the tool's own comment says so and names D-C's second run as
its first real capture. `-- reseed` occurs **zero** times in all five previously committed
captures, every one of which predates M5 stage D-A4.

**1,473 lines emitted, 1,473 matched.** Both forms appeared:

```
09:22:50.422 > I (9795)  panel: -- reseed : … triggers=0 below=0 cover=-/- of 448
09:23:00.448 > I (35833) panel: -- reseed : … triggers=0 below=0 cover=990/986 of 448
13:28:31.582 > I (14767161) panel: -- reseed : … triggers=0 below=0 cover=735/646 of 448
```

The first is the **`cover=-/-` sentinel** — `have_seed_bounds_` still false, the low-water marks
still `0xFFFFFFFF` — which the grammar admits via `[-\d]+` and the reader guards with `isdigit()`
rather than `int()`. It was exercised by `--selfcheck` against synthetic text and had never been
seen from hardware. **The pre-flight the brief ordered is exactly what caught it, in the window it
was designed to catch it in.**

**What this does NOT prove.** One boot, so *the boot's last line* and *the last line of the file*
are the same rule: the parse is proven and the **per-boot placement is not**. Segmentation itself
is proven elsewhere — re-run over `bench-2026-08-30-D-C-soak.log.gz` the tool reports 7 boots by
panel clock and 7 by ROM — so what is outstanding is only the combination.

## 4 · Read while the file was open — none of it a §4 check

Recorded because a reader of run 2 will want the comparison, and because a 4.10 h single-boot
capture on this image is itself evidence about the image:

- **No watchdog reset in 4.10 h.** One `POWERON`, uptime monotonic. Backlog **D10** is unresolved
  and this says nothing about whether it is fixed — stage E took one reset in 32.25 h, so 4 h is
  far inside the interval where seeing none proves nothing. It is the shake-out that made this
  image the one worth soaking rather than a fresh rebuild.
- **Grey 11.9 min = 4.85% of uptime**, against the first D-C run's 71.4%. The publish-boundary
  change (stage E) is the obvious candidate and this capture does not isolate it.
- **`cover=` is walking toward the trigger.** 990/986 → 968/915 → 735/646 against the 448 the
  coverage trigger fires below, i.e. the book stayed live long enough to consume ~340 levels of
  seeded coverage without a `drop_book`. Check 7's mechanism is **armed and approaching** on this
  image, which is the first direct evidence that its tail phenomenon is reachable here.
- **`-- signals` is a histogram, not a maximum**, and it answers §6's open question about how to
  expose the ping interval without a second run: `<10s:0 10-15:0 15-18:3 18-20:382 20-22:347
  22-25:4 25-40:0 >=40s:0`. **`>=40s: 0`** — no interval reached the 39,985 ms threshold, and none
  reached 2× the median. That is check 2's falsifier shape, on a 4 h population, and it is
  **not** check 2: D12 asks whether `>=2x med` crossings occur *after calibration completes*, and
  answering it needs the long run.

## 5 · What this capture is entitled to close

- ☑ **Deliverable 1**, in full, on the image that then went on to soak.
- ☐ Nothing else. No §4 check is answered, `§1`'s > 24 h is untouched, and D-A4's board box needs
  `triggers=` and `adopted=`, which are 0 here for a reason that is a verdict on 4 hours.
