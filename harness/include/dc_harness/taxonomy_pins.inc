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
