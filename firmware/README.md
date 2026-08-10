# DepthCharge firmware — ESP32-S3

M3 **stage C**: the feed half. Wi-Fi → TLS WebSocket → Anvil → the same `engine/`
the host harness runs → `SnapshotChannel` → a serial console. **No panel yet** —
the HUB75 render task is stage D, and the console task here is its placeholder,
deliberately shaped like it (Core 1, `consume()`, redraw only on a new version).

---

## Build, flash, monitor

```sh
cp firmware/include/secrets.h.example firmware/include/secrets.h   # then edit it
cd firmware
pio run                       # build (both environments)
pio run -e depthcharge -t upload -t monitor   # the usual loop
pio device monitor            # 115200, esp32_exception_decoder
```

**Two environments, identical but for one symbol.** `depthcharge` is the
baseline (Wi-Fi modem sleep **off**); `depthcharge-ps` builds the same firmware
with `-D DC_WIFI_POWER_SAVE=1`, leaving modem sleep at the Arduino default
(`WIFI_PS_MIN_MODEM`). They are the two arms of the stall experiment below —
always name the environment on the command line, because `pio run -t upload`
with no `-e` builds *and uploads both*, and the second one wins.

`pio` is PlatformIO Core; on this desk it lives at
`%USERPROFILE%\.platformio\penv\Scripts\pio.exe`.

> **Do not lower `CORE_DEBUG_LEVEL`.** The precompiled framework ships
> `CONFIG_LOG_MAXIMUM_LEVEL=1` and `CONFIG_LOG_DEFAULT_LEVEL=1` (ERROR), so at
> the usual `CORE_DEBUG_LEVEL=1` every `ESP_LOGI`/`ESP_LOGW` here is compiled
> out and the board comes up apparently mute while working perfectly. Stage C's
> entire evidence is the serial log, so `platformio.ini` sets `3` (INFO) and
> `setup()` additionally calls `esp_log_level_set("*", ESP_LOG_INFO)` — both
> halves are needed, compile-time and runtime. Turn it down after stage D if the
> Arduino core's own chatter becomes annoying, not before.

The host build is unaffected and must stay green alongside it:

```sh
cmake --workflow --preset host      # or host-mingw on Windows
```

### `secrets.h`

Git-ignored. Two lines, and the example file is committed as the shape:

```cpp
inline constexpr char kWifiSsid[]     = "your-ssid";
inline constexpr char kWifiPassword[] = "your-password";
```

2.4 GHz only — the S3 has no 5 GHz radio. A dual-band SSID that never associates
is the first thing to check.

---

## How `engine/` gets in

It is compiled from where it lives. Two lines of `platformio.ini` are the whole
mechanism:

- `-I ../engine/include` puts the engine headers on the path;
- `build_src_filter` adds exactly **one** engine `.cpp`,
  `engine/src/anvil/anvil_frame_streaming.cpp`.

Nothing is copied, vendored or forked. The nlohmann parser is deliberately not
listed and must never be — it allocates per parse (invariant #7) and needs a
JSON library that has no business on the target. This mirrors the host build,
where every executable also names either `dc_engine_anvil` or
`dc_engine_anvil_streaming`; the firmware is just one more caller choosing.

---

## Board configuration

The stock `esp32-s3-devkitc-1` board definition is the **N8** variant (8 MB
flash, no PSRAM). The bench DevKit is an **ESP32-S3-WROOM-1 N16R8**
(`hardware/BRINGUP.md`), so `platformio.ini` overrides flash size, partition
table and memory type.

**If the board fails to boot or loops at startup**, the PSRAM lines are the
first thing to bisect — stage C uses no PSRAM at all:

```ini
board_build.arduino.memory_type = qio_opi   ; <- comment this out first
build_flags = -D BOARD_HAS_PSRAM            ; <- and this
```

Stage D will want PSRAM back, so record the answer either way rather than
leaving it commented.

---

## What runs where

| Task | Core | Job |
| --- | --- | --- |
| `esp_websocket_client` (its own) | unpinned | TLS, socket reads, hands chunks to the reassembler |
| `dc_feed` | **0** | reassembled frame → parse → adapter → book → `publish`. **Never logs** — see below |
| `dc_panel` | **1** | `consume()` → serial log (stage D: HUB75) |
| Arduino `loopTask` | 1 | the WebSocket supervisor, every 250 ms, and nothing else |

**`dc_feed` deliberately contains no `ESP_LOGx`, and stage D must not add one.**
Arduino routes `ESP_LOGx` to `log_printfv`, which `malloc`s for any line over 64
characters and takes the UART bus mutex with `portMAX_DELAY` around a busy-wait
on the TX FIFO. A single log line on the feed task would make it block on a
mutex the console task holds on the other core — the exact coupling invariant #4
forbids — through an allocation invariant #7 forbids. Everything worth reporting
is a counter in `FeedTask::Stats`, and the Live↔Stale transitions are printed by
the console from the published `DisplaySnapshot`, which is where they belong.

**The supervisor exists because auto-reconnect does not cover a clean close.**
On a server-side CLOSE, `esp_websocket_client`'s task echoes the close frame,
breaks out of its run loop and calls `vTaskDelete(NULL)`; `auto_reconnect` is
only consulted on the abort/error path, so nothing restarts it. Invariant #5
still holds — the panel greys, via `CLOSED` or the RX watchdog — but it would
grey *forever*, which at a bench reads as an intermittent hang hours into a
healthy run. It has since taken over the retry cadence as well, because this
vintage's `esp_websocket_client_config_t` has no `reconnect_timeout_ms` and the
library's own 10 s wait is unreachable: `loopTask` polls
`esp_websocket_client_is_connected()` at 250 ms and does `stop()` + `start()`
2 s after the socket goes down, then leaves that attempt 7 s of immunity so it
never kills a handshake it started. It has to be `loopTask` — those two calls are
documented as unsafe from the event handler — and `stop()` blocks for ~2.5 s on
an already-dead socket, which is a Core 1 stall stage D inherits.

The WebSocket client's task cannot be pinned — this vintage of
`esp_websocket_client_config_t` has no `task_core_id` — which is fine, because
it only memcpys into a slot and posts it. Everything that touches the book is on
`dc_feed`, and that is invariant #8 by construction rather than by convention.

Two hand-offs cross tasks and there are no others:

- `FramePipe` — four 16 KiB slots plus two FreeRTOS queues, transport → feed;
- `SnapshotChannel` — the engine's wait-free three-slot mailbox, feed → console.

### Why the feed is a task and not the WebSocket callback

The obvious design runs the pipeline straight inside the event callback. Two
facts rule it out. The RX watchdog has to fire when data *stops*, so it needs a
context that is awake during silence — and it raises `Gap{Disconnect}`, which
writes to the book, so it cannot live in a separate timer without creating a
second writer and breaking invariant #8. A task with a blocking timed receive
solves both: **the receive timeout is the watchdog**.

---

## Stale and resync

- **RX watchdog: 1000 ms.** M1's measured number — worst healthy inter-frame
  silence 640 ms, median ~69 ms, the observed outage 4,468 ms. The host replay
  uses the identical constant, which is what makes the M1 goldens a preview of
  this firmware rather than an analogy.
- The watchdog and the socket-close callback both raise `Gap{Disconnect}`, at
  most once per outage.
- Recovery is **transport-driven, never `seq`-driven**. Anvil's wire `seq` is a
  global counter and non-monotonic on one socket (M0: 42 backward steps in five
  minutes), so it is decoded for diagnostics and never used for ordering. On
  reconnect the fresh `snapshot` is the new baseline and the phase-1 book adopts
  it (`docs/vendor/anvil-protocol.md` §4).
- Only a `Snapshot` clears stale. A socket coming back up does not, and neither
  does a trade — tape is not book state.

---

## TLS

The server certificate is validated against a **pinned root: ISRG Root X1**,
embedded in `src/anvil_root_ca.hpp`.

This is not the `esp_crt_bundle` the brief asked for, and the reason is a
capability gap rather than a preference: the bundle is attached through
`esp_tls_cfg_t::crt_bundle_attach`, and the IDF 4.4 `esp_websocket_client_config_t`
that ships with Arduino-ESP32 2.0.14 has no such field. Reaching the bundle
means moving to pure ESP-IDF and re-running M2's first light.

The chain was measured against the live server rather than assumed:

```text
0  CN=anvil.garethcooke.com     issued by  Let's Encrypt YE1
1  Let's Encrypt YE1            issued by  ISRG Root YE
2  ISRG Root YE                 issued by  ISRG Root X2
3  ISRG Root X2                 issued by  ISRG Root X1   <- the pinned anchor
```

and the pin was verified with a control:

```sh
openssl verify -CAfile isrg_x1.pem -untrusted chain.pem leaf.pem   # OK
openssl verify -CAfile bogus.pem   -untrusted chain.pem leaf.pem   # error 20 at depth 3
```

ISRG Root X1 expires **2035-06-04**. If Anvil ever leaves the ISRG hierarchy the
handshake fails loudly at connect — it never degrades into a wrong ladder.

No `Origin` header is sent (ARCHITECTURE §7 closed this; M0 measured the
deployed upgrade accepting a client that sends none). The one-line fallback is
commented in `ws_transport.cpp` if that ever changes.

---

## The bench acceptance (owner)

**Run a host capture at the same time.** The first bench run measured ~6.7
messages/s attempted where M0 measured 15.5, and the log alone could not say
whether the server had slowed since July or the board was missing frames
upstream of the reassembler — the two have different fixes. A simultaneous
capture from the desk gives the other end of the comparison, and the board now
prints a `-- rate` line to compare it against. Start this *before* flashing, and
leave it running across the whole acceptance:

```sh
py tools/capture_anvil.py --out harness/replay/_local/anvil_101_m3c.ndjson --duration 300
build/host/dc_replay harness/replay/_local/anvil_101_m3c.ndjson   # prints frames/s, gaps
```

`_local/` is git-ignored (see `harness/replay/.gitignore`). Compare
`dc_replay`'s frames/s and max gap against the board's `-- rate` and
`worst_gap`. If the host also reads ~6/s the server has slowed and the 1000 ms
watchdog margin needs re-deriving; if the host reads ~15/s and the board reads
~6/s, frames are being lost between the socket and the reassembler and that is
ours to fix.

1. Flash, open the monitor, and confirm in order: Wi-Fi up with an IP, `ws`
   connecting, `websocket connected`, then `*** LIVE at v… (first frame) ***`.
2. Watch the steady lines — one a second, version climbing, `bid`/`ask`/`spread`
   moving, `tape=8` once trades have flowed.
3. **Pull the Wi-Fi** (unplug the AP, or take the board out of range).
   - Within ~1 s: `RX watchdog: no frame for 1000 ms -> Gap{Disconnect}` and
     `*** STALE (disconnect) at v… — panel greys here ***`.
4. **Restore it.** The client reconnects on its own; Anvil sends a fresh
   snapshot; expect `*** LIVE at v… ***` followed by `grey for NNNN ms before
   resync`, and the version advancing again.
5. Let it run ten minutes and read the `--` statistics block (every 10 s).

What the statistics should say on a healthy run:

| Line | Healthy |
| --- | --- |
| `errors` | `parse=0 price=0 ticker=0 unknown=0` |
| `pipe` | `oversize=0 no_slot=0 qfull=0 abandoned=0 cont=0` |
| `size` | `max` comfortably under the 16,384 B cap |
| `feed` | `worst_gap` well under 1000 ms except across the pull |
| `channel` | `published_v` and `consumed_v` tracking each other |
| `rate` | `0% lost`, and `in` matching what the host capture sees |
| `heap` | `free` delta **0** and `largest` not falling (see the caveat below) |

Reading the two that are new, and easy to misread:

- **`no_slot` is inbound loss, not consumer lag.** It counts whole WebSocket
  messages discarded before the parser because every slot was busy. It should be
  **0**; the first run hit 16% on two slots, which is why there are now four.
- **`superseded` on the `channel` line is the healthy one.** That is the
  latest-value mailbox dropping frames the consumer was too slow to take — which
  it is designed to do, losing nothing, because every `DisplaySnapshot` is a
  complete book state. A few per cent is normal.
- **`chunks/msg` near 3.00** confirms multi-chunk reassembly is genuinely
  running on the wire (an ~8 KB frame through a 4 KiB RX buffer), rather than
  only in the host test. `1.00` would mean frames have shrunk below 4 KiB and
  that path has stopped being exercised.

A non-zero `cont` would mean Anvil has started fragmenting at the WebSocket
layer, which this IDF vintage cannot reassemble correctly (no FIN bit) — see
`src/frame_reassembler.hpp`. A non-zero `oversize` means a book frame passed
16 KiB and `kFrameCapacity` should be raised. A climbing `revivals` in the `ws`
log lines means the supervisor is doing real work and Anvil is closing sockets.
If `no_slot` is still non-zero at four slots, raise `kFrameSlots` again before
reaching for `kWsRxBufferBytes` — the buffer is small on purpose (it is what
keeps `chunks/msg` at 3 and the reassembly path exercised), and trading that
away should be a decision, not a reflex.

Capture the log to `hardware/` or `docs/` as the stage C evidence.

---

## The stall characterisation run (owner) — do this *before* stage D

The open defect: on the bench the panel greys every 15–20 s with the socket up
(`wd_gaps=5` in 90 s, `sock_gaps=0`, `connects=1`, worst silence 2,461 ms) and
nothing at the desk end of the same LAN reproduces it — a socket deliberately
throttled to the board's own message rate has a worst book-event silence of
594 ms and trips the watchdog zero times in four minutes. So the seconds are
spent on the board (DESIGN.html strain 12). This run finds out **where**.

### The three lines to read

The statistics block now carries a distribution rather than a high-water mark:

```text
-- arrive : <100:1180 100-250:88 250-500:9 500-1k:1 1-1.5k:0 1.5-2.5k:0 >2.5k:0 | n=1278 worst=612 ms >1s=0 mode=-
-- event  : <100:1100 100-250:96 250-500:12 500-1k:3 1-1.5k:5 1.5-2.5k:2 >2.5k:0 | n=1218 worst=2461 ms >1s=7 mode=1-1.5k
-- a->e   : <1:900 1-5:210 5-25:104 25-100:3 100-500:1 0.5-1k:0 >1k:0 | n=1218 worst=88 ms | qwait=41230 us behind=2/6 msgs_in=1279
```

- **`arrive`** — inter-arrival gaps between whole messages, counted on the
  WebSocket client's task, *including* messages dropped for want of a slot. That
  inclusion is deliberate: slot exhaustion is caused by the feed being slow, so
  an arrival series that skipped them would blame the network for our own
  lateness.
- **`event`** — the same silence the RX watchdog greys on: gaps between events
  reaching the book. `>1s` is a count of the occasions the panel had grounds to
  grey, and on a run with `sock_gaps=0` it should equal `wd_gaps` on the `feed`
  line (a hole is recorded when the next event lands, so a socket outage leaves
  a sample here too, and one still-open hole is missing until it ends).
  `mode=` names the fullest bucket at or above 1 s — the shape of the population,
  which a maximum cannot give.
- **`a->e`** — for one message, arrival → the book having moved. `qwait` is the
  worst slice of that spent waiting for the feed task to run at all, and
  `behind` is the most messages ever left queued behind the one being processed
  (sampled after the dequeue, so it tops out one below the queue depth).

Cumulative since boot, so the **last block of the run is the answer**; the
histograms are diagnostics read across cores without synchronisation, so treat
them as human-readable and never gate anything on them.

### The verdict table

| `arrive` >1s | `event` >1s | reading |
| --- | --- | --- |
| fills | fills | **transport** — the bytes stopped. Wi-Fi, TLS, the socket, or Anvil's egress to this client |
| clean | fills | **ours** — the bytes arrived and the pipeline sat on them |
| clean | fills, `qwait` in seconds, `behind` non-zero | the feed task did not run: Core-0 starvation, or something blocking on this side |
| clean | fills, `qwait` small, `worst_frame` large | the work itself got slow — parser or book |

### The procedure

1. **Baseline, power save off.** `pio run -e depthcharge -t upload -t monitor`.
   Confirm the association line reads `power-save requested=OFF driver ps=0` —
   read back out of the driver with `esp_wifi_get_ps()`, not echoed from what we
   asked for, so it cannot agree with us by construction.
2. **Let it run ≥10 minutes with no deliberate disconnects**, and log the whole
   session to a file. `sock_gaps=0` and `connects=1` at the end is what makes the
   distribution a *steady-state* one; if the socket dropped, note it, because a
   reconnect costs a ~2.5 s blocking `stop()` on Core 1 and can put a >2.5 s
   sample in `event` that has nothing to do with the steady stall. **The absence
   of a >2.5 s sample in a run with `connects=1` is itself a result** — it says
   the 2,461 ms outlier was the reconnect and the steady population is the
   1–1.5 s one.
3. **Contrast, power save on.** `pio run -e depthcharge-ps -t upload -t monitor`,
   same duration, same conditions. Confirm `power-save requested=ON driver ps=1`.
4. **Record the `>1s` bucket count for each arm**, from both `arrive` and
   `event`, plus `n` and the window length so they are rates and not raw counts.
   Modem sleep parks the radio between DTIM beacons and shows up **arrival-side**
   — if it moves `arrive`, the mechanism is radio scheduling and the baseline's
   `setSleep(false)` is load-bearing; if it moves neither, power save is excluded
   as a candidate on evidence rather than by inspection of a `setSleep` call.
5. Commit the log and the reading to `hardware/bench-YYYY-MM-DD-feed-stall.md`,
   and update strain 12 with the ranking the data supports.

Pair it with a simultaneous `tools/capture_anvil.py` from the desk, as above —
the desk is the control for "was the wire quiet", and it costs nothing to have.

---

## Invariant #7 on the target

`heap_probe` samples free bytes, the **low-water mark** and the largest free
block across a steady-state window that opens after connect + first snapshot.

It is not the `heap_trace_start` the brief asked for, and that is a hard
constraint rather than a shortcut: the precompiled Arduino framework ships with
`CONFIG_HEAP_TRACING_OFF=y`, so there is no trace code to link against without
building ESP-IDF from source.

**Be clear about what the numbers can and cannot show.**
`heap_caps_get_minimum_free_size` is a *since-boot* minimum with no reset, so it
dips once early and then never moves — it cannot detect steady churn, and an
earlier version of this README claimed otherwise. What the three numbers do
establish is that nothing is **leaking** and nothing is **fragmenting**.

And the transport does allocate — statically knowable, so not an open question:
`esp_websocket_client` dispatches every event through `esp_event_post_to`, which
heap-copies the 28-byte payload and frees it when the handler returns. At a
4 KiB RX buffer and ~8.7 KB frames that is ~37 balanced `calloc`/`free` pairs a
second. So invariant #7 on this target reads as **no net allocation and no
fragmentation drift**: the engine half holds in the strong form and is proven
(host `alloc_probe` zero over both traces; no undefined `operator new` in the
parser's xtensa object), the transport half cannot without abandoning
`esp_websocket_client`, and bounded balanced churn of one fixed-size block is
compatible with what the invariant protects. The bench confirms the churn stays
balanced over hours; it does not decide whether it allocates. Full reasoning in
`src/heap_probe.hpp`; the ARCHITECTURE §9 amendment is written from this.

---

## Source map

| File | Job |
| --- | --- |
| `src/main.cpp` | statics, task creation, bring-up order |
| `src/ws_transport.*` | Wi-Fi, TLS WebSocket, Espressif event → `WsChunk` |
| `src/frame_reassembler.hpp` | chunks → whole messages. **No ESP-IDF** — host-tested by `harness/tests/test_frame_reassembler.cpp` |
| `src/gap_histogram.hpp` | fixed-bucket distributions for the stall hunt. **No ESP-IDF** — host-tested by `harness/tests/test_gap_histogram.cpp` |
| `src/frame_pipe.*` | the four-slot pool + queues, transport → feed, and the arrival histogram |
| `src/feed_task.*` | Core 0: the pipeline, the RX watchdog, the only book writer |
| `src/serial_console.*` | Core 1: `consume()` → log. Stage D replaces the body |
| `src/heap_probe.*` | invariant #7 instrumentation |
| `src/anvil_root_ca.hpp` | the pinned TLS root, with its provenance |
