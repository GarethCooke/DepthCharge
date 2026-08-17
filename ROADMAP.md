# DepthCharge — Roadmap

Semi-stable. Update the **Status** column as milestones complete; anything structural
belongs in `ARCHITECTURE.md` §9 instead. Sessions: your milestone's brief in
`docs/briefs/` overrides the one-line summary here.

**Standing priority note:** Anvil (In Progress) and FrontierView interview prep rank ahead
of DepthCharge in the owner's queue. DepthCharge sessions are opportunistic; keep
milestones evening-sized.

## Tracks

- **[A]gentic** — Opus / Claude Code sessions, converging on the harness's red/green.
- **[B]ench** — owner at the bench (soldering iron, KiCad, printer); Claude assists in
  review mode only.

M1 and M2 share no dependencies: software sessions and bench time run in parallel.

## Milestones

| #  | Track | Milestone                     | Goal / definition of done                                                                                          | Depends on | Status |
| -- | ----- | ----------------------------- | ------------------------------------------------------------------------------------------------------------------ | ---------- | ------ |
| M0 | A     | Trace + harness               | Live Anvil WS traces captured & vendored; replay harness parses them; golden-test + CMake skeleton; ctest green.    | —          | ✅ Done (2026-07-23), in-tree green — brief: `docs/briefs/M0-trace-and-harness.md` |
| M1 | A     | Console ladder off replay     | `FeedEvent` types real; Anvil adapter (frames→events); phase-1 book (adopt snapshot + trade ring); console ladder renders a replay; goldens green. | M0 | ✅ Done (2026-07-26), in-tree green — brief: `docs/briefs/M1-console-ladder-off-replay.md` |
| M2 | B     | Panel smoke test              | ESP32-S3 DevKit + 64×64 HUB75 + PSU wired; HUB75 DMA library demo runs; photo in `hardware/`.                        | —          | ✅ Done (2026-07-26), in-tree green — brief: `docs/briefs/M2-bench-bringup.md` (bench; owner-driven). Agentic sessions have no blocking software work until M3 — MP stage 1 is available in parallel. |
| M3 | A+B   | Live Anvil on the panel       | Firmware net task (TLS WS, nominated `Origin` header) + render task; engine unchanged from M1; live ladder on the panel; pull-the-Wi-Fi test shows grey stale state then clean resync. | M1, M2 | ✅ **Done (2026-08-16)** — the pull-the-Wi-Fi acceptance passed on its second run: panel greys with no hue, station rejoins **unaided** (no reset in the log), deauth → LIVE in **18.7 s**, one rejoin call. Record: [`hardware/bench-2026-08-16-pull-the-wifi-acceptance.md`](hardware/bench-2026-08-16-pull-the-wifi-acceptance.md). The first run **failed** and found the rejoin livelock (ARCHITECTURE §9 2026-08-16 pm, DESIGN strain 20) — that failure is the best argument this project has produced for keeping a bench criterion in a DoD. Stages A ✅ (2026-08-07, wait-free `SnapshotChannel`), B ✅ (2026-08-08, streaming allocation-free parser, goldens unchanged), C ✅ (2026-08-09, feed proven on the board), **D ✅ (flashed and long since proven — the 23.6 h soak of 2026-08-15/16 ran the panel continuously; the "not flashed" note this cell carried until 2026-08-16 was stale by a week)**. The transport rewrite is **done and accepted** (owned WS client over esp-tls; 10.9 h on one connection, zero errno-silent deaths, zero framing rejects — brief closed 2026-08-16), the TCP window is rebuilt and is now the default build, and the supervisor recycles a silent socket after five minutes. Two items remain owed against M3's *brief* and neither gates this tick: the weak-node 1 h soak (transport brief DoD, never run on the final build), and the ghosting re-check with `clkphase = false` (`hardware/BRINGUP.md`). |
| M4 | A     | Kraken adapter                | Delta application + CRC32 verification; dense-window book lands here; Kraken traces + goldens; panel switches venue. | M3 | ☐ **Next** — and it inherits two things from M3. A venue's byte rate is a design input (DESIGN strain 19: the object already runs at 51–79% of Anvil's wire and Kraken's full-depth stream is larger), and its DoD should include at least one criterion the harness structurally cannot stage (strain 20). **Stage 0 ✅ done 2026-08-16** — brief: [`docs/briefs/M4-stage-0-price-the-kraken-wire.md`](docs/briefs/M4-stage-0-price-the-kraken-wire.md), observations: [`harness/replay/NOTES-kraken.md`](harness/replay/NOTES-kraken.md). **What it fixed, before an adapter existed to be wrong: (1) the premise that Kraken's stream is larger than Anvil's — it is a third the size at *four times* the rendered depth** (depth 25 = 6.12 KiB/s, depth 100 = 9.91, against Anvil's 30.8 at depth 27; 5.0× and 3.1× headroom), so strain 19's sizing worry does not transfer to this venue and the depth choice is a design decision, not a transport one; **(2) `depth: 27` is REFUSED, not rounded up** the way A7's tier ladder rounds — and the refusal leaves a live, heartbeating socket with a permanently empty book unless the ack's `success` is checked; **(3) `kRxWatchdogMs = 1000` is wrong here by 4.5×** — worst *book* silence is 4,535 ms on BTC/USD and 9,007 ms on a quiet pair, while a 1 Hz heartbeat floors *byte* silence at 998–1,026 ms, so the two counting rules that agree at Anvil disagree 9× here; **(4) invariant #3 now has a number** — 8,677/8,677 checksums reproduce from the verbatim text and **0/2,786** from float-parsed text; **(5) the client-side truncation rule is load-bearing and hides at depth** (checksum matches 379/1,412 at depth 10, 4,435/4,435 at depth 100 — untestable in a short trace at the depth most likely to be developed against). Four 60 s slices committed (121 KiB gzipped); `dc_replay` deliberately cannot read them yet. **Both decisions are now TAKEN** (stage-0 brief session log; ARCHITECTURE §9 rows dated 2026-08-16 stage 0): **depth 25 with `kDisplayLevels` staying 27** — the two unfilled rows a side are §4's "depth beyond N is unknown, not zero" *rendered*, so a ladder differing in height by venue is correct rather than a wart to reconcile at M7 — and **strain 3 resolves as `static_assert`s, not a `concept`**, because host and target would compile one spelling under two dialects, which type-checks and is therefore worse than convention. Stage 0's own tooling is now pinned too: `ctest` runs `kraken_tool_selfcheck` (mutation-verified, add-rows-only pin table with a `--pin` mode that refuses to overwrite one). **The d10 and d25 BTC/USD slices are the truncation goldens, and which traces qualify is not predictable from depth** — d25 catches on BTC/USD and misses on the quiet pair; depth 100 evicts levels *hardest* of the three and detects nothing. Two plausible heuristics were fitted and falsified, so the criterion is the direct measurement (`ok_never_truncating < checksummed`), and M5 inherits it as a capture recipe: liquid pair, shallowest offered depth, **no reconnect in the committed window** — a resync heals a non-truncating book and destroys the trace's value as a guard. **M4 now runs as four evening-sized stages** (ARCHITECTURE §9, 2026-08-17): **A** the replay dialect · **B** the adapter · **C** the dense-window book in `engine/` · **D** the bench. **Stage A ✅ done 2026-08-17** — brief: [`docs/briefs/M4-stage-A-the-replay-dialect.md`](docs/briefs/M4-stage-A-the-replay-dialect.md). **It precedes the adapter because invariant #6 says so:** no merge without replay coverage, and `dc_replay` could not read a Kraken capture at all — it rejected the metadata header on the required `ticker` and would have rejected 61 of the depth-25 slice's 1,599 records for carrying no string `type`. An adapter written before the reader is an adapter that cannot be covered, and the pressure at that point is to reshape the committed traces to suit the reader. **The reader learns the wire; the wire does not learn the reader.** Stage A ships: the `venue` metadata tag (additive, absent = `anvil`, both writers emitting it); one reader dispatching to per-venue decoders with the sink shape pinned by `static_assert`s (strain 3, option (ii), first use); Kraken's decoder as a **classifier that emits no `FeedEvent`s**; the record taxonomy measured and pinned by `trace_taxonomy_selfcheck`; and the replay driver's disconnect threshold made **venue-declared** — Anvil 1,000 ms, Kraken 15,000 ms — so the quiet pair's legitimate 9,007 ms silences stop reading as disconnects. **Measured on the committed slices: the Anvil constant invents 25 disconnects in 60 s on the quiet pair by the record-arrival rule and 12 more by the book-event rule; at the declared 15,000 ms both are 0, on all four slices.** The four Anvil traces are byte-identical through the changed readers — 12 outputs across three programs, diffed. **Reviewed and accepted 2026-08-17; the review's four follow-ups are done and the tree is green at 21/21.** Three things are now written down as **stage B's inheritance** (stage-A session log, § *Owed by stage B*, and DESIGN strain 22): **(a)** the dispatch must prove it dispatched right — *a Kraken golden fails if it was produced by the Anvil parser*, with the decoder's identity in the pinned output and a mutation that swaps the dispatch expecting red; **(b)** a **deliberately captured Kraken resync slice** is owed, and it cannot be one of the four truncation traces, because a resync heals a non-truncating book and the two capture purposes are mutually exclusive; **(c)** the venue table's duplication of `kRxWatchdogMs` closes when stage B lifts it into the shared firmware table. The review also confirmed the resync rule's Anvil agreement is **not** agreement-by-absence — the reconnect trace scores 1 under both the old and the new rule, while all four Kraken slices score 0 under the new rule and **1 under the old one**, which is the bug stage A fixed, quantified. **RULING 2026-08-17, and it supersedes stage A's own decision 2 in part (ARCHITECTURE §9): staleness stops counting book events and counts the venue's declared LIVENESS SIGNAL instead** — Kraken's 1 Hz heartbeat, Anvil's 2 Hz `summary`, which the read-and-report confirms is a fixed engine-thread deadline firing on an empty queue (62 byte-identical idle frames with `seq` advancing, nominal 500 ms, worst 547). The two-clock split stands; what the staleness clock counts does not. **No threshold on book silence can be correct** — a quiet market and a dead subscription are identical on the wire, and MINA/GBP's healthy 25,843 ms proves the bound is a market property. Book-event silence becomes `age_ms` (stage A2), rendered as a number and never as grey; the threshold becomes **self-calibrating** from the observed median of the liveness signal, because both venues' intervals are values DepthCharge cannot read back. **`docs/vendor/anvil-protocol.md` re-pinned at `4801ed8` (2026-08-17), closing §9 item 8** — Anvil corrected the keepalive clause that denied idle emission, and §3.5 gained a *guarantee* that emission is timer-driven with the interval explicitly excluded from it: *"A client that needs a staleness threshold must derive it from the cadence it observes on the connection it is using, not from a number read out of this document."* The self-calibrating threshold was written the day before that sentence existed and satisfies it unchanged — **not independent convergence, since both descend from the same instruction to Anvil (*promise the mechanism, not the interval, and refuse the number*), but evidence that the ruling survived a second implementer who could have found it awkward.** **Stage A is complete and COMMITTED** (`5745b52`…`93c6957`, the ten-commit split from the stage-A ruling brief, executed). **The twelve items stage A left owed are now triaged** — brief: [`docs/briefs/M4-triage-of-the-twelve.md`](docs/briefs/M4-triage-of-the-twelve.md) — and the sort moved five of them out of stage B, so **the rest of M4 runs as A2 · B1 · B2 · C · D** (ARCHITECTURE §9, 2026-08-17 M4 triage). Three questions decided there and recorded rather than left to look like oversights: **the client ping is deferred to M6** (transport work; the windowed deficit is enough for a numeric header), **the uninitialised-book question splits** (the engine state is C, the rendering is D, because the two candidate renderings differ in nothing a host test can assert), and **the defensive resubscribe is not built** (its trigger would be book silence, which the ruling established carries no information — it would fire every ~26 s on a healthy MINA/GBP). **Stage A2 ✅ done 2026-08-17** — brief: [`docs/briefs/M4-stage-A2-the-age-meter.md`](docs/briefs/M4-stage-A2-the-age-meter.md). §5 gains `age_ms` + `has_age`, both landing in padding the hand-off already had (`sizeof(DisplaySnapshot)` unmoved at 1,168 B and now pinned by `static_assert`, because three documents quote a byte count derived from it); the estimator is a **sliding-window supremum** — Lindley's backlog recursion over 256 liveness arrivals — and it is measured against a **baseline latched once per connection**, not against the rolling median the grey threshold uses, because sharing that median makes the meter read **0.0 s through a 111 s backlog**. The header renders to minutes and hours (`2m56s`, `1193h02m`), and nothing branches on the number. **The four healthy Anvil traces read a 0.5–0.9 s noise floor with zero grey episodes**, the reconnect trace peaks at 2.3 s and then reports *no reading* until the new connection has re-measured the venue's clock. **The blind spot is pinned as a test rather than described:** a socket already behind when it latched cannot detect it — on `_local/drain-120ms.ndjson` the meter reads **12.5 s against a true ~122 s** — and that case is uncoverable by any capture, because a backlog belongs to one client's socket and a trace records only when that client got the bytes. It is what the deferred client ping exists to close. **Reviewed before commit, and the review paid for itself twice:** the first implementation latched its baseline from the threshold clock's rolling median, which is deliberately *not* reset at a disconnect — so a reconnect away from a backlogged socket would have handed the fresh healthy connection the throttled cadence and reported no lag for ever; and fixing that exposed the resumption burst (`253 871 174 475 …` ms against a 500 ms broadcast), which an 8-interval latch reads as 478 ms and a 32-interval one as 499.3. Green at 24/24, **committed as six** (`52af6e0`…`91c7a8d`), each verified green in a worktree of its own. |
| M5 | A     | Binance adapter               | Partial-depth easy mode, then full diff stream with REST-snapshot bracketing and gap recovery; traces + goldens.     | M4 | ☐ — **and it needs its own detector before it can have a golden.** M4 stage 0's trace recipe (liquid pair, shallowest depth, no healing event in the window) works at Kraken because that venue publishes a **CRC32 over the top 10 levels** — an independent oracle that answers "is my book the venue's book?" outright. **Binance publishes no checksum**; `U`/`u` bracketing detects lost or misordered messages and says nothing about whether the resulting book is correct, and it can be satisfied while the book is wrong. So inventing the oracle is part of M5's work, not a detail of it — see [`harness/replay/NOTES-kraken.md`](harness/replay/NOTES-kraken.md) § "What transfers to M5, and — more importantly — what does not". **The one clause that does transfer, unchanged: whatever the oracle is, prove the trace catches a deliberately broken implementation before pinning it** (ARCHITECTURE §9, 2026-08-16 stage 0). Binance's own healing event is the REST re-snapshot on gap recovery, and it belongs outside any guarding window for the same reason a Kraken resync does. |
| M6 | B     | Carrier PCB                   | KiCad carrier (WROOM-1-N16R8, 2× 74HCT245, HUB75 IDC, USB-C 5 V/3 A with CC pulldowns, bulk caps, EC11); DRC clean; fabbed & bring-up. | M3 | ☐ |
| M7 | A+B   | Enclosure + board mode        | Printed enclosure, smoked acrylic front; encoder modes: ladder (symbol/venue/zoom) + Anvil 12-ticker summary board mode. | M4, M6 | ☐ |
| MP | A     | Portfolio portal              | **Executes in the `garethcooke-portfolio` repo.** Stage 1 (any time after M0): `/projects/depthcharge` live with In Progress badge, concept art, tags, repo/architecture links, plus two drive-by fixes. Stage 2 (after M3): real hardware photos/video. Stage 3 (any time): the design doc reaches the portal — `/depthcharge/design` renders `docs/DESIGN.html`, tracked by its own `writeup-sources.json` entry so it drifts independently of the architecture page. Briefs: stages 1–2 in the **portfolio** repo at `docs/briefs/MP-portfolio-portal.md`; stage 3 here at [`docs/briefs/MP-design-doc-on-the-portal.md`](docs/briefs/MP-design-doc-on-the-portal.md). | Stage 2: M3 | ◐ Stages 1 ✅ (2026-07-22) and 3 ✅ (2026-08-08, pending commit in garethcooke-portfolio); **stage 2 is UNGATED as of 2026-08-16** — M3 is done, so the real hardware photos/video can go up, and the stale-panel shot from the acceptance is a good one |

## Backlog (not scheduled)

Items carry a stable ID — `A*` Anvil-side, `D*` DepthCharge-side — so briefs and session
logs can cite one without quoting it. Each is a one-line **what**, then indented detail only
where there is evidence to carry. Closed items move to the bottom rather than being deleted.

### Anvil-side (cross-referenced only)

**Anvil is a separate repo and is not modified from here.** Nothing below is a proposal to
Anvil; each is a note of what DepthCharge is waiting on, working around, or has measured
from the outside. They live on Anvil's own backlog.

**The ask was [`docs/anvil-handover-2026-08-16.md`](docs/anvil-handover-2026-08-16.md); the reply
is [`docs/anvil-handback-2026-08-16.md`](docs/anvil-handback-2026-08-16.md).** Anvil answered the
same day: **A7, A2, A3 part 1 and A6 are done, pushed and deployed**, its backlog is rebuilt
around these IDs, and one item is new (**A2b**). Both sides now cite the same letters.

| ID  | Item                            | Standing                                                      |
| --- | ------------------------------- | ------------------------------------------------------------- |
| A7  | `depth` parameter on `/ws`      | ✅ **DONE + INTEGRATED 2026-08-16** — 112.6 → 30.8 KiB/s       |
| A6  | TLS-chain rotation              | ✅ **DONE** — in Anvil's `deploy/README.md`, lock-step flagged |
| A2  | Client pings / keepalive        | ✅ **DONE** — and the ordering is now *measured*, closing D5   |
| A3  | Per-socket send-queue behaviour | ◐ **pt 1 documented**; pt 2 (bounding it) open, owner's call  |
| A2b | The 2 min 56 s stall            | ☐ **NEW, open** — split out of A2; two Anvil hypotheses       |
| A1  | Sequenced incremental L2 feed   | ☐ Sized, not started — **and A3 pt 2 must be decided first**  |
| A4  | Chaos flag for gap testing      | ☐ Open; near-required alongside A1                            |
| A5  | Feeder realism                  | ☐ Open; nothing here is blocked on it                         |
| B1  | `summary.last` = traded price   | ✅ **DONE Anvil-side** — semantic change, **no action here**   |

**Nothing on this side is blocked on Anvil any more.** The two integration tasks the hand-back
asked for are both done: `&depth=27` is in `kAnvilPath`, and `docs/vendor/anvil-protocol.md` is
re-pinned — now at **`04db612`**, not the hand-back's `b4d31c2`, because Anvil landed **B1** the
same evening (below).

**B1 · `summary.last` is a traded price, not the book mid.** *Anvil `864ee2f`/`04db612`, found by
checking their repo rather than by being told.* A **semantic** change inside wire version 1 with
**no version bump**, which is the class a shape check cannot catch: same key, same JSON string
type, same `""` sentinel — but `""` now means *this ticker has not traded yet* rather than *the
book is empty*, and the value **persists after the book empties**.

- **No action here, and it is structural rather than lucky.** `AnvilAdapter` files `summary` under
  `FrameKind::Summary → ++stats_.summary_ignored` and returns, so no field of a summary frame has
  ever reached the book, the ladder or the panel. The only thing this project derives from
  `summary` is its **arrival count as a 2 Hz clock** (`staleness.hpp`), and B1 moves a value, not
  a cadence. No engine change, no firmware change, no golden moves.
- **M7 is the milestone that will care.** The board mode is what finally *reads* `summary`, and it
  must treat `last` as a traded price with a persistence rule, not as a mid — a board built on the
  old meaning would show a price on an empty book and be correct to.
- **The lesson repeats within eight hours:** the vendored header already argued for re-vendoring
  *on a schedule rather than on a symptom*, having sat three weeks stale once. It was stale again
  by the same evening. Worth a standing check rather than a resolution.

The detail below runs in ID order **except A7, which comes first because it was the cheapest
item here and the largest win** — and it is now closed.

**~~A7 · A `depth` parameter on `/ws`~~ — DONE Anvil-side, DEPLOYED, and INTEGRATED here
2026-08-16.** `kAnvilPath` is `/ws?ticker=101&depth=27`. That one constant is the entire
integration; `engine/` did not move, exactly as the ask predicted.

- **Verified on this side rather than taken on trust** — the ask's own lesson was that a nearby
  measurement gets read as answering the question that mattered.
  `harness/replay/anvil_101_depth27_20260816.ndjson` (90 s, 1,399 frames, committed and now a
  ctest case) priced by `tools/anvil_frame_economics.py`: **`book` mean 8,428 → 2,471 B**,
  **112.6 → 30.8 KiB/s**, **27.3% of the 2026-08-09 baseline**. This repo predicted 27.8%.
  Anvil's own deployed-server figure of 2,476 B agrees to **0.2%**.
- **Against the 23.6 h soak's worst measured hour (56 KiB/s): 1.8× headroom, where it was 0.5×.**
  That is the staleness fix, and it needed no protocol redesign, no sequencing and no resync.
- **DEPTH IS SERVED IN TIERS** — 1,2,3,5,8,10,15,20,30,40,50,75,100,150,200,300,500,1000, then
  unlimited. A request rounds **up**, never down, so asking 27 is **served 30** and the adapter
  truncates the last three. Not in the ask; it came out of Anvil's review, and the reason is
  sound — `depth` arrives from an unauthenticated query string, so free-form depth would have
  cost one serialisation per distinct depth in use, per ticker, per tick.
- The tier's cost, stated because Anvil sized it as "a few percent": 30 served against 27
  rendered is **9.7% of `book` bytes** the panel never draws. Trivially worth paying; if a later
  ladder ever offers 27 exactly, that is where the remaining tenth is.
- **A premise in our own ask was wrong, and it is worth recording.** We cited
  `GET /api/book?depth=` as proof that "the name, concept and validation already exist". The
  parameter existed and was **silently ignored** — there was no validation to reuse, and Anvil
  fixed the REST surface as part of A7. Arguing from the existence of a parameter is not the
  same as arguing from its behaviour.

**A1 · Sequenced incremental L2 feed.** Promoted 2026-08-11 to the thing that makes the
hardware work at all; **demoted 2026-08-16 by A7, which has now shipped** — the query parameter
took the stream to 27.3% for one constant on each side, so this is not blocking anything and is
judged purely on its own merits. On those it is still the better design.

- **Anvil sized it 2026-08-16 and the prerequisite is small.** The per-ticker monotonic book
  sequence is **S and independent of Stage 4 in both directions**, so it lands well before it:
  the publish loop already stamps per ticker each tick, and a second counter adds no cross-thread
  state, no lock and no engine change. One scoping point buys that size — **books only**. A
  sequence that also orders *trades* against books needs the broadcaster to merge-sort two
  independent sources, and a delta feed does not need it.
- **NEW AND ORDER-CHANGING: A3 part 2 must be decided BEFORE A1, not alongside it.** Our own note
  said the two "have to be answered together"; Anvil sharpened it into a direction. The
  per-ticker sequence is genuinely gap-detectable end-to-end **because** the per-socket queue is
  unbounded and therefore lossless. If A3 part 2 is resolved by *dropping* `book` frames, that
  property dies — benign under full replaces, **fatal under deltas**. So the queue decision
  constrains the sequence design, and taking A1 first would risk building on a guarantee A3 is
  about to remove.

- *Sized 2026-08-16.* "A tenth the bytes" was a 3× under-claim: **a median of one level changes
  between consecutive `book` frames** (mean 1.1, p99 3, max 4 over 90 s), so an upper-bound
  delta encoding is **1.3% of current `book` bytes** — against A7's 26.5%.
- Its load-bearing prerequisite is a **per-ticker monotonic sequence**: the global stamp is
  sparse *and* non-monotonic per socket by design, and unlike a full replace, a missed delta
  never heals. PROTOCOL §1 already names this as the change "if strict gap detection is ever
  wanted". Worth doing first and alone, even if deltas never follow.
- **It is not Stage 5** and must not wait for it — Stage 5 is binary ITCH over LAN UDP
  multicast, gated behind Stage 4.

- *Rewritten 2026-08-16.* The three claims this item used to make — that a 5,744-byte window
  caps the board at 65.5 KiB/s, that no firmware lever reaches it, and that the alternative
  is a rebuilt ESP-IDF — are all superseded, because the rebuild happened.
- **What shipped instead:** `liblwip.a` rebuilt from the shipped esp-idf v4.4.6 vintage with
  `TCP_WND` 5744 → 17232; now the default build.
- **Soak (23.6 h, 2026-08-15/16)** says it is load-bearing: **56–87 KiB/s at every hour of
  the day** (floor 56 mid-morning, ceiling 87 at midnight) against Anvil's 110.4 KiB/s wire —
  **51–79%** of it (corrected 2026-08-16 from "60–80%", which was arithmetic, not measurement:
  56/110.4 = 50.7%), and most hours *above* the stock window's hard cap. Lag slope fell from
  +0.57 s/s on every stock-window run to **+0.083 s/s over the final 10.9 h**.
- **RX loop instrumented and exonerated:** `wait 0 / read 98–99 / feed 0` in every hour, so
  the board is bound by how fast bytes come off the wire into it, and there is nothing left
  to optimise on this side.
- **Residual is path bandwidth** on the transatlantic hop. Until a smaller feed exists the
  object runs tens of seconds behind at UK peak and honestly says so (ARCHITECTURE §9,
  2026-08-16) — but the *smaller feed* no longer has to be this item. A7 is.

**~~A2 · Client pings / keepalive~~ — DONE 2026-08-16, and the ordering is MEASURED.**
`PROTOCOL.md` §3 gains a **Keepalive** subsection: a client ping is always answered, no rate
limit and no payload cap (our 10 s cadence is explicitly fine), the server **never initiates** a
ping in v1 and **never uses pongs to detect dead clients**, and — the load-bearing sentence —
**the pong is queued behind whatever is already waiting for that socket.**

- **D5's "still owed" is closed, and closed properly.** This repo had recorded that the ordering
  was read from Anvil's source and *never captured under induced backpressure*, so the number
  was evidence rather than a guarantee. Anvil captured it: `tests/tools/pong_ordering_probe.py`
  stops reading until frames back up in the send queue, then sends one ping and reports the
  pong's **position in the byte stream** rather than its RTT — because a slow reader inflates the
  round-trip whether or not the server reordered anything, so timing alone cannot separate a
  server-side queue from a client-side one. **425,890 bytes drained as 149 frames; the pong at
  index 148 — last, with zero frames after it.** Read it as a guarantee.
- Two things would still silently break it, recorded as "please don't change" rather than as
  defects: a ping after a two-way close handshake is never ponged, and the behaviour depends on
  `max_payload` staying default with `CROW_ENFORCE_WS_SPEC` undefined. `PROTOCOL.md` now says to
  re-run the probe on a Crow upgrade rather than assume it survived.
- The request for a **server-side heartbeat is withdrawn** and recorded as such on both sides.

**A2b · The 2 min 56 s stall — NEW, open, and Anvil split it out for a good reason.** Our A2
carried two unrelated things: a doc confirmation and an open-ended investigation. Sizing the pair
as "S, mostly withdrawn" hid the second behind the first.

- Our framing is preserved intact: nginx is exonerated, but a successful request on a
  **different** connection cannot prove that flow was healthy, so from outside the evidence
  cannot separate a server stall from one transatlantic flow in RTO backoff.
- **Two Anvil-internal hypotheses fit the signature** (total silence on a healthy TCP connection,
  spontaneous recovery, HTTPS unaffected), neither confirmed: **fan-out head-of-line blocking**
  — the broadcaster holds one mutex across the *entire* fan-out, so one stalled socket's
  unbounded queue stalls every socket on every ticker, and it would free **spontaneously** when
  that socket disconnected, which matches the unexplained recovery; or **engine-thread publish
  starvation** — the ~2 Hz summary walk touches every resting order, and the broadcaster skips a
  ticker whose sequence has not moved, producing silence on a healthy connection.
- The first hypothesis is Anvil's leading candidate and **makes A3 part 2 look much less
  optional**.
- The fix Anvil recorded is **server-side telemetry rather than a wire heartbeat** — broadcaster
  last-tick and worst fan-out duration, engine last-publish per ticker, on `/api/health` — so the
  next stall is diagnosable from outside, including by us. **D5 already splits half of this from
  our side** (`age` high + `rtt` high = our queue; `age` high + `rtt` low = upstream); the
  telemetry would say *which* upstream cause, which our instrument structurally cannot.

**A3 · Per-socket send-queue behaviour.** *Part 1 (document it) is **DONE 2026-08-16**; part 2
(bound it) remains open and is the owner's call.*

- **Part 1 shipped.** `PROTOCOL.md` §4 carries **"Slow consumers — the server queues, it does not
  shed"**: no cap, no drop rule, no coalescing, no disconnect threshold, unbounded in v1, lag
  linear with no plateau — with our measured figures cited in Anvil's text. The client rule we
  asked for is now **contract**: measure freshness against your own clock, never infer it from
  message rate. Our withdrawn bullet is retired into a *Closed* section with the record intact,
  and it names the distinction the original conflated — coalescing *does* exist, but it is the
  ~14 Hz book tick, which is upstream and global, not per socket.
- **Part 2 is sized M, not S.** Crow exposes no queue depth and no send-completion callback, so
  any bound needs a patch to the vendored header plus an atomic counter to keep the check off the
  broadcaster thread. Our stated preference — disconnected rather than served 100-second-old
  data — is recorded.
- **A7 narrows it for us but does not close it:** a *full-depth* socket that stops reading still
  costs the server the original ~110 KiB/s. Our own socket is now a third of that.
- **And it is now coupled to A1 in a direction we had not seen** — see A1.

- The earlier wording here said "coalescing / even backpressure-shedding", on the strength of
  a 2026-08-09 rate-and-gap measurement; that conclusion is **withdrawn**
  (ARCHITECTURE §9, 2026-08-11).
- **Measured 2026-08-11** with `tools/anvil_freshness_probe.py`: a desk socket throttled to
  25% of the stream sees **every** frame kind thinned by the same fraction and its lag grow
  **linearly to 111 s over 150 s with no plateau**, implying **~12.4 MB still queued for that
  one socket** and rising. A DepthCharge board on one socket accumulated ~98 s of backlog in
  210 s of uptime.
- **Confirmed in Anvil's source 2026-08-16**, so it is construction rather than weather:
  `CrowWsSubscriber::deliver()` → `conn.send_text()` → Crow's per-connection `write_buffers_`,
  a `std::vector<std::string>` with **no cap, no drop policy and no coalescing** on the path.
  One socket that stops reading costs the server ~110 KiB/s of RAM for as long as it stays
  connected.
- **Downstream rule** — stands, and is now sharper: never assume a thinned stream is a fresh
  one, and M4/M5 delta venues cannot tolerate either shape without gap + resync.

**A4 · Chaos flag** for deterministic gap testing. Near-required if A1 ever happens: a delta
feed's gap-recovery path cannot be proven without one.

**A5 · Feeder realism** — Hawkes arrivals / mirror mode / FrontierView execution-algo
participant. DepthCharge is its future test client.

**~~A6 · TLS-chain rotation~~ — DONE 2026-08-16.** The note is in Anvil's `deploy/README.md`
beside the certbot step: it records that the firmware pins ISRG Root X1 as its sole anchor with
no fallback and no console, that routine renewals are invisible to us, and that a CA change or a
lost cross-sign is a silent handshake failure fixable only by reflash — flagged as a **lock-step
change: coordinate the firmware update first, then rotate.** The original ask, for the record:

**A6 · TLS-chain rotation** — a DepthCharge-firmware-pinned dependency: an Anvil CA/chain
change means a lock-step firmware update. *Sharpened 2026-08-16:* the firmware pins **ISRG
Root X1** as its only anchor and Anvil deploys with certbot behind nginx, so ordinary renewals
are invisible **so long as the renewed leaf still chains to X1**; a CA move or any shortening
of the presented chain (there are *two* cross-sign hops, and YE←X2 is the newer) is loud on
DepthCharge's serial console and silent on Anvil's. **The whole ask is one line in Anvil's
`deploy/README.md`** — tell DepthCharge before changing CA or chain configuration — or an
explicit `--preferred-chain`, which survives a certbot default change in a way a README line
does not.

- **Owed here, found 2026-08-16:** `firmware/src/anvil_root_ca.hpp`'s own header says the
  ESP-IDF bundle is unreachable because `WiFiClientSecure` offers only `cert_pem`. **That died
  with the M3 transport rewrite** — `ws_transport.cpp` builds an `esp_tls_cfg_t` by hand, which
  has `crt_bundle_attach`, and the framework ships `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` with
  200 certs including X1. The pin is now a *choice*, not a constraint; the comment is stale and
  should say so, and "switch to the bundle" is a live option worth its own decision.

### DepthCharge-side

**D1 · The rejoin re-rolls the mesh lottery, and the board never roams off what it draws.**
*[A] — one session, host-testable policy half.*

- **Measured 2026-08-16** during the passing acceptance: boot's explicit scan joined at
  **−40 dBm**, the recovery's plain `WiFi.begin()` landed at **−70** and drifted to −76, and
  the watchdog greys went from 1 to **14 in three minutes**.
- **Why:** `connect_wifi()` surveys and joins the strongest by name; the supervisor's rejoin
  cannot, because a blocking all-channel scan on loopTask would stall the 250 ms supervise
  poll. `ALL_CHANNEL_SCAN` + `BY_SIGNAL` narrow the lottery and do not close it — which is
  what §9 (2026-08-13) already recorded when they failed their five-boot acceptance, and the
  boot fix was never extended to this path.
- **The shape of the fix is already established by M3:** the socket connect used to block
  loopTask and now runs on the RX task, with the supervisor deciding and the task doing; the
  rejoin's scan-then-join wants the same treatment.
- Details in [`hardware/bench-2026-08-16-pull-the-wifi-acceptance.md`](hardware/bench-2026-08-16-pull-the-wifi-acceptance.md).

**D2 · Weak-node 1 h soak.** *[B] — owner-driven at the bench.* The one transport-brief
acceptance bar never run on the final build: pin or re-roll the association to a −7x sibling,
hold it an hour, and check that the fades grey the panel without killing the socket (board
B's `…:F9` record is the comparison). Small, and the last thing standing between the
transport rewrite and a fully ticked DoD. *The board is currently sitting on a −75 dBm
association by accident, which is most of the setup.*

**D3 · Crucible post — book structures under fire.** flat_map vs dense window, driven by
Anvil's *trend* workload.

**D4 · Live web mirror** *(optional)* — a browser twin of the panel's `DisplaySnapshot` feed.
*That* would be companion site #4 and trigger the shared-component-repo review. The portfolio
page itself is scheduled work (MP) inside the portfolio repo and does **not** count toward
the threshold.

### Closed

**~~D5 · Ping the venue instead of waiting on it — the liveness watchdog gets a clock.~~**
*[A] — **DONE 2026-08-16**, host-green and building on all three firmware arms; unflashed.*
`firmware/src/ws_ping.hpp` (`PingProbe`, ESP-IDF-free) + `harness/tests/test_ws_ping.cpp`; the
client ping is on by default and `depthcharge-noping` is the control arm. The board prints a
`-- ping` line directly under `-- age`, because the pair is the diagnosis: **age high + rtt high
= our own undrained queue (A7 is the fix); age high + rtt low = lag upstream in the broadcaster
(A7 would not help).** That split is the thing no instrument here could make before.

- **Shipped with it, and it would have been a silent regression otherwise:** the silence
  recycle's clock moved from byte-arrival to **data**-arrival (`SupervisorInput::last_data_us`,
  stamped in `on_chunk`). A ping manufactures a pong every 10 s, and a pong is bytes — so the
  board's own control traffic would have kept `kSilenceRecycleUs` from ever firing against a
  peer that answers pongs and publishes nothing, which is exactly the 2026-08-16 00:12 stall.
  ARCHITECTURE §9 carries the general rule.
- **~~Still owed: the Anvil half of A2~~ — CLOSED 2026-08-16, the same day.** This said the
  ordering was read from source and never captured under induced backpressure, so the number was
  evidence rather than a guarantee. Anvil captured it — pong last of 149 frames, zero after it —
  and `PROTOCOL.md` §3 Keepalive now states it as contract. **D5 rests on a measured property.**
  Detail under A2 above.
- Bounds that do not go away: it is **blind to producer-side lag** by construction, and nothing
  branches on it — §6 #5 means a pong can never turn the panel green.

**~~M3 close-out~~** — transport brief closes, docs catch up, Anvil ask rewritten. DONE
2026-08-16 (`66e2f77`…`79c1867` plus the docs commit):
`docs/briefs/M3-closeout-transport-and-docs.md` is executed and its session log is the
hand-off. The old client's build arms are deleted, the rebuilt-window framework is the
default build, `WsSupervisor` recycles a silent socket after five minutes, and the transport
brief's DoD is ticked except the weak-node hour (D2).

**~~Own the websocket client~~** — SHIPPED. Client landed 2026-08-15 (`c52b268`), day-soak
proven, and the old client deleted from the tree 2026-08-16 (`66e2f77`). Brief:
`docs/briefs/M3-transport-own-the-websocket-client.md`, closed with its acceptance numbers in
ARCHITECTURE §9 (2026-08-16).
