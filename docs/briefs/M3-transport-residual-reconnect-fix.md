# M3 · transport residual — kill the reconnect greys

**Track:** Firmware + one owner Wi-Fi setting, bench · **Status:** Part 2 done (host-green, unflashed);
Parts 1 and 3 open
**Executor:** owner does the Deco setting and flashes/runs; CC does the firmware.
**Read first:** `ARCHITECTURE.md` §6 invariant #5 (two-state; watchdog on book-event) and §7
(the DepthCharge→Anvil couplings — this brief adds a DepthCharge→AP coupling, see Part 3);
`firmware/src/ws_transport.cpp` (`supervise()`, `kReviveAfterUs`, and the reconnect backoff);
the two ~11-min bench runs at −34 dBm (near node) this follows.

## Why

M3's steady-state flashing is **solved**. Moving the board from a far Deco node to the near one
took rssi −75 → −34 dBm and the 1–2.5 s mid-connection stalls vanished — a full 11-min run
showed only two ~1.1 s cadence blips, each greying ~130 ms. The board is exonerated end to end:
90 % idle, clean pipe, `a->e ≤ 22 ms`.

The one remaining source of greys is **socket drops**: ~4 per 11 min at −34 dBm, in **pairs**
(drop → reconnect → a second drop 15–80 s later, then ~7 min quiet), each a **6–9 s grey**. They
are **not signal** (−34 dBm, board idle) and **not Anvil** (the web client, and Anvil's box, are
on Ethernet and never drop through them). They are the ESP32↔Deco association — a mesh
steering/roaming loop herding a 2.4 GHz-only client. This brief cuts their frequency and shortens
any that survive.

## Part 1 — cut the frequency (owner, no code, do first)

Do the Deco-side setting before any firmware change; it may fix this with nothing to flash.

1. Put the board on a **dedicated 2.4 GHz-only SSID** (or the Deco "IoT" network if it has one)
   with **band-steering / "Smart Connect" OFF** and **fast-roaming (802.11 r/k/v) OFF**. (Lighter
   touch if you don't want a new SSID: just turn steering + fast-roaming off on the current one —
   then no `secrets.h` change or reflash is needed.)
2. If you made a new SSID, point `firmware/secrets.h` at it and reflash (owner handles creds).
3. Run ~20–30 min at the near node and watch the `-- feed` line: `connects` and `sock_gaps`
   should stop climbing. If drops go to ~0, Part 3 is unnecessary.

## Part 2 — cut the duration (CC) · ✅ done 2026-08-10, **but not the way this asked**

> **The premise below was false and is kept for the record.** The backoff is not additive with
> the socket bring-up: it is spent *inside* a 5 s sleep in the library's own task, and
> `esp_websocket_client_stop()` blocks for whatever is left of that sleep. Cutting the backoff
> moves time from a `vTaskDelay` into a blocking call and greys the panel for exactly as long.
> The evidence, and the change that does work, are below the original text.

~~A drop today is a 6–9 s grey: ~2 s `supervise()` grace + ~3.7 s to bring the socket up + backoff.
Shorten it:~~

- ~~Drop the client reconnect backoff (`reconnect_timeout_ms` / `WEBSOCKET_RECONNECT_TIMEOUT_MS`)
  toward ~2 s and trim the `supervise()` down-grace to match, so a drop greys for **~2–3 s**
  instead of 6–9.~~
- **Preserve the invariant** `kReviveAfterUs > reconnect_backoff + handshake_budget` — still
  binding, now spelled `kRetryCycleUs` in `ws_supervisor.hpp`, and joined by a second one the
  two-handle design needs.
- Measure grey-per-drop before/after from the `grey for N ms before resync` lines. **Still owed —
  this is the bench half and it is the owner's.**

### What the archive says

`esp_websocket_client` ships precompiled in Arduino-ESP32 2.0.14, so this was read out of
`libesp_websocket_client.a` with `xtensa-esp32s3-elf-objdump` rather than from a version of the
IDF source that may not be the one that was built:

| where | what |
| --- | --- |
| `.literal.esp_websocket_client_init + 0x30` | `0x2710` = 10000 → `wait_timeout_ms` is 10 s |
| `.text.esp_websocket_client_task + 0x3b5` | `if (state == WAIT_TIMEOUT) vTaskDelay(wait_timeout_ms / 2)` → **5000 ms** |
| `.text.esp_websocket_client_stop + 0x3a` | `run = false`, then `xEventGroupWaitBits(STOPPED_BIT, …, portMAX_DELAY)` |
| `.text.esp_websocket_client_task + 0x36e` | `if (!config->auto_reconnect) { run = false; break; }` |
| `.text.esp_websocket_client_init + 0x535` | `auto_reconnect = !config->disable_auto_reconnect` |

The task cannot see `run = false` until it wakes, so **stop() blocks for `5000 ms −
(time since the abort)`** and the sum is a constant. Run C's own numbers close it:

| term | ms |
| --- | ---: |
| our backoff + 250 ms poll | 2445 |
| blocked in `stop()` | 2545 |
| — the library's 5 s sleep, split in two — | **4990** |
| DNS + TCP + TLS + upgrade | 4018 |
| Anvil's snapshot → LIVE | 435 |
| **total** | **9443** vs the panel's `grey for 9451 ms` |

### What was done instead

`WsTransport` now keeps **two client handles**. A reconnect starts the idle one and publishes it
as live; the handle that just dropped is left to expire on its own clock and is never waited for.
`esp_websocket_client_stop()` is not called anywhere, and `disable_auto_reconnect = true` — without
which a retired handle would wake 10 s later and open a second live socket to Anvil behind our back.

The whole 5 s leaves the grey path. Predicted grey per drop, from the terms above:
**~4.7 s** (250 ms detect + 250 ms backoff + connect 4018 + snapshot 435), against 9.45 s measured.
**Not the 2–3 s this brief asked for**, and the reason is the 4018 ms connect, which is now the
largest term by 8× and has never been decomposed — so the same change adds a `dns=N ms` figure to
the restart line, taken on our clock immediately before the connect (which also warms lwIP's cache,
so the library's own lookup is a hit). The next bench run splits DNS from TLS for the first time.

Also here, because it is the same file and has been open for four sessions: the reconnect *policy*
is extracted to `firmware/src/ws_supervisor.hpp`, ESP-IDF-free and host-tested. It gained a Wi-Fi
association gate — an attempt is **held**, not deferred, while the station is unassociated, so an
AP-steering drop retries on the first poll after re-association instead of a full cycle later.

## Part 3 — pin the association (CC, only if Part 1 doesn't kill the drops)

If drops persist after the dedicated SSID, pin the STA so the Deco can't herd it:

- `wifi_config.sta.bssid_set = true` with the **near node's BSSID**, roaming assist off. The board
  then rejoins the *same* box on every reconnect, which breaks the pair-loop (reconnect →
  immediate re-steer → second drop).
- **New coupling to record (§7, log in §9):** a pinned BSSID means the board will *not* fail over
  to another mesh node if the near one reboots or moves — a deliberate trade for a fixed desk
  device. Record it beside the TLS-pin and republish-cadence couplings; a node change becomes a
  firmware/secrets event, not a transparent one.

## Acceptance

At the near node, ~20–30 min: `connects` / `sock_gaps` flat (drops ≈ 0 after Part 1, or after
Part 3 if needed), and any drop that does occur greys for ~2–3 s, not 6–9. Two-state unchanged,
`kRxWatchdogMs` still 1000 ms, no golden moved (firmware/network only), invariant #7 intact.
Append the run, the Deco settings used, and the final reconnect constants as a session-log block
on `docs/briefs/M3-live-anvil-on-the-panel.md`.

---

## Session log

### 2026-08-10 · Opus 5 · the backoff was never the term; the sleeper was

**Done.** Part 2, by refusing its premise and fixing the thing the premise was hiding. Part 1 is
the owner's and is untouched. Part 3 is not started and is still correctly gated on Part 1.

- **The finding**, above: the 2 s backoff and the 2.5 s blocking `stop()` are two halves of one
  5 s sleep inside `esp_websocket_client`'s own task, so cutting the backoff buys nothing. Read
  out of the shipped archive (five call sites, tabulated above) and cross-checked against the
  committed bench: 2445 + 2545 = 4990 against 5000, and the decomposition totals 9443 ms against
  the panel's `grey for 9451 ms`. **This is the entry's main output** — without it the obvious
  next session cuts the backoff, measures no change, and concludes the lever is exhausted.
- **Two client handles**, `disable_auto_reconnect = true`, no `stop()` anywhere. Details and
  costs in `ARCHITECTURE.md` §9 (2026-08-10). Measured by building HEAD with and without in a
  scratch worktree: **+8 B RAM, +956 B flash**; ~10 KiB heap for the spare's buffers, taken once
  at boot, so invariant #7 is untouched. Only one TLS context is ever live — `abort_connection()`
  closes the transport at the drop, which the 2026-08-09 bench saw as `free=+47780`.
- **`firmware/src/ws_supervisor.hpp`** — the host-test extraction open since 2026-08-09, now
  done because this change is the one that needed it. ESP-IDF-free, owns every reconnect
  constant and both static_asserts, 10 test cases in `test_ws_supervisor.cpp`.
- **`dns=N ms`** on the restart line, on our clock, immediately before the connect. Splits the
  4018 ms term that is now 85% of the remaining grey and has never been decomposed.

**Decisions, with why.**

1. *Two handles rather than waking the sleeper.* `xTaskAbortDelay()` on the library's task would
   also work and is ~30 lines against ~120 — both `INCLUDE_xTaskAbortDelay` and
   `INCLUDE_xTaskGetHandle` are 1 in the shipped `FreeRTOSConfig.h` and both symbols are in
   `libfreertos.a`, so it was live, not hypothetical. Rejected because the abort has to be issued
   from a second context while `stop()` blocks in the first, and the callback would then be
   holding a task handle across the window in which that task deletes itself — a freed-TCB
   dereference whose failure mode is heap corruption on an object meant to run for a week. The
   spare handle costs 10 KiB and touches nothing but public API.
2. *Auto-reconnect off.* This reverses the 2026-08-09 decision to leave it on as a backstop. It
   is not a free reversal: `supervise()` is now the **only** recovery path. Justified because the
   backstop was given 12 s twice on the bench and recovered neither outage, and required because
   a retired handle that reconnects itself is a second live socket into one reassembler.
3. *The backoff is one poll period, not zero.* The pass that first sees the socket down can only
   record *when* it noticed, so one period is what the loop shape costs anyway. The real "don't
   retry into a radio that has not rejoined" protection is the association gate, which is a
   condition rather than a delay.
4. *The attempt clock is stamped at the decision, not after the platform call.* That ordering
   existed because `stop()` blocked for up to 2.5 s before an attempt could begin. There is no
   `stop()`; stamping early is now both simpler and the conservative direction.

**One real bug, found by the new tests and fixed in the header rather than the test.** Both
supervisor clocks used `0` for "unset", and the boot connection is stamped from
`esp_timer_get_time()` — a clock that reads 0 at reset. The first test written against the
extracted policy produced `t = 0`, which read as "no attempt in flight" and opened a second
connection straight through the cold TLS handshake: the exact bug the boot stamp was added to fix
in the first place, re-entering through an encoding. Each clock carries its own validity flag now.
This is the argument for the extraction in one paragraph — on the board it would never have fired,
because `esp_timer_get_time()` is a second or two in by the time `start()` runs.

**State of the tree.** `cmake --workflow --preset host-mingw` green: **11/11 ctest**, 167 doctest
cases in `dc_tests`, `dc_tests_streaming` unchanged. Firmware builds clean. No engine change, no
golden moved, `kRxWatchdogMs` still 1000 ms, two-state unchanged, invariant #7 intact.
**Caveat: another session was editing this tree concurrently** (the `reject_log` / connect-time
parse-burst work, which this brief lists as out of scope). Everything above was verified with both
changes present; the memory figures were isolated in a scratch worktree for that reason.

**NOT DONE — the bench half, and it is the point.** Nothing here has run on hardware. No
before/after `grey for N ms`, no `dns=` figure, no confirmation that a drop now recovers on the
spare handle at all.

**Exact next step:** Part 1 (owner: Deco steering + fast-roaming off), then one ≥20 min run at the
near node on this firmware, recording per drop: `grey for N ms before resync`, the `feed down N ms
— opening handle X (attempt #N, dns N ms)` line, and `socket up on handle X, N ms into attempt #N`.
Three things fall straight out of it — whether the grey is now ~4.7 s; whether DNS or TLS owns the
4 s; and whether `connects`/`sock_gaps` are flat, which decides Part 3. Also worth one check the
console gives free: steady-state `free=` should sit ~10 KiB below the previous run's 124,812, and
if it does not, the spare handle is not costing what this entry claims. Append the run as a
session-log block on `docs/briefs/M3-live-anvil-on-the-panel.md`, as Acceptance asks.

---

*Out of scope:* the connect-time `parse` burst (~1,281 frames rejected in the first ~60 s, then
flat) is a separate parser/protocol ticket, not this. It doesn't touch the book or the drops.
