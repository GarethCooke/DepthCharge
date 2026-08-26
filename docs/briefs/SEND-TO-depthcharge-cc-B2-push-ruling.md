# SEND TO DEPTHCHARGE CC — the early push stands; record it, and change the mechanism

Commits 1–7 went to `origin/master` at 18:13:47, from outside the session, before their ladder
completed. **Leave the history alone. Do not rebase, do not amend, do not force-push.** They are
green now; what was wrong was the order of evidence and publication, not the tree.

## 1 · Why rewriting is the worse option, and it is not a close call

**`a67f2e1` is commit 1, it is on `origin/master`, and `ARCHITECTURE.md` references it exactly
once** — as the SHA that discharges §9's untracked-evidence clause. Rebasing 1–7 rewrites every
hash from commit 1 onward, so §9 would carry a SHA that resolves to nothing on the branch. **That
breaks the precise guarantee this stage existed to establish**: an assertion with a findable
committed trace behind it. Orphaning the discharge SHA to tidy a process record trades the thing
for the record of the thing.

It also recurses. Repairing §9 after a rewrite needs a commit whose content is the new hash of an
earlier commit — `SHA-PENDING` round two, on a history already rewritten once, with the rewritten
history now depending on the repair.

**And the method decides it independently.** This repository records rather than hides: the heap
alarm retracted by medians, the largest-block "ratchet" that proved a sawtooth, and — exactly this
shape — M4 stage D's own ROADMAP line, *"the 'nothing pushed' this entry used to carry was never
checked against the remote and was false."* That entry was corrected in place, not tidied away.
Rewriting so the ladder appears to have completed before the push would **manufacture a record
that never existed**, which would be the first time this project's artefacts told a story that is
not true. The cost of the honest version is one sentence.

## 2 · What to record

A session-log line in the voice M4 stage D used — what happened, when, from where, that the ladder
completed afterwards and every commit is green in isolation. **No §9 row.** Nothing architectural
moved and no assertion is weakened; the tree is exactly what the split says it is.

## 3 · The half that is worth more than the apology — the rule has no mechanism

*"Nothing is pushed until every commit has been shown green in isolation"* is a rule stated in
prose, in a brief, held by a session. **It was breached from outside that session, and nothing
failed.** Prose has no reach there. That is the same species as the stale report line B1 named and
the sentinel guard this stage just fixed: a check that depends on the right person reading the
right document at the right moment is not a check.

**Do not reach for a pre-push hook.** `.git/hooks` is unversioned and absent on a fresh clone, so a
hook is precisely *"a guard that depends on somebody remembering"* — the failure `CLAUDE.md` names
one paragraph above, and the same shape as the stale worktree it warns about in the paragraph
below. Use a mechanism the repository can see instead:

> **Push the stage to its own branch. Fast-forward `master` only once the ladder has closed.**
> An unfinished split is then visible and pushable without being published, and the act that
> publishes it is a separate, deliberate one.

Record it in `CLAUDE.md` beside the push-discipline paragraph — tooling rather than architecture,
so no §9 row, filed the way the sentinel rule was.

**This is not retrospective.** `origin/master` is at `5c21b08` (commit 7); **8–12 are still
unpushed**, so the branch discipline applies to them tonight rather than starting next stage.

## Definition of done

- [ ] History untouched — no rebase, no amend, no force-push; `a67f2e1` still reachable from
      `origin/master` and still the SHA §9 names.
- [ ] Session-log line recording the early push, in the M4-stage-D correction's voice.
- [ ] Branch-then-fast-forward recorded in `CLAUDE.md` beside the push discipline, with the
      unversioned-hook reasoning stated rather than implied.
- [ ] Commits 8–12 pushed to a stage branch, `master` fast-forwarded only after the ladder closes.
