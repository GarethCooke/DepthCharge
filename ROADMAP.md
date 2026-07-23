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
| M0 | A     | Trace + harness               | Live Anvil WS traces captured & vendored; replay harness parses them; golden-test + CMake skeleton; ctest green.    | —          | ☐ Next — brief: `docs/briefs/M0-trace-and-harness.md` |
| M1 | A     | Console ladder off replay     | `FeedEvent` types real; Anvil adapter (frames→events); phase-1 book (adopt snapshot + trade ring); console ladder renders a replay; goldens green. | M0 | ☐ |
| M2 | B     | Panel smoke test              | ESP32-S3 DevKit + 64×64 HUB75 + PSU wired; HUB75 DMA library demo runs; photo in `hardware/`.                        | —          | ☐ |
| M3 | A+B   | Live Anvil on the panel       | Firmware net task (TLS WS, nominated `Origin` header) + render task; engine unchanged from M1; live ladder on the panel; pull-the-Wi-Fi test shows grey stale state then clean resync. | M1, M2 | ☐ |
| M4 | A     | Kraken adapter                | Delta application + CRC32 verification; dense-window book lands here; Kraken traces + goldens; panel switches venue. | M3 | ☐ |
| M5 | A     | Binance adapter               | Partial-depth easy mode, then full diff stream with REST-snapshot bracketing and gap recovery; traces + goldens.     | M4 | ☐ |
| M6 | B     | Carrier PCB                   | KiCad carrier (WROOM-1-N16R8, 2× 74HCT245, HUB75 IDC, USB-C 5 V/3 A with CC pulldowns, bulk caps, EC11); DRC clean; fabbed & bring-up. | M3 | ☐ |
| M7 | A+B   | Enclosure + board mode        | Printed enclosure, smoked acrylic front; encoder modes: ladder (symbol/venue/zoom) + Anvil 12-ticker summary board mode. | M4, M6 | ☐ |
| MP | A     | Portfolio portal              | **Executes in the `garethcooke-portfolio` repo.** Stage 1 (any time after M0): `/projects/depthcharge` live with In Progress badge, concept art, tags, repo/architecture links, plus two drive-by fixes. Stage 2 (after M3): real hardware photos/video. Brief: `docs/briefs/MP-portfolio-portal.md`. | Stage 2: M3 | ☐ |

## Backlog (not scheduled)

- Anvil-side (lives on Anvil's backlog, cross-referenced only): chaos flag for
  deterministic gap testing; sequenced incremental L2 feed (DepthCharge is its future
  test client); feeder realism (Hawkes arrivals / mirror mode / FrontierView
  execution-algo participant).
- DepthCharge: Crucible post — book structures under fire (flat_map vs dense window,
  driven by Anvil's *trend* workload).
- Optional **live web mirror** of the panel's `DisplaySnapshot` feed (a browser twin of
  the hardware). *That* would be companion site #4 and trigger the shared-component-repo
  review. The portfolio page itself is scheduled work (MP) inside the portfolio repo and
  does **not** count toward the threshold.
