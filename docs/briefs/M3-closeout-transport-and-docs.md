# M3-closeout — the transport brief closes, the docs catch up, and the Anvil ask gets rewritten

**Track:** Agentic (Claude Code, **Opus** — plain `claude` in the terminal, not a Fable
session; nothing here needs Fable's ceiling and the owner's Fable budget is spent for the
week) · **Status:** Ready to start · **Size:** one session, ~4 focused parts, none large.
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

- ☐ Transport brief DoD ticked where the evidence supports it; unticked items named as owed.
- ☐ espws/nopp arms and `DC_OWNED_WS` deleted; host + firmware green.
- ☐ Window build is the default `depthcharge` env; provenance comment travels with it.
- ☐ Silence-recycle in `WsSupervisor` (5 min), host-tested, one commit, §9 row.
- ☐ ROADMAP: M3 status honest, Anvil backlog bullet rewritten to 2026-08-16 truth.
- ☐ DESIGN.html: statusbar, transport diagrams, strains 11/14/18 + new path-ceiling strain,
  status strip.
- ☐ Session log per protocol; tree clean and green.

## Out of scope

The Kraken adapter (M4). Any Anvil change. Another soak (the owner runs those). The M6
carrier board. Re-testing the ghosting on the panel (owner, at the bench: with clkphase
false, is the header speckle gone, and did the green right-edge dots go with it — the
answer decides whether M6 still owes the display side anything; record it in
`hardware/BRINGUP.md`).
