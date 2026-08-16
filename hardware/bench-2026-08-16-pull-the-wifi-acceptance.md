# Bench, 2026-08-16 — the pull-the-Wi-Fi acceptance

**M3's definition of done**, and the only criterion in the milestone that is neither a host
test nor a soak: *the pull-the-Wi-Fi test shows grey stale state then clean resync.*

Owner-driven, at the desk, blocking the board's MAC in the Deco app. **Run twice: it failed
the first time, and the failure was a real defect. It passed the second time on the fixed
build.** Both runs are recorded, because the first one is the more valuable of the two.

---

## Run 1, 08:44–09:02 — FAILED

Log: `firmware/logs/device-monitor-260816-085158.log` (and `…-084427.log` for the first
episode). Build: `66e2f77`…`e850ad2`.

**The grey half passed.** Blocking the station greyed the panel honestly and it carried no
hue — the photograph shows the ladder geometry intact in white/grey, which is
`static_assert(all_grey(kStalePalette))` doing its job on real hardware for the first time.
The socket autopsy named the cause correctly and the supervisor's holdoff line printed once.

**The resync half failed.** The station never re-associated:

```
08:57:39.362  Reason: 1 - UNSPECIFIED           the deauth
08:57:42.230  Reason: 202 - AUTH_FAIL           Arduino's one retry, refused
08:57:44.703  wifi down 5249 ms — rejoining (#1)
              … #2 … #388, one per second, for five minutes, never associating
```

**388 rejoin calls produced two `AUTH_FAIL` responses from the AP.** Had each call been an
attempt the AP refused there would have been ~388, 60 ms apart. They never arrived: a rejoin
begins with `WiFi.disconnect()`, an association needs ~4 s, and the 1 s refused-retry cadence
destroyed the attempt in flight every time. `WL_CONNECT_FAILED` is sticky, so once latched it
kept selecting that path forever — the failure was total, not intermittent.

The apparent "recovery" in the earlier episode was not one: `rst:0x1 (POWERON)` at 08:52:17,
four seconds before the association that worked. `connect_wifi()` runs once at boot and
nothing interrupts it, which is precisely why a power cycle succeeded where 483 rejoins had
not.

Diagnosis, fix and the general rule: ARCHITECTURE §9 (2026-08-16 pm), DESIGN strain 20,
commit `e032a7f`.

---

## Run 2, 09:36–09:41 — PASSED

Log: `firmware/logs/device-monitor-260816-093644.log`. Build: `ccc11ec` (the fix).

| time | event |
| --- | --- |
| 09:36:48.940 | flash boot; explicit scan joins `EE:D3:62:AE:81:9A` at **−40 dBm**, strongest of 3 |
| 09:36:57.948 | `LIVE at v2` — grey for 4,323 ms from boot |
| 09:37:43.659 | **`STALE (disconnect) at v572`** — the panel greys, 5.2 s *before* the deauth, because the RX watchdog fires on data stopping rather than on the socket dying (invariant #5's strict reading) |
| 09:37:48.857 | `Reason: 1 - UNSPECIFIED` — Deco deauths |
| 09:37:48.864 | `socket end #1 [read]: 55142 ms, 4445639 bytes, 652 data / 0 ctrl frames` |
| 09:37:49.496 | `reconnect due but the station is not associated — holding` (once) |
| 09:37:51.879 | `Reason: 202 - AUTH_FAIL` — Arduino's own retry, 3.02 s after the deauth |
| 09:37:59.246 | `wifi down 10249 ms … rejoining (#1)` — the new 10 s takeover |
| 09:38:07.444 | `socket up: dns 0 ms, connect+upgrade 4016 ms, rssi −70 dBm` |
| 09:38:07.516 | **`LIVE at v573`** — grey for 23,856 ms |

**Verdict: pass.**

- **No reset anywhere between the outage and the recovery.** The only `rst:0x` in the log is
  the flash boot, before the test. The board rejoined unaided.
- **One rejoin call, one AP answer.** The 388-to-2 signature is gone; the first takeover
  succeeded outright, which is better than the one-for-one ratio predicted as the tell.
- Deauth → LIVE: **18.7 s**. Rejoin → LIVE: 8.3 s (10 s cadence + ~4 s association + ~4 s
  TLS connect + Anvil's snapshot). Inside the 10–20 s predicted before the run.
- `connects=2`, `sock_gaps=1` — the boot connection and exactly one recovery.
- The panel went price-and-bars → grey → price-and-bars, observed at the desk.

---

## What the passing run also showed, and it is not free

**The recovery landed on a weak node, and stayed there.**

| | boot (explicit scan) | after the rejoin |
| --- | --- | --- |
| BSSID | `…:9A` | not printed on this path |
| rssi | **−40 dBm** | **−70 dBm**, drifting to −75/−76 |
| watchdog greys | 1 (the resync) | **14 in the following three minutes** |

`connect_wifi()` surveys every sibling and joins the strongest by name. The supervisor's
rejoin cannot: it calls a plain `WiFi.begin(ssid, password)` and relies on
`WIFI_ALL_CHANNEL_SCAN` + `WIFI_CONNECT_AP_BY_SIGNAL`, because a blocking all-channel scan on
loopTask would stall the 250 ms supervise poll for seconds. Those two settings narrow the
lottery and do not close it — which is exactly what ARCHITECTURE §9 (2026-08-13) recorded
when they failed their five-boot acceptance, and the boot fix that replaced them was never
extended to this path.

So a recovery re-rolls the mesh draw, the board never roams off what it draws, and one
unlucky rejoin costs the rest of the run. Here it cost a factor of ~14 in watchdog greys.

**This does not fail the DoD** — the criterion is grey then clean resync, and both happened —
but it is now a measured cost rather than a documented hypothetical, and it is on the
backlog (`ROADMAP.md`). The shape of the fix is already established by this milestone: the
socket connect used to block loopTask and now runs on the RX task, with the supervisor
deciding and the task doing. The rejoin's scan-then-join wants the same treatment.

---

## Owed, still

- **Weak-node 1 h soak** (transport brief DoD) — never run on the final build. Ironically the
  board is currently sitting on a −75 dBm association by accident, which is most of the
  setup.
- **Ghosting re-check** with `clkphase = false` — is the header speckle gone, and did the
  green right-edge dots go with it (`hardware/BRINGUP.md`).
