# MP — Portfolio portal (garethcooke.com)

**Track:** Agentic · **Status:** Not started
**Executes in the `garethcooke-portfolio` repo, not this one.** Open the session there;
this brief travels with it. The portfolio repo's own conventions govern — existing
project entries are the spec, and that repo is the design-token *source*, so the
copy-tokens rule for companion sites does not apply here.

## Goal

garethcooke.com becomes the portal to DepthCharge exactly as it is for Anvil, Crucible,
FrontierView, and the hardware projects: a discoverable project card, a project page, and
an architecture link — plus two small drive-by fixes noticed along the way.

## Stage 1 — stub page (any time after M0)

1. **Project entry** matching the existing projects data schema. Discover the schema from
   the Anvil and MorayGlow entries; do not invent fields. Suggested content (owner may
   edit): status **In Progress**; tags `C++20 · ESP32-S3 · HUB75 · Order Books ·
   WebSocket · Market Data · KiCad · PlatformIO`; blurb draft:
   > A physical limit-order-book display: an ESP32-S3 driving a 64×64 LED matrix behind
   > smoked acrylic, rendering live depth first from my own matching engine (Anvil), then
   > from Kraken and Binance — bids stacking green, asks red, trades flashing white at
   > the touch, grey when the data can't be trusted. The portable C++20 book engine is
   > proven on a desktop replay harness before it ever touches the microcontroller;
   > custom carrier PCB and printed enclosure.
2. **`/projects/depthcharge` page** using the **hardware** project layout (MorayGlow is
   the exemplar), not the software layout (Anvil is that exemplar).
3. **Placeholder visual:** source `depthcharge_concept.svg` from the DepthCharge repo at
   `docs/media/depthcharge_concept.svg` (local checkout: `~/repos/DepthCharge`, or per
   its README); commit it to the site's assets as the hero/gallery item, captioned as
   concept art.
4. **Links:** GitHub repo (if/when the owner makes it public — confirm before linking)
   and a "Design & architecture →" target. Options: render `ARCHITECTURE.md` as a site
   page (matches Anvil's pattern) or link the repo file. Session proposes, owner decides;
   record in the log.
5. **Drive-by fix A:** the screenshots-tab heading renders `// PCB Photos` on *software*
   projects (visible today on `/projects/anvil`). Make the heading variant-aware.
   On DepthCharge it will eventually be literally correct; on Anvil it currently isn't.
6. **Drive-by fix B:** replace the stock create-next-app `README.md` with a real one —
   what the site is, the stack, dev/build commands, deploy flow, and the relationship to
   the companion sites.

## Stage 2 — real hardware (after M3)

Swap concept art for photos/video of the live panel (the pull-the-Wi-Fi grey-out makes a
good clip); refresh the blurb; keep In Progress until M7.

## Constraints

- Match existing components, tokens, and page grammar; no restyling, no shared-component
  extraction, no restructuring of other pages beyond fix A.
- Deploy through the existing flow (Amplify on push); nothing new in infra.

## Definition of done

☐ Site builds and lints clean; page live in production.
☐ Fix A verified on `/projects/anvil`; fix B committed.
☐ Session log below filled in; MP status updated in the DepthCharge repo's
  `ROADMAP.md` (note: different repo — leave the owner a reminder if not done).

## Out of scope

The optional live web mirror (a browser twin of the panel — see ROADMAP backlog; it
would be companion site #4 and trigger the shared-component review). Any changes in the
DepthCharge repo itself.

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step. -->
