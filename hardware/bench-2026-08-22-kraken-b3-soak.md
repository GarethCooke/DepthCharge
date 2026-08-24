# Bench record — M4 stage D, B3: the 25-hour Kraken soak, 2026-08-22/23

The acceptance evidence for M4's definition of done and for three `ARCHITECTURE.md` §9 rows
dated 2026-08-23. Unlike every previous bench record in this directory, **the raw capture is
committed rather than quoted**, because three of its findings are statistical and a reader who
cannot re-run the arithmetic cannot check them.

## Provenance

| | |
| --- | --- |
| raw capture | `hardware/bench-2026-08-22-kraken-b3-soak.log.gz` (3,373,814 B gzipped) |
| raw sha256 | `6a9139f615e9197ddd9b4e36a5bcb71274ede232b38cd56ab6b5ccb27c007fa8` |
| raw size | 33,481,892 B · 259,195 lines |
| first marker | `2026-08-22T21:33:20+01:00` |
| last marker | `2026-08-23T22:46:40+01:00` |
| capture | **2 port opens, 0 losses, 0 gaps, 0 open failures** |
| board | ESP32-S3 DevKit, COM7, 64×64 HUB75, `depthcharge-kraken`, MINA/GBP |
| image | shipping — proven from the log by the **absence** of `DC_SOAK_TEST_TAG` on every SOAK line |

The frozen copy at `firmware/logs/FROZEN-kraken-b3-soak-20260822.log` is read-only and
byte-identical; the committed `.gz` decompresses to the same sha256, verified. **Every figure
below is emitted by `tools/soak_report.py` from the frozen file** — none is hand-read.

The two port opens are a script swap thirteen minutes in, not a fault: the first capture was
replaced with one that holds `SetThreadExecutionState`, after the host was found to be set to
sleep at 120 minutes on AC with the board USB-powered from it. The board was not reset — `up=`
runs monotonically across the swap.

## The run

```
SOAK lines        : 9,051
board uptime      : 621s -> 91412s  (25.39 h)
REBOOTS           : NONE - uptime monotonic
live at end       : 1
final counters    : grey_n=195 wd=2 sock=2 connects=3
                    resync_req=193 heals=215 crc_fail=193 refused=0 owed=0
worst_age (final) : 5.0s
grey total        : 3904s = 65.1 min = 4.27% of uptime
```

**Parity with M3's 23.6 hours is met and exceeded**, and the board never restarted.

## 1 · The half-open case, twice

```
  up=  15492s  wd   0->1
  up=  15633s  sock 0->1          watchdog 141s earlier
  up=  85446s  wd   1->2
  up=  85737s  sock 1->2          watchdog 291s earlier
```

Both steps read off the **same** clock — the SOAK line's own, at 10 s cadence, so ±10 s. The
heartbeat stopped and the socket was still believed up for another 2.4 and 4.9 minutes. This is
the condition B1 could not produce from the network side and had to mute the firmware to stage;
here it arrived unprovoked, twice.

**It is not universal, and the full table is an argument rather than a tally.** Three of the four
combinations have now been observed:

| | `sock` died | `sock` survived |
| --- | --- | --- |
| **`wd` fired** | ×2 — the two DNS outages, genuine half-open (this section) | ×1 — `wd` 2→3 at `up=127760s`, `sock` and `connects` unmoved for the following 20 min: the watchdog firing over a LIVE socket, healed by the resync **level** with no reconnect |
| **`wd` silent** | ×1 — a socket that died with no preceding liveness stall | **never, and it cannot be** |

**The empty cell is the point.** A book that stops updating while the socket is alive and the
heartbeat keeps arriving is exactly the case the 2026-08-17 ruling says is undetectable: a quiet
market and a silently dead subscription are identical on the wire, which is why point 6 of that
ruling accepts an unobserved false-colour as the price of the design. Its absence from 25 hours is
therefore **consistent with the ruling rather than evidence of coverage** — the cell cannot be
populated by this instrument, so an empty cell says nothing about how often the case occurs. The
CRC covers the neighbouring case where updates arrive and disagree; nothing covers silence.

## 2 · Grey time reads worse than it is

```
episodes : 194   total 3891s (64.8 min)   median 2080 ms
  <= 10s : n=192  median=2078 ms  max=6038 ms  total=432s
  >  10s : n=2    ['25.6 min', '32.0 min']     total=3459s   (89% of all grey)
```

192 of the 194 episodes are checksum heals at a **median of 2,080 ms**, matching A5's 1.6–2.1 s
exactly. The two DNS outages are 89% of the grey time.

## 3 · The heal/drop loop is real — the finding with teeth

```
THE CRITERION: P(a checksum failure lands within 10s of the preceding heal)
  observed         : 32/185 = 17.30%
  Poisson baseline : 2.07%   (1 - exp(-10/478))
  ratio            : 8.36x
  VERDICT          : MATERIALLY ABOVE BASELINE

inter-arrival histogram (s):
       0-10s    18  ##################
      10-30s    40  ########################################
      30-60s    14  ##############
     60-120s    14  ##############
    120-300s    28  ############################
    300-600s    23  #######################
   600-1800s    36  ####################################
      1800+s    12  ############

pipe no_slot : 1,594   (8.57 drops per STALE(checksum), 8.26 per counted failure)   qfull=0 oversize=0
```

Bimodal where a venue artefact would be flat. **A drop corrupts the book, the CRC catches it,
the heal re-subscribes, the snapshot burst overruns the four-slot pipe, and the next failure
follows within seconds.** A5 saw 3 failures and 10 drops and recorded the overrun as a
consequence; at 25 hours it is the input as well. Not fixed here — the remedy is a venue-sized
pipe, already priced in DESIGN §08, and it must not be built in the sitting that measured it.

## 4 · Largest free block is a sawtooth, not a ratchet

```
  47092 B   up     621s ..   15482s   (4.13 h)
  45044 B   up   15492s ..   15512s        <- -2,048
  42996 B   up   15522s ..   15542s        <- -2,048
  40948 B   up   15552s ..   15582s        <- -2,048
  38900 B   up   15592s ..   17015s   (0.40 h, the retry storm)
  49140 B   up   17025s ..   87350s   (19.53 h)   <- recovered, ABOVE the start
  31732 B   up   87360s ..   91412s   (1.13 h, second outage)
```

Exact 2,048 B decrements during the retry storm, full recovery afterwards, and — read off the
live board after the capture ended — **back to 49,140 again**. Free heap is flat throughout:
3-hour medians 61,132 / 61,128 / 61,164.

**The risk is narrower than it sounds.** `Panel::begin()` allocates at boot and is not exposed
to runtime fragmentation. What is exposed is the reconnect's own TLS handshake — both the cause
of the steps and the thing that would fail if they ever went deep enough. **Two events cannot
distinguish a sawtooth from a slow ratchet with recovery**; a second capture is running.

## 5 · DNS, and the association that never dropped

```
resolution FAILURES : 105    all at 13,999-14,000 ms, rc=202
successful connects : 2
  dns_ms  conn+upg_ms  rssi
   14000         5227    -43
   14000        10867    -47
  successes >= 13,000 ms: 2/2
  HYPOTHESIS (primary resolver dead, secondary reached on timeout): CONSISTENT

autopsy assoc= values: ['1']  (n=108)
wifi down / rejoin / wifi-up events: 0
```

**Both successes also paid the full 14,000 ms.** That is a specific dead server in what DHCP
hands out, not general flakiness — no resolver addresses appear in the log, so which one is not
identified here. The association never dropped: every autopsy says `assoc=1`, rssi −42 to −47,
zero Wi-Fi events in 25.39 hours.

Neither outage was Kraken and neither was the firmware.

## Discrepancies between the script and the figures first reported

Per the work order the script wins; both are recorded rather than either being quietly adopted.

| figure | first reported | script | resolution |
| --- | --- | --- | --- |
| grey episodes | 194 | 194 `grey for` lines, counter `grey_n=195` | **RESOLVED.** `grey_n` was already **1** on the first SOAK line in the capture (`up=621s`): the boot grey happened before the capture attached. 195 − 1 = 194, matching the lines exactly. |
| CRC failures | 193 | counter `crc_fail=193`; **187** `STALE (checksum)` lines, of which 186 carry the ` at vNNN` the strict grammar needs | **PARTLY EXPLAINED, 6 REMAIN.** `crc_fail` was **0** at attach, so the pre-attach window explains nothing here — unlike `grey_n` above. One line is mangled by two tasks writing the UART at once (9 such lines in the whole capture, 0.003%). That leaves **six counted failures with no STALE line**. The plausible reading — they landed while the panel was already grey, so `raise_gap_once()` raised no new Gap — is a hypothesis and is not tested here. |
| grey total | 64.9 min | 64.8 min from episodes, 65.1 min from the counter | different sources; the counter is authoritative and includes the open episode |
| heap trend | "-1,516 B, possible leak" | flat: medians 61,132 / 61,128 / 61,164 | **the alarm was wrong** — a two-point read of a noisy series |
| largest block | "35% drop, fragmentation" | sawtooth, recovers to 49,140 | narrowed, see §4 |

## What this does not settle

- **Whether the largest-block sawtooth is bounded.** Two events, and a third is not coming for
  free: **the steps are outage-driven, not time-driven** — the two in this run were 19.5 h apart
  and both landed on a DNS outage. Leaving the board on does not answer it. **The DNS fault is
  currently the only reliable generator of the events the question needs, so repairing the
  resolver removes the instrument.** That is a choice to make deliberately — capture the series
  first, or accept that the question stays open — rather than discover afterwards that the
  supply of events has gone.
- **Which resolver is dead.** The log does not carry resolver addresses.
- **Whether the heal/drop loop can resonate.** It turned over 193 times in 25 hours without
  running away. Nothing measured here says it cannot.
- **Six checksum failures with no STALE line.** `crc_fail=193` against 187 `STALE (checksum)` lines. One of the seven is a UART interleaving artefact; the other six are unexplained. The `grey_n` discrepancy beside it looked like the same problem and was not — it resolved cleanly to the pre-attach boot grey, which is a reminder that two similar-looking gaps need separate explanations rather than one shared guess.
