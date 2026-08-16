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
| M4 | A     | Kraken adapter                | Delta application + CRC32 verification; dense-window book lands here; Kraken traces + goldens; panel switches venue. | M3 | ☐ **Next** — and it inherits two things from M3. A venue's byte rate is a design input (DESIGN strain 19: the object already runs at 51–79% of Anvil's wire and Kraken's full-depth stream is larger), and its DoD should include at least one criterion the harness structurally cannot stage (strain 20). |
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

**The hand-over document is [`docs/anvil-handover-2026-08-16.md`](docs/anvil-handover-2026-08-16.md)**
— these items written for an Anvil session, with the evidence and the file-level detail. Give
it to an Anvil CC; keep this table as the index. Anvil's own backlog is one bullet at the tail
of its `docs/anvil-plan.md`, and its claim is the one this repo withdrew (A3), so the first
thing the hand-over asks for is that the section be rebuilt around these IDs.

| ID | Item                            | Standing                                                          |
| -- | ------------------------------- | ----------------------------------------------------------------- |
| A7 | `depth` parameter on `/ws`      | **Do first** — cheapest, and it closes the staleness gap outright |
| A1 | Sequenced incremental L2 feed   | Right long-term; **no longer blocking** if A7 lands               |
| A2 | Heartbeat / keepalive           | Mostly answerable on this side — see D5                           |
| A3 | Per-socket send-queue behaviour | **Unbounded** — answered by Anvil's source; document it           |
| A4 | Chaos flag for gap testing      | Nice-to-have; near-required alongside A1                          |
| A5 | Feeder realism                  | Nice-to-have                                                      |
| A6 | TLS-chain rotation              | Lock-step dependency; a one-line ask, worth now                   |

The detail below runs in ID order **except A7, which comes first because it is both the
cheapest item here and the largest win** — measured 2026-08-16, and it demoted A1.

**A7 · A `depth` parameter on `/ws`** — `?ticker=101&depth=32`, truncating that socket's
`snapshot`/`book` frames; default unchanged so the web client is untouched.

- **Measured 2026-08-16** across both committed captures (`tools/anvil_frame_economics.py`,
  written for this): `book` frames are **98% of Anvil's wire** at a mean **8,428 bytes**, and
  they carry **~205 levels** — `ANVIL_BOOK_DEPTH` defaults to 0, "all resting levels", which is
  deliberate for the web client's scrollable ladder. **DepthCharge renders 27 a side and
  discards the rest.**
- **Truncated to 27/side the whole stream falls to 27.8% of its current size — 110.4 KiB/s
  becomes ~31 KiB/s**, against the soak's measured floor of 56 KiB/s. It fits inside the *worst*
  hour of the day with ~2× headroom, with no protocol redesign, no sequencing and no resync.
- `GET /api/book?ticker=&depth=` already takes exactly this parameter, and `/ws`'s `onaccept`
  already parses `?ticker=` out of the upgrade — so the concept, the name and the plumbing all
  exist. The one real design question is Anvil's: per-socket depth breaks the
  serialise-once-fan-out-to-all path (answer: serialise once per distinct depth in use).
- **Nothing in `engine/` changes to consume it** — the adapter takes 83–126 levels/side today
  and truncates above `kMaxSnapshotLevels`; shallower is trivially in contract. New traces and
  goldens would be owed once the wire changes, not before.

**A1 · Sequenced incremental L2 feed.** Promoted 2026-08-11 to the thing that makes the
hardware work at all; **demoted 2026-08-16 by A7** — a query parameter gets ~80% of the benefit
for ~2% of the work, so this is no longer blocking and should be judged on its own merits. On
those it is still the best answer, and the firmware side is still out of levers.

- *Sized 2026-08-16.* "A tenth the bytes" was a 3× under-claim: **a median of one level changes
  between consecutive `book` frames** (mean 1.1, p99 3, max 4 over 90 s), so an upper-bound
  delta encoding is **1.3% of current `book` bytes** — against A7's 26.5%.
- Its load-bearing prerequisite is a **per-ticker monotonic sequence**: the global stamp is
  sparse *and* non-monotonic per socket by design, and unlike a full replace, a missed delta
  never heals. PROTOCOL §1 already names this as the change "if strict gap detection is ever
  wanted". Worth doing first and alone, even if deltas never follow.
- **It is not Stage 5** and must not wait for it — Stage 5 is binary ITCH over LAN UDP
  multicast, gated behind Stage 4.

- *Rewritten 2026-08-16.* The three claims this item used to make — that a 5,744-byte window
  caps the board at 65.5 KiB/s, that no firmware lever reaches it, and that the alternative
  is a rebuilt ESP-IDF — are all superseded, because the rebuild happened.
- **What shipped instead:** `liblwip.a` rebuilt from the shipped esp-idf v4.4.6 vintage with
  `TCP_WND` 5744 → 17232; now the default build.
- **Soak (23.6 h, 2026-08-15/16)** says it is load-bearing: **56–87 KiB/s at every hour of
  the day** (floor 56 mid-morning, ceiling 87 at midnight) against Anvil's 110.4 KiB/s wire —
  **51–79%** of it (corrected 2026-08-16 from "60–80%", which was arithmetic, not measurement:
  56/110.4 = 50.7%), and most hours *above* the stock window's hard cap. Lag slope fell from
  +0.57 s/s on every stock-window run to **+0.083 s/s over the final 10.9 h**.
- **RX loop instrumented and exonerated:** `wait 0 / read 98–99 / feed 0` in every hour, so
  the board is bound by how fast bytes come off the wire into it, and there is nothing left
  to optimise on this side.
- **Residual is path bandwidth** on the transatlantic hop. Until a smaller feed exists the
  object runs tens of seconds behind at UK peak and honestly says so (ARCHITECTURE §9,
  2026-08-16) — but the *smaller feed* no longer has to be this item. A7 is.

**A2 · Heartbeat / keepalive.** Two reasons, and the first one is probably ours.

- Without one, DepthCharge's ~80 ms-republish liveness watchdog false-greys a
  quiet-but-live book (strain 10). **2026-08-16: Crow answers an unsolicited client PING with a
  PONG with no application code, and the pong cannot overtake anything already queued on that
  socket** — so a client-side ping measures **the server's write-path backlog for this
  connection**, which is stronger than TCP liveness and still blind to producer-side lag in the
  broadcaster. Ours to build (D5); the Anvil half shrinks to "please treat that ordering as a
  contract, and say so in `PROTOCOL.md`" — they never claimed it, it is stock vendored Crow.
- **Observed 2026-08-16 00:12:** the WS endpoint went silent mid-stream for **2 min 56 s** on
  a live TCP connection and then resumed on the same connection, while plain HTTPS to the
  same host answered 200 in 0.5 s throughout. *Attribution corrected 2026-08-16:* the earlier
  wording here ("so the WS server wedged") over-read it. nginx is excluded (`proxy_read_timeout
  3600s`), but a healthy HTTPS request on a **different** connection cannot prove *this* flow
  was healthy, so a server stall and a single transatlantic flow stalling in RTO backoff are
  not separable from here. One unreproduced observation; worth Anvil's eyes, not a verdict.

**A3 · Per-socket send-queue behaviour.** *The open question is closed: it queues, without
bound.* What remains is documenting it, and Anvil's call on whether to bound it.

- The earlier wording here said "coalescing / even backpressure-shedding", on the strength of
  a 2026-08-09 rate-and-gap measurement; that conclusion is **withdrawn**
  (ARCHITECTURE §9, 2026-08-11).
- **Measured 2026-08-11** with `tools/anvil_freshness_probe.py`: a desk socket throttled to
  25% of the stream sees **every** frame kind thinned by the same fraction and its lag grow
  **linearly to 111 s over 150 s with no plateau**, implying **~12.4 MB still queued for that
  one socket** and rising. A DepthCharge board on one socket accumulated ~98 s of backlog in
  210 s of uptime.
- **Confirmed in Anvil's source 2026-08-16**, so it is construction rather than weather:
  `CrowWsSubscriber::deliver()` → `conn.send_text()` → Crow's per-connection `write_buffers_`,
  a `std::vector<std::string>` with **no cap, no drop policy and no coalescing** on the path.
  One socket that stops reading costs the server ~110 KiB/s of RAM for as long as it stays
  connected.
- **Downstream rule** — stands, and is now sharper: never assume a thinned stream is a fresh
  one, and M4/M5 delta venues cannot tolerate either shape without gap + resync.

**A4 · Chaos flag** for deterministic gap testing. Near-required if A1 ever happens: a delta
feed's gap-recovery path cannot be proven without one.

**A5 · Feeder realism** — Hawkes arrivals / mirror mode / FrontierView execution-algo
participant. DepthCharge is its future test client.

**A6 · TLS-chain rotation** — a DepthCharge-firmware-pinned dependency: an Anvil CA/chain
change means a lock-step firmware update. *Sharpened 2026-08-16:* the firmware pins **ISRG
Root X1** as its only anchor and Anvil deploys with certbot behind nginx, so ordinary renewals
are invisible **so long as the renewed leaf still chains to X1**; a CA move or any shortening
of the presented chain (there are *two* cross-sign hops, and YE←X2 is the newer) is loud on
DepthCharge's serial console and silent on Anvil's. **The whole ask is one line in Anvil's
`deploy/README.md`** — tell DepthCharge before changing CA or chain configuration — or an
explicit `--preferred-chain`, which survives a certbot default change in a way a README line
does not.

- **Owed here, found 2026-08-16:** `firmware/src/anvil_root_ca.hpp`'s own header says the
  ESP-IDF bundle is unreachable because `WiFiClientSecure` offers only `cert_pem`. **That died
  with the M3 transport rewrite** — `ws_transport.cpp` builds an `esp_tls_cfg_t` by hand, which
  has `crt_bundle_attach`, and the framework ships `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` with
  200 certs including X1. The pin is now a *choice*, not a constraint; the comment is stale and
  should say so, and "switch to the bundle" is a live option worth its own decision.

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

**D5 · Ping the venue instead of waiting on it — the liveness watchdog gets a clock.**
*[A] — small, host-testable, and it retires most of A2.* **Found 2026-08-16 in Anvil's vendored
Crow:** an unsolicited client PING is answered with a PONG by the library, no application code
involved, and the pong goes into the *same* per-connection write queue as data — it **cannot
overtake frames already queued**, so a round-trip prices the server's write-path backlog for
this connection. Stronger than TCP liveness, and honestly bounded: it is **blind to
producer-side lag** (a pong is posted ahead of frames the broadcaster has not handed over yet),
the ordering is one-sided, and this is read from source, never yet captured under induced
backpressure — worth a desk experiment before any firmware depends on it. Work: send on a
cadence from the RX task, feed the round-trip into `SupervisorInput`, and let the panel
distinguish "quiet book" from "minutes behind". Wants the Anvil half of A2 first, which costs
Anvil a sentence.

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
