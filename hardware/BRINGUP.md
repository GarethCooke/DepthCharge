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

## Flashing — use 115200, not the configured 921600

**`upload_speed = 921600` in `firmware/platformio.ini` fails on this desk, and it fails in the
worst possible way: mid-write, after the erase.** Measured 2026-08-27 (M5 stage D-A1), on the
CH343 bridge at COM7:

```text
Changing baud rate to 921600
Changed.
WARNING: Failed to communicate with the flash chip, read/write operations will fail.
Configuring flash size...
Flash will be erased from 0x00000000 to 0x00003fff...
   ... (three more regions, including 0x00010000 to 0x000e7fff)
Compressed 15104 bytes to 10401...
Writing at 0x00000000... (100 %)
A fatal error occurred: Serial data stream stopped: Possible serial noise or corruption.
```

**Read what that leaves behind.** The erase completed and the write did not. The bootloader
region had just been rewritten and the 881 KB application region was erased and never
repopulated, so the board is left **not merely unflashed but partly erased** — it will not boot,
and the serial output is whatever the ROM bootloader says rather than anything this firmware
wrote. **That is the trap this section exists for:** a board that has just been flashed and now
prints nothing reads exactly like a firmware fault, and the last thing anybody suspects is the
upload that reported an error thirty seconds earlier and scrolled away. It is the same failure
mode as the dead address line below — a hardware-side cause wearing a firmware-side symptom.

**The remedy, and it is reliable:** re-flash at 115200. Both images went up clean at that speed
on the same cable, same port, same session, `Hash of data verified`, ~20 s each.

> **CORRECTED 2026-08-30 (M5 stage D-A3): THE ENV-VAR REMEDY BELOW DOES NOT WORK, and
> following it reproduced the very failure it exists to avoid.**
> `PLATFORMIO_UPLOAD_SPEED` is **silently ignored** by this PlatformIO: the upload announced
> *"Changing baud rate to 921600"* and died mid-write, leaving the board partly erased exactly
> as described above. `pio run` in this version also has no `-O` / `--project-option`, so there
> is no command-line override either.
>
> **What actually works:** edit `upload_speed` in `firmware/platformio.ini` to 115200, flash,
> and **revert it immediately**, checking with `git diff firmware/platformio.ini` that nothing
> is left behind. Verified 2026-08-30: `Hash of data verified` on all four regions, ~91 s.
>
> The reasoning below for NOT lowering the committed constant is unchanged and still correct
> -- the failure has not been root-caused, and a future desk with a better cable should not
> inherit a slow flash. What changed is only that the mechanism named for keeping it
> un-lowered does not exist.

```powershell
cd C:\Development\Projects\DepthCharge\firmware
# Set upload_speed = 115200 in platformio.ini first, and revert it after.
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e depthcharge-binance -t upload
git -C .. diff --stat firmware/platformio.ini     # must be empty afterwards
```

The env-var override is deliberate rather than editing `platformio.ini`: **the failure has not
been root-caused** and may be this cable, this hub, or this bridge rather than the speed as
such. `esptool`'s own warning names the chip connection first. Until somebody establishes which,
lowering the committed constant would be pinning a workaround as a design decision — and a
future desk with a better cable would inherit a slow flash for no stated reason. **If 921600 is
ever seen to work reliably here, delete this section rather than leaving it to be true by luck.**

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

## Diagnosing a wrong picture

**Read the dark rows before touching a constant.** The `FM6126A` fallback above is the first
thing this file offers for a bad picture, and on 2026-08-24 that cost an evening: the driver
enum and `clkphase` were both tried and both reverted, because neither can reach the fault.
The address bus is not covered anywhere else here, and it is the more likely failure on a
bare-jumper interconnect.

### Signature: a dead address line

This panel is 1/32 scan. Five bits A–E select one of 32 row-pairs, and address *k* drives
physical row *k* on R1/G1/B1 and row *k+32* on R2/G2/B2. Lose one address bit of weight *w*
and two things follow mechanically:

1. Half the addresses are never selected, so **two dark bands of *w* rows each** appear —
   at rows *w*…2*w*−1 and *w*+32…2*w*+31.
2. The data for the unreachable addresses lands *w* rows earlier, so **every lit row shows
   two logical rows superimposed**: physical row *j* displays logical *j* and logical *j+w*.

**The width of the dark band names the bit.** E is the MSB, so a dead E gives *w* = 16:
rows 16–31 and 48–63 go dark, and the picture reads as "everything shifted up".

Observed 2026-08-24 with E (GPIO 10) loose, against the M4 ladder:

| physical | shows | reads as |
|---|---|---|
| 0–5 | header **+** asks 16–21 | red bars over the ticker symbol |
| **16–31** | nothing | black band |
| 32–34 | asks[0], spread, bids[0] — **unshifted**, addresses 0–2 are below 16 | correct-looking centre |
| 45–47 | bids **+** the cyan tape strip (logical 61–63) | a stray blue line mid-panel |
| **48–63** | nothing | black bottom quarter |

The fastest confirmation is the **heartbeat pixel**, which the renderer pins at (63, 63):
with E dead it sits at (63, 47), displaced by exactly one E-bit. If it is not in the
bottom-right corner, the address bus is the fault and no timing constant will fix it.

### What cannot cause it

- **`clkphase`** reaches only `invert_pclk` on the CLK pin.
- **`cfg.driver`** changes the power-on register sequence; `fm6124init()` resets only
  R1/R2/G1/G2/B1/B2/CLK/LAT/OE and never touches A–E.

Neither goes near the address bus. Both are dead ends for this symptom — check them off the
list rather than trying them.

### Checks, cheapest first

1. **Reseat the address jumpers** — A=17, B=18, C=8, D=9, **E=10**. This was the 2026-08-24
   cause: a bare jumper on E worked loose while the board was out on the bench.
2. **Continuity** GPIO → panel pin for the bit the band width named.
3. **Scope it.** A live address line is a square wave at the row-scan rate; flat or floating
   is the proof. Worth adding to the four-line survey above, which never covered A–E.
4. **From firmware, without touching the hardware:** set `cfg.gpio.e = -1` and flash. If the
   picture is identical to the fault, the panel is definitively receiving no E.

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
  - **2026-08-14 supersession: retried deliberately on the owned WS client (M3 transport
    rewrite), and the collapse does not reproduce.** A/B/A at ~10 min per arm, same divider
    24, RSSI −38…−47: 6.58 msg/s median (`true`, 25 min) → 6.78 (`false`) → 6.48 (`true`),
    byte rate pinned at the familiar ~44-50 KB/s inbound ceiling in all three arms (the
    same evening's window rebuild showed that ceiling is the RX path's, not the TCP
    window's — ARCHITECTURE §9 2026-08-14), `connects=1 sock_gaps=0`
    throughout. **`clkphase = false` now ships and the header-ghosting fix is free.** Whether
    2026-08-11's real variable was the old client or that week's mesh weather is left open —
    the separating experiment needs the deleted client. The M6 carrier items (ground plane,
    short leads, 74HCT245s) stay load-bearing for the remaining right-edge artefact, and the
    10× CLK probe is still owed. Logs `device-monitor-260814-{151322,154752,155929}.log`.
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
