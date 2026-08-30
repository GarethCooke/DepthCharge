# M5 Stage D-A3 — the wire and the instruments

**Track:** Agentic [desk] · **Status:** DONE 2026-08-30 — ctest 52/52, six arms build, and all three lines confirmed on the board · **Size:** one desk evening, and the ping wire
is most of it
**Written:** 2026-08-30 by the desk seat. **Split 2026-08-30** — deliverables 5–7 (the re-seed
mechanism, `InFlight`, the header marker) moved to **D-A4**; see § *Why this stage is only four
deliverables*.

**D-A3 is the stage that makes D-C runnable, and nothing else.** D-C's brief established that the
soak has four blockers, and every one of them is this stage's. Two were always named here (the ping
wire, the soak instrumentation), one was named nowhere (routing the per-venue policy), and one had
been drafted into D-C by mistake and belongs here by D-A2's own *Out of scope*:

> The re-seed **mechanism** and its memory, `DisplaySnapshot::reseed` → `InFlight`, the **liveness
> ping wire**, and the **soak instrumentation** — **D-A3**, which is the last desk evening before
> the bench.

**Read first**

| Source | Why |
| --- | --- |
| `M5-stage-D-C-the-soak.md` **§2** | The four gaps, each verified against the tree. This brief is their work order; that one is why they matter. |
| `firmware/src/venue_build.hpp:316-326` | The ping wire's ownership note, written by D-A1 rather than improvised: **what it costs and why it was not done there**. |
| `M5-stage-C-…md` § *Owed by stage D*, **rows 3, 4, 5** | The three checks the instruments have to make readable, with their numbers. |
| `ARCHITECTURE.md` §6 invariant **#8** | The ping wire crosses a task boundary; #8 governs it. |
| `M5-stage-D-A4-…md` | What left this stage, and the ruling §2 hands it. |

**Depends on:** D-A2 ✅ (`83c0bf6`), D-B ✅ (`05e05d6`). **Blocks:** **D-C entirely** — and nothing
else, which is the point of the split.

---

## Why this stage is only four deliverables

**Wall-clock.** The soak is the only thing left in M5 that cannot be compressed: it is a run longer
than a day, and no amount of desk work shortens it. Deliverables 1–4 are exactly what unblock it,
and **all six of D-C's named checks are satisfiable with these four alone** — nothing in D-C's list
needs the re-seed mechanism, `InFlight`, or the header marker.

So those three move to **D-A4**, which becomes a desk evening that **runs while the soak is
running** rather than a day in front of it. The split costs nothing and buys a day. It also keeps
this stage's failure attributable, which is the reason D-A2 gave for splitting D-A3 out in the
first place: the ping wire changes a signature across all three venues, and bundling a memory
decision with it makes a regression unattributable.

## 1 · The four gaps — the soak's blockers

**Deliverable 1 · Wire the liveness ping to the watchdog.** Binance's liveness signal is the venue's
20 s **server ping**, a control frame the adapter cannot see. `venue_build.hpp:325` currently ships
`liveness_count(const Adapter&) { return 0; }`, so `armed_` is never set and `expired()` is
permanently false — five `SOAK` fields are structurally inert as a result (`age`, `worst_age`,
`baseline`, `wd`, and the whole `-- age` reading). **The cost, stated by D-A1 rather than
discovered here:** the count lives in `PingProbe` on the **RX task** while the watchdog is stamped
on the **feed task**, so it crosses a task boundary **invariant #8 governs**, and
`liveness_count`'s signature has to change **for all three venues**. That signature change is the
reason this was not folded into D-A1 or D-A2, and it is most of this evening.

**Deliverable 2 · Route the per-venue liveness policy to the firmware clock. This is a SEPARATE
change from deliverable 1 and no brief before D-C's had named it.** `harness/include/dc_harness/
venue.hpp:298` holds Binance's `{multiple = 2.0, ceiling_ms = 60000.0}`.
`firmware/src/liveness_watchdog.hpp:316` default-constructs `LivenessClock clock_;`, and **no
`LivenessPolicy` appears anywhere under `firmware/`**. So the board runs multiple **4.0**, ceiling
**30,000 ms**, and stage C's derived **39,927.94 ms** threshold — the number D-C's parity check and
multiplier falsifier are both written against — **is not on the board at all**. Wiring the ping
without routing the policy produces a calibrated clock against the wrong constants, which is worse
than an uncalibrated one because it looks right.

**Deliverable 3 · A ping-interval instrument.** D-C's first named check is *"any interval reaching
2 × median on a healthy socket raises k"*. The board prints a **median** and a **sample count**
(`render_task.cpp:824`); the falsifier is about the **maximum**, and `-- ping`'s `worst` / `run` are
**round-trip times**, a different quantity. Emit a maximum beside the median; prefer a coarse
histogram reusing `gap_histogram.hpp`'s existing shape rather than a third one. At a 20 s cadence a
24 h run holds ~4,300 intervals. **`LivenessWatchdog::worst_gap_ns()` already exists and is never
printed** — check whether it is the quantity wanted before adding a counter beside it.

**Deliverable 4 · A reconnect-time largest-block reading, and a repaired `soak_report.py`.**

- `heap_caps_get_largest_free_block` is called in exactly three places — `heap_probe.cpp`
  (periodic), `panel.cpp` (DMA caps, once) and `rest_fetch.cpp` (fetch-scoped). The socket-up line
  at `ws_transport.cpp:637` prints `dns / connect+upgrade / fd / rssi` and **no heap figure**, and
  the `SOAK` line's `largest=` is a 10 s periodic sample. **Sample it where the socket comes up**,
  which is the point D-C's most load-bearing check specifies. Do not reuse the fetch reading: a
  fetch takes the largest block to a few KB by design, and `rest_fetch.cpp:396` already WARNs below
  16,717 B on **every** fetch.
- **`tools/soak_report.py` matches zero lines of a current capture.** The `time` filter's
  `HH:MM:SS.mmm > ` prefix breaks every `^`-anchored grammar; `RE_PIPE` fails **even de-prefixed**
  because the board now prints `no_slot=3 max_held=4 of 4 qfull=0` (`b9a37eb`) against a pattern
  expecting `no_slot=… qfull=…`; and it is guarded by a bare `if pipe:`, so the section **drops
  silently**. Fix all three, **make the guard loud**, and add grammars for `-- age`, `-- ping`,
  `-- frame`, `-- slot` / `-- slots`. **Prove it against a real capture** — a reader that has never
  parsed a real line is not a reader, and this one has been broken through at least two stages that
  believed they were leaving it usable.

## 2 · RULING — the re-seed choice is (a) or (c), and (b) is closed

**Recorded as a ruling rather than an observation, because it is not a new finding.** It is two
decisions already taken, read together, and their signs are opposite.

Stage C priced candidate **(b)**, drop-gap-reseed, at exactly one cost: *"free in memory, greys the
panel for the length of a fetch on a book that was still correct."* **D-B's decision 2 refused
exactly that cost** — the ladder keeps its colour on `InFlight` because greying a book that has not
gone wrong lies in the safe direction for the **3.1–5.6 s** a fetch takes. **The same sentence is
(b)'s price in one document and the thing forbidden in the other.**

The mechanism is arithmetic, not preference: (b) drops the book, a `Gap` reaches the engine,
`status` becomes `Stale`, and the panel greys — keeping its levels, which `book.hpp:106`'s
`mark_stale` retains deliberately *"for the renderer to grey"*, so the picture is a grey ladder
rather than an empty one. Grey either way; decision 2 says coloured.

> **THE RULING. The re-seed mechanism is (a) or (c). (b) is CLOSED.** It is not rejected on cost or
> on taste — it is **unreachable while decision 2 stands**, and D-A4 does not re-derive this.
>
> **AND THE ONLY WAY BACK: reopening (b) means reopening decision 2, deliberately and first.** That
> is the owner's to do. What it may not be is a consequence discovered half-way through an
> implementation — which is the shape this would have taken, because **D-B rejected (b) without
> naming it**, and a stage reading only stage C's table would have found (b) the cheapest option on
> the page.

**This stage records the ruling and executes none of it.** The mechanism is D-A4's.

## 3 · Constraints

- **Invariant #8 governs deliverable 1.** The ping count and the watchdog stamp live on different
  tasks; whatever carries the count across must keep the single-writer property, and the crossing
  must be argued in the log rather than assumed. D-A2's third task is the precedent for how such a
  crossing gets stated.
- **`engine/` stays host-buildable** and free of ESP-IDF includes; the ping wire is firmware-side.
- **Every deliverable here touches `firmware/` or `tools/`**, so per `ARCHITECTURE.md` §9
  (2026-08-30) **ctest does not verify the firmware half** — each firmware commit needs
  `pio run -e depthcharge-binance` in its worktree, with `secrets.h` copied in first
  (`CLAUDE.md`, commit discipline).
- **Do not start the soak**, and do not build the re-seed mechanism. A stage that ends by launching
  a 24 h run has bundled D-C into itself and made a failure unattributable.

## 4 · Known unknowns — resolve and record

- **Whether `worst_gap_ns()` is the ping-interval maximum** deliverable 3 wants, or a different
  silence. It is present and never printed; two counters for one event is how `wd=` already
  disagrees with `LivenessWatchdog::firings()`.
- **Whether routing the policy changes Anvil or Kraken.** Stage C's claim was that they *"do not
  move at all, by construction"* because `firmware/` passes nothing. Deliverable 2 makes
  `firmware/` pass something — so that construction ends, and the two venues' thresholds must be
  **shown** unmoved rather than assumed unmoved. **This is the attribution problem stage C avoided,
  arriving one stage later.**
- **Whether wiring the ping gives `armed_` a second setter.** If it does, M4's card 30 changes state
  and strain 26's remedy (b) reopens. Record the answer either way; it is one line and it settles a
  live dependency.
- **What the header shows during the ~11 minute age no-reading window.** `NOTES` §C.4 says only
  "D's". It becomes visible the moment deliverable 1 lands, so it arrives here even though it was
  never assigned here. If it needs a rendering decision, that is the owner's and not this stage's to
  improvise.
- **The ~302.9 s rx-silence recycle** — sources say "D-A3 or D-C" and choose neither. If it is a
  transport change it is this stage's; if it is only whether it recurs on the working image over a
  day, it is D-C's.

## 5 · Definition of done

- ☑ The ping is wired; `-- age` shows a **non-zero median and a real sample count** on the board.
- ☑ The policy is routed; the board reads **39,887 ms** once calibrated (2.0 × its own 19,944 ms
      median), and **Anvil's and Kraken's are shown unchanged** by assertion and by test.
- ☑ A ping-interval maximum (or histogram) prints, and D-C's falsifier is computable from a capture.
- ☑ A largest-free-block reading is taken **at reconnect** and prints.
- ☑ `tools/soak_report.py` parses a **current** capture — non-zero on every regex it owns — with the
      silent `if pipe:` guard made loud, proved on a real file.
- ☑ Whether `armed_` gained a second setter is recorded. **It did not** — card 30 unmoved.
- ☑ Any decision with architectural weight to `ARCHITECTURE.md` §9.
- ☐ ctest green **and** `pio run -e depthcharge-binance` green in a worktree for every firmware
      commit; session log · ROADMAP; split proposed; nothing committed until approved.

## 6 · Out of scope

**The re-seed mechanism and its memory, `DisplaySnapshot::reseed` → `InFlight`, and the header
marker — D-A4**, which inherits §2's ruling and does not re-argue it. **The soak itself — D-C**,
which this stage unblocks and must not pre-empt. Every rendering decision — **D-B**, closed
2026-08-30. `worst_frame` — closed as the wrong instrument. `kFrameCapacity` sizing — closed by
D-A2. The median convention (card 29), strain 29's tripwire, §3b's defects and the 64,046 B
correction — the **M5 close-out**.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->

### 2026-08-30 · Opus 5 · all four gaps closed at the desk; the board reading is owed

**Done.** All four deliverables implemented and proven on the host. Six firmware arms build; ctest is
**52/52**, up from 50 — two of the new ones are this stage's. **Nothing has been flashed**, so every
DoD clause phrased *"on the board"* is desk-complete and bench-pending.

**Deliverable 1 — the ping is wired, and the wire is the pipe.** `WsTransport::on_ping` increments
`FramePipeStats::server_pings` on the RX task; `feed_task.cpp` differences it. **The count therefore
crosses the RX→feed boundary through `FramePipe`, which is already that boundary** — so invariant
#8's single-writer rule is satisfied by the mechanism that existed rather than by a new one, and the
brief's warning that this "crosses a task boundary invariant #8 governs" cost a field rather than a
design. `liveness_count` now takes `(const Adapter&, std::uint32_t server_pings)` at all three
venues, as D-A1 predicted it would have to.

> **AND THE DEFECT THAT WOULD HAVE SURVIVED THE FLASH.** The old code differenced a local captured
> around `adapter_.on_frame`, so the check ran **only when a message had been dequeued**. At Anvil
> and Kraken that is harmless — their clock *is* a message. At Binance it would have been useless in
> exactly the case the watchdog exists for: the ping arrives while no depth frame does, so a stream
> going quiet would never have been checked and the watchdog would never have armed. The loop's own
> comment predicted it before the wire existed. `service_liveness()` is now called on **both** loop
> paths, and differences a member rather than a local so calling it twice per iteration is a no-op.

**Deliverable 2 — the policy is routed, and it needed a new home.** The Binance policy lived only in
`harness/include/dc_harness/venue.hpp`, which `firmware/` cannot include. Copying it would have been
one edit from silent disagreement, so all three policies moved to a new engine header
`depthcharge/venue_liveness.hpp` that both sides include. `LivenessWatchdog` gained a policy
constructor; `FeedTask` constructs with `venue::kLivenessPolicy`. **Anvil's and Kraken's are shown
unchanged rather than assumed** — `static_assert`s hold the constants equal to the shipping defaults,
and a test holds the *behaviour* equal across four cadences at every step from uncalibrated to
calibrated. Stage C could say "by construction"; this stage cannot, and that was the DoD clause.

**Deliverable 3 — the maximum already existed and could not be printed.** `worst_gap_ns_` is exactly
max(liveness inter-arrival) and was never printed. It also **could not** be: it is 64-bit, and this
file's own rule is that only 32-bit mirrors cross to the render core. And it is **reconnect-
contaminated by design** — `on_socket_change` deliberately does not reset `last_ns_`, so the first
arrival after an outage measures the whole hole, which a test pins. So the instrument is a new
`Histogram<PingScale>` **gated on `armed_`**, admitting only intervals whose two ends are inside one
healthy connection. Ungated, the first reconnect of any run would have parked a ~300 s interval in
the falsifier's bucket and D-C would have read a permanently-tripped falsifier as a finding.
**`PingScale::kFirstLong` is the 40 s bucket**, so `count_from(kFirstLong)` is literally *"how many
intervals reached 2 × median"* — the falsifier as one printed field, not an arithmetic exercise at
the bench. Printed on a new `-- signal` line, named so it cannot be confused with `-- ping`'s
round-trip or `-- feed`'s frame silence: **three quantities, two of which were called `worst_gap`.**

**Deliverable 4 — the reconnect reading, and a tool that read nothing.** The largest-free-internal
sample is taken in `WsTransport::open_socket()` immediately before `esp_tls_init()` — once per
attempt by construction, since that function has one caller gated on an `exchange()`. **It is also
printed on the ENOMEM path**, because the one reconnect where the heap actually ran out never reaches
the `socket up` line. Appended to that line rather than inserted, so the existing grammar's groups
keep their indices. `kTlsBlockBytes` moved to `heap_probe.hpp` rather than being copied.

> **`tools/soak_report.py` matched ZERO lines of a current capture, and there were three
> independent breakages, not one.** The `time` filter's prefix broke every `^` anchor; `max_held=%u
> of %u` had been inserted into `-- pipe` between `no_slot` and `qfull`, so the positional read
> printed **max_held under the label `qfull`** — plausibly and wrongly; and `^\[(\d+)\]` could not
> match `[  9316]` because the core pads millis to width 8, so those two grammars worked **only on
> captures past ~2.8 h of uptime**, which is why they looked fine on the 25 h soak. All fixed, with
> named groups so the next insertion cannot re-label anything, a **loud** regex census replacing six
> silent `if x:` guards, and a `--selfcheck` now in ctest. It parses the current capture and
> immediately reported `pipe max_held: peak 4 of 4 *** REACHED kFrameSlots ***` on a 200 s run.

**Decisions, with why.**

1. **The policy went to `engine/`, not into `firmware/` as a copy.** Two homes for one constant is
   how the board and the harness came to disagree in the first place; the disagreement is silent,
   because the board greys on a threshold nobody reads and the harness asserts one nobody runs.
2. **The histogram is gated on `armed_` and `worst_gap_ns_` is not.** They answer different
   questions — *how big was the outage* and *what is this venue's healthy cadence* — and a test pins
   both, in opposite directions, on the same event.
3. **`dc_tests_binance` was added.** `venue_build.hpp`'s Binance arm was compiled by **nothing**: no
   host target selected `DC_VENUE=3`, so every change to it went green on all four ctest binaries by
   not being compiled by any of them. Its first run found a `-Werror=comment` line-continuation in
   `binance_root_ca.hpp` that had been in the tree unseen since the venue was added.
4. **`#else` became `#elif DC_VENUE == DC_VENUE_ANVIL` in `test_venue_build.cpp`.** `#else` meant
   "not Kraken", which was Anvil while there were two arms and became "Anvil or Binance" the moment
   a third target existed — so every Anvil fact was asserted against a Binance build. **A two-branch
   conditional that names only one of its branches is a trap that springs on whoever adds the
   third.**

**A known unknown, answered: `armed_` did NOT gain a second setter.** It is still assigned in
`on_liveness` alone; what changed is where the counter it differences comes from. So **M4's card 30
is unmoved and strain 26's remedy (b) does not reopen** — the trade stays closed for the reason
stage C gave.

**And a correction to this session's own §9 row, made by doing the work it demanded.** The
2026-08-30 row claimed the host suite *"does not compile `firmware/src/` at all"*. It was asserted
from `CMakeLists.txt:5`'s prose rather than from the link lines: `CMakeLists.txt:341` puts
`firmware/src` on the test targets' include path and host tests reach **sixteen** ESP-IDF-free
headers. The rule and the 26-commit measurement stand; the claim about what was invisible narrows,
and the corrected test is mechanical — *does a host test include this file?* Recorded as a
correction inside the row rather than by editing its sentence, because a row that quietly repaired
its own overstatement would be a third instance of the thing the row is about.

**Exact next step.** **Flash `-e depthcharge-binance` and read three lines**: `-- age` for a non-zero
median and a real sample count, `-- signal` for `n` climbing and `>=2x med` at 0, and `socket up:`
for the `largest internal before=` figure. Then run `tools/soak_report.py` over that capture — the
`-- signal` grammar has never seen a real board line, only the synthetic one in its selfcheck, and
that is the last gap between this stage and D-C's first named check.

**BENCH CONFIRMATION, 2026-08-30 17:57–18:01** — `firmware/logs/device-monitor-260830-175742.log`,
`-e depthcharge-binance`, one boot, 147 KB. All three owed lines read, and the middle one is the
whole stage:

```
-- signal : server-ping n=8 max=22354 ms >=2x med=0 | median 19944 ms threshold 39887 ms CALIBRATED
-- signals: <10s:0 10-15:0 15-18:1 18-20:4 20-22:2 22-25:1 25-40:0 >=40s:0
-- age    : ... | server-ping median 19944 ms, grey at 39887 ms after 8 sample(s) | ...
socket up: dns 78 ms, connect+upgrade 1970 ms, fd 48, rssi -41 dBm
          | largest internal before=102388 after=53236 (a session needs 2 x 16717 B)
```

- **The clock is armed and calibrated on Binance for the first time.** `n=8`, and the signal names
  itself `server-ping` rather than `server-ping (NOT WIRED)`.
- **The policy is on the board.** Before any arrival the threshold read **60,000 ms** — this venue's
  ceiling, not the 30,000 ms global — and after calibration **39,887 ms**, which is 2.0 × the
  board's own median. A default-constructed clock would have shown 4.0 × 19,944 = 79,776 clamped to
  30,000. The multiple is 2.0 on the board, which is deliverable 2's entire claim.
- **The board's median is 19,944 ms against stage C's corpus figure of 19,963.97** — 20 ms apart,
  which is an independent confirmation of the number the whole derivation rests on.
- **The falsifier reads 0** and the distribution is tight: 8 intervals across `15-18`, `18-20`,
  `20-22`, `22-25`, nothing at or beyond 40 s.
- **The reconnect reading works**, and its figure is not the one the D-C check was written against:
  **102,388 B before** the TLS context, where D-A1 measured 17,396 B and sized a 679 B margin. D-A2
  moved FramePipe's slabs to PSRAM and raised the reserve, so the margin is now enormous. **D-C
  should re-derive the margin from this rather than quote D-A1's** — the 16,717 B threshold is
  unaffected, the headroom above it is not.
- **`tools/soak_report.py` parses this capture**, `-- signal` included: 7 matches on the partial run
  and a clean regex census. D-C's first named check is computable from a real board log, which was
  the last gap between this stage and that one.

**One number to hand D-C rather than resolve here.** Worst/median on this connection is
22,354 / 19,944 = **1.12**, where stage C's corpus gave **1.005** over ten intervals. Both are far
below the 2.0 the multiplier clears, so nothing moves — but eight intervals is not a distribution
either, and the soak is what turns this into one. It is exactly the reading D-C's first check exists
to take.

**And a defect in `hardware/BRINGUP.md`, found by following it.** Its flashing remedy — set
`PLATFORMIO_UPLOAD_SPEED=115200` — **does not work on this PlatformIO**: the variable is ignored,
the upload ran at 921600 and failed mid-write in exactly the documented way, leaving the board
partly erased. `pio run` in this version has no `-O`/`--project-option` either. What worked was a
temporary edit of `upload_speed` in `platformio.ini`, reverted immediately and verified against
`HEAD`. BRINGUP is corrected to say so, with the reason the env var was chosen preserved.
