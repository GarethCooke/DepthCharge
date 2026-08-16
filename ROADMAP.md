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
| M3 | A+B   | Live Anvil on the panel       | Firmware net task (TLS WS, nominated `Origin` header) + render task; engine unchanged from M1; live ladder on the panel; pull-the-Wi-Fi test shows grey stale state then clean resync. | M1, M2 | ☐ **Next** — stages A ✅ (2026-08-07, wait-free `SnapshotChannel`), B ✅ (2026-08-08, streaming allocation-free parser, goldens unchanged), C ✅ (2026-08-09, feed proven on the board), **D ✅ (flashed and long since proven — the 23.6 h soak of 2026-08-15/16 ran the panel continuously; the "not flashed" note this cell carried until 2026-08-16 was stale by a week)**. The transport rewrite is **done and accepted** (owned WS client over esp-tls; 10.9 h on one connection, zero errno-silent deaths, zero framing rejects — brief closed 2026-08-16), the TCP window is rebuilt and is now the default build, and the supervisor recycles a silent socket after five minutes. **The pull-the-Wi-Fi acceptance RAN on 2026-08-16 and FAILED** — the grey half passed (the panel greyed honestly and carried no hue, photographed), the resync half did not: the station could not re-associate and only a power cycle recovered the board. Root cause found, fixed and host-tested the same morning — `WifiSupervisor`'s 1 s refused-retry cadence was aborting every association attempt before it could complete (388 rejoins, 2 AP responses; ARCHITECTURE §9, 2026-08-16 pm). **M3 now needs the acceptance re-run on the fixed build**, which is owner-driven at the bench and is the only thing between here and the tick. Two smaller items are owed and do not gate it: the weak-node 1 h soak (transport brief DoD), and the ghosting re-check with `clkphase = false` (`hardware/BRINGUP.md`). |
| M4 | A     | Kraken adapter                | Delta application + CRC32 verification; dense-window book lands here; Kraken traces + goldens; panel switches venue. | M3 | ☐ |
| M5 | A     | Binance adapter               | Partial-depth easy mode, then full diff stream with REST-snapshot bracketing and gap recovery; traces + goldens.     | M4 | ☐ |
| M6 | B     | Carrier PCB                   | KiCad carrier (WROOM-1-N16R8, 2× 74HCT245, HUB75 IDC, USB-C 5 V/3 A with CC pulldowns, bulk caps, EC11); DRC clean; fabbed & bring-up. | M3 | ☐ |
| M7 | A+B   | Enclosure + board mode        | Printed enclosure, smoked acrylic front; encoder modes: ladder (symbol/venue/zoom) + Anvil 12-ticker summary board mode. | M4, M6 | ☐ |
| MP | A     | Portfolio portal              | **Executes in the `garethcooke-portfolio` repo.** Stage 1 (any time after M0): `/projects/depthcharge` live with In Progress badge, concept art, tags, repo/architecture links, plus two drive-by fixes. Stage 2 (after M3): real hardware photos/video. Stage 3 (any time): the design doc reaches the portal — `/depthcharge/design` renders `docs/DESIGN.html`, tracked by its own `writeup-sources.json` entry so it drifts independently of the architecture page. Briefs: stages 1–2 in the **portfolio** repo at `docs/briefs/MP-portfolio-portal.md`; stage 3 here at [`docs/briefs/MP-design-doc-on-the-portal.md`](docs/briefs/MP-design-doc-on-the-portal.md). | Stage 2: M3 | ◐ Stages 1 ✅ (2026-07-22) and 3 ✅ (2026-08-08, pending commit in garethcooke-portfolio); stage 2 gated on M3 |

## Backlog (not scheduled)

- Anvil-side (lives on Anvil's backlog, cross-referenced only): chaos flag for
  deterministic gap testing; **sequenced incremental L2 feed — promoted 2026-08-11 from
  a nice-to-have to the thing that makes the hardware work at all, and still promoted
  after the firmware side did everything it could.** *Rewritten 2026-08-16: the three
  claims this bullet used to make — that a 5,744-byte window caps the board at
  65.5 KiB/s, that no firmware lever reaches it, and that the alternative is a rebuilt
  ESP-IDF — are all superseded, because the rebuild happened.* `liblwip.a` was rebuilt
  from the shipped esp-idf v4.4.6 vintage with `TCP_WND` 5744 → 17232 and now ships as the
  default build, and a 23.6-hour soak says it is load-bearing: **56–87 KiB/s at every hour
  of the day** (floor 56 mid-morning, ceiling 87 at midnight) against Anvil's
  110.4 KiB/s wire — 60–80% of it, and most hours *above* the stock window's hard cap. The
  RX loop is instrumented and exonerated: `wait 0 / read 98–99 / feed 0` in every hour, so
  the board is bound by how fast bytes come off the wire into it and there is nothing left
  to optimise on this side. Lag slope fell from +0.57 s/s on every stock-window run to
  **+0.083 s/s over the final 10.9 h**. **The residual is path bandwidth on the
  transatlantic hop, and the sized fix is still the delta feed** — a tenth the bytes closes
  it completely — so this stays promoted; until then the object runs tens of seconds behind
  at UK peak and honestly says so (ARCHITECTURE §9, 2026-08-16). **New Anvil-side
  observation, 2026-08-16 00:12:** the WS endpoint went silent mid-stream for **2 min 56 s**
  on a live TCP connection and then resumed on the same connection, while plain HTTPS to
  the same host answered 200 in 0.5 s throughout — so the WS server wedged or restarted
  while the rest of the box was fine. Worth a look at Anvil's WS server, and it gives the
  heartbeat/keepalive item below a second reason. Still must ship a
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
- **DepthCharge: M3 close-out (transport brief closes, docs catch up, Anvil ask rewritten)**
  — DONE 2026-08-16 (`66e2f77`…`79c1867` plus the docs commit):
  `docs/briefs/M3-closeout-transport-and-docs.md` is executed and its session log is the
  hand-off. The old client's build arms are deleted, the rebuilt-window framework is the
  default build, `WsSupervisor` recycles a silent socket after five minutes, and the
  transport brief's DoD is ticked except the weak-node hour.
- **DepthCharge: weak-node 1 h soak** — the one transport-brief acceptance bar never run on
  the final build. Owner-driven at the bench: pin or re-roll the association to a −7x
  sibling, hold it an hour, and check that the fades grey the panel without killing the
  socket (board B's `…:F9` record is the comparison). Small, and it is the last thing
  standing between the transport rewrite and a fully ticked DoD.
- ~~**DepthCharge: own the websocket client**~~ — SHIPPED. Client landed 2026-08-15
  (`c52b268`), day-soak proven, and the old client deleted from the tree 2026-08-16
  (`66e2f77`). Brief: `docs/briefs/M3-transport-own-the-websocket-client.md`, closed with
  its acceptance numbers in ARCHITECTURE §9 (2026-08-16).
- DepthCharge: Crucible post — book structures under fire (flat_map vs dense window,
  driven by Anvil's *trend* workload).
- Optional **live web mirror** of the panel's `DisplaySnapshot` feed (a browser twin of
  the hardware). *That* would be companion site #4 and trigger the shared-component-repo
  review. The portfolio page itself is scheduled work (MP) inside the portfolio repo and
  does **not** count toward the threshold.
