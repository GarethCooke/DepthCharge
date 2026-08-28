# M5 Stage D-A2 — the seed on the board

**Track:** Mixed [desk, ending in one flash] · **Status:** **Done 2026-08-29 — a live Binance book on the panel.** · **Size:** one evening
**Executor:** Claude Code. **No panel judgement** — what the board *renders* is D-B's.

D-A1 left a board that connects, holds diffs and draws an honest grey ladder with nothing servicing
its eight re-seed requests. This evening fetches the seed, so the bracket can be satisfied and a
Binance book reaches the panel for the first time. **`seeds_unconfirmed` becomes a meaningful reading
here** — D-A1's brief asked for it before a REST client existed, which was that brief's error.

---

## 1 · A concurrent TLS session does not fit, and that is the decision

**Measure this before building anything.** The seed is an HTTPS GET to `data-api.binance.vision`
while the WebSocket session to `data-stream.binance.vision` is up — **two concurrent TLS sessions**,
and this board has never had two.

| quantity | value | source |
| --- | ---: | --- |
| mbedTLS per session | **two contiguous 16,717 B blocks**, pinned internal (`esp_mbedtls_mem_calloc`, `MALLOC_CAP_INTERNAL \| MALLOC_CAP_8BIT`) | `hardware/bench-2026-08-11-feed-lag.md:360` |
| largest free internal block, Binance build, socket up | **17,396 B** | D-A1 bench |

One block clears by 679 B. **A second session needs two more and there is nowhere to put them.**
Neither `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC` nor `EXTERNAL` is set in the tree, so the IDF default —
internal — is in force.

**And the reserve does not cover this.** `kReserveInternalBytes = 80 KiB` was justified on a worst
draw of **62,140 B measured with one session**. D-A1's cut is sound for the build it was measured on
and **its evidence does not reach a two-session build.** Re-measure the draw with the fetch running;
if it exceeds the reserve, the reserve moves back and the panel loses a rung — which is the honest
outcome, not a failure.

**Four candidates, and the first is not a general answer:**

- **(a) Fetch before the WebSocket opens.** Sequential, no concurrency, and it works for the *initial*
  seed. **It cannot serve a re-seed**, which fires on a live book by design — so it solves this
  evening and leaves D-A3 with the same wall. Acceptable only if taken deliberately as a staging step.
- **(b) Tear the socket down for the fetch.** Needs the diff stream buffered across a ≤15 s fetch:
  B2 measured **~150 events / ~8,200 levels**, against `buf_lvl_`'s 2,048. **It does not fit** — and
  it breaks bracket continuity. Ruled out here rather than left open.
- **(c) `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`** — mbedTLS to PSRAM. Continuous with D-A1's precedent
  and it solves both sessions at once. **But it is global**: it moves the *WebSocket's* TLS buffers
  too, which is the per-frame hot path, and that is exactly the objection that kept `bids_`, `asks_`
  and `frame_` internal. **Measure the latency cost — do not assume it.** `worst_parse_us` and the
  frame cadence are already printed.
- **(d) A smaller TLS record for the REST session only** — `esp_tls` per-connection config with a
  reduced `in_content_len` and `max_fragment_length` negotiated. Surgical, touches nothing the
  WebSocket uses. Verify the venue honours the fragment negotiation before relying on it.

**Recommendation: measure first, then (d) if the venue negotiates, (c) if it does not, and (a) only
as a declared staging step with D-A3 named as the owner of the general case.** Say which and why.

*One ordering observation worth testing while measuring: the panel sizes itself from free internal at
`panel.begin()`, before any TLS session exists. If the REST session were established first, the
budget would be honest rather than a guess. That may be the cheapest half of the answer.*

## 2 · The REST client

One GET to `data-api.binance.vision` for `/api/v3/depth?symbol=…&limit=1000`. **The body is 64,046 B**
— eleven committed BTCUSDT bodies, all identical, and the length is constant because this pair's
integer-digit width is constant on this tape, **not** because of 8-dp padding.

`on_rest_body(std::string_view)` takes it whole, so the body needs a 64 KB buffer. **PSRAM**, on
D-A1's precedent — written once, read once, freed, never DMA'd — with the same `unique_ptr` +
`new (std::nothrow)` shape and the same degradation path if it is null. Reuse the transport's TLS
machinery; do not write a second.

## 3 · The seed adopted, and remedy (a) confirmed on hardware

The bracket path already exists. What is new is that a diff can now satisfy it: `seed_bracket_ok`
goes non-zero, `FeedStatus` reaches `Live`, and the panel draws a Binance book. **`seeds_unconfirmed`
now counts something** — a seed withheld because the feed never corroborated it.

`kBinanceReseedCoverLevels` = 448, `kBinanceReseedMarginLevels` = 192. **Do not move either** — B2
sized the margin as 168 plus 14% with its reason stated, and this stage has no evidence that bears
on it.

## 4 · Re-measure what the second session costs

Print, on every fetch: largest free internal block before and after, `free_total`, and the draw
against `kReserveInternalBytes`. **The reconnect check D-A1 recorded beside the reserve now has a
sibling** — *below 16,717 B the reserve cut is wrong at the second socket* becomes *…at the second
socket or the seed fetch, whichever comes first.* Record both.

## 5 · Flash, and one capture

Board boots double-buffered, connects, fetches, brackets, draws a **live** Binance book.
`seed_bracket_ok` non-zero, `reseeds` no longer climbing unanswered, heap figures printed at the
fetch. **Do not judge how the ladder looks.**

## 6 · Writeback

Session log; `ROADMAP.md`; `NOTES-binance.md` with provenance beside every measured figure; §9 for
the TLS-concurrency ruling, which is architectural; `docs/DESIGN.html` where a card moves.

---

## Constraints

- **No panel judgement.** D-B's.
- **§6 frozen, §4 does not move.** No new `GapReason`, no new `FeedEvent::Kind`. **Eighth asking.**
- **Not in this stage:** the re-seed *mechanism* and its memory (strain 28), the liveness ping wire,
  and the soak instrumentation. **All D-A3** — see below.
- **Nothing moves a pin or a golden.** `sizeof(DisplaySnapshot)` stays 1,168 on both.
- `venue_budget.hpp`'s `static_assert` must still hold, or its firing is a **decision** and not a
  licence to raise the number.
- Per-commit verification in a fresh detached worktree, `CMAKE_HOME_DIRECTORY` read from the cache
  before the pass is believed, loop inline, `host-mingw`.
- **Push to `m5/stage-d-a2`. Fast-forward `master` only once the ladder closes. Commit only when asked.**

## Known unknowns — resolve and record

Whether the venue negotiates a reduced TLS fragment. What `EXTERNAL_MEM_ALLOC` costs the WebSocket's
frame path, measured. Whether the two-session draw exceeds 80 KiB. Whether establishing the REST
session before `panel.begin()` changes the budget honestly.

## Definition of done

- ☒ The TLS-concurrency question **measured and decided**: (d) ruled out on the wire — the venue does
      not negotiate `max_fragment_length`; (c) is a **framework rebuild**, not a flag, because
      `libmbedtls.a` is precompiled and `INTERNAL_MEM_ALLOC 1` is explicitly set; (a)/(b) unnecessary
      once `FramePipe` left `.bss`. Both sessions now coexist.
- ☒ Two-session draw measured: **44,296–47,744 B** against the 106,496 B reserved, so the total was
      over-forecast — but **fragmentation was badly under-forecast**: largest free internal block falls
      to **3,060–4,596 B** during a fetch, an order of magnitude below the 16,717 B one session needs.
      The reserve did not move; the **non-overlap rule** is what makes that safe.
- ☒ REST client fetching **64,046 B** — byte-exact to the eleven committed bodies — into a 96 KiB PSRAM
      buffer, over its own esp-tls session sharing the venue's pinned anchor.
- ☒ Seed adopted, **`bracket ok=4 FAIL=0 unconfirmed=0`** on hardware.
- ☒ Largest-block printed at every fetch, with the 16,717 B threshold warned on when crossed. The
      D-A1 reconnect check now reads *“at the second socket or the seed fetch, whichever comes first”*.
- ☒ **Flashed: a live Binance book on the panel** — `live=1 rows=54/54 unknown=0`, held continuously
      from 60 s to 140 s with `oversize=0` and the seed line static.
- ☒ ctest green (50/50, 434 doctest cases); writeback done; split laddered and pushed to
      `m5/stage-d-a2`.

## Out of scope

The re-seed **mechanism** and its memory, `DisplaySnapshot::reseed` → `InFlight`, the **liveness ping
wire**, and the **soak instrumentation** — **D-A3**, which is the last desk evening before the bench.
*Split from this stage deliberately: the ping wire changes a signature across all three venues, and
the re-seed mechanism depends on §1's answer. Bundling either with a first-ever REST client makes a
failure unattributable — and this seat has under-sized three stages running, all in the same
direction.*
Every rendering decision (**D-B**). The soak (**D-C**). The multiplier falsifier (**D-C**, and it
needs D-A3's ping wire before it can be tested at all).

## Session log

<!-- Append one block per session: date · model · done · decisions (with why) ·
     exact next step for the following session. -->

### 2026-08-28/29 · Claude Opus 5 (1M) · desk + bench — **a live Binance book**

**Done.** All seven DoD boxes. The panel holds a live Binance ladder, `bracket ok=4 FAIL=0`,
`oversize=0`, seed line static. Host 50/50 (434 doctest cases, up from 409); all seven images build.

**§1's answer, and both of the brief's favoured candidates were dead.** (d) — measured on the wire:
`data-api.binance.vision` does **not** negotiate `max_fragment_length`; the ServerHello extension list
is byte-identical with and without the request and extension id=1 is absent from both. (c) — the brief
says *“neither `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC` nor `EXTERNAL` is set”*; in fact `INTERNAL_MEM_ALLOC
1` **is** set (`qio_opi/include/sdkconfig.h:548`) and `libmbedtls.a` is **precompiled**, so (c) is a
second framework rebuild rather than a build flag. What made both moot was a fifth candidate the brief
did not list: **`FramePipe` to PSRAM**, which returned 65,536 B of internal SRAM and took the steady
largest block 17,396 → 57,332 B. Two sessions then fit.

**The pre-seed buffer was sized against the wrong quantity, and the deadline was unkeepable.** Its own
comment sized it at ~2.5× a *round trip*; what it must survive is `kBinanceFetchDeadlineMs`, 10× larger.
At 64/2,048 the buffer overflowed after a median **6.20 s** and **0 of 2,668 fifteen-second windows
survived**. Re-sized to **256/32,768** against a measured worst 15 s window of **152 events / 12,458
levels** — the event bound is cadence-bounded (15 s ÷ 100 ms = 150, so 1.68× guards jitter, not the
market), the level bound is market-bounded and gets 2.63×. `buf_` followed `buf_lvl_` to PSRAM, so the
adapter's INTERNAL footprint **fell 2,040 B while the horizon rose 6×**. §9 row written.

**D2, run as the safety net you asked for, answered sharper than the inference.** Not “the body cannot
bracket”: the body **is** adopted, fails its bracket against the zeroed buffer, and is dropped inside
one call — 50 IP weight for a ladder that lived one function call. So abandon-on-overflow is a real
saving. It is also the suite's first path that exercises `seeds_unconfirmed`.

**The non-blocking design does not work on this build, and that was the stop-and-report.**
`esp_tls_conn_new_async` was polled **1,780 times over 15 s, four runs running**, never leaving
`conn_state=1` (CONNECTING) with `errno=119` (EINPROGRESS). Its CONNECTING dispatch jumps past the
`FD_ZERO`/`FD_SET` that arms its `fd_set`s, and `select()` zeroes those on timeout. **Partly
corroborated only:** raising `timeout_ms` 2 → 600 ms changed step duration not at all, which the
empty-set account alone does not explain — the outcome is certain and was measured four times, the
mechanism is recorded as inferred. The synchronous call succeeds first time.

**What the gate was worth.** Everything else in the contract WAS verified by disassembly before a line
was written, and two of those verifications were load-bearing: a `select()` timeout returns 0 with
`conn_state` untouched (so the connect *is* pollable in principle), and `mbedtls_ssl_set_hostname` —
which sets both SNI and the CN target — is given `cfg->common_name` when non-NULL, which is what makes
the literal-IP connect safe. `getaddrinfo` is called unconditionally inside the connect with no
`AI_NUMERICHOST`, and DNS on this board measured 14,000 ms, so the fetch takes a **dotted quad** and
`warm_dns` resolves the seed host on the RX task, publishing it as **one atomic 32-bit word** — a
`char[16]` could tear into a valid-looking wrong address.

**Two of my own bugs, both found on hardware and both worth recording.** `service_seed` formatted the
address into a **switch-case local** and `RestFetch` kept the pointer; esp-tls calls `conn_new_async`
on every step, so it read dead stack — the board reported `couldn't get hostname for :16.7<garbage>`.
And a heredoc turned `'\0'` into a **literal NUL byte** in the source, which compiled. The first is
now impossible (the address is owned); the second is the hazard my own notes warn about.

**The third task, and the two grounds for the non-overlap rule.** A blocking fetch on the feed task
stalled it **1.93 s**, dropped ~15 pipe messages and **broke the very bracket the seed exists to
satisfy** — `bracket ok=0 FAIL=1 dropped=116`, measured. Moving it to its own task at priority 4
(below the feed's 5, so the feed preempts it structurally) took that to `ok=3 FAIL=0 dropped=18`.
A fetch and a TLS reconnect never overlap, on two independent grounds: **memory** — a fetch takes the
largest free internal block to **3,060–4,596 B**, an order of magnitude below the 16,717 B one session
needs, so a reconnect against it cannot fit and the failure would land on the reconnect; and
**correctness** — a body is bracketed against the diff stream, so once the socket drops the body is
worthless before it arrives. Enforced in host-tested policy, not in transport discipline. **Abandon
does not interrupt a syscall**: the flag is checked between esp-tls calls, so the honest bound is one
`kFetchCallTimeoutMs` (5 s), stated rather than implied. Two §9 rows; `frame_pipe.hpp`'s *“exactly
two… deliberately the only two”* amended rather than quietly falsified.

**`kFrameCapacity` 16 KiB → 64 KiB, and its own instruction is what asked for it.** *“If `oversize` is
ever non-zero on the bench, raise this constant”* — it was (`oversize=4` within 90 s), so it is.
**And the paragraph above it is Anvil's reasoning, false at a diff venue:** *“a dropped frame costs one
refresh”* holds only where frames are idempotent full replaces. Here a dropped diff drops the whole
book. **64 KiB is 2.29× the largest observed message on this venue's stream (28,639 B)**, deliberately
near the 1.9× this constant was originally sized at for Anvil. Affordable only because the slabs are
in PSRAM: +196,608 B would have taken the D-A1 board past no-panel by a factor of five; internal cost
is now zero.

**Before → after, on the board:** `live` toggling every 10–20 s with `resync_req` 1→4 in 80 s, and
`oversize` climbing → **`live=1` held 60 s–140 s, `oversize=0`, `resync_req` flat at 3, seed line
static**. Fetch round trips **3,143–4,485 ms**, body 64,046 B every time.

**Not done, and owed.** `worst_frame` reads **99,597 us** — up from 4,297 at D-A1 and unexplained; it
is not the fetch (that is a different task now) and it wants rooting out before the soak. `no_slot=17`
over 1,286 messages. The `dc_engine_header_check` target has raced twice on a parallel build, failing
on the alphabetically-first generated file and passing on retry — benign but real. And **D-C now has a
sharper check than the one D-A1 handed it**: the fetch takes the largest block to ~4 KB, so
*“below 16,717 B”* is not a tripwire at the fetch, it is the normal state — the reading that matters
is at the **reconnect**, and the rule is what keeps the two apart.

**Exact next step.** **D-B**: the four rendering decisions, now that there is a live ladder to judge —
and it inherits none of the `kFrameCapacity` question, which is closed. Then **D-A3** (the re-seed
mechanism, the liveness ping wire) and **D-C** (the soak).
