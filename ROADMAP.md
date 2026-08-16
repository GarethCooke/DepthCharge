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
| M3 | A+B   | Live Anvil on the panel       | Firmware net task (TLS WS, nominated `Origin` header) + render task; engine unchanged from M1; live ladder on the panel; pull-the-Wi-Fi test shows grey stale state then clean resync. | M1, M2 | ✅ **Done (2026-08-16)** — the pull-the-Wi-Fi acceptance passed on its second run: panel greys with no hue, station rejoins **unaided** (no reset in the log), deauth → LIVE in **18.7 s**, one rejoin call. Record: [`hardware/bench-2026-08-16-pull-the-wifi-acceptance.md`](hardware/bench-2026-08-16-pull-the-wifi-acceptance.md). The first run **failed** and found the rejoin livelock (ARCHITECTURE §9 2026-08-16 pm, DESIGN strain 20) — that failure is the best argument this project has produced for keeping a bench criterion in a DoD. Stages A ✅ (2026-08-07, wait-free `SnapshotChannel`), B ✅ (2026-08-08, streaming allocation-free parser, goldens unchanged), C ✅ (2026-08-09, feed proven on the board), **D ✅ (flashed and long since proven — the 23.6 h soak of 2026-08-15/16 ran the panel continuously; the "not flashed" note this cell carried until 2026-08-16 was stale by a week)**. The transport rewrite is **done and accepted** (owned WS client over esp-tls; 10.9 h on one connection, zero errno-silent deaths, zero framing rejects — brief closed 2026-08-16), the TCP window is rebuilt and is now the default build, and the supervisor recycles a silent socket after five minutes. Two items remain owed against M3's *brief* and neither gates this tick: the weak-node 1 h soak (transport brief DoD, never run on the final build), and the ghosting re-check with `clkphase = false` (`hardware/BRINGUP.md`). |
| M4 | A     | Kraken adapter                | Delta application + CRC32 verification; dense-window book lands here; Kraken traces + goldens; panel switches venue. | M3 | ☐ **Next** — and it inherits two things from M3. A venue's byte rate is a design input (DESIGN strain 19: the object already runs at 60–80% of Anvil's wire and Kraken's full-depth stream is larger), and its DoD should include at least one criterion the harness structurally cannot stage (strain 20). |
| M5 | A     | Binance adapter               | Partial-depth easy mode, then full diff stream with REST-snapshot bracketing and gap recovery; traces + goldens.     | M4 | ☐ |
| M6 | B     | Carrier PCB                   | KiCad carrier (WROOM-1-N16R8, 2× 74HCT245, HUB75 IDC, USB-C 5 V/3 A with CC pulldowns, bulk caps, EC11); DRC clean; fabbed & bring-up. | M3 | ☐ |
| M7 | A+B   | Enclosure + board mode        | Printed enclosure, smoked acrylic front; encoder modes: ladder (symbol/venue/zoom) + Anvil 12-ticker summary board mode. | M4, M6 | ☐ |
| MP | A     | Portfolio portal              | **Executes in the `garethcooke-portfolio` repo.** Stage 1 (any time after M0): `/projects/depthcharge` live with In Progress badge, concept art, tags, repo/architecture links, plus two drive-by fixes. Stage 2 (after M3): real hardware photos/video. Stage 3 (any time): the design doc reaches the portal — `/depthcharge/design` renders `docs/DESIGN.html`, tracked by its own `writeup-sources.json` entry so it drifts independently of the architecture page. Briefs: stages 1–2 in the **portfolio** repo at `docs/briefs/MP-portfolio-portal.md`; stage 3 here at [`docs/briefs/MP-design-doc-on-the-portal.md`](docs/briefs/MP-design-doc-on-the-portal.md). | Stage 2: M3 | ◐ Stages 1 ✅ (2026-07-22) and 3 ✅ (2026-08-08, pending commit in garethcooke-portfolio); **stage 2 is UNGATED as of 2026-08-16** — M3 is done, so the real hardware photos/video can go up, and the stale-panel shot from the acceptance is a good one |

## Backlog (not scheduled)

Items carry a stable ID — `A*` Anvil-side, `D*` DepthCharge-side — so briefs and session
logs can cite one without quoting it. Each is a one-line **what**, then indented detail only
where there is evidence to carry. Closed items move to the bottom rather than being deleted.

### Anvil-side (cross-referenced only)

**Anvil is a separate repo and is not modified from here.** Nothing below is a proposal to
Anvil; each is a note of what DepthCharge is waiting on, working around, or has measured
from the outside. They live on Anvil's own backlog.

| ID | Item                            | Standing                                      |
| -- | ------------------------------- | --------------------------------------------- |
| A1 | Sequenced incremental L2 feed   | **Promoted** — makes the hardware work at all |
| A2 | Heartbeat / keepalive           | Needed — two independent reasons              |
| A3 | Per-socket send-queue behaviour | Open question; document in `PROTOCOL.md`      |
| A4 | Chaos flag for gap testing      | Nice-to-have                                  |
| A5 | Feeder realism                  | Nice-to-have                                  |
| A6 | TLS-chain rotation              | Lock-step dependency; no action yet           |

**A1 · Sequenced incremental L2 feed.** Promoted 2026-08-11 from a nice-to-have to the thing
that makes the hardware work at all — and *still* promoted after the firmware side did
everything it could. A tenth the bytes closes the gap completely.

- *Rewritten 2026-08-16.* The three claims this item used to make — that a 5,744-byte window
  caps the board at 65.5 KiB/s, that no firmware lever reaches it, and that the alternative
  is a rebuilt ESP-IDF — are all superseded, because the rebuild happened.
- **What shipped instead:** `liblwip.a` rebuilt from the shipped esp-idf v4.4.6 vintage with
  `TCP_WND` 5744 → 17232; now the default build.
- **Soak (23.6 h, 2026-08-15/16)** says it is load-bearing: **56–87 KiB/s at every hour of
  the day** (floor 56 mid-morning, ceiling 87 at midnight) against Anvil's 110.4 KiB/s wire —
  60–80% of it, and most hours *above* the stock window's hard cap. Lag slope fell from
  +0.57 s/s on every stock-window run to **+0.083 s/s over the final 10.9 h**.
- **RX loop instrumented and exonerated:** `wait 0 / read 98–99 / feed 0` in every hour, so
  the board is bound by how fast bytes come off the wire into it, and there is nothing left
  to optimise on this side.
- **Residual is path bandwidth** on the transatlantic hop, and the sized fix is still the
  delta feed — so this stays promoted. Until then the object runs tens of seconds behind at
  UK peak and honestly says so (ARCHITECTURE §9, 2026-08-16).

**A2 · Heartbeat / keepalive.** Two independent reasons now.

- Without one, DepthCharge's ~80 ms-republish liveness watchdog false-greys a
  quiet-but-live book (strain 10).
- **Observed 2026-08-16 00:12:** the WS endpoint went silent mid-stream for **2 min 56 s** on
  a live TCP connection and then resumed on the same connection, while plain HTTPS to the
  same host answered 200 in 0.5 s throughout — so the WS server wedged or restarted while the
  rest of the box was fine. Worth a look at Anvil's WS server in its own right.

**A3 · Per-socket send-queue behaviour.** Document it in `PROTOCOL.md` *and establish whether
it bounds queue depth.*

- The earlier wording here said "coalescing / even backpressure-shedding", on the strength of
  a 2026-08-09 rate-and-gap measurement; that conclusion is **withdrawn**
  (ARCHITECTURE §9, 2026-08-11).
- **Measured 2026-08-11** with `tools/anvil_freshness_probe.py`: a desk socket throttled to
  25% of the stream sees **every** frame kind thinned by the same fraction and its lag grow
  **linearly to 111 s over 150 s with no plateau**, implying **~12.4 MB still queued for that
  one socket** and rising. A DepthCharge board on one socket accumulated ~98 s of backlog in
  210 s of uptime.
- **Downstream rule** — stands, and is now sharper: never assume a thinned stream is a fresh
  one, and M4/M5 delta venues cannot tolerate either shape without gap + resync.

**A4 · Chaos flag** for deterministic gap testing.

**A5 · Feeder realism** — Hawkes arrivals / mirror mode / FrontierView execution-algo
participant. DepthCharge is its future test client.

**A6 · TLS-chain rotation** — a DepthCharge-firmware-pinned dependency: an Anvil CA/chain
change means a lock-step firmware update.

### DepthCharge-side

**D1 · The rejoin re-rolls the mesh lottery, and the board never roams off what it draws.**
*[A] — one session, host-testable policy half.*

- **Measured 2026-08-16** during the passing acceptance: boot's explicit scan joined at
  **−40 dBm**, the recovery's plain `WiFi.begin()` landed at **−70** and drifted to −76, and
  the watchdog greys went from 1 to **14 in three minutes**.
- **Why:** `connect_wifi()` surveys and joins the strongest by name; the supervisor's rejoin
  cannot, because a blocking all-channel scan on loopTask would stall the 250 ms supervise
  poll. `ALL_CHANNEL_SCAN` + `BY_SIGNAL` narrow the lottery and do not close it — which is
  what §9 (2026-08-13) already recorded when they failed their five-boot acceptance, and the
  boot fix was never extended to this path.
- **The shape of the fix is already established by M3:** the socket connect used to block
  loopTask and now runs on the RX task, with the supervisor deciding and the task doing; the
  rejoin's scan-then-join wants the same treatment.
- Details in [`hardware/bench-2026-08-16-pull-the-wifi-acceptance.md`](hardware/bench-2026-08-16-pull-the-wifi-acceptance.md).

**D2 · Weak-node 1 h soak.** *[B] — owner-driven at the bench.* The one transport-brief
acceptance bar never run on the final build: pin or re-roll the association to a −7x sibling,
hold it an hour, and check that the fades grey the panel without killing the socket (board
B's `…:F9` record is the comparison). Small, and the last thing standing between the
transport rewrite and a fully ticked DoD. *The board is currently sitting on a −75 dBm
association by accident, which is most of the setup.*

**D3 · Crucible post — book structures under fire.** flat_map vs dense window, driven by
Anvil's *trend* workload.

**D4 · Live web mirror** *(optional)* — a browser twin of the panel's `DisplaySnapshot` feed.
*That* would be companion site #4 and trigger the shared-component-repo review. The portfolio
page itself is scheduled work (MP) inside the portfolio repo and does **not** count toward
the threshold.

### Closed

**~~M3 close-out~~** — transport brief closes, docs catch up, Anvil ask rewritten. DONE
2026-08-16 (`66e2f77`…`79c1867` plus the docs commit):
`docs/briefs/M3-closeout-transport-and-docs.md` is executed and its session log is the
hand-off. The old client's build arms are deleted, the rebuilt-window framework is the
default build, `WsSupervisor` recycles a silent socket after five minutes, and the transport
brief's DoD is ticked except the weak-node hour (D2).

**~~Own the websocket client~~** — SHIPPED. Client landed 2026-08-15 (`c52b268`), day-soak
proven, and the old client deleted from the tree 2026-08-16 (`66e2f77`). Brief:
`docs/briefs/M3-transport-own-the-websocket-client.md`, closed with its acceptance numbers in
ARCHITECTURE §9 (2026-08-16).
