# Bench 2026-08-30 — M5 stage D-B, the panel photographed

Five photographs of a grey panel, taken at the D-B sitting, and the two serial captures of that
morning. Committed because **questions 1 and 3 are judgements no counter reproduces** — the panel is
the evidence, and without the images the decisions are assertions. Same reason
`bench-2026-08-22-kraken-b3-soak.log.gz` is in this directory.

## THE ONE THING TO READ BEFORE LOOKING AT THEM

**Four of the five are not attributed to a firmware image. The fifth attributes itself, from its own
pixels, and it is not the one anybody expected.** All five were renamed to carry only what is known
of them from the outside — which question each was offered for. The build claim they used to have in
their filenames (`WORKING` on four, `DEFECT` on one) is an assertion the evidence does not reach for
four of them, and a filename is read long after a caveat is forgotten.

Those four render a uniformly grey ladder with a legible header, and **nothing in them tells you
which firmware produced it.** That is not a flaw in the photography. It is the demonstration —
arriving the same day — of the `ARCHITECTURE.md` §9 rule committed at `dcad05b`: *a
deliberately-broken image must mark the periodic line, not the boot.*

The exception is `bench-2026-08-30-D-B-grey-panel-q3-oblique.jpg`, whose header reads **`SEQ GAP`**
— a state the defect image provably cannot reach. See *The one photograph that attributes itself*.

**It happened three times in one sitting, which is why this section leads the file.** A first set of
photographs was sent as question 1's evidence and was the working image. A replacement was sent
after flashing `depthcharge-binance-silent` and was superseded again — *"it took a bit longer to
come through"* — and the set committed here is the third. **At no point could the photographer or
the reviewer tell the versions apart by looking**, and the two that were wrong are not preserved
because their provenance is no longer certain; a photograph nobody can attribute is not evidence of
anything.

The operational consequence, stated because a soak will hit it: **a photograph must be attributed by
a capture that overlaps it, not by which build was last flashed.** Flashing is not the same event as
the panel reaching the state, and the gap between them is long enough to photograph.
`DC_SOAK_SILENT_TAG` on every SOAK line is what distinguishes the images; a photograph with no clock
on it cannot be laid against that line, and that is the whole of the problem here.

**What this does NOT touch: the decisions.** Question 3 was decided at the sitting, from the working
image, in the chair — *the grey reads as not-trusted-yet rather than broken* — and question 1 was
decided from the defect image at the end of the same sitting. Both stand, and both live in
`docs/briefs/M5-stage-D-B-the-four-rendering-decisions.md` with their reasoning. **Only the
photographic attribution failed, and only for four of the five.** These images corroborate what a
grey panel looks like; the decisions never rested on them being proof of which build drew it.

## The files

| File | Offered for | Header | Attributed |
| --- | --- | --- | --- |
| `bench-2026-08-30-D-B-grey-panel-q1.jpg` | **1** — what a silent feed renders | `NO LINK` | no |
| `bench-2026-08-30-D-B-grey-panel-q3-closeup.jpg` | 3 — whether the grey reads right | `RESYNC` | no |
| `bench-2026-08-30-D-B-grey-panel-q3-full-panel.jpg` | 3 | `RESYNC` | no |
| `bench-2026-08-30-D-B-grey-panel-q3-oblique.jpg` | 3 — the angle M4's ramp defect needed | **`SEQ GAP`** | **yes — working image** |
| `bench-2026-08-30-D-B-grey-panel-q3-desk-distance.jpg` | 3 — the viewing distance the judgement is about | `RESYNC` | no |

**Offered for** is the question each image was taken to illustrate; it is not a claim about which
firmware drew it. **Header** is the value slot's text, and it is a clock rather than a name in three
of the four `RESYNC` / `NO LINK` cases — see *The header is not the discriminator* — with the one
exception the last column records.

| Capture | Window | Image |
| --- | --- | --- |
| `bench-2026-08-30-D-B-silent-stream-054547.log.gz` | 05:45:51–05:51:18 | defect, tag on every SOAK line |
| `bench-2026-08-30-D-B-silent-stream-055239.log.gz` | 05:52:43–06:00:40 | defect, tag on every SOAK line |

The captures **are** attributed, by the tag they carry. They are gzipped copies of
`firmware/logs/device-monitor-260830-054547.log` and `…-055239.log`, verified byte-identical to the
originals, committed here because `/firmware/logs/` is gitignored at `.gitignore:27` and this file
would otherwise cite evidence the repository does not hold.

## The tag, named

`DC_SOAK_SILENT_TAG` resolves to this exact string, and the defect image appends it to **every**
SOAK line it prints:

```
 *** SILENT-STREAM DEFECT IMAGE: misspelled stream, NO frame will ever arrive ***
```

One whole line as captured, so it is recognisable in context rather than only in isolation:

```
05:45:57.861 > I (9871) panel: SOAK venue=binance up=10s live=0 age=- worst_age=0.0s baseline=0ms grey_n=1 grey_ms=9483 wd=0 sock=0 connects=1 rows=0/54 unknown=54 crc_rows=0 (0.0%) resync_req=0 heals=0 owed=0 refused=0 crc_fail=0 heap=23500 largest=13300 frames=0 drawn=1 *** SILENT-STREAM DEFECT IMAGE: misspelled stream, NO frame will ever arrive ***
```

**Do not mistake the boot banner for it.** `main.cpp:134` prints a different string, once:
`*** SILENT-STREAM BUILD: subscribing to a DELIBERATELY MISSPELLED stream (/ws/btcusdt@depth@100mss).
The socket will open, pings will be answered, and NO depth frame will ever arrive. This board is not
broken. ***`. The §9 rule committed at `dcad05b` is about the **periodic** line; the banner is the
half that does not discharge it, and a capture attached without a reset never contains it.

The captures were produced by the flash task itself, not by hand: `monitor_filters =
esp32_exception_decoder, time, log2file` is set in `[env]` at `firmware/platformio.ini:215`, and
`pio project config --json-output` resolves it identically for `[env]`, `env:depthcharge-binance`
and `env:depthcharge-binance-silent`.

## What the captures attribute, and what they do not

**Four of the five photographs are not attributed to a firmware image.** Three legs:

1. **EXIF is stripped from all five files.** Every one has been through a transfer that re-encoded
   it — all five are exactly 1350×1800 with an empty EXIF block. The only time any of them carries
   is its mtime: when it was saved into this directory, not when the shutter fired.
2. **There is no capture of `depthcharge-binance` anywhere on 2026-08-30.** Both captures are the
   defect image: every SOAK line in both carries the tag, and `live=1` appears in neither. The
   working image ran that morning unrecorded, so a photograph of it could not have been attributed
   by an overlapping capture even in principle.
3. **The four q3 files were saved at 05:50:43–05:50:45**, inside the first capture's window, at
   which point the defect image had been running for five minutes.

Leg 3 is the one that looks damning and is not, and the next section is why: the oblique frame shows
a panel state **neither capture ever rendered**, which proves those save-times are transfer times
rather than shutter times. What legs 1–3 establish for the remaining four is narrower but still
binding — **the attribution rested on sequence, and sequence is what the paragraph at the top of
this file says is not attribution.** So the claim is dropped from the filenames rather than defended
in them.

### The one photograph that attributes itself

`…-q3-oblique.jpg` reads **`SEQ GAP`** in the header. That is `GapReason::SeqGap`
(`ladder_render.hpp:396`), and it is the only one of the six `reason_text` strings whose second word
is three glyphs, so it cannot be confused with `RESYNC` at this resolution.

**The defect image cannot produce it.** Every site that raises it —
`binance_adapter.hpp:897`, `952`, `960`, `1114` — sits downstream of a received depth frame or a
buffered event, and both 08-30 captures hold `msgs_in=0`, `frames=0`, `seqbreak=0` and
`bracket ok=0 FAIL=0` on every line, with `STALE (resync)` as the sole stale transition in each. A
feed that never speaks has nothing to gap.

**The state is a real one on the working image**, so this is not a reading of noise:
`firmware/logs/device-monitor-260828-175118.log` carries no silent tag and reaches
`bracket ok=0 FAIL=1 unconfirmed=1 reseeds=1` while SOAK still reads `live=0` — a seed-bracket
failure before any snapshot is published, which drops the book with `SeqGap` and leaves exactly what
the photograph shows: `SEQ GAP` in the header over an empty grey ladder.

Two consequences. **The oblique frame is the working image**, attributed by its own pixels and not
by anybody's account of the order. And **its mtime is provably not its shutter time** — it depicts a
state neither 08-30 capture rendered, so it was taken outside both windows. That is affirmative
evidence for the owner's account of the sequence, and it is the reason leg 3 above is stated as a
fact about save-times rather than as a verdict.

### The header is not the discriminator, and it looks like one

Three of the four remaining files show `RESYNC` and one shows `NO LINK`. **That difference is a
socket gap, not a firmware identity.** Both captures show the defect image passing through both
states, at the same point in each run:

| capture | boot | `sock 0 → 1` | header before | header after |
| --- | --- | --- | --- | --- |
| `…-054547.log.gz` | 05:45:51 | between `up=310s` and `up=320s` | `RESYNC` | `NO LINK` |
| `…-055239.log.gz` | 05:52:43 | between `up=310s` and `up=320s` | `RESYNC` | `NO LINK` |

So `RESYNC` is what the defect image displayed for the whole of capture 1 — 05:50 included — and
`NO LINK` is what it displayed at the end of capture 2. Reading either as *which build is this*
gives the wrong answer in both directions. `RESYNC` in particular is not a socket state at all: it
is the `Book` constructor default (`book.hpp:330`) and is **never assigned anywhere in the Binance
path**, so on this venue it means only *no gap event since boot*.

The lesson is not that the header is useless. It is that **`SEQ GAP` discriminates and `RESYNC` does
not**, and telling them apart needed the reachability argument above rather than the appearance of
difference. Recorded for the same reason the rolling-shutter banding is: so it is not re-diagnosed
later as evidence it never was. (The repeat at `up=310–320s` across two independent runs is worth a
look on its own — the driver is ~302.9 s of rx silence, *"socket up but silent for 300 s —
recycling it"* — but it is a transport question and belongs to D-A3 or D-C, not to this file.)

### What stands in the photographs' place

**The `strings` check on the artefacts** — attribution of the *binary* rather than of the panel.
Over the six images built 2026-08-29 19:24–19:25, the literal `SILENT-STREAM DEFECT IMAGE` appears
**once**, in `depthcharge-binance-silent/firmware.bin`, and **zero** times in `depthcharge`,
`depthcharge-binance`, `depthcharge-kraken`, `depthcharge-noping` and `depthcharge-ps`. That is what
§9's 2026-08-29 row asks to be verified rather than assumed: the shipping binary does not *contain*
the marker rather than merely not printing it.

Taken with the captures, that is what carries question 1. The defect image demonstrably ran, greyed,
and never went live — `rows=0/54 unknown=54 frames=0` for the full 480 s of the second run — which
is the claim. For the four unattributed frames the gap closes the day a photograph carries a
timestamp that can be laid against a capture, and not before.

## What they show

Uniform grey across every row, **no colour separation anywhere**, header glyphs legible, and the
dark `Ink::HeaderBed` band behind the header text — invariant #5's one named exception, working.
Rows the book does not fill are painted `Ink::Background` (`ladder_render.hpp`, `draw_side`), which
is why an unseeded book reads as a dim field with only the spread row and the two chrome rules
brighter.

**This is the result the milestone was built for.** At stage B1 the misspelled stream was measured
producing *"a populated 100-level ladder rendered ● LIVE"* — invariant #5's one unacceptable output,
arriving through the mechanism installed to prevent it. Remedy (a) withholds the Snapshot until a
diff brackets the seed, so the feed that never speaks now greys. **First time the forbidden output
has been prevented on hardware rather than in a test** — and the evidence for that sentence is the
captures above, which never reach `live=1`, not the photographs.

## What is NOT in these photographs, stated so nobody infers it

- **Which firmware produced any given one, except the oblique frame.** See *What the captures
  attribute*. The tag is named in full above, so it can be recognised without the capture to hand;
  what no photograph here supplies is a timestamp to lay against one.
- **The wash level as the eye sees it.** Phone exposure flattens every lit pixel towards the same
  brightness; the desk-distance frame is the closest to the real judgement, and the judgement itself
  was made from the chair, not from these.
- **Diagonal banding is rolling shutter against the panel's multiplexed refresh.** Not a defect, not
  visible to the naked eye, and recorded here so it is not re-diagnosed from the images later — the
  same trap `BRINGUP.md`'s dead-E-line signature exists to close.
