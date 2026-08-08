# MP (stage 3) — the design doc reaches the portal

**Track:** Agentic · **Status:** Not started
**Executor:** Claude Code **in the `garethcooke-portfolio` repo**, not this one. Nothing in
DepthCharge changes. This brief lives here because MP is a DepthCharge roadmap milestone and
the repo convention is that briefs live with the milestone (see `ROADMAP.md` row MP,
"Executes in the `garethcooke-portfolio` repo").

**Read first, in the portfolio repo:**
`.claude/commands/refresh-writeup.md` (the method — it is binding, not advisory),
`writeup-sources.json` (the manifest), `src/app/depthcharge/architecture/page.tsx` (the page
being changed), `src/app/depthcharge/architecture/diagrams.tsx` (its hand-built JSX diagrams).

**Read first, in DepthCharge:** `docs/DESIGN.html` — the new source. Resolve its location
from the manifest (below); do not hardcode a path.

---

## Where the DepthCharge repo is, and how to find it

This is the one piece of cross-repo plumbing, and it is already solved — do not ask the
owner for a path.

`writeup-sources.json` carries a `repoRoot` and each entry carries a `repo.dir`:

```json
"repoRoot": "C:/Development/Projects",
"$repoRootNote": "Parent directory holding the source repos. Override with the
                  WRITEUP_REPO_ROOT environment variable on a different machine.",
...
{ "slug": "depthcharge-architecture", "repo": { "dir": "DepthCharge", ... } }
```

So the source repo is `${repoRoot}/${repo.dir}` = `C:/Development/Projects/DepthCharge`.

You do not have to compute that by hand. `npm run check:writeups` prints the absolute path
in its own output, on the `diff it:` line:

```text
diff it: git -C "C:\Development\Projects\DepthCharge" diff fbd6d80..HEAD -- ARCHITECTURE.md README.md ROADMAP.md
```

**Access note.** DepthCharge may not be in this session's working directories, so the file
tools may prompt. Reading through git avoids it entirely and is preferred anyway, because it
pins you to a commit rather than to whatever is currently checked out:

```bash
git -C "C:/Development/Projects/DepthCharge" show HEAD:docs/DESIGN.html
git -C "C:/Development/Projects/DepthCharge" log --oneline -8
```

If `repoRoot` is wrong on this machine, fix it via `WRITEUP_REPO_ROOT` or the manifest —
that is a manifest bug, and `check:writeups` reports it as `broken` rather than `drift`.

## Goal

`/depthcharge/architecture` currently renders three DepthCharge sources — `ARCHITECTURE.md`,
`README.md`, `ROADMAP.md` — and is three commits behind them. Meanwhile the richest
description of the system, `docs/DESIGN.html`, is invisible to the site and to the drift
checker: a session can rewrite the entire design doc and the portfolio will never notice.

At the end: the manifest tracks `docs/DESIGN.html` as a fourth source, the page reflects it
and the three drifted commits, and `npm run check:writeups` reads `ok`. From then on the
design doc is on the same automated leash as everything else.

## Deliverables

### 1. Add the source to the manifest

In `writeup-sources.json`, entry `depthcharge-architecture`:

```json
"sources": ["ARCHITECTURE.md", "README.md", "ROADMAP.md", "docs/DESIGN.html"],
```

`scripts/check-writeup-sources.mjs` is file-type agnostic — it shells out to
`git log/diff -- <sources>` — so an `.html` path needs no script change. Verify by running
the checker before and after: the drift interval should widen to include the design-doc
commits (`bda8ab3`, `791c968`).

### 2. Refresh the page against the full interval

Run `/refresh-writeup depthcharge-architecture` and follow that command's method exactly.
Two of its rules matter more than usual here and are the ones to re-read before editing:

- **Render, never paste.** `docs/DESIGN.html` is a standalone 70 KB document with its own
  palette, monospace type scale and "illuminated plate" figures. Dropped into this site it
  would read as a different site. Its mermaid becomes hand-built JSX in `diagrams.tsx`, in
  the page's existing visual language — same rule the command already states for the other
  write-ups.
- **Portfolio-original content is untouchable.** The `Note` blocks, the "What it
  demonstrates" close, `metadata`/`openGraph`, the existing three diagrams, and the CTA
  framing are not in any source and must survive the refresh.

Edits drop into the page's existing helpers — `<P>`, `<H2>`, `<H3>`, `<C>`, `<Bullets>`,
`<Numbered>`, `<DataTable>`, `<Note>`. Do not introduce new primitives for a refresh.

### 3. The substantive correction: the channel is built

Commit `ae87a83` ("engine: make SnapshotChannel a real wait-free cross-core mailbox") is the
one change that makes the page's current wording out of date rather than merely incomplete.

`page.tsx:306` currently says the `DisplaySnapshot` "is published through a version-stamped
double buffer — the render side takes the latest complete version and never blocks the
writer". That was written as *design intent* quoted from `ARCHITECTURE.md` §5. It is now a
shipped, tested mechanism — and it is **not a double buffer**.

`SnapshotChannel` keeps **three** `DisplaySnapshot` slots (`kSlots = 3`) and one lock-free
32-bit atomic word carrying a slot index in bits 1..0 and a FRESH bit at 0x4. `publish` and
`consume` each swap their own slot into that word with a single `exchange`, so the writer's
slot, the reader's slot and the ready slot are always three distinct objects — which is what
lets `publish` be a fixed-size copy plus one atomic op with no loop and no lock. Evidence:
`harness/tests/test_snapshot_channel.cpp`, `channel_stress.hpp`, and a committed clean
ThreadSanitizer run via `harness/tsan.sh`.

`ARCHITECTURE.md` §9 records the choice explicitly: *"the feed→render hand-off is a
three-slot mailbox, not a seqlock or a two-slot double buffer"*. Do not describe it as a
double buffer or a seqlock — both were considered and rejected, and the §9 row says so.

Say it is built and proven, not planned. Invariant #4 moved from promise to evidence, which
is the most portfolio-relevant thing that has happened to this project since M1.

### 3b. The claim the drift checker cannot see

`page.tsx:872` currently says, of invariant #4:

> "The hand-off is wait-free on the writer side; **overflow drops frames and reports it as a
> gap.** Per-event cost stays bounded independent of consumer speed."

The second clause is being retracted upstream. Invariant #4 is being rewritten to say the
mailbox is a *latest-value* one whose superseded frames "drop silently — lossless at the book
level, because every published frame is a *complete* render state, not a delta", and that
`Gap{Overflow}` is therefore "a *feed-side* signal — a venue reassembly buffer, first produced
at a delta venue (M4/M5) — never a render-hand-off event."

**This is a `page-contradicts-source` case that `npm run check:writeups` will not report,**
because at the time of writing that ARCHITECTURE.md edit is *uncommitted* in the DepthCharge
working tree and the checker only reads committed history.

**Precondition:** confirm the invariant-#4 rewrite is committed in DepthCharge before
starting, and take the interval to *that* commit. If it is still uncommitted, stop and tell
the owner — do not render the page against a dirty working tree, and do not commit it
yourself (DepthCharge is read-only from here). Verify with:

```bash
git -C "C:/Development/Projects/DepthCharge" status --short
git -C "C:/Development/Projects/DepthCharge" log --oneline -3 -- ARCHITECTURE.md
```

The other two commits in the interval are smaller but real: `14ee78c` ("prove `engine/`
compiles for the target, not just for the host") strengthens the invariant-#1 claim the page
already makes, and `9357d40` records the M2 bench bring-up.

### 4. New material worth surfacing

The page's ten sections are architecture and rationale. `docs/DESIGN.html` adds
implementation fact the page has no equivalent of. Take what serves a visiting engineer;
this is a portfolio page, not a mirror.

| From `DESIGN.html` | Page has it? | Recommendation |
| --- | --- | --- |
| §03 the parse seam — one declaration, two definitions, chosen by the linker | no | **Take it.** Cheapest possible seam, and it is *why* M3 stage B is a one-target swap |
| §02 the `LevelSpan` lifetime rule (borrowed spans, valid for one sink call) | no | **Take it.** The sharpest design decision in the codebase |
| §07 measured `sizeof` — whole engine 18,128 B for one symbol | no | **Take it.** Concrete numbers land well; keep it to two or three figures, not the whole table |
| §06 how it is proved — 55 doctest cases, replay goldens, `alloc_probe` replacing global `operator new` | partial | **Take the alloc probe.** Turning invariant #7 into a measurement is the memorable bit |
| §08 nine "where the design is under strain" entries | no | **Owner's call — see Known unknowns** |
| §01/§04/§05 pipeline, book stages, stale semantics | yes | Already covered; correct only if drifted |

### 5. Record the refresh and verify

Update `refreshedAt` for the entry to the DepthCharge commit the page now reflects — hash,
date and subject. Skipping this silently resets the drift baseline to nothing. Then:

```bash
npm run check:writeups   # must read ok
npm run lint
npm run build
```

## Constraints

- **DepthCharge is read-only from this work.** No commits, no edits, no branches in
  `C:/Development/Projects/DepthCharge`. If the design doc looks wrong, report it — do not
  fix it from the portfolio repo.
- **The repo docs stay authoritative.** `refresh-writeup.md`: "The repo doc is authoritative
  for the facts; the page is authoritative for how those facts are told here." The page is a
  narrative rendering, not a copy, and never the source of truth.
- **Public-repo link rule.** DepthCharge is public, so a path token that names a real file
  renders as a hyperlink via `C` — add any new path to `REPO_FILES` and confirm the file
  exists first. Note the deliberate exclusion already documented there: `firmware/` and
  `hardware/` stay plain because they hold only a `.gitkeep`, and an underline is a promise
  there is something to see. `docs/DESIGN.html` is a real file and may be linked.
- **Diagram fidelity.** If a JSX diagram's topology no longer matches the source, update the
  JSX. `PipelineDiagram`, `PanelBudgetDiagram` and `StaleStateDiagram` are the three that
  exist; the channel change may affect the first.
- **Deliberately unspecified** (session decides, records in the log): whether new material
  becomes new `H2` sections or extends existing ones; how many of the `sizeof` figures to
  show; whether the parse seam earns a fourth diagram or is prose.

## Known unknowns

- **Does the public page render §08's strain list?** `docs/DESIGN.html` §08 names nine places
  the design is carrying load it has not been asked to bear — an internal register, written
  to be attacked. A portfolio page that names its own weak points reads as more credible,
  not less, and strain point 2 is now a *success* story ("was a claim; M3 stage A made it a
  mechanism"). But it is the owner's editorial call, not the session's. **Ask before
  rendering it**; if the answer does not arrive, do everything else and leave it out, and say
  so in the log.
- **Does `PipelineDiagram` still match?** Check its hand-off depiction against the shipped
  two-slot channel before deciding whether it needs redrawing.
- **Interval size.** Adding a fourth source widens the diff range to commits the checker has
  never reported. Read `git log` over the *new* source separately so design-doc history is
  not lost inside the ARCHITECTURE/ROADMAP diff.

## Definition of done

☐ `docs/DESIGN.html` present in the `depthcharge-architecture` `sources` array.
☐ Page corrected: `SnapshotChannel` described as a shipped, TSan-proven wait-free mechanism,
  not as planned design intent.
☐ Selected new material rendered as JSX in the page's own voice; no pasted HTML, no pasted
  mermaid, no new component primitives.
☐ Any new path token added to `REPO_FILES` and verified to exist.
☐ Portfolio-original content (Note blocks, closes, metadata, existing diagrams) intact.
☐ `refreshedAt` updated to the DepthCharge commit the page now reflects.
☐ `npm run check:writeups` reads `ok`; `npm run lint` and `npm run build` pass.
☐ Session log below filled in — including the strain-list decision and its reason.

## Out of scope

MP stage 2 (real hardware photography, gated on M3 completion); any change to the DepthCharge
repo; restyling `/depthcharge/architecture` or the site's design tokens; the `/anvil/architecture`
and `/frontierview/agent` write-ups, both of which currently read `ok`; adding a fourth
write-up page (this is a refresh of an existing one, not a new route); embedding or linking
the published Claude artifact of `DESIGN.html` — the portfolio renders from the repo source,
not from an artifact URL.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->
