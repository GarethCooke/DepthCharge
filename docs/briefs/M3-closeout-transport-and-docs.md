# M3-closeout — the transport brief closes, the docs catch up, and the Anvil ask gets rewritten

**Track:** Agentic (Claude Code, **Opus** — plain `claude` in the terminal, not a Fable
session; nothing here needs Fable's ceiling and the owner's Fable budget is spent for the
week) · **Status:** ✅ **Done 2026-08-16** — see the session log at the foot of this file
· **Size:** one session, ~4 focused parts, none large.
**Read first:** `CLAUDE.md`, `ARCHITECTURE.md` §6 + the four newest §9 rows (2026-08-13,
2026-08-14 ×2, 2026-08-16), `docs/briefs/M3-transport-own-the-websocket-client.md` from its
2026-08-14 session-log entries to the end (five entries — they ARE the hand-off), then this
file. `docs/DESIGN.html` §08 and §09 before touching it.

## Where things stand (2026-08-16, 00:20)

Six commits landed on `m3/stage-d-the-panel` on 2026-08-15/16 (`c52b268` … `f79aff2`);
tree clean, host suite 11/11, firmware compiles. In one line each:

- **The transport rewrite is in and proven**: owned WS client over esp-tls, host-tested frame
  parser, day-long soak with `connects=1` per boot, zero errno-silent deaths, zero `SPLIT`
  rejects. The bench acceptance in the transport brief's DoD is met in substance.
- **`clkphase = false` ships** — the header ghosting fix; A/B/A showed no feed cost on the
  owned client.
- **The TCP window is rebuilt** — `liblwip.a` from esp-idf v4.4.6 with `TCP_WND` 5744 → 17232,
  shipped as `[env:depthcharge-wnd]` via a local package override
  (`C:\local\framework-arduinoespressif32-wnd17232`, README + rebuild scripts inside). A day
  soak proved it load-bearing: 56–87 KiB/s all day vs the stock window's 65.5 hard cap; lag
  slope +0.08 s/s over 10.9 h vs +0.57 before. **`TCP_RECVMBOX_SIZE` must stay 6** (mapped
  hazard, README).
- **The RX loop is instrumented** (`rx_budget.hpp`, `-- rx` line): wait 0 / read 99 / feed 0
  every hour — the board is wire-bound, its processing is free, the RX path is exonerated.
- **The age clock is 64-bit** (was uint32 µs, saturated at 4294.9 s all day; fixed + tested).
- **A live Anvil stall was observed** (00:12–00:15): server silent 2m56s on a live socket,
  resumed on the same connection, board held it and went LIVE instantly. That refined the
  half-open decision below.

The board is off. COM7 is free. Nothing is running.

## Deliverables

### 1. Close the transport brief (`docs/briefs/M3-transport-own-the-websocket-client.md`)

- Tick every DoD box that the evidence supports, citing the log/commit in the tick line.
  Deliverable 0 (five-boot strongest-sibling): the 2026-08-14 boots all joined the −4x
  sibling in their scans — check `firmware/logs/device-monitor-260814-*.log` and
  `…-260815-002728.log` boot banners (`wifi: joining … strongest of N siblings`) and tick if
  five consecutive boots hold; if not, say so honestly and leave it open. The 3 h strong-node
  soak: `device-monitor-260815-002728.log` (23.6 h, one boot from 13:15 to 00:06 alone is 10.9 h,
  connects=1). The weak-node 1 h soak was **not run** on the final build — leave it unticked
  and note it as owed, do not fake it.
- **Delete the old-client arms.** `[env:depthcharge-espws]`, `[env:depthcharge-nopp]`, the
  `DC_OWNED_WS` / `DC_WS_PINGPONG` switches and every `#if DC_OWNED_WS` arm in
  `ws_transport.hpp/.cpp` and `main.cpp`. The espws arm's `kAnvilUri`/`kAnvilPortText` and
  the `static_assert` tying `kAnvilPort` to it go too if nothing else uses them (grep first).
  Keep `test_frame_reassembler.cpp`'s "old client's world" case only if it still compiles
  and still tests something the FIN path doesn't; otherwise fold it. Both host and firmware
  must build after; `pio run -e depthcharge-wnd` is the firmware check.
- **Make the window build THE build.** `[env:depthcharge]` should carry the
  `platform_packages` override; `[env:depthcharge-wnd]` becomes an alias or is deleted (the
  README path in the comment block moves with it); `[env:link-autopsy-wnd]` likewise folds
  into `link-autopsy`. Move the wnd17232 provenance comment to the shared `[env]` block.
- Add the **silence-recycle** the brief's last two entries specify: `SupervisorInput`
  gains `last_rx_us`; `WsSupervisor` treats "socket connected but no bytes for
  `kSilenceRecycleUs`" as a death and starts an attempt; **constant = 5 minutes** (the
  2026-08-16 00:16 addendum explains why not 15 s — a real Anvil stall recovered on the
  held socket in 2m56s and must cost nothing). Host test in `test_ws_supervisor.cpp`:
  silent socket → `StartAttempt` after the threshold, never before, never while bytes flow.
  `WsTransport` feeds `last_rx_us` from the RX task's arrival stamp (already exists as
  `at_us` in `rx_main`). This is a policy change to a supervisor with a day of clean soak
  behind it — small diff, reviewed, one commit of its own.
- Session-log entry per protocol.

### 2. ROADMAP.md

- M3 status cell: stage D is flashed and long since proven (the row still says "not
  flashed"); the transport rewrite is done; state what is actually left for M3's ✅ — by
  the DoD that is the pull-the-Wi-Fi acceptance photo/video, owner-driven — or tick M3 if the
  owner has already done it (ask via the session log, do not assume). Mark M4 **Next** if M3
  ticks.
- **Rewrite the Anvil-side backlog bullet (lines ~35–58).** It is stale in three claims:
  "caps the board at 65.5 KiB/s", "no firmware lever reaches it", "the alternative is a
  rebuilt ESP-IDF". The truth as of 2026-08-16: the window IS rebuilt (17232) and the board
  runs at 56–87 KiB/s = 60–80% of the wire, wire-bound with the RX path free; the residual
  drift (+0.08 s/s over 10.9 h) is path bandwidth on the transatlantic hop; **the sequenced
  incremental L2 feed remains the sized fix** (a tenth the bytes closes it fully) and stays
  promoted; **add** the new Anvil-side observation: the WS endpoint stalled 2m56s at
  2026-08-16 00:12 while HTTPS answered — worth a look at Anvil's WS server, and the
  heartbeat/keepalive item now has a second reason. Keep "Anvil is not modified from here".
- The "own the websocket client — brief written and ready to schedule" bullet: it shipped;
  delete or mark done with the commit.

### 3. `docs/DESIGN.html` (its §09 lists the triggers; all fired)

- Statusbar: source hash + line count are from 2026-08-11 — regenerate from HEAD.
- Transport class/sequence diagrams: `WsTransport` is esp-tls + `WsFrameParser` +
  `FrameReassembler` + RX task on core 0; the two-handle esp_websocket_client picture is
  gone. `RxBudget` is a new member; `die()` is the one teardown path.
- §08 strains: **strain 11** still describes esp_event dispatch/calloc churn in the
  present tense — close it (the path is gone with the library). **Strain 14** is closed
  (was done 2026-08-14 — verify). **Strain 18** (the owned framing's own risks) — update
  with the review's five confirmed findings and their fixes. **Open a new strain** for the
  transatlantic path-bandwidth ceiling with the soak's numbers (56–87 KiB/s, `feed 0%`,
  +0.08 s/s) and the two levers that remain (Anvil delta feed; nothing on this side). Add
  the `TCP_RECVMBOX_SIZE=14` hazard where the build/toolchain is described.
- Milestone status strip to match ROADMAP.
- Do NOT touch anything the code doesn't support; DESIGN is "drawn from source", and the
  Read-before rule says it loses to ARCHITECTURE on any disagreement.

### 4. Small owed items from the 2026-08-15 review (do if time allows, skip cleanly if not)

- `harness/tests`: extract the fake slot pool shared by `test_ws_frame.cpp` (`Pipe`) and
  `test_frame_reassembler.cpp` (`FakeSlots`) into one header (`fake_slots.hpp`), keeping the
  richer `FakeSlots` surface (`acquire_failures`, `arrivals`/`published_at`).
- `test_ws_frame.cpp` SPLIT@ test: call `reject_log.hpp`'s `find_second_frame_header`
  instead of re-spelling the signature.
- `ws_transport.hpp`: collapse the five overlapping Anvil endpoint constants to two source
  spellings composed at compile time (mostly moot once the espws arm is deleted — check).
- Memory note for the machine: `C:\Users\garet\.claude\projects\c--Development-Projects-DepthCharge\memory\`
  already records the WSL rebuild pipeline, COM7 discipline and the Bash-sandbox trap; read
  it, and add anything this session learns of that kind.

## Constraints

- All of `ARCHITECTURE.md` §6. Nothing in `engine/`. Integer ticks. No allocation on the
  firmware hot path. `kRxWatchdogMs` stays 1000. Goldens stay.
- **Do not raise `CONFIG_LWIP_TCP_RECVMBOX_SIZE`.** Do not touch the package under
  `C:\local\` except to read it.
- Bench work is owner-driven; this session needs no board (COM7 discipline in the memory
  notes if one is used: kill every `device monitor` process before any flash, and test-open
  COM7).
- Build/test: `cmake --build --preset host` then `ctest --preset host` **from PowerShell**
  (the Bash sandbox breaks compilers and test binaries silently; the `--workflow` preset
  will fail on this machine because the cache is MinGW-generated — build/test presets
  individually). Firmware: `pio run -e depthcharge` (or `-wnd` until it is folded) via
  `$HOME\.platformio\penv\Scripts\pio.exe`.
- Review before commit per `CLAUDE.md` (the owner's `code-review` skill); commit messages
  imperative and scoped, e.g. `firmware: the supervisor recycles a silent socket after five minutes`.
- Hand-off protocol at the end (session log, DoD ticks, ROADMAP, DESIGN, §9 if anything
  has architectural weight — the silence-recycle probably does: one row).

## Definition of done

- ☑ Transport brief DoD ticked where the evidence supports it; unticked items named as owed.
  Six of seven ticked with the log line or commit cited; the weak-node 1 h soak is left
  unticked and named as the one bar never run on the final build.
- ☑ espws/nopp arms and `DC_OWNED_WS` deleted; host + firmware green. (`66e2f77` — also
  `DC_WS_PINGPONG`, `kClientWaitTimeoutMs`, `kClientSelfExitUs` and the two-handle assert.)
- ☑ Window build is the default `depthcharge` env; provenance comment travels with it —
  into `[env]` rather than onto one environment, so no arm differs from the daily driver by
  more than its own flag, and `link-autopsy-wnd` folds away. The comment gained the recovery
  path for a machine without the package (`79c1867`).
- ☑ Silence-recycle in `WsSupervisor` (5 min), host-tested, one commit (`e281df9`), §9 row.
- ☑ ROADMAP: M3 status honest (stage D flashed and soaked; the pull-the-Wi-Fi photo named as
  the only thing left, and the tick left to the owner), Anvil backlog bullet rewritten to
  2026-08-16 truth with the new WS-stall observation added.
- ☑ DESIGN.html: statusbar (hash + line count from HEAD), §01 diagram captions, strains 11
  and 14 closed, 18 updated with the five confirmed review findings, **19 opened** for the
  transatlantic path ceiling, the `TCP_RECVMBOX_SIZE` hazard added to §07 where the build is
  described, §09's change list and the footer. One out-of-scope correction taken because it
  contradicted the constitution: §05 still asserted that Anvil *sheds* to a slow consumer,
  which ARCHITECTURE §9 withdrew on 2026-08-11.
- ☑ Session log per protocol; tree clean and green.

## Out of scope

The Kraken adapter (M4). Any Anvil change. Another soak (the owner runs those). The M6
carrier board. Re-testing the ghosting on the panel (owner, at the bench: with clkphase
false, is the header speckle gone, and did the green right-edge dots go with it — the
answer decides whether M6 still owes the display side anything; record it in
`hardware/BRINGUP.md`).

## Session log

### 2026-08-16 · Claude Opus 5 · executed; M3 has one owner-driven thing left

**Done.** All four deliverables. Five commits on `m3/stage-d-the-panel`:

| commit | what |
| --- | --- |
| `66e2f77` | the old client's arms come out; the window build becomes the build |
| `e281df9` | the supervisor recycles a silent socket after five minutes |
| `447522a` | one fake slot pool; the SPLIT@ test calls the reject log's own scanner |
| `79c1867` | one spelling of "silent"; the window build's costs written down |
| *(this)* | ARCHITECTURE §9 ×2, both briefs, ROADMAP, DESIGN.html |

No board was used. The object was off and COM7 free throughout; every claim about the
hardware is read out of committed logs.

**Decisions, with why.**

1. **The five-boot acceptance is ticked on sixteen boots.** Every boot from 2026-08-14
   onward joined `EE:D3:62:AE:81:9A` at −39…−47 dBm against siblings at −64…−86, and each
   boot's `wifi up: bssid=` line confirms the association landed where its own scan pointed.
   The bar is relative by the brief's own instruction, and it is met by a wide margin.
2. **The weak-node hour is left unticked.** The day soak contains weak-sibling readings but
   the board was never *pinned* to one on the final build, so there is no controlled arm.
   Calling that an acceptance would be precisely the "measurement that answers a nearby
   question" §9 has now convicted four times.
3. **`platform_packages` went to `[env]`, not to `[env:depthcharge]`** as the brief
   suggested. Every other environment here exists to be compared against the daily driver,
   and an arm that differs in the framework as well as in its one flag is not a controlled
   arm — `link-autopsy` in particular measures raw stack throughput, which is the one number
   the window changes. The cost is real and is now written into `platformio.ini` beside the
   rationale: no firmware env builds without the local package, and there are two documented
   ways out (rebuild it, or drop the two lines and accept the 65.5 KiB/s cap).
4. **`depthcharge-ping` survives, flagged.** Its question is closed by the silence recycle,
   which needs no server cooperation; the brief scoped the deletions to the *old client's*
   arms and this is not one. Deleting it is a one-line owner call, noted in `platformio.ini`,
   `ws_transport.hpp` and DESIGN strain 18.
5. **`M3`'s row is not ticked, and the ROADMAP says why.** The DoD is the pull-the-Wi-Fi
   acceptance on the panel; this session had no board and cannot know whether the owner has
   already done it. The row asks, per the brief's "ask via the session log, do not assume".
   If it is done: tick M3 and mark M4 **Next**.
6. **One out-of-scope DESIGN correction taken.** §05 still stated that Anvil sheds to a slow
   consumer. ARCHITECTURE §9 withdrew that on 2026-08-11 (it queues; only freshness against
   a reference clock can tell the two apart). DESIGN loses to the constitution on any
   disagreement, so leaving it would have been leaving a known-false claim in the document
   whose whole premise is that it is believed. Struck through with the correction beside it,
   per §9's own rule about not deleting the reasoning.

**The review found two real defects before any of this landed**, both in the silence
recycle, both fixed and both recorded in §9 because the reasoning generalises: the RX task
was trusting what a request flag *implied* about its own provenance (a slow-but-successful
connect falsifies it, and the first draft would have torn down a healthy socket inside one
read timeout), and the recycle's label was latched at the outage rather than read live (so
every retry after one would have printed "socket up but silent" over a socket that was
gone). The remaining review items are also cleared: `socket_is_silent()` is now one
`constexpr` function both cores call and both are host-tested through, a 200k-poll property
walk asserts the policy's two invariants over an arbitrary input trace, and `autopsy()`
records that `deaths_` now counts a socket end this firmware chose.

**Measured.** Host 11/11, 250 doctest cases / 829,925 assertions in `dc_tests`, 46/46 in
`dc_tests_streaming`. Firmware: all five environments build; `depthcharge` is
141,888 B RAM / 874,597 B flash, **+160 B RAM / +1,876 B flash** against the 2026-08-14
owned arm.

**Owed, in priority order.**

1. **Re-run the pull-the-Wi-Fi acceptance on the fixed build.** It ran the same morning
   and **failed**, which is recorded below.
2. **The weak-node 1 h soak** — the transport brief's last unticked bar. Pin or re-roll to a
   −7x sibling for an hour; fades may grey, nothing may die.
3. **The ghosting re-check** with `clkphase = false` — is the header speckle gone, and did
   the green right-edge dots go with it? The answer decides whether M6 still owes the
   display side anything. Record in `hardware/BRINGUP.md`.

None of the three is agentic work. **The next agentic milestone is M4, the Kraken adapter**,
and it inherits one sizing input from this session: a venue's byte rate is a design input,
and Kraken's full-depth stream is larger than Anvil's against a path that already runs at
60–80% of the wire (DESIGN strain 19).

---

### 2026-08-16 (09:00) · Claude Opus 5 · the acceptance ran, and it failed — the station could not rejoin

The owner ran the pull-the-Wi-Fi test an hour after the close-out landed. **Half of it
passed and half of it failed, and the failing half was a real defect that every host test
and a 23.6-hour soak had missed.**

**What passed.** Blocking the board in Deco greyed the panel exactly as invariant #5
requires — the photograph shows white/grey only, no hue anywhere. The socket autopsy named
the cause correctly (`socket end #2 [read]: 125756 ms, 9027305 bytes`,
`errno=113 (ECONNABORTED)`, `rssi=0 assoc=0`), the WS supervisor printed its holdoff line
exactly once, and the Wi-Fi supervisor took over on schedule.

**What failed.** The station never came back. `device-monitor-260816-085158.log`:

| | |
| --- | --- |
| 08:57:39.362 | `Reason: 1 - UNSPECIFIED` — Deco deauths |
| 08:57:42.230 | `Reason: 202 - AUTH_FAIL` — Arduino's one retry, refused, **2,868 ms later** |
| 08:57:44.703 | `wifi down 5249 ms … rejoining (#1)` — our takeover |
| … | #2 … #388, one per second, never associating |

**388 rejoin calls produced two `AUTH_FAIL` responses from the AP.** Every rejoin begins
with `WiFi.disconnect()`; an association needs ~4 s; the 1 s refused-retry cadence
destroyed each attempt before it could resolve. `WL_CONNECT_FAILED` is sticky, so once
latched it selected the fast path forever. The earlier "recovery" at 08:52 was not one —
`rst:0x1 (POWERON)`, a power cycle, four seconds before the association that worked.

**Fixed and host-tested the same morning** (`e032a7f`, branch `m3/wifi-rejoin-livelock`).
The full reasoning is ARCHITECTURE §9's 2026-08-16 (pm) row; the short version is that the
fast path was sized against the time to be *refused* (60 ms, measured) when the quantity it
had to respect was the time to be *accepted* (4,035 ms, also now measured).

**Decisions, with why.**

1. **The regression is a station model, not another example case.** `FakeStation` encodes
   the three facts that matter — 60 ms to be refused, 4 s to be accepted, `disconnect()`
   destroys whatever is in flight — and drives an AP that stops refusing part-way through.
   It fails on the old code with the station still unassociated ten minutes after the AP
   opened, which is the bench behaviour reproduced on the desk. No example-based test would
   have caught this, because every individual decision the old policy made looked correct.
2. **`kWifiRejoinAfterUs` was fixed too, and it was not in the original diagnosis.** The
   review of the fix caught that the *first* takeover had no assert on it at all and
   justified its five seconds with a "28 ms" figure for Arduino's own retry. Today's log
   measures that retry at **2,868 ms**, and if the AP accepts it is a full association. Same
   defect, one constant over. Both are now asserted at ≥ 2× the measured association.
3. **A refusals counter was drafted and dropped.** It would have made the 388-vs-2 ratio
   visible on the stats block, but it samples a sticky flag at 250 ms and therefore reads
   *one* in the broken case and *one* in the healthy case. An instrument that cannot
   separate the two worlds it was built for is worse than none. The signal that actually
   diagnosed this is already free and event-driven: Arduino's `Reason:` lines.
4. **The asserts now demand a factor, not an inequality.** The 2026-08-10 version was
   satisfied at exactly 1.0× by a constant that turned out to be an under-estimate. Setting
   the cycle back to five seconds is now a compile error — verified by doing it.

**What this says about the milestone, and it is worth more than the bug.** The DoD that
found this is the *only* acceptance in M3 that is not a host test or a soak, and it is the
one that caught a defect making the object unusable after any Wi-Fi interruption. The host
suite was green, the 23.6 h soak was green, and neither could see it, because neither ever
took the association away and gave it back. **An owner-driven bench acceptance earned its
place in the DoD today.**

**Exact next step.** Flash the fixed build and re-run the same test: block in Deco, confirm
grey, unblock, and watch for `wifi up:` **without a reset in front of it**. Expect recovery
within ~10–20 s of unblocking (one rejoin cycle plus the ~4 s association plus the ~4 s
socket connect). The tell that it is genuinely fixed rather than lucky is the ratio: with a
10 s cadence every rejoin should now draw its own `Reason: 202` from the AP while it is
still blocked, so `rejoining (#N)` and `Reason: 202` lines should be roughly one for one
instead of 388 to 2.

---

### 2026-08-16 (09:45) · Claude Opus 5 · the re-run passed, M3 is done, and the passing run found the next thing

**M3 is ticked.** `hardware/bench-2026-08-16-pull-the-wifi-acceptance.md` is the record and
carries both runs, because the failing one is the more valuable.

**Verified from the log rather than from the report** — the pattern that caught the
power-cycle earlier this morning:

- **One `rst:0x` in the whole log, and it is the flash boot before the test.** No reset
  between the outage and the recovery: the station rejoined unaided.
- **One rejoin call, one AP answer.** The 388-to-2 signature is gone; the first takeover
  succeeded outright, which beats the one-for-one ratio predicted as the tell.
- Deauth 09:37:48.857 → `LIVE at v573` 09:38:07.516 = **18.7 s**, inside the 10–20 s
  predicted before the run. `connects=2`, `sock_gaps=1`.
- The panel greyed at 09:37:43.659, **5.2 s before the deauth** — the RX watchdog fires on
  data stopping, not on the socket dying, which is invariant #5's strict reading behaving
  exactly as ARCHITECTURE §9 (2026-08-09) says it must.

**And the passing run surfaced strain 21, which is why reading the log still mattered.** The
recovery landed on a **−70 dBm** node where boot's explicit scan had joined at **−40**, and
the watchdog greys went from 1 to **14 in three minutes**. `connect_wifi()` surveys and joins
the strongest by name; the rejoin path cannot, because a blocking all-channel scan on
loopTask would stall the supervise poll. `ALL_CHANNEL_SCAN` + `BY_SIGNAL` narrow that lottery
and do not close it — the same finding §9 recorded on 2026-08-13 for the boot path, which was
fixed there and left here. The board does not roam, so one unlucky rejoin is the association
for the rest of the run.

It does not fail the DoD (grey, then clean resync, both observed) and it is not a regression
from this morning's fix — it has been true since 2026-08-10. It is simply the first time
anyone measured what it costs. On the backlog with the numbers, and the fix has a known
shape: `WsSupervisor` decides while the RX task does the blocking work, and `WifiSupervisor`
wants the identical split.

**Owed after M3, none of it blocking:**

1. **Rejoin scan-then-join off loopTask** (strain 21) — one session, policy half host-testable.
2. **Weak-node 1 h soak** — the transport brief's last unticked bar. The board is sitting on
   a −75 dBm association by accident right now, which is most of the setup.
3. **Ghosting re-check** with `clkphase = false` (`hardware/BRINGUP.md`).

**Next milestone: M4, the Kraken adapter**, marked **Next** in the ROADMAP, and it inherits
two rules rather than one. Size the venue's byte rate before writing the adapter — the object
already runs at 60–80% of Anvil's wire and Kraken's full-depth stream is larger (strain 19).
And put at least one criterion in its DoD that the harness structurally cannot stage, because
M3's did, almost by accident, and it was the only thing that caught a defect making the object
unusable after any Wi-Fi interruption (strain 20).

MP stage 2 (real hardware photos/video on the portfolio) is ungated by this tick.

### 2026-08-16 (evening) · Claude Opus 5 · the Anvil ask, executed — and it turned out to be a different ask

Follow-on to deliverable 4 of this brief ("rewrite the Anvil-side backlog bullet"), which
rewrote it *here* but never got it to Anvil. Doing that properly meant reading Anvil's tree,
and that changed the ask.

**Done.**

- **`docs/anvil-handover-2026-08-16.md`** — the ask as a document an Anvil session can act on,
  with evidence and Anvil-side file references. ROADMAP's backlog table is now its index.
- **The vendored protocol is re-pinned** — `docs/vendor/anvil-protocol.md` moves
  `d501652` (2026-07-07) → `e8d313f` (2026-07-26). **Anvil fixed the `seq` contradiction three
  weeks ago** in response to M0's finding and nobody here noticed, so the pin was stale and
  its header still carried a correction Anvil had already made. Verified doc-only: exactly one
  commit touched `PROTOCOL.md` between the pins, no field, type, shape or endpoint moved, so
  no adapter, trace or golden is affected.
- **The withdrawn shedding claim is gone from the last two places it survived** — the vendored
  header, and a DESIGN §07 paragraph the close-out's §05 fix did not reach.
- **`tools/anvil_frame_economics.py`** — new, and the reason the ask changed.
- ROADMAP: **A7** added and put first; A1 demoted; A3 closed as a question; A2 halved; A6
  sharpened; **D5** added. ARCHITECTURE §9 row; DESIGN strain 19 rewritten.

**Decisions, with why.**

1. **A7 goes ahead of A1, and A1 stops being blocking.** `book` frames are 98% of Anvil's wire
   at ~8.4 KB and ~205 levels; a median of *one* level changes between consecutive frames; the
   panel renders 27 a side. Truncating to what it renders is 27.8% of the stream — ~31 KiB/s
   against a measured floor of 56 — and `GET /api/book` already takes `depth`. The delta feed
   is 1.3% and still the better design, but it is no longer the cheap fix, and this project
   spent a milestone calling it the thing that makes the hardware work at all.
2. **The A2 ask is halved rather than pressed.** Crow answers client pings with pongs already,
   and queues them behind a backlog — so the liveness signal is ours to take (D5) and Anvil
   owes a sentence, not a feature.
3. **The 2m56s stall is downgraded to an observation.** The old wording said the WS server
   "wedged"; HTTPS succeeding on a *different* connection cannot establish that. nginx is
   excluded on its timeouts; beyond that the evidence does not separate a server stall from one
   flow in RTO backoff, and the note says so.
4. **The hand-over admits the bad note.** DepthCharge put the withdrawn shedding claim on
   Anvil's backlog and left it there for a week; the document says so in its own voice rather
   than quietly replacing it. It also stops calling that bullet "wrong": only its central
   clause was withdrawn — the ~8.3 msg/s measurement and the "a delta feed cannot be lossless"
   sentence both stand, and the second is the reason A1 asks for deltas *alongside* the
   full-replace `book` rather than instead of it.
5. **Every load-bearing claim was verified against source before the document was called
   done**, by an adversarial pass told to default to REFUTED. Eight claims: one CONFIRMED
   outright (the unbounded queue), one CONFIRMED with the re-pin proven wire-safe, and six
   returned corrections that are now in the text. **The one that mattered was DepthCharge being
   wrong about DepthCharge:** the draft said the board "cannot use the ESP-IDF cert bundle —
   `WiFiClientSecure` exposes `cert_pem` and nothing else", which was true of the *old*
   transport and **died with the rewrite this brief shipped**. `ws_transport.cpp` builds an
   `esp_tls_cfg_t` by hand, `crt_bundle_attach` is right there, and the framework ships the
   200-cert bundle with X1 inside it. Sent as written, A6 would have collapsed on a one-line
   reply. The general lesson is the milestone's own, again: **a constraint recorded when it was
   true does not expire on its own** — `anvil_root_ca.hpp`'s header still asserts it, and that
   is now on the backlog under A6. Other corrections folded in: the ping/pong measures the
   *write-path backlog*, not end-to-end freshness (it is blind to producer-side lag); the nginx
   exclusion now leads with symptom-in-kind rather than a template that only has a `listen 80`
   block; the A1 baseline claim was backwards on the race that matters to a delta client; and
   "~1 MB of RAM" / "on the feed since 2026-07" were both unsupported.

6. **"60–80% of the wire" was arithmetic, not measurement, and it is 51–79%.** 56/110.4 = 50.7%.
   Cause: both probes divide by 1024 and printed "KB/s", so with two spellings in play the ratio
   got eyeballed rather than divided, then quoted five times. Probe labels fixed at the source;
   corrected in place in ROADMAP and DESIGN (living documents) and **left standing in this brief
   and in §9's rows**, per the amendment log's own rule — with the correction recorded above
   them (§9, 2026-08-16 eve). The firmware's identical mislabel (`render_task.cpp` computes KiB,
   prints "KB/s") is deliberately the one instance left: it could not be compiled or flashed in
   this session, and an unbuilt firmware edit is worse to leave behind than a wrong label.
   Nothing parses either string. **So the three appearances above — lines ~81, ~248 and ~365 —
   are superseded where they stand.**

**Not done / next.** Nothing has been given to Anvil yet — the hand-over is written, not sent.
No commit made; the tree is dirty with the six files above and ctest is green. M4 is still
**Next**, and it inherits one more rule than it did this morning (§9, 2026-08-16 eve): size
the bytes you will *render*, not the bytes the venue offers.

---

### 2026-08-16 (D5) · Opus 5 · the venue gets pinged, and the recycle nearly lost its clock

**Context.** A7 is now underway Anvil-side, which frees the two DepthCharge-side items that were
queued behind it. The owner asked for both.

**Item 1 was already done, and the finding is that the log said otherwise.** `staleness.hpp`'s
`SecondsText` and every accessor behind `age`/`worst`/`run`/`over` have been 64-bit since
`1dea077` — the *same commit* that added the §9 soak row whose closing sentence reads "Fix owed
before the next soak". So the row recorded a defect and shipped its fix together, and the
sentence has been read as outstanding ever since. Closed explicitly in §9 rather than by editing
that row, per the table's rule. One residual is deliberate and is stated there so a future reader
counting fields does not think it was missed: `worst_ahead_us_` is still a clamped uint32, and
cannot produce a false negative because the test that fires on it is the uncapped per-mille ratio.

**Item 2, D5, is done and is the substantial half.** `firmware/src/ws_ping.hpp` — `PingProbe`,
ESP-IDF-free, the sixth instrument written to that rule — with `harness/tests/test_ws_ping.cpp`
(13 cases). The client ping is **on by default**; `depthcharge-ping` inverts to
`depthcharge-noping` as the control arm. A `-- ping` line prints directly under `-- age`.

**What it buys, which is the reason to have built it rather than the reason it was on the list.**
The ping's original question — provoke a half-open socket into an error — stays closed; the
silence recycle owns recovery. The question it answers now is one nothing here could answer:
`staleness.hpp` says how old the book is and cannot say *whose fault*. A pong queues behind
everything already posted to this connection, so the round-trip prices **this socket's
server-side send queue**. Age high + rtt high is our own undrained queue and A7 is the fix; age
high + rtt low is lag upstream in the broadcaster and A7 would not help. That split is the
diagnosis, and it is why the two lines are adjacent and share a vocabulary.

**The thing that would have shipped as a silent regression, and it is the entry §9 is written
around.** The silence recycle's clock was fed from *byte* arrival. A ping manufactures a pong
every 10 s, and a pong is bytes — so the board's own control traffic would have refreshed the
detector, and a peer that answers pongs while publishing nothing could never have been recycled.
That peer is not hypothetical: it is the 2026-08-16 00:12 stall, and the recycle is the only
recovery path there is. `SupervisorInput::last_rx_us` is now `last_data_us`, stamped in
`on_chunk` from the same pre-parse `arrival_us` the read used — strictly stronger, no precision
lost. **General rule: an instrument that generates traffic must not feed a detector that measures
traffic.**

**Three design points pinned rather than commented.** One ping in flight at a time, because RFC
6455 §5.5.2 lets a peer answer several with one pong and a correlation key cannot create the
responses it would correlate. An unsolicited pong (§5.5.3) is counted, never treated as an answer
— the round-trip it would fabricate is small and plausible. And **nothing branches on any of it**:
§6 #5 means a pong must never turn the panel green, so the type exposes no `healthy()`/`live()`
predicate at all and the render task holds it `const`.

**Review, in two passes, and the second one is the one that mattered.** The first pass ran
mid-build and was partial; the owner asked whether the code had actually been reviewed, and the
honest answer was "not completely". The full pass found the worst item.

*First pass — both the sibling instrument's scars arriving unchanged in a new file.*
(1) The peak had no run-level survivor, so the deepest reading this probe can take — a round-trip
that outlives its own socket — was banked at `note_disconnect` and erased by the `note_connect`
seconds later. That is exactly how 21 reconnects made the 86-minute run of 2026-08-09 look
healthy. Fixed with `worst_rtt_ever_us_` and a `bank_peak` helper, mirroring `StalenessEstimator`.
(2) `render` returned early on "no reading on this socket yet" and printed a shrug over a
74-second finding it was holding. Fixing it also collapsed two spellings ("outstanding" /
"waiting") of one quantity into one — caught by a test asserting the old wording.

*Second pass — and the first item is the one this project would least like to ship.*
(3) **The header claimed the single-writer design put this file "in a stronger position than any
other counter in this firmware". That is false, and `staleness.hpp` had already written the
warning against making it** — verbatim: "do not copy their sentence into this file", because
64-bit stores on the LX7 are two word stores and the render task reads across cores. The claim
was corrected into the split it should always have been: the *durations* are safe, but for a
stated reason (a round-trip cannot outlive its socket, and `kSilenceRecycleUs` bounds that at
five minutes, so the high word is always zero — and if that constant ever grows past ~71.6
minutes the guarantee lapses); the *timestamp* `ping_sent_us_` has no such bound and can put one
absurd `waiting` on one log line past 71.6 minutes of uptime, exactly as the age clock can. A
false safety claim in a header is worse than none, in a file whose entire failure mode is a
confident wrong number.
(4) The pingless arm printed "no round-trip yet" on every statistics block forever — on the line,
indistinguishable from a venue that is being asked and is not answering, which is the *opposite*
finding. `DC_WS_PING`/`kClientPingEnabled` moved from `ws_transport.hpp` into `ws_ping.hpp` so
`render()` can say "disabled at build" instead, and so the flag has one spelling rather than two.
(5) `connections()` counted connect *attempts* — `note_connect` runs before DNS/TCP/TLS — so the
name overstated it. Renamed `connect_attempts()`.

*Third pass — the two items the second pass had deferred, taken on the owner's word.*
(6) **`kUsPerMs` was a new named constant used by one file while three siblings inlined `1000u`
at four sites** — the repeated-literal case, arriving from the other direction. Moved to
`gap_histogram.hpp` beside the other shared helpers and adopted at all five sites. **It is
`std::uint32_t`, and the width is the finding**: every pre-existing site divides a `uint32`, so
the obvious `std::uint64_t` spelling would have promoted `stall_probe`'s per-event `gap_us` and
`recovery_us`, `reject_log`'s age and `render_task`'s window to 64-bit division — and the LX7 has
no 64-bit divide instruction, so each becomes a libgcc call. A tidy-up that silently added four
runtime calls to per-event paths would have been a poor trade for a name.
(7) **The `kClientPingEnabled == false` branch of `render()` is now covered.** The second pass
called it an accepted gap; it is one `add_executable`. `dc_tests_noping` builds
`test_ws_ping_disabled.cpp` with `-D DC_WS_PING=0` — the same one-source-two-link-configs trick
`dc_tests_streaming` already uses — and pins the property that only exists in that arm: the line
must never claim a reading. Its first case asserts the flag actually reached the target, because
without that the other three would pass vacuously against the enabled render.

*Verified, not changed:* the render task's stack is 6,144 B and the statistics block's frame grew
by the 128 B of `char ping[128]` beside the existing `char age[208]` — ~2%, on a task whose
documented greedy consumer is `vsnprintf` rather than these buffers.

*The one limit rather than a defect:* no host test can
catch a future session repointing `last_data_us` back at byte-arrival. `WsTransport` is the
ESP-IDF half and is not host-buildable, and a strong typedef would not help either — both
quantities are `esp_timer_get_time()` results of the same type, differing only in *where* they
are taken, which no type can express. What exists instead is the policy-end contract test, the
comment at the single stamping site, and the §9 rule. Stated so nobody later mistakes the
coverage for more than it is.

**Green.** `cmake --workflow --preset host-mingw` 11/11 (266 cases, 847,548 assertions).
`depthcharge`, `depthcharge-noping`, `depthcharge-ps` all build. RAM 43.3%, flash 13.4%.

**Not done / next, and none of it is faked.**

1. **Nothing is flashed.** Every claim above is a desk claim. The bench reading that matters is
   the first `-- ping` line against a real Anvil socket, and the number to expect on a healthy
   one is **~87 ms** — the transatlantic RTT. Anything much larger, with `-- age` also large, is
   the queue; anything ~87 ms with `-- age` large is upstream, and that would be a finding.
2. **The A2 half is still owed by Anvil**, and it is one sentence: the pong-ordering this rests
   on is stock vendored Crow, read from source and never captured under induced backpressure.
   Until it is a contract, the number is evidence, not a guarantee.
3. **The hand-over is still unsent** — unchanged from the entry above.
4. **Now unblocked, and deliberately not taken:** the previous entry left `render_task.cpp`'s
   `KiB`-computed / `"KB/s"`-printed mislabel as the one instance standing, because it "could not
   be compiled or flashed in this session". It can be compiled now. It is a one-line fix and it
   is the owner's call whether it rides with this change or its own.

---

### 2026-08-16 (A7) · Opus 5 · Anvil answered the same day, and the staleness problem is closed

**Context.** [`docs/anvil-handback-2026-08-16.md`](../anvil-handback-2026-08-16.md) arrived
answering the A1–A7 ask: **A7, A2, A3 part 1 and A6 are done, pushed and deployed.** Two
integration tasks landed here and both are done.

**1. `&depth=27`, and it is the entire A7 integration.** `kAnvilPath` is now
`/ws?ticker=101&depth=27`. `engine/` did not move — the adapter already accepted 83–126 levels a
side and truncated above `kMaxSnapshotLevels`, exactly as the ask predicted it would.

**Measured here rather than taken on trust, because that is this milestone's whole scar tissue.**
`tools/capture_anvil.py` gained a `--depth` argument (it could not otherwise capture what the
board subscribes to, and a trace that does not match the firmware is a golden describing a
different stream). A 90-second capture is committed as
`harness/replay/anvil_101_depth27_20260816.ndjson` with three ctest cases — `dc_replay`,
`dc_replay_streaming` and `dc_ladder` — kept **alongside** the full-depth traces rather than
replacing them, because the shallow book is the new normal and the deep one is what every golden
before today was derived from. Priced by `tools/anvil_frame_economics.py`:

| | 2026-08-09 baseline | `depth=27` |
|---|---|---|
| `book` mean | 8,428 B | **2,471 B** |
| levels / frame | ~205 | **60.0** (30 a side — the tier, confirmed from outside) |
| 90 s of wire | 10.4 MB | **2.84 MB = 27.3%** |
| sustained | 112.6 KiB/s | **30.8 KiB/s** |

Against the soak's worst measured hour of 56 KiB/s: **1.8× headroom where there was 0.5×.** This
repo predicted 27.8%; Anvil's independent deployed-server figure of 2,476 B agrees to 0.2%.

**The one contract fact that binds M4/M5: depth is served in TIERS** — 1,2,3,5,8,10,15,20,30,…,
rounding **up** and never down. Ask 27, receive 30. Anvil's reason is sound (`depth` arrives from
an unauthenticated query string, so free-form depth would cost one serialisation per distinct
depth in use, per ticker, per tick). The generalisation is in §9: **a venue's depth parameter is
a request, not a contract, and an adapter must not assume it got what it asked for.** The tier's
cost, stated because Anvil sized it as "a few percent": 9.7% of `book` bytes the panel never draws.

**2. `docs/vendor/anvil-protocol.md` re-pinned at `b4d31c2`** — Anvil flagged it stale and it was.
It gains §3 `depth` (with the tier ladder), §3 **Keepalive**, §4 **Slow consumers**, and the
`/api/book?depth=` fix. **Two notes this file has been carrying for weeks now close**: the
2026-08-11 slow-consumer note (§4 documents it in Anvil's own words, citing our figures, and the
client rule is now contract — *measure freshness against your own clock, never infer it from
message rate*), and the A2 pong-ordering caveat.

**3. A correction we owed ourselves.** `ws_ping.hpp` said the pong ordering was "read from
Anvil's source and has never been captured under induced backpressure — read this number as
evidence and not as a guarantee". Anvil captured it: their probe stops reading until frames back
up, then sends one ping and reports the pong's **position in the byte stream** rather than its
RTT — because a slow reader inflates the round-trip whether or not the server reordered anything.
**425,890 bytes as 149 frames; the pong at index 148, last, zero after it.** D5 now rests on a
measured property, and the correction is made in the header, ROADMAP and §9 rather than left to
rot the way the age-clock "fix owed" sentence did.

**4. A premise in our own ask was wrong.** We cited `GET /api/book?depth=` as proof that "the
name, concept and validation already exist". It took the parameter and **silently ignored it** —
there was no validation to reuse, and Anvil fixed the REST surface as part of A7. Recorded in §9:
arguing from a parameter's existence is not arguing from its behaviour.

**What changed in the backlog.** A7/A2/A6 closed; A3 part 1 closed, part 2 open and sized M;
**A2b is new** (the 2 min 56 s stall, split out of A2 — Anvil's leading hypothesis is fan-out
head-of-line blocking driven by A3, which would free spontaneously when the stalled socket
disconnected, matching our unexplained recovery). **A1 gained an ordering constraint we had not
seen:** the per-ticker sequence is gap-detectable *because* the queue is unbounded and lossless,
so if A3 part 2 is resolved by dropping `book` frames that property dies — benign under full
replaces, fatal under deltas. **A3 must be decided before A1**, not alongside it.

**Green.** Host 15/15 (three new replay cases). `depthcharge` builds.

**Not done / next.** Still nothing flashed — and the flash is now more interesting than it was
this morning, because the board should come up on a third of the bytes. The bench readings to
take on the first run: `-- age` should stop climbing, and `-- ping` should sit near **87 ms**.
A7 is the fix for the staleness that three sessions attributed to this firmware; nobody has yet
seen it work on the panel.

---

### 2026-08-16 (soak) · the >=90-minute bar is cleared, and two things on record are corrected

**4h 51m of monitor, 3.88 h on a single connection** (`device-monitor-260816-175433.log`, 6.8 MB).
The bar the merge commit set — a >=90-minute single-connection soak — is cleared more than twice
over. Both corrections below are to claims *this session* made, and both are recorded rather than
quietly amended.

**1. "age 0.2 s, flat" was a 13-minute reading. The 3.88-hour answer is a STEP.**

| window | mean age |
|---|---|
| 18:54-19:32 | 0.40 s |
| 19:32-20:11 | 0.87 s |
| 20:11-20:50 | 0.99 s |
| **20:50-21:29** | **3.52 s** |
| 21:29-22:07 | 6.77 s |
| 22:08-22:47 | 7.42 s |

Least-squares slope over the connection: **+0.000672 s/s**, against the control's +0.25 s/s —
**372x shallower** — and a cumulative deficit of **15 of 27,964 summaries (0.054%)** against the
control's 24.9%. So the verdict is unchanged and the merge stands; what is wrong is the word
*flat*. **The mechanism is worth keeping**: a socket draining at exactly 100% keeps pace with the
broadcast but can never *repay* a deficit, so any hit it takes becomes a permanent offset. Age
under A7 is therefore a staircase of absorbed incidents, not a ramp — and reading a short window
as proof of flatness is the same short-sample error this milestone has now made twice.

**2. The step is the path, and the stall probe says so without being asked.**
`-- holes : n=766 board=1 link=765 mixed=0 unknown=0 | burst=761 cadence=3`. **765 of 766 holes
are link-attributed; the firmware caused one in 4.8 hours.** They are `burst`-shaped — a gap then
a lump — i.e. delayed-then-delivered congestion, not loss. It is not the association: rssi held
**-40 to -47 dBm** all evening. It is corroborated by the venue: Anvil's `seq` rate rose
**180 -> 189/s** and inbound **30.2 -> 33-35 KiB/s** across the evening, consistent with the known
midnight peak. The hole count is concentrated in the 21:00 hour (297) exactly where the step is.

**3. THE RECONNECT ITEM IN THE MERGE COMMIT IS NOW PROVEN, not argued.** That commit lists as
unproven: *"the reconnect path re-sending `&depth=27` is a source-level argument, never observed —
connects=1 in both runs."* At **18:54** the socket dropped (`sock_gaps=1`, `connects=2`; panel grey
4,341 ms; `socket up: dns 78 ms, connect+upgrade 3887 ms` on attempt #2). The replacement socket
then ran **3.88 h at ~100.3% drain**. Had `&depth=27` not been re-sent on the fresh upgrade, that
connection would have been full-depth and would have degraded to ~75% drain with a +0.25 s/s ramp,
exactly like the control. It did not. **The depth subscription demonstrably survives a reconnect.**

**4. `worst_rtt_ever_us_` earned itself on the bench.** At the drop the line reads
`-- ping : no round-trip yet (run 5056 ms)` — a ping was in flight when the socket died, and that
5,056 ms reading is the deepest the probe took. It exists *only* because the code review added a
run-level survivor; without it `note_connect` would have erased it seconds later. The review
finding was justified by the sibling instrument's history; it is now justified by an observation.

**5. Clean everywhere else.** `parse=0 price=0 ticker=0 unknown=0 trunc=0` throughout;
`ping 1298/1298` — every ping answered across 4.8 h; heap back to baseline exactly
(`free=50180 (+0)` after 85k+ frames, one -1024 B `largest` step at the reconnect then flat); the
71.6-minute uptime tearing bound was crossed at ~19:06 with no absurd reading in 1,750 age samples
or 1,298 ping samples. 762 watchdog greys are the panel honestly reporting >1 s book holes and
recovering in 98-430 ms each.

**Still open, unchanged:** the new build never saturated (mean 2,100 B per read against the
4,096 B buffer ceiling), so its margin to failure is bounded below at ~2.4x and still not measured
from above. That wants a synthetic-load or busy-venue run, not a longer soak.
