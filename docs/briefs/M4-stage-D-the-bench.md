# M4 Stage D — the bench

**Two sittings, two owners.** Part A is agentic and desk-only; Part B is bench and yours. They do
not share an evening, and Part B does not start until Part A's board has been proven to build and
run.

This is M4's last stage. It opens with the riskiest deletion in the milestone — one that changes
when the panel greys, replacing behaviour established over M3's 23.6-hour soak, and which no host
test can check.

**Read first**

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §5, §6, §9 | The staleness ruling, the age definition, the absence rule, the mutation and coincidence rows. |
| `DESIGN.html` §08 | The host/target age divergence residue and its tripwire. Both close in Part A. |
| Stage C's session log | Policies are byte-identical at depth 25 into 27 rows. The depth numbers Part B decides on. |
| B2's session log | `resync_wanted()` exists and nothing in `firmware/` reads it. |
| M3's soak record | 23.6 hours. Part B's soak claims parity against that number or states why not. |

**Depends on:** C ✅. **Closes:** M4.

---

# Part A — preparation *(Track [A], CC, desk only, one evening)*

**Part A's job is to hand the bench a board that builds, flashes and runs. It is not to prove the
board correct — that is what the bench is for.**

## A1 · Item 8, the deletion

Both D tripwires close here, and a partial lift is worse than none.

- Lift `age_estimator.hpp` and `liveness_clock.hpp` into the firmware build.
- Delete `firmware/src/staleness.hpp`, and with it `AgeText`'s duplication of `SecondsText`.
- Delete `kRxWatchdogMs`. The venue table holds no duration, so this is a deletion, not a
  reconciliation.
- Rewire the RX watchdog onto the calibrated liveness threshold. **Host and target now compute the
  book's age with the same code** — record in DESIGN §08 that the residue is closed and by what.

## A2 · Firmware acts on `resync_wanted()`

Today a CRC failure on the board would grey permanently rather than heal. The latch exists and
says the right thing; nothing reads it. Wire it: drop the book, resubscribe, wait for the fresh
snapshot. This is a wiring gap, not a vocabulary gap — §4 does not move.

## A3 · A Kraken build that flashes

Compile-time venue selection, default stays Anvil, Kraken build proven to compile and flash.
Runtime switching is M7 and does not arrive early.

## A4 · Instrumentation the bench can actually read

The bench judges with its eyes and its evidence has to survive the evening.

- On the panel or its header, whatever fits: age, liveness state, rows filled and unknown.
- On serial, a line per interval carrying the same, plus resyncs, disconnects, greys, max age,
  free heap and uptime — so an unattended soak produces a file rather than a memory.
- The depth question needs one number the bench cannot compute at a glance: **rows within the
  checksum's reach.** Stage C measured 37.0% for top and thinned against 11.9% for largest at
  depth 100; the board should report its own.

## A5 · A short proving run, not a soak

Thirty minutes on the desk, Kraken build, panel up, serial captured. **Part A's DoD is "it built,
it flashed, it ran and logged for thirty minutes" — not "it was correct."** Report the serial file
and stop.

**Part A constraints:** §4 and §5 frozen; no window policy chosen; no rendering decisions; commit
nothing; code review before the split; per-commit verification in a detached worktree with
`CMAKE_HOME_DIRECTORY` confirmed.

---

# Part B — the bench *(Track [B], yours, one evening plus an unattended soak)*

**Record, decide, work-order. Do not fix at the bench.** A finding written down at midnight is
worth more than a patch made at midnight, and this project's discipline has held for eleven
stages precisely because the seat that decides is not the seat that types.

## B1 · The criterion, which is already written

- MINA/GBP holds colour through 26 s of legitimate book silence.
- The panel greys within the calibrated liveness threshold of the heartbeat stopping — cut the
  connection and time it. Expect ~4 s at Kraken, ~2 s at Anvil.
- A CRC failure heals rather than greying permanently (A2's wiring, first sight on hardware).

## B2 · Three decisions that need eyes

1. **Uninitialised rendering.** 27 rows of unknown, or grey? Stage C made the state explicit and
   deliberately did not resolve it, because the two candidates differ in nothing a host test can
   assert. Decide at desk distance.
2. **Where the age goes.** The value slot currently carries last-price and stale-reason. Age is a
   third claimant on the same pixels.
3. **Whether to subscribe deeper than the panel can draw.** Stage C's finding is that at depth 25
   into 27 rows nothing is dropped and the policy choice is not a choice — the question only
   exists above the panel's height. Decide with the checksum-reach numbers in hand, and note that
   whoever owns this also owns D6.

## B3 · The soak

Parity with M3's 23.6 hours, or a stated reason for less. What it must produce: false greys,
genuine greys, resyncs, disconnects and reconnects, max observed age, heap trend, and whether the
Kraken connection survives unattended overnight at all — **that last one is untested and is the
new risk.** M3's soak proved an Anvil endpoint; Kraken is different TLS, a different keepalive
regime, and a venue with its own opinions about idle sockets.

## B4 · Absorb the M3 bench residues while the board is out

D1 (rejoin re-rolls the mesh lottery), D2 (weak-node one-hour soak), and the `clkphase = false`
ghosting re-check are all bench items waiting on a bench evening. This is one. Folding them in
costs an hour and saves an evening.

---

## M4's definition of done

M4 is complete when the panel renders a Kraken book off the wire, greys within the calibrated
liveness threshold when the heartbeat stops, holds colour through 26 s of legitimate book silence,
heals from a checksum failure rather than greying permanently, and shows a book age that is a lag
estimate rather than a time-since-anything. Each clause belongs to exactly one stage, which was
the test of whether the split was drawn in the right places.

## Out of scope

Binance (M5). The enclosure and the encoder (M7). The runtime venue toggle (M7). The client ping
(M6). D6's present-masks, priced and parked. Any code change made at the bench.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     measured figures / evidence · exact next step. -->

### 2026-08-20 · Opus 5 (1M) · **Part A done — A1, A2, A3, A4. A5 NOT run: nothing has been flashed.**

**The board that Part B benches exists, builds, and has never been powered on.** Everything below
is desk work, and the split's whole point is that the desk cannot promote it. Nothing is committed;
the split is proposed at the end of this entry and per-commit verification runs on approval.

---

#### A1 · Item 8, the deletion — done

`sample_window.hpp`, `liveness_clock.hpp` and `age_estimator.hpp` moved from
`harness/include/dc_harness/` to `engine/include/depthcharge/`, namespace `dc::harness` →
`depthcharge`. `firmware/src/staleness.hpp` and `harness/tests/test_staleness.cpp` are **deleted
whole** — `StalenessEstimator`, `SecondsText`, `kSummaryPeriodUs`, `drain_percent`,
`premise_suspect()` and the `AHEAD` warning went with them. `kRxWatchdogMs` is gone, and so is the
`static_assert` that tied it to `kHoleThresholdUs`.

**The destination is `engine/`, not `firmware/`, and the brief left it open.** DESIGN strain 23's
owner paragraph and B1's log both said `engine/include/depthcharge/`; strain 17's older verdict said
`firmware/`. `engine/` is the only choice that satisfies both ends — the firmware's include path is
`-I ../engine/include` and nothing else, and the harness reaches `<depthcharge/…>` through
`dc_engine` already. It also buys the target-compile proof for free: the root `CMakeLists` globs
`engine/include/*.hpp` into `dc_engine_target_check`, so **all three headers now compile under
xtensa GCC 8.4 at `-Werror -fno-exceptions -fno-rtti` on every host configure** (19 xtensa objects,
was 16). That answered the two questions nobody could answer by reading: `AgeText`'s `%u` under
`-Wformat -Werror`, and GCC 8.4's libstdc++ on `std::sort`/`std::array`/`numeric_limits`.

**It is three files, not the two the brief names.** `age_estimator.hpp` includes both of the others;
lifting two of three leaves the firmware unable to resolve `dc_harness/sample_window.hpp`, which is
the partial lift B1 warned is worse than none.

**The rewire is a behavioural inversion and it lives in a new host-tested header.** The brief says
of it that "no host test can check" it. That is true of the *behaviour* and not of the *policy*, so
`firmware/src/liveness_watchdog.hpp` holds the rule and `harness/tests/test_liveness_watchdog.cpp`
tests it — the same split `ws_supervisor.hpp` and `gap_histogram.hpp` already use. What is left in
`feed_task.cpp` is a FreeRTOS queue timeout and an `esp_timer` reading. **Part B still owns the
transition**; what the desk closed is the divergence, not the behaviour.

**What the deletion cost, measured against a build of `HEAD` in a detached worktree** — because M3
banked the opposite as a property and a silently expired property is worse than a stated regression:

| | pre-change (`1e1214b`) | after A1–A4 (Anvil) | Kraken build |
| --- | --- | --- | --- |
| flash | 877,053 B | **880,885 B** (+3,832) | 883,357 B |
| RAM | 142,048 B | **144,488 B** (+2,440) | 152,816 B |
| `feed_task.cpp.o` `.text` | 6,708 B | **8,806 B** | 10,823 B |
| soft-float helpers in `feed_task.cpp.o` | **none** | `__divdf3 __fixdfdi __gedf2 __gtdf2 __ledf2 __ltdf2 __muldf3 __subdf3` | same |

The RAM figure is almost exactly the 2,344 B of window the two clocks carry. **M3's streaming-parser
acceptance recorded "zero floating-point instructions in the disassembly" as a fact about this
firmware; that fact is now false, on purpose,** and it is written into DESIGN's footer and
ARCHITECTURE §9 rather than left to be discovered. The *runtime* cost is not measured and cannot be
from here — it lands in `worst_parse_us`, which the board already prints, so A5's run produces the
number instead of an argument about it.

**One exactly-equivalent optimisation taken:** `LivenessClock::threshold_ms()` is now cached and
refreshed in `on_liveness()`, because the value is a pure function of the interval ring and the ring
changes in exactly one place. Without it the feed loop would `std::sort` 32 doubles on **every queue
wake** to compute a number that can only change 1–2 times a second. The equivalence is a test
(`the cached threshold equals the recomputed one at every step`, over a randomised walk), not an
argument, and the mutation for it goes red.

**Strain 23 closes. Strain 22's first clause closes. Strain 17's residue (1) closes. Strain 10 is
CORRECTED rather than executed** — its prescribed resolution ("move the host driver onto the
event-armed rule") predates the 2026-08-17 ruling and contradicts it, so doing as it said would have
propagated the wrong rule to both ends. What remains of it is smaller and now stated: the replay
driver still arms on any-record silence, the firmware on liveness silence, they agree on every
committed input, and aligning them is a brief of its own because it moves golden-covered code.

#### A2 · The firmware acts on `resync_wanted()` — done, and then redesigned by review

Shipped: `firmware/src/resync.hpp` (`SubscriptionSignal` + `ResyncPolicy`, both host-tested), the
transport's `maybe_subscribe`, and `send_text` built on a new `build_masked_text` in `ws_frame.hpp`.

**The first design was edge-triggered and review killed it. That is the most important thing in this
entry.** The adapter latched, the feed task consumed the latch into a one-shot flag, the transport
consumed that with an `exchange(false)`. **Four** paths destroyed a request that nothing could ever
re-raise — because `KrakenAdapter::verify_checksum` can only latch while the book is *baselined*,
and dropping the book clears that:

1. the 5 s floor refusing a second mismatch;
2. a request arriving with no socket;
3. a request arriving mid-heal;
4. **the liveness watchdog firing over a LIVE socket** — which drops the book and deliberately
   latches nothing, on the reasoning that "a reconnect subscribes on its own". True for a dead
   socket. A 4 s RF fade is not one, and this board has measured 3.9 s fades that killed nothing.

Every one of them ends with the panel grey over a healthy heartbeating socket **for the rest of the
run** — the five-minute silence recycle cannot save it, because the heartbeat is data and keeps that
clock fresh. That is precisely the failure A2 exists to remove, reintroduced by A2.

The signal is now the **level** `!adapter.has_baseline()`, republished every frame and polled. A
refusal costs nothing. Writing the tests for it then exposed a second bug in the fix — the level is
necessarily true from the subscribe until the snapshot lands, so a floor anchored on *completed
heals* (zero on a fresh connection) lets the next pass unsubscribe the subscription just made. The
floor now anchors on the last **subscribe frame**, opening one included, which covers the snapshot's
flight time with the same number and is the honest formulation anyway.

**`refused()` is wired too, and was not before.** `KrakenAdapter`'s contract says the feed task turns
a refused subscribe into `die()` so the supervisor retries; nothing in `firmware/` read it. A
delisted pair or an unsupported depth would have left a live socket over an empty book with the panel
reading NO LINK and no counter naming why — the exact state stage 0 measured when `depth: 27` was
refused.

**The two numbers B2 declined to choose:** `kResyncGapUs` = 1 s (the committed resync slice used
~985 ms and produced a byte-shape-identical mid-stream snapshot — the only evidence there is) and
`kResyncMinIntervalUs` = 5 s (a heal costs a measured 3,548 ms book hole; `capture_kraken.py`'s
`MIN_RECONNECT_GAP_S = 5.0` is the only Kraken-side pacing number this project has; the endpoint is
behind Cloudflare). **Neither is measured. Both are stated so the bench can falsify them.**

#### A3 · A Kraken build that flashes — built, not flashed

`DC_VENUE` in `firmware/src/venue_build.hpp`, defaulting to Anvil; `[env:depthcharge-kraken]`;
`dc_tests_kraken` as the host arm, following the `DC_WS_PING` → `dc_tests_noping` precedent exactly.
Also `kraken_endpoint.hpp` and `kraken_root_ca.hpp`, and three VS Code tasks.

**The TLS anchor was measured, not assumed, and twice.** The OS verifier reports leaf ← WE1 ← GTS
Root R4 ← GlobalSign; a hand-parsed TLS 1.2 `Certificate` message with no trust store consulted shows
what the server actually sends: **three certificates, ending with GTS Root R4 cross-signed by
GlobalSign**. Either root would validate, so the tiebreak is lifetime — GlobalSign expires
**2028-01-28**, the self-signed GTS Root R4 **2036-06-22**. The pin rests on the two R4s sharing a
public key, so that was checked rather than assumed: the 97-byte P-384 point from the self-signed R4
appears byte-for-byte in the cross-signed certificate the server sent, and **in neither of the other
two**. It is the first ECDSA anchor in this firmware; if the handshake fails with "feature
unavailable" rather than a verification error, that is the thing to check.

**The subscribe frame is pinned against the committed corpus, byte for byte.** Its bytes existed only
in `tools/capture_kraken.py` until now; a C++ spelling differing in key order would still be
*accepted* by Kraken while silently no longer being the frame the traces were captured with. The
assertion is against the trace text itself rather than a second literal, and the mutation for it goes
red. `static_assert`s read the depth back out of the JSON and compare it to
`kKrakenSubscribeDepth`, so the subscribe and the adapter cannot disagree.

**MINA/GBP, because the criterion chose it** — 25,843 ms of legitimate book silence is the number the
whole staleness ruling rests on, and BTC/USD would never exercise it.

#### A4 · Instrumentation — done

* **Panel:** the age is drawn in the header, priority **value > age > symbol**, no ladder row moved.
  `kLevels` stays 26 and every pinned row constant is untouched. **This is provisional and Part B
  decision 2 still owns it** — a second header line would cost *three* levels a side, not one, which
  is why it was not taken. Cost stated: with a 7-character price the symbol id is dropped; with
  MINA/GBP's 4-decimal prices and a one-digit id all three fit (`2 · 1.5s · 0.1234` = 4 + gap + 19 +
  gap + 29 px).
* **Serial:** one `SOAK …` line per 10 s block carrying venue, uptime, live, age, worst age,
  baseline, grey episodes and total grey ms, watchdog/socket gaps, connects, rows filled/unknown,
  **rows within the checksum's reach** and its percentage, resync raises, heals, `owed`, refusals,
  CRC failures, free heap, largest block, frames and drawn. Every field is a run total or extreme, so
  two lines an hour apart subtract. `grep SOAK` is the whole reading protocol. On Anvil it is
  followed by a line saying **in words** that this venue publishes no checksum, so no rendered row was
  ever externally confirmed — a `crc_rows=0 (0.0%)` reads as a failure, and that is the absence rule.
* The `-- age` line is rewritten: `drain %` and `summary N of M` went with the estimator that divided
  by a hardcoded 500 ms. It now prints the windowed estimate's own working — age, worst, this
  connection's **baseline**, the venue's rolling **median** and the **grey-at** threshold derived
  from it. The last two must read differently from the baseline, or nobody will believe they measure
  different things.
* **Grey is COUNTED, not inferred.** It used to be readable off `event_gaps`' >1 s column, which
  worked only while the threshold and that bucket edge were the same number. They are not any more —
  the threshold is ~2 s at Anvil and ~4 s at Kraken while the edge is a fixed measurement scale,
  deliberately unmoved so a hole bucketed today is comparable with one bucketed during the 23.6 h
  soak. A derived number that quietly stops being derivable is worse than no number.

#### A5 · The thirty-minute proving run — NOT RUN

**Nothing has been flashed.** Both images build; neither has met the board. Two things the bench
should know before it starts:

* **`platformio.ini` pins `upload_port`/`monitor_port` to `COM7`, and the CP210x currently
  enumerates as `COM3`.** Left alone this is the first five minutes of the evening.
* The Anvil default build is unchanged in behaviour except for A1's rewire, so it is the honest
  control: flash `depthcharge` first, confirm the panel still comes up and the SOAK line reads, then
  flash `depthcharge-kraken`.

---

#### The review, which found more than the implementation did

Run as a six-dimension adversarial pass with three independent refuters per finding. **42 raw
findings; three of them critical, and two of those were the same defect found from two directions.**
The criticals are written up under A2 above — they are the edge-vs-level class, and they would each
have produced a dead terminal over a healthy socket.

Fixed in this sitting, beyond the criticals: the stall/reconnect baseline confusion; the threshold
floor sized against the wrong quantity; a backwards stamp regressing the watchdog's deadline;
`refused()` unwired; `last_subscribe_us_` surviving its socket; the console walking the estimator's
256-entry ring and a `double` baseline across the core boundary (it now reads the age from the
`DisplaySnapshot` that crossed correctly, and 32-bit mirrors for the rest); `send_text` having no
host coverage at all; 125 spelled three times for two different facts; `kHasSubscription` governing
nothing while five raw `#if`s decided the behaviour; the reject log's `SPLIT@` scan hard-coded to
Anvil's frame opening and therefore a permanent no-op on the Kraken build; `dc_tests_kraken` not
compiling the header its CMake comment claimed it did; steady-state `ESP_LOGx` lines over 64 bytes
on the RX task; and a handful of assertions that mirrored a definition and could not go red.

**Two findings are recorded and NOT fixed**, because both are wider than this stage:

1. **`kFrameSlots = 4` and the reassembler's whole-message drop are justified in-comment by an
   *Anvil* property** — "book frames are idempotent full replaces". At Kraken a dropped `book/update`
   is a lost amendment. It is not silent — the CRC32 catches it and A2's heal repairs it, which is
   the system degrading correctly — but the comments name the wrong reason and the slot count was
   never sized for a delta venue. Kraken's largest observed message is 1,841 B against a 16 KiB slot,
   so a venue-selected capacity would buy more slots for less RAM. **Bench item / M5.**
2. **`kParseStatusCount`'s `static_assert` anchors on the last enumerator**, so a status *appended*
   to either engine enum compiles clean and is silently uncounted. Closing it properly wants a
   trailing `Count` member on the engine enums, which would make every exhaustive `switch` warn.
   Recorded in the header as a known limit.

#### Evidence

* **Host: green from a WIPED build tree.** 33 ctest; `dc_tests` **367 cases / 895,787 assertions**;
  `dc_tests_kraken` 8; `dc_tests_streaming` 50; `dc_tests_noping` 4. 19 xtensa header objects.
* **Firmware: both images build.** `depthcharge` 144,488 B RAM / 880,885 B flash;
  `depthcharge-kraken` 152,816 / 883,357.
* **Mutations: 20 run, 20 red.** Two of the first ten survived their first attempt and both were real
  coverage gaps rather than filter accidents — one was a doctest `-tc` pattern containing a comma
  (which selects nothing and reports success), and the other showed that the age's own fit test is
  redundant with `draw_text`'s clipping while the space it *reserves* is what the symbol yields to.
  Both are fixed and both now go red.
* **The goldens are unchanged through the lift**, which is the proof that moving the clocks altered
  no arithmetic.

#### Exact next step

**Part B, at the bench, with the board.** In order:

1. Fix `upload_port`/`monitor_port` (COM3), flash `depthcharge`, confirm the control still runs.
2. Flash `depthcharge-kraken`. Watch the first sixty seconds on serial: the boot line names the
   venue, then DNS, TLS against GTS Root R4, the upgrade, `subscribe sent (#1)`, the ack, the first
   snapshot, and `SOAK … live=1`. **Every step that can fail says so on that log.**
3. Time the three B1 criteria with a stopwatch: colour held through 26 s of MINA/GBP book silence;
   grey within ~4 s of the heartbeat stopping (cut the connection); a CRC failure healing rather than
   greying for ever.
4. Then B2's three decisions, B3's soak, and B4's M3 residues.

**Part A's DoD is "it built, it flashed, it ran and logged for thirty minutes". Two of those four are
done.** The flash and the run are the bench's, and this entry does not claim them.

---

### 2026-08-20 (later) · Opus 5 (1M) · **A5 ran. Part A is complete.**

The board was flashed and the thirty minutes were captured. **A5's DoD — "it built, it
flashed, it ran and logged for thirty minutes" — is met**, and the run produced more than the
brief asked it to.

Raw capture: `firmware/logs/kraken-a5-20260820.log` (5,194 lines, 1,860 s, one continuous
monitor, no reset). Not committed — `/firmware/logs/` is gitignored, and the precedent for
quoting a raw log rather than committing it is `hardware/bench-2026-08-09-ws-reconnect.md`.

#### The port note in the entry above was WRONG, and the correction matters

That entry says `platformio.ini` pins COM7 while the board enumerates as COM3, and calls it
the first five minutes of the bench evening. **It is the other way round.** COM3 is a
Silicon Labs CP210x belonging to something else on this desk; COM7 is a CH343 and it is the
DepthCharge board — `esptool chip_id` answers `ESP32-S3 (revision v0.2), MAC
10:10:d9:4a:56:d4` on COM7 and refuses COM3. `platformio.ini` has been right all along and
needs no edit. Recorded rather than quietly deleted, because a session that had "fixed" the
port on that advice would have flashed ESP32 firmware onto another project's board.

#### A5 finding 1 — the Kraken build could not connect, and it was mine

First flash: Wi-Fi up, DNS resolved, **TLS verified against the pinned GTS Root R4** — and
then every attempt failed at the HTTP upgrade with `upgrade headers did not end in 512
bytes`, for ever, `connects=0 frames=0`, panel grey. The instrumentation read it correctly
throughout (`-- crc` all zeros, `-- age` `UNCALIBRATED` at the 30 s ceiling, `seq -1`), which
is the one consolation.

The header buffer was a 512-byte array on the RX task's stack, sized for Anvil. Measured
against both live endpoints rather than guessed:

| endpoint | 101 header block |
| --- | --- |
| `anvil.garethcooke.com` | ~200 B (plain nginx) |
| `ws.kraken.com` | **1,002 B** (Cloudflare) |

Almost all of the difference is two `set-cookie` headers — `__cf_bm` and `_cfuvid` — whose
values are generated per connection, so the number MOVES. It is now `kUpgradeHeaderBytes =
2048` in BSS rather than on a 6 KiB stack that also has to hold an mbedtls handshake, and the
used size is printed on every connect (`upgrade ok: 1002 of 2048 header bytes`) so the next
overflow is a glance instead of a desk probe.

**This is the class of defect A5 exists for and no host test could have found**: the wire
shape of a CDN in front of a venue.

#### A5 findings 2 and 3 — two reporting defects, caught by reading the board's own output

* `subscribe sent (#2)` on the *first* subscribe: the log printed `subscribes() + 1` after
  `step()` had already counted it.
* `owed=1` for a whole run over a book that was live and verifying. `owed_` was set when a
  poll hit the floor and cleared only when a heal *began* — and a poll only happened while
  the level was up, so once the snapshot landed nothing ever cleared it. The policy now takes
  the level either way (`on_poll(bool, now)`), with a test.

#### What thirty-one minutes of MINA/GBP actually showed

Final line, `up=1852s`:

```
SOAK venue=kraken up=1852s live=1 age=0.0s worst_age=1.8s baseline=1001ms
     grey_n=4 grey_ms=16804 wd=0 sock=0 connects=1
     rows=50/54 unknown=4 crc_rows=20 (40.0%)
     resync_req=3 heals=3 owed=0 refused=0 crc_fail=3
     heap=61136 largest=47092 frames=2451 drawn=1376
```

**THE HEADLINE: the panel held colour through a 160-SECOND book-event silence.** M4's
criterion is 26 s. Measured `worst_gap` climbed 5,016 → 25,281 → 25,769 → **160,373 ms**, and
`wd_gaps=0` for the whole run. The `-- event` histogram counts **183 book silences over one
second**, all classified link-bound (`-- holes n=183 board=0 link=183`, Core 0 at 92% idle).

*The deleted `kRxWatchdogMs = 1000` would have greyed this panel 183 times in 31 minutes.*
That is the 2026-08-17 ruling, quantified on hardware, and it is a bigger number than the
ruling's own argument dared use. The 160 s gap fell between t≈240 s and t≈450 s, with no grey
episode anywhere near it, so the panel was demonstrably coloured throughout.

**THREE GENUINE CHECKSUM FAILURES, THREE HEALS, THREE RECOVERIES**, over one socket that never
dropped:

| # | detected | unsubscribe | subscribe | LIVE again | grey |
| --- | --- | --- | --- | --- | --- |
| 1 | 998,228 ms | 998,425 | 999,883 | 1,000,043 | **1,814 ms** |
| 2 | 1,160,546 | 1,160,726 | 1,161,955 | 1,162,181 | **1,634 ms** |
| 3 | 1,799,151 | 1,800,277 | 1,801,295 | 1,801,296 | **2,144 ms** |

`connects=1 sock=0 wd=0` throughout: every heal happened over a live, heartbeating socket,
which is the exact case B2 said the latch existed for and the exact case the transport had
never been asked to handle. **M4's fourth DoD clause — *heals from a checksum failure rather
than greying permanently* — is observed.** The measured cost is ~1.6–2.1 s of grey, against
B2's predicted 3,548 ms book-event hole; better than predicted because the unsubscribe ack
comes back in milliseconds and the 1 s gap dominates.

**And it is the case the review's critical finding was about.** Failures 1 and 2 are 162 s
apart and 2 and 3 are 639 s apart, so the 5 s floor never throttled one — but under the
edge-triggered design I first wrote, *any* of the four loss paths would have left the panel
grey for the remainder of the run. The level design was not defensive: it is what makes this
table have three rows instead of one.

**The checksum ledger closes**: `seen=597 ok=564 FAIL=3 unverifiable=30 unchecksummed=0`, and
564 + 3 + 30 = 597. `unchecksummed=0` says every book message carried the field the healing
path depends on.

**Heap is flat**: 61,136 B free / 47,092 B largest, identical on every one of the 185 soak
lines. No leak in 31 minutes.

Other numbers worth having: the calibrated threshold settled at **4,006 ms** from an observed
heartbeat median of 1,001 ms (B1 predicted ~4 s); `baseline 1001 ms` latched cleanly and the
age read **0.0 s** on a healthy feed with a worst of 1.8 s across the three heals;
`crc_rows=20 (40.0%)` was constant, exactly stage C's arithmetic for 25 levels a side into 27
rows; `worst_frame=11,097 µs` and `a->e worst=26 ms` bound the soft-float cost of stamping the
age on every publish; ping rtt 23–31 ms, worst 509; rssi −29 to −42 dBm.

#### What the run did NOT settle, stated so it is not read as settled

* **What caused the three checksum failures is unknown.** The obvious suspect was the pipe:
  `no_slot=10`. It is not — the timeline refutes it. `no_slot` was **0** when the first
  failure occurred at 991 s, and rose 0→5, 6→9 and 9→10 *during each heal*, i.e. the drops
  are a CONSEQUENCE of the re-subscription snapshot burst hitting a four-slot pipe, not the
  cause of the divergence. Correcting this before writing it down is the only reason it is
  not recorded here as a cause. **What is left is a real open question for Part B**: 3
  failures in 567 gradeable messages (0.53%) on a wire whose committed corpus scored
  4,878/4,878. Candidates: the depth-25 truncation rule, the very large MINA quantities
  (`x3512371940039` at 8 decimals), or a genuine venue-side divergence.
* **The four-slot pipe DOES overrun on a heal**, which is the major review finding recorded
  and not fixed, now with a measurement: 10 drops, all clustered on the three snapshot
  bursts. Kraken's largest message here is 1,970 B against a 16,384 B slot, so a
  venue-selected capacity would buy more slots for less RAM. Bench item / M5.
* **The Kraken build costs a panel colour rung.** `-- panel depth=5` against the Anvil
  control's `depth=6`: the adapter's extra ~8 KiB pushes the HUB75 framebuffer down a rung at
  `Panel::begin()`. Visible on the panel, and nobody had predicted it.
* **The Anvil control's age meter reads ~2 s on a healthy feed, and Kraken's reads 0.0 s.**
  The control latched `baseline 484 ms` against an observed `median 502 ms` — 3.6% low, which
  manufactures 3.6% of wall-clock as phantom lag until the 256-arrival window bounds it near
  4.6 s. Kraken latched 1,001 against 1,001 and reads 0.0. So the board's first 32 *summary*
  intervals run ~3% fast where its first 32 *heartbeat* intervals do not. Stage A2 measured
  n=32 → 499.3 ms on a replay and chose 32 for exactly this reason; on real hardware at boot
  it is not enough for Anvil. **Not fixed here** — it is a stage-A2 constant, the evidence is
  one 75-second control run, and changing it without a second measurement would be the
  mistake stage A2 was written to avoid.
* Nothing here is a soak. B3's parity claim against M3's 23.6 hours is untouched, and
  **whether the Kraken connection survives overnight is still unknown** — 31 minutes on one
  socket is encouraging and is not the question B3 asks.

#### Evidence, final

* **Host, from a wiped tree:** 33 ctest; `dc_tests` 367 cases / 895,787 assertions;
  `dc_tests_kraken` 8; `dc_tests_streaming` 50; `dc_tests_noping` 4; 19 xtensa objects.
* **Firmware:** `depthcharge` 144,488 B RAM / 880,885 B flash; `depthcharge-kraken` 154,864 /
  883,661 (the upgrade buffer added 2 KiB of BSS).
* **Mutations: 20 run, 20 red** — five only after a first attempt showed the check did not
  catch them.
* **Per-commit verification: 4/4 green in isolation**, each in a detached worktree with
  `CMAKE_HOME_DIRECTORY = C:/tmp/dc-verify` **confirmed** before the pass was believed. Host
  32/32 at the engine commit (the Kraken arm does not exist yet) and 33/33 after; firmware
  built at both commits that touch it.

Committed as **four**, not five: `render_task.cpp` carries A1's `-- age` rewrite, A2's
subscription counters and A4's `SOAK` line in one file, so A4 cannot be a commit of its own
without one that does not build. **Nothing is pushed.**

#### Exact next step

**Part B, at the bench.** The board is running the Kraken build now. In order:

1. Time the two remaining B1 criteria with a stopwatch and eyes on the panel: colour held
   through 26 s of book silence (the log says 160 s; the panel is what the criterion is
   about), and grey within ~4 s of the heartbeat stopping — **cut the connection and time
   it**, which is the one clause nothing above has exercised.
2. B2's three decisions, now with the numbers in hand: `crc_rows` is a constant 40.0% at the
   shipped depth, so decision 3's depth question is the one stage C said it was.
3. B3's soak. The new risk B3 names — whether Kraken survives unattended overnight — is still
   open; 31 minutes says nothing about it.
4. B4's M3 residues while the board is out.
