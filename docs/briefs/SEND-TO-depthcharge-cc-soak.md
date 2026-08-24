# SEND TO DEPTHCHARGE CC — preserve the soak, extract, then write up

**Do not write the session log yet. Task 0 first, and do not stop the board.**

25 hours is not re-runnable at a sensible price, the raw capture is the only copy of it, and the
session about to derive figures from it is also the session most likely to touch it.

---

## Task 0 — freeze the evidence, before anything reads it

1. **Copy the raw capture to a stable path and never edit it in place.** Record SHA-256, byte
   size, line count, and the first and last timestamps. Every figure from here derives from the
   copy, not from the live file.
2. **Keep the board running and keep capturing.** Open a *second* capture file rather than
   truncating or appending to the first. The fragmentation question needs more reconnect events
   than two, and the board is already producing them for free.
3. **Commit the raw log compressed**, under the existing evidence conventions, with its hash
   pinned. It is the evidence for M4's DoD and for a §9 row with architectural weight. If it
   exceeds ~10 MB compressed, commit a lossless derived event log plus the hash of the raw, keep
   the raw backed up outside the repo, and say in the log exactly what was kept and what was not.

## Task 1 — an extraction script, because the figures must be reproducible

The heap alarm was raised and retracted because a noisy series was read at its endpoints. Hand-read
numbers do not go into a §9 row.

- `tools/`, stdlib only, takes the frozen log and emits every figure in the report.
- **Reproduce the already-reported numbers as a check on the script**: 25.00 h captured, 25.39 h
  uptime, 194 grey episodes, 64.9 min grey, 192 heals at 2,080 ms median, 193 CRC failures,
  105 DNS resolution failures, the two outage durations, the 3-hour heap medians
  61,132 / 61,128 / 61,164, worst age 5.0 s, `refused=0`, `owed=0`.
- **If the script disagrees with the report on any figure, the script wins and the discrepancy is
  reported** — do not quietly adopt either number.

## Task 2 — three reads, and one of them could change the write-up

### (a) Are the 193 CRC failures clustered or uniform?

This is the one with teeth. B2 recorded the four-slot pipe overrunning on heal bursts — 10 drops,
all on snapshot bursts. If a drop corrupts the book, the checksum catches it, that triggers a
heal, the heal bursts, and the burst drops again, then 193 heals in 25 hours is that loop turning
over slowly rather than a venue artefact.

- Inter-arrival histogram of CRC failures across the whole run.
- **The criterion:** the probability that the next CRC failure falls within ten seconds of a
  completed heal, against the baseline rate implied by 193 events in 25 hours. Materially above
  baseline means the loop is real.
- Correlate failures with logged pipe drops and with heal completions.
- Report the answer plainly. "193 CRC failures, all healed" written up without this check reads
  benign and may be describing a self-sustaining loop that has not yet found a resonance.

### (b) Does the largest free block recover, or ratchet?

- Full time series, not endpoints. `47,092 → 49,140 at ~6 h → 31,732` is not monotonic, so it may
  relax between events.
- Correlate each step with reconnects *and separately* with TLS handshakes. The hypothesis worth
  testing: mbedTLS handshake buffers are the large short-lived allocations on that board, so a
  step tracking handshakes rather than reconnects generally would name the mechanism.
- **State the risk precisely, because it is narrower than it sounds:** `Panel::begin()` allocates
  at boot, so runtime fragmentation does not threaten it. The exposure is anything wanting a large
  contiguous block *during* a run — name what does, if anything.
- Two events cannot distinguish a ratchet from a sawtooth. Say so, and let the continuing capture
  answer it.

### (c) The DNS timing distribution

- Resolve durations for both successes and failures. **If every success also takes ~14,000 ms,
  the primary resolver is not answering and the stack is falling through to a secondary** — that
  is a specific dead server in what DHCP hands out, not general upstream flakiness.
- Report the resolver addresses if they appear in the log.
- Confirm from the log that `assoc=1` held throughout with zero Wi-Fi events, so the write-up can
  state the association never dropped rather than implying it.

## Task 3 — then the write-up

Session log, ROADMAP, DESIGN, and the §9 row. Two things the row must carry:

**The half-open win is conditional, and the condition is the interesting half.** The liveness
watchdog beat the transport by 141 s and 291 s *because Kraken sends unsolicited heartbeats*. At a
venue that speaks only when the book moves, the same code detects nothing. That is the 2026-08-17
ruling read forward, and it is a live constraint on Binance at M5 rather than a caveat.

**The cached-IP fallback is a backlog item with two guards, or it is a downgrade.** Kraken's
endpoint sits behind rotating infrastructure: cache with a TTL, always attempt fresh resolution
first and fall back only on failure, and **keep using the hostname for SNI and certificate
validation** — connecting to a cached address with the address as the name is how certificate
validation gets silently disabled. Price it, do not build it.

Also record, as an instance under the coincidence-class row rather than as a new finding: **a
two-point read of a noisy series is not a measurement.** The heap alarm was raised from endpoint
samples and retracted by medians.

---

## Constraints

- The board keeps running and keeps logging. Do not stop the capture to tidy anything.
- The frozen log is read-only. All figures derive from it via the script.
- No firmware changes, no fixes at all — the DNS fallback and the pipe overrun are both priced and
  parked.
- Code review before the split; per-commit verification in a detached worktree with
  `CMAKE_HOME_DIRECTORY` confirmed. **Commit nothing until the split is approved.**

## Definition of done

- [ ] Raw capture frozen, hashed, sized, timestamped; second capture file open; board still up.
- [ ] Extraction script committed; every reported figure reproduced from the frozen log, or the
      discrepancy reported.
- [ ] CRC clustering answered against the stated criterion, with the pipe-drop correlation.
- [ ] Largest-free-block series plotted or tabulated in full, steps attributed, risk stated
      narrowly, ratchet-versus-sawtooth left open with the reason.
- [ ] DNS distribution reported; primary-resolver hypothesis confirmed or refuted; `assoc=1`
      confirmed from the log.
- [ ] §9 row with the conditionality clause; cached-IP fallback priced with both guards; two-point
      read recorded as an instance.
- [ ] Green; split proposed; nothing committed.
