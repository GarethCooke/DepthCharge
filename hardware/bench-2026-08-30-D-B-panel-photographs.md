# Bench 2026-08-30 — M5 stage D-B, the panel photographed

Five photographs taken at the D-B sitting, committed because **questions 1 and 3 are judgements no
counter reproduces** — the panel is the evidence, and without the images the decisions are
assertions. Same reason `bench-2026-08-22-kraken-b3-soak.log.gz` is in this directory.

## THE ONE THING TO READ BEFORE LOOKING AT THEM

**Four of these are the WORKING image and one is the DEFECT image, and they are visually
indistinguishable.** Both render a uniformly grey ladder with a legible header. Nothing in any
photograph tells you which firmware produced it.

That is not a flaw in the photography. It is the demonstration — arriving the same day — of the
`ARCHITECTURE.md` §9 rule committed at `dcad05b`: *a deliberately-broken image must mark the
periodic line, not the boot.*

**It happened three times in one sitting, which is why this section leads the file.** A first set of
photographs was sent as question 1's evidence and was the working image. A replacement was sent
after flashing `depthcharge-binance-silent` and was superseded again — *"it took a bit longer to
come through"* — and the file committed here is the third. **At no point could the photographer or
the reviewer tell the versions apart by looking**, and the two that were wrong are not preserved
because their provenance is no longer certain; a photograph nobody can attribute is not evidence of
anything.

The operational consequence, stated because a soak will hit it: **a photograph must be attributed by
a capture taken at the same instant, not by which build was last flashed.** Flashing is not the same
event as the panel reaching the state, and the gap between them is long enough to photograph.
`DC_SOAK_SILENT_TAG` on every SOAK line is what distinguishes them. **The filenames below are the
owner's assertion of which is which, not the attribution itself** — see *What the captures
attribute, and what they do not*, which is why that distinction is now written out rather than
assumed.

## The tag, named — because the captures are not in this repository

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

The two captures of this sitting are `firmware/logs/device-monitor-260830-054547.log`
(05:45:51–05:51:18) and `firmware/logs/device-monitor-260830-055239.log` (05:52:43–06:00:40). Both
carry the tag on every SOAK line. **Neither is in this repository** — `/firmware/logs/` is
gitignored at `.gitignore:27`, the same rule that keeps every other raw capture out — so the string
above is the repo's only copy outside `firmware/src/binance_endpoint.hpp:99`. They were produced by
the task itself, not by hand: `monitor_filters = esp32_exception_decoder, time, log2file` is set in
`[env]` at `platformio.ini:215` and resolves identically for `depthcharge-binance-silent`.

## What the captures attribute, and what they do not

**Question 1's photographic attribution is UNCONFIRMED.** Written here rather than left implied,
because this file's own rule is what convicts it: a photograph is attributed by a capture that
overlaps it, and none of these five is.

- **No photograph carries a timestamp.** EXIF is stripped from all five files — they have been
  through a transfer that re-encoded them. The only time any of them has is its mtime, which
  records when it was saved into this directory, not when the shutter fired.
- **Both captures are the defect image, and there is no capture of `depthcharge-binance` anywhere
  on 2026-08-30.** Every SOAK line in both files carries the tag, and `live=1` appears in neither.
  So the working image ran that morning unrecorded, and no photograph taken then could have been
  attributed to it even in principle.
- **The four files labelled WORKING were saved at 05:50:43–05:50:45**, inside the first capture's
  window, at which point the defect image had been running for five minutes.

What the WORKING labels rest on is **sequence** — the defect image was flashed after those
photographs were sent — and sequence is exactly what the paragraph above says is not attribution.
The labels are kept, because the owner's account of the order is the best evidence there is and it
is probably right; they are marked **asserted, not confirmed**, so that nobody later reads a
filename as a measurement.

### The header is not the discriminator, and it looks like one

The four WORKING files show `RESYNC` in the header; the DEFECT file shows `NO LINK`. **That
difference is a socket gap, not a firmware identity.** Both captures show the defect image passing
through both states, at the same point in each run:

| capture | boot | `sock 0 → 1` | header before | header after |
| --- | --- | --- | --- | --- |
| `…-054547.log` | 05:45:51 | between `up=310s` and `up=320s` | `RESYNC` | `NO LINK` |
| `…-055239.log` | 05:52:43 | between `up=310s` and `up=320s` | `RESYNC` | `NO LINK` |

So `RESYNC` is what the defect image displayed for the whole of capture 1 — 05:50 included — and
`NO LINK` is what it displayed at the end of capture 2. Reading the header as *which build is this*
gives the wrong answer in both directions. Recorded for the same reason the rolling-shutter banding
is: so it is not re-diagnosed later as evidence it never was. (The repeat at `up=310–320s` in two
independent runs is itself worth a look, but it is a transport question and belongs to D-A3 or D-C,
not to this file.)

### What stands in the photographs' place

**The `strings` check on the artefacts** — attribution of the *binary* rather than of the panel.
Over the six images built 2026-08-29 19:24–19:25, the literal `SILENT-STREAM DEFECT IMAGE` appears
**once**, in `depthcharge-binance-silent/firmware.bin`, and **zero** times in `depthcharge`,
`depthcharge-binance`, `depthcharge-kraken`, `depthcharge-noping` and `depthcharge-ps`. That is
what §9's 2026-08-29 row asks to be verified rather than assumed: the shipping binary does not
*contain* the marker rather than merely not printing it.

Taken with the captures, that is what carries question 1. The defect image demonstrably ran,
greyed, and never went live — `rows=0/54 unknown=54 frames=0` for the full 480 s of the second run
— which is the claim. **The photographs corroborate what the grey looks like; they are not the
evidence of which build produced it.** The gap is closed the day a photograph carries a timestamp
that can be laid against a capture, and not before.

## The files

| File | Image | Question |
| --- | --- | --- |
| `…-q1-silent-feed-DEFECT-image.jpg` | `depthcharge-binance-silent` | **1** — what a silent feed renders |
| `…-q3-grey-WORKING-image-closeup.jpg` | `depthcharge-binance` | 3 — whether the grey reads right |
| `…-q3-grey-WORKING-image-full-panel.jpg` | `depthcharge-binance` | 3 |
| `…-q3-grey-WORKING-image-oblique.jpg` | `depthcharge-binance` | 3 — the angle M4's ramp defect needed |
| `…-q3-grey-WORKING-image-desk-distance.jpg` | `depthcharge-binance` | 3 — the viewing distance the judgement is about |

The **Image** column is the owner's attribution throughout — **no photograph here is confirmed by
a capture**, and the q3 rows in particular rest on the order the images were taken in. See *What the
captures attribute, and what they do not*.

## What they show

Uniform grey across every row, **no colour separation anywhere**, header glyphs legible, and the
dark `Ink::HeaderBed` band behind the header text — invariant #5's one named exception, working.

**On the defect image this is the result the milestone was built for.** At stage B1 this same
misspelled stream was measured producing *"a populated 100-level ladder rendered ● LIVE"* —
invariant #5's one unacceptable output, arriving through the mechanism installed to prevent it.
Remedy (a) withholds the Snapshot until a diff brackets the seed, so the feed that never speaks now
greys. **First time the forbidden output has been prevented on hardware rather than in a test.**

## What is NOT in these photographs, stated so nobody infers it

- **Which firmware produced any given one.** See *What the captures attribute*. The tag is named
  in full above, so it can be recognised without the capture to hand; what no photograph here
  supplies is a timestamp to lay against one.
- **The wash level as the eye sees it.** Phone exposure flattens every lit pixel towards the same
  brightness; the desk-distance frame is the closest to the real judgement, and the judgement itself
  was made from the chair, not from these.
- **Diagonal banding is rolling shutter against the panel's multiplexed refresh.** Not a defect, not
  visible to the naked eye, and recorded here so it is not re-diagnosed from the images later — the
  same trap `BRINGUP.md`'s dead-E-line signature exists to close.
