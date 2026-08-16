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
