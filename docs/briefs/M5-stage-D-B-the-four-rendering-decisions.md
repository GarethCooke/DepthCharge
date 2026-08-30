# M5 Stage D-B — the four rendering decisions

**Track:** Bench [owner-driven, judged by eye] · **Status:** All four decisions taken 2026-08-30; docs half done (strains 24, 26, 28 · two §9 rows); ROADMAP line and D-C outstanding · **Size:** one evening
**Written:** 2026-08-29 by the desk seat, because D-B had no work order and D-C is behind it.

**Every decision in this brief is taken by looking at the panel.** Nothing here can be settled by a
host test, and a session that produces one has answered a different question. The desk's job was to
put a live ladder in front of the owner and get out of the way; that is done — the board holds a
Binance book, `live=1 rows=54/54`, `bracket ok=4 FAIL=0`, `oversize=0`, and on the cleanest capture
`resync_req=0` for 140 s with both cores 95% idle.

**Read first**

| Source | Why |
| --- | --- |
| `M5-stage-C-…md` § *Owed by stage D*, **row 1** | The four questions, verbatim, and the ruling that they are D's. **Do not re-argue that** — C's scoping ruling settled it from M4 twice, and item 4 leaving C is the only reason C fitted in an evening. |
| `docs/DESIGN.html` strain 24 | The unvalidated-rows question, and why the tripwire is the subscribed depth. |
| `docs/DESIGN.html` strain 26, 28 | Remedy (a) — what is withheld and why — and what a re-seed in flight has to render. |
| `firmware/src/ladder_render.hpp` | Where every one of these decisions is expressed, and the `Ink` vocabulary they must be expressed in. |
| `M4-closing-bench-sitting.md` | The precedent for the sitting: M4's two rendering decisions were taken the same way, and one — the ramp failing at six-bit depth — was legible only at an oblique angle. |

**Depends on:** D-A2 ✅ (`83c0bf6`). **Blocks:** D-C, and only D-C.

---

## The four questions

### 1 · What a silent feed renders

The stageable half of §2's parity case. A misspelled stream returns HTTP 101, answers pings and
delivers nothing for ever — committed as `binance_btcusdt_DEFECT_silent_stream_20260826.ndjson`.

**The board that produces it is built and ready to flash: `-e depthcharge-binance-silent`**, or the
VS Code task *DepthCharge: Flash + Monitor (Binance SILENT-STREAM defect, USB)*. Three things about
it are worth knowing before it goes on:

- **It is one character**, and deliberately the committed capture's exact spelling. The suffix
  `static_assert` in `binance_endpoint.hpp` is **inverted on this arm rather than switched off**, so
  the defect build cannot silently become a *different* typo — which might not be silent at all.
- **It says so on every SOAK line**, not just at boot — `DC_SOAK_SILENT_TAG`, the same mechanism
  as `DC_SOAK_TEST_TAG` and for the reason that comment gives: captures are attached without
  resetting the board, so a boot-only banner is invisible to exactly the captures that matter. This
  image is indistinguishable from a broken board by design, and a photograph of it is
  indistinguishable from a fault report.
- **Flash it last.** It leaves a board that never goes live, so questions 2–4 want the working
  image and this one wants the end of the sitting.

M4 had to invent `DC_TEST_MUTE_LIVENESS` for the equivalent case because both network-side methods
deauthenticated. Nothing like that is needed here: the venue stages the defect for us, and no
test-only code enters the shipping image.

Remedy (a) already prevents the forbidden output: no `Snapshot` is published, so the ladder cannot
go live over a feed that has never spoken. **The question is what it should LOOK like**, and
specifically whether it is distinguishable from an ordinary pre-seed grey. It probably should not
be — a client cannot tell a silent stream from a slow one — but that is a decision, and if the
answer is "identical" it should be recorded as a decision rather than left as an absence.

### 2 · What a re-seed in flight renders

`DisplaySnapshot::reseed` exists and D-A2 left it deliberately unreachable. **D-A3 advances it to
`InFlight`; this stage decides what `InFlight` draws.** The two are separable in that order and not
the other, so if D-A3 has not run, decide it here from the mock and let D-A3 wire it.

The honest options, and none is obviously right: the ladder keeps its colour with a marker (it is
still correct — a re-seed fires on a book that has not gone wrong); the ladder greys (safe, but
greys a correct book for the length of a fetch measured at **3.1–5.6 s** on this board); or the
header alone changes. **B2's adoptability measurement is what makes this real** — 0 of 7 adoptable
at `limit=1000` on the liquid pair — so a re-seed is not rare.

### 3 · Whether remedy (a)'s grey *reads* right

The one question that is purely visual. The board now spends its first ~20 s grey while the seed is
fetched and bracketed, on every boot and every reconnect. **Does that read as "not trusted yet" or
as "broken"?** At desk distance, at 224 brightness, with the header carrying a stale reason.

D-A1 recorded the grey as an observation and was explicitly forbidden from judging it. This is where
it is judged.

### 4 · Strain 24's unvalidated levels

Binance publishes **no checksum**, so `venue::kValidatedDepth` is 0 and the SOAK line already says
so in words: *"binance publishes no checksum, so NO rendered row on this build was ever externally
confirmed."*

The card has been dormant while the subscribed depth stayed at or below 27. **It is not dormant
here**: the seed is `limit=1000` and the emitted window is 256, so the rendered 25 rows a side are
drawn from a book far deeper than anything confirmed. The cheap resolution named on the card is that
the panel shows unvalidated levels and that is fine, **said out loud once rather than assumed**. The
expensive one is a per-row marker `DisplaySnapshot` cannot currently express — the same +8 bytes on
the struct and +24 in the mailbox the price-axis window needs, priced at ROADMAP **D6**.

---

## What the bench needs, and what it must not do

- **Flash `-e depthcharge-binance`** for questions 2–4, then `-e depthcharge-binance-silent`
  **last** for question 1 (see `hardware/BRINGUP.md` — `upload_speed=921600` fails mid-write on this
  desk; use 115200). Re-flash the working image before anything else is judged.
- **Photograph each decision.** M4's precedent: the ramp failure was legible only at an oblique
  angle, so a decision taken from memory of the panel is a decision taken from the wrong evidence.
- **Do not tune the transport.** If the ladder misbehaves in a way that is not a rendering question,
  it belongs to D-A3 or D-C. The temptation this evening is to fix what is seen; the cost is that
  the four decisions do not get taken and D-C inherits them.
- **`sizeof(DisplaySnapshot)` stays 1,168** unless question 4 goes the expensive way, in which case
  it is a D6 decision and not this stage's.

## Known unknowns — resolve and record

Whether question 2 can be decided before D-A3 wires `InFlight`. Whether the ~20 s boot grey is
acceptable or wants a distinct "seeding" appearance. Whether question 4's cheap resolution survives
being looked at.

## Definition of done

- ☑ Question 1 decided, with `-e depthcharge-binance-silent` flashed and photographed.
- ☑ Question 2 decided, and stated in a form D-A3 can implement without re-deciding.
- ☑ Question 3 decided by eye at desk distance, photographed.
- ☑ Question 4 decided; if cheap, the sentence is written down where a reader will meet it.
- ☑ Any decision with architectural weight to `ARCHITECTURE.md` §9; `docs/DESIGN.html` where a card
      moves — strains 24, 26 and 28 all have a D-B half.
- ☐ ctest green; session log · ROADMAP; split proposed; nothing committed until approved.

## Out of scope

The re-seed **mechanism** and its memory, and the liveness ping wire — **D-A3**. The soak — **D-C**.
`worst_frame`: **closed 2026-08-29** — it was the wrong instrument rather than a regression. As a
distribution the modal frame is 1–2.5 ms and p99 is 10–25 ms, with 10 of 1,808 slow enough to cost
pipe slots; the bare maximum was reporting 0.2% of the population as though it described the feed
path. Not fetch-correlated, and it does not touch the render path. PSRAM slab scanning is the named,
untested residue (four frames in 1,808 over 50 ms) and belongs to **D-C**, which watches
`slow(>25ms)` rather than the maximum.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->

### 2026-08-29 · Opus 5 · the defect image, built but not flashed

**Done.** Question 1's board exists: `[env:depthcharge-binance-silent]` builds the misspelled-stream
image, with a VS Code task beside the other seven. **The working Binance image is still on the
board** — that was the instruction, and the defect image is for the end of the sitting because it
leaves a board that never goes live. All eight environments build; host suite 50/50.

**Decisions, with why.**

1. **The suffix `static_assert` is INVERTED on the defect arm rather than skipped.** That assertion
   exists to stop the path drifting by one character; switching it off for the one build whose path
   *is* a deliberate typo would remove the only thing separating the defect we meant from some other
   typo — which might not be silent at all. Both builds stay pinned, to different pins.
   **Mutation-verified:** changing the arm to `@depth@1000ms` is rejected at compile time.
2. **The arm `extends` the Binance env rather than copying it.** It is not a fourth venue beside the
   other three — it is the Binance build *plus one flag*, and a copied `build_src_filter` would
   silently stop matching the moment the parent gained anything, while still compiling. Verified
   against the resolved configuration: every option identical, `build_flags` differing by exactly
   `-D DC_BINANCE_SILENT_STREAM=1`.
3. **The image marks itself on every SOAK line, not only at boot** (`DC_SOAK_SILENT_TAG`). The boot
   banner was written first and was the weaker half: `DC_SOAK_TEST_TAG`'s own comment already
   records why, from the run of 2026-08-20 — captures are attached *without* resetting the board, so
   a boot-only marker is invisible to exactly the captures that matter. It bites harder here than it
   does for the mute tag, because a muted-liveness capture still looks like a working board and a
   **silent-stream capture looks like a fault**. Verified with `strings`: the marker is in the
   defect binary and absent from the other seven, not merely unprinted.

**Not done, deliberately.** Nothing was flashed and none of the four questions was decided — all
four are judged by eye and are the owner's.

**Exact next step.** The sitting itself: flash `-e depthcharge-binance` for questions 2–4, then
`-e depthcharge-binance-silent` **last** for question 1, and re-flash the working image afterwards.


### 2026-08-30 · Opus 5 · the sitting: all four taken at the panel

**Done.** The four questions are decided, by eye, at the board — 2–4 on `-e depthcharge-binance` and
question 1 on `-e depthcharge-binance-silent` flashed last, as the brief required. **No code changed
this session**, and that is the shape of the result rather than a shortfall: three of the four are
decisions to leave the renderer exactly as it stands, and the fourth (question 2) is an instruction
to D-A3 rather than an edit here.

**Decisions, with why.** Taken in the order **1 · 3 · 2 · 4**, because 1 and 3 are one judgement seen
twice and reading them apart hides that.

**1 · A silent feed renders as the ordinary pre-seed grey — IDENTICAL, and that is correct.** The
defect image was flashed last and photographed, and the panel it produces is not to be made
distinguishable from a slow venue's. **Why: a client cannot tell a silent stream from a slow one, so
a panel that distinguished them would be claiming knowledge the firmware does not have.** Remedy (a)
already prevents the forbidden output — no `Snapshot` is published, so the ladder cannot go live over
a feed that has never spoken — and what this settles is that nothing further is owed on top of it.
The brief asked for the answer to be **recorded as a decision rather than left as an absence**, and
that is precisely what this line buys: the sameness is *chosen*, so a later session that "improves"
it by giving a silent feed its own appearance is **reversing a decision, not filling a gap**.

**3 · Remedy (a)'s grey READS RIGHT.** At desk distance, at 224 brightness, with the header carrying
the stale reason, the ~20 s boot grey reads as *not trusted yet* and not as *broken*. It stands
unchanged, and the known unknown **"does the boot grey want a distinct seeding appearance"** closes
**no**. D-A1 recorded the grey and was forbidden to judge it; this is the judgement.

> **3 is what makes 1 safe, and the two must be read together.** Question 1's answer is only
> acceptable *because* the grey a silent feed shows is a grey that reads as withholding. Had 3 gone
> the other way, "identical" would have meant a panel that looks broken on every ordinary boot as
> well — the same pixels, a different verdict. Anyone reopening either question inherits both.

**2 · A re-seed in flight changes the HEADER ONLY. The ladder keeps its colour.** For D-A3, stated
so it need not be re-decided: on `ReseedState::InFlight` the **live palette stays selected** and
**every ladder `Ink` is unchanged** — the only difference from `None` is inside `draw_header`. Grey
is not used. **Why:** a re-seed fires on a book that has *not* gone wrong — the trigger is coverage
falling below `kBinanceReseedCoverLevels`, not a fault — so greying it would have the panel lie in
the safe direction for the **3.1–5.6 s** a fetch measures on this board, and B2's adoptability
figure (**0 of 7 adoptable at `limit=1000`** on the liquid pair) says that is not a rare event.
Between the two surviving options, header-only wins because **the header is already where this
renderer puts every statement about trust** — `reason_text` in the value slot — so the marker joins
a vocabulary that exists instead of opening a second one on the ladder, where invariant-grade
meaning (colour = side and rank) is already spoken for.

**The header slot is PINNED: the marker takes the SYMBOL's.** Decided here rather than left to
D-A3, because a slot left open is a slot D-A3 would settle without the panel in front of it, and
this stage exists precisely to take the decisions that need the panel. The standing priority is
**VALUE > AGE > SYMBOL**, and it selects the symbol on its own terms: the value slot holds the last
price and **a live book during a re-seed still has one** — the whole point of question 2's answer is
that the book has not gone wrong — while the age is the one number a person watching the ladder
cannot infer from the ladder (M4 stage D, A4's argument, unchanged). The symbol is the claimant that
already yields to both, and `draw_header`'s own comment gives the reason it is cheapest to spend:
*"a header that overlaps is worse than one missing an id nobody is reading."* This object shows
exactly one instrument, so its id is the least informative eight characters on the panel, and a
re-seed marker is strictly more informative than what it displaces for the **3.1–5.6 s** it
displaces it.

**Carried to D-A3 as constraints, not preferences.** The marker is **at most eight characters** and
is asserted against the real header width in `test_ladder_render.cpp`, the way every `reason_text`
string already is — that test is what stops a longer word being added without the desk saying so.
It yields exactly as the symbol yields today, so a header too narrow to fit it drops the marker
rather than overlapping the price or the age. And it is drawn in `Ink::Symbol`: the slot's ink, not
a new one, because a new `Ink` costs a stale-palette entry and a `static_assert`, and this marker
never appears on a grey panel — `InFlight` is a live-palette state by the decision above.

**4 · The panel shows unvalidated levels, and that is ACCEPTED — strain 24's cheap resolution, said
out loud once.** `venue::kValidatedDepth` is **0** on Binance as it is on Anvil, the seed is
`limit=1000`, the emitted window is 256, and the 25 rows a side are therefore drawn from a book far
deeper than anything a checksum ever confirmed — because on this venue nothing ever is. Accepted,
**and the per-row marker is deferred rather than rejected**. Why: the expensive resolution is not a
better-looking panel, it is a **different struct** — `+8` bytes on `DisplaySnapshot` and `+24` in the
mailbox, which is exactly what the price-axis window needs and is already priced at ROADMAP **D6**.
Buying those bytes here would buy them twice, or buy them for the smaller of the two claimants.
**`sizeof(DisplaySnapshot)` stays 1,168**, as the brief required of every outcome but the expensive
one. The firmware already says the sentence on every SOAK line in the venue's own words; **where a
*reader* meets it is DESIGN strain 24, and that write-down is the half still owed** — which is why
question 4's box above is the one left unticked.

**Evidence, and what this session could and could not confirm.** The decisions are the owner's,
taken at the panel from photographs at the bench per M4's precedent — a decision taken from memory of
the panel is taken from the wrong evidence — together with the serial capture of the defect image.

- **CONFIRMED, and an earlier reading in this entry was wrong.** Both captures of the sitting carry
  the tag on every SOAK line: `firmware/logs/device-monitor-260830-054547.log` (05:45:51–05:51:18)
  and `…-055239.log` (05:52:43–06:00:40). **The defect task already tees** — `monitor_filters =
  esp32_exception_decoder, time, log2file` sits in `[env]` at `platformio.ini:215` and
  `pio project config --json-output` resolves it identically for `depthcharge-binance-silent`, its
  parent and `[env]`. The comment above that line already explains why it is there rather than on
  the CLI: **`pio run` has no `-f` option**, so the `run -t upload -t monitor -f log2file` anyone
  would reach for fails outright. Nothing needed changing, and changing it would have broken the
  task. The earlier reading in this entry — that the task streamed to the terminal without teeing —
  came from searching `firmware/logs/` **while the sitting was still running**, before either
  capture had been flushed; the absence was a clock, not a defect.
- **What the captures establish, which is more than the tag.** Both are the defect image and
  **`live=1` appears in neither**: `rows=0/54 unknown=54 frames=0` throughout, grey for the full
  480 s of the second run. That is question 1's actual claim, evidenced — the feed that never
  speaks greys and stays grey. The forbidden output measured at B1 on this same misspelled stream
  (*"a populated 100-level ladder rendered ● LIVE"*) does not occur.
- **Question 1's PHOTOGRAPHIC attribution is UNCONFIRMED, and the five files are renamed to stop
  claiming otherwise.** They are now `bench-2026-08-30-D-B-grey-panel-q1.jpg` and
  `…-grey-panel-q3-{closeup,oblique,full-panel,desk-distance}.jpg`: which question each was offered
  for, which is known, and no build claim, which was not. Three legs. EXIF is stripped from all
  five, so their only timestamp is the mtime at which they were saved into `hardware/`; the four q3
  files landed at **05:50:43–05:50:45**, inside capture 1's window, five minutes into a defect run;
  and **there is no capture of `depthcharge-binance` anywhere on 2026-08-30**, so no photograph
  taken that morning could have been attributed by an overlapping capture even in principle. The
  old labels rested on sequence, which `bench-2026-08-30-D-B-panel-photographs.md` says is not
  attribution. **The `strings` check stands in their place**: over the six images built 2026-08-29
  19:24–19:25 the literal `SILENT-STREAM DEFECT IMAGE` appears **once**, in
  `depthcharge-binance-silent/firmware.bin`, and **zero** times in `depthcharge`,
  `depthcharge-binance`, `depthcharge-kraken`, `depthcharge-noping` and `depthcharge-ps` — §9's
  2026-08-29 row verified rather than assumed. Binary attribution plus a capture that never reaches
  `live=1` carries question 1; the photographs corroborate the *appearance*.
- **ONE PHOTOGRAPH ATTRIBUTES ITSELF, and it is question 3's.** `…-grey-panel-q3-oblique.jpg` reads
  **`SEQ GAP`** in the header — `GapReason::SeqGap`, `ladder_render.hpp:396`, and the only one of
  the six `reason_text` strings with a three-glyph second word. **The defect image cannot produce
  it**: every site that raises it (`binance_adapter.hpp:897, 952, 960, 1114`) sits downstream of a
  received frame or a buffered event, and both captures hold `msgs_in=0 frames=0 seqbreak=0
  bracket ok=0 FAIL=0` on every line with `STALE (resync)` as the sole stale transition. A feed
  that never speaks has nothing to gap. The state is a real one on the working image —
  `device-monitor-260828-175118.log`, no silent tag, reaches `bracket ok=0 FAIL=1` while still
  `live=0`, a seed-bracket failure before any publish, which is `SEQ GAP` over an empty grey
  ladder. **So question 3 was judged on the working image, and one frame proves it from its own
  pixels.** It also proves the save-times are transfer-times: that frame shows a state neither
  capture rendered, so it was taken outside both windows.
- **A trap worth naming: the header mostly is NOT a discriminator, and looked like one.** Three q3
  files show `RESYNC` and the q1 file shows `NO LINK`; that difference is a **socket gap, not a
  firmware identity** — both captures show the defect image passing through both states at the same
  point in each run (`sock 0→1` between `up=310s` and `up=320s`, twice, on ~302.9 s of rx silence).
  `RESYNC` is not a socket state at all: it is the `Book` constructor default (`book.hpp:330`) and
  is never assigned anywhere in the Binance path. **`SEQ GAP` discriminates and `RESYNC` does not**,
  and separating them took the reachability argument above rather than the appearance of a
  difference — which is the §9 row this sitting earned.

**Still owed on this stage.** The docs half — **strains 24, 26 and 28 each have a D-B half**, and
question 4's sentence needs its home on card 24; any of the four with architectural weight to
`ARCHITECTURE.md` §9. Then ctest, the ROADMAP line, and the split.

**A candidate §9 row, raised not written.** *Photographs are attributed by a capture that overlaps
them, and nothing else — not by which build was last flashed, and not by the file's name.* It has
the shape of a §9 rule: it generalises past this sitting, it was paid for (three sets of
photographs, two of them discarded because their provenance became uncertain), and the remedy is
mechanical — the tag already exists, and what is missing is a photograph carrying a timestamp that
can be laid against it. Left to the docs half rather than decided here, because §9 rows are the
owner's and this stage's four questions are answered without it.

**Exact next step.** The docs half: strain 26 records that (a)'s grey is judged and reads right;
strain 28 records header-only **with the symbol slot pinned** and hands D-A3 the eight-character
constraint rather than a choice; strain 24 takes question 4's sentence and the D6 deferral. Then
decide whether the two captures are worth committing to `hardware/` gzipped — the precedent is
`bench-2026-08-22-kraken-b3-soak.log.gz` in the same directory, and the argument for it is that
`bench-2026-08-30-D-B-panel-photographs.md` now cites captures that `/firmware/logs/` keeps out of
the repository. Nothing else in this stage is open.
