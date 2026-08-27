# M5 Stage D-A1 — the footprint, and the venue build

**Track:** Mixed [desk, ending in one flash] · **Status:** **Desk work done 2026-08-27 (deliverables 2, 3, 5, 6; 1 void, 4 not needed); deliverable 7 — the flash — outstanding.** · **Size:** one evening
**Executor:** Claude Code. **No panel judgement** — what the board *renders* is D-B's, and this stage
must not decide it.

The Binance adapter is **100,824 B of `.bss`** against Kraken's 16,744, and `FeedTask g_feed` is
namespace-scope, so it is claimed before the heap exists. Projected forward the Binance build
reaches `choose_depth` with a budget of **0** and prints *"no colour depth fits in 0 B — running
WITHOUT a panel."* This evening buys the room back, in the order that spends the least, and ends
with a board that boots **double-buffered**, connects to the stream, and draws an honestly grey
ladder — because remedy (a) withholds the Snapshot until a diff brackets it, and no REST client
exists yet.

**The acceptance is the double-buffer floor, not Kraken's rung.** `choose_depth` walks two ladders
and `panel.cpp` argues the discontinuity itself: *"lower the colour depth before giving up the
second buffer — tearing on a book that redraws 13 times a second is a visible defect on a panel
whose whole job is to be believed."*

**Read first**

| Source | Why |
| --- | --- |
| `docs/briefs/M5-stage-D-the-shape-and-three-decisions.md` §1 | The five levers, their order, and the arithmetic that rules the arrays out as the primary one. |
| `M5-stage-C-…md` § *Owed by stage D* | The six-row inheritance. Items 1 and 5 are **not** this stage's. |
| `firmware/src/heap_probe.cpp` / `.hpp` | The rule deliverable 2 amends, and the `free_total` line deliverable 2 prints. |
| `firmware/src/panel.hpp` / `.cpp` | `kReserveInternalBytes` = 98,304, `kMinDoubleBufferedDepth` = 3, `panel_cost_bytes`, and the two-ladder fallback. |
| `firmware/src/venue_build.hpp`, `platformio.ini` `[env:depthcharge-kraken]` | The shape deliverable 6 copies. |
| `docs/DESIGN.html` strain 27 | Why FramePipe is **priced and not moved** in deliverable 3. |

**Depends on:** C ✅, the corrections at `f719f89`. **Blocks:** D-A2, D-B, D-C.

---

## Deliverables

### 1 · The four WS buffers to PSRAM — the free lever, and the one that proves the mechanism

**+16,384 B, no code.** `malloc_alwaysinternal_limit` is 4096 on this build, so anything ≥ 4,097 B
already goes PSRAM-first. Measured and left unbuilt since 2026-08-11.

**Do it first and do it on the Anvil build**, where nothing else changes: flash, read
`dma-internal free` against the recorded **179,300**, and confirm the delta. A lever whose effect is
only ever observed alongside three others is a lever nobody has measured.

### 2 · `buf_lvl_` to PSRAM — and the rule amended in the same commit

**+32,768 B.** The pre-seed and re-seed buffer: written once, drained once, never DMA'd, and not the
per-diff path. **`bids_`, `asks_` and `frame_` stay internal** — they are touched on every diff at
100 ms, that objection is correct, and this stage does not test it.

Two things in the same commit, because a rule stepped around silently is worse than a rule changed:

- **Amend `heap_probe.cpp`'s comment** in the M4-stage-D correction voice. Its three grounds —
  latency, DMA, and the internal SRAM stage D spends on the panel — are about the steady-state feed
  path and do not reach a one-shot buffer. **ARCHITECTURE §5 already says *"on target the window
  lives in internal SRAM, the tail in PSRAM"***; say that this is the tail.
- **Print `HeapSample::free_total`.** It already samples the PSRAM-inclusive figure and is never
  shown, so this is **one format specifier**, not a new capability — and without it the soak cannot
  see the one allocation D-A2 will add.

### 3 · Price FramePipe. Do not move it, do not shrink it

4 × 16 KiB = **65,536 B**, the only lever large enough to matter alone — and **DESIGN strain 27 says
its slot count is already wrong in the other direction**: B3 measured `no_slot = 1,594` with heal
clustering at 8.36× the Poisson baseline. **Reducing slots is contraindicated.** Moving its buffers
is a different question, and it is a panel-latency question, which is D-B's.

Record what moving would cost and what it would risk. **Build nothing here.** If deliverables 1–2
clear the floor, this stays a priced option.

### 4 · The arrays — only for the residual, and only if there is one

Steps 1–2 put `.bss` at **68,056** and the budget at **~35,488** against d3-double's **32,000** —
the floor clears by **3,488 B**. Thin. If the measured board disagrees, trim the arrays **by the
deficit**, and record the deficit as the reason.

**Do not re-derive the bounds from the sweep.** The ladder's 500 comes from the two witnesses whose
5× disagreement is B1's stated reason for not fitting that bound, and sizing to the worst observed
is the margin-of-1.000× failure stage C found in the multiplier. If a trim is needed, state the new
margin over the measured worst explicitly.

### 5 · A compile-time budget, so the floor is not a boot-time surprise

3,488 B of headroom means **one more field in the adapter removes the panel**, discovered at boot
through a log line nobody is watching. Pin the input you control:

```
inline constexpr std::size_t kVenueInternalBudgetBytes = <derived>;
static_assert(internal_resident_bytes<BinanceAdapter>() <= kVenueInternalBudgetBytes, ...);
```

Derive the constant **beside the assertion**, from the measured free-internal, `kReserveInternalBytes`
and `panel_cost_bytes(kMinDoubleBufferedDepth, true)`, and say in the comment that firing it is a
**decision** and not a licence to raise the number. This is the `sizeof(DisplaySnapshot) == 1168`
pattern, and it exists for the same reason.

*The floor itself cannot be a `static_assert` — the budget depends on runtime free heap. Asserting
the adapter's internal footprint is the part that is knowable at compile time, and it is the part
that regresses.*

### 6 · The venue build

`DC_VENUE=3`, the `#error` guard at `venue_build.hpp:41` widened, a `DC_VENUE_BINANCE` row, and
`[env:depthcharge-binance]` modelled on the Kraken env. **`kHasSubscription = false`** — Binance puts
the stream in the URL, so this venue sits where Anvil does and the existing
`DC_VENUE_HAS_SUBSCRIPTION` branches already cover it.

**Two hosts:** `data-stream.binance.vision` for the stream, `data-api.binance.vision` for the seed
D-A2 will fetch. Both under `binance.vision`, so **whether that is one anchor or two is a
measurement** — M4's precedent is to take it off the live server twice, and the same precedent says
prefer the longer-lived self-signed root when two chains present.

### 7 · Flash, and one short capture

The board boots, `choose_depth` reports **double-buffered**, the socket connects, pings are answered,
and the ladder is **grey** with ~~`seeds_unconfirmed` counting~~ **the `-- seed` line showing diffs
held and no bracket** (`buffered=` climbing, `bracket ok=0 FAIL=0`). Print the depth chosen, the
budget it was chosen from, and both heap figures on every connect.

> **CORRECTED 2026-08-27 at the bench, and the error was this brief's own.**
> `seeds_unconfirmed` increments in exactly one place — `binance_adapter.hpp`'s `drop_book`, under
> `if (had_book && !bracket_checked_)`, where `had_book` is `seed_ == SeedState::Seeded`. A build
> with **no REST client** never reaches `Seeded`, so the counter is **structurally zero**, not
> incidentally so. **This brief's own *Out of scope* removes the REST client**, so the DoD asked
> for a reading its own scope had already made unreachable — the same clause-belongs-to-no-stage
> failure §2 of the shape brief diagnosed in M5's DoD, arriving inside the stage that quoted it.
> The board behaved correctly throughout.

**That grey is remedy (a) demonstrated on hardware** — the first time invariant #5's forbidden output
has been prevented on a panel rather than in a test. Record it as an observation. **Do not judge how
it looks.**

### 8 · Writeback

Session log with decisions and why; `ROADMAP.md`'s M5 row; `NOTES-binance.md` for every measured
figure with its provenance; `ARCHITECTURE.md` §9 for the PSRAM precedent, which is this project's
first; `docs/DESIGN.html` where a card moves.

---

## Constraints

- **No panel judgement.** Every *what does it look like* question is D-B's. If the work seems to need
  one, that is the signal the split was drawn wrong — raise it.
- **§6 frozen, §4 does not move.** No new `GapReason`, no new `FeedEvent::Kind`. **Seventh asking.**
- **No REST client.** The seed and the re-seed mechanism are D-A2's.
- **Levers in order, one commit each**, so each is attributable. A lever whose effect is only visible
  in a batch has not been measured.
- **Nothing moves a pin or a golden.** `sizeof(DisplaySnapshot)` stays 1,168, verified on both.
- **Per-commit verification in a fresh detached worktree**, `CMAKE_HOME_DIRECTORY` read from the
  cache before the pass is believed, loop inline, `host-mingw` presets.
- **Push to `m5/stage-d-a1`. Fast-forward `master` only once the ladder closes.**
- **Commit only when asked.**

## Known unknowns — resolve and record

Whether the board's real free-internal matches the projection, and by how much. Whether the two
hosts share a TLS anchor. Whether steps 1–2 clear the floor on hardware or a trim is needed.
What the PSRAM latency cost actually is on the paths that now touch it — **measure it, since the rule
being amended names latency first.**

## Definition of done

- ☒ ~~WS buffers in PSRAM, delta measured **on the Anvil build** against 179,300.~~ **VOID — the
      buffers do not exist.** Closed by static evidence instead: `esp_websocket_client` and its spare
      handle went on 2026-08-16, the owned transport's buffers are `.bss`, and the 16 KB was already
      inside the baseline. Replaced by the `kReserveInternalBytes` 96→80 KiB lever, which returns the
      same 16,384 B from the same obsolete sentence.
- ☒ `buf_lvl_` in PSRAM; `heap_probe.cpp`'s rule amended in the same commit; `free_total` printed.
- ☒ FramePipe **priced, not built**, with strain 27's counter-argument recorded — and the pricing
      found `kFrameCapacity` is 0.57× this venue's largest message (28,639 B against a 16,384 B slot;
      13 of 3,119 over). See `NOTES-binance.md` §4.
- ☒ Arrays **not** trimmed — no board deficit exists to size a trim by, and the reserve lever
      cleared the floor. `bids_`, `asks_` and `frame_` are untouched.
- ☒ `kVenueInternalBudgetBytes` + `static_assert`, derivation beside it (`venue_budget.hpp`),
      mutation-verified: fires for Binance, not for Kraken.
- ☒ `[env:depthcharge-binance]` builds; **all seven** environments still build;
      `sizeof(DisplaySnapshot)` unmoved at 1,168 (its `static_assert` compiles into every image).
- ☒ **Flashed 2026-08-27: double-buffered, connected, ladder grey.** Depth, budget and both
      heap figures printed on connect. Anvil control moved `budget=` by **exactly 16,384**.
      ~~`seeds_unconfirmed` counting~~ — **this clause was unreachable and the brief is what was
      wrong, not the board**; see the correction in the session log. The evidence for remedy (a)
      is the `-- seed` line instead.
- ☒ ctest green from a clean configure (50/50); session log · ROADMAP · NOTES · §9 · DESIGN done;
      split proposed in the log; **nothing committed.**

## Out of scope

The REST client, the seed, the re-seed mechanism and `DisplaySnapshot::reseed` → `InFlight` (**D-A2**).
Every rendering decision — the silent feed, the re-seed in flight, whether the grey reads right,
strain 24's unvalidated rows (**D-B**). The soak and its instrumentation (**D-A2** designs it, **D-C**
runs it). The multiplier falsifier (**D-C**). M4's residues and card 30. The close-out's carried
items — card 29, strain 29's wording, the `CLAUDE.md` line, the client-ping rehoming.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->

### 2026-08-27 · Claude Opus 5 (1M) · desk

**Done.** Deliverables 2, 3, 5 and 6 complete; deliverable 1 **void**; deliverable 4 **not needed**;
deliverable 7 (the flash) not started — bench step, wants the owner at the board. Host suite 50/50
green from a clean configure; all seven PlatformIO environments build. **Nothing committed** (the
brief says commit only when asked). Proposed split at the end of this entry.

**The finding that reshaped the stage, found before anything was built.** Deliverable 1 —
*"+16,384 B, no code"* — reclaims **0 B**. Its source (`bench-2026-08-11:358`) measures *"the four
WebSocket buffers (rx+tx × two handles) `malloc`'d at `esp_websocket_client_init`"*, and every noun
in that clause was deleted on 2026-08-16 with the owned transport; `ws_transport.hpp:243-252`
records the spare handle going and its ~10 KiB with it. The surviving buffers are `rx_buf_[4096]`
and `upgrade_hdr_[2048]`, **`.bss` members** of a namespace-scope object, and
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` steers `malloc` only. **The 16 KB was already reclaimed and is
already inside the 177,040 baseline** — the stage's arithmetic double-counted it, turning a
+3,488 B clearance into a **12,920 B deficit**, i.e. a single-buffered board. *Why it was catchable
on the desk:* the brief's own instruction to measure lever 1 alone on the Anvil build would have
caught it at the flash; reading the transport caught it for free.

**Decision — the replacement lever is `kReserveInternalBytes`, 96 KiB → 80 KiB (owner-approved).**
*Why this and not the arrays:* it is the **same obsolete sentence**. `panel.hpp` sizes the 96 KiB to
cover *"two esp_websocket_client handles' buffers (~10 KiB each — the spare-handle reconnect)"* —
one dead premise, two numbers derived from it, and this one is recoverable. Its guard (*"do NOT
lower this without a run that shows the steady-state figure improving for some other reason"*) is
**satisfied, not waived**: the owned transport is that other reason and the 2026-08-24 Kraken soak
is the run — worst-ever draw **62,140 B through `connects=4`**, leaving 36,164 B untouched; 80 KiB
keeps 19,780 B over the measured worst case. Caveat recorded beside the constant: the two figures
come from masks differing in two bits, so 62,140 is an estimate with a known soft edge — immaterial
at a 32% margin, and deliverable 7 confirms it (the Anvil `budget=` line must move by exactly
16,384 and nothing else). *Why not the arrays:* no board deficit exists to size a trim by, and the
shape brief's own table says re-fitting them cannot both restore a rung and keep headroom.

**Decisions with why, the rest.**

- *`buf_lvl_` via `std::unique_ptr` + `new (std::nothrow)`, not `heap_caps_malloc`.* Keeps `engine/`
  ESP-IDF-free (invariant #1) while still landing in PSRAM, because ≥ 4,097 B is tried there first
  on this build. `nothrow` because the ctor is `noexcept` and the target compiles `-fno-exceptions`;
  a null buffer routes into the **existing** `Gap{Overflow}` re-seed path rather than needing a new
  `GapReason` (§6 stays frozen — eighth asking).
- *Binance's `liveness_count` returns a constant 0.* This venue's clock is the **server ping**, a
  control frame answered in `WsTransport::on_ping` on the RX task; it never becomes a message, so it
  never reaches the differencing site in `feed_task.cpp`. `frames_in` would have compiled and
  stamped the watchdog with a market event dressed as a clock. **A quantity this build cannot
  measure is reported as absent, not approximated.** Consequence stated at the constant: the
  watchdog is never armed on this build, so the grey comes from remedy (a) and from the transport's
  own silence recycle, not from the liveness path. **Wiring the ping to the watchdog is owed before
  D-C can test *Owed by stage D* item 5's parity claim** — it crosses the RX/feed task boundary
  invariant #8 governs and changes `liveness_count`'s signature for all three venues, so it was
  raised rather than improvised under a stage whose acceptance does not reach it.
- *`DC_VENUE_HAS_WIRE_SEQ` added.* `last_wire_seq()` guarded on `DC_VENUE_HAS_SUBSCRIPTION` — one
  macro answering two questions that coincided only while there were two venues. Binance broke it
  loudly. §9 row written, because the twin case (a third venue *with* a subscription) would not fail
  loudly at all.
- *`kParseStatusCount` is 7 at this venue*, not 6: Binance has `TooManyLevels`. Six would not
  overflow — `reject_log.hpp` bounds-checks the slot — it would do the quieter thing that file's
  own comment warns about and stop the tally adding up.
- *One TLS anchor, and the finding is stronger than "shared root": a shared **leaf**.* Both hosts
  serve the identical wildcard certificate (`*.binance.vision`, same SHA-1). Pinned the self-signed
  Amazon Root CA 1 (expires 2038-01-17) over the two 2037-12-31 alternatives, per M4's precedent,
  and **verified** both hosts against it alone with `-verify_return_error`.

**The finding deliverable 3 was not looking for.** `kFrameCapacity` = 16,384 B is **0.57×** this
venue's largest observed message on the board's own stream (28,639 B; 13 of 3,119 over, 0.417%),
against the 1.9× margin it was sized to at Anvil. Each overflow is a defined drop → the next diff
fails `U == last_u + 1` → `Gap{SeqGap}` → grey + re-seed. Honest, but recurring, and it cannot
simply be raised: 4 × 32 KiB is +65,536 B against a 35,464 B budget. Fewer slots is contraindicated
by strain 27. **Moving FramePipe to PSRAM is the only lever that relaxes both** — which strengthens
the option this deliverable was told only to price, and still does not decide it (per-message
latency, therefore D-B). Built nothing.

**Numbers this sitting established.** `sizeof(BinanceAdapter)` 100,824 → 68,064 host / 68,060
xtensa. Static RAM: Anvil 146,560, Kraken 154,888, Binance 206,216 — the `.bss`-delta model
validated against the target linker to within 4 B. Anvil free-internal is **176,828–177,236** (four
runs, 2026-08-24), **not** the brief's 179,300 (one sample, 2026-08-20, higher than all four).
Projected Binance budget **35,464 B** against d3-double's 32,000 — clears by **3,464 B**.

**Proposed split — five commits, none created.** The ladder has not been run, because nothing is
committed yet.

1. `engine: the pre-seed buffer is the tail, not the window` — `buf_lvl_` to the heap;
   `heap_probe.cpp`'s rule amended and `free_total` printed in the same commit.
2. `firmware: the reserve is sized for a client that no longer exists` — 96 → 80 KiB, with the soak
   measurement beside it.
3. `firmware: a compile-time budget for the venue's internal footprint` — `venue_budget.hpp`.
4. `firmware: the Binance venue build` — `DC_VENUE=3`, the two headers, the env, and the three holes
   the third venue exposed (`DC_VENUE_HAS_WIRE_SEQ`, `kParseStatusCount`, the `render_task` arm).
5. `docs: what D-A1 measured, and the lever that was not there` — NOTES §D-A1, three §9 rows,
   DESIGN, ROADMAP, this log.

**Exact next step.** Owner: approve or amend the split; then the ladder runs (detached worktree per
commit, `CMAKE_HOME_DIRECTORY` confirmed against the worktree before any pass is believed, loop
inline, `host-mingw` presets) and it pushes to `m5/stage-d-a1`, with `master` fast-forwarded only
once the ladder closes. Then **deliverable 7 at the bench**: flash `-e depthcharge-binance` and read
three things in order — (a) on the **Anvil** build, `budget=` moved by exactly 16,384 and nothing
else did, which is the reserve change's confirmation and the one thing that would falsify the mask
caveat; (b) `choose_depth` reports **double-buffered** at d3 with `budget=` near 35,464; (c) the
ladder is **grey** with `seeds_unconfirmed` counting and `-- seed` reading `bracket ok=0 FAIL=0`.
**Do not judge how the grey looks — that is D-B's.**

### 2026-08-27 (later) · Claude Opus 5 (1M) · **bench — deliverable 7 done, split approved**

**Both readings pass. The falsifier cleared exactly.** Owner approved the five-commit split on
these numbers; the ladder and the push to `m5/stage-d-a1` follow this entry.

**Reading 1 — the Anvil control. PASS, and the falsifier is exact.**

```
dma-internal free=176804 largest=172020 | reserve=81920 budget=94884
UP: 64x64 depth=6 double-buffered | predicted=78080 measured=77488 B | free 176804 -> 99316
```

`176,804 − 81,920 = 94,884`. At the old reserve the same free gives `176,804 − 98,304 = 78,500`,
so the budget moved by **16,384 exactly**. Depth unchanged at 6 double-buffered, as predicted —
Anvil already sat on `kMaxColourDepth`, which is what makes it a clean control. *(Against the
08-24 log's `budget=78,744` the raw delta reads 16,140, because `free` itself drifted 244 B
between runs. The same-run subtraction is the valid comparison.)*

**Reading 2 — the Binance acceptance. PASS.**

```
dma-internal free=117548 largest=114676 | reserve=81920 budget=35628
UP: 64x64 depth=3 double-buffered | predicted=32000 measured=31816 B | free 117548 -> 85732
```

Free came in at 117,548 against the desk's projected 117,392 — **out by 156 B**, so the
`.bss`-delta model held on the real board as well as against the linker. Budget 35,628 clears
d3's 32,000 by 3,628. Ladder grey and demonstrably not frozen: `live=0`, `rows=0/54 unknown=54`,
`grey_n=1 grey_ms=69,609` across the full 70 s uptime, `drawn=61`. All error counters zero;
`-- pipe: oversize=0 no_slot=0` over 580 frames.

**Correction — `seeds_unconfirmed`, and it was a BRIEF error, not a board one.** The DoD asked
for it to be counting. It read 0, and it cannot read anything else here: it increments only in
`drop_book` under `had_book && !bracket_checked_`, and `had_book` means `seed_ ==
SeedState::Seeded`. **This brief's own *Out of scope* removes the REST client**, so the adapter
never becomes `Seeded` and there is no withheld seed to count. **A DoD criterion made unreachable
by the same document's scope section** — which is precisely the failure the shape brief's §2
diagnosed in M5's own DoD (*"as written this one belongs to none"*), reproduced inside the stage
that cited it. Deliverable 7 and the DoD box are corrected at source above.

**What actually evidences remedy (a), and it is better evidence than the counter would have
been:**

```
-- seed : buffered=572 dropped=0 overflow=8 | bracket ok=0 FAIL=0 unconfirmed=0 reseeds=8
```

Diffs held rather than applied, the pre-seed buffer recycling through `Gap{Overflow}` because no
snapshot ever arrives to drain it, and a re-seed asked for eight times with nothing to service
it. **The panel stayed grey for 70 s over a healthy socket carrying 580 real depth frames** —
which is invariant #5's forbidden output prevented on hardware, and it is the *held* frames that
show it, not an unfilled counter.

**The reserve cut is not free on Binance, and it now owes the soak a named check.** On Anvil it
is pure arithmetic — d6 was already chosen, so no memory moved. On Binance it buys the second
buffer (15,816 B) out of the network stack's heap: steady state is **free 32,244 / largest block
17,396**, the tightest this project has run at. mbedTLS needs **two contiguous 16,717 B blocks
per session**, so the margin is **679 B**, observed at `connects=1` — the reconnect case was
never exercised. Recorded as a threshold beside `kReserveInternalBytes` rather than as a
watch-list line, because B3 measured the largest block moving as a **sawtooth** over 25.39 h, so
an end-of-run reading would say nothing and a mid-run dip is the event:

> **D-C CHECK:** record the largest free internal block **at every reconnect**. If it ever falls
> below **16,717 B**, the reserve cut is wrong at the second socket and `kReserveInternalBytes`
> goes back to 96 KiB.

Mitigation stated so the check reads correctly rather than alarmingly: at socket-down the old
context frees first and the hole grows (2026-08-11 saw free jump +54,720), so an ordinary
reconnect allocates from a much larger hole. The case it guards is the half-open socket whose
context is still held while the new one is built — B3 saw two of those in 25.39 h.

**A bench condition, now in `hardware/BRINGUP.md`.** The configured `upload_speed = 921600`
failed **mid-write, after the erase** — *"Failed to communicate with the flash chip"*, then
*"Serial data stream stopped"*, leaving the bootloader rewritten and the 881 KB application
region erased and unpopulated. A board in that state does not boot and reads exactly like a
firmware fault, which is why it belongs in the file that already carries the dead-E-line
signature: same shape, a hardware-side cause wearing a firmware-side symptom. **115200 is clean**
— both images, `Hash of data verified`, ~20 s each. Left as a `PLATFORMIO_UPLOAD_SPEED` override
rather than lowering the committed constant, because the failure is **not root-caused** and may
be this cable or bridge rather than the speed.

**Not observed, and worth saying so:** `oversize=0` over 580 frames in 70 s. The desk predicted
~0.417% on this stream, i.e. ~2.4 expected in that window, so a zero is unsurprising but this
capture **neither confirms nor refutes** the `kFrameCapacity` finding. It needs the soak.

**Provenance.** Both flashes were taken from the **uncommitted working tree** at the state that
became the five commits below; logs `firmware/logs/device-monitor-260827-225015.log` (Anvil) and
`…-225151.log` (Binance). One `AUTH_FAIL` (reason 202) during association, recovered on retry,
`connects=1` thereafter. The board is left running the Binance image.

**Exact next step.** Ladder closed and pushed to `m5/stage-d-a1` — `master` is fast-forwarded only
by the owner. Then **D-A2**: the REST client and the seed, which is also what makes
`seeds_unconfirmed` a meaningful reading for the first time.
