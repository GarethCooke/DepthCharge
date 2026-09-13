# Bench 2026-09-13 — D7's scope trace: the switch-on does not touch the rail

**What this is.** Backlog **D7**'s method change, executed: *"Put a probe on the DevKit's 3V3 and
5V rails and switch the panel on ONCE."* One switch-on, captured single-shot, with the serial
monitor running. It answers what a switch-on does to the 3V3 rail. **It does not settle the
1-in-6 question** — see §5.

**The headline is a negative, and it inverts D7's premise.** Nothing in the switch-on pulls the
rail down. What the capture found instead is that the rail is at its **worst while the panel is
OFF**, and that the switch-on *improves* it.

## 1 · Provenance

| | |
| --- | --- |
| scope | Siglent SDS1104X-E (100 MHz), both probes compensated at 10× on the day |
| CH1 | DevKit **3V3** header pin, ground clip on an adjacent G pin — 10×, DC, **20 MHz BW limit**, 500 mV/div, offset −3.00 V |
| CH2 | **Panel** 5 V at the panel's supply, same ground reference — 10×, DC, 20 MHz BW limit, 500 mV/div, offset −3.50 V |
| acquisition | **Peak Detect**, 7.00 Mpts, 5.00 MSa/s, 100 ms/div (1.4 s record), Delay −500 ms |
| trigger | Edge, **CH2 rising, 2.50 V**, Single |
| board | ESP32-S3 DevKit, ribbon attached, running the live ladder; `venue=binance` |
| monitor source | `firmware/logs/device-monitor-260913-160253.log` (gitignored) |
| capture | `hardware/bench-2026-09-13-D7-switch-on.log.gz` |
| gz sha256 | `b0aebabee2d4035afb62946a25754527597c3e372fa837fb7873912161bff9a3` |
| **inflated sha256** | `1f54e3ec256ee2e8ac23bf650049f795fae9bdcbbe8f50e43b38d642cc8e0130` |
| bytes | 108,876 on disk · 773,161 inflated · 5,314 lines |
| span | 16:02:55.280 → 16:25:44.892 · board uptime **1,442 s → 2,803 s** (22.7 min of a 46.7 min run) |
| screenshots | `bench-2026-09-13-D7-switch-on.png` (the 1.4 s record) · `bench-2026-09-13-D7-switch-on-zoom.png` (±7 ms, Delay 0) |

The gz is a **snapshot of a monitor that was still running**, so it ends mid-run rather than at a
stopping point. The round trip was **verified byte-identical** before the source was left behind.

The two screenshots came off the scope's USB stick through this session and were **re-encoded in
transit** — same 800×480 frame, valid PNG, but not byte-identical to the files the scope wrote.

The rails were **not tied** for this measurement: panel on its own supply, DevKit on USB, common
GND only — the topology `hardware/BRINGUP.md:95` records and D7 names as already-the-case.

## 2 · The numbers

| | |
| --- | --- |
| CH1 minimum, whole 1.4 s record | **2.76 V** |
| CH1 minimum, ±7 ms around the switch-on | **2.80 V** — *higher* than the record's floor |
| CH1 mean | 3.18–3.19 V |
| panel rail | crosses 2.50 V at t=0, reaches ~5 V within **≈5 ms** |
| resets in the window | **zero** — `rst:0x` and `BROWNOUT` both match 0 times in 5,314 lines |
| `connects=1` throughout, uptime monotonic | no reboot of any kind |

**Dip depth: none attributable to the event.** The record's floor is 2.76 V and it is not near the
trigger — zoom to ±7 ms around the switch-on and the minimum *rises* to 2.80 V. The deepest
excursions sit in the panel-off baseline band, before the event. There is no dip to time, so no
dip duration is recorded.

The accumulated `Min` column reads 2.42 V over ~3,000 measurements, but that statistic spans
earlier acquisitions taken at other vertical settings during setup — **indicative only**, not a
reading of this capture.

**Margin.** The build's detector is read from the framework rather than assumed:
`CONFIG_ESP32S3_BROWNOUT_DET=y`, `CONFIG_ESP32S3_BROWNOUT_DET_LVL=7`
(`C:\local\framework-arduinoespressif32-wnd17232\tools\sdk\esp32s3\qio_opi\include\sdkconfig.h:320-322`)
— level 7 is the **lowest** of the seven thresholds. The rail sat 0.43 V below its own mean at
worst (2.76 V against 3.19 V) and never approached the bottom of the screen (1.0 V). **The volts-per-level table is not in
this package**, and Espressif documents those labels as estimates, so no threshold voltage is
quoted here. Resolving it is a small owed item; it does not change the verdict.

## 3 · What the capture found instead: the panel-off state is the bad one

With the panel unpowered the 3V3 rail carries a band **several hundred millivolts wide**. At the
switch-on the band collapses to a thin trace and the mean lifts. The mechanism is that the ESP is
driving fourteen HUB75 outputs into unpowered inputs, so current flows through the panel's input
clamp diodes into its dead 5 V rail — **the board phantom-powers the panel through its signal
pins** whenever it is up and the panel is not.

**It is real, not probe artefact.** The control was run: probe tip on the same G pin as the clip,
and the band vanished. A GND-to-GND reference check is the only thing that separates rail noise
from loop pickup, and it was done before any of the above was believed.

**Consequence for M6.** A panel-side load switch that firmware enables after boot removes this
state entirely, and it is the same part D7 was asking whether the carrier needs. The carrier ties
the two rails that this bench keeps separate, so the switch-on transient measured here is **not**
transferable to the carrier — it is measured for the separate-rail topology only.

## 4 · A second finding, recorded because the probe went looking

**The DevKit's `5V` header pin is not carrying USB VBUS on this board.** The scope read 2.9 V on
it and a multimeter read 1.8 V on the same pin — a node whose voltage depends on the meter hung on
it is not being driven. CH2 on the 3V3 pin read 3.14 V against CH1's 3.19 V, so both channels were
sound; and no regulator makes a clean 3.19 V out of a 1.8 V input, so that pin is not the LDO's
feed. Cause not established — an open jumper, a failed Schottky, or a board variant that leaves it
unconnected. **The carrier must not assume that pin does anything**, and the DevKit cannot be
powered through it.

## 5 · What this does NOT answer

**One switch-on cannot distinguish a 1-in-6 mechanism from a 0-in-6 one.** D7's own statistics
paragraph is the reason the count was not attempted; the same arithmetic says a single clean
switch-on is consistent with every rate in the 95% interval `[2.1%, 48.4%]`. What the trace does
supply is the thing counting never could: **the rail was nowhere near the detector, so if the
brownouts are real, the mechanism is not a sag visible at the DevKit's 3V3 header pin** — leaving
a fast transient below the 20 MHz limit, a ground-referenced event, or the mains-phase
common-mode path as the remaining candidates.

## 6 · Incidental, and not D7's business

In the same 22.7 min window the panel **greyed 15 times** (`grey_n` 17 → 32) for **109.6 s** of grey
(`grey_ms` 130,768 → 240,378) — **8.1%** of the window. Every recovery is a REST re-seed: 15 of
them, **3.170–5.131 s** each, mean 4.214 s. Greys ran 3.872–19.594 s, mean 7.343 s. Every one of
the fifteen printed

```
W rest: largest internal block 3572 B is BELOW the 16717 B a TLS session needs
        — the reserve is wrong for two sessions (see panel.hpp)
```

with the largest free block down to **7,668 B** during the fetch. The grey duration is dominated by
a seed path running out of contiguous heap. `wd=0 sock=0 connects=1` throughout — the socket never
died; these are sequence gaps and the reseed each one costs. **Worth its own backlog card; nothing
here is evidence about D7.**
