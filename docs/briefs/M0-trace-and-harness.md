# M0 — Trace + harness (ground truth before code)

**Track:** Agentic · **Status:** Not started
**Read first:** `/ARCHITECTURE.md` (constitution — invariants are binding), `ROADMAP.md`.

## Goal

Before any book logic exists, establish the objective ground truth every later session
converges on: real captured Anvil wire traffic, a replay harness that can feed it back
deterministically, and a green `ctest` target. After M0, every session's review cost is
"ctest green, read the diff".

## Deliverables

1. **Capture tool** — `tools/capture_anvil.py` (Python 3, `websockets`): connects to
   `wss://anvil.garethcooke.com/ws?ticker=<id>`, writes NDJSON — line 1 a metadata object
   (`{captured_at, url, ticker, tool_version}`), then one line per received frame as
   `{"rx_ns": <monotonic-ns>, "frame": <verbatim JSON>}`. Frames must be stored verbatim
   — no normalisation at capture time.
2. **Traces** — at least two, committed under `harness/replay/`:
   - `anvil_<ticker>_baseline.ndjson` — ≥ 5 minutes of steady streaming (feeder on).
   - `anvil_<ticker>_reconnect.ndjson` — a capture spanning at least one forced
     disconnect/reconnect (kill the network briefly), so snapshot-on-reconnect and any
     seq discontinuity behaviour is on record.
3. **Protocol snapshot** — copy Anvil's `PROTOCOL.md` (local checkout:
   `~/repos/Anvil/PROTOCOL.md` or per its README) to `docs/vendor/anvil-protocol.md`
   with a header line recording its version and copy date. Canonical source remains the
   Anvil repo; the vendored copy pins what this repo was built against.
4. **Observations** — `harness/replay/NOTES.md`: frame kinds actually seen
   (`snapshot` / `book` / `trade` / `summary` / other), their field shapes, seq behaviour
   across frames and across the reconnect, cadence measured from `rx_ns`, and any
   surprises vs the vendored protocol.
5. **Build + test skeleton** —
   - Top-level `CMakeLists.txt` (C++20, warnings-as-errors) with `engine/` as an
     initially header-only library target and `harness/` executables.
   - `engine/include/depthcharge/feed_event.hpp` — the contract types from
     ARCHITECTURE §4, verbatim in intent. No behaviour yet.
   - `dc_replay` — reads an NDJSON trace, validates line structure, prints per-kind frame
     counts and cadence stats. No book, no adapter logic yet.
   - `dc_tests` (doctest, wired to ctest) — first goldens: trace files parse, frame
     counts match pinned expectations, metadata line present.
   - A `host` CMake preset so the loop is `cmake --preset host && ctest --preset host`
     (or equivalent two commands documented in the root README).

## Constraints

- All ARCHITECTURE invariants apply; note esp. #1 (no ESP-IDF in `engine/`) and #3
  (integer ticks — relevant only to `feed_event.hpp` at this stage).
- Python is allowed in `tools/` only.
- Harness-side JSON parser is the session's choice (nlohmann acceptable here); record
  the choice and rationale in the session log. Do **not** pick the firmware parser yet.
- Must build on at least one of the owner's boxes (Windows MinGW-w64 GCC ≥13 or Ubuntu
  GCC 13) with no exotic dependencies; aim for both, verify one.

## Known unknowns (resolve and record in NOTES.md)

- Whether the deployed WS upgrade accepts a client sending **no `Origin` header**
  (non-browser clients don't send one; the server enforces an origin allowlist on
  upgrade). If rejected, add a nominated `Origin` header to the capture tool and note
  it — the firmware will need the same header.
- Whether `book` frames and `snapshot` frames are distinct shapes or the same shape in
  different roles; whether `summary` frames carry a `seq`.

## Definition of done

☑ Two traces committed, including the reconnect trace.
☑ `docs/vendor/anvil-protocol.md` pinned with version + date.
☑ `NOTES.md` answers both known unknowns.
☑ `ctest` green from a clean clone (`cmake --workflow --preset host`) on Ubuntu
  GCC 13.3 — 3 ctest / 8 doctest cases / 52 assertions.
☑ Session log below filled in; ROADMAP M0 ticked.

## Out of scope

Book maintenance, adapter event emission, rendering of any kind, firmware, Kraken/Binance.

## Session log

<!-- Append one block per session: date · model · what was done · decisions made
     (with why) · what the next session should start with. -->

### 2026-07-23 · Opus 4.8 (1M) · M0 implemented (green in-tree; hand-off commit pending)

**Done.** All five deliverables built against the **live** Anvil server (it was up,
feeder on, 12 tickers 101–112). Capture tool + slicer in `tools/`; two committed
traces sliced from full local captures; vendored protocol; `NOTES.md`; CMake +
`host` preset + `feed_event.hpp` + `dc_replay` + `dc_tests`. `cmake --preset host
&& ctest --preset host` → 3 ctest / 8 doctest cases / 52 assertions, all green,
warnings-as-errors clean (GCC 13.3).

**Decisions (with why):**

- **Capture tool is stdlib-only RFC 6455**, not the `websockets` package the brief
  named. *Why:* this box has no `pip`/`ensurepip`, and a stdlib client is strictly
  more portable ("no exotic dependencies") — and it let me capture real traces now.
  It sends **no `Origin`** by default and auto-retries with `https://<host>` only if
  rejected.
- **Harness JSON parser = nlohmann/json 3.11.3** (vendored, `SYSTEM` include,
  compiled in one TU `trace.cpp`). doctest 2.4.11 reused from the Anvil checkout.
  *Why:* brief blesses nlohmann for the harness; single-header vendoring is
  offline-reproducible; `SYSTEM` keeps `-Werror` off third-party. **Harness-only —
  it must never touch `engine/` or the firmware hot path (invariant #7).** The
  firmware parser is still undecided, as the brief requires.
- **Committed-trace policy (deviates from brief's "≥5 min committed").** Book frames
  are ~8 KB at ~12/s ≈ 100 KB/s, so a literal 5-min baseline is ~30 MB in git
  forever. Per owner direction: run the full 5-min capture but keep it **local &
  git-ignored** (`harness/replay/_local/`, `*.full.ndjson`); commit only a **90 s
  slice** (`tools/slice_trace.py`). Committed sizes: baseline 1406 frames /
  8.75 MiB raw / **182 KiB gzip**; reconnect 1288 frames / 8.00 MiB / **168 KiB
  gzip**. gzip ≈ git-storage proxy → ~350 KiB total. **This sets the trace policy
  for Kraken/Binance at M4.** `NOTES.md` observations are drawn from the full 5-min
  window (and say so).
- **Reconnect trace** = client-initiated reconnect with a **4 s simulated drop**
  (`--reconnect-gap`), sliced to 30 s-before + gap + 60 s-after the resync. *Why:*
  deterministic, records snapshot-on-reconnect + seq behaviour without killing the
  host network; the gap is visible in `rx_ns` (4.47 s).
- **Build loop is `cmake --workflow --preset host`** (one command; needs CMake ≥3.25).
  *Why:* the brief's literal `cmake --preset host && ctest --preset host` **skips the
  build** — from a clean clone the tests are "Not Run" (verified). A workflow preset
  does configure→build→test in one command; individual configure/build/test presets
  also exist. Updated README and CLAUDE.md's loop line to match.

**Known-unknowns — resolved (see NOTES.md):** (1) WS upgrade **accepts no `Origin`
header** → firmware won't need one (keep the nominated-`Origin` fallback ready).
(2) `book` and `snapshot` are the **same shape** (full top-N replace); `summary`
**carries a `seq`**.

**⚠ ARCHITECTURE correction needed (flagged for owner — constitution NOT edited).**
The headline M0 finding: Anvil's wire `seq` is a single **global** counter and is
**non-monotonic** in one ticker's received stream (42 backward steps over 5 min:
`summary→book`, `trade→book` from cross-ticker broadcast + book coalescing), and it
**continues across reconnect** (no per-connection reset). This contradicts:
- **§1** "Anvil … every frame carrying a monotonic `seq`" — inaccurate.
- **§4** listing "Anvil's frame `seq`" as a usable native scheme to normalise.
Suggested §4 edit: state that the Anvil adapter **synthesises its own monotonic
`Seq`** (safe because `snapshot`/`book` are idempotent full replaces); the wire
`seq` is unusable for gap detection; `Gap{Disconnect}` is synthesised transport-side
(the server emits no `error`/gap frames). Vendored `docs/vendor/anvil-protocol.md`
has a header note pointing at this.

**Review.** Ran an adversarial multi-agent review (16 agents; 5 dimensions +
per-finding verify). 9 confirmed / 2 refuted; **all 9 fixed**: SIGINT responsiveness
in the capture recv loop; Origin-retry socket close; slicer non-dict-frame guard;
**engine header now compiled under `-Werror` in `dc_tests`** so its `static_assert`
guards actually run in ctest (previously dormant); `host-mingw` preset added;
NOTES depth band corrected to ~84–126 levels/side.

**Remaining to fully close M0:** the **hand-off commit** (awaiting owner go-ahead —
"commit only when the user asks"). On commit, DoD boxes 1 & 4 (traces committed;
clean-clone ctest) hold.

**Next session — M1 (console ladder off replay).** `engine/`: implement the Anvil
adapter (frame JSON → `FeedEvent`s) + phase-1 book (adopt latest `snapshot`/`book`,
trade ring) + console ladder; goldens off the committed replays. Firm constraints
from M0: **(a)** synthesise a monotonic `FeedEvent.Seq` locally — do **not** trust
the wire seq; **(b)** parse Anvil's decimal-string prices (`"10.012"`) → integer
ticks (pick/record a per-symbol `tick_size`; no float in book data); **(c)** ignore
but tolerate `summary` frames; **(d)** synthesise `Gap{Disconnect}` from the
transport, keyed off the mid-stream snapshot / socket close. Decide the firmware
JSON parser separately (not nlohmann).
