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

☐ Two traces committed, including the reconnect trace.
☐ `docs/vendor/anvil-protocol.md` pinned with version + date.
☐ `NOTES.md` answers both known unknowns.
☐ `ctest` green from a clean clone on one owner box, two commands max.
☐ Session log below filled in; ROADMAP M0 ticked.

## Out of scope

Book maintenance, adapter event emission, rendering of any kind, firmware, Kraken/Binance.

## Session log

<!-- Append one block per session: date · model · what was done · decisions made
     (with why) · what the next session should start with. -->
