# M6 — Carrier PCB

**Track:** Bench · **Status:** Not started
**Read first:** `/ARCHITECTURE.md` §6, `ROADMAP.md` M6 row, `hardware/BRINGUP.md`,
`hardware/bench-2026-09-13-D7-switch-on.md`, `hardware/bench-2026-09-13-clk-10x.md`.

**This brief generates no board files.** `CLAUDE.md` Boundaries: the hardware track is
owner-driven and sessions prepare checklists and review artefacts, not KiCad. What follows is
the requirement list, each item carrying the measurement that put it there, plus the questions
the layout has to answer and the ones it cannot.

## Goal

At the end of M6 the object runs on a carrier board instead of a DevKit and a handful of
jumpers, and the two panel artefacts that have been open since M3 — the header ghosting and the
right-edge residue — have been **re-tested on that board** rather than argued about. The
interconnect stops being a variable. Until it is one PCB, every signal-integrity question
reopens on every re-plug, and M7's enclosure has nothing to mount.

The carrier is also the only way to settle a trade M3 could not: `clkphase = false` clears the
ghosting and, on jumpers in 2026-08-11's measurement, collapsed inbound Wi-Fi. That collapse did
not reproduce on the owned WS client (2026-08-14) and the separating experiment needs a board
whose ground plane removes the mechanism. **If the noise argument evaporates on the carrier, the
ghosting fix is free** — `BRINGUP.md:202` has been asking for that re-test for a month.

## Requirements (each with the measurement behind it)

**Signal integrity**

1. **2× 74HCT245 buffering all 13 HUB75 lines.** Already in the ROADMAP row; `BRINGUP.md:202`
   upgraded it from nice-to-have to load-bearing.
2. **Series termination footprints at every 245 output — populated, not optional.**
   `bench-2026-09-13-clk-10x.md` §5: CLK overshoots to 3.72 V worst against a 3.18 V rail and
   undershoots to **−760 mV**, past the ~−0.3 V input floor these receivers specify, on a fully
   powered panel. An HCT output drives harder and transitions faster than the S3 pad it
   replaces, so the buffers alone do not fix this. **~33 Ω is a placeholder** — the real value
   comes from the trace impedance at layout (see Known unknowns 1). **The footprints are the
   irreversible decision, not the value.** Pads left unpopulated or fitted with 0 Ω links cost
   nothing; pads not placed cost a respin the first time the artefact persists.
3. **Unbroken ground plane under the whole HUB75 run.** `BRINGUP.md:180`: the 2026-08-11
   ghosting tracks *switching activity*, not current — dimming made it worse — and the aggressor
   is ~14 outputs switching simultaneously down a ribbon with no return path.
4. **Shortest practical HUB75 run.** Place the IDC to minimise trace length before anything
   else in the layout. Tonight's ~57 MHz ring is the 20 cm jumper pair resonating; the fix is
   geometry first, termination second.
5. **Antenna keepout.** `BRINGUP.md:184`: the DevKit's 2.4 GHz antenna currently sits a hand's
   width from the ribbon and the panel "jams its own radio". The module's antenna edge gets
   clear board area and no HUB75 copper under or beside it.

**Power**

6. **Panel-side load switch, firmware-enabled after boot.** Two independent measurements, both
   the same structure. `bench-2026-09-13-D7-switch-on.md` §3: with the panel's own supply off,
   the board **phantom-powers it through its input clamp diodes**, and that state is the rail's
   worst. `bench-2026-09-13-clk-10x.md` §5: with everything correctly powered, the same clamps
   are forward-biased by CLK's undershoot. On a single-supply carrier the first case returns at
   **every cold boot** — 3V3 comes up through a regulator faster than the panel's bulk caps
   charge, so GPIOs drive into an under-volted panel for some milliseconds. Pick a part with
   controlled slew and it solves the bulk-cap inrush in the same component.
7. **The enable must precede the first render.** `firmware/src/panel.cpp` deliberately washes
   the framebuffer between `begin()` and the first draw so the panel is never "on and black" —
   invariant 5's reading rule. A load switch inserts a new ordering constraint into exactly that
   window; enabling it late reintroduces the state that wash exists to prevent.
8. **Bulk capacitance sized against 2.6 A, not 0.25 A.** `BRINGUP.md` Power: full white at full
   brightness is the PSU number and representative ladder content is a tenth of it.
9. **DECIDE THE INPUT — the ROADMAP's USB-C 5 V/3 A has no margin.** 2.6 A panel worst case plus
   ~0.3 A module is **2.9 A against 3.0 A: 97 % of budget.** The M3 standalone supply is a
   5 V/5 A Mean Well precisely because 2.6 A wanted headroom. Three honest options, and this
   brief does not pick one: **(a)** USB-C alone, with a *measured* firmware brightness ceiling
   rather than an assumed one — defensible, since a ladder render never approaches full white;
   **(b)** USB-C for the module and a separate 5 V input for the panel, which keeps the 5 A
   supply and costs a second connector; **(c)** USB-C alone, documented as brightness-limited.
   Whichever is chosen, record it with its number.
10. **Do not assume a 5 V header pin exists.** `bench-2026-09-13-D7-switch-on.md` §4: the
    DevKit's is not driven. The carrier takes 5 V from its own input, not from a module pin.

**Module and housekeeping**

11. **ESP32-S3-WROOM-1 N16R8**, bare module — 16 MB flash, 8 MB octal PSRAM.
12. **Reserved GPIOs honoured:** 26–37 (flash + octal PSRAM), strapping 0/3/45/46, native USB
    19/20. `BRINGUP.md` records that pads silkscreened 35/36/37 exist on the DevKit and are
    electrically unusable — the carrier has no such trap to inherit, but the pin map must not
    reintroduce one.
13. **Pin map carried forward unchanged** from `BRINGUP.md`'s continuity-verified table, so the
    shipped firmware flashes onto the carrier without a source change. Any deviation is a
    firmware change and must be argued, not discovered at bring-up.
14. **Programming and serial header**, with `BRINGUP.md`'s 115200 flashing trap in mind: if the
    carrier's USB bridge makes 921600 reliable, that section gets deleted rather than left true
    by luck.
15. **EC11 rotary encoder** — M7's input, placed now because adding it later is a respin.

## Deliverables

1. KiCad schematic.
2. KiCad layout, DRC clean.
3. Fab package generated and ordered.
4. Assembled board, bring-up checklist executed and recorded as `hardware/bench-<date>-M6-carrier-bringup.md`.
5. **The three CLK frames repeated on the carrier**, recalling
   `hardware/bench-2026-09-13-clk-10x-setup.xml` so the comparison is an A/B and not a
   reconstruction. Overshoot, undershoot and ring frequency against tonight's numbers.
6. **`clkphase = false` re-tested** on the carrier, with the msg/s figure beside it.
7. `hardware/BRINGUP.md` updated: carrier pin map, any deltas, and the sections the board makes
   obsolete deleted rather than left standing.

## Known unknowns

1. **Trace impedance, and therefore the real series value.** 33 Ω is a placeholder. Compute it
   from the stack-up at layout and record the arithmetic.
2. **The FM6124's actual input floor.** Its datasheet is not in this repo. The −0.3 V figure in
   tonight's record is the generic shift-register number and is explicitly flagged as unverified.
   Confirm before quoting it in a design document.
3. **Does `clkphase = false` still cost the radio on a board with a plane?** If not, the
   ghosting fix is free and `firmware/src/panel.cpp`'s shipped setting changes.
4. **Is `S3_LCD_DIV_NUM = 24` still wanted?** Its stated justification is falsified in mechanism
   (`bench-2026-09-13-clk-10x.md` §4 — a divider changes repetition rate, not transition time)
   while its setup/hold and radio-noise reasons stand. The falsifier is cheap and does **not**
   need the carrier: rebuild at 16, reflash, re-read Rise and Fall. Worth doing before layout,
   because the answer changes how hard requirement 2 has to work.
5. **Does the carrier clear the right-edge artefact?** `BRINGUP.md:197` leaves it open and names
   the M6 items as its candidate fix. If the board does not clear it, that is a finding.

## Definition of done

- ☐ Schematic and layout committed; DRC clean with the report in the repo.
- ☐ Board fabbed, assembled, and powered without smoke.
- ☐ Shipped firmware flashes and runs unchanged — no source edit to accommodate the board.
- ☐ CLK re-measured against the saved setup; overshoot and undershoot recorded against tonight's.
- ☐ `clkphase = false` re-tested with a msg/s number.
- ☐ Ghosting and right-edge artefact each recorded as cleared, unchanged, or changed.
- ☐ Power input decision recorded with its measured number.
- ☐ `hardware/BRINGUP.md` updated; obsolete sections deleted.
- ☐ ctest green from a clean clone (nothing here should touch it — if it does, that is the finding).
- ☐ Session log appended; `ROADMAP.md` M6 status updated.

## Out of scope

- **The enclosure** — M7, and it depends on this board's outline rather than the other way round.
- **Board modes and the encoder's behaviour** — M7. M6 places the part and routes it, nothing more.
- **Deeper edge characterisation.** Tonight's rise and fall are at the SDS1104X-E's 3.5 ns floor.
  A real number wants a 500 MHz instrument and is not worth buying one for.
- **Probing at the panel connector.** The right measurement, eventually, but it needs a
  spring-tip ground and its own sitting.
- **Anything in `engine/`.** No invariant in §6 is in play here.

## Session log

<!-- Append one block per session. -->

**2026-09-13 · Opus (chat seat) · drafted.** Written from the two bench records taken the same
evening (`bench-2026-09-13-D7-switch-on.md`, `bench-2026-09-13-clk-10x.md`), plus `BRINGUP.md`'s
accumulated M6 requirements. **No board files generated** — `CLAUDE.md` Boundaries.
*Decision: requirement 2 (series termination) is stated as mandatory footprints rather than a
mandatory value*, because the value depends on a stack-up that does not exist yet while the pads
are irreversible at fab. *Decision: the power input is left as three costed options rather than
picked*, because 2.9 A against 3.0 A is a judgement about how the object will be used, not a
measurement. **Exact next step:** run Known unknown 4's falsifier (rebuild at
`S3_LCD_DIV_NUM=16`, reflash, re-read Rise/Fall on the saved scope setup) — it is ten minutes,
needs no board, and its answer changes how hard the termination has to work.
