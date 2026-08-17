# M4 Stage 0 — price the Kraken wire

**Track:** Agentic [A] · **Status:** ✅ Done (2026-08-16) · **Size:** one evening
**Executor:** Claude Code, **desk only. No board, no flash, no `engine/` change, no adapter.**

**This is M4's stage 0, not M4.** It exists because M3's own closing rule says so
(ARCHITECTURE §9, 2026-08-16 eve): *a venue's byte rate is not a property of the venue — it
is a property of what you asked it for; price the stream against what the object renders
before sizing anything against it.* Kraken's book subscription takes a `depth`, so that
parameter must be fixed on evidence **before** the adapter exists, not chosen inside it.
Lands as `docs/briefs/M4-stage-0-price-the-kraken-wire.md`.

**Read first**

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §4 | The boundary contract. Kraken is named in it three times already — no seq, CRC failure becomes `Gap{ChecksumFail}`, and the declare-and-verify rule for venues that publish no tick metadata. |
| `ARCHITECTURE.md` §5 | The dense-window target design, and the phase-1 form M4 replaces. |
| `ARCHITECTURE.md` §6 | Frozen. #1, #2, #3 and #7 are all load-bearing here. |
| `ARCHITECTURE.md` §9, 2026-08-16 (eve) and 2026-08-11 | The two rules this stage applies: size what you render; measure freshness, not cadence. |
| `docs/DESIGN.html` strains 3, 5, 19, 20 | Strain 3 is deliverable 6. Strain 19 is the path this measurement is compared against. Strain 20 is the DoD rule M4 inherits. |
| `docs/briefs/M0-trace-and-harness.md` | **The precedent. This stage is M0 repeated for a second venue** — capture tool, traces, observations doc, nothing else. Follow its shape. |
| `tools/capture_anvil.py` | The client to reuse or clone; its `--depth` header comment records the tier-rounding lesson learned the hard way. |
| `tools/anvil_frame_economics.py` | The pricing tool, and its `--verify` mode — the reason its numbers are measurements rather than estimates. |
| `tools/slice_trace.py` | The commit policy: full capture stays local and untracked, a representative slice is committed. Its header already says "reused for Kraken/Binance traces from M4". |
| `harness/replay/NOTES.md` | The shape of the observations doc Kraken now needs its own copy of. |

**Depends on:** M3 ✅. Nothing pending in the tree.

---

## What is already believed about the wire, and none of it is measured

Read from Kraken's WebSocket v2 documentation on 2026-08-16. **Every line below is a
hypothesis to confirm against the capture** — this stage exists because this project has
five times mistaken a nearby measurement for the one that mattered, and a vendor doc is not
even a measurement.

- Public endpoint `wss://ws.kraken.com/v2`; no auth; TLS **with SNI required** (Cloudflare
  fronts it, and a missing SNI is a 403 rather than a handshake error).
- Subscribe is a client→server frame:
  `{"method":"subscribe","params":{"channel":"book","symbol":["BTC/USD"],"depth":N,"snapshot":true}}`
- **Depth tiers are 10, 25, 100, 500, 1000**, one depth per symbol per connection.
- `type:"snapshot"` then `type:"update"`; `qty: 0` removes a level.
- The CRC32 checksum covers the **top 10 levels only, regardless of subscribed depth**, asks
  ascending then bids descending, decimal points removed and leading zeros stripped.
- After each update the client must **truncate to the subscribed depth itself** — Kraken does
  not send `qty: 0` for levels that fall out of scope.
- v2 puts price and qty on the wire at **full precision as JSON numbers**, not as quoted
  strings the way v1 did. Kraken's own guidance is to decode them with a decimal or string
  decoder.

### Two of those already change the design, and they are why this is a stage

**1. There is no 27 tier.** `kDisplayLevels` is 27; the choice is 25 (render one fewer level a
side than Anvil does) or 100 (subscribe four times the depth and discard 73). That is a
*rendering* decision wearing a transport decision's clothes, and it is the owner's, not CC's.

**2. Invariant #3 meets a wire that speaks floating point.** No float may ever touch book
data, and the CRC32 needs the original decimal text, not a reconstructed one. The streaming
parser must scale to integer ticks **from the token text**, and the checksum must be computed
from the bytes as sent. Confirm from the *verbatim* capture text — not from anything that has
been through `json.loads` — whether the numbers are quoted or bare.

### And the thing most likely to bite M4, which this capture measures for free

**Anvil broadcasts on a timer; Kraken publishes on change.** `kRxWatchdogMs` is 1000 ms,
calibrated twice against Anvil's measured 391 ms worst healthy gap. On a quiet Kraken pair a
silence of several seconds is *correct behaviour*, and invariant #5 would grey a panel showing
a perfectly fresh book. That is a real defect waiting at M4 and it costs nothing to size now.
**Capture the inter-message gap distribution and report it. Do not change the constant.**

---

## Deliverables

### 1. `tools/capture_kraken.py`

Same NDJSON contract as `capture_anvil.py`: line 1 metadata, then
`{"rx_ns": <monotonic-ns>, "frame": <verbatim JSON>}`, frames spliced in **verbatim** so key
order and number formatting are byte-preserved. Stdlib only — the box has no `pip`.

Reuse the RFC 6455 client rather than rewriting it: extract it to `tools/wsclient.py` **only
if `capture_anvil.py`'s output stays byte-identical across the refactor** (check with a
committed trace). If it does not, clone the client into the new tool and say so in its header
with the reason. Do not silently diverge two copies.

**One genuinely new capability:** this tool must *send* an application text frame — the
subscribe — where `capture_anvil.py` is a pure consumer. Masking already exists for the close
frame; extend it, and record the subscribe *and* the subscription ack in the trace like any
other frame.

Arguments: `--symbol`, `--depth`, `--duration`, `--out`, `--cycles`, `--max-frames`.

### 2. The captures

Four, 90 s each, full captures kept local and untracked:

| symbol | depth | asks |
| --- | --- | --- |
| a liquid pair (`BTC/USD`) | 25 | the candidate |
| a liquid pair (`BTC/USD`) | 100 | the control, and the truncation cost |
| a liquid pair (`BTC/USD`) | 10 | the floor, and the checksum tier |
| **a deliberately quiet pair** | 25 | the silence question above |

The quiet pair is not padding. It is the only one of the four that can answer the watchdog
question, and it is the cheapest thing in this brief.

**Cloudflare rate-limits connection attempts (~150 per rolling 10 minutes per IP, ban on
breach).** Do not loop reconnects. One connect per capture; if a capture fails, wait before
retrying. A ban costs the evening.

### 3. Price them

Generalise `anvil_frame_economics.py` to price any capture in this format, or clone it — same
rule and same reason as deliverable 1. Keep its `--verify` discipline: re-serialisation must
be byte-exact, or the byte counts are estimates and must be labelled as such.

Per message type, per capture: count, total bytes, mean, share of wire, sustained KiB/s,
levels per message, and **levels changed per message** — that last column is what made A7
obvious and it is the one a rate-only measurement cannot produce.

Then the comparison that decides the depth: each candidate against **30.8 KiB/s** (what the
board draws from Anvil today at `depth=27`) and against **56 KiB/s** (the 23.6 h soak's worst
measured hour). State the headroom as a multiple, the way A7 was stated.

Report inter-message gaps as a distribution with the worst gap named, for all four captures.

### 4. `harness/replay/NOTES-kraken.md`

M0's observations doc, for Kraken. Message kinds actually seen; field shapes; whether prices
arrive quoted or bare **in the verbatim text**; what a `snapshot` carries that an `update` does
not (does the snapshot carry a checksum? v1 did not; v2's docs read as though it does — settle
it); heartbeat/status channel behaviour; what a reconnect looks like; the inferred tick size
and qty step and whether either is discoverable from the WS stream alone or needs the REST
`AssetPairs` call; and every place the wire disagrees with the doc summary above.

### 5. Committed slices

One slice per depth under `harness/replay/`, via `slice_trace.py` (extend it if its
Anvil-shaped `type` sniffing cannot find a Kraken resync — do not fake one). `dc_replay` will
not read these: **record that plainly and say why** rather than reshaping the trace to suit a
reader that has not been written yet. No adapter this evening.

### 6. The two decisions, written up and NOT taken

Follow the Stage E precedent: propose, do not merge.

**a. The depth decision.** Arithmetic from deliverable 3, the 25-versus-27 rendering
consequence stated in rows and pixels, a recommendation, and the note that changing
`kDisplayLevels` touches §5.

**b. Strain 3 — concept or convention — and settle its toolchain half by compiling, not by
arguing.** Put a throwaway C++20 `concept` through `dc_engine_target_check` under the current
Arduino 6.5.0 / xtensa GCC 8.4 toolchain and **record what the compiler says**. Then write the
three options with that result attached: (i) pin the adapter shape with a `concept`, which
carries whatever toolchain move the compiler just proved is required; (ii) pin it with
`static_assert`s on the sink signature, weaker but toolchain-neutral; (iii) leave it
convention and accept two adapters drifting. Recommend, do not implement. **If (i) turns out
to mean the GCC 15.2 / pioarduino move, that is milestone-weight and does not ride in on M4's
coat-tails** — it becomes its own §9 entry and its own decision.

### 7. Writeback

Session log here; ROADMAP M4 row gains a line saying stage 0 is done and what it fixed;
DESIGN strain 3 updated with the compiler result. `docs/DESIGN.html` loses to ARCHITECTURE on
any disagreement, as always.

---

## Constraints

- **All of §6, frozen.** Nothing in `engine/`, nothing in `firmware/`, no adapter, no golden.
- Python lives in `tools/` only, stdlib only.
- `capture_anvil.py`'s output does not move. If a refactor moves it, the refactor is wrong.
- Full captures stay untracked; commit slices.
- Host build stays green (`cmake --build --preset host`, then `ctest --preset host`, from
  PowerShell — the Bash sandbox breaks compilers silently on this machine).
- **Commit only when asked.** Report what changed and what it measured.
- Do not reopen `kRxWatchdogMs`. Measure the gaps; the constant is M4's problem, not this
  evening's.

## Known unknowns — resolve and record

Whether the RFC 6455 client extracts cleanly or has to be cloned. Whether price/qty are quoted
on the wire. Whether the snapshot carries a checksum. Whether `depth: 25` actually holds 25 a
side under drift, or quietly holds fewer. Where tick size and qty step come from. What
Kraken's quiet-pair silence looks like against a 1000 ms watchdog. Whether Kraken's own
truncate-to-depth rule interacts with `kMaxSnapshotLevels` in any way §4's "depth beyond N is
unknown, not zero" does not already cover.

## Definition of done

- ☑ `tools/capture_kraken.py` exists, is stdlib-only, sends a subscribe, and writes the same
      NDJSON contract as the Anvil tool. `capture_anvil.py`'s output is byte-identical to
      before.
- ☑ Four captures taken; three depth slices committed under `harness/replay/`.
- ☑ Each capture priced: bytes, share, KiB/s, levels/message, **levels changed/message**, and
      the inter-message gap distribution with its worst gap.
- ☑ Headroom stated as a multiple against 30.8 and 56 KiB/s, per candidate depth.
- ☑ `harness/replay/NOTES-kraken.md` written, including every place the wire disagrees with
      this brief's doc summary.
- ☑ The depth recommendation written, with the 25-versus-27 consequence stated. **Not taken.**
- ☑ The strain-3 compiler result recorded and the three options written up. **Not taken, not
      implemented.**
- ☑ Host build and ctest green; session log; ROADMAP line; DESIGN strain 3 updated.

## Out of scope

The adapter itself. Deltas, CRC32 verification code, the dense-window book, re-anchoring, the
cold tail. Any `engine/`, `firmware/` or golden change. The board — nothing is flashed this
evening. Binance (M5). `kRxWatchdogMs` and every other watchdog constant. `kDisplayLevels`.
The M4 brief, which this stage exists to make writeable.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     measured figures / evidence · exact next step. -->

### 2026-08-16 · Claude Opus 5 (1M) · stage 0 executed end to end

**Done.** All eight DoD boxes. `tools/capture_kraken.py`, `tools/wsclient.py`,
`tools/tracefile.py`, `tools/kraken_frame_economics.py`; four 90 s captures plus a
fifth reconnect capture; four committed 60 s slices; `harness/replay/NOTES-kraken.md`;
the two decisions below. Host build and ctest green, unchanged. Nothing in `engine/`,
`firmware/`, or any golden. No board, no flash, no adapter.

**Both extractions were verified rather than asserted, because the brief's rule was
conditional.** The RFC 6455 client and the capture reader are now shared, not cloned:

- `wsclient.py` — checked by serving the 1,513 frames of
  `anvil_101_baseline_20260809.ndjson` from a loopback WS server and running the
  pre- and post-refactor `capture_anvil.py` against it. With `rx_ns` and
  `captured_at` normalised (a clock and a date, the only non-reproducible fields)
  the outputs are **byte-identical over all 1,514 lines**. A live capture cannot
  demonstrate this — the frames differ every run — which is why the check replays a
  committed trace at the client instead.
- `tracefile.py` — checked by running `anvil_frame_economics.py` over all four
  committed traces, plain, `--verify`, and with two `--depth` values, before and
  after. Identical except one deliberate change: the error-path message now says
  "not a DepthCharge capture?" rather than "not an Anvil capture?", since two venues
  share the reader. No measurement output moved.

**Three things the wire said that the brief's doc summary did not.** Full detail in
`NOTES-kraken.md`; these are the ones that change M4.

1. **Kraken sends a 1 Hz `heartbeat`, and book silence is nothing like Anvil's.**
   Worst book-event gap in 90 s: **4,535 ms on BTC/USD at depth 25**, 5,058 ms at
   depth 10, **9,007 ms on the quiet pair** (whose p90 is 8,480 ms — that is its
   normal condition). Against Anvil's 391 ms worst gap, from which
   `kRxWatchdogMs = 1000` was derived at 2.6× margin. Meanwhile the heartbeat floors
   *byte* silence at 998–1,026 ms in every capture, straddling the same constant. So
   the two counting rules that agree at Anvil disagree **9×** here, and the
   byte-armed one sits exactly on the heartbeat's noise floor. **Not reopened, per
   the brief** — but M4 cannot ship the Anvil constant unexamined, and this is the
   evidence for whatever replaces it.
2. **`depth: 27` is REFUSED, not rounded up** —
   `{"error":"Subscription depth not supported","success":false}`. Anvil rounds an
   unsupported depth up and serves the next tier (A7); Kraken keeps a strict
   whitelist. Two venues, one parameter, opposite failure modes. Worse, the refusal
   is silent afterwards: `status` had already arrived and heartbeats kept coming, so
   a client that does not check `success` on the ack sits on a live, chattering
   socket with a permanently empty book — an invariant #5 trap the transport cannot
   see.
3. **Tick size and qty step ARE available over the WebSocket**, on the v2
   `instrument` channel (`price_precision`, `qty_precision`, `price_increment`,
   `qty_increment`, `qty_min`), so §4's declare-and-verify can become
   verify-against-the-venue at this venue. Two catches: that channel uses exponent
   notation (`1e-08`, `5e-05`) where the book channel never does, and REST
   `AssetPairs`'s `wsname` is the **v1** name (`XBT/USD`), not the v2 symbol
   (`BTC/USD`).

**Two measurements worth keeping even if every decision below is overruled.**

- **Invariant #3 has a number here.** Checksums reproduced from the verbatim capture
  text: **2,786 / 2,786** at depth 25 (8,677 / 8,677 across all four captures).
  Reproduced after a `json.loads` → `json.dumps` float round-trip: **0 / 2,786**.
  Not degraded — zero, in every capture. Only 1,480 of 2,878 frames survive that
  round-trip byte-for-byte at all, and 92 of them contain a quantity a float prints
  in exponent notation (`0.00005100` → `5.1e-05`), which the checksum has no
  spelling for. The M4 parser must scale to ticks *from the token text* and keep
  those digits reachable for the CRC.
- **Truncation is load-bearing, and it hides at depth.** Maintained without
  truncating, the checksum matches **379/1,412 at depth 10, 910/2,786 at depth 25,
  and 4,435/4,435 at depth 100.** A non-truncating client is *100% correct* at depth
  100 over 90 seconds and 27% correct at depth 10 — the defect's visibility scales
  with how far the market moves relative to the subscription depth, so the depth
  most likely to be developed against is the one where a short test cannot find it.
  On record only because the same window was run at three depths.

**Method note, recorded because it corrects an existing one.** `NOTES.md`'s M3
addendum says the cure for the 15.625 ms `rx_ns` grid is "capture from WSL or raise
the process timer resolution". Measured this session: `timeBeginPeriod(1)` does
**not** move `time.monotonic_ns()` on Python 3.12 / Windows 11 — still 15.0 ms.
`time.perf_counter_ns()` steps at 0.0002 ms. The clock was the lever, not the timer
period, and it matters here far more than at Anvil: the p50 book gap at depth 25 is
**0.1 ms**, so the old clock would have reported a distribution made mostly of its
own quantisation. `capture_kraken.py` uses `perf_counter_ns` and records the choice
in every trace's metadata; `capture_anvil.py` deliberately keeps `monotonic_ns` so
its traces stay comparable with the four committed ones.

**Two instrument bugs found and fixed in the same session, both of the same species
— a tool that reported a plausible number.** The truncation column first judged
which levels a 27-row ladder needs against the *pre-message* book only, silently
discarding every new best bid and undercounting the truncated feed by up to 14%; and
the `--verify` float-shadow experiment shared one `try` with the venue's own book, so
92 real checksum comparisons were skipped rather than counted. Both are noted here
rather than buried because the numbers they produced looked entirely reasonable.

---

### Decision (a) — the depth. **Proposed, not taken.**

**Recommendation: subscribe `depth: 25`, and do not change `kDisplayLevels`.**

**The byte argument does not decide this, and that is the first finding.** Every
offered depth is cheaper than what the board already runs on all day:

| subscription | sustained | vs 30.8 KiB/s (Anvil today) | vs 56 KiB/s (soak's worst hour) |
| --- | ---: | ---: | ---: |
| depth 10 | 3.05 KiB/s | **10.10×** | **18.36×** |
| **depth 25** | **6.12 KiB/s** | **5.03×** | **9.15×** |
| depth 100 | 9.91 KiB/s | **3.11×** | **5.65×** |

Depth 100 — four times the levels the panel draws — costs **a third of what Anvil
costs today**. The staleness problem that consumed M3 cannot be recreated at this
venue by any available depth choice. The honest comparison is 9.81 against 6.07
KiB/s of update traffic: **1.62×, not 4×**, because updates concentrate at the touch
(truncating the depth-100 stream to 27 a side saves only 14.8%). So this is a design
decision wearing a transport decision's clothes — exactly what the brief said it
was, only more so than expected.

**What decides it is unverified state.** The CRC32 covers the top 10 levels *only*,
at every depth. At depth 25 the client maintains 15 unverified levels a side; at
depth 100 it maintains 90, none of which the panel ever draws, all of which can be
silently wrong, and any of which can walk into the drawn window when the market
moves. Add the disguised truncation defect above — untestable at 100 in a short
trace — and depth 100 is the option that is harder to be *sure* about, not the safe
one.

**The rendering consequence, in rows and pixels.** `kDisplayLevels` is 27 and the row
budget is 5 header + 1 rule + 27 ask + 1 spread + 27 bid + 1 rule + 2 strip = 64
(`ladder_render.hpp:261-280`). A 25-deep book leaves **four rows unfilled: 6–7 and
59–60**, 4 × 64 = **256 pixels, 6.25% of the panel** — and they fall at the *outer*
ends of each ladder, farthest from the spread, because `levels[0]` is the touch and
successive levels walk away from it (`draw_side`, `ladder_render.hpp:532`).

**And it needs no code change at all.** `kLevels` is already clamped to what the
book publishes and `draw_side` already paints unfilled rows as `Ink::Background`:
`const int n = side.count < kLevels ? side.count : kLevels`. A shallower venue is a
case the renderer has handled since stage D. **So this recommendation does not touch
§5** — `kDisplayLevels` stays 27, the geometry stays, and Kraken simply fills 25 of
the 27 rows. Changing `kDisplayLevels` to 25 *would* be a §5 change, and it would
also cost Anvil two rows it currently fills; that is the option to reject.

**The real cost, stated plainly because it is the owner's to weigh.** The object
would render 27 levels on Anvil and 25 on Kraken, so M7's venue switch changes the
ladder's height by two rows a side. That is a visible inconsistency in a desk object
whose whole point is being looked at.

**The coherent alternative**, if that inconsistency is unacceptable: subscribe
`depth: 100` and truncate to 27 locally, giving venue-identical geometry for 1.62×
the bytes and still 3.11× headroom. Its price is the 90 unverified levels a side and
the truncation bug that a 90-second test cannot see. Both options are affordable;
they differ in what can go wrong quietly.

**Depth 10 is rejected outright**: it is below the panel's own rendering depth, so
the ladder would fill 10 of 27 rows, and its book gaps are the worst measured
(5,058 ms).

---

### Decision (b) — strain 3, `concept` or convention. **Proposed, not taken, not implemented.**

**The toolchain half was settled by compiling, and the answer is not the one the
brief expected.** Every command below is the exact one `dc_engine_target_check` runs
(`CMakeLists.txt:97-101`), against `xtensa-esp32s3-elf-g++ 8.4.0
(crosstool-NG esp-2021r2-patch5)`, with host `g++ 15.2.0` for contrast.

| spelling | target, `-std=c++2a` | target, `+ -fconcepts` | host, `-std=c++20` |
| --- | --- | --- | --- |
| `#include <concepts>` | **fatal: `concepts: No such file or directory`** | — | ok |
| C++20 `template <typename S> concept FeedSink = requires(…) {…};` | **`error: 'concept' does not name a type`** … `note: 'concept' only available with -fconcepts` | **ok** | ok |
| Concepts-TS `concept bool FeedSink = …` | — | ok | **`error: variable concepts are no longer supported`** |
| `static_assert` on `<type_traits>` | ok | ok | ok |

**So option (i) does NOT require the GCC 15.2 / pioarduino move.** The C++20 concept
*syntax* compiles on today's target toolchain for **one flag**. Measured cost of that
flag on the engine as it stands: all **9/9** engine headers and
`anvil_frame_streaming.cpp` compile with `-fconcepts` added, and the resulting object
file is **byte-identical** — same hash, `.text` 5,861 B either way. The flag is free.

Three conditions come with it, and they are the whole of the risk:

- **The `<concepts>` library is absent.** No `std::invocable`, `std::same_as`,
  `std::convertible_to`. Any concept must be hand-written from `requires`-expressions
  plus `<type_traits>`.
- **Only the C++20 spelling is portable.** GCC 8.4 also accepts `concept bool`, and
  GCC 15.2 rejects it — so writing to what the target's `-fconcepts` most naturally
  wants would break invariant #1's *host* half. One spelling satisfies both.
- **GCC 8.4's `-fconcepts` is the Concepts TS, not C++20 concepts.** The syntax
  overlaps; the semantics (subsumption, partial ordering, where a `requires` clause
  may appear) do not, necessarily. That is the honest residual: a concept could mean
  one thing on the desk and another on the board — precisely the failure class
  invariant #1's target half exists to catch, arriving through the very feature meant
  to make the boundary safer. Nothing here has measured that divergence; the flag was
  proven to compile, not to mean the same thing.

**The three options, with that result attached.**

**(i) Pin the adapter shape with a C++20 `concept`.** Cost: one flag in two places —
`dc_engine_target_check` in `CMakeLists.txt` *and* the PlatformIO `build_flags`, since
`engine/` headers are compiled into the firmware. Buys a diagnostic at the call site
naming the requirement, which is what makes a second adapter conform rather than
drift. Risk: the TS-versus-C++20 semantic gap above, unmeasured. **Not** milestone
weight — the brief's escape clause ("if (i) turns out to mean the GCC 15.2 move, that
is its own decision") does not trigger.

**(ii) Pin it with `static_assert`s on the sink signature.** Compiles everywhere
today, no flag, no toolchain question. A `SinkContract<Sink>` instantiated in the
adapter can assert invocability, `void` return and `noexcept` — the last of which a
concept would also have to state explicitly. Weaker in exactly one way that matters:
it fires *inside* the template, so the diagnostic names the adapter's line rather
than the caller's, and it documents nothing at the declaration a second adapter's
author would read.

**(iii) Leave it convention.** Zero cost today; the failure is two adapters that
diverge with nothing to catch it, which is what strain 3 has been warning about since
M1 and what the second adapter makes real rather than hypothetical.

**Recommendation: (ii) now, and re-open (i) at the same moment as the toolchain.**
Reasoning: the flag is free but its *semantics* are unproven, and buying a stronger
compile-time contract with an unmeasured semantic divergence between the two
compilers that must agree is the wrong trade for the one milestone that finally has
two adapters to keep honest. (ii) is available today at zero risk and captures most
of the value — the sink's shape stops being convention. If the toolchain moves for
its own reasons (pioarduino / IDF 5.x, GCC 14–15), (i) becomes free of the residual
too and should be taken then. **What would change this recommendation** is a
measurement nobody has: a concept whose subsumption behaviour is exercised on both
compilers and shown to agree. That is a cheap experiment and a fair thing for M4 to
run if it wants (i).

---

### 2026-08-16 (planning seat) · both decisions TAKEN

The owner took both decisions the same evening, from the planning seat. The two
sections above are left standing as written — they were proposals when written and the
reasoning is the part worth keeping — and this is what changed:

- **(a) Depth 25. `kDisplayLevels` stays 27. §5 untouched.** Taken as recommended,
  **with one addition to the rationale that is better than the one proposed**: the two
  unfilled rows a side are not a cosmetic cost, they are §4's *"depth beyond N is
  unknown, not zero"* **rendered**. A panel that padded 25 levels into 27 rows would
  be inventing book. So ladder height differing by venue at M7 is *correct behaviour*,
  not a wart to reconcile later — and it is cheaper to defend now than to rediscover
  at M7 when someone tries to make the two venues look alike.
- **(b) Strain 3 → option (ii).** Taken, and the owner sharpened why the compile
  result *strengthens* the case rather than weakening it: the hazard was never "it
  will not compile", it is that **both would compile and mean different things** —
  host on C++20 concepts, target on Concepts TS behind `-fconcepts`, one spelling, two
  dialects. That is a worse failure than convention **because it type-checks**, and
  the thing that would quietly stop being true is invariant #1's "same translation
  units on host and target".

Both are now `ARCHITECTURE.md` §9 rows, dated 2026-08-16 (stage 0), drafted by the
owner and transposed into the table's date · change · why shape. The second also
corrects the 2026-08-07 subset row's parenthetical about `<concepts>` — recorded
there rather than edited into it, per that table's own rule. A **third** row was added
after the pinning work below turned up a rule with three instances in this table
already: a trace containing the system's own healing events measures recovery, not the
defect.

**Also done in the same pass — the owner's process item, which was the one thing
stage 0 got wrong twice.** `tools/kraken_frame_economics.py --selfcheck` now pins the
figures this tool produced when it was reviewed, over the four committed slices, in a
`KNOWN_ANSWERS` table; `ctest` runs it (`kraken_tool_selfcheck`, optional on a Python 3
interpreter being found, exactly like `dc_engine_target_check` is optional on the
xtensa toolchain). It is **mutation-verified**: removing the truncation, keeping
leading zeros in the checksum token, and widening the CRC to the top 12 are each
caught, and the clean table passes. An unpinned trace is a **failure, not a skip** —
adding a committed trace without recording its figures would otherwise leave a green
test that measures nothing about it.

**The pin table is add-rows-only, and `--pin` enforces it rather than asking.** The
owner's reason is the load-bearing one: a new trace's figures come out of the very
tool the pins guard, so regenerating the table wholesale launders a drifted tool into
a green suite — the new rows would agree with the bug by construction and the old
rows, the only thing that could contradict it, would be gone. `--pin` refuses a trace
that already has a row, so replacing one requires deleting it first, which shows up in
a diff. The procedure is in the file above `KNOWN_ANSWERS`: **run the mutants, then
`--selfcheck`, then `--pin`, then commit** — step 2 before step 3 because figures are
only worth pinning if the tool was known-good when it produced them. That sequence was
followed for the two fields added this session: the other eight were re-checked and
passed unchanged *first*.

### The correction that came out of the process item: it is not depth, and it is not eviction either

The owner's refinement — that the detecting property is churn rather than depth, since
levels must actually be pushed out of the window — is right that depth is the wrong
explanation, and **d25 catches on BTC/USD while d25 misses on the quiet pair** proves
it outright. But eviction *rate* was then measured and is **anti-correlated** with
detection: depth 100 evicts 467.6 levels per 1,000 book messages, the highest of the
three BTC captures, and detects nothing; the quiet pair evicts at 633.3 per 1,000 and
detects nothing.

A second hypothesis — evicted levels *returning* to the checksummed top 10 — fitted
all four committed slices exactly and was **falsified by the fifth capture the same
evening**: the reconnect trace scores 3 returns and detects nothing. Two mechanisms,
both certain from the code rather than inferred: a price can only re-enter the book
because the venue *sent* it, and that message corrects a non-truncating book too; and
`Book.replace` clears every level, so **a resync heals a non-truncating book
outright** — a trace containing a reconnect is a *worse* truncation golden than one
without, which is the opposite of the instinct that a reconnect trace is more
thorough.

So the criterion is the direct measurement, `ok_never_truncating < checksummed`,
printed per trace as **CATCHES** / **MISSES** and pinned. Both counters are kept as a
description of *why* a trace does or does not exercise the rule. **M5 inherits a
recipe rather than a heuristic:** liquid pair, shallowest offered depth, no reconnect
inside the committed window — then check it with the tool before trusting it.
Capturing the most production-like configuration and assuming it covers the rule is
exactly what would have happened here at depth 100, where a 90-second test is 100%
clean while the client is wrong.

**Two heuristics fitted and discarded in one evening is the milestone's own failure
mode arriving in a new place** — a measurement that answers a nearby question read as
answering the one that mattered. The difference this time is that the falsifying
capture already existed, and the cost of checking was one command.

---

## What M4's brief must carry

Set by the owner from the planning seat, 2026-08-16, and recorded here because this
brief is the durable artefact — stage 0 existed to make M4's brief writeable, and
these are the four things that were not obviously in its scope the day before.

1. **The subscription ack is load-bearing, and failing it is fatal — not a `Gap`.** A
   refused `depth` leaves a live socket, a `status` frame and 1 Hz heartbeats over a
   permanently empty book. `Gap` means *the book is unknown until the next snapshot*;
   this is *no snapshot is ever coming*, and rendering it as honest grey forever is
   honest about the wrong thing. The adapter checks `success` and treats false as a
   configuration error with its own reported state. **Nothing in the current
   vocabulary says "this feed will never start" — check whether that needs saying
   before writing code that needs it.** That is a §4 question, so it is stop-and-raise,
   not a session's call.
2. **The three clocks, decided explicitly.** Not a new value for `kRxWatchdogMs`. The
   question is *which arrival stamps the clock that greys the panel* — byte, data
   frame, or book event — per the §9 row of 2026-08-16 (stage 0), and whether the
   answer is per-venue. If it is, it belongs beside the venue's other declared
   metadata rather than in a firmware constant.
3. **Name the truncation goldens in the brief, and do not infer them from depth.** The
   d10 and d25 BTC/USD slices catch a missing truncation; d100 and the quiet pair do
   not, and depth does not predict which is which (see the session log above and
   `NOTES-kraken.md`). The criterion is the measurement
   `ok_never_truncating < checksummed`, which `--verify` prints as CATCHES / MISSES.
   A committed trace that exposes the defect quickest is worth more than the one that
   looks most like production.
4. **The strain-20 criterion, and stage 0 has already found it.** M4's DoD needs one
   bar the harness structurally cannot stage. It is the quiet pair on the panel: *the
   ladder holds its colour through a nine-second legitimate silence, and greys when the
   feed actually dies.* A replay trace can stage the silence; only the bench can stage
   both halves against the same firmware in one sitting.

Plus the trace-reader question in the session log above: `dc_replay` cannot read a
Kraken capture, and the traces were deliberately not reshaped to suit it.

**Exact next step.** Write that brief.
It now has: a priced wire at three depths, a depth recommendation awaiting the
owner's decision, a settled strain-3 toolchain fact, and three things that must be in
its scope which were not obviously in it yesterday — **(1)** the watchdog/liveness
rule for a venue that publishes on change behind a 1 Hz heartbeat, **(2)** the
subscribe-ack `success` check, without which a refused subscription is
indistinguishable from a healthy quiet market, and **(3)** the trace-reader question:
`dc_replay` cannot read a Kraken capture (it fails on the metadata header's required
`ticker`, and 61 of the depth-25 slice's 1,599 records carry no string `type`), so
M4 must decide whether the metadata contract becomes venue-tagged, the reader learns
a second dialect, or Kraken gets its own reader. The traces were deliberately not
reshaped to suit today's reader.
