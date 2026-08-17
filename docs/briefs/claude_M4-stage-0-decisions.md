# M4 stage 0 — decisions taken, and what M4 inherits

*2026-08-16, from the planning seat. Stage 0 executed and green; nothing committed.*

*Corrected 2026-08-16 (late): item 3 below is **superseded in place**, per the §9 convention —
struck where it stands, with the correction beside it, because this file is cross-referenced as
the source of truth for the four M4 items and a stale line here is worse than a stale line
anywhere else.*

## Decisions

**(a) Depth 25. `kDisplayLevels` stays 27. §5 untouched.** Agreed as recommended, with one
addition to the rationale worth writing down: the two unfilled rows a side are not a cosmetic
cost, they are §4's *"depth beyond N is unknown, not zero"* rendered. A panel that padded 25
levels into 27 rows would be inventing book. Ladder height differing by venue at M7 is
therefore the correct behaviour, not a wart to reconcile — and it is cheaper to defend now, in
a §9 row, than to rediscover at M7 when someone tries to make the two venues look alike.

**(b) Strain 3 → option (ii), `static_assert`s on the sink signature. (i) re-opens when the
toolchain moves for its own reasons.** Agreed, and the compile result strengthens the case
rather than weakening it. The hazard was never "it will not compile" — it is that **both**
would compile and mean different things: the host builds C++20 concepts, the target builds
Concepts TS behind `-fconcepts`, and `engine/` acquires two dialects with one spelling. That
is a worse failure than convention, because it type-checks. Invariant #1's "same translation
units on host and target" is the thing that would quietly stop being true.

## Two §9 rows, drafted

Same table shape as the existing entries — date · the rule/decision · the evidence.

> **2026-08-16 (stage 0)** — **A venue's silence is not one measurement, and the clock must be
> stamped by the thing whose absence you actually care about.** DepthCharge now has three
> distinct clocks where it believed it had one: byte arrival, data-frame arrival, and
> **book-event arrival**. At Anvil the three coincide, because a timer broadcast means bytes
> stop when the book stops — which is why `kRxWatchdogMs = 1000` survived two re-measurements.
> At Kraken they do not, and the divergence is 9×. **This is the same rule §9 recorded on
> 2026-08-16 for D5** — the silence recycle moved off byte arrival because our own pongs
> manufactured bytes over a dead publication — generalised one notch: Kraken's heartbeat is an
> *application* frame, so it stamps `last_data_us` too, and the split D5 made is not deep
> enough for an event-driven venue. **Evidence:** worst book silence 4,535 ms (BTC/USD) and
> 9,007 ms (quiet pair, p90 8,480 — its normal condition), against a 1 Hz heartbeat flooring
> byte silence at 998–1,026 ms in every capture. **No constant changed at stage 0.** Which
> clock drives invariant #5's grey at an event-driven venue is an M4 decision and is
> §4-adjacent; it is not a threshold tweak.

> **2026-08-16 (stage 0)** — **Strain 3 resolves as `static_assert`s, not a `concept`; and the
> 2026-08-07 subset row is accurate about the header and incomplete about the language
> feature.** Measured, not argued: a C++20 `concept` costs one flag (`-fconcepts`) on xtensa
> GCC 8.4 — 9/9 `engine/` headers compile and the object file is byte-identical — so the
> pioarduino migration everyone assumed was the price is not the price. `<concepts>` the
> header is genuinely absent, as 2026-08-07 says. **The decision is (ii) anyway**, because
> GCC 8.4's `-fconcepts` is the Concepts TS and not C++20: host and target would both compile
> the same spelling under different dialects, which is a silent divergence in the one property
> invariant #1 exists to protect. Option (i) re-opens if and when the toolchain moves for
> unrelated reasons. **The 2026-08-07 row stands as written** — per this table's rule, the
> reasoning is the valuable part. It is corrected here rather than edited there. **And the
> shape of its incompleteness is this project's own recurring one, now at instance six:** an
> inventory taken by one method (does this header exist?) got read as answering another (is
> this feature available?).

## What M4's brief must now carry

1. **The subscription ack is load-bearing, and failing it is fatal, not a `Gap`.** A refused
   `depth` leaves a live socket, a `status` frame, and 1 Hz heartbeats over a permanently empty
   book. `Gap` means *the book is unknown until the next snapshot*; this is *no snapshot is
   ever coming*, and rendering it as honest grey forever is honest about the wrong thing. The
   adapter checks `success` and treats false as a configuration error with its own reported
   state. **Nothing in the current vocabulary says "this feed will never start" — check whether
   that needs saying before writing code that needs it.**
2. **The three clocks, decided explicitly.** Not a new value for `kRxWatchdogMs`. The question
   is which arrival stamps the clock that greys the panel, per the §9 row above, and whether
   the answer is per-venue — in which case it belongs beside the venue's other declared
   metadata, not in a firmware constant.
3. ~~**The depth-10 slice is the truncation golden, and say so in its header.** Truncation not
   implemented is invisible at depth 100 over a short trace (4,435/4,435) and fails fast at
   depth 10 (379/1,412). The committed trace that exposes the defect quickest is worth more
   than the one that looks most like production.~~

   **SUPERSEDED 2026-08-16 (late) — the conclusion held, the stated reason did not, and the
   reason is the part a later session would have reused.** It is **two** slices, not one (d10
   and d25 on the liquid pair), and **depth is not the detecting property**: d25 catches on
   BTC/USD and misses on the quiet pair. Eviction rate was the next candidate and is
   *anti*-correlated with detection (BTC d100 evicts 467.6/1k and misses; MINA d25 evicts
   633.3/1k and misses) — necessary, not sufficient. A third candidate, evicted levels
   returning to the checksummed top 10, fitted all four slices and was falsified inside a
   minute by the reconnect capture. **The criterion is the direct measurement**
   (`ok_never_truncating < checksummed`, printed per trace as CATCHES/MISSES and pinned), and
   **a trace containing a reconnect is a worse golden, not a richer one** — `Book.replace`
   clears every level, so a resync repairs a book that never truncated. Full working in the
   stage-0 session log, `harness/replay/NOTES-kraken.md`, the pin table and the ROADMAP.
4. **The strain-20 criterion, and stage 0 has already found it.** M4's DoD needs one bar the
   harness structurally cannot stage. It is the quiet pair on the panel: *the ladder holds its
   colour through a nine-second legitimate silence, and greys when the feed actually dies.* A
   replay trace can stage the silence; only the bench can stage both halves against the same
   firmware in one sitting.

## One process item

Both instrument bugs produced plausible numbers, which is the only reason they were dangerous.
The checksum work has handed over a free oracle — 8,677/8,677 from verbatim text, 0/2,786 from
float-parsed. **Pin those two figures as a known-answer self-check on the committed slices**,
so a future regression in the tooling fails loudly instead of reporting something believable.
The firmware instruments are host-tested to exactly this standard; the Python ones are not, and
that gap has now cost real time twice in one evening.

*(Done, and gone further: `--selfcheck` pins eight figures per slice, `ctest` runs it as
`kraken_tool_selfcheck`, three mutants are caught against a clean control, `--pin` refuses to
overwrite an existing row, and an unpinned trace fails rather than skips.)*
