# M5 Stage D-B — the four rendering decisions

**Track:** Bench [owner-driven, judged by eye] · **Status:** Not started · **Size:** one evening
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
delivers nothing for ever — committed as `binance_btcusdt_DEFECT_silent_stream_20260826.ndjson` and
reproducible on the board by flashing with the stream name one character wrong.

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

- **Flash `-e depthcharge-binance`** (see `hardware/BRINGUP.md` — `upload_speed=921600` fails
  mid-write on this desk; use 115200).
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

- ☐ Question 1 decided, with the defect build flashed and photographed.
- ☐ Question 2 decided, and stated in a form D-A3 can implement without re-deciding.
- ☐ Question 3 decided by eye at desk distance, photographed.
- ☐ Question 4 decided; if cheap, the sentence is written down where a reader will meet it.
- ☐ Any decision with architectural weight to `ARCHITECTURE.md` §9; `docs/DESIGN.html` where a card
      moves — strains 24, 26 and 28 all have a D-B half.
- ☐ ctest green; session log · ROADMAP; split proposed; nothing committed until approved.

## Out of scope

The re-seed **mechanism** and its memory, and the liveness ping wire — **D-A3**. The soak — **D-C**.
`worst_frame`'s remaining outlier: attributed at D-A2 (wall-clock, **not** fetch-correlated, roughly
one frame in a couple of thousand, PSRAM named as the untested hypothesis) and it does not touch the
render path.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->
