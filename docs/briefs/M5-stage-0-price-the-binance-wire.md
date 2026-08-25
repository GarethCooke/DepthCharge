# M5 Stage 0 — price the Binance wire, and invent the oracle

**Track:** Agentic [A] · **Status:** ✅ Done (2026-08-24) · **Size:** one evening
**Executor:** Claude Code, **desk only. No board, no flash, no adapter, no `engine/` change, no golden.**

Lands as `docs/briefs/M5-stage-0-price-the-binance-wire.md`.

**This is the Kraken stage 0 again, plus one thing that stage did not have to do.** At Kraken the
oracle was handed over by the venue: a CRC32 over the top 10 levels that answers *is my book the
venue's book?* outright, and every later stage was built on it — the removal rule proven 4,878/4,878,
the truncation goldens chosen by measurement, the mismatch path driven by a synthetic. **Binance
publishes no checksum.** `U`/`u` bracketing detects a lost or misordered message and says nothing
about whether the book those messages built is correct; it can be 100% clean while the client is
wrong, which is precisely the failure mode M4 stage 0 caught at depth 100. So **inventing the oracle
is this evening's work, not a detail of it**, and until it exists M5 cannot have a golden worth
pinning.

**Read first**

| Source | Why |
| --- | --- |
| `ARCHITECTURE.md` §4 | The boundary contract. Nothing in it moves this evening; `GapReason` has been enough four askings running. |
| `ARCHITECTURE.md` §6 | Frozen. #3/#4 (integer ticks, verified representable) and #6 (no merge without replay coverage) are the two that bite here. |
| `ARCHITECTURE.md` §9 | The 2026-08-16 stage-0 rows, the coincidence-class row, the never-observed row, the mutation-verification row. |
| `docs/briefs/M4-stage-0-price-the-kraken-wire.md` | The template for this evening, and the **checklist item it left for whoever writes the Binance detector** — read that paragraph before section 4. |
| `harness/replay/NOTES-kraken.md` | § *What transfers to M5, and — more importantly — what does not.* |
| `docs/briefs/M4-stage-A-the-replay-dialect.md` | The `venue` metadata tag, the per-venue decoder contract, and *the reader learns the wire; the wire does not learn the reader.* |
| `docs/briefs/M4-stage-B1-the-adapter.md` | How a wire's number precision was proven rather than assumed (8,172 level entries, zero exponent notation). Section 7 repeats it here. |
| `docs/DESIGN.html` §08 strains **22** and **25** | Strain 22: adding a venue is four edits in two languages and **only three fail loudly**. Strain 25: the two-scanner duplication whose stated trigger is a third venue. This evening is the first test of both. |
| `tools/capture_kraken.py`, `tools/tracefile.py`, `tools/kraken_frame_economics.py` | The tools being generalised or cloned, and the rule for which. |

**Depends on:** M4 ✅ (2026-08-24). **Blocks:** M5's brief, which this stage exists to make writeable.

---

## Deliverables

### 0 · Before anything: does the venue answer at all

One `curl` against `https://data-api.binance.vision/api/v3/exchangeInfo?symbol=BTCUSDT` and one
short WebSocket connect to `wss://data-stream.binance.vision`. A 451 or a refused handshake from
this IP ends the evening at the top rather than after the tooling is written, and the answer goes in
the log either way. **Use the market-data-only hosts** (`data-stream.binance.vision`,
`data-api.binance.vision`), not `stream.binance.com` / `api.binance.com`: they serve public market
data only, require no key, and no API credential is to exist anywhere in this repository at any
point.

### 1 · `tools/capture_binance.py`

Same NDJSON contract as the other two capture tools: line 1 metadata carrying `venue: "binance"`
(additive tag, M4 stage A), then `{"rx_ns": <monotonic-ns>, "frame": <verbatim JSON>}` with frames
spliced in byte-preserved. Stdlib only. Reuse `tools/wsclient.py` **only if `capture_anvil.py`'s and
`capture_kraken.py`'s outputs stay byte-identical across the refactor** — check against a committed
trace of each. If either moves, clone and say so in the header with the reason.

Two capabilities neither existing tool has:

- **It must record REST responses in a trace format that has only ever carried WS frames.** The diff
  stream is ungradeable in replay without the snapshot it is bracketed against, so `dc_replay` must
  eventually be able to read one. **Propose the record shape; do not implement the reader.** Note
  which way the pressure runs: stage A's rule was that the reader learns the wire, but there is no
  wire here to learn — a REST body is a *fetch this client chose to make*, and the honest shape
  records the request as well as the response. Whether that is a `kind` field on the record, a
  second stream, or something else is this deliverable's recommendation and M5's decision.
- **Binance names streams in the URL path** (`/ws/<stream>` or `/stream?streams=a/b/c`) rather than
  by a subscribe message, so the tool may send no application frame at all. Record whether the
  combined-stream wrapper (`{"stream": ..., "data": ...}`) changes the frame shape, and capture it
  verbatim either way. The masking path exists for the close frame; keep it.

Arguments: `--symbol`, `--streams`, `--duration`, `--out`, `--snapshot-every`, `--limit`,
`--max-frames`.

### 2 · The captures

Four, ~90 s each. Full captures untracked; commit slices.

| # | symbol | streams | asks |
| --- | --- | --- | --- |
| 1 | `BTCUSDT` | `@depth@100ms` + `@depth20@100ms` | the candidate configuration, **and the oracle test** |
| 2 | `BTCUSDT` | `@depth` + `@depth20` (1000 ms) | what the slower tick costs and what it coalesces |
| 3 | a deliberately quiet pair | `@depth@100ms` + `@depth20@100ms` | the silence question — the Binance twin of Kraken's 26 s |
| 4 | `BTCUSDT` | `@depth@100ms`, **deliberate mid-capture reconnect** | what a resync looks like, captured on purpose rather than found |

Every capture opens with a REST snapshot and takes further ones at interval; capture 1 takes them
often enough to give section 4 a population rather than an anecdote.

**Captures 1–3 are clean windows and capture 4 is deliberately not** — keep them apart in the notes.
Stage 0's recipe (*liquid pair, shallowest depth, no healing event inside the committed window*)
governs anything that will later serve as a **guard**; capture 4 exists to show what the healing path
looks like before code is written against it, which is B2's rule — when a stage makes the client do
something new, capture what comes back before trusting the code that handles it. A trace containing
the system's own healing events measures recovery, not the defect.

**Rate limits, and the Kraken rule transfers with different numbers.** 300 connection attempts per
5 minutes per IP; 5 *incoming* messages per second on a connection; 1,024 streams per connection; a
connection to the stream host is valid for **24 hours** and is then closed. REST `/api/v3/depth`
costs IP weight by tier — 5 for `limit` ≤ 100, 25 to 500, 50 to 1,000, **250** for 1,001–5,000. Do
not loop reconnects and do not poll snapshots at `limit=5000` because the docs' own worked example
does; a ban costs the evening.

### 3 · Price them

Generalise `kraken_frame_economics.py` or clone it — same rule and same reason as deliverable 1.
Keep the `--verify` discipline: re-serialisation must be byte-exact or the byte counts are estimates
and must be labelled as such.

Per stream, per capture: count, total bytes, mean, share of wire, sustained KiB/s, levels per
message, and **levels changed per message**. Then the same two yardsticks stage 0 used at Kraken, so
the three venues are directly comparable: **30.8 KiB/s** (the board against Anvil at `depth=27`) and
**56 KiB/s** (the 23.6 h soak's worst measured hour). Headroom as a multiple.

**One column that is new and is the price of the oracle: what `@depth20` costs on top of `@depth`.**
If section 4 recommends the partial-depth oracle for the *board* and not only for the harness, this
number is what it costs there.

Inter-message gaps as a distribution with the worst gap named, per stream, per capture — and the
question capture 3 exists for: **does `@depth` emit on a tick when nothing changed, or does it go
silent?** Kraken's answer to the equivalent question is what the whole 2026-08-17 liveness ruling
rests on, and Binance's row in the venue table cannot be written without it.

### 4 · The oracle — the reason this stage is not just stage 0 again

Name all three, measure all three, **recommend one and take none**.

**(a) The venue's own partial-depth stream, on the same socket.** `@depth20@100ms` is the venue's
top-20 book, computed venue-side, stamped with `lastUpdateId`. That is the closest structural
analogue to Kraken's CRC32 this venue offers — an independent venue-published answer to *is my book
the venue's book?*, bounded to 20 levels a side. **Measure the thing it depends on: does a
partial-depth payload's `lastUpdateId` ever equal a diff event's `u` exactly, and in what fraction
of ticks?** Where it does, the comparison is exact; where it does not, the instant is
*unverifiable* — and B2 already built that discipline, so report it as `seen == matched + failed +
unverifiable` with a fourth outcome unable to appear quietly.

**(b) REST re-snapshot comparison.** Replay diffs to a captured snapshot's `lastUpdateId` and
compare. Exact when that id equals some event's `u`; **ambiguous when it lands strictly inside an
event's `[U, u]`**, because a coalesced event cannot be split. Measure how often each occurs.
**And carry M4 stage 0's own warning forward, which was written for this paragraph:** at Binance the
REST re-snapshot is the venue's *healing* event and it is **scheduled rather than incidental**, so a
detector built on it risks measuring recovery instead of the defect, and cannot be fixed by choosing
a clean window the way a Kraken resync could. That is an argument for (a) over (b), and it should be
stated as one rather than left implicit.

**(c) A second, independent implementation.** A Python reference book byte-diffed against the C++
adapter over the same trace — the differential-oracle shape Anvil already runs. Say plainly what it
proves and what it does not: it catches divergence between two books, and **cannot** catch a shared
misreading of the wire, which is exactly the class (a) and (b) exist to catch.

**The clause that transfers unchanged (ARCHITECTURE §9, 2026-08-16 stage 0): whatever the oracle is,
prove the trace catches a deliberately broken implementation before pinning it.** Concretely, three
mutants must go red and an honest control must go green — `qty: 0` not treated as removal; no
truncation to the rendered depth; bid and ask sides swapped. An oracle that cannot fail is not an
oracle, and a trace nobody has broken on purpose is a trace nobody has checked.

### 5 · `U`/`u`, and exactly what it is worth

Measured, not assumed, and reported as numbers:

- Is `U == prev_u + 1` held on every event in every capture, or does the venue skip? Count.
- Spot carries **no `pu`** (USD-M futures does), so the continuity test is one-sided. Confirm that
  against the captures rather than the documentation.
- Is the documented bracketing satisfiable on the first attempt — snapshot `lastUpdateId` inside the
  first buffered event's `[U, u]` — or does it need the retry the procedure allows for? How often?
- **Then the sentence M5's brief will open with: how many of section 4's three mutants does `U`/`u`
  bracketing catch on its own?** If the answer is zero, that is the finding, and it is the whole
  argument for the oracle.

### 6 · Two protocol facts that reach the firmware, and neither is the adapter's problem

Record both. **Fix neither** — they are M5 stage findings, not stage-0 work.

- **The server pings every 20 s and closes the socket if no pong arrives within 60 s.** **The pong
  path already exists and is right** — `WsTransport::on_ping` (`firmware/src/ws_transport.cpp:909`)
  writes a masked pong carrying the ping's payload straight back, from inside the parser callback,
  and gets two pings in one read right where a single *pong owed* flag would not. Its own comment
  already names the case: *a server that enforces pongs will close.* **So what is missing is not code,
  it is evidence.** No committed trace is known to contain a server ping, and B2's third blind-spot
  class is exactly *a corpus that has never contained a frame kind*. Binance is the first venue that
  certainly sends one and certainly enforces the deadline, so: confirm a ping frame is present in a
  committed capture, confirm the pong went back, and report the count — that turns a correct-looking
  path into a recorded one. **And check the capture tool itself:** if `capture_binance.py` reuses
  `tools/wsclient.py`, that Python client must answer pings too, or a 90-second capture dies at
  sixty.
- **A connection is valid for 24 hours and is then closed by the venue.** M3's soak was 23.6 h and
  would have missed it by twenty-four minutes. The supervisor already reconnects, so this is probably
  benign — but it is the first *scheduled* disconnect this project has met, and it means M5's soak
  must exceed 24 h or it proves nothing about it. Say so where the soak will be specified.

### 7 · The numbers, proven the way B1 proved Kraken's

`tick_size` and `qty_step` come from `/api/v3/exchangeInfo` — `PRICE_FILTER.tickSize` and
`LOT_SIZE.stepSize`. §6 requires the adapter to verify every wire price is exactly representable at
the declared scale, never to round silently. So do what B1 did: **count level entries across the
captures, count how many carry exactly the declared precision, and count exponent notation.** One
table, and it decides whether integer ticks are safe at this venue or whether the adapter needs a
path Kraken never did.

Second number in the same section: **`kMaxSnapshotLevels` is 256 per side.** REST `/api/v3/depth`
offers up to 5,000. Recommend the `limit` the board should use against that ceiling and against the
weight tiers, and note that Kraken's offered-depth whitelist is pinned by `static_assert` — the same
treatment is available here and costs nothing.

### 8 · Writeback

`harness/replay/NOTES-binance.md`, including **every place the wire disagrees with this brief** —
this document is a summary of documentation and the notes are a record of measurement, and where they
disagree the notes win. Session log below. A line on the ROADMAP M5 row saying stage 0 is done and
what it settled. And two strains get answers rather than actions: **strain 25** — does Binance need a
third JSON scanner, which is its stated extraction trigger — and **strain 22** — adding a venue is
four edits in two languages and only three fail loudly; this evening adds the `tools/tracefile.py`
`VENUES` row, so it is the first and cheapest test of whether that asymmetry costs anything.

---

## Constraints

- **All of §6, frozen.** Nothing in `engine/`, nothing in `firmware/`, no adapter, no golden, no
  `dc_replay` reader change. Reading firmware source for section 6 is reading, not editing.
- Python lives in `tools/` only, stdlib only.
- `capture_anvil.py`'s and `capture_kraken.py`'s outputs do not move. If a refactor moves either, the
  refactor is wrong.
- Full captures stay untracked; commit slices. Pin tables are **add-rows-only**.
- Market-data hosts only. **No API key in this repository, ever** — nothing here needs one.
- Host build stays green (`cmake --build --preset host`, then `ctest --preset host`, from PowerShell —
  the Bash sandbox breaks compilers silently on this machine).
- **Commit only when asked.** Report what changed and what it measured.
- **Do not decide the easy-mode question.** ROADMAP's M5 line says *partial-depth easy mode, then full
  diff stream*, written before M4 existed. Price both, report what the easy mode still buys now that
  delta application, resync and a healing path are all built, and **leave the decision to the owner**.
  Section 4(a) may well have converted the easy mode from a stepping stone into the oracle, which is a
  different argument for keeping it and should be made as one.
- **Already rejected, do not re-propose:** synthesising a snapshot into a slice header to make a
  windowed trace gradeable. Named and refused at M4 stage A — a trace is wire truth, and a derived
  baseline would be the first thing in `harness/replay/` that no venue ever sent.

## Known unknowns — resolve and record

Whether the endpoints answer from this IP at all. Whether `@depth20`'s `lastUpdateId` ever coincides
exactly with a diff `u`, and how often. Whether the combined-stream wrapper is worth its bytes against
one stream per socket. Whether `@depth` goes silent on an unchanged tick, and what that makes Binance's
row in the venue table. Whether the trace format can carry a REST fetch without a contract change.
Whether the owned WS client answers a server ping. Whether Binance's number precision holds on the
wire the way Kraken's did. Whether a third scanner is needed, or whether Binance's grammar is close
enough to one of the two that already exist.

## Definition of done

- ☑ Endpoint reachability answered in one line before any tool is written.
- ☑ `tools/capture_binance.py` exists, stdlib-only, writes the same NDJSON contract with
      `venue: "binance"`; the other two tools' outputs byte-identical to before.
- ☑ Four captures taken; slices committed under `harness/replay/`.
- ☑ Each capture priced per stream: bytes, share, KiB/s, levels/message, **levels changed/message**,
      gap distribution with worst gap. Headroom against 30.8 and 56 KiB/s.
- ☑ The `@depth20`-on-top-of-`@depth` cost stated as a number.
- ☑ **All three oracle candidates measured; one recommended; none implemented.** Recommendation
      states what it can and cannot see, in the shape B2 used for the CRC's top-10 reach.
- ☑ **Three mutants run against the recommended oracle and red; the honest control green.**
- ☑ `U`/`u` continuity measured, and the mutant-catch count for `U`/`u` alone reported.
- ☑ Ping/pong: the transport's actual behaviour established from source and a server ping present in
      a committed capture — or its absence stated.
- ☑ Number precision counted; `limit` recommendation made against `kMaxSnapshotLevels` = 256.
- ☑ REST-in-the-trace record shape **proposed**, with the reader change named and not made.
- ☑ `NOTES-binance.md` written, including every disagreement with this brief.
- ☑ Strain 25 and strain 22 answered in the log.
- ☑ Host build and ctest green; session log; ROADMAP M5 line.

## Out of scope

The adapter. Delta application, the `U`/`u` state machine, gap recovery, any REST client on the
target. Any `engine/`, `firmware/`, `dc_replay` or golden change. The board — nothing is flashed and
nothing is soaked this evening. **M5's own brief, which this stage exists to make writeable.** The
scanner extraction (strain 25) — answered, not done. The runtime venue toggle (M7). The client ping
(M6). M4's carried-forward bench residues — D1, D2, D7's scope trace — which are bench items and this
is a desk evening.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     measured figures / evidence · exact next step. -->

### 2026-08-24 · Claude Opus 5 (1M) · stage 0 complete, desk only

**Done.** All fourteen definition-of-done boxes. Endpoints answered first try (REST 200,
WS 101). `tools/capture_binance.py`, `tools/binance_frame_economics.py`,
`tools/binance_oracle.py` and `tools/binance_pins.py` are new; `wsclient.py`,
`tracefile.py`, `slice_trace.py` and `CMakeLists.txt` changed additively. Six captures
plus two wrapper probes; five slices committed. Host build and `ctest` green at **35/35**,
including two new tests. **Nothing in `engine/`, `firmware/`, `dc_replay` or any golden was
touched, and no adapter was written.** Full measurements: `harness/replay/NOTES-binance.md`.

**The four decisions with reasons, all four now in `ARCHITECTURE.md` §9.**

1. **The oracle is (a), the venue's own `@depth20` stream** — recommended, not built.
   `lastUpdateId` coincides exactly with a diff `u` on **899/899** payloads, so the
   comparison is exact rather than probabilistic; a correct client grades **884/884**.
   *Why not (b):* the REST re-snapshot **is** the venue's healing event and is scheduled,
   so a detector on it measures recovery, and unlike a Kraken resync no clean window can
   dodge it. *Why not (c):* two implementations that share a misreading agree perfectly and
   are both wrong — the exact class (a) catches 884/884. *What (a) cannot see, stated now:*
   it is **blind below rank 20** while the panel draws 25 — the inverse of Kraken's
   CRC-10-under-25 but the same gap — and `@depth20` is the deepest spot tier, so it cannot
   be closed by subscribing deeper.

2. **`U`/`u` catches 0 of 3 book mutants** and is not a book oracle. It *is* an exact
   transport-loss detector: it caught the deliberate reconnect's 2,204 missed updates on
   the first event after it. *Why it matters:* the shallow-seed defect below was invisible
   to it across 890/890 clean events.

3. **`limit=1000` for the seed, plus a re-snapshot schedule.** *Why:* the seeded window is
   a **price** window, not a level count — `limit=100` covers ~$16 a side and BTCUSDT moved
   **$29.85 in 90 s**, so the best ask left it on 406 of 901 ticks. `kMaxSnapshotLevels =
   256` still fails **33/884** inside 90 s. Depth alone cannot fix it at any tier, because a
   resting order predating the seed is never restated and so never enters the diff stream.

4. **Scale is a constant 8 decimals, not `tickSize`.** *Why:* 202,012/202,012 level entries
   carry exactly 8 fractional digits on both a 2 dp and a 3 dp tick; a scale derived from
   `tickSize` would be wrong by 10^6 on BTCUSDT. `tickSize`/`stepSize` become §6 validators,
   not the source of the scale. Zero exponents, zero bare floats: invariant #3 needs no new
   path and the adapter needs no float and no `Decimal`.

**The finding that outranks the brief's own agenda.** **Binance declares no liveness signal.**
Both depth streams are change-driven — 30 messages in 90 s on ATOMEUR, worst gap **10,503.8 ms**
on a stream named `@100ms` — and there is no `summary`, no `heartbeat`, no equivalent. This is
the question the 2026-08-23 B3 row explicitly required M5 to answer before an adapter is
written, and the answer is *none*, so the 2026-08-17 grey ruling has nothing to arm on and
goes blind rather than degrading. The venue's 20 s server PING is a real unsolicited liveness
signal, already answered correctly, but it sits one layer below §4's `FeedEvent` boundary.
**M5 must decide: publish ping arrival across that boundary, or grey on Binance by socket
death alone.** The M6 client ping does not substitute — it proves the round trip when we ask,
and the point of a liveness signal is that it arrives when we do not.

**Two mistakes made and corrected this evening, both recorded because the shape recurs.**
*(i)* `binance_oracle.py` first compared a REST body against the **live** book, when the
fetch takes ~1.0–1.5 s and the stream has moved 10–15 events past the id it names. It graded
nothing and printed **GREEN against every mutant** — the *oracle that cannot fail*, produced
by an off-by-a-second. Fixed by grading against the book image at that id; `VACUOUS (0
graded)` is now a distinct verdict so the state cannot hide again. *(ii)* `--from-baseline`
cut committed slices **at** the baseline record instead of merely **containing** one, which
discarded the diffs bridging the snapshot forward and took the oracle from 884/884 green to
**250/250 red** on a slice of the same capture. Reconcile **by id, never by position** — which
is what the venue's own buffered procedure says. The flag is now `--require-baseline`, a guard.

**A latent bug in shared code, found by this venue.** `wsclient._fill` caught
`socket.timeout` but not `ssl.SSLWantReadError` — what a TLS socket raises when the deadline
passes holding a **partial TLS record**. Capture 2 died after **4 frames**; after the one-line
fix it took **180**. Anvil summarises at 500 ms and Kraken heartbeats at 1 Hz, so neither ever
left a 1.0 s poll mid-record: ARCHITECTURE §9's never-observed class, on the shared client.
**Byte-identity re-proved after every change** by the loopback-replay method `wsclient.py`'s
header documents — `capture_anvil.py` identical over 1,514 lines, `capture_kraken.py` over
1,601, both including runs with a server PING injected mid-stream; and 1,011 lines of output
from the three economics tools, `gap_stats.py` and both selfchecks over all eleven committed
traces, identical.

**Strain 25 — answered: NO third JSON scanner is needed.** Measured over the captures,
Binance's wire contains **zero float tokens, zero exponent tokens, zero string escapes, zero
`true`/`false`/`null` literals, and nests at most 4 deep**; every number is a plain integer
(largest ~9.9×10^10, comfortably inside uint64) and every price and quantity is an unescaped
string. That is a **strict subset** of what both existing scanners already handle, so the
card's stated extraction trigger — *a third venue* — arrives and **does not fire**. The
duplication stands, and the reason to extract must now come from somewhere other than
Binance. Caveat recorded: this is measured over depth streams and `/api/v3/depth` bodies,
which share the grammar; `/api/v3/exchangeInfo` is much richer (booleans, nested filter
objects) but is a one-shot config fetch and need not be parsed on the target at all.

**Strain 22 — answered, and the card understates it in one direction while overstating it in
another.** *Overstated:* the predicted symptom was "a Python tool refuses a trace the C++
reader accepts". The reverse happened — the Python tools now read Binance and
`venue.hpp` still has **zero** occurrences of it — and that is benign, because it is the same
deliberate Python-first staging Kraken went through between stage 0 and stage A. *Understated,
and this is the part worth carrying:* the expensive edit was not the `VENUES` row at all. It
was the **six venue-conditional predicates** in `tracefile.py` (`is_book_event`, `is_snapshot`,
`rebaselines`, `is_trade`, `is_liveness`, `record_kind`), every one of which was written as
`if venue == "anvil": … else: <Kraken's answer>`. A third venue inherits Kraken's semantics by
**fallthrough**, and a missing branch does not fail loudly or quietly — it returns a confident
wrong answer. Verified: without its branch, `is_book_event("binance", <a depthUpdate>)` returns
**False**, so a pricing tool reports zero book events on a full capture. So the accurate form
of the card is *"adding a venue is four edits in two languages, three of which fail loudly, plus
six predicates whose default is the previous venue's answer and which fail silently"*, and the
structural fix available is to make the venue dispatch exhaustive rather than defaulted.

**REST-in-the-trace: shape proposed, reader named and not made.** One optional `kind` key
before `frame` — `"rest"` and `"control"` — where `dir` already sits; absent means what every
existing record already is, so the four Anvil and six Kraken traces are untouched.
`read_capture` skips kinded records unless a caller opts in. The honest note is that the
pressure runs the other way from stage A's rule: there is no wire to learn, because a REST body
is *a fetch this client chose to make*, so the record carries `req` as well — a transcript plus
a question. The reader that must eventually exist is a `binance` row in
`harness/include/dc_harness/venue.hpp` plus a decoder; **both deliberately not written.**

**Left to the owner, as instructed: the easy-mode question.** Both configurations are priced
(`@depth` alone 9.47 KiB/s; `@depth` + `@depth20` 22.84 KiB/s at the 100 ms tick, 7.84 KiB/s
at 1000 ms). What has changed since ROADMAP's line was written is that **section 4(a) may have
converted the easy mode from a stepping stone into the oracle** — and that is a different
argument for keeping `@depth20` than "it is simpler to build first". Against it: it is +141%
wire and the *majority* of the stream, 7.13× on a quiet pair, and blind below rank 20.

**Exact next step.** Write M5's brief, which this stage existed to make writeable. It has three
inputs it did not have this morning: the oracle recommendation with its measured reach and
price; the **liveness decision** (ping across the §4 boundary, or grey by socket death) which is
now a prerequisite rather than a detail; and the `limit=1000`-plus-re-snapshot-schedule finding,
whose interval M5 must derive from the walk rate rather than pick. M5's soak must exceed **24 h**
or it proves nothing about the venue's scheduled disconnect.

### 2026-08-24 (later) · Claude Opus 5 (1M) · the four review items, worked

Reviewed at `docs/briefs/SEND-TO-depthcharge-cc-M5-stage-0-corrections.md`. All four done;
build and `ctest` green at **35/35**; still nothing committed.

**1 · The 150/151 — found, and it is the first branch: a capture-edge artefact.** The odd
payload is `lastUpdateId = 99076452055` at line 306 of
`binance_btcusdt_d100ms_20260824.ndjson` — **the last record in the file**, record 301 of
301 — and its id is **beyond the highest diff `u` the slice contains** (99076452014). The
slice holds **151 partials against 150 diffs**: `slice_trace.py` cuts on an `rx_ns` window
boundary, and that boundary fell between the partial and the diff event ending on the same
update. The full 90 s capture over the same stretch is **901/901 with none odd**.

*It was not resolved by dropping the capture*, which the review explicitly forbade and which
would have been the §9 coincidence-class error in its purest form. The claim now has a stated
boundary condition instead of a footnote contradicting it: **exact on every payload of every
complete capture (899/899, 901/901, 90/90, 29/29) — and a window cut on a time boundary can
still end between the two streams' publications of one update boundary.** `unverifiable` is
therefore a **live** bucket, and it did its job: the oracle reported that payload as
`never-reached-before-capture-ended` rather than matching or failing it silently. The weaker
sentence is now in the notes, the §9 row, the ROADMAP line and the DESIGN strip.

**2 · The silent skip is gone.** The `file(GLOB)` is replaced by the **seven slice names
spelled out**, each checked with `EXISTS` at configure time and a `FATAL_ERROR` naming the
missing file — *an unpinned trace is a failure, not a skip*, the rule this repo already holds
from the Kraken pin table. Mutation-verified: moving one slice aside makes `cmake` exit 1
with `Committed Binance slice missing: binance_atomeur_deepseed_20260824.ndjson`, and
restoring it configures clean. The file list is now part of what is pinned.

**3 · Three witnesses, and the quiet pair answered a question nobody asked.** Two more
deep-seeded captures:

| witness | coincidence | honest | mutants |
| --- | ---: | ---: | --- |
| `btcusdt_deepseed` | 899/899 | 884/884 | 3 of 3 RED |
| `btcusdt_deepseed2` (later window, same evening) | **901/901** | **886/886** | **3 of 3 RED** |
| `atomeur_deepseed` (quiet pair) | 8/8 | 8/8 | **2 of 3** |

`deepseed2` is what actually de-single-sources the suite — an independent window reproducing
every headline figure. **The quiet pair did something better than confirm.** It was taken on
the review's reasoning that a boundary mismatch would show on a thin book; instead it showed
that **on a book that never churns the bounded-window mutant is not caught, because it is not
a mutant there.** ATOMEUR moved so little in 90 s (8 graded ticks) that truncating to 25
levels a side never removed a level later needed, so the mutant's book is byte-identical to
the honest one at every graded tick.

Added it to `--check` naively and the suite would have gained a witness that could only ever
agree. So `--check` now reports **`NOT EXERCISABLE`** — neither pass nor failure — and
**fails outright unless some trace exercises all three**. *A capture too quiet to break is
indistinguishable from an oracle too weak to notice.* Both new states are mutation-verified,
including the full-coverage guard (ATOMEUR alone → FAILED).

**One correction inside the correction, recorded because it is the same species twice in one
evening.** Exercisability was first detected by a heuristic — *did the book ever exceed the
window?* — which answered **EXERCISABLE** for ATOMEUR and was **wrong**: the seed held more
than 25 levels, but none below rank 20 ever rose into view, so the truncation changed nothing
that was ever graded. It is now measured directly, by comparing the mutant's graded books
against the honest one's. A heuristic standing in for a comparison is what produced the
VACUOUS bug too.

Pinned in the order asked: **mutants → `--selfcheck` → `--pin`**. The selfcheck correctly
called the two new slices `NOT PINNED` before they were added, and **add-rows-only holds —
no pre-existing figure moved** (verified field by field against the five earlier rows). Seven
slices, 70 pinned figures, 3.2 MB.

**4 · The §9 reach sentence corrected, in the project's favour.** It is not "the inverse of"
Kraken's gap; it is the **same gap and smaller**, and both numbers are now in the row:
**Kraken's CRC validates 10 of the 25 rows drawn; Binance's partial stream validates 20 of
25.** Stated so a later session does not open an evening to close a hole already narrower
than the one shipping since M4.

**Line endings, before any commit.** `core.autocrlf=true` here and the repo stores LF, so a
CRLF working copy is correct — but my edits had left the tree **inconsistent**:
`ARCHITECTURE.md` genuinely **mixed** (308 CRLF + 5 LF), `CMakeLists.txt` CRLF, and five
files converted to pure LF by Python writes. All seven are now **uniform CRLF**, matching a
fresh checkout. `git diff --stat` is content-only (`ROADMAP.md | 2 +-`), so the
commit-per-claim split is reviewable. The `.ndjson` slices are **untouched and still LF** —
`.gitattributes` marks them `-text` because they are byte-compared ground truth, and the
normalisation pass deliberately never saw them (it iterated tracked-modified files only).

**Exact next step, unchanged.** Write M5's brief. The liveness decision (publish the venue's
20 s PING across the §4 boundary, or grey by socket death alone) is its first input, not a
detail; `limit=1000` plus a re-snapshot schedule whose interval M5 must derive from the walk
rate is its second; and M5's soak must exceed **24 h** or it proves nothing about the venue's
scheduled disconnect.
