# M4 — the closing bench sitting

**Track [B], yours. Record, decide, work-order. Do not fix anything at the bench.**

Ordered so the observations that need the board undisturbed come before anything that power-cycles
it. Board should be `live=1` before you start; if DNS is out, items 1 and 2 wait.

---

## 1 · Uninitialised rendering *(B2 decision, board must be live)*

The state stage C made explicit and deliberately did not resolve. Two candidates: **27 rows of
unknown**, or **grey**. It differs in nothing a host test can assert, which is why it is here.

**Where to see it.** Two routes, and the second is free:

- **At boot**, between power-on and the first snapshot. Brief.
- **On every CRC heal** — book dropped, resubscribe, fresh snapshot. Median 2,080 ms, and the soak
  saw 193 in 25 hours, so roughly one every eight minutes. Watch the panel for ten minutes and one
  will present itself.

**What to judge.** Whether a panel full of unknown reads as *nothing has arrived yet* or as *the
panel is broken*. Grey already means "do not trust this"; if unknown reads the same at desk
distance, the distinction is invisible where it matters and grey is the simpler answer.

Record: which you chose, and what it looked like. One sentence each.

## 2 · Depth *(B2 decision, board must be live)*

Stage C's result is that at depth 25 into 27 rows nothing is dropped and all three policies are
byte-identical — the window is a no-op at the shipped configuration. So this is not a policy
choice; it is whether to subscribe deeper than the panel can draw.

**What it costs and buys.** Checksum share barely moves: 40.0% validated at depth 25 against 37.0%
for top and thinned at depth 100. The real price is parse load on the ESP32, and the soak is the
only thing that has measured that.

**What to judge at the panel.** Whether 27 rows of a 25-level book looks like the whole book or
looks truncated. If it looks complete, depth stays 25, the policies stay a no-op, and D6 stays
parked — which is the cheap answer and the one stage C's numbers argue for.

Record: the decision, and whether D6 moves with it.

## 3 · D7's scope trace *(needs the Siglent; power-cycles the board)*

One switch-on replaces sixty. Do this after items 1 and 2, since it ends the current uptime.

- Probe the **DevKit's 3.3 V rail**, and the 5 V feeding it if you have a second channel.
- **Read the configured brownout threshold out of the build** rather than assuming a number — the
  margin is only meaningful against the actual BOD setting.
- Trigger on the falling edge, single shot, then switch the panel on. Ribbon attached, board live
  if you can manage it; if DNS is out, take it anyway and note the state.
- Capture dip depth, dip duration, and margin to threshold.
- **Save the trace to `hardware/`** alongside M2's photos, with the trigger settings and the probe
  points written beside it. That is what turns D7 from an anecdote into evidence.

Record: the numbers, and whether sequencing is a requirement or a nice-to-have at M7. That is the
question the scope exists to answer.

## 4 · B4's residues *(the board is already out)*

- **`clkphase = false` ghosting re-check.** Flip it, look, flip it back. Visual, one minute.
- **D1 — rejoin re-rolls the mesh lottery.** Needs the far node.
- **D2 — weak-node one-hour soak.** Unattended once started; start it last and let it run while
  you write up.

---

## What not to do

No firmware changes, no constants moved, no "while I'm here" fixes. Every finding becomes a work
order for a desk session. Eleven stages of this project have held that line and the bench evening
is where it usually breaks.

## In parallel, at the desk

The four lead times — 141, 291, 140, 301 s — are two pairs, and the second is about twice the
first. One read of the log around those four events, asking which counter reaches which limit at
each: a retry budget, a keepalive count, a retransmission sequence. If the mechanism can be named,
half-open detection becomes a predictable margin rather than an observation, which is worth having
before Binance at M5. CC's job, not the bench's.

## Closing M4

M4 is complete when the panel renders a Kraken book off the wire, greys within the calibrated
liveness threshold when the heartbeat stops, holds colour through 26 s of legitimate book silence,
heals from a checksum failure rather than greying permanently, and shows a book age that is a lag
estimate rather than a time-since-anything. Everything but items 1 and 2 above is already
evidenced.

---

## Session log

### 2026-08-24 · Claude Opus 5 · the bench sitting

**The evening opened on a fault, not on the brief.** The panel was rendering the ladder
displaced upward with red bars over the ticker symbol, at boot and at every CRC heal. It cost
most of the sitting and is the reason items 3 and 4 are largely unrun.

**Cause: the E address line (GPIO 10) had worked loose** — a bare jumper, pulled during the
brownout investigation. Not firmware. The renderer was never wrong.

Two levers were tried and reverted first, both dead ends, and the reason they are dead ends is
worth keeping: `clkphase` reaches only `invert_pclk` on the CLK pin, and `cfg.driver` changes
the power-on register sequence (`fm6124init()` resets only the six data pins plus CLK/LAT/OE).
**Neither goes near the address bus.** `hardware/BRINGUP.md` offered `FM6126A` as the first
remedy for a bad picture and that is what sent the bench at the driver IC; it now carries a
`## Diagnosing a wrong picture` section with the signature written down.

The signature, because it is unmistakable once seen: a dead address bit of weight *w* gives
**two dark bands of *w* rows** and **every lit row showing two logical rows superimposed**
(physical *j* = logical *j* + logical *j+w*). **The band width names the bit** — E is the MSB,
so *w* = 16, rows 16–31 and 48–63 dark. Fastest check is the heartbeat pixel, which the
renderer pins at (63, 63); with E dead it sits at (63, 47).

A second variable was loose at the same time and is worth recording as a process failure:
`default_envs = depthcharge` is **Anvil**, so a bare `pio run -t upload` silently swapped the
venue underneath the experiment. Three arms were flashed and compared before anyone noticed
they were Anvil builds while the symptom lived on Kraken. Kraken needs `-e depthcharge-kraken`
every time.

#### Item 1 — uninitialised rendering · **DECIDED: grey**

Not 27 rows of unknown. Grey already means "do not trust this"; the boot frame carries a stale
reason in the header and unknown rows would add nothing a person reads at desk distance.

**What it looked like:** blank for a few seconds from power-on to `Panel::begin()`, then the
grey wash with `RESYNC` in the header's value slot, then the ladder. Honest, and it reads as
*nothing has arrived yet* rather than *broken*.

**But the message could only be read at an oblique angle** — the second time this ramp has
failed at the bench for the same reason. The whole grey ramp was chosen at eight-bit depth;
this build runs six, and a 4×6 glyph at 255 against a 64 wash has no contrast left once
quantisation and the CIE1931 curve have taken their share. The 2026-08-11 fix (text to full
white, ladder inks lifted) treated the symptom and left the wash alone.

**Fixed, two changes:**

1. **`Ink::HeaderBed`, black in both palettes.** The header band was already washed flat before
   the glyphs go down; it just used `Ink::Background`. Giving it its own ink puts the text on
   black without spending the grey anywhere else. This is a **deliberate hole in "grey, never
   blank"** and is six rows wide — `none_black_except()` now permits exactly this one ink, and
   a test pins the bed inside the header band so the exception cannot widen into the ladder.
2. **Stale background 64 → 8.** Owner's call. With the text on its own black bed the wash is no
   longer holding up legibility, and at six bits 64 was simply too bright to sit beside for an
   evening. **Not zero** — it remains the entirety of invariant #5's "this panel is ON and not
   to be trusted" signal on the boot frame, and the build fails if it reaches black.

Verified on the panel: *"much better"*, then *"looks good"* at 8.

#### Item 2 — depth · **DECIDED: depth stays 25, D6 stays parked**

Counted at the panel: **25 red rows above the midpoint, 25 green below.** That is the whole
subscribed book — Kraken subscribes at depth 25, the panel draws `kLevels = 26` a side, so 25
rows fill and one row at each outer edge stays background. It reads as complete, not truncated.

So the window is a no-op at the shipped configuration, the three policies stay byte-identical,
and there is no reason to subscribe deeper than the panel can draw. **D6 does not move.**

The count also independently confirmed the E-line repair: 25 contiguous rows a side is not
possible with a dead address bit.

*(The brief says "27 rows" — `kLevels` is 26. The 27 comes from the stale row-budget comments
in `ladder_render.hpp`, which still describe the pre-4×6-font layout: four of the seven row
constants are documented wrong. Not fixed here; it is desk work and it is exactly the table a
person would use to decide "is this shifted?".)*

#### Item 4 — `clkphase` ghosting re-check · **CLOSED**

Looked at with the panel honest and the address bus repaired: **no ghosting at
`clkphase = false`.** The setting that shipped on 2026-08-15 is correct and the header-glyph
artefact is gone. Worth noting the 2026-08-15 decision was taken on throughput evidence alone
(msg/s, KB/s, connects, sock_gaps — three arms, no photograph); this is the first time anyone
has actually looked at the picture it produces.

#### Not done

- **Item 3 — D7's scope trace.** No trace, no numbers. The brownout investigation is what
  disturbed the ribbon; it did not produce a capture. Still owed.
- **Item 4 · D1** — rejoin re-rolls the mesh lottery. Needs the far node.
- **Item 4 · D2** — weak-node one-hour soak. Not started.

#### State of the tree

Host suite **33/33 green**. Kraken image flashed with the black header bed; the grey = 8 change
is built and green but **not yet flashed**. Uncommitted: `hardware/BRINGUP.md`,
`firmware/src/ladder_render.hpp`, `harness/tests/test_ladder_render.cpp`, and this brief.

The 48-hour soak capture started 08:50 was killed at 15:28 to free COM7 (6.5 h, 8.45 MB, at
`firmware/logs/kraken-b3-soak2-20260824.log`, which is gitignored). Nothing in the repo ever
asked for 48 hours; the only unattended run M4 owes is D2's **one** hour.

#### M4 IS CLOSED

Grey = 8 is flashed and confirmed on the panel. With items 1 and 2 decided, every clause of
"Closing M4" above is evidenced: the panel renders a Kraken book off the wire, greys within the
calibrated liveness threshold, holds colour through 26 s of legitimate book silence, heals from
a checksum failure, and shows a book age that is a lag estimate. Ticked in `ROADMAP.md`;
**M5 (Binance) is now Next**. The black header bed is recorded in `ARCHITECTURE.md` §9 as a
named, bounded exception to invariant #5 rather than living only here.

#### Carried forward — owed, and none of it blocked this tick

- **Item 3 — D7's scope trace.** No capture. It answers whether power sequencing is a
  requirement at M7, not anything M4 claims.
- **D1** — rejoin re-rolls the mesh lottery. Needs the far node (a Deco sibling at −7x dBm;
  the term enters the project at `hardware/bench-2026-08-09-ws-reconnect.md`).
- **D2** — weak-node **one**-hour soak, the M3 transport brief's last unticked bar. Needs the
  board associated at −7x dBm, not a second device. Bar is *socket survives every fade the
  watchdog reports*; greys allowed, deaths are not.
- **The stale row-budget comments** in `ladder_render.hpp` — four of seven row constants are
  documented wrong, describing the pre-4×6-font layout. This evening walked straight into that
  trap: it is the table a person reads to decide "is the panel shifted?".
