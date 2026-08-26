// taxonomy_pins.inc — the pinned record taxonomy of every committed trace.
//
// Included once, into kKnownTaxonomy in harness/src/dc_taxonomy_main.cpp. It is
// its own file so that a pin change is a one-file diff a reviewer can read
// without scrolling past the program that produced it.
//
// ADD ROWS. NEVER REGENERATE THE TABLE. `dc_taxonomy --pin` refuses a trace
// that already has a row; the reasoning is in dc_taxonomy_main.cpp's header and
// it is load-bearing, not ceremony.
//
// A row that moves means one of exactly two things, and both want a human:
//   * the reader or a decoder changed what a committed file means — the alarm
//     this table exists to raise; or
//   * a trace was re-captured — then the row moves WITH it, in the same commit,
//     and the figures in harness/replay/NOTES-kraken.md move too.
// Never "adjust the pin until it passes".
//
// Measured 2026-08-17 (M4 stage A). The four Anvil rows were cross-checked
// against pins that already existed and were derived independently at M0:
// dc_tests.cpp asserts 1406 records / book 1088 / snapshot 1 / trade 136 /
// summary 181 on the baseline and 1288 / 1012 / 1 / 103 / 172 on the reconnect,
// and both agree with these rows exactly. The four Kraken rows were
// cross-checked against a Python pass over the same files written for the
// purpose — same kind names, same counts, same watchdog firings.

// --- Anvil ------------------------------------------------------------------
// `untyped` is 0 at this venue by construction: TraceReader still requires a
// string `type` of every Anvil frame, and a trace that lost one still fails.
// wd_declared == wd_anvil because Anvil's declared threshold IS 1000 ms — the
// column is not redundant, it is the statement that the venue table did not
// move Anvil's number. The reconnect trace's single firing is the ~4.5 s
// simulated drop the capture was taken for.
    {"anvil_101_baseline.ndjson", "anvil",
     /*records=*/1406, /*tx=*/0, /*untyped=*/0, /*book_events=*/1225,
     /*snapshots=*/1, /*resyncs=*/0,
     /*wd_declared=*/0, /*wd_anvil=*/0, /*book_wd_declared=*/0, /*book_wd_anvil=*/0,
     /*liveness_events=*/181, /*liveness_firings=*/0,
     "book=1088 snapshot=1 summary=181 trade=136"},
    {"anvil_101_baseline_20260809.ndjson", "anvil",
     /*records=*/1513, /*tx=*/0, /*untyped=*/0, /*book_events=*/1332,
     /*snapshots=*/1, /*resyncs=*/0,
     /*wd_declared=*/0, /*wd_anvil=*/0, /*book_wd_declared=*/0, /*book_wd_anvil=*/0,
     /*liveness_events=*/181, /*liveness_firings=*/0,
     "book=1210 snapshot=1 summary=181 trade=121"},
    {"anvil_101_depth27_20260816.ndjson", "anvil",
     /*records=*/1399, /*tx=*/0, /*untyped=*/0, /*book_events=*/1218,
     /*snapshots=*/1, /*resyncs=*/0,
     /*wd_declared=*/0, /*wd_anvil=*/0, /*book_wd_declared=*/0, /*book_wd_anvil=*/0,
     /*liveness_events=*/181, /*liveness_firings=*/0,
     "book=1075 snapshot=1 summary=181 trade=142"},
    {"anvil_101_reconnect.ndjson", "anvil",
     /*records=*/1288, /*tx=*/0, /*untyped=*/0, /*book_events=*/1116,
     /*snapshots=*/1, /*resyncs=*/1,
     /*wd_declared=*/1, /*wd_anvil=*/1, /*book_wd_declared=*/1, /*book_wd_anvil=*/1,
     /*liveness_events=*/172, /*liveness_firings=*/1,
     "book=1012 snapshot=1 summary=172 trade=103"},
// Added 2026-08-17. The first capture of an IDLE Anvil — local server, feeder
// never started, three tickers, empty books throughout — and the only committed
// trace in which no order ever exists. `trade=0` is why the kinds string has
// three entries where every other Anvil row has four.
//
// It is the file the 2026-08-17 ruling's point 2 rests on: `summary` at
// **2.008/s** with a p50 of exactly 500.0 ms on a queue that never had anything
// in it. Read the row beside `NOTES.md`'s idle-emission section before treating
// `book=1511` as noise — 1,511 book frames over an empty book, byte-identical
// apart from `seq`, is the measurement that says a genuinely idle Anvil book
// does NOT produce no frames.
    {"anvil_101_feederoff_20260817.ndjson", "anvil",
     /*records=*/1753, /*tx=*/0, /*untyped=*/0, /*book_events=*/1512,
     /*snapshots=*/1, /*resyncs=*/0,
     /*wd_declared=*/0, /*wd_anvil=*/0, /*book_wd_declared=*/0, /*book_wd_anvil=*/0,
     /*liveness_events=*/241, /*liveness_firings=*/0,
     "book=1511 snapshot=1 summary=241"},

// --- Kraken -----------------------------------------------------------------
// `untyped` is the number M4 stage A exists for: 61 of the depth-25 slice's
// 1,599 records carry no string `type`, and every one is accounted for —
// 59 heartbeats + the subscribe ack + the subscribe this side sent. The other
// three slices read 62 because they caught 60 heartbeats in their 60 s window
// rather than 59.
//
// wd_anvil is deliverable 4's measurement and the reason the threshold is now
// venue-declared. Every one of those firings is spurious: on the quiet pair the
// M1 rule invents 25 disconnects in 60 s from a market that was working
// perfectly, and the book rule invents 12 more. At the declared 15,000 ms both
// are 0 on all four slices. Note d100's `wd_anvil=0` against d10's 7 and the
// quiet pair's 25 — the record-arrival rule is sitting on the 1 Hz heartbeat's
// own noise floor (worst record gaps 998-1,026 ms), so whether it fires is
// jitter rather than signal. That is the "straddles the constant" finding from
// stage 0, arriving as four numbers instead of a sentence.
    {"kraken_btcusd_d10_20260816.ndjson", "kraken",
     /*records=*/902, /*tx=*/1, /*untyped=*/62, /*book_events=*/839,
     /*snapshots=*/1, /*resyncs=*/0,
     /*wd_declared=*/0, /*wd_anvil=*/7, /*book_wd_declared=*/0, /*book_wd_anvil=*/13,
     /*liveness_events=*/60, /*liveness_firings=*/0,
     "ack:subscribe=1 book/snapshot=1 book/update=838 heartbeat=60 status/update=1 tx:subscribe=1"},
    {"kraken_btcusd_d25_20260816.ndjson", "kraken",
     /*records=*/1599, /*tx=*/1, /*untyped=*/61, /*book_events=*/1537,
     /*snapshots=*/1, /*resyncs=*/0,
     /*wd_declared=*/0, /*wd_anvil=*/5, /*book_wd_declared=*/0, /*book_wd_anvil=*/10,
     /*liveness_events=*/59, /*liveness_firings=*/0,
     "ack:subscribe=1 book/snapshot=1 book/update=1536 heartbeat=59 status/update=1 tx:subscribe=1"},
    {"kraken_btcusd_d100_20260816.ndjson", "kraken",
     /*records=*/2535, /*tx=*/1, /*untyped=*/62, /*book_events=*/2472,
     /*snapshots=*/1, /*resyncs=*/0,
     /*wd_declared=*/0, /*wd_anvil=*/0, /*book_wd_declared=*/0, /*book_wd_anvil=*/3,
     /*liveness_events=*/60, /*liveness_firings=*/0,
     "ack:subscribe=1 book/snapshot=1 book/update=2471 heartbeat=60 status/update=1 tx:subscribe=1"},
    {"kraken_minagbp_d25_20260816.ndjson", "kraken",
     /*records=*/93, /*tx=*/1, /*untyped=*/62, /*book_events=*/30,
     /*snapshots=*/1, /*resyncs=*/0,
     /*wd_declared=*/0, /*wd_anvil=*/25, /*book_wd_declared=*/0, /*book_wd_anvil=*/12,
     /*liveness_events=*/60, /*liveness_firings=*/0,
     "ack:subscribe=1 book/snapshot=1 book/update=29 heartbeat=60 status/update=1 tx:subscribe=1"},
// Added 2026-08-17. THE EXTREME, and the golden the staleness ruling exists to
// serve: a **25,843.3 ms** healthy book silence with the heartbeat keeping
// 936-1,042 ms time straight through it. The row above covers the same case at
// 9.0 s; this one is not redundant with it and neither replaces the other (see
// NOTES-kraken.md, "Two quiet-pair traces, two jobs").
//
// Read `book_wd_declared=3` beside `liveness_firings=0`. That pair IS the ruling:
// the WITHDRAWN 15,000 ms book-silence constant would have invented three
// disconnects in this 95 s window on a feed that never lost a packet, and the
// liveness rule invents none. `wd_anvil=36` is the same point at 1,000 ms.
//
// Two properties of this slice that are deliberate and must not be read as
// defects. `tx=0` and `snapshots=0`: the window starts 65 s into a 600 s capture,
// so it begins MID-STREAM, after the subscribe and after the on-connect
// snapshot. That makes it a LIVENESS golden and NOT a book golden — a stage-B
// adapter replaying it starts against an unknown book, which is correct
// behaviour and is why the truncation goldens are separate files.
    {"kraken_minagbp_d25_20260817.ndjson", "kraken",
     /*records=*/144, /*tx=*/0, /*untyped=*/95, /*book_events=*/49,
     /*snapshots=*/0, /*resyncs=*/0,
     /*wd_declared=*/0, /*wd_anvil=*/36, /*book_wd_declared=*/3, /*book_wd_anvil=*/10,
     /*liveness_events=*/95, /*liveness_firings=*/0,
     "book/update=49 heartbeat=95"},
// Added 2026-08-18 (M4 stage B2). THE RESYNC SLICE, and the first committed
// trace at any venue in which this client SPEAKS TWICE: `tx=3` is a subscribe,
// an unsubscribe and a second subscribe, provoked deliberately mid-capture by
// `capture_kraken.py --resubscribe-after` on ONE connection.
//
// `snapshots=2, resyncs=1` is the whole row. The venue serves a second
// `book/snapshot` mid-stream and — measured on the wire the same evening — it
// is byte-shape IDENTICAL to the on-connect one: same channel, same type, same
// `data[0]` keys, no marker of any kind. So the resync is detectable ONLY by
// position, which is exactly what the venue-free predicate uses, and a
// Kraken-specific branch would have had nothing to branch on.
//
// `ack:unsubscribe=1` is new to this table and it found a defect: the adapter's
// parser filed every `method` frame as a subscribe ack, because until this
// capture existed no trace at any venue contained an unsubscribe. See
// kraken_frame.hpp's FrameKind::UnsubscribeAck.
//
// The quiet pair, deliberately: a resync needs a wall-clock window long enough
// for both subscriptions to calibrate their heartbeat clocks, and only a slow
// pair gives 72 s of that in 14 KiB. The busy equivalent
// (BTC/USD d25, 2,661 frames in 46 s) was captured the same evening, ran the
// same path, and is 636 KiB — kept in `_local/`, quoted in NOTES-kraken.md.
    {"kraken_minagbp_d25_resync_20260818.ndjson", "kraken",
     /*records=*/99, /*tx=*/3, /*untyped=*/79, /*book_events=*/19,
     /*snapshots=*/2, /*resyncs=*/1,
     /*wd_declared=*/0, /*wd_anvil=*/32, /*book_wd_declared=*/2, /*book_wd_anvil=*/7,
     /*liveness_events=*/73, /*liveness_firings=*/0,
     "ack:subscribe=2 ack:unsubscribe=1 book/snapshot=2 book/update=17 heartbeat=73 "
     "status/update=1 tx:subscribe=2 tx:unsubscribe=1"},

// --- Binance (M5 stage A) ---------------------------------------------------
// Measured 2026-08-25 by this program, over the seven committed slices, after
// --selfcheck had shown all eleven pre-existing rows unchanged. That order is
// the procedure in dc_taxonomy_main.cpp's header and it is what makes these
// rows trustworthy: they came out of a reader that had just been proved not to
// have moved what the other files mean.
//
// FOUR COLUMNS READ DIFFERENTLY AT THIS VENUE, and each is a fact about the
// venue rather than a quirk of the table:
//
//   untyped     counts FRAME-BEARING records with no string `type`. Binance
//               sends none anywhere, so it is `records` minus the control
//               records, which have no frame to be untyped about.
//   snapshots   is the REST fetch count and nothing else. `@depth20` replaces
//               the top 20 outright and re-baselines nothing below it, and the
//               venue publishes ten a second — counting those would report 151
//               snapshots on a fifteen-second slice. `resyncs` is therefore the
//               scheduled re-seed, which at this venue is the design and not an
//               incident.
//   wd_declared is 0 because this venue NEVER DECLARED a record-arrival
//               constant: it arrived after the 2026-08-17 ruling withdrew the
//               other two, so venue.hpp holds -1 and the column is not
//               computed. It is not "zero firings".
//   wd_anvil    is large and that is the finding, not a defect. Record arrival
//               is not this venue's liveness clock: the quiet pair went 10.5 s
//               between records on a socket whose pings never missed a beat.
//
// `ping=N` in the kinds string is the 2026-08-25 ruling's signal, counted in a
// committed file for the first time — 17 across the seven slices. The three
// with the longest windows measure its cadence independently of stage 0's
// figure: medians of 19,951.7 / 20,011.6 / 20,013.3 ms against the 19,970 ms
// NOTES-binance.md recorded.
//
// **THOSE THREE FIGURES ARE THE INTERPOLATED MEDIAN, NOT THE ONE THE SHIPPED
// CLOCK COMPUTES, AND THEY ARE DELIBERATELY LEFT STANDING (M5 stage B2).**
// `sample_window.hpp` owns the convention -- a LOWER median by nearest rank,
// because an interpolated one invents an interval that never occurred on the
// wire -- and `LivenessClock` and `AgeEstimator` use it. `harness/src/trace.cpp`
// carries a second copy and interpolates, which is where these came from. The
// clock's answers for the same three files are 19,947.7 / 19,962.8 / 19,973.9.
// They agree to 0.1 ms at Anvil and Kraken, whose cadences are flat, which is
// why the divergence survived three milestones -- the coincidence class again.
//
// CORRECTING THEM HERE IS EXACTLY WHAT IS BEING DEFERRED. A convention change
// rewrites figures inside this file, and *no existing golden moves* is what
// makes a split reviewable; bundled into another stage, nothing in the diff
// would distinguish a convention change from a defect. So it must be its own
// stage, where the moved figures have nothing else to hide behind.
//
// OWNER: **M5 close-out**. EXPIRY: when `harness/` and `engine/` compute the
// median by one convention. TRIPWIRE: if any stage before the close-out needs to
// quote or re-pin a Binance cadence figure, this closes first. The pinned
// COLUMNS below are unaffected either way -- no median is a pinned column, and
// `liveness_firings` is computed from the clock's own threshold, not from these.
// Carried in three places: here, `test_binance_adapter.cpp`'s two-conventions
// case (**which must be INVERTED, not deleted or relaxed**), and DESIGN strain
// 29. If the close-out ships without inverting it, the clause moves to whichever
// stage next touches a cadence figure rather than lapsing. `liveness_firings` is 0 everywhere, and see
// venue.hpp for why that is weaker evidence than it looks: no slice is long
// enough to calibrate, so all seven ran on the uncalibrated 30,000 ms default.
    {"binance_atomeur_d100ms_20260824.ndjson", "binance",
     /*records=*/69, /*tx=*/0, /*untyped=*/64, /*book_events=*/64,
     /*snapshots=*/5, /*resyncs=*/4,
     /*wd_declared=*/0, /*wd_anvil=*/14, /*book_wd_declared=*/0, /*book_wd_anvil=*/14,
     /*liveness_events=*/5, /*liveness_firings=*/0,
     "depthUpdate=30 partialDepth=29 ping=5 rest=5"},
    {"binance_atomeur_deepseed_20260824.ndjson", "binance",
     /*records=*/23, /*tx=*/0, /*untyped=*/18, /*book_events=*/18,
     /*snapshots=*/2, /*resyncs=*/1,
     /*wd_declared=*/0, /*wd_anvil=*/3, /*book_wd_declared=*/0, /*book_wd_anvil=*/2,
     /*liveness_events=*/5, /*liveness_firings=*/0,
     "depthUpdate=8 partialDepth=8 ping=5 rest=2"},
    {"binance_btcusdt_d1000ms_20260824.ndjson", "binance",
     /*records=*/131, /*tx=*/0, /*untyped=*/128, /*book_events=*/128,
     /*snapshots=*/6, /*resyncs=*/6,
     /*wd_declared=*/0, /*wd_anvil=*/26, /*book_wd_declared=*/0, /*book_wd_anvil=*/26,
     /*liveness_events=*/3, /*liveness_firings=*/0,
     "depthUpdate=61 partialDepth=61 ping=3 rest=6"},
// The 15 s candidate-configuration slice, and the ONE committed Binance trace
// with no ping in it at all — expected at a 20 s cadence, and the reason
// `liveness_events=0` here is evidence of the window rather than of the venue.
    {"binance_btcusdt_d100ms_20260824.ndjson", "binance",
     /*records=*/305, /*tx=*/0, /*untyped=*/305, /*book_events=*/305,
     /*snapshots=*/4, /*resyncs=*/4,
     /*wd_declared=*/0, /*wd_anvil=*/0, /*book_wd_declared=*/0, /*book_wd_anvil=*/0,
     /*liveness_events=*/0, /*liveness_firings=*/0,
     "depthUpdate=150 partialDepth=151 rest=4"},
    {"binance_btcusdt_deepseed_20260824.ndjson", "binance",
     /*records=*/503, /*tx=*/0, /*untyped=*/502, /*book_events=*/502,
     /*snapshots=*/2, /*resyncs=*/2,
     /*wd_declared=*/0, /*wd_anvil=*/0, /*book_wd_declared=*/0, /*book_wd_anvil=*/0,
     /*liveness_events=*/1, /*liveness_firings=*/0,
     "depthUpdate=250 partialDepth=250 ping=1 rest=2"},
    {"binance_btcusdt_deepseed2_20260824.ndjson", "binance",
     /*records=*/502, /*tx=*/0, /*untyped=*/501, /*book_events=*/501,
     /*snapshots=*/1, /*resyncs=*/1,
     /*wd_declared=*/0, /*wd_anvil=*/0, /*book_wd_declared=*/0, /*book_wd_anvil=*/0,
     /*liveness_events=*/1, /*liveness_firings=*/0,
     "depthUpdate=250 partialDepth=250 ping=1 rest=1"},
// The single-stream capture (`/ws/btcusdt@depth@100ms`), which is why this is
// the only Binance row with no `partialDepth` — there is no audit stream in it.
// Its 5,401 ms record gap is the deliberate reconnect, and it is the only
// Binance slice whose `wd_anvil` of 1 is a real event rather than a quiet pair.
    {"binance_btcusdt_reconnect_20260824.ndjson", "binance",
     /*records=*/556, /*tx=*/0, /*untyped=*/554, /*book_events=*/554,
     /*snapshots=*/6, /*resyncs=*/6,
     /*wd_declared=*/0, /*wd_anvil=*/1, /*book_wd_declared=*/0, /*book_wd_anvil=*/1,
     /*liveness_events=*/2, /*liveness_firings=*/0,
     "depthUpdate=548 ping=2 rest=6"},

// --- THE DEFECT FIXTURE (M5 stage B1 follow-up, 2026-08-26) -----------------
// NOT A GOLDEN AND NOT A WITNESS. This row pins a capture in which the venue
// accepted the socket, answered our pongs, sent three pings on the normal
// cadence, served a successful REST seed from a different host, and delivered
// ZERO depth frames -- because the stream name in the URL path was misspelled by
// one character. Replayed, it produces a populated, coloured, LIVE ladder over a
// feed that has never spoken.
//
// The row is short and every number in it is the point:
//   records=4        one REST body and three pings. Nothing else arrived, ever.
//   book_events=1    the REST body. The diff stream contributed nothing.
//   snapshots=1      the seed. It is the ONLY thing that ever re-baselined.
//   liveness_events=3, liveness_firings=0
//                    THE LIVENESS CLOCK WAS FED NORMALLY THROUGHOUT, at a
//                    measured 20,004.8 ms median, and never fired. That is not
//                    a defect in the clock; it is the 2026-08-25 ruling working
//                    exactly as written on a signal that proves the socket and
//                    never the feed (ARCHITECTURE 9, 2026-08-25, both rows).
//   kinds "ping=3 rest=1"
//                    no `depthUpdate`, no `partialDepth`. A Binance kinds string
//                    with no book kind in it at all is what this file is for.
//
// **PINS A DEFECT, NOT A CONTRACT, AND IS EXPECTED TO INVERT.** See
// `test_binance_adapter.cpp`'s silent-stream case for the expiry clause: when C
// picks one of DESIGN strain 26's four remedies, the ladder stops being LIVE and
// this row's FIGURES stay exactly as they are -- the taxonomy counts records,
// not rendered state, so nothing here moves when the remedy lands. If it does
// move, something changed what this file MEANS and that wants a human.
    {"binance_btcusdt_DEFECT_silent_stream_20260826.ndjson", "binance",
     /*records=*/4, /*tx=*/0, /*untyped=*/1, /*book_events=*/1,
     /*snapshots=*/1, /*resyncs=*/0,
     /*wd_declared=*/0, /*wd_anvil=*/0, /*book_wd_declared=*/0, /*book_wd_anvil=*/0,
     /*liveness_events=*/3, /*liveness_firings=*/0,
     "ping=3 rest=1"},

// --- THE RULING'S EVIDENCE (M5 stage B2, 2026-08-26) ------------------------
// **THESE TWO ARE NOT SLICES, AND THE DISTINCTION IS THE ROW.** Every other
// Binance file above is a window cut out of a longer local capture, sized to the
// smallest stretch that exercises the property it pins. These are the complete
// 90 s captures the 2026-08-25 audit-stream ruling was signed on, committed
// byte-for-byte, and they are here because that ruling's clause said its figures
// were "the one set of numbers in this table with no committed trace behind
// them".
//
// A GUARD TRACE PINS A BEHAVIOUR and should be cut to the smallest window that
// exercises it -- M4 stage 0's capture recipe. A RULING'S EVIDENCE PINS A
// DECISION and cannot be cut at all, because the figure IS a rate over a
// complete capture: 90 partial payloads, 90 coinciding with a diff `u`, 88
// graded GREEN. Take 40 s out of the middle and the numbers that come back are
// 40/40 and 38/38, which evidence a different (weaker) claim than the one the
// ruling was taken on. Two categories, two slicing rules, and this is the first
// time the corpus has carried the second.
//
// SO: `records` here is the whole capture and must never shrink. If a future
// stage wants a mixed-cadence GUARD trace, it cuts one from `_local/` under a
// new name and pins it as a guard -- it does not re-cut these.
//
// What the columns say, and every one of them is the venue rather than a quirk:
//   records=997/999   the complete captures, 89.9 s and 90.0 s.
//   partialDepth=90   the 1000 ms audit tick over 90 s. **The first committed
//                     traces at that cadence**; every earlier Binance file runs
//                     `@depth20@100ms` and reports ~10x this. It is the
//                     configuration decision 2 put on the board.
//   depthUpdate=899/901   the diff stream is UNCHANGED at 100 ms, which is the
//                     other half of decision 2: the audit stream is 9.98x
//                     cheaper because it is the same payload at a tenth of the
//                     rate, not a smaller one.
//   snapshots=4       four `limit=1000` fetches at the tool's 25 s cadence. The
//                     first seeds; the other three are graded as re-snapshots.
//   ping=4            three intervals -- not enough to calibrate anything, which
//                     is why `liveness_firings=0` here is a fact about a 90 s
//                     window and not about the venue (see venue.hpp, and the
//                     ~200 s capture B2 took for the case that IS long enough).
//   wd_anvil=0        the busy pair never goes 1,000 ms between records.
    {"binance_btcusdt_mixed1_20260825.ndjson", "binance",
     /*records=*/997, /*tx=*/0, /*untyped=*/993, /*book_events=*/993,
     /*snapshots=*/4, /*resyncs=*/4,
     /*wd_declared=*/0, /*wd_anvil=*/0, /*book_wd_declared=*/0, /*book_wd_anvil=*/0,
     /*liveness_events=*/4, /*liveness_firings=*/0,
     "depthUpdate=899 partialDepth=90 ping=4 rest=4"},
    {"binance_btcusdt_mixed2_20260825.ndjson", "binance",
     /*records=*/999, /*tx=*/0, /*untyped=*/995, /*book_events=*/995,
     /*snapshots=*/4, /*resyncs=*/4,
     /*wd_declared=*/0, /*wd_anvil=*/0, /*book_wd_declared=*/0, /*book_wd_anvil=*/0,
     /*liveness_events=*/4, /*liveness_firings=*/0,
     "depthUpdate=901 partialDepth=90 ping=4 rest=4"},

// --- THE CALIBRATION CAPTURE (M5 stage B2, 2026-08-26) ----------------------
// **THE LENGTH IS THE PROPERTY.** Every other Binance file is 15-90 s, and
// `kMinSamples = 8` intervals at this venue's ~20 s ping cadence is ~160 s of
// wall clock -- so until this file existed the self-calibrating branch of
// `liveness_clock.hpp` was unreachable from the host suite, and C was being
// asked to choose between raising the ceiling, dropping the multiplier and
// making both per-venue against a path no test executed.
//
// `liveness_events=11` is therefore the row's whole point: ten intervals against
// kMinSamples' eight. `test_binance_adapter.cpp` asserts the calibrated flag on
// this file and asserts its ABSENCE on the 5-ping capture above, because a flag
// that is true everywhere proves nothing.
//
// DELIBERATELY NOT GRADEABLE, and that is a choice rather than a shortfall.
// Single-stream `/ws/atomeur@depth@100ms`, so no `@depth20` and nothing for the
// oracle to compare against -- same shape as the reconnect slice, pinned VACUOUS
// for the same reason. One opening REST seed at limit=100, `snapshot_every_s: 0`,
// weight 5 for the whole capture.
//
//   wd_anvil=37       the withdrawn 1,000 ms record-arrival rule inventing 37
//                     disconnects in 221 s on a market that never lost a packet.
//                     The quiet pair's case at four times the length of the
//                     earlier one, and the same verdict.
//   book_wd_anvil=37  a **26,807.8 ms** book silence with the ping keeping
//                     19,957-20,068 ms time straight through it. The Binance
//                     twin of Kraken's MINA/GBP 25,843 ms hole.
//   liveness_firings=0
//                     over 221 s, calibrated. Not the same statement as the
//                     zeros above it, which are all uncalibrated.
    {"binance_atomeur_d100ms_liveness_20260826.ndjson", "binance",
     /*records=*/95, /*tx=*/0, /*untyped=*/84, /*book_events=*/84,
     /*snapshots=*/1, /*resyncs=*/0,
     /*wd_declared=*/0, /*wd_anvil=*/37, /*book_wd_declared=*/0, /*book_wd_anvil=*/37,
     /*liveness_events=*/11, /*liveness_firings=*/0,
     "depthUpdate=83 ping=11 rest=1"},
