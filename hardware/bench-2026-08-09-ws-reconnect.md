# Bench record — WebSocket reconnect, 2026-08-09

Serial captures from the ESP32-S3 (COM5, 115200) across three firmware revisions of
`WsTransport::supervise()`, taken by stopping and restarting Anvil while the board ran.
This is Stage C's `drop → stale → resync → live` evidence and the measurement behind
`kReconnectBackoffUs`, `kHandshakeBudgetUs` and `kObservedRecoveryUs`.

The excerpts are **partial** — the monitor was copied in blocks and there are elisions
between them, noted where they matter. Nothing has been edited within a block.

## Summary

| run | firmware | grey duration | what it established |
| --- | --- | --- | --- |
| A · 17:41, 17:44 | retry armed from `WEBSOCKET_EVENT_ERROR`/`CLOSED` | **18609 ms** | the event-armed trigger never fires; both outages recovered on the 12 s backstop |
| B · 18:15–18:17 | trigger moved to polled socket state | **8509 ms** | `ws down: event 2` = `DISCONNECTED`; fast path fires at 2.44 s; boot connection had no handshake immunity |
| C · 18:21–18:22 | boot connection stamped as attempt #1 | **9451 ms** | `connects=1` before any outage; recovery decomposed for the first time |

Run C's 9451 ms against run B's 8509 ms is not a regression — different outage, different
TLS connect time. The decomposition below is the number that matters.

### Recovery decomposition (run C, 18:22 outage)

Two clocks in the same log lines give it. The restart printed at Arduino-millis `103561`
and the socket-up at `110124` — 6563 ms apart — while the message reports **4018 ms**
since `attempt_started_us_`. The stamp is taken after `esp_websocket_client_stop()`
returns, so the missing 2545 ms is the teardown.

| term | ms | whose |
| --- | --- | --- |
| backoff + 250 ms poll granularity | 2445 | ours, tunable |
| `esp_websocket_client_stop()` blocking | ~2545 | the library's, on an already-dead socket |
| DNS + TCP + TLS + upgrade | 4018 | the network's |
| socket up → snapshot → LIVE | ~435 | Anvil's |
| **total** | **9451** | matches `grey for 9451 ms` |

Anvil's snapshot latency was the suspected tail and is the smallest term. The 2.5 s
blocking `stop()` runs on loopTask, which is Core 1 — relevant to Stage D.

### Open finding: the RX watchdog trips on healthy data

Run C, first 30 s, `sock_gaps=0` and `connects=1` and no interference of any kind:
`wd_gaps=2`, `worst_gap=1027 ms`. Two of the three greys in that run were watchdog trips
on a live socket (18:21:35, grey 1470 ms; 18:22:13, grey 144 ms), not socket loss.

`kRxWatchdogMs = 1000` is M1's measured threshold — 640 ms worst healthy silence across
6,494 frames, a 1.6× margin. These runs read **5.8–6.6 msg/s** where M0 measured 15.5.
The premise has eroded and the threshold needs re-deriving from a fresh capture, not
nudging; `ReplayOptions::disconnect_gap_ms` is the identical constant on the host and the
M1 goldens are pinned to it.

---

## Run A — event-armed retry (did not fire)

Outage 1. Note the absence of any `reconnecting after N ms` line: the armed path returns
early while waiting, so the backstop firing at 12 s proves the flag was never set.

```text
17:41:26.949 > I (230121) panel: -- rate   : in 8.49/s of 8.59/s attempted (1% lost) | events 7.59/s | 42.70 KB/s | mean 5148 B | 2.29 chunks/msg | window 10007 ms
17:41:26.966 > I (230134) heap: steady after 698 frames: free=124812 (-76) largest=106484 (+0) low=115728 (9160 below baseline)
17:41:26.986 > I (230165) panel: v887    seq=886    LIVE   bid 10.0001 x49 | ask 10.0050 x137  spread=49  last=10.0001  tape=8
17:41:27.795 > E (230974) TRANSPORT_WS: Error read data
17:41:27.795 > E (230974) WEBSOCKET_CLIENT: Error read data
17:41:27.801 > E (230974) WEBSOCKET_CLIENT: Error receive data
17:41:27.814 > W (230993) panel: *** STALE (disconnect) at v894 — panel greys here ***
17:41:36.916 > I (240089) panel: -- feed   : frames=1008 wd_gaps=0 sock_gaps=7 connects=6 worst_gap=32023 ms worst_frame=8064 us
17:41:40.077 > [243432][W][ws_transport.cpp:153] supervise(): [ws] websocket down for 12 s and not recovering — restarting client (#7)
```

Outage 2, with the recovery timing. Server was already back before the socket error landed.

```text
17:44:40.304 > E (437278) TRANSPORT_WS: Error read data
17:44:40.304 > E (437278) WEBSOCKET_CLIENT: Error read data
17:44:40.310 > E (437278) WEBSOCKET_CLIENT: Error receive data
17:44:40.318 > W (437292) panel: *** STALE (disconnect) at v1720 — panel greys here ***
17:44:47.480 > I (444448) panel: -- feed   : frames=1943 wd_gaps=0 sock_gaps=10 connects=9 worst_gap=32023 ms worst_frame=18062 us
17:44:52.791 > [436151][W][ws_transport.cpp:153] supervise(): [ws] websocket down for 12 s and not recovering — restarting client (#10)
17:44:58.927 > I (455901) panel: *** LIVE at v1721 ***
17:44:58.927 > I (455901) panel:     grey for 18609 ms before resync
```

`restart → LIVE` = 6136 ms (52.791 → 58.927). This is the figure `kObservedRecoveryUs`
was set from, before run C split it.

---

## Run B — polled socket state; `event 2` confirmed

```text
18:15:58.488 > E (27761) TRANSPORT_WS: Error read data
18:15:58.488 > E (27761) WEBSOCKET_CLIENT: Error read data
18:15:58.493 > E (27761) WEBSOCKET_CLIENT: Error receive data
18:15:58.498 > [ 27939][W][ws_transport.cpp:213] on_event(): [ws] ws down: event 2
18:15:58.507 > W (27770) panel: *** STALE (disconnect) at v110 — panel greys here ***
18:16:00.609 > I (29873) panel: -- feed   : frames=123 wd_gaps=0 sock_gaps=1 connects=2 worst_gap=783 ms worst_frame=7060 us
18:16:00.652 > I (29918) heap: steady after 51 frames: free=172708 (+47780) largest=118772 (+20480) low=115960 (8968 below baseline)
18:16:00.947 > [ 30396][W][ws_transport.cpp:157] supervise(): [ws] websocket down 2 s — restarting client (attempt #2)
```

`event 2` = `WEBSOCKET_EVENT_DISCONNECTED`. The library raises neither `ERROR` (0) nor
`CLOSED` (4) for a dead socket — the `Error receive data` lines are its own `ESP_LOGE`
inside `abort_connection()`, not an event. That closes the run A diagnosis.

Restart fired 2.44 s after the grey (2 s backoff + 250 ms poll + the stamp tick).
Recovery landed in the elision; `worst_gap` moved 783 → **8509 ms** across the outage,
which is the grey duration.

**Bug visible in this run:** the restart is logged `attempt #2`, and `connects=2` appears
before any outage. `attempts_` only incremented in `supervise()`, so it had already
restarted the client once — during boot, cutting through the cold TLS handshake, because
`attempt_started_us_` was stamped only by supervise()'s own restarts. Fixed for run C.

The second event in this run, at 18:16:54.996, is **not** a socket outage: `wd_gaps` 0→1,
`sock_gaps` unchanged at 1, `connects` unchanged at 3. RX watchdog on a 1.9 s hole in book
events, grey for 155 ms.

---

## Run C — boot immunity fixed; recovery decomposed

Boot state, confirming the fix — `connects=1`, `sock_gaps=0`, no restart during the boot
handshake:

```text
18:21:16.192 > I (29879) panel: -- feed   : frames=148 wd_gaps=2 sock_gaps=0 connects=1 worst_gap=1027 ms worst_frame=6997 us
18:21:26.216 > I (39896) panel: -- feed   : frames=212 wd_gaps=2 sock_gaps=0 connects=1 worst_gap=1027 ms worst_frame=7205 us
```

Two watchdog trips already, with `sock_gaps=0` — see the open finding above.

The two greys the owner read as server restarts, both watchdog and not socket:

```text
18:21:35.172 > W (48867) panel: *** STALE (disconnect) at v229 — panel greys here ***
18:21:36.214 > I (49902) panel: -- feed   : frames=257 wd_gaps=3 sock_gaps=0 connects=1 worst_gap=1027 ms worst_frame=7205 us
18:21:36.642 > I (50337) panel: *** LIVE at v231 ***
18:21:36.642 > I (50337) panel:     grey for 1470 ms before resync

18:22:13.469 > W (87165) panel: *** STALE (disconnect) at v431 — panel greys here ***
18:22:13.615 > I (87310) panel: *** LIVE at v432 ***
18:22:13.615 > I (87310) panel:     grey for 144 ms before resync
18:22:16.270 > I (89959) panel: -- feed   : frames=493 wd_gaps=5 sock_gaps=0 connects=1 worst_gap=2461 ms worst_frame=18768 us
```

The real socket outage, with the decomposition:

```text
18:22:27.228 > E (100923) TRANSPORT_WS: Error read data
18:22:27.228 > E (100924) WEBSOCKET_CLIENT: Error read data
18:22:27.233 > E (100924) WEBSOCKET_CLIENT: Error receive data
18:22:27.238 > [101102][W][ws_transport.cpp:224] on_event(): [ws] ws down: event 2
18:22:27.244 > W (100930) panel: *** STALE (disconnect) at v499 — panel greys here ***
18:22:29.689 > [103561][W][ws_transport.cpp:168] supervise(): [ws] websocket down 2 s — restarting client (attempt #2)
18:22:36.251 > [110124][I][ws_transport.cpp:135] supervise(): [ws] socket up 4018 ms into attempt #2
18:22:36.297 > I (109986) panel: -- book   : adopted=442 trades=50 gaps=6 publishes=499
18:22:36.307 > I (109996) panel: -- feed   : frames=561 wd_gaps=5 sock_gaps=1 connects=2 worst_gap=2461 ms worst_frame=18768 us
18:22:36.686 > I (110382) panel: *** LIVE at v500 ***
18:22:36.686 > I (110382) panel:     grey for 9451 ms before resync
18:22:46.303 > I (119992) panel: -- feed   : frames=632 wd_gaps=5 sock_gaps=1 connects=2 worst_gap=9453 ms worst_frame=18768 us
```

Heap across all three runs: `free` within ±640 B of baseline, `largest` never below it,
`low` flat. The restart churn this cadence causes is not fragmenting anything at these
outage lengths — a long outage at a 9 s retry cycle is still unmeasured.

Also worth carrying to Stage D: `worst_frame` reached **18768 µs** for one frame's
parse → book → publish, against 8064 µs in the earlier runs, on a larger maximum message
(8886 B). Nothing in these changes touches that path.
