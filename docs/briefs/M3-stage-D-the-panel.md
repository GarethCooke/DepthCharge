# M3 Stage D — the panel

**Track:** Mixed [A+B] · **Status:** CC half done 2026-08-10 (host-proven, not flashed); bench acceptance outstanding · **Size:** one evening
**Executor:** Mixed. Claude Code writes the firmware and the writeback; **Gareth flashes,
watches the panel, and takes the photo.** CC does not drive the board.

**This expands the M3 brief's Stage D section — it does not replace it.**
`docs/briefs/M3-live-anvil-on-the-panel.md` remains the milestone's Definition of Done and
the session log; this file is the work order for the sitting. Its outcome lands as a session-log
entry there and as the two Stage D ticks in that file's DoD.

**Read first**

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §2, §5, §6 | The two-core split, the `DisplaySnapshot` contract, the invariants. **§6 is frozen — #5 is what this stage exists to prove.** |
| `ARCHITECTURE.md` §9, the two 2026-08-09 entries | Why `kRxWatchdogMs` stays at 1000 ms, and why invariant #7 reads in two halves on the target. |
| `docs/briefs/M3-live-anvil-on-the-panel.md` | Stage D section + **all four session logs**. The hardware pin block at the top is pinned — wire to it, do not re-derive. |
| `hardware/BRINGUP.md` | M2's output: verified pin map, FM6124 init path, measured draw, signal integrity. |
| `hardware/bench-2026-08-09-ws-reconnect.md` | Stage C's evidence, the recovery decomposition, and the **2.5 s blocking `stop()` on loopTask — which is Core 1, and therefore yours now.** |
| `firmware/README.md` | "What runs where", and the **`dc_feed` never logs** rule. |
| `firmware/src/serial_console.hpp` | The shape you are replacing. Read the header comment before the body — it says what must not change. |

**Depends on:** Stage C, done and bench-proven. **Two commits are pending and must be in the
tree before you start** (parse instrument; the two-handle reconnect fix). If `git status` does
not look like that, stop and ask.

---

## State that is not in the repo yet — binding context

Three things were settled at the desk after the last session log was written. You need all
three, because two of them close findings the repo still shows as open, and one of them will
otherwise send you chasing a bug that no longer exists.

**1. The steady-state flashing is SOLVED, and it was not the watchdog's fault.** Root cause was
Deco mesh mis-association — the board had attached to a far node at −75 dBm. Moving it to the
near node (−34 dBm) eliminated the mid-connection stalls; an 11-minute run showed two ~130 ms
cadence blips and nothing else. Band-steering and fast-roaming were already off, so steering was
never the mechanism.

This closes `hardware/bench-2026-08-09-ws-reconnect.md`'s **"Open finding: the RX watchdog trips
on healthy data."** ARCHITECTURE §9 had already ruled the correct half — Anvil's cadence is
17.02 frames/s with a 391 ms worst gap, the board's multi-second holes were board-side, and
raising the threshold would have hidden a real freeze. The mesh fix names the board-side cause.
**`kRxWatchdogMs` stays at 1000 ms. Do not re-derive it, do not nudge it, and do not touch
`ReplayOptions::disconnect_gap_ms` — the goldens are pinned to it.** If the panel greys on a
healthy feed during your bench run, that is a finding to report, not a constant to change.

**2. The transport residual is decided and shipping.** What remains is occasional socket-drop
reconnects on a strong, stable association — roughly four per eleven minutes, in pairs, then
minutes of clean running. The two-handle reconnect fix takes a drop from ~9.5 s to ~4.7 s of
grey. **That is the whole treatment. A reconnect grey is invariant #5 telling the truth, and
Stage D's job is to render it, not to prevent it.**

**3. Parked — not Stage D, do not reopen.** The parse-burst reassembler fix (client-side
reassembly truncating large book frames under the connect flood), and the optional
`WIFI_EVENT_STA_DISCONNECTED.reason` logging. Both are real, both are low priority, neither is
this evening.

---

## Goal

The object comes alive. The same `engine/` that runs under `ctest` on the desk draws a live
Anvil order book on the 64×64 panel M2 lit: bids stacking green below the spread, asks red
above it, the last price tracking, and — the whole point — **the moment the book stops
advancing, the colour drains out of the panel within a second and does not come back until a
fresh snapshot does.** That is invariant #5 in copper and light, and it retires the last
structural risk in the project.

---

## Deliverables

### 0. The writeback — first move, before any firmware (~15 min)

Add an M3 session-log entry to `docs/briefs/M3-live-anvil-on-the-panel.md` recording the three
items above: the mesh root cause and the −75 → −34 dBm fix, the transport-residual decision, and
the parked list. **Explicitly close the Stage C bench record's open finding** — a line in
`hardware/bench-2026-08-09-ws-reconnect.md` under that heading pointing at the new entry is
enough; do not rewrite the run data.

Do this first and separately. It is a docs-only change, it is what stops the *next* session
re-opening a settled question, and it means the rest of the evening can be pure firmware.

### 1. The render task — replace the body, keep the shape

`serial_console.cpp`'s body becomes the HUB75 renderer. The header comment names the three
properties Stage C proved on silicon and that this stage must not break: **Core 1**, consumption
via `SnapshotChannel::consume` with a redraw only when it returns true, and **reading nothing but
the `DisplaySnapshot` it was handed** (invariant #8).

- **It stays the single consumer.** Only the render task reads `DisplaySnapshot`; there is no
  second reader and no third participant. If the serial evidence and the panel both want the
  frame, they get it from the same `consume()` in the same task.
- **Keep the serial output.** The `*** STALE (reason) at vN ***` / `*** LIVE at vN ***`
  transition lines and the 10 s statistics block are the bench acceptance evidence and the only
  way to read the run after the fact. They cost a blocked task for a few ms; the HUB75 DMA
  refreshes from the framebuffer autonomously, so a blocked render task means one skipped
  redraw, not a dark panel.
- **Priority above `loopTask`.** The supervisor's `esp_websocket_client_stop()` blocks for
  ~2.5 s on `loopTask`, which is Core 1. The console runs at priority 3 today against
  `loopTask`'s 1 (the Arduino default); keep that margin or better, and **do not move any
  supervisor work into the render task.** During that 2.5 s the panel should already be grey —
  the `Gap{Disconnect}` was published from Core 0 — and the render task must stay awake to keep
  drawing it. Confirm at the bench that the reconnect grey looks the same as any other grey; a
  visible hitch there is the 2.5 s block showing through, and it is a finding, not a fix.
- **Drive the loop off the panel frame period**, not Stage C's flat 20 ms `vTaskDelay`. Target
  the concept's ~30 fps; idle rather than spin when `consume()` reports nothing new.
- **A one-pixel render heartbeat, toggled every drawn frame, in a corner.** Two lines of code.
  Invariant #5 covers the *feed* going quiet; nothing covers the *renderer* dying, and a dead
  render task leaves exactly the frozen ladder the project calls its one unacceptable output.
  The heartbeat is the cheapest possible coverage and doubles as an fps read at the bench.

### 2. Panel bring-up — M2's contract, unchanged

Pin map and FM6124 init exactly as pinned in the M3 brief's hardware block and
`hardware/BRINGUP.md` (`mxconfig.driver = HUB75_I2S_CFG::FM6124;`, fallback `FM6126A`;
`R1=4 G1=5 B1=6 R2=7 G2=15 B2=16 A=17 B=18 C=8 D=9 E=10 CLK=11 LAT=12 OE=13`).
`ESP32-HUB75-MatrixPanel-DMA` on the Arduino 2.0.14 / espressif32 6.5.0 stack already in
`platformio.ini` — the language ceiling is xtensa GCC 8.4, so `-std=gnu++2a` and no `<span>`,
`<ranges>`, `<concepts>` or `<bit>`.

Three traps M2 already stepped over, all in BRINGUP:

- **The `i2s_pins` struct order is `R1,G1,B1,R2,G2,B2,A,B,C,D,E,LAT,OE,CLK`** — LAT, OE, CLK,
  in that order, so the last three initialisers are `12, 13, 11` and *not* `11, 12, 13`.
  M2 flags it as version-dependent: **confirm the field order against the library actually
  vendored, and if it differs, the BRINGUP pin table governs.**
- **Panel 5 V from the PSU, never the DevKit; common GND only, 5 V rails not tied.**
- **Pixel-shift artifacts are a wiring symptom first.** Bare jumpers are bench-proven for M3,
  but if the panel shows shifted or torn pixels, re-scope CLK on a 10× probe **before**
  suspecting the firmware (BRINGUP forward note).

**Framebuffer memory is the one real unknown and it must be measured, not assumed.** Library
buffers are internal DMA-capable RAM — never PSRAM. Stage C's build sat at RAM 40.7 %. Report
the number before and after, and the library's own reported allocation. **If double buffering at
the default colour depth does not fit, lower `color_depth_bits` before giving up the second
buffer** — a two-colour depth ladder loses nothing to a shallower ramp, and tearing on a book
that redraws 13×/s is a visible defect. Record whichever way it lands, and record the PSRAM
answer (`BOARD_HAS_PSRAM` / `memory_type = qio_opi`) that Stage C left open.

### 3. The ladder

The M1 console ladder is the visual spec; the panel renders the same `DisplaySnapshot` at 64×64.
Aesthetics within the constraints below are your call (§8), as the console ladder's were.

**The row arithmetic, which is tighter than it reads.** `kDisplayLevels` is 27 a side, so
27 + 1 spread + 27 is **55 of the 64 rows** before a header or a sparkline exists. Nine rows
remain for both. At one pixel per level there is no room for per-level price text — the ladder is
a depth histogram, and text is reserved for the header anchors (symbol, last price, and best
bid/ask if they fit).

**If the arithmetic does not work against the real font, draw fewer levels. Do not change
`kDisplayLevels`** — that is a §5 change to `engine/`, and it means stop and raise it.

- Bids green below the gap, asks red above it, best-of-book adjacent to the spread on both sides
  (`asks[0]` and `bids[0]` are already best-first).
- Bar length encodes qty, normalised against the largest qty in the visible window. **Integer
  arithmetic only** — `len = qty * width / max_qty` in 64-bit, no float touches book data
  (invariant #3). Floats are permitted only at the text-formatting edge.
- Prices formatted with `depthcharge::format_scaled` into a stack buffer, exactly as
  `serial_console.cpp` already does it. **No `String`, no `std::string`, no `printf("%f")`, no
  `harness/`'s `format_px`.** Pass `const char*` to the GFX print methods.
- Redraw only on a new version. `consume()` already reports nothing-new.

### 4. The two-state render — the reason this stage exists

**One palette selection, taken from `snap.status`, at the top of the draw — and no other code
path picks a colour.** Make it structurally impossible to draw a live-coloured ladder from a
stale snapshot, rather than a rule someone has to remember. The honesty bit travels inside
`DisplaySnapshot` precisely so this is available.

- `status == Stale` → the whole panel renders in a single dim grey ramp. **Geometry stays,
  hue goes.** The shape of the book is still readable, and from across the desk it is
  unmistakably not live.
- **Grey, never blank.** A dark panel is ambiguous with "powered off" and says nothing where the
  invariant requires it to say "not trusted".
- **The boot frame.** Stage C publishes one frame before any data arrives, so v1 is
  `Stale{Resync}` with `bid_count == ask_count == 0`. That must render as an honest grey empty
  frame — a header, a greyed chrome, no ladder — not a black screen.
- Show the stale reason somewhere small. It is in the payload; the bench should not have to read
  the serial log to know whether it is looking at a disconnect or a resync.
- **There is no third state.** No "probably fine", no fade, no partial colour, no last-known-good
  overlay. Live or Stale.

### 5. Stretch — cut these without guilt if the evening runs out

Marked stretch deliberately: the stage is done and shippable without them, and neither is worth
a second sitting on its own.

- **Trade-print flash.** The trade ring is already in the payload (`kTradeRingSize` 8, newest
  first), so this is a colour test at the touched level, no new state. White flash decaying over
  a few frames.
- **The last-price sparkline strip.** The decision is already made and must be *recorded* even if
  the code is not written: **render-side sampled state, a fixed ring in the render task — never a
  new `DisplaySnapshot` field.** Sample on version change when `has_last`. Adding a field would be
  a §4/§5 change; if the render-side ring genuinely cannot work, stop and raise it rather than
  extending the type. Note that `last_px` legitimately advances while Stale (tape is still real);
  the grey wash covers it, so no special case is needed.

### 6. Acceptance — at the bench, owner-driven

1. **Pull the Wi-Fi.** Live ladder → pull → **panel greys within ~1 s** (watchdog deadline
   1000 ms plus at most one render period) → restore → next snapshot → clean live ladder. No
   torn frame, no frozen intermediate, no flash of a coloured stale book. This is the M3 DoD line.
2. **A ~10-minute steady soak on the near node.** With the mesh fix in place the panel should hold
   colour throughout; `wd_gaps` should stay at 0 and `sock_gaps` low. Any grey should correspond
   to a socket drop in the log, and a reconnect grey should read ~4.7 s. **This is the visual
   confirmation that the mesh fix holds** — the first time it is checked on a panel rather than
   in a log.
3. **No regression on the feed side.** The `arrive` / `event` / `a→e` histograms with the panel
   running against Stage C's baseline: board ~90 % idle, a→e ≤ 22 ms. If the LCD_CAM DMA and the
   Wi-Fi/TLS stack are starving each other, this is where it shows, and it is a priority /
   placement question — not an engine one.
4. **Heap flat over the soak.** Free delta 0, largest block not falling, per invariant #7's
   target-side reading. The render task now carries the logging that used to be the console's, so
   this is worth re-reading rather than assuming.
5. **Photo or clip committed to `hardware/`**, as M2's first-light photo was. Live ladder and the
   grey, ideally the same run. This is also what unblocks **MP stage 2** (real hardware
   photos/video on the portfolio portal), which has been gated on M3 since the roadmap was
   written — so shoot it like it will be published, because it will be.

---

## Guardrails

- **All §6 invariants apply and are frozen.** If a step seems to need violating one, **stop and
  raise it** — do not refactor through it. #1: nothing ESP-IDF, FreeRTOS or Arduino enters
  `engine/`, and **`engine/` should not be touched at all this stage.** #4: the feed task is never
  blocked by the render task. #5: above. #7: no heap allocation on the feed→render path after
  connect and first snapshot — the library's `begin()` allocation is before that boundary and is
  fine; anything per-frame is not. #8: one writer, one reader, one meeting point.
- **A moved golden means stop.** Stage D touches `firmware/` only, so `test_replay_goldens.cpp`
  cannot legitimately move. If it does, something is wrong that is bigger than this stage.
- **`cmake --workflow --preset host` stays green on every commit**, including the firmware ones.
- **`dc_feed` contains no `ESP_LOGx` and Stage D must not add one.** Arduino routes it through
  `log_printfv`, which allocates and takes the UART mutex — a log line on the feed task couples
  the two cores exactly as invariant #4 forbids.
- **Do not lower `CORE_DEBUG_LEVEL`** while the serial log is still the acceptance evidence.
- **Commit only when asked.** Work in the tree, run the host workflow, report what changed and
  what it measured. Gareth decides what gets committed and when.
- **Do not reopen** the watchdog constant, the parse-burst reassembler, or the disconnect
  reason-code logging.

## Known unknowns — resolve and record

Framebuffer fit (colour depth × double buffer × internal RAM) and the PSRAM lines. Render task
priority and period against `loopTask` and its 2.5 s blocking `stop()`. LCD_CAM/GDMA coexistence
with Wi-Fi/TLS at ~30 fps against Anvil's ~13.4 book frames/s. The row budget against the real
font. Brightness — M2 measured 2.6 A at full white and **0.25 A at representative ladder
content** against a 5 V/5 A supply, so there is no power argument for running dim; record what
you set and why.

## Definition of done

- [x] Session-log entry written; Stage C's watchdog open finding explicitly closed.
      *(2026-08-10 entry in `M3-live-anvil-on-the-panel.md`; the bench record's finding heading
      now carries a CLOSED callout pointing at it, run data unedited.)*
- [x] Render task on Core 1 draws the live ladder — bids green, asks red, spread gap, last price.
      *(`render_task.*`, was `serial_console.*`. Core 1, priority 3, 33 ms period off
      `vTaskDelayUntil`, redraw gated on `consume()`. **Confirmed on the panel 2026-08-11** and
      photographed.)*
- [x] `status == Stale` greys the whole panel through a single palette selection; boot frame
      renders as honest grey, not black.
      *(Stronger than asked: the renderer emits `Ink` and cannot name a colour;
      `static_assert(all_grey(kStalePalette))` makes a coloured stale panel a **build
      failure**. **Confirmed on the panel**: the honest grey `RESYNC` frame appears before
      Wi-Fi finishes associating, and the ladder colours on the first snapshot.)*
- [x] Render heartbeat pixel present. *(Bottom-right, toggled every drawn frame, host-tested.)*
- [ ] Pull-the-Wi-Fi acceptance passes: live → grey within ~1 s → clean resync. **— owner, at
      the bench. The BEHAVIOUR was observed dozens of times on 2026-08-11 (grey on watchdog and
      on socket drop, clean resync, `grey for N ms` matching the log); what is missing is the
      deliberate acceptance run and its record.**
- [ ] ~10-minute soak holds colour; greys correspond to logged socket drops.
- [ ] Feed-side histograms and heap unregressed with the panel running. *(Looked healthy on
      every good run — `a→e` all in the 5–25 ms bucket, `no_slot=0`, heap flat, `worst paint`
      ~2 ms of the 33 ms period — but not read as a deliberate ten-minute comparison.)*
- [ ] Photo/clip in `hardware/`. **Photographed twice during the session; neither image is
      committed yet, and that is the outstanding half.** *(Everything else this line asks for
      is recorded: RAM/flash 41.9% / 13.2%, task priorities feed 5 on Core 0, render 3 on
      Core 1, loopTask 1; PSRAM present at 8,385,975 B and **not** used for the framebuffer;
      colour depth **6, double-buffered**, 78,080 B, chosen at run time and printed at boot.)*
- [ ] M3 brief's Stage D ticks filled; **ROADMAP M3 ticked and M4 marked Next only when the
      acceptance passes.**

## Out of scope

The stretch items if the evening runs out. Brightness/ambient-sensor polish. Multi-ticker,
venue switching, the rotary encoder. Chaining a second panel. OTA, provisioning UI, the carrier
PCB. Any change to `engine/`, to the `FeedEvent` / `DisplaySnapshot` vocabulary, or to the
watchdog constants. The three parked transport items. Kraken, Binance, and anything delta-venue.
