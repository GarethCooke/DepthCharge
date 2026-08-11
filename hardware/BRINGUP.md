# DepthCharge — Hardware bring-up record (M2)

## Panel
- **Module:** Muen P3 64×64 indoor LED matrix, 192×192mm, 1/32 scan, HUB75E.
- **Driver IC:** FM6124 (read off the board as `FM 6124D`; latch `FM TC7262E`).
  Library init: `mxconfig.driver = HUB75_I2S_CFG::FM6124;` — fallback `FM6126A`.
- **LEDs:** SMD2121, Epistar.
- **On hand:** 2× modules + 2× HUB75 ribbons + one two-module power cable.

## DevKit
- **ESP32-S3-WROOM-1 N16R8** (16MB flash + 8MB octal PSRAM).
- **Reserved GPIOs (do not use):** 26–37 (flash + octal PSRAM), strapping 0/3/45/46,
  native USB 19/20. Header pads silkscreened 35/36/37 are physically present but wired
  to PSRAM — electrically unusable.

## Pin map (HUB75E → ESP32-S3, verified by continuity + first light)
| HUB75E | GPIO |   | HUB75E | GPIO |
|---|---|---|---|---|
| R1 | 4  |   | A   | 17 |
| G1 | 5  |   | B   | 18 |
| B1 | 6  |   | C   | 8  |
| R2 | 7  |   | D   | 9  |
| G2 | 15 |   | E   | 10 |
| B2 | 16 |   | CLK | 11 |
|    |    |   | LAT | 12 |
|    |    |   | OE  | 13 |

Panel GND → DevKit GND. Panel 5V from bench supply, **not** the DevKit.

## Power (measured)
- **Full white, full brightness: 2.6 A @ 5V** (worst case — the PSU sizing number).
- **Representative ladder content: 0.25 A @ 5V** (typical draw).
- **M3 standalone PSU:** 5V/5A screw-terminal (Mean Well LRS-50-5 class) — ~2× headroom
  on 2.6A. The two-module power cable's bare-wire input bolts to it.
- Bench topology: Siglent SPD3303X-E CH1 at 5V (current-limited), DevKit on USB,
  common GND (CH1− strapped to DevKit GND), 5V rails **not** tied.

## Signal integrity (scoped at full white, 1× probe)
Four lines surveyed, each against its own failure mode — all pass:
- **R1** (data): fast clean edges, valid at the clock.
- **CLK** (~2MHz): single threshold crossing per edge — no double-clocking.
- **LAT**: single clean strobe per line — no double-latch.
- **OE**: clean enable window per line — no blanking-timing corruption.

Common, benign: minor CLK crosstalk on the baselines (within margin); edge softening
partly attributable to the 1× probe. Jumper interconnect **characterised and proven for M3**.

## Forward notes
- **Interconnect:** bare jumpers are bench-proven and adequate for M3. The enclosed
  standalone object wants a tidier interconnect (shorter leads / ground plane / carrier)
  — an enclosure-era item, not an M2 gap. Re-scope CLK on a 10× probe if edge rate is
  ever suspect (M3 pixel-shift artifacts → look here first).
  - **2026-08-11 (M3 stage D): the forward note came true, and the interconnect is now
    a measured constraint rather than a tidiness preference.** With the ladder running,
    the panel shows ghost pixels immediately beside header glyphs — a shifted copy of the
    real data landing in adjacent shift-register positions. It tracks the *switching
    activity* of the paired scan rows (1/32 scan: row y and row y+32 share a slot), not
    their current: dimming the bars off the rail made it **worse**, because under BCM a
    channel at 255 is DC-on for the whole frame while an intermediate value toggles
    bit-planes several times per 9.5 ms frame. **The aggressor is edges, not amps.**
  - **The two panel-timing options trade the artifact against the radio, and we cannot
    have both on this interconnect.** `clkphase = false` (invert CLK) clears the ghosting
    completely — and collapses inbound Wi-Fi from ~6 msg/s to **~2 msg/s** against Anvil's
    17/s, with `sock_gaps=0`, `connects=1`, 0 % lost and both cores ~96 % idle. Reproduced
    consistently in both directions. It cannot be firmware: `clkphase` reaches only
    `esp_rom_gpio_connect_out_signal(CLK, LCD_PCLK_IDX, invert, false)`, a GPIO-matrix
    inversion on one pin, with no change to DMA, data rate or CPU load. Inverting CLK
    simply aligns its edge with the thirteen data/control lines beside it, so ~14 outputs
    switch **simultaneously** down a ribbon with no ground plane, a hand's width from the
    DevKit's 2.4 GHz antenna. The panel samples its own data better and jams its own radio.
  - **Shipped setting: `clkphase = true` (library default).** The feed wins — a ghosted
    header is cosmetic, a 2 msg/s feed greys the panel every few seconds. Recorded in
    `firmware/src/panel.cpp` so it is not retried blind.
  - **M6 requirement, stated with evidence:** the carrier PCB needs a ground plane under
    the HUB75 run and short leads, and the roadmap's existing `2× 74HCT245` buffers and
    bulk caps are load-bearing rather than nice-to-have. Re-test `clkphase = false` on the
    carrier: if the noise argument evaporates, the ghosting fix is free.
  - **Still owed: CLK on a 10× probe.** The 2026-08-11 finding is behavioural, not
    measured — the signal-integrity survey above used a **1× probe** and recorded "edge
    softening partly attributable to the probe", so the edge rate both symptoms turn on
    has never actually been characterised.
- **First-light sketch** kept in the M2 experiment area, out of `engine/`.

## Status
M2 complete — every claim above backed by a measurement or trace. Desk-resolvable
unknowns (driver IC, reserved pins, pin map) closed before the bench; bench-only unknowns
(draw, signal integrity) closed with instruments.
