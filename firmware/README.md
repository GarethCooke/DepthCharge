# DepthCharge firmware — ESP32-S3

M3 **stage D**: the whole object. Wi-Fi → TLS WebSocket → Anvil → the same
`engine/` the host harness runs → `SnapshotChannel` → a 64×64 HUB75 ladder on
Core 1, with the serial log beside it as the bench evidence.

The render task is `src/render_task.*`. It was `src/serial_console.*` through
stage C, which existed to prove the three properties stage D was told not to
break — Core 1, `consume()`-gated redraw, and reading nothing but the
`DisplaySnapshot` (invariant #8). Those are unchanged; the body draws now.

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
first thing to bisect — nothing here uses PSRAM at all:

```ini
board_build.arduino.memory_type = qio_opi   ; <- comment this out first
build_flags = -D BOARD_HAS_PSRAM            ; <- and this
```

**Stage D does not want PSRAM back, and this is the answer stage C left open.**
The HUB75 framebuffers must be internal DMA-capable RAM; the library only places
them in SPIRAM when `SPIRAM_DMA_BUFFER` is defined, which this build never
defines. The two lines above are about the N16R8 board override and nothing else.
`Panel::begin()` prints `psram N B (present=yes, NOT used for the framebuffer)`
at boot so the distinction is on the record every run rather than in a comment.

---

## What runs where

| Task | Core | Job |
| --- | --- | --- |
| `esp_websocket_client` (its own) | unpinned | TLS, socket reads, hands chunks to the reassembler |
| `dc_feed` | **0** | reassembled frame → parse → adapter → book → `publish`. **Never logs** — see below |
| `dc_panel` | **1** | `consume()` → HUB75 ladder, then the serial log. Priority 3, 33 ms period |
| HUB75 DMA (LCD_CAM + GDMA) | — | refreshes the panel from the framebuffer autonomously; no task, no CPU |
| Arduino `loopTask` | 1 | the WebSocket supervisor every 250 ms, and an rssi sample every 500 ms |
| FreeRTOS idle | 0 and 1 | the per-core idle probe's hook — a register read and an add, and the reason an idle core spins rather than sleeping (see below) |

**`dc_feed` deliberately contains no `ESP_LOGx`, and stage D must not add one.**
Arduino routes `ESP_LOGx` to `log_printfv`, which `malloc`s for any line over 64
characters and takes the UART bus mutex with `portMAX_DELAY` around a busy-wait
on the TX FIFO. A single log line on the feed task would make it block on a
mutex the console task holds on the other core — the exact coupling invariant #4
forbids — through an allocation invariant #7 forbids. Everything worth reporting
is a counter in `FeedTask::Stats`, and the Live↔Stale transitions are printed by
the render task from the published `DisplaySnapshot`, which is where they belong.

**`dc_panel` is the single consumer, and the panel and the log come out of the
same `consume()`.** A second task reading `DisplaySnapshot` would be a third
participant at the one meeting point invariant #8 allows exactly two. It also
runs at **priority 3 against `loopTask`'s 1**, on the same core: the supervisor
runs there, and the panel has to keep drawing the grey through whatever it is
doing. The serial block costs this task a few blocked milliseconds and that is
affordable precisely because the HUB75 DMA refreshes from the framebuffer with
no CPU at all — a blocked render task is one skipped redraw, never a dark panel.

**The supervisor exists because auto-reconnect does not cover a clean close.**
On a server-side CLOSE, `esp_websocket_client`'s task echoes the close frame,
breaks out of its run loop and calls `vTaskDelete(NULL)`; `auto_reconnect` is
only consulted on the abort/error path, so nothing restarts it. Invariant #5
still holds — the panel greys, via `CLOSED` or the RX watchdog — but it would
grey *forever*, which at a bench reads as an intermittent hang hours into a
healthy run. It has since taken over the retry cadence as well, because this
vintage's `esp_websocket_client_config_t` has no `reconnect_timeout_ms` and the
library's own 10 s wait is unreachable from the config.

**It recovers onto a SPARE HANDLE, and never waits for the dead one.** There are
two `esp_websocket_client` handles, both built at boot; `loopTask` polls
`esp_websocket_client_is_connected()` at 250 ms and, one poll after the socket
goes down, starts the idle handle and publishes it as live. The handle that
dropped is left to expire on its own clock. `esp_websocket_client_stop()` is not
called anywhere, and `disable_auto_reconnect` is set — without it the retired
handle would wake 10 s later and open a *second* live socket to Anvil.

The reason is worth knowing before touching any of it: a socket that aborts puts
the library's own task into `vTaskDelay(wait_timeout_ms / 2)` — **5 seconds** —
and `stop()` sets `run = false` and then blocks until that task wakes. So the
old design's 2 s backoff and its 2.5 s blocking `stop()` were two halves of one
5 s sleep (2445 + 2545 = 4990 on the 2026-08-09 bench), and shortening the
backoff would have moved time between them without moving the grey by a
millisecond. All five facts were read out of the precompiled archive with
`objdump`; the offsets are cited in `ws_supervisor.hpp` and the consequences in
DESIGN §08 strain 14.

The *policy* — when an attempt is due, how long it is immune, and the Wi-Fi
association gate that holds an attempt rather than deferring it — is
`firmware/src/ws_supervisor.hpp`, which is ESP-IDF-free and host-tested in
`test_ws_supervisor.cpp`. `WsTransport` keeps the platform half. It still has to
be `loopTask` — `start()` is documented as unsafe from the event handler — and
the one call there that can still block is the DNS warm, which runs only when the
station is associated and prints what it cost as `dns=N ms`.

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

## The panel

64×64 HUB75, `ESP32-HUB75-MatrixPanel-DMA` pinned at **3.0.15**, driven from
`src/panel.cpp` with M2's verified pin map and `FM6124` init.

**The pins are assigned by field name, and that is not style.** The library's
config takes them positionally as
`int8_t r1, g1, b1, r2, g2, b2, a, b, c, d, e, lat, oe, clk`, so a brace list
ends `…, 12, 13, 11` — LAT, OE, CLK — and **not** `11, 12, 13`.
`hardware/BRINGUP.md` records that as the trap M2 stepped over and warns the
order is version-dependent; it was checked against the vendored 3.0.15 header and
is as above. Assigning by name means the next version can reorder them and
nothing here has to notice. The library's own ESP32-S3 defaults are *different*
from BRINGUP's map (A=18, B=8, C=3, D=42, E=−1, LAT=40, OE=2, CLK=41), so an
omitted pin is a silently wrong panel rather than a build error.

**`NO_GFX` is set, and it does two jobs.** It drops Adafruit GFX (and BusIO) for
a text path this firmware does not use — `ladder_font.hpp` owns a 3×5 font of 41
glyphs, 205 bytes of `.rodata`, because the 64-row budget has to be provable in
`ctest` rather than discovered on the panel. And it removes `print()` and
`setTextColor()`, which is what makes invariant #5's palette selection
*structural*: with no GFX there is no way to put anything on this panel without
naming an `Ink`, and `ladder_render.hpp` cannot name a colour.

### How the grey is guaranteed

The renderer emits `Ink` — a role. The palette is chosen **once**, from
`snap.status`, where `PanelCanvas` is built in `RenderTask::paint()`, and the
canvas is the only place an `Ink` becomes an RGB value. `kStalePalette` is proven
entirely on the grey ramp **at compile time**:

```cpp
static_assert(all_grey(kStalePalette), ...);          // no hue anywhere
static_assert(none_black_except(kStalePalette, Ink::Count), ...);  // grey, never blank
```

A new `Ink` without a stale entry fails the build; a stale entry given a hue
fails the build. `test_ladder_render.cpp` pins the complement — for the same
book, the `Ink` grid over the ladder region is **identical** Live and Stale.
Geometry stays, hue goes.

### The row budget

```text
row  0.. 4   header      symbol left; last price right when live, stale reason when not
row      5   rule
row  6..32   asks         worst at the top, asks[0] hard against the spread
row     33   the spread gap
row 34..60   bids         bids[0] hard against the spread, worst at the bottom
row     61   rule
row 62..63   tape strip   last-price sparkline, and the render heartbeat at (63,63)
```

5 + 1 + 27 + 1 + 27 + 1 + 2 = **64 exactly**, with `kDisplayLevels` intact at 27
a side. `kLevels` is *derived* from the leftover rows and clamped to
`kDisplayLevels`, so if the header ever needs a second line the ladder loses
levels and the build stays green — which is the brief's instruction ("draw fewer
levels; never change `kDisplayLevels`", that being a §5 change to `engine/`). It
cannot silently overrun: `kStripRows` would go negative and a `static_assert`
fires.

Bars are `qty * 64 / max_qty` in `int64`, normalised across **both** sides so the
ladder is one picture of the book; a level that exists is never invisible (one
pixel floor), the same rule the M1 console ladder applies. Prices go through
`depthcharge::format_scaled` into a stack buffer — no `String`, no `%f`, no
`std::string` (invariants #3 and #7).

**The heartbeat pixel** at (63,63) toggles on every drawn frame. Invariant #5
covers the *feed* going quiet; nothing covers the *renderer* dying, and a dead
render task leaves exactly the frozen ladder ARCHITECTURE calls the one
unacceptable output. It doubles as a draw-rate read at the bench.

**The sparkline and the trade flash are render-side sampled state** — a fixed
ring in `LadderView`, never a new `DisplaySnapshot` field, which would be a §4/§5
change to the vocabulary the two cores share for a cosmetic want. `last_px`
legitimately advances while Stale (the tape is still real) and needs no special
case: the grey wash covers it.

### Memory, and why the boot order is what it is

The framebuffers are **internal DMA-capable RAM, never PSRAM** — the library only
places them in SPIRAM when `SPIRAM_DMA_BUFFER` is defined, which this build never
defines (`BOARD_HAS_PSRAM` in `platformio.ini` is the N16R8 board override, a
different thing). Cost is `32 rows × 64 px × depth × 2 B × buffers`, so
**65,536 B** at eight-bit colour double buffered.

That is the same pool Wi-Fi, lwIP and mbedTLS draw from, so the order decides
which half degrades when it is tight — and the two are not comparable. A panel
that has to drop a colour bit is cosmetic; a TLS handshake that cannot allocate
is a dead object. So:

1. Wi-Fi associates,
2. `Panel::begin()` sizes itself against what is actually left, holding back
   `kReserveInternalBytes` (96 KiB, deliberately generous),
3. the WebSocket client — which allocates a TLS context per handshake for the
   rest of the run — comes up last.

The colour depth is a **calculation, not a retry loop**, and the library's own
source is why: when a row buffer fails to allocate, `setupDMA()` returns false
leaving everything it already took attached, above a comment reading
`// TODO: should we release all previous rowBitStructs here???`. A second
`begin()` would `emplace_back` *more* rows onto the same vector, so a failed
attempt costs most of a framebuffer permanently and the retry beneath it fails
for a reason unrelated to the depth it was testing. One measurement, one attempt.
The ladder is depth 8→3 **double buffered first**, and only then single buffered
— tearing on a book that redraws 13 times a second is a visible defect on a panel
whose whole job is to be believed.

`Panel::begin()` is **never fatal**: a board that cannot light a panel still runs
the feed and prints the whole acceptance log, the same honest degradation the
idle probe takes. It also runs whether or not Wi-Fi came up and *before* the halt,
so a board with a bad `secrets.h` shows an honest grey `RESYNC` frame rather than
a dark panel that reads as "powered off".

**Brightness is 160/255, and it is not a power decision.** M2 measured 2.6 A at
full white and 0.25 A at representative ladder content against a 5 V/5 A supply,
so 255 has ~2× headroom. 160 is an eye-comfort choice at desk distance. One
constant (`kPanelBrightness`); raise it for the acceptance photograph if the
camera wants it.

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
4. **Restore it.** The supervisor opens the spare handle; Anvil sends a fresh
   snapshot. Expect, in order: `feed down N ms — opening handle B (attempt #2,
   dns N ms)`, then `socket up on handle B, N ms into attempt #2`, then
   `*** LIVE at v… ***` followed by `grey for NNNN ms before resync`, and the
   version advancing again.
   - While the station is still down you should see **one** line —
     `reconnect due but the station is not associated — holding` — and not one
     every 250 ms. The attempt then goes out on the first poll after the station
     is back, not a retry cycle later.
   - The handle letter must **alternate** across drops (A → B → A). If two
     consecutive recoveries name the same handle, or `handle X would not start`
     appears, the assumption DESIGN §08 strain 14 rests on has moved and the
     archive needs re-reading before anything else is believed.
   - `grey for NNNN ms` is the number this change exists to move: 9,451 ms on
     2026-08-09, predicted ~4,700 ms now, of which ~4,000 is the connect. The
     `dns=` figure says how much of that 4 s is the resolver — the first split of
     that term anyone has taken.
5. Let it run ten minutes and read the `--` statistics block (every 10 s).

What the statistics should say on a healthy run:

| Line | Healthy |
| --- | --- |
| `errors` | `parse=0 price=0 ticker=0 unknown=0` |
| `reject` | **absent entirely** — the line only prints when something was rejected |
| `pipe` | `oversize=0 no_slot=0 qfull=0 abandoned=0 cont=0` |
| `size` | `max` comfortably under the 16,384 B cap |
| `feed` | `worst_gap` well under 1000 ms except across the pull |
| `channel` | `published_v` and `consumed_v` tracking each other |
| `rate` | `0% lost`, and `in` matching what the host capture sees |
| `age` | `drain 100%` and an `age` under a second — see below, and note this is the one line that has never yet read healthy |
| `heap` | `free` delta **0** and `largest` not falling (see the caveat below) |

Reading the two that are new, and easy to misread:

- **`reject` is the breakdown of a non-zero `parse`, and it prints the bytes.**
  It is silent on a healthy run on purpose: `errors` already says `parse=0`, and a
  line of zeroes every ten seconds would bury the one that matters. When it does
  appear there are two shapes of it, and they answer different questions:

  ```text
  -- reject : n=1281 not-json=1281 no-type=0 bad-shape=0 bad-price=0 other-ticker=0 | logged=10 suppressed=1271 over 1 connects
  -- reject : #1 c1/#1 +412 ms not-json len=4096 head[{"type":"book","seq":362011,"ticker":101,"bids":[{"price":"10.0352","qty":24,] tail[0349","qty":214,"orders]
  ```

  The tally is the run; the `#n` lines are the first **ten payloads of each
  connect**, printed the moment they happen. Read them in this order, because the
  three statuses have three different owners:

  | What you see | What it means | Who fixes it |
  | --- | --- | --- |
  | `no-type` on a whole payload ending `}` | a frame shape this client does not know | the venue — skip-not-error it, and document the frame in `docs/vendor/anvil-protocol.md` |
  | `not-json`, `len` a multiple of 4096, `tail` ending mid-token | the message was cut at an RX-buffer boundary | ours — the reassembler / `kWsRxBufferBytes` |
  | `not-json` with a `SPLIT@nnnn` | two messages landed in one buffer | ours — the WebSocket client under burst load |
  | `bad-shape` on a whole `book`/`trade` payload | Anvil changed a payload | re-vendor the protocol |

  `SPLIT@` is worth knowing about before you need it: a spliced buffer *begins*
  like a valid frame and *ends* like a valid frame, so head and tail both look
  right and the reject reads as inexplicable. The offset is computed rather than
  left to be spotted.

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

### `-- age` — how OLD the book is, which is not the same as whether it stopped

```text
-- age    : age 97.4 s (worst 124.0 s, run 124.0 s) | summary 172 of 420 over 210.0 s | drain 41% this window (8 in 10001 ms)
```

**Every other line in this block can read perfectly while the panel shows a book
a hundred seconds behind the market.** That is not hypothetical — it is what the
2026-08-11 bench was doing with `parse=0`, all pipe counters zero and both cores
85–92% idle. The watchdog measures *stopped*, the parse counters measure *wrong*,
the stall probe measures *whose fault the stopping was*. None of them measures
*old*, and neither did invariant #5.

Anvil's `summary` is a fixed **2 Hz timer broadcast**, so its rate is a fact
about elapsed time rather than about trading — the only clock on this wire. The
deficit between 2.00/s and what this socket receives, integrated since the socket
came up, is the age. It costs one counter the adapter was already keeping.

| Field | Healthy | What a bad value means |
| --- | --- | --- |
| `drain` | **100%** | the instantaneous half: summaries this window against the 2.00/s broadcast. 41% means the board is receiving 41% of the stream and falling behind by 0.59 s per second |
| `age` | **under 1 s** | how far behind the book on the panel is. Resolution is one summary period, so a socket that is exactly current reads a 0–500 ms sawtooth |
| `worst` | tracks `age` | the peak on **this connection**; reset when the socket reconnects, because the backlog dies with the socket |
| `run` | tracks `worst` | the peak across the **whole run**, retained through reconnects. `worst` far below `run` means reconnects have been flushing a backlog and hiding it — which is exactly why the 2026-08-09 86-minute run looked healthy at 21 reconnects |
| `seq` | climbing | the **join key**: Anvil's global wire seq. Join it against a simultaneous desk capture and the lag is a subtraction, sampled at the publish rate — the measurement, where the stopwatch is a biased proxy for it |
| `AHEAD n.n s (m%)` | **absent** | summaries arriving faster than 2 Hz for long. The denominator is wrong for this server and every age above is scaled by the ratio — re-measure with `py tools/anvil_freshness_probe.py --reference-only`. Tested as a *ratio* against a 5% tolerance, deliberately: a ratio is scale-free, so it survives the 500 ms constant being wrong by any amount, a venue that changes its period, and the board's own crystal error — none of which an absolute threshold survives. The wire itself is 2.0003/s over 30 minutes, so the tolerance is ~3,000× the real drift and this fires only on something structural |

**Read `age` beside `drain`, never alone**, and read the assumption in
`src/staleness.hpp` before quoting either. The deficit is an age only if the
missing summaries were **queued** rather than dropped; a rate cannot tell those
apart, and mistaking one for the other is precisely the error that cost this
milestone the 2026-08-09 "Anvil sheds evenly" finding. That assumption has been
checked at the desk for Anvil and holds (linear queuing, every kind thinned
equally, no plateau — `hardware/bench-2026-08-11-feed-lag.md`); it is **not**
inherited by Kraken or Binance.

A rejected `summary` counts as missing and inflates the age, so cross-check
`-- errors parse=` in the same block.

---

## The feed-lag run (owner)

The one measurement this firmware cannot make about itself: **is the age the
board reports the age the panel actually has?**

**Do not use a stopwatch for it.** Freezing the web client and timing until the
panel catches up measures `age ÷ drain`, not `age` — the panel's market-time
cursor advances at the drain fraction, so at this board's ~0.41 the stopwatch
reads about **2.4×** whatever `-- age` prints, and that is the two instruments
agreeing. The full derivation, and its confirmation against this project's own
committed desk data, is in `hardware/bench-2026-08-11-feed-lag.md`.

Use the **`seq` join** instead. Anvil's wire seq is one global counter, so the
same broadcast carries the same value on every socket; the board prints its
latest one on the `-- age` line, and joining that against a simultaneous desk
capture gives a continuous lag curve at the publish rate with no stopwatch, no
human and no `1/f`. Three rules:

1. **Start the desk control first**, and leave it up for the whole session:

   ```sh
   py tools/anvil_freshness_probe.py --reference-only --seconds 1800
   ```

2. **Log to a file, from `firmware/`.** `log2file` is in `monitor_filters` now,
   beside `time` — the per-line timestamp is what every interval is read off, and
   the log lands in the gitignored `firmware/logs/`. `pio run` has **no `-f`
   option**; the filters come from `platformio.ini`.
3. **Do not require one connection.** `-- age` resets per connection and reports
   its own window, so each connection is a complete lag curve from zero to peak.
   On a board that drops every few minutes, twenty short curves are better data
   than one long run that never happens. Record the wall-clock of every drop —
   regular means a queue filling to a bound, scattered means a link.

And the desk tool that settles the mechanism rather than the magnitude — two
sockets, one throttled, matched on wire `seq`, which is the measurement
`anvil_drain_probe.py` could not make:

```sh
py tools/anvil_freshness_probe.py --seconds 150 --delay 0.25
```

**Never run that one beside a live bench session.** It deliberately starves a
socket toward tens of megabytes of server-side queue, and if that pressure makes
Anvil drop other sockets, a board drop caused by the probe is indistinguishable
from the board's own backlog closing its connection — the confound manufactures
the evidence. `--reference-only` is one normal socket and is safe concurrently.

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

### Stage D — the panel acceptance

Everything above still applies; this is what the panel adds. Read it beside the
serial log, because the whole point of stage D is that the two must agree.

**Before the run, three lines at boot say whether the panel got what it needed:**

```text
I panel-hw: psram 8388608 B (present=yes, NOT used for the framebuffer)
I panel-hw: dma-internal free=NNNNNN largest=NNNNNN | reserve=98304 budget=NNNNNN | rungs d8=65536 d6=49152 d4=32768 d3=24576 (double), d8single=32768
I panel-hw: UP: 64x64 depth=8 double-buffered brightness=160 refresh=NNN Hz | predicted=65536 measured=NNNNN B | dma-internal free NNNNNN -> NNNNNN
```

`predicted` is our arithmetic, `measured` is the heap delta, and the library
prints its own `Allocating N bytes memory for DMA BCM framebuffer(s)` just above.
**Three independent numbers for the same allocation** — they should agree to
within the allocator's per-block overhead, and a disagreement means this
arithmetic has drifted from the library's. Record the depth and whether it is
double buffered; that is the framebuffer-fit answer the brief asked for, and if
it landed below 8 the `budget` figure on the line above says by how much.

**`SINGLE-BUFFERED` in that line is a finding, not a pass.** It means no colour
depth down to 3 fitted double buffered, and tearing is now possible.

1. **First light.** Within a second of `Wi-Fi up`, the panel should show an
   honest **grey empty frame** — header, two rules, the spread row, no ladder —
   reading `101 RESYNC`. Not black. That is the v1 `Stale{Resync}` frame stage C
   publishes before any data, and it is the boot state the object spends its
   first seconds in.
2. **Live.** On the first snapshot the panel colours: bids **green** stacking
   below the spread, asks **red** above it, best-of-book hard against the gap on
   both sides, and the header switching to `101 <last price>`. The heartbeat
   pixel in the bottom-right corner starts blinking at the draw rate.
3. **Pull the Wi-Fi.** The colour must drain **within ~1 s** — the 1000 ms
   watchdog deadline plus at most one 33 ms render period — leaving the same
   ladder in grey with the header reading `101 DISCONNECT`. Watch for what must
   *not* happen: a torn frame, a frozen intermediate, or any flash of a coloured
   stale book. **This is the M3 definition-of-done line.**
4. **Restore it.** Grey → next snapshot → clean live ladder. `grey for NNNN ms`
   in the log should read ~4,700 ms, and the reconnect grey should look
   *identical* to any other grey. A visible hitch there is something blocking
   Core 1 showing through, and it is a finding to report rather than a thing to
   fix at the bench.
5. **A ~10-minute soak on the near mesh node.** With the 2026-08-10 mesh fix in
   place the panel should hold colour throughout, `wd_gaps` at 0 and `sock_gaps`
   low. Any grey should correspond to a socket drop in the log. This is the first
   time the mesh fix is checked on a panel rather than in a log.
6. **The feed side must not regress.** `arrive` / `event` / `a→e` against stage
   C's baseline: Core 0 ~90 % idle, `a→e` ≤ 22 ms. If the LCD_CAM DMA and the
   Wi-Fi/TLS stack are starving each other, this is where it shows, and it is a
   priority/placement question — not an engine one.
7. **Heap flat over the soak.** `free` delta 0, `largest` not falling. The render
   task now carries the logging the console used to, so this is worth re-reading
   rather than assuming.
8. **Photo or clip to `hardware/`**, live ladder and the grey, ideally from the
   same run. This is also what unblocks **MP stage 2** on the portfolio portal, so
   shoot it like it will be published.

The new statistics line, every 10 s:

```text
-- panel  : depth=8 double bright=160 refresh=NNN Hz fb=65536 B (free NNNNNN -> NNNNNN) | drew 134 at 13.40/s worst paint 1830 us of 33000 us period
```

| Field | Healthy | What a bad value means |
| --- | --- | --- |
| `drew … /s` | tracks the `-- rate` line's `events/s` (~13/s) | well below it: the render task is not keeping up — priority or placement, not the engine |
| `worst paint` | a small fraction of 33,000 µs | approaching the period: the paint has no headroom, and since it is a fixed 64 hlines plus a header it should not move with book depth. It moving *is* the finding |
| `double` | `double` | `SINGLE` means the framebuffer did not fit twice; expect tearing |
| `NOT RUNNING` | absent | `Panel::begin()` failed — the boot lines above say at what and with how much free |

---

## The stall characterisation run (owner)

> **Closed 2026-08-10 — the cause was the Wi-Fi link, not the board.** The runs
> below were done; both arms filled their >1 s buckets together and neither
> indicted the firmware. The board had associated to a far Deco node at −75 dBm;
> moving it to the near node (−34 dBm) left **two ~130 ms blips in eleven
> minutes**. So the lever bundle this section pointed at —
> `esp_websocket_client buffer_size`, the 5,744-byte `CONFIG_LWIP_TCP_WND_DEFAULT`,
> WS-task priority and core — **should not be pulled**; none of them was ever the
> mechanism. `kRxWatchdogMs` stays at 1000 ms. The instruments stay, and the
> procedure below is still how to read them; stage D re-runs it with a panel
> beside it. Full record: `docs/briefs/M3-live-anvil-on-the-panel.md`, 2026-08-10.

The defect as it stood: on the bench the panel greys every 15–20 s with the socket up
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

### Where that split stops, and why it needed a third signal

The 2026-08-09 runs answered "how often" and then refused to answer "which
half": both arms held `connects=1` and `sock_gaps=0` while `arrive` and `event`
filled their >1 s buckets **together** — 17 holes over 11 minutes with power save
off, 25 over 10.5 minutes with it on, worst 2,461 ms and 1,893 ms. That reads as
"transport" on the old table, and it is an over-claim on two counts:

- **The arrival stamp is not the wire.** It is taken in `WsTransport::on_event`,
  on `esp_websocket_client`'s own task, already downstream of the Wi-Fi driver,
  lwIP, the socket read, the TLS record decrypt and the `esp_event` hop. A hole
  in `arrive` means *no complete message reached our callback for a second*,
  which a busy Core 0 produces just as well as a quiet socket. The split's real
  boundary is the FramePipe queue hop, not the antenna.
- **Anvil sheds to a slow consumer, evenly** (measured: a client throttled to a
  quarter rate gets 4× fewer messages and is never silent for more than one drain
  interval). So "the server went quiet to us" is a *symptom* of this socket
  backing up, not an independent cause. "Anvil shed it" and "Wi-Fi delayed it"
  look identical from inside the data path.

So the fork — **board-bound** (Core 0 cannot drain the stream) versus
**link-bound** (the frames are not arriving) — needs a signal from outside that
path. Three more lines carry it:

```text
-- cpu    : window c0=88% c1=96% over 10001 ms | healthy c0=90% c1=96% n=1180 | probe 88231441 passes worst 412 cyc of 24000
-- rssi   : now -69 min -78 max -64 dBm n=1204
-- holes  : n=17 board=12 link=3 mixed=2 unknown=0 | burst=10 cadence=7 | seq 41/s
-- hole   : #12 1240 ms c0=9%/90% c1=93% rssi=-70 seq+3 of +51 | recov 3,4,6,78 ms -> BOARD-BOUND burst
```

- **`cpu`** — per-core idle. `window` is the console's own reading over the last
  10 s; `healthy` is the baseline built from every sub-1 s inter-event window,
  and it is the reference each hole is judged against. `probe … worst N cyc of M`
  is the instrument checking itself: `N` is the longest interval it accepted as
  idle and `M` the threshold above which an interval is treated as work, so `N`
  approaching `M` means the verdict is at the limit of its own resolution.
- **`holes`** — every >1 s book-hole, classified. **`n` here must equal `>1s` on
  the `event` line**; they are two independent instruments counting the same
  thing off the same threshold, so a disagreement is a bug rather than a nuance.
- **`hole`** — one line per hole as it completes, printed at `W` so it stands out
  in the scrollback. Every derived judgement sits beside the numbers it came
  from: `c0=9%/90%` is this hole's Core-0 idle against the healthy baseline, and
  `seq+3 of +51` is the wire-seq step across the hole against what Anvil's own
  publish rate says a *fresh* frame would have carried.

### The verdict table

| Core 0 idle across the hole | Recovery | Reading |
| --- | --- | --- |
| far below `healthy` | burst, `seq` step small | **board-bound** — the frames sat unread in the socket buffer while Core 0 was busy |
| far below `healthy` | cadence, `seq` step ≈ fresh | **board-bound** — Anvil shed the middle because this socket backed up |
| at or above `healthy` | burst, `seq` step small | **link-bound** — the frames existed and the air delayed them |
| at or above `healthy` | cadence | **link-bound** — nothing arrived and nothing was waiting to |

Board-bound is firmware's to fix and can step-change to zero, because a board
that keeps up is never shed. Link-bound is not reachable from this repo at all —
it becomes 2.4 GHz channel, placement and antenna.

And the two rows the old table still owns, unchanged: `event` >1s filling with
`arrive` clean is ours *and localisable*, with `qwait` in seconds meaning the
feed task did not run and `qwait` small with a large `worst_frame` meaning the
work itself got slow.

### The procedure

1. **One environment.** `pio run -e depthcharge -t upload -t monitor`. Naming it
   is not optional — with no `-e`, `pio run -t upload` uploads *both* and the
   second one wins. Power save is settled (both arms measured, 17 vs 25 holes),
   so `depthcharge-ps` is not part of this run.
2. Confirm at start-up, in order: `power-save requested=OFF driver ps=0`, then
   `per-core idle probe up at 240 MHz`. **If the second line is missing or reads
   `idle probe NOT RUNNING`, stop** — every hole verdict in that run is
   `unknown`, deliberately, rather than a plausible-looking 0% idle.
3. **Let it run ≥10 minutes, steady state, and do not pull the Wi-Fi.**
   `connects=1` and `sock_gaps=0` at the end is what makes this a steady-state
   distribution rather than a mixture; a hole that spanned a reconnect prints
   `(socket dropped)` and must not be pooled with the others, because a reconnect
   costs a ~2.5 s blocking `stop()` on Core 1 and is a different phenomenon.
4. **Read `cpu` before `holes`.** The tally is derived from per-hole idle against
   the `healthy` baseline, so a baseline that looks wrong invalidates the tally
   above it rather than being a separate observation. Sanity checks worth doing
   in this order: `holes n=` equals `event >1s=`; `no_slot=0` (a dropped message
   never reaches the recovery series, so the burst/cadence split degrades if this
   is non-zero); `window` and `healthy` idle in the same neighbourhood.
5. Commit the log and the reading to `hardware/bench-YYYY-MM-DD-feed-stall.md`,
   append the verdict to the session log in
   `docs/briefs/M3-live-anvil-on-the-panel.md`, and update strain 12.

Pair it with a simultaneous `tools/capture_anvil.py` from the desk, as above —
the desk is the control for "was the wire quiet", and it costs nothing to have.

### If you suspect the instrument

The idle probe measures idle by keeping a hook being called on each core's idle
task, which means that core does not execute `waiti` and spins instead of
sleeping. That is deliberate and it is what makes the interval between two calls
a measure of idle time rather than of the interrupt rate. It cannot delay real
work — the idle task is priority 0 — and there is no power management to
interfere with (`CONFIG_PM_ENABLE` is not set), but it is a genuine change to
what the board does, so:

```sh
# PowerShell
$env:PLATFORMIO_BUILD_FLAGS="-D DC_IDLE_PROBE=0"; pio run -e depthcharge -t upload -t monitor
$env:PLATFORMIO_BUILD_FLAGS=""      # and clear it afterwards, or every later build inherits it
```

rebuilds the same firmware with the probe compiled out and every idle figure
reported as unknown. **Verified rather than assumed** — that build's `.elf`
contains `per-core idle probe COMPILED OUT` and does not contain `per-core idle
probe up`. (`pio run` has no `--build-flag` option, which is why this goes
through the environment variable; the variable *appends* to `build_flags`, so
nothing in `platformio.ini` is lost.) If the >1 s hole counts differ materially
between the two builds, the instrument is part of the phenomenon and that is the
finding. Whether the probe ships past this characterisation is a stage D
decision.

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
| `src/stall_probe.hpp` | classifies a >1 s book-hole board-bound vs link-bound, plus the recovery shape and rssi. **No ESP-IDF** — host-tested by `harness/tests/test_stall_probe.cpp` |
| `src/reject_log.hpp` | what the parser threw away: the first ten payloads of each connect, with status, whole length, head, tail and any spliced second frame. **No ESP-IDF** — host-tested by `harness/tests/test_reject_log.cpp` |
| `src/core_idle.*` | the one platform half of that: per-core idle from an idle hook, because this framework has FreeRTOS run-time stats compiled out |
| `src/frame_pipe.*` | the four-slot pool + queues, transport → feed, and the arrival histogram |
| `src/feed_task.*` | Core 0: the pipeline, the RX watchdog, the only book writer |
| `src/ws_supervisor.hpp` | when a reconnect is due and how long it is immune. **No ESP-IDF** — host-tested by `harness/tests/test_ws_supervisor.cpp` |
| `src/staleness.hpp` | how OLD the book is, from the deficit against Anvil's 2 Hz `summary` broadcast. **No ESP-IDF** — host-tested by `harness/tests/test_staleness.cpp`. Raises no `Gap` and nothing branches on it |
| `src/ladder_font.hpp` | the 3×5 panel font, 41 glyphs. **No ESP-IDF** — host-tested by `harness/tests/test_ladder_render.cpp`, which is the point: the 64-row budget is computed from these metrics |
| `src/ladder_render.hpp` | the 64×64 ladder: geometry, `Ink`, the two palettes, the sparkline ring and the trade flash. **No ESP-IDF** — host-tested. Cannot name a colour |
| `src/panel.*` | the HUB75 driver glue: pin map by field name, the colour-depth decision, and `PanelCanvas` — the only place an `Ink` becomes an RGB value |
| `src/render_task.*` | Core 1: `consume()` → panel, then the log. Was `serial_console.*` through stage C |
| `src/heap_probe.*` | invariant #7 instrumentation |
| `src/anvil_root_ca.hpp` | the pinned TLS root, with its provenance |
