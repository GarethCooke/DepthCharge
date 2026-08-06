# M2 — Bench bring-up

**Track:** Bench [B] · **Status:** Not started
**Executor:** Gareth at the bench, **not** Claude Code. This is a bench work order — wiring,
probing, measuring — so it reads as a procedure with decision points, not as CC commands.
**Read first:** `ARCHITECTURE.md` (repo structure; the `firmware/` and `hardware/` rows)
and the `ESP32-HUB75-MatrixPanel-DMA` library docs. Hardware is identified: panel driver
**FM6124** (chips read as `FM 6124D` + `FM TC7262E` latch), DevKit **ESP32-S3-WROOM-1 N16R8**
(16MB flash + 8MB octal PSRAM). The pin table and driver init below are pre-resolved from those.
**Depends on:** M0 (done). Independent of M1 — both are gated only on M0. **Gates M3.**
**Parts:** all on hand — 2× Muen P3 64×64 modules (1/32 scan, HUB75E), their HUB75 ribbons,
the two-module power cable; bench supply for 5V. No purchase required (a 2×8 IDC breakout
only if not already in the bits box).

## Goal

De-risk every hardware unknown so M3 can be pure firmware. At the end: one panel lit off the
DevKit with a known-good library test pattern, and four facts captured in the repo — the
verified HUB75E→ESP32-S3 pin map, the actual driver IC and its working init path, the
measured full-white current, and a scope check of signal integrity. M2 ships **knowledge and
a wiring contract**, not DepthCharge code.

## Deliverables (also the bring-up order — follow it; the sequencing is safety-relevant)

1. **Pre-flight facts — resolved from photos; confirm at the bench.**
   - **Driver IC: FM6124.** Read off the module back as `FM 6124D` (row/data driver) plus
     `FM TC7262E` (a latch in the data path — transparent, no config). The FM6124 is a
     constant-current driver needing an explicit init, **not** a plain shift-register — so
     expect a dark or garbled panel on any default config. Init is in step 4.
   - **DevKit: ESP32-S3-WROOM-1 N16R8** (16MB flash + 8MB octal PSRAM). The octal PSRAM makes
     **GPIO26–37 hard-reserved** — including the header pads silkscreened 35/36/37, which are
     physically present but wired to PSRAM inside the module and electrically unusable. The pin
     table in step 2 already avoids that trap.
2. **Pin map — pre-assigned to N16R8 free pins; wire to this, verify, record.**
   All 14 HUB75E signals mapped to GPIOs that are free on the N16R8 — none in the 26–37
   PSRAM/flash block, none strapping (0/3/45/46), none USB (19/20). They cluster on the DevKit's
   J1 header edge for tidy jumpering and easy scope access.

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

   Plus panel `GND` → DevKit `GND`. The panel's 5V comes from the **bench supply, not the
   DevKit** (step 3). The assignment isn't sacred — the library pins are fully user-defined, so
   any free pin works; the only hard rule is avoiding the no-go set, which this already does.
   Library config (confirm the struct field order against your library version — it is
   `R1,G1,B1,R2,G2,B2,A,B,C,D,E,LAT,OE,CLK`; the table above governs if they ever differ):

   ```cpp
   HUB75_I2S_CFG::i2s_pins pins = {
     4, 5, 6,           // R1, G1, B1
     7, 15, 16,         // R2, G2, B2
     17, 18, 8, 9, 10,  // A, B, C, D, E
     12, 13, 11         // LAT, OE, CLK
   };
   ```

   - **Verify against the module silkscreen** with a continuity check (multimeter/scope) —
     confirm each ribbon pin lands where you think **before** powering.
   - **Record this map** in `hardware/` — it becomes M3's firmware pin config and then cannot drift.
3. **Power, wired safely.**
   - Bench supply to **5V**, current limit set **low initially (~2A)**. Panel powered from the
     bench supply via the bare-wire harness into the binding posts; DevKit powered separately
     from **USB**.
   - **Critical:** the panel's 5V and the DevKit's USB 5V share **GND only** — do **not** tie
     the two 5V rails together. Common ground, separate supplies.
   - Power the panel with no data first: quiescent draw should be small. An immediate jump to
     the current limit = a wiring short → stop and recheck before going further.
4. **First light — known-good library example, not DepthCharge code.**
   - Flash a simple `ESP32-HUB75-MatrixPanel-DMA` example (a solid fill or the plasma demo)
     with the step-2 pin config and **`mxconfig.driver = HUB75_I2S_CFG::FM6124;`** set — the
     FM6124 needs this init or the panel stays dark. **Ramp brightness from low.**
   - Clean full panel → FM6124 init is correct; record it.
   - Still dark, or lit with ghosting / wrong brightness / half-panel → **fallback: try
     `FM6126A`** (closely related sequence; some 6124-marked panels want the 6126A init). One
     of the two produces a clean panel — record which.
5. **Measure the draw — this sizes the M3 supply.**
   - Display **full white at full brightness**; read the bench supply's current. That worst-case
     figure sizes the standalone M3 PSU (a screw-terminal Mean Well, 5V with headroom over this
     number — the bare-wire harness bolts straight to it).
   - Also note draw at representative ladder content (mostly dark, a few coloured bars) for the
     realistic figure. Record both.
   - Confirm the bench supply **holds 5V** under full-white load — sag here is the brownout that
     masquerades as a firmware fault later.
6. **Signal integrity — the scope check your bench makes possible.**
   - Probe **CLK, LAT, OE, and one RGB data line** against what the library emits. Confirm clean
     edges, expected timing, no ringing / ground-bounce that would corrupt data at clock speed.
   - This is the hardware analogue of M1's independently-derived goldens: verify the timing is
     right, don't just trust that it lit. If the jumper leads ring or glitch, **record it** —
     M3 may need shorter leads, a ground plane, or the IDC breakout seated closer, rather than
     that being debugged as firmware.
7. **Write it down.** Create the bring-up record in `hardware/` (e.g. `hardware/BRINGUP.md`)
   capturing: panel spec, driver IC + working init, the verified pin map, measured full-white
   and representative draw, and the signal-integrity notes. This doc is the source M3's firmware
   and the eventual enclosure both read from.

## Constraints

- **Don't contaminate `engine/`.** M2 uses a third-party library example as a probe; keep all
  bench/test code in a scratch or `firmware/`-experiment location, never in `engine/` (invariant
  #1 in spirit — the engine stays host-buildable with no library/hardware leakage). M3 is where
  DepthCharge firmware properly meets the panel.
- **One panel only.** Bring up a single module. The second exists; chaining to 64×128 is outside
  current ARCHITECTURE scope — don't design for it here.
- **No DepthCharge rendering logic.** No ladder, no engine, no `DisplaySnapshot` on the panel yet
  — that's M3. M2 proves the physical chain with known-good code.

## Known unknowns (resolve and record)

- **Driver IC** — ✔ resolved: FM6124 (fallback FM6126A). Confirm at first light. (Steps 1, 4.)
- **DevKit reserved pins** — ✔ resolved: N16R8 → GPIO26–37 + strapping + USB off-limits; the
  pin table avoids them. (Steps 1, 2.)
- **Final GPIO pin map** — ✔ pre-assigned (step 2); the open part is the continuity check
  against the module silkscreen before power. (Step 2.)
- **Full-white current** — open: measured, not estimated; sizes the M3 PSU. (Step 5.)
- **Signal integrity over jumpers** — open: does bare-jumper wiring hold at HUB75 clock speed,
  or does M3 need shorter leads / a ground plane / a carrier? The scope answers it. (Step 6.)

## Definition of done

☐ One panel displays a stable, correct known test pattern from the library example.
☐ Driver IC identified; the working init path (default or specific) recorded.
☐ Full-white current measured and recorded; bench supply confirmed to hold 5V under it.
☐ Signal integrity checked on CLK/LAT/OE/one RGB line; any concerns recorded.
☐ Verified HUB75E→ESP32-S3 pin map recorded in `hardware/`.
☐ `hardware/BRINGUP.md` (or equivalent) created with panel spec, driver IC + init, pin map,
  draw figures, and integrity notes.
☐ No changes to `engine/` or any DepthCharge source; bench/test code kept out of the engine boundary.
☐ Session log filled in; ROADMAP M2 ticked and M3 marked Next.

## Out of scope

DepthCharge firmware and engine code (M3); the render task and the ladder on the panel (M3);
Wi-Fi / TLS / network (M3); the standalone PSU purchase (deferred — M2 produces the sizing
number); enclosure, acrylic, mounting (post-M3); chaining the second panel / 64×128 (outside
current ARCHITECTURE scope); a custom carrier PCB (only if the signal-integrity check proves
jumpers inadequate — then it's a future hardware task, not M2).

## Session log

<!-- Append one block per session: date · done · decisions (with why) · measured figures ·
     exact next step. -->
