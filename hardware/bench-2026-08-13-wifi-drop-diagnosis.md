# Bench record — the Wi-Fi drop diagnosis, 2026-08-13

A day-long instrumented hunt for the recurring websocket drops, run as six serial captures
and a two-board controlled experiment, ending with the mechanism named and labelled. Raw
logs are `firmware/logs/device-monitor-260813-*.log`, untracked per the 08-09 precedent;
excerpts are quoted. (Two caveats carried honestly: the workstation slept ~17:45–19:50, so
the evening captures have gaps — board-side cumulative counters bridge them; and all counts
are as of each capture's stated window end — both evening captures ran on past 20:25, and
every post-window death is also `errno=119`. Three auxiliary/aborted captures exist beside
the six listed.)

| Capture | Board · firmware | Window | What it establishes |
| --- | --- | --- | --- |
| `095512` | A · depthcharge | ~09:45–10:15 | The morning storm: a reconnect every ~7 s |
| `111358` | A · link-autopsy | 11:14–11:49+ | Clean baseline on a strong association |
| `133735` | A · depthcharge | 13:37–16:38 | The afternoon storm, on a named node |
| `143917` | B · link-autopsy | 14:39–16:38 | The control, pinned to the same node |
| `163850` | A · depthcharge+errno | 16:39–20:25 | **The labels: 17/17 deaths errno-silent** |
| `164005` | B · link-autopsy+gap | 16:40–20:25 | The fade record: weather real, never fatal |

## The morning storm, decoded

The 09:55 log shows the panel greying every ~7 s: `Error read data` → grey → ~4.7 s
reconnect → ~2 s alive → repeat, reaching attempt #159 by 10:09. Three facts pulled out of
that capture reframed the whole problem:

1. **The Wi-Fi association never dropped.** 74 socket deaths, zero `Reason:` deauth lines,
   zero `WifiSupervisor` rejoins, zero holdoffs — with `CORE_DEBUG_LEVEL=3` proven able to
   print them on this rig. Every death was a TCP/TLS-flow death under a held association.
2. **Every handshake succeeded.** 74/74 TLS connects at 3.7–4.7 s — at −78…−85 dBm. A link
   that completes multi-round-trip handshakes every cycle is not too weak to carry a socket.
3. **The storm ended on its own** at 10:09:42, mid-run, no reflash, RSSI still −79…−85 —
   and the same socket then held for minutes draining a 227 s Anvil backlog.

## Eliminations banked during the day

- **Server-side policy** — the autopsy baseline held the production feed 35+ min with zero
  server pings observed and zero client pings sent; no liveness deadline exists to violate.
  (The main firmware's `ctrl` counts are pongs answering its *own* 10 s pings.)
- **Board hardware** — both DevKits fail under the main firmware (owner's morning test) and
  both run clean under the autopsy client; both see the sick node at the same strength
  (−74…−80), so neither antenna is the variable.
- **Panel EMI / `S3_LCD_DIV_NUM` di/dt theory** — bare board reproduced the storm; the
  healthy 08-11 soak ran the identical LCD_CAM drive *with* the panel attached.
- **Power save** — `driver ps=0` read back at every boot on the `depthcharge` env.
- **Heap/CPU starvation** — Core 0 ≥89 % idle through every hole; heap flat (`free=29984`)
  across 33,780 frames, though reconnect churn drives the low-water mark down
  (`low=9344`, 20.5 KB below baseline — worth an eye, not yet a problem).
- **Signal level per se** — the afternoon association sat at −66…−85 for two hours; holes
  cluster in waves, not at the RSSI floor, and the quiet morning stretch ran at −80…−85.

## The instrument the day was missing: which node

`connect_wifi()` now prints `bssid=` and `ch=` at association (`ws_transport.cpp`), because
every Deco broadcasts the same SSID and `rssi=-78` alone cannot say *which* node the board
camped on. First boot with the instrument (13:37):

```text
wifi up: ip=192.168.68.64 bssid=EE:D3:62:AE:81:F9 ch=4 rssi=-68 dBm | … driver ps=0 …
```

−68 dBm, with the house quiet — while the autopsy client on the same desk had just spent
two hours at −33…−41. Fresh boots draw different nodes (observed −33, −50, −76 across three
boots of board B), the ESP32 never roams off its choice, and the main firmware then lives
or dies by that lottery. **Which physical Deco unit `EE:D3:62:AE:81:F9` is remains to be
identified in the Deco app.**

## The afternoon storm, on the named node (capture `133735`)

Board A on `EE:D3:62:AE:81:F9` at −61…−85: **167 holes — 150 watchdog stalls and the rest
socket deaths (66 `Error read data`, 47 connects)** in 2 h 8 m, in waves: minutes of ~1 s
stalls, then bursts of 14–45 s outages chaining into each other (worst gap 77.5 s including
its recovery). All 167 classify LINK-BOUND, `board=0`. And again: **zero Wi-Fi rejoins all
afternoon** — the association held at −85 while sockets died above it.

Two systemic notes for other briefs:

- **Stage E coupling.** During waves the book lags (age 171 s observed); each socket death
  then adopts a fresh snapshot and skips the queue (`seq+101020` in one hole). A socket
  death is currently the only mechanism that un-lags the book — the storm and the age
  problem compound.
- The reconnect flood re-exercises the parked parse-burst truncation (`parse=349` today
  against 0 on the healthy soak), consistent with its 08-10 description.

## The controlled experiment: two boards, one node

From 15:08, board B (autopsy client: esp-tls + minimal WS, no client pings, no deadlines)
was **pinned to the same BSSID** (`p` command, `PINNING to EE:D3:62:AE:81:F9 ch 4`) and sat
at −71…−80 beside board A for the rest of the day:

| | Board A (depthcharge) | Board B (autopsy, pinned) |
| --- | --- | --- |
| association | held throughout | held throughout (deliberate re-assocs only) |
| socket deaths | continuous (36 connects by 20:25) | **0, ever** |
| data silences | 1–45 s waves; worst chain 77.5 s | fades of 0.5–3.9 s, **all survived** |
| longest socket | minutes | **3.7 h unbroken, 534 MB** |

Same node, same channel, same signal, same seconds: the production stack stalls and dies;
the minimal client streams uninterrupted. This also re-reads the morning's two-board test
correctly: both boards failed *running the main firmware* — the failure followed the
firmware, not the silicon, and not (alone) the weather.

## The labels (capture `163850`) — the mechanism, named

`on_event()`'s errno capture (the `ws down` path in `ws_transport.cpp`) put a label on every
death the log caught. **17 of 17
deaths read `errno=119 (EINPROGRESS)` — the stale leftover of the socket's own connect
phase.** Not one ECONNRESET, not one ETIMEDOUT: the failing read set *no socket error at
all*, so the failure sits above TCP — and half the deaths arrived via `event 4`
(`WEBSOCKET_EVENT_CLOSED`, the library's *clean-close* path) rather than an error event.

Beside them, **85 framing-corruption rejects**, repeatedly co-timed with the deaths, with
one recurring signature:

```text
-- reject : #1  … not-json len=8959 SPLIT@1 head[t{"type":"book","seq":…
-- reject : #34 … not-json len=8703 SPLIT@1 head[.{"type":"book","seq":…
-- reject : #20 … not-json len=58          head[1},{"price":"9.9602",…
```

`SPLIT@1` with exactly **one stray byte** glued ahead of clean JSON (the tail byte of the
previous message), and mid-JSON fragments delivered as whole messages: an off-by-one in
the client's payload-offset accounting, caught red-handed. TLS's record MAC guarantees the
wire was intact, so the mangling is post-decryption, inside the client's buffer path. This
is the parked "parse-burst reassembler truncation" finding of 2026-08-10, now co-located
with the socket deaths and both attributed to the same layer.

And the deaths happen at −39 dBm too: the strong-node death rate (~1 per 6–10 min,
climbing with evening household load) **is** the "accepted residual" of the 2026-08-10
brief — it was never weather, it was always this.

## The fade record (capture `164005`) — the weather, measured

B's per-window worst-gap instrument on the sick node: **~1,140 five-second windows, mean
worst-fade ~580 ms (roughly 600 of them ≥ 500 ms), maximum 3,914 ms — zero deaths, zero
stalls ≥ 15 s, 534 MB unbroken.** The fades
are real and shared; they are what board A's 1,000 ms RX watchdog honestly reports as
wd-gaps on a weak association (closing the 2026-08-09 §9 open defect: *what stalls the
feed 1–2.5 s with the socket up* — RF fades, amplified by camping on a −7x node). They
never killed a socket on the client that owns its error path.

## The boot lottery, measured

Four consecutive boots of board A between 16:39 and 16:41, house quiet, same desk:

```text
16:39:22  bssid=EE:D3:62:AE:81:9A  ch=4  rssi=-40
16:40:09  bssid=EE:D3:62:AE:81:B3  ch=4  rssi=-76
16:40:13  bssid=EE:D3:62:AE:81:F9  ch=4  rssi=-76
16:40:15  bssid=EE:D3:62:AE:81:9A  ch=4  rssi=-39
```

Three sibling Deco BSSIDs on one channel; **half the draws landed on a −76 node with a
−40 node standing beside it**. The framework default is `WIFI_FAST_SCAN` — first probe
response wins — and the ESP32 never roams off its draw. This is the mis-association of
2026-08-10, mechanised: it was never fixed by moving the board, it was re-rolled.

## Verdict

Two independent faults, compounding:

1. **The `esp_websocket_client` vintage corrupts its own frame accounting and tears
   sockets down errno-silently**, at a base rate on any association (the residual),
   multiplied under retransmission-heavy conditions (the storms). Every reconnect then
   costs ~4.7 s of grey and — because Anvil queues — a backlog skip (`seq+113984` ≈ 11
   minutes in one observed hole).
2. **Boot-time fast-scan association is a lottery across mesh siblings**, and a −76 draw
   turns ordinary RF weather into constant watchdog greys and amplified death rates.

Fixed and pending:

- **Applied, flashed, and FAILED its acceptance** (21:38–21:40): `WiFi.setScanMethod(
  WIFI_ALL_CHANNEL_SCAN)` + `setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL)` in `connect_wifi()`.
  Five resets drew `9A −59 / B3 −86 / 9A −64 / 9A −67 / F9 −73` — **two of five on weak
  siblings with by-signal demonstrably active** (boot 2 joined −86 with a ~−60 node up).
  Two lessons recorded: the driver's sorted join is not reliable across mesh siblings on
  this vintage, so the fallback — an explicit scan + strongest-BSSID `begin()`, the
  `wifi_diag` survey productized — is promoted into the owned-client brief as its first
  deliverable; and every sibling read ~20 dB below its afternoon figure at 21:40 (even
  `9A` at −59…−67 vs −39), so absolute-dBm acceptance bars are the wrong shape — the
  restated bar is *relative*: every boot joins the strongest sibling visible in its own
  scan.
- **Brief written, not attempted tonight:** replace the websocket client layer with an
  owned minimal WS client over esp-tls — `docs/briefs/M3-transport-own-the-websocket-client.md`.
  Board B is its working prototype and today's soak numbers are its acceptance bars.
- The `depthcharge-nopp` arm (ping/pong off) is built and kept for a negative-result run;
  the evidence no longer points at ping/pong.
- Deco-side: identify `…:F9`/`…:B3` in the app; their weakness is real even if no longer
  the killer.

## Tools this day produced

- `firmware/diag/wifi_diag.cpp` (`-e wifi-diag`) — per-BSSID mesh survey, association
  tracking, STA disconnect reason codes, pin-to-strongest.
- `firmware/diag/link_autopsy.cpp` (`-e link-autopsy`) — owns the socket (esp-tls +
  hand-rolled WS on the production endpoint); per-death mbedtls rc / errno / SO_ERROR /
  bytes / lifetime / close frames; per-window worst-fade; three switchable flows; BSSID
  pin to the sick node.
- Main firmware: `bssid=`/`ch=` on the association line; `errno` on every `ws down`;
  the all-channel/by-signal association fix; the `depthcharge-nopp` experiment arm;
  `[platformio] default_envs` guard against the flash-every-env foot-gun.
