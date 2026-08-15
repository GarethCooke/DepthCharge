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
| M3 | A+B   | Live Anvil on the panel       | Firmware net task (TLS WS, nominated `Origin` header) + render task; engine unchanged from M1; live ladder on the panel; pull-the-Wi-Fi test shows grey stale state then clean resync. | M1, M2 | ☐ **Next** — stage A ✅ (2026-08-07, wait-free `SnapshotChannel`), stage B ✅ (2026-08-08, streaming allocation-free parser, goldens unchanged), stage C ✅ (2026-08-09, feed proven on the board). Stage D written and host-proven 2026-08-10 but **not flashed**: the 64×64 ladder renders in `dc_tests` as a grid of `Ink`, and "stale carries no hue" is a `static_assert`. **The pull-the-Wi-Fi acceptance on the panel is the only thing left in M3**, and it is owner-driven at the bench. |
| M4 | A     | Kraken adapter                | Delta application + CRC32 verification; dense-window book lands here; Kraken traces + goldens; panel switches venue. | M3 | ☐ |
| M5 | A     | Binance adapter               | Partial-depth easy mode, then full diff stream with REST-snapshot bracketing and gap recovery; traces + goldens.     | M4 | ☐ |
| M6 | B     | Carrier PCB                   | KiCad carrier (WROOM-1-N16R8, 2× 74HCT245, HUB75 IDC, USB-C 5 V/3 A with CC pulldowns, bulk caps, EC11); DRC clean; fabbed & bring-up. | M3 | ☐ |
| M7 | A+B   | Enclosure + board mode        | Printed enclosure, smoked acrylic front; encoder modes: ladder (symbol/venue/zoom) + Anvil 12-ticker summary board mode. | M4, M6 | ☐ |
| MP | A     | Portfolio portal              | **Executes in the `garethcooke-portfolio` repo.** Stage 1 (any time after M0): `/projects/depthcharge` live with In Progress badge, concept art, tags, repo/architecture links, plus two drive-by fixes. Stage 2 (after M3): real hardware photos/video. Stage 3 (any time): the design doc reaches the portal — `/depthcharge/design` renders `docs/DESIGN.html`, tracked by its own `writeup-sources.json` entry so it drifts independently of the architecture page. Briefs: stages 1–2 in the **portfolio** repo at `docs/briefs/MP-portfolio-portal.md`; stage 3 here at [`docs/briefs/MP-design-doc-on-the-portal.md`](docs/briefs/MP-design-doc-on-the-portal.md). | Stage 2: M3 | ◐ Stages 1 ✅ (2026-07-22) and 3 ✅ (2026-08-08, pending commit in garethcooke-portfolio); stage 2 gated on M3 |

## Backlog (not scheduled)

- Anvil-side (lives on Anvil's backlog, cross-referenced only): chaos flag for
  deterministic gap testing; **sequenced incremental L2 feed — promoted 2026-08-11 from
  a nice-to-have to the thing that makes the hardware work at all.** A 5,744-byte TCP
  receive window across the measured **87 ms** RTT to `52.204.246.224` (AWS us-east-1)
  caps the board at **65.5 KiB/s** against Anvil's **110.4 KiB/s** full-book stream, so
  the panel falls permanently behind — 328 s measured — and no firmware lever reaches it
  (ARCHITECTURE §9, 2026-08-11). A delta feed at a tenth the bytes fits inside the
  existing window; the alternative is a rebuilt ESP-IDF. Still must ship a
  heartbeat/keepalive, or DepthCharge's ~80 ms-republish liveness watchdog false-greys a
  quiet-but-live book (strain 10). DepthCharge is its future test client; feeder realism
  (Hawkes arrivals / mirror mode / FrontierView execution-algo participant); TLS-chain
  rotation is a DepthCharge-firmware-pinned dependency (an Anvil CA/chain change = a
  lock-step firmware update); **per-socket send-queue behaviour — document it in
  PROTOCOL.md *and establish whether it bounds queue depth.*** The earlier wording here
  said "coalescing / even backpressure-shedding", on the strength of a 2026-08-09
  rate-and-gap measurement; that conclusion is withdrawn (ARCHITECTURE §9, 2026-08-11).
  Measured on 2026-08-11 with `tools/anvil_freshness_probe.py`: a desk socket throttled to
  25% of the stream sees **every** frame kind thinned by the same fraction and its lag grow
  **linearly to 111 s over 150 s with no plateau**, implying **~12.4 MB still queued for that
  one socket** and rising. A DepthCharge board on one socket accumulated ~98 s of backlog in
  210 s of uptime. **Anvil is not modified from here and no Anvil change is proposed** — this
  is a note on their backlog. The downstream rule stands and is now sharper: never assume a
  thinned stream is a fresh one, and M4/M5 delta venues cannot tolerate either shape without
  gap+resync.
- **DepthCharge: M3 close-out (transport brief closes, docs catch up, Anvil ask rewritten)** — brief written and ready for an Opus session: `docs/briefs/M3-closeout-transport-and-docs.md`. It is the hand-off from the 2026-08-14..16 Fable sessions and supersedes the item below as the next thing to run.
- **DepthCharge: own the websocket client** — SHIPPED 2026-08-15 (`c52b268`, day-soak proven); brief was
  (`docs/briefs/M3-transport-own-the-websocket-client.md`). The 2026-08-13 bench convicted
  `esp_websocket_client` of the recurring socket deaths with labelled evidence (17/17
  errno-silent, off-by-one framing corruption caught in the act) while a minimal owned
  client held one socket 3.7 h / 534 MB / zero deaths pinned to the worst node in the
  house (ARCHITECTURE §9, 2026-08-13; `hardware/bench-2026-08-13-wifi-drop-diagnosis.md`).
  Independent of, and compatible with, the framework-rebuild lever above.
- DepthCharge: Crucible post — book structures under fire (flat_map vs dense window,
  driven by Anvil's *trend* workload).
- Optional **live web mirror** of the panel's `DisplaySnapshot` feed (a browser twin of
  the hardware). *That* would be companion site #4 and trigger the shared-component-repo
  review. The portfolio page itself is scheduled work (MP) inside the portfolio repo and
  does **not** count toward the threshold.
